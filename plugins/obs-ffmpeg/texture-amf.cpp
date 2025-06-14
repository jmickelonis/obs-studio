/* AMF texture encoding based on work by nowrep:
   https://github.com/nowrep/obs-studio */

#include <util/threading.h>
#include <opts-parser.h>
#include <obs-module.h>
#include <obs-avc.h>

#include <unordered_map>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <vector>
#include <mutex>
#include <deque>
#include <map>
#include <array>
#include <inttypes.h>

#ifndef OBS_AMF_DISABLE_PROPERTIES
#include "amf-properties.h"
#endif

#include <AMF/components/VideoEncoderHEVC.h>
#include <AMF/components/VideoEncoderVCE.h>
#include <AMF/components/VideoEncoderAV1.h>
#include <AMF/core/Factory.h>
#include <AMF/core/Trace.h>

#ifdef _WIN32
#include <dxgi.h>
#include <d3d11.h>
#include <d3d11_1.h>

#include <util/windows/device-enum.h>
#include <util/windows/HRError.hpp>
#include <util/windows/ComPtr.hpp>
#endif

#ifdef __linux
#include <algorithm>
#include <GL/glcorearb.h>
#include <GL/glext.h>
#include <EGL/egl.h>
#include <vulkan/vulkan.h>
#include <AMF/core/VulkanAMF.h>
#endif

#include <util/platform.h>
#include <util/util.hpp>
#include <util/pipe.h>
#include <util/dstr.h>

using namespace amf;

/* ========================================================================= */
/* Junk                                                                      */

#define do_log(level, format, ...) \
	blog(level, "[%s: '%s'] " format, enc->encoder_str, obs_encoder_get_name(enc->encoder), ##__VA_ARGS__)

#define error(format, ...) do_log(LOG_ERROR, format, ##__VA_ARGS__)
#define warn(format, ...) do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define info(format, ...) do_log(LOG_INFO, format, ##__VA_ARGS__)
#define debug(format, ...) do_log(LOG_DEBUG, format, ##__VA_ARGS__)

struct amf_error {
	const char *str;
	AMF_RESULT res;

	inline amf_error(const char *str, AMF_RESULT res) : str(str), res(res) {}
};

#ifdef _WIN32
struct handle_tex {
	uint32_t handle;
	ComPtr<ID3D11Texture2D> tex;
	ComPtr<IDXGIKeyedMutex> km;
};
#elif defined(__linux__)

#define __USE_OPENCL true

#define VK_CHECK(f)                                                      \
	{                                                                \
		VkResult res = (f);                                      \
		if (res != VK_SUCCESS) {                                 \
			blog(LOG_ERROR, "Vulkan error: " __FILE__ ":%d", \
			     __LINE__);                                  \
			throw "Vulkan error";                            \
		}                                                        \
	}

static VkFormat to_vk_format(AMF_SURFACE_FORMAT fmt)
{
	switch (fmt) {
	case AMF_SURFACE_NV12:
		return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
	case AMF_SURFACE_P010:
		return VK_FORMAT_G16_B16R16_2PLANE_420_UNORM;
	default:
		throw "Unsupported AMF_SURFACE_FORMAT";
	}
}

static VkFormat to_vk_format(enum gs_color_format fmt)
{
	switch (fmt) {
	case GS_R8:
		return VK_FORMAT_R8_UNORM;
	case GS_R16:
		return VK_FORMAT_R16_UNORM;
	case GS_R8G8:
		return VK_FORMAT_R8G8_UNORM;
	case GS_RG16:
		return VK_FORMAT_R16G16_UNORM;
	default:
		throw "Unsupported gs_color_format";
	}
}

static GLenum to_gl_format(enum gs_color_format fmt)
{
	switch (fmt) {
	case GS_R8:
		return GL_R8;
	case GS_R16:
		return GL_R16;
	case GS_R8G8:
		return GL_RG8;
	case GS_RG16:
		return GL_RG16;
	default:
		throw "Unsupported gs_color_format";
	}
}

struct texture_info {
	GLuint glsem = 0;
	VkSemaphore sem = VK_NULL_HANDLE;
	VkFence fence = VK_NULL_HANDLE;
	struct {
		uint32_t width = 0;
		uint32_t height = 0;
		VkImage image = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		GLuint glmem = 0;
		GLuint gltex = 0;
		GLuint fbo = 0;
	} planes[2];
	GLuint sem_tex[2];
	GLenum sem_layout[2];
};

struct amf_tex {
	AMFVulkanSurface vulkanSurface;
	AMFSurface *surface;
	VkCommandBuffer copyCommandBuffer;
	int64_t ts;
};
typedef std::shared_ptr<amf_tex> amf_tex_p;

#endif

struct adapter_caps {
	bool is_amd = false;
	bool supports_avc = false;
	bool supports_hevc = false;
	bool supports_av1 = false;
};

/* ------------------------------------------------------------------------- */

static std::map<uint32_t, adapter_caps> caps;
static bool h264_supported = false;
static AMFFactory *amf_factory = nullptr;
static AMFTrace *amf_trace = nullptr;
static void *amf_module = nullptr;
static uint64_t amf_version = 0;

/* ================================================================================================================= */
/* The structure and tables below are used to determine the appropriate minimum encoding level for the codecs. AMF
 * defaults to the highest level for each codec (AVC, HEVC, AV1), and some client devices will reject playback if the
 * codec level is higher than its decode abilities.
 */

struct codec_level_entry {
	const char *level_str;
	uint64_t max_luma_sample_rate;
	uint64_t max_luma_picture_size;
	amf_int64 amf_level;
};

// Ensure the table entries are ordered from lowest to highest
static std::vector<codec_level_entry> avc_levels = {{"1", (uint64_t)1485 * 256, 99 * 256, AMF_H264_LEVEL__1},
						    {"1.1", (uint64_t)3000 * 256, 396 * 256, AMF_H264_LEVEL__1_1},
						    {"1.2", (uint64_t)6000 * 256, 396 * 256, AMF_H264_LEVEL__1_2},
						    {"1.3", (uint64_t)11880 * 256, 396 * 256, AMF_H264_LEVEL__1_3},
						    {"2", (uint64_t)11880 * 256, 396 * 256, AMF_H264_LEVEL__2},
						    {"2.1", (uint64_t)19800 * 256, 792 * 256, AMF_H264_LEVEL__2_1},
						    {"2.2", (uint64_t)20250 * 256, 1620 * 256, AMF_H264_LEVEL__2_2},
						    {"3", (uint64_t)40500 * 256, 1620 * 256, AMF_H264_LEVEL__3},
						    {"3.1", (uint64_t)108000 * 256, 3600 * 256, AMF_H264_LEVEL__3_1},
						    {"3.2", (uint64_t)216000 * 256, 5120 * 256, AMF_H264_LEVEL__3_2},
						    {"4", (uint64_t)245760 * 256, 8192 * 256, AMF_H264_LEVEL__4},
						    {"4.1", (uint64_t)245760 * 256, 8192 * 256, AMF_H264_LEVEL__4_1},
						    {"4.2", (uint64_t)522240 * 256, 8704 * 256, AMF_H264_LEVEL__4_2},
						    {"5", (uint64_t)589824 * 256, 22080 * 256, AMF_H264_LEVEL__5},
						    {"5.1", (uint64_t)983040 * 256, 36864 * 256, AMF_H264_LEVEL__5_1},
						    {"5.2", (uint64_t)2073600 * 256, 36864 * 256, AMF_H264_LEVEL__5_2},
						    {"6", (uint64_t)4177920 * 256, 139264 * 256, AMF_H264_LEVEL__6},
						    {"6.1", (uint64_t)8355840 * 256, 139264 * 256, AMF_H264_LEVEL__6_1},
						    {"6.2", (uint64_t)16711680 * 256, 139264 * 256,
						     AMF_H264_LEVEL__6_2}};

// Ensure the table entries are ordered from lowest to highest
static std::vector<codec_level_entry> hevc_levels = {
	{"1", 552960, 36864, AMF_LEVEL_1},           {"2", 3686400, 122880, AMF_LEVEL_2},
	{"2.1", 7372800, 245760, AMF_LEVEL_2_1},     {"3", 16588800, 552960, AMF_LEVEL_3},
	{"3.1", 33177600, 983040, AMF_LEVEL_3_1},    {"4", 66846720, 2228224, AMF_LEVEL_4},
	{"4.1", 133693440, 2228224, AMF_LEVEL_4_1},  {"5", 267386880, 8912896, AMF_LEVEL_5},
	{"5.1", 534773760, 8912896, AMF_LEVEL_5_1},  {"5.2", 1069547520, 8912896, AMF_LEVEL_5_2},
	{"6", 1069547520, 35651584, AMF_LEVEL_6},    {"6.1", 2139095040, 35651584, AMF_LEVEL_6_1},
	{"6.2", 4278190080, 35651584, AMF_LEVEL_6_2}};

/* Ensure the table entries are ordered from lowest to highest.
 *
 * The AV1 specification currently defines 14 levels, even though more are available (reserved) such as 4.3 and 7.0.
 *
 * AV1 defines MaxDisplayRate and MaxDecodeRate, which correspond to TotalDisplayLumaSampleRate and
 * TotalDecodedLumaSampleRate, respectively, defined in the specification. For the table below, MaxDecodeRate is being
 * used because it corresponds to all frames with show_existing_frame=0.
 *
 * Refer to the following for more information: https://github.com/AOMediaCodec/av1-spec/blob/master/annex.a.levels.md
 */
static std::vector<codec_level_entry> av1_levels = {
	{"2.0", (uint64_t)5529600, 147456, AMF_VIDEO_ENCODER_AV1_LEVEL_2_0},
	{"2.1", (uint64_t)10454400, 278784, AMF_VIDEO_ENCODER_AV1_LEVEL_2_1},
	{"3.0", (uint64_t)24969600, 665856, AMF_VIDEO_ENCODER_AV1_LEVEL_3_0},
	{"3.1", (uint64_t)39938400, 1065024, AMF_VIDEO_ENCODER_AV1_LEVEL_3_1},
	{"4.0", (uint64_t)77856768, 2359296, AMF_VIDEO_ENCODER_AV1_LEVEL_4_0},
	{"4.1", (uint64_t)155713536, 2359296, AMF_VIDEO_ENCODER_AV1_LEVEL_4_1},
	{"5.0", (uint64_t)273715200, 8912896, AMF_VIDEO_ENCODER_AV1_LEVEL_5_0},
	{"5.1", (uint64_t)547430400, 8912896, AMF_VIDEO_ENCODER_AV1_LEVEL_5_1},
	{"5.2", (uint64_t)1094860800, 8912896, AMF_VIDEO_ENCODER_AV1_LEVEL_5_2},
	{"5.3", (uint64_t)1176502272, 8912896, AMF_VIDEO_ENCODER_AV1_LEVEL_5_3},
	{"6.0", (uint64_t)1176502272, 35651584, AMF_VIDEO_ENCODER_AV1_LEVEL_6_0},
	{"6.1", (uint64_t)2189721600, 35651584, AMF_VIDEO_ENCODER_AV1_LEVEL_6_1},
	{"6.2", (uint64_t)4379443200, 35651584, AMF_VIDEO_ENCODER_AV1_LEVEL_6_2},
	{"6.3", (uint64_t)4706009088, 35651584, AMF_VIDEO_ENCODER_AV1_LEVEL_6_3}};

/* ========================================================================= */
/* Main Implementation                                                       */

enum class amf_codec_type {
	AVC,
	HEVC,
	AV1,
};

struct amf_base {
	obs_encoder_t *encoder;
	const char *encoder_str;
	amf_codec_type codec;
	bool fallback;
	const wchar_t *output_data_type_key;

	AMFContextPtr amf_context;
#ifdef __linux__
	AMFContext1Ptr amf_context1;
#endif
	AMFComponentPtr amf_encoder;
	AMFBufferPtr packet_data;
	AMFRate amf_frame_rate;
	AMFBufferPtr header;
	AMFSurfacePtr roi_map;

	std::deque<AMFDataPtr> queued_packets;

	AMF_VIDEO_CONVERTER_COLOR_PROFILE_ENUM amf_color_profile;
	AMF_COLOR_TRANSFER_CHARACTERISTIC_ENUM amf_characteristic;
	AMF_COLOR_PRIMARIES_ENUM amf_primaries;
	AMF_SURFACE_FORMAT amf_format;

	amf_int64 max_throughput = 0;
	amf_int64 requested_throughput = 0;
	amf_int64 throughput = 0;
	int64_t dts_offset = 0;
	uint32_t cx;
	uint32_t cy;
	uint32_t linesize = 0;
	uint32_t roi_increment = 0;
	int fps_num;
	int fps_den;
	bool full_range;
	bool bframes_supported = false;
	bool first_update = true;
	bool roi_supported = false;
	bool pre_analysis_supported = false;

	inline amf_base(bool fallback) : fallback(fallback) {}
	virtual ~amf_base() = default;
	virtual void init() = 0;

	void reinitialize()
	{
		AMF_RESULT res = amf_encoder->Flush();
		if (res != AMF_OK)
			throw amf_error("AMFComponent::Flush failed", res);

		res = amf_encoder->ReInit(cx, cy);
		if (res != AMF_OK)
			throw amf_error("AMFComponent::ReInit failed", res);

#ifdef __linux__
		onReinitialize();
#endif
	}

#ifdef __linux__
	virtual void onReceivedPacket(const int64_t &ts) {};
	virtual void onReinitialize() {};
#endif
};

using buf_t = std::vector<uint8_t>;

#ifdef _WIN32
using d3dtex_t = ComPtr<ID3D11Texture2D>;

struct amf_texencode : amf_base, public AMFSurfaceObserver {
	volatile bool destroying = false;

	std::vector<handle_tex> input_textures;

	std::mutex textures_mutex;
	std::vector<d3dtex_t> available_textures;
	std::unordered_map<AMFSurface *, d3dtex_t> active_textures;

	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;

	inline amf_texencode() : amf_base(false) {}
	~amf_texencode() { os_atomic_set_bool(&destroying, true); }

	void AMF_STD_CALL OnSurfaceDataRelease(amf::AMFSurface *surf) override
	{
		if (os_atomic_load_bool(&destroying))
			return;

		std::scoped_lock lock(textures_mutex);

		auto it = active_textures.find(surf);
		if (it != active_textures.end()) {
			available_textures.push_back(it->second);
			active_textures.erase(it);
		}
	}

	void init() override
	{
		AMF_RESULT res = amf_context->InitDX11(device, AMF_DX11_1);
		if (res != AMF_OK)
			throw amf_error("InitDX11 failed", res);
	}
};
#elif defined(__linux__)

// Some necessary forward refs
static void amf_encode_base(amf_base *enc, AMFSurface *amf_surf, encoder_packet *packet, bool *received_packet);
static int64_t convert_to_amf_ts(amf_base *enc, int64_t ts);

struct amf_texencode : amf_base {
	std::vector<amf_tex_p> input_textures;
	std::vector<amf_tex_p> available_textures;
	std::deque<amf_tex_p> active_textures;

	std::unique_ptr<AMFVulkanDevice> vk;
	VkQueue queue = VK_NULL_HANDLE;
	VkCommandPool cmdpool = VK_NULL_HANDLE;
	VkCommandBuffer cmdbuf = VK_NULL_HANDLE;
	struct texture_info textures = {};
	std::unordered_map<gs_texture *, GLuint> read_fbos;

	VkSubmitInfo copySubmitInfo;

	PFN_vkGetMemoryFdKHR vkGetMemoryFdKHR;
	PFN_vkGetSemaphoreFdKHR vkGetSemaphoreFdKHR;
	PFNGLGETERRORPROC glGetError;
	PFNGLCREATEMEMORYOBJECTSEXTPROC glCreateMemoryObjectsEXT;
	PFNGLDELETEMEMORYOBJECTSEXTPROC glDeleteMemoryObjectsEXT;
	PFNGLIMPORTMEMORYFDEXTPROC glImportMemoryFdEXT;
	PFNGLISMEMORYOBJECTEXTPROC glIsMemoryObjectEXT;
	PFNGLMEMORYOBJECTPARAMETERIVEXTPROC glMemoryObjectParameterivEXT;
	PFNGLGENTEXTURESPROC glGenTextures;
	PFNGLDELETETEXTURESPROC glDeleteTextures;
	PFNGLBINDTEXTUREPROC glBindTexture;
	PFNGLTEXPARAMETERIPROC glTexParameteri;
	PFNGLTEXSTORAGEMEM2DEXTPROC glTexStorageMem2DEXT;
	PFNGLGENSEMAPHORESEXTPROC glGenSemaphoresEXT;
	PFNGLDELETESEMAPHORESEXTPROC glDeleteSemaphoresEXT;
	PFNGLIMPORTSEMAPHOREFDEXTPROC glImportSemaphoreFdEXT;
	PFNGLISSEMAPHOREEXTPROC glIsSemaphoreEXT;
	PFNGLWAITSEMAPHOREEXTPROC glWaitSemaphoreEXT;
	PFNGLSIGNALSEMAPHOREEXTPROC glSignalSemaphoreEXT;
	PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers;
	PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers;
	PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer;
	PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D;
	PFNGLBLITFRAMEBUFFERPROC glBlitFramebuffer;

	inline amf_texencode() : amf_base(false) {}
	~amf_texencode()
	{
		if (!vk)
			return;

		VkDevice vkDevice = vk->hDevice;
		vkDeviceWaitIdle(vkDevice);
		vkDestroyCommandPool(vkDevice, cmdpool, nullptr);

		for (amf_tex_p &tex_ptr : input_textures) {
			amf_tex &tex = *tex_ptr;
			tex.surface->Release();
			AMFVulkanSurface &vulkanSurface = tex.vulkanSurface;
			vkFreeMemory(vkDevice, vulkanSurface.hMemory, nullptr);
			vkDestroyImage(vkDevice, vulkanSurface.hImage, nullptr);
		}

		obs_enter_graphics();

		if (textures.glsem) {
			for (int i = 0; i < 2; ++i) {
				auto &p = textures.planes[i];
				vkFreeMemory(vkDevice, p.memory, nullptr);
				vkDestroyImage(vkDevice, p.image, nullptr);
				glDeleteMemoryObjectsEXT(1, &p.glmem);
				glDeleteTextures(1, &p.gltex);
				glDeleteFramebuffers(1, &p.fbo);
			}
			vkDestroySemaphore(vkDevice, textures.sem, nullptr);
			vkDestroyFence(vkDevice, textures.fence, nullptr);
			glDeleteSemaphoresEXT(1, &textures.glsem);
		}

		for (auto f : read_fbos)
			glDeleteFramebuffers(1, &f.second);

		obs_leave_graphics();

		amf_encoder->Terminate();
		amf_context1->Terminate();
		amf_context->Terminate();

		vkDestroyDevice(vkDevice, nullptr);
		vkDestroyInstance(vk->hInstance, nullptr);
	}

	/* Using Vulkan, we cache/re-use surfaces, so OnSurfaceDataRelease is never called.
	   Instead, we match input/output packets to decide what's available. */
	virtual void onReceivedPacket(const int64_t &ts) override
	{
		while (!active_textures.empty()) {
			auto &tex = active_textures.front();
			if (tex->ts > ts)
				break;
			active_textures.pop_front();
			available_textures.push_back(tex);
		}
	}

	virtual void onReinitialize() override
	{
		for (amf_tex_p &tex : active_textures)
			available_textures.push_back(tex);
		active_textures.clear();
		return;
	}

	void init() override
	{
		vk = std::make_unique<AMFVulkanDevice>();
		vk->cbSizeof = sizeof(AMFVulkanDevice);

		std::vector<const char *> instance_extensions = {
			VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
			VK_KHR_SURFACE_EXTENSION_NAME,
		};

		std::vector<const char *> device_extensions = {
			VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
			VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
			VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME,
			VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME,
		};

		amf_size count = 0;
		amf_context1->GetVulkanDeviceExtensions(&count, nullptr);
		device_extensions.resize(device_extensions.size() + count);
		amf_context1->GetVulkanDeviceExtensions(&count, &device_extensions[device_extensions.size() - count]);

		VkApplicationInfo appInfo = {
			.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
			.pApplicationName = "OBS",
			.apiVersion = VK_API_VERSION_1_2,
		};

		VkInstanceCreateInfo instanceInfo = {
			.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
			.pApplicationInfo = &appInfo,
			.enabledExtensionCount = (uint32_t)instance_extensions.size(),
			.ppEnabledExtensionNames = instance_extensions.data(),
		};
		VK_CHECK(vkCreateInstance(&instanceInfo, nullptr, &vk->hInstance));

		uint32_t deviceCount = 0;
		VK_CHECK(vkEnumeratePhysicalDevices(vk->hInstance, &deviceCount, nullptr));
		std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
		VK_CHECK(vkEnumeratePhysicalDevices(vk->hInstance, &deviceCount, physicalDevices.data()));
		for (VkPhysicalDevice dev : physicalDevices) {
			VkPhysicalDeviceDriverProperties driverProps = {
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES,
			};

			VkPhysicalDeviceProperties2 props = {
				.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
				.pNext = &driverProps,
			};
			vkGetPhysicalDeviceProperties2(dev, &props);
			if (driverProps.driverID == VK_DRIVER_ID_MESA_RADV ||
			    driverProps.driverID == VK_DRIVER_ID_AMD_OPEN_SOURCE ||
			    driverProps.driverID == VK_DRIVER_ID_AMD_PROPRIETARY) {
				vk->hPhysicalDevice = dev;
				break;
			}
		}
		if (!vk->hPhysicalDevice) {
			throw "Failed to find Vulkan device";
		}

		uint32_t deviceExtensionCount = 0;
		VK_CHECK(vkEnumerateDeviceExtensionProperties(vk->hPhysicalDevice, nullptr, &deviceExtensionCount,
							      nullptr));
		std::vector<VkExtensionProperties> deviceExts(deviceExtensionCount);
		VK_CHECK(vkEnumerateDeviceExtensionProperties(vk->hPhysicalDevice, nullptr, &deviceExtensionCount,
							      deviceExts.data()));
		std::vector<const char *> deviceExtensions;
		for (const char *name : device_extensions) {
			auto it = std::find_if(deviceExts.begin(), deviceExts.end(), [name](VkExtensionProperties e) {
				return strcmp(e.extensionName, name) == 0;
			});
			if (it != deviceExts.end()) {
				deviceExtensions.push_back(name);
			}
		}

		float queuePriority = 1.0;
		std::vector<VkDeviceQueueCreateInfo> queueInfos;
		uint32_t queueFamilyCount;
		vkGetPhysicalDeviceQueueFamilyProperties(vk->hPhysicalDevice, &queueFamilyCount, nullptr);
		std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(vk->hPhysicalDevice, &queueFamilyCount,
							 queueFamilyProperties.data());
		for (uint32_t i = 0; i < queueFamilyProperties.size(); ++i) {
			VkDeviceQueueCreateInfo queueInfo = {
				.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
				.queueFamilyIndex = i,
				.queueCount = 1,
				.pQueuePriorities = &queuePriority,
			};
			queueInfos.push_back(queueInfo);
		}

		VkDeviceCreateInfo deviceInfo = {
			.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			.queueCreateInfoCount = (uint32_t)queueInfos.size(),
			.pQueueCreateInfos = queueInfos.data(),
			.enabledExtensionCount = (uint32_t)deviceExtensions.size(),
			.ppEnabledExtensionNames = deviceExtensions.data(),
		};
		VK_CHECK(vkCreateDevice(vk->hPhysicalDevice, &deviceInfo, nullptr, &vk->hDevice));

		AMF_RESULT res = amf_context1->InitVulkan(vk.get());
		if (res != AMF_OK)
			throw amf_error("InitVulkan failed", res);

		vkGetDeviceQueue(vk->hDevice, 0, 0, &queue);

		VkCommandPoolCreateInfo cmdPoolInfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.queueFamilyIndex = 0,
			.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		};
		VK_CHECK(vkCreateCommandPool(vk->hDevice, &cmdPoolInfo, nullptr, &cmdpool));

		allocateCommandBuffer(&cmdbuf);

#define GET_PROC_VK(x)                                 \
	x = reinterpret_cast<decltype(x)>(             \
		vkGetDeviceProcAddr(vk->hDevice, #x)); \
	if (!x)                                        \
		throw "Failed to resolve " #x;

#define GET_PROC_GL(x)                                            \
	x = reinterpret_cast<decltype(x)>(eglGetProcAddress(#x)); \
	if (!x)                                                   \
		throw "Failed to resolve " #x;

		GET_PROC_VK(vkGetMemoryFdKHR);
		GET_PROC_VK(vkGetSemaphoreFdKHR);
		GET_PROC_GL(glGetError);
		GET_PROC_GL(glCreateMemoryObjectsEXT);
		GET_PROC_GL(glDeleteMemoryObjectsEXT);
		GET_PROC_GL(glImportMemoryFdEXT);
		GET_PROC_GL(glIsMemoryObjectEXT);
		GET_PROC_GL(glMemoryObjectParameterivEXT);
		GET_PROC_GL(glGenTextures);
		GET_PROC_GL(glDeleteTextures);
		GET_PROC_GL(glBindTexture);
		GET_PROC_GL(glTexParameteri);
		GET_PROC_GL(glTexStorageMem2DEXT);
		GET_PROC_GL(glGenSemaphoresEXT);
		GET_PROC_GL(glDeleteSemaphoresEXT);
		GET_PROC_GL(glImportSemaphoreFdEXT);
		GET_PROC_GL(glIsSemaphoreEXT);
		GET_PROC_GL(glWaitSemaphoreEXT);
		GET_PROC_GL(glSignalSemaphoreEXT);
		GET_PROC_GL(glGenFramebuffers);
		GET_PROC_GL(glDeleteFramebuffers);
		GET_PROC_GL(glBindFramebuffer);
		GET_PROC_GL(glFramebufferTexture2D);
		GET_PROC_GL(glBlitFramebuffer);

#undef GET_PROC_VK
#undef GET_PROC_GL
	}

	uint32_t memoryTypeIndex(VkMemoryPropertyFlags properties, uint32_t typeBits)
	{
		VkPhysicalDeviceMemoryProperties prop;
		vkGetPhysicalDeviceMemoryProperties(vk->hPhysicalDevice, &prop);
		for (uint32_t i = 0; i < prop.memoryTypeCount; i++) {
			if ((prop.memoryTypes[i].propertyFlags & properties) == properties && typeBits & (1 << i)) {
				return i;
			}
		}
		return 0xFFFFFFFF;
	}

	inline void allocateCommandBuffer(VkCommandBuffer *buffer)
	{
		if (!buffer)
			buffer = &cmdbuf;
		VkCommandBufferAllocateInfo info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandPool = cmdpool,
			.commandBufferCount = 1,
		};
		VK_CHECK(vkAllocateCommandBuffers(vk->hDevice, &info, buffer));
	}

	inline void beginCommandBuffer(VkCommandBuffer *buffer = nullptr)
	{
		if (!buffer)
			buffer = &cmdbuf;
		static VkCommandBufferBeginInfo info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		};
		VK_CHECK(vkBeginCommandBuffer(*buffer, &info));
	}

	inline void endCommandBuffer(VkCommandBuffer *buffer = nullptr)
	{
		if (!buffer)
			buffer = &cmdbuf;
		VK_CHECK(vkEndCommandBuffer(*buffer));
	}

	inline void submitCommandBuffer(VkCommandBuffer *buffer = nullptr)
	{
		if (!buffer)
			buffer = &cmdbuf;
		endCommandBuffer(buffer);
		VkSubmitInfo info = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = buffer,
		};
		VK_CHECK(vkQueueSubmit(queue, 1, &info, nullptr));
	}

	inline void waitForFence(VkFence *fence = nullptr)
	{
		if (!fence)
			fence = &textures.fence;
		VK_CHECK(vkWaitForFences(vk->hDevice, 1, fence, VK_TRUE, UINT64_MAX));
		VK_CHECK(vkResetFences(vk->hDevice, 1, fence));
	}

	void createTextures(encoder_texture *from)
	{
		beginCommandBuffer();
		for (int i = 0; i < 2; ++i) {
			auto &plane = textures.planes[i];

			obs_enter_graphics();
			auto tex = from->tex[i];
			auto gs_format = gs_texture_get_color_format(tex);
			plane.width = gs_texture_get_width(tex);
			plane.height = gs_texture_get_height(tex);
			obs_leave_graphics();

			VkExternalMemoryImageCreateInfo extImageInfo = {
				.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
				.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
			};

			VkImageCreateInfo imageInfo = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
				.pNext = &extImageInfo,
				.imageType = VK_IMAGE_TYPE_2D,
				.format = to_vk_format(gs_format),
				.extent =
					{
						.width = plane.width,
						.height = plane.height,
						.depth = 1,
					},
				.arrayLayers = 1,
				.mipLevels = 1,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.tiling = VK_IMAGE_TILING_OPTIMAL,
				.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
				.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
			};
			VK_CHECK(vkCreateImage(vk->hDevice, &imageInfo, nullptr, &plane.image));

			VkMemoryRequirements memoryReqs;
			vkGetImageMemoryRequirements(vk->hDevice, plane.image, &memoryReqs);

			VkExportMemoryAllocateInfo expMemoryAllocInfo = {
				.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
				.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
			};

			VkMemoryDedicatedAllocateInfo dedMemoryAllocInfo = {
				.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
				.image = plane.image,
				.pNext = &expMemoryAllocInfo,
			};

			VkMemoryAllocateInfo memoryAllocInfo = {
				.pNext = &dedMemoryAllocInfo,
				.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
				.allocationSize = memoryReqs.size,
				.memoryTypeIndex =
					memoryTypeIndex(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryReqs.memoryTypeBits),
			};
			VK_CHECK(vkAllocateMemory(vk->hDevice, &memoryAllocInfo, nullptr, &plane.memory));
			VK_CHECK(vkBindImageMemory(vk->hDevice, plane.image, plane.memory, 0));

			VkImageMemoryBarrier imageBarrier = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				.image = plane.image,
				.subresourceRange =
					{
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.layerCount = 1,
						.levelCount = 1,
					},
				.srcAccessMask = 0,
				.dstAccessMask = 0,
			};
			vkCmdPipelineBarrier(cmdbuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
					     0, 0, nullptr, 0, nullptr, 1, &imageBarrier);

			imageBarrier.oldLayout = imageBarrier.newLayout;
			imageBarrier.srcQueueFamilyIndex = 0;
			imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
			vkCmdPipelineBarrier(cmdbuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
					     0, 0, nullptr, 0, nullptr, 1, &imageBarrier);

			// Import memory
			VkMemoryGetFdInfoKHR memFdInfo = {
				.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
				.memory = plane.memory,
				.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
			};
			int fd = -1;
			VK_CHECK(vkGetMemoryFdKHR(vk->hDevice, &memFdInfo, &fd));

			obs_enter_graphics();

			glCreateMemoryObjectsEXT(1, &plane.glmem);
			GLint dedicated = GL_TRUE;
			glMemoryObjectParameterivEXT(plane.glmem, GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
			glImportMemoryFdEXT(plane.glmem, memoryAllocInfo.allocationSize, GL_HANDLE_TYPE_OPAQUE_FD_EXT,
					    fd);

			glGenTextures(1, &plane.gltex);
			glBindTexture(GL_TEXTURE_2D, plane.gltex);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_TILING_EXT, GL_OPTIMAL_TILING_EXT);
			glTexStorageMem2DEXT(GL_TEXTURE_2D, 1, to_gl_format(gs_format), imageInfo.extent.width,
					     imageInfo.extent.height, plane.glmem, 0);

			glGenFramebuffers(1, &plane.fbo);
			glBindFramebuffer(GL_FRAMEBUFFER, plane.fbo);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, plane.gltex, 0);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

			bool import_ok = glIsMemoryObjectEXT(plane.glmem) && glGetError() == GL_NO_ERROR;

			obs_leave_graphics();

			if (!import_ok)
				throw "OpenGL texture import failed";

			textures.sem_tex[i] = plane.gltex;
			textures.sem_layout[i] = GL_LAYOUT_TRANSFER_SRC_EXT;
		}

		VkExportSemaphoreCreateInfo expSemInfo = {
			.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
			.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT,
		};

		VkSemaphoreCreateInfo semInfo = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
			.pNext = &expSemInfo,
		};
		VK_CHECK(vkCreateSemaphore(vk->hDevice, &semInfo, nullptr, &textures.sem));

		endCommandBuffer();
		VkSubmitInfo submitInfo = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &cmdbuf,
		};
		VkFenceCreateInfo fenceInfo = {
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		};
		VK_CHECK(vkCreateFence(vk->hDevice, &fenceInfo, nullptr, &textures.fence));
		VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, textures.fence));
		waitForFence();

		VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		copySubmitInfo = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &textures.sem,
			.pWaitDstStageMask = &waitStage,
			.commandBufferCount = 1,
		};

		// Import semaphores
		VkSemaphoreGetFdInfoKHR semFdInfo = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
			.semaphore = textures.sem,
			.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT,
		};
		int fd = -1;
		VK_CHECK(vkGetSemaphoreFdKHR(vk->hDevice, &semFdInfo, &fd));

		obs_enter_graphics();

		glGenSemaphoresEXT(1, &textures.glsem);
		glImportSemaphoreFdEXT(textures.glsem, GL_HANDLE_TYPE_OPAQUE_FD_EXT, fd);

		bool import_ok = glIsSemaphoreEXT(textures.glsem) && glGetError() == GL_NO_ERROR;

		obs_leave_graphics();

		if (!import_ok)
			throw "OpenGL semaphore import failed";
	}

	inline GLuint getReadFBO(gs_texture *tex)
	{
		auto it = read_fbos.find(tex);
		if (it != read_fbos.end()) {
			return it->second;
		}
		GLuint *tex_obj = static_cast<GLuint *>(gs_texture_get_obj(tex));
		GLuint fbo;
		glGenFramebuffers(1, &fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *tex_obj, 0);
		read_fbos.insert({tex, fbo});
		return fbo;
	}

	amf_tex_p getOutputTexture()
	{
		if (available_textures.size()) {
			amf_tex_p tex = available_textures.back();
			available_textures.pop_back();
			return tex;
		}

		VkImageCreateInfo imageInfo = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.format = to_vk_format(amf_format),
			.extent =
				{
					.width = cx,
					.height = cy,
					.depth = 1,
				},
			.arrayLayers = 1,
			.mipLevels = 1,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.tiling = VK_IMAGE_TILING_LINEAR,
			.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		};
		VkImage vkImage;
		VK_CHECK(vkCreateImage(vk->hDevice, &imageInfo, nullptr, &vkImage));

		VkMemoryRequirements memoryReqs;
		vkGetImageMemoryRequirements(vk->hDevice, vkImage, &memoryReqs);
		VkMemoryAllocateInfo memoryAllocInfo = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = memoryReqs.size,
			.memoryTypeIndex =
				memoryTypeIndex(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryReqs.memoryTypeBits),
		};
		VkDeviceMemory vkMemory;
		VK_CHECK(vkAllocateMemory(vk->hDevice, &memoryAllocInfo, nullptr, &vkMemory));
		VK_CHECK(vkBindImageMemory(vk->hDevice, vkImage, vkMemory, 0));

		beginCommandBuffer();
		VkImageMemoryBarrier imageBarrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.image = vkImage,
			.subresourceRange =
				{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.layerCount = 1,
					.levelCount = 1,
				},
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
		};
		vkCmdPipelineBarrier(cmdbuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
				     nullptr, 0, nullptr, 1, &imageBarrier);
		submitCommandBuffer();

		amf_tex_p tex_ptr = std::make_shared<amf_tex>();
		amf_tex &tex = *tex_ptr;

		tex.vulkanSurface = {
			.cbSizeof = sizeof(AMFVulkanSurface),
			.hImage = vkImage,
			.hMemory = vkMemory,
			.iSize = (amf_int64)memoryAllocInfo.allocationSize,
			.eFormat = imageInfo.format,
			.iWidth = (amf_int32)imageInfo.extent.width,
			.iHeight = (amf_int32)imageInfo.extent.height,
			.eCurrentLayout = imageInfo.initialLayout,
			.eUsage = AMF_SURFACE_USAGE_DEFAULT,
			.eAccess = AMF_MEMORY_CPU_LOCAL,
		};

		AMF_RESULT res = amf_context1->CreateSurfaceFromVulkanNative(&tex.vulkanSurface, &tex.surface, nullptr);
		if (res != AMF_OK)
			throw amf_error("CreateSurfaceFromVulkanNative failed", res);

		VkCommandBuffer &copyCommandBuffer = tex.copyCommandBuffer;
		allocateCommandBuffer(&copyCommandBuffer);
		beginCommandBuffer(&copyCommandBuffer);

		auto &planes = textures.planes;

		VkImageMemoryBarrier imageBarriers[2];
		for (int i = 0; i < 2; ++i) {
			imageBarriers[i] = {
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				.image = planes[i].image,
				.subresourceRange =
					{
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.layerCount = 1,
						.levelCount = 1,
					},
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
				.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL,
				.dstQueueFamilyIndex = 0,
			};
		}
		vkCmdPipelineBarrier(copyCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, imageBarriers);

		VkImageCopy imageCopy = {
			.srcSubresource =
				{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.layerCount = 1,
				},
			.dstSubresource =
				{
					.aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT,
					.layerCount = 1,
				},
			.extent =
				{
					.depth = 1,
				},
		};

		for (int i = 0; i < 2; ++i) {
			auto &plane = planes[i];
			auto &extent = imageCopy.extent;
			extent.width = plane.width;
			extent.height = plane.height;
			if (i)
				imageCopy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT;
			vkCmdCopyImage(copyCommandBuffer, plane.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				       tex.vulkanSurface.hImage, VK_IMAGE_LAYOUT_GENERAL, 1, &imageCopy);
		}

		for (int i = 0; i < 2; ++i) {
			auto barrier = &imageBarriers[i];
			barrier->srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			barrier->dstAccessMask = 0;
			barrier->srcQueueFamilyIndex = 0;
			barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
		}
		vkCmdPipelineBarrier(copyCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
				     VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 2, imageBarriers);

		VK_CHECK(vkEndCommandBuffer(copyCommandBuffer));

		input_textures.push_back(tex_ptr);
		return tex_ptr;
	}

	bool encode(encoder_texture *texture, int64_t pts, encoder_packet *packet, bool *received_packet)
	{
		if (!texture)
			throw "Encode failed: bad texture handle";

		if (!textures.glsem)
			createTextures(texture);

		amf_tex_p tex_ptr = getOutputTexture();
		amf_tex &tex = *tex_ptr;

		/* ------------------------------------ */
		/* copy to output tex                   */

		obs_enter_graphics();

		auto &planes = textures.planes;
		for (int i = 0; i < 2; ++i) {
			GLuint read_fbo = getReadFBO(texture->tex[i]);
			auto &plane = planes[i];
			glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, plane.fbo);
			glBlitFramebuffer(0, 0, plane.width, plane.height, 0, 0, plane.width, plane.height,
					  GL_COLOR_BUFFER_BIT, GL_NEAREST);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		}
		glSignalSemaphoreEXT(textures.glsem, 0, 0, 2, textures.sem_tex, textures.sem_layout);

		obs_leave_graphics();

		/* ------------------------------------ */
		/* copy to submit tex                   */

		copySubmitInfo.pCommandBuffers = &tex.copyCommandBuffer;
		VK_CHECK(vkQueueSubmit(queue, 1, &copySubmitInfo, textures.fence));
		waitForFence();

		AMFSurface *amf_surf = tex.surface;
		amf_surf->SetPts(convert_to_amf_ts(this, pts));
		amf_surf->SetProperty(L"PTS", pts);
		tex.ts = pts;
		active_textures.push_back(tex_ptr);

		/* ------------------------------------ */
		/* do actual encode                     */

		amf_encode_base(this, amf_surf, packet, received_packet);
		return true;
	}
};

#endif

struct amf_fallback : amf_base, public AMFSurfaceObserver {
	volatile bool destroying = false;

	std::mutex buffers_mutex;
	std::vector<buf_t> available_buffers;
	std::unordered_map<AMFSurface *, buf_t> active_buffers;

#if __USE_OPENCL
	AMFComputePtr amfCompute = nullptr;
#endif

	inline amf_fallback() : amf_base(true) {}
	~amf_fallback()
	{
		os_atomic_set_bool(&destroying, true);
#if __USE_OPENCL
		amfCompute = nullptr;
#endif
	}

	void AMF_STD_CALL OnSurfaceDataRelease(amf::AMFSurface *surf) override
	{
#if __USE_OPENCL
		if (amfCompute)
			return;
#endif

		if (os_atomic_load_bool(&destroying))
			return;

		std::scoped_lock lock(buffers_mutex);

		auto it = active_buffers.find(surf);
		if (it != active_buffers.end()) {
			available_buffers.push_back(std::move(it->second));
			active_buffers.erase(it);
		}
	}

	void init() override
	{
#if defined(_WIN32)
		AMF_RESULT res = amf_context->InitDX11(nullptr, AMF_DX11_1);
		if (res != AMF_OK)
			throw amf_error("InitDX11 failed", res);
#elif defined(__linux__)
		AMF_RESULT res = amf_context1->InitVulkan(nullptr);
		if (res != AMF_OK)
			throw amf_error("InitVulkan failed", res);

#if __USE_OPENCL
		if (amf_context->InitOpenCL() == AMF_OK &&
		    amf_context->GetCompute(amf::AMF_MEMORY_OPENCL, &amfCompute) == AMF_OK)
			blog(LOG_INFO, "Initialized OpenCL");
		else
			blog(LOG_WARNING, "Could not initialize OpenCL");
#endif
#endif
	}
};

/* ------------------------------------------------------------------------- */
/* More garbage                                                              */

template<typename T> static bool get_amf_property(amf_base *enc, const wchar_t *name, T *value)
{
	AMF_RESULT res = enc->amf_encoder->GetProperty(name, value);
	return res == AMF_OK;
}

template<typename T> static void set_amf_property(amf_base *enc, const wchar_t *name, const T &value)
{
	AMF_RESULT res = enc->amf_encoder->SetProperty(name, value);
	if (res != AMF_OK)
		error("Failed to set property '%ls': %ls", name, amf_trace->GetResultText(res));
}

#define set_avc_property(enc, name, value) set_amf_property(enc, AMF_VIDEO_ENCODER_##name, value)
#define set_hevc_property(enc, name, value) set_amf_property(enc, AMF_VIDEO_ENCODER_HEVC_##name, value)
#define set_av1_property(enc, name, value) set_amf_property(enc, AMF_VIDEO_ENCODER_AV1_##name, value)

#define get_avc_property(enc, name, value) get_amf_property(enc, AMF_VIDEO_ENCODER_##name, value)
#define get_hevc_property(enc, name, value) get_amf_property(enc, AMF_VIDEO_ENCODER_HEVC_##name, value)
#define get_av1_property(enc, name, value) get_amf_property(enc, AMF_VIDEO_ENCODER_AV1_##name, value)

#define get_opt_name(name)                                                      \
	((enc->codec == amf_codec_type::AVC)    ? AMF_VIDEO_ENCODER_##name      \
	 : (enc->codec == amf_codec_type::HEVC) ? AMF_VIDEO_ENCODER_HEVC_##name \
						: AMF_VIDEO_ENCODER_AV1_##name)
#define get_opt_name_enum(name)                                                      \
	((enc->codec == amf_codec_type::AVC)    ? (int)AMF_VIDEO_ENCODER_##name      \
	 : (enc->codec == amf_codec_type::HEVC) ? (int)AMF_VIDEO_ENCODER_HEVC_##name \
						: (int)AMF_VIDEO_ENCODER_AV1_##name)
#define set_opt(name, value) set_amf_property(enc, get_opt_name(name), value)
#define get_opt(name, value) get_amf_property(enc, get_opt_name(name), value)
#define set_avc_opt(name, value) set_avc_property(enc, name, value)
#define set_hevc_opt(name, value) set_hevc_property(enc, name, value)
#define set_av1_opt(name, value) set_av1_property(enc, name, value)
#define set_enum_opt(name, value) set_amf_property(enc, get_opt_name(name), get_opt_name_enum(name##_##value))
#define set_avc_enum(name, value) set_avc_property(enc, name, AMF_VIDEO_ENCODER_##name##_##value)
#define set_hevc_enum(name, value) set_hevc_property(enc, name, AMF_VIDEO_ENCODER_HEVC_##name##_##value)
#define set_av1_enum(name, value) set_av1_property(enc, name, AMF_VIDEO_ENCODER_AV1_##name##_##value)

/* ------------------------------------------------------------------------- */
/* Implementation                                                            */

#ifdef _WIN32
static HMODULE get_lib(const char *lib)
{
	HMODULE mod = GetModuleHandleA(lib);
	if (mod)
		return mod;

	return LoadLibraryA(lib);
}

#define AMD_VENDOR_ID 0x1002

typedef HRESULT(WINAPI *CREATEDXGIFACTORY1PROC)(REFIID, void **);

static bool amf_init_d3d11(amf_texencode *enc)
try {
	HMODULE dxgi = get_lib("DXGI.dll");
	HMODULE d3d11 = get_lib("D3D11.dll");
	CREATEDXGIFACTORY1PROC create_dxgi;
	PFN_D3D11_CREATE_DEVICE create_device;
	ComPtr<IDXGIFactory> factory;
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	ComPtr<IDXGIAdapter> adapter;
	DXGI_ADAPTER_DESC desc;
	HRESULT hr;

	if (!dxgi || !d3d11)
		throw "Couldn't get D3D11/DXGI libraries? "
		      "That definitely shouldn't be possible.";

	create_dxgi = (CREATEDXGIFACTORY1PROC)GetProcAddress(dxgi, "CreateDXGIFactory1");
	create_device = (PFN_D3D11_CREATE_DEVICE)GetProcAddress(d3d11, "D3D11CreateDevice");

	if (!create_dxgi || !create_device)
		throw "Failed to load D3D11/DXGI procedures";

	hr = create_dxgi(__uuidof(IDXGIFactory2), (void **)&factory);
	if (FAILED(hr))
		throw HRError("CreateDXGIFactory1 failed", hr);

	obs_video_info ovi;
	obs_get_video_info(&ovi);

	hr = factory->EnumAdapters(ovi.adapter, &adapter);
	if (FAILED(hr))
		throw HRError("EnumAdapters failed", hr);

	adapter->GetDesc(&desc);
	if (desc.VendorId != AMD_VENDOR_ID)
		throw "Seems somehow AMF is trying to initialize "
		      "on a non-AMD adapter";

	hr = create_device(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device,
			   nullptr, &context);
	if (FAILED(hr))
		throw HRError("D3D11CreateDevice failed", hr);

	enc->device = device;
	enc->context = context;
	return true;

} catch (const HRError &err) {
	error("%s: %s: 0x%lX", __FUNCTION__, err.str, err.hr);
	return false;

} catch (const char *err) {
	error("%s: %s", __FUNCTION__, err);
	return false;
}

static void add_output_tex(amf_texencode *enc, ComPtr<ID3D11Texture2D> &output_tex, ID3D11Texture2D *from)
{
	ID3D11Device *device = enc->device;
	HRESULT hr;

	D3D11_TEXTURE2D_DESC desc;
	from->GetDesc(&desc);
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	desc.MiscFlags = 0;

	hr = device->CreateTexture2D(&desc, nullptr, &output_tex);
	if (FAILED(hr))
		throw HRError("Failed to create texture", hr);
}

static inline bool get_available_tex(amf_texencode *enc, ComPtr<ID3D11Texture2D> &output_tex)
{
	std::scoped_lock lock(enc->textures_mutex);
	if (enc->available_textures.size()) {
		output_tex = enc->available_textures.back();
		enc->available_textures.pop_back();
		return true;
	}

	return false;
}

static inline void get_output_tex(amf_texencode *enc, ComPtr<ID3D11Texture2D> &output_tex, ID3D11Texture2D *from)
{
	if (!get_available_tex(enc, output_tex))
		add_output_tex(enc, output_tex, from);
}

static void get_tex_from_handle(amf_texencode *enc, uint32_t handle, IDXGIKeyedMutex **km_out,
				ID3D11Texture2D **tex_out)
{
	ID3D11Device *device = enc->device;
	ComPtr<ID3D11Texture2D> tex;
	HRESULT hr;

	for (size_t i = 0; i < enc->input_textures.size(); i++) {
		struct handle_tex &ht = enc->input_textures[i];
		if (ht.handle == handle) {
			ht.km.CopyTo(km_out);
			ht.tex.CopyTo(tex_out);
			return;
		}
	}

	hr = device->OpenSharedResource((HANDLE)(uintptr_t)handle, __uuidof(ID3D11Resource), (void **)&tex);
	if (FAILED(hr))
		throw HRError("OpenSharedResource failed", hr);

	ComQIPtr<IDXGIKeyedMutex> km(tex);
	if (!km)
		throw "QueryInterface(IDXGIKeyedMutex) failed";

	tex->SetEvictionPriority(DXGI_RESOURCE_PRIORITY_MAXIMUM);

	struct handle_tex new_ht = {handle, tex, km};
	enc->input_textures.push_back(std::move(new_ht));

	*km_out = km.Detach();
	*tex_out = tex.Detach();
}
#endif

static constexpr amf_int64 macroblock_size = 16;

static inline void calc_throughput(amf_base *enc)
{
	amf_int64 mb_cx = ((amf_int64)enc->cx + (macroblock_size - 1)) / macroblock_size;
	amf_int64 mb_cy = ((amf_int64)enc->cy + (macroblock_size - 1)) / macroblock_size;
	amf_int64 mb_frame = mb_cx * mb_cy;

	enc->throughput = mb_frame * (amf_int64)enc->fps_num / (amf_int64)enc->fps_den;
}

static inline int get_avc_preset(amf_base *enc, const char *preset);
#if ENABLE_HEVC
static inline int get_hevc_preset(amf_base *enc, const char *preset);
#endif
static inline int get_av1_preset(amf_base *enc, const char *preset);

static inline int get_preset(amf_base *enc, const char *preset)
{
	if (enc->codec == amf_codec_type::AVC)
		return get_avc_preset(enc, preset);

#if ENABLE_HEVC
	else if (enc->codec == amf_codec_type::HEVC)
		return get_hevc_preset(enc, preset);

#endif
	else if (enc->codec == amf_codec_type::AV1)
		return get_av1_preset(enc, preset);

	return 0;
}

// Disabling throughput/preset compatibility checks for now.
// Seems to not work on Linux (always falls back to the lowest option).
#define _CHECK_THROUGHPUT false

#if _CHECK_THROUGHPUT
static inline void refresh_throughput_caps(amf_base *enc, const char *&preset)
{
	AMF_RESULT res = AMF_OK;
	AMFCapsPtr caps;

	set_opt(QUALITY_PRESET, get_preset(enc, preset));
	res = enc->amf_encoder->GetCaps(&caps);
	if (res == AMF_OK) {
		caps->GetProperty(get_opt_name(CAP_MAX_THROUGHPUT), &enc->max_throughput);
		caps->GetProperty(get_opt_name(CAP_REQUESTED_THROUGHPUT), &enc->requested_throughput);
	}
}

static inline void check_preset_compatibility(amf_base *enc, const char *&preset)
{
	/* The throughput depends on the current preset and the other static
	 * encoder properties. If the throughput is lower than the max
	 * throughput, switch to a lower preset. */

	refresh_throughput_caps(enc, preset);
	if (astrcmpi(preset, "highQuality") == 0) {
		if (!enc->max_throughput) {
			preset = "quality";
			set_opt(QUALITY_PRESET, get_preset(enc, preset));
		} else {
			if (enc->max_throughput - enc->requested_throughput < enc->throughput) {
				preset = "quality";
				refresh_throughput_caps(enc, preset);
			}
		}
	}

	if (astrcmpi(preset, "quality") == 0) {
		if (!enc->max_throughput) {
			preset = "balanced";
			set_opt(QUALITY_PRESET, get_preset(enc, preset));
		} else {
			if (enc->max_throughput - enc->requested_throughput < enc->throughput) {
				preset = "balanced";
				refresh_throughput_caps(enc, preset);
			}
		}
	}

	if (astrcmpi(preset, "balanced") == 0) {
		if (!enc->max_throughput) {
			preset = "speed";
			set_opt(QUALITY_PRESET, get_preset(enc, preset));
		} else {
			if (enc->max_throughput - enc->requested_throughput < enc->throughput) {
				preset = "speed";
				refresh_throughput_caps(enc, preset);
			}
		}
	}
}
#endif

static inline int64_t convert_to_amf_ts(amf_base *enc, int64_t ts)
{
	constexpr int64_t amf_timebase = AMF_SECOND;
	return ts * amf_timebase / (int64_t)enc->fps_den;
}

static inline int64_t convert_to_obs_ts(amf_base *enc, int64_t ts)
{
	constexpr int64_t amf_timebase = AMF_SECOND;
	return ts * (int64_t)enc->fps_den / amf_timebase;
}

static void convert_to_encoder_packet(amf_base *enc, AMFDataPtr &data, encoder_packet *packet)
{
	if (!data)
		return;

	enc->packet_data = AMFBufferPtr(data);
	data->GetProperty(L"PTS", &packet->pts);

	uint64_t type = 0;
	AMF_RESULT res = data->GetProperty(enc->output_data_type_key, &type);
	if (res != AMF_OK)
		throw amf_error("Failed to GetProperty(): encoder output "
				"data type",
				res);

	switch (enc->codec) {
	case amf_codec_type::AVC:
	case amf_codec_type::HEVC:
		switch (type) {
		case AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_IDR:
			packet->priority = OBS_NAL_PRIORITY_HIGHEST;
			break;
		case AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_I:
			packet->priority = OBS_NAL_PRIORITY_HIGH;
			break;
		default:
			packet->priority = OBS_NAL_PRIORITY_LOW;
			break;
		}
		break;
	case amf_codec_type::AV1:
		switch (type) {
		case AMF_VIDEO_ENCODER_AV1_OUTPUT_FRAME_TYPE_KEY:
			packet->priority = OBS_NAL_PRIORITY_HIGHEST;
			break;
		case AMF_VIDEO_ENCODER_AV1_OUTPUT_FRAME_TYPE_INTRA_ONLY:
			packet->priority = OBS_NAL_PRIORITY_HIGH;
			break;
		case AMF_VIDEO_ENCODER_AV1_OUTPUT_FRAME_TYPE_SWITCH:
		case AMF_VIDEO_ENCODER_AV1_OUTPUT_FRAME_TYPE_SHOW_EXISTING:
			packet->priority = OBS_NAL_PRIORITY_DISPOSABLE;
			break;
		default:
			packet->priority = OBS_NAL_PRIORITY_LOW;
			break;
		}
		break;
	}

	packet->data = (uint8_t *)enc->packet_data->GetNative();
	packet->size = enc->packet_data->GetSize();
	packet->type = OBS_ENCODER_VIDEO;
	packet->dts = convert_to_obs_ts(enc, data->GetPts());
	packet->keyframe = type == AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_IDR;

#ifdef __linux__
	enc->onReceivedPacket(packet->dts);
#endif

	if (enc->dts_offset && enc->codec != amf_codec_type::AV1)
		packet->dts -= enc->dts_offset;
}

#ifndef SEC_TO_NSEC
#define SEC_TO_NSEC 1000000000ULL
#endif

struct roi_params {
	uint32_t mb_width;
	uint32_t mb_height;
	amf_int32 pitch;
	bool h264;
	amf_uint32 *buf;
};

static void roi_cb(void *param, obs_encoder_roi *roi)
{
	const roi_params *rp = static_cast<roi_params *>(param);

	/* AMF does not support negative priority */
	if (roi->priority < 0)
		return;

	const uint32_t mb_size = rp->h264 ? 16 : 64;
	const uint32_t roi_left = roi->left / mb_size;
	const uint32_t roi_top = roi->top / mb_size;
	const uint32_t roi_right = (roi->right - 1) / mb_size;
	const uint32_t roi_bottom = (roi->bottom - 1) / mb_size;
	/* Importance value range is 0..10 */
	const amf_uint32 priority = (amf_uint32)(10.0f * roi->priority);

	for (uint32_t mb_y = 0; mb_y < rp->mb_height; mb_y++) {
		if (mb_y < roi_top || mb_y > roi_bottom)
			continue;

		for (uint32_t mb_x = 0; mb_x < rp->mb_width; mb_x++) {
			if (mb_x < roi_left || mb_x > roi_right)
				continue;

			rp->buf[mb_y * rp->pitch / sizeof(amf_uint32) + mb_x] = priority;
		}
	}
}

static void create_roi(amf_base *enc, AMFSurface *amf_surf)
{
	uint32_t mb_size = 16; /* H.264 is always 16x16 */
	if (enc->codec == amf_codec_type::HEVC || enc->codec == amf_codec_type::AV1)
		mb_size = 64; /* AMF HEVC & AV1 use 64x64 blocks */

	const uint32_t mb_width = (enc->cx + mb_size - 1) / mb_size;
	const uint32_t mb_height = (enc->cy + mb_size - 1) / mb_size;

	if (!enc->roi_map) {
		AMFContext1Ptr context1(enc->amf_context);
		AMF_RESULT res = context1->AllocSurfaceEx(AMF_MEMORY_HOST, AMF_SURFACE_GRAY32, mb_width, mb_height,
							  AMF_SURFACE_USAGE_DEFAULT | AMF_SURFACE_USAGE_LINEAR,
							  AMF_MEMORY_CPU_DEFAULT, &enc->roi_map);

		if (res != AMF_OK) {
			warn("Failed allocating surface for ROI map!");
			/* Clear ROI to prevent failure the next frame */
			obs_encoder_clear_roi(enc->encoder);
			return;
		}
	}

	/* This is just following the SimpleROI example. */
	amf_uint32 *pBuf = (amf_uint32 *)enc->roi_map->GetPlaneAt(0)->GetNative();
	amf_int32 pitch = enc->roi_map->GetPlaneAt(0)->GetHPitch();
	memset(pBuf, 0, pitch * mb_height);

	roi_params par{mb_width, mb_height, pitch, enc->codec == amf_codec_type::AVC, pBuf};
	obs_encoder_enum_roi(enc->encoder, roi_cb, &par);

	enc->roi_increment = obs_encoder_get_roi_increment(enc->encoder);
}

static void add_roi(amf_base *enc, AMFSurface *amf_surf)
{
	const uint32_t increment = obs_encoder_get_roi_increment(enc->encoder);

	if (increment != enc->roi_increment || !enc->roi_increment)
		create_roi(enc, amf_surf);

	if (enc->codec == amf_codec_type::AVC)
		amf_surf->SetProperty(AMF_VIDEO_ENCODER_ROI_DATA, enc->roi_map);
	else if (enc->codec == amf_codec_type::HEVC)
		amf_surf->SetProperty(AMF_VIDEO_ENCODER_HEVC_ROI_DATA, enc->roi_map);
	else if (enc->codec == amf_codec_type::AV1)
		amf_surf->SetProperty(AMF_VIDEO_ENCODER_AV1_ROI_DATA, enc->roi_map);
}

static void amf_encode_base(amf_base *enc, AMFSurface *amf_surf, encoder_packet *packet, bool *received_packet)
{
	auto &queued_packets = enc->queued_packets;
	uint64_t ts_start = os_gettime_ns();
	AMF_RESULT res;

	*received_packet = false;

	bool waiting = true;
	while (waiting) {
		/* ----------------------------------- */
		/* add ROI data (if any)               */
		if (enc->roi_supported && obs_encoder_has_roi(enc->encoder))
			add_roi(enc, amf_surf);

		/* ----------------------------------- */
		/* submit frame                        */

		res = enc->amf_encoder->SubmitInput(amf_surf);

		if (res == AMF_OK || res == AMF_NEED_MORE_INPUT) {
			waiting = false;

		} else if (res == AMF_INPUT_FULL) {
			os_sleep_ms(1);

			uint64_t duration = os_gettime_ns() - ts_start;
			constexpr uint64_t timeout = 5 * SEC_TO_NSEC;

			if (duration >= timeout) {
				throw amf_error("SubmitInput timed out", res);
			}
		} else {
			throw amf_error("SubmitInput failed", res);
		}

		/* ----------------------------------- */
		/* query as many packets as possible   */

		AMFDataPtr new_packet;
		do {
			res = enc->amf_encoder->QueryOutput(&new_packet);
			if (new_packet)
				queued_packets.push_back(new_packet);

			if (res != AMF_REPEAT && res != AMF_OK) {
				throw amf_error("QueryOutput failed", res);
			}
		} while (!!new_packet);
	}

	/* ----------------------------------- */
	/* return a packet if available        */

	if (queued_packets.size()) {
		AMFDataPtr amf_out;

		amf_out = queued_packets.front();
		queued_packets.pop_front();

		*received_packet = true;
		convert_to_encoder_packet(enc, amf_out, packet);
	}
}

static bool amf_encode_tex(void *data, uint32_t handle, int64_t pts, uint64_t lock_key, uint64_t *next_key,
			   encoder_packet *packet, bool *received_packet)
#ifdef _WIN32
try {
	amf_texencode *enc = (amf_texencode *)data;
	ID3D11DeviceContext *context = enc->context;
	ComPtr<ID3D11Texture2D> output_tex;
	ComPtr<ID3D11Texture2D> input_tex;
	ComPtr<IDXGIKeyedMutex> km;
	AMFSurfacePtr amf_surf;
	AMF_RESULT res;

	if (handle == GS_INVALID_HANDLE) {
		*next_key = lock_key;
		throw "Encode failed: bad texture handle";
	}

	/* ------------------------------------ */
	/* get the input tex                    */

	get_tex_from_handle(enc, handle, &km, &input_tex);

	/* ------------------------------------ */
	/* get an output tex                    */

	get_output_tex(enc, output_tex, input_tex);

	/* ------------------------------------ */
	/* copy to output tex                   */

	km->AcquireSync(lock_key, INFINITE);
	context->CopyResource((ID3D11Resource *)output_tex.Get(), (ID3D11Resource *)input_tex.Get());
	context->Flush();
	km->ReleaseSync(*next_key);

	/* ------------------------------------ */
	/* map output tex to amf surface        */

	res = enc->amf_context->CreateSurfaceFromDX11Native(output_tex, &amf_surf, enc);
	if (res != AMF_OK)
		throw amf_error("CreateSurfaceFromDX11Native failed", res);

	int64_t last_ts = convert_to_amf_ts(enc, pts - 1);
	int64_t cur_ts = convert_to_amf_ts(enc, pts);

	amf_surf->SetPts(cur_ts);
	amf_surf->SetProperty(L"PTS", pts);

	{
		std::scoped_lock lock(enc->textures_mutex);
		enc->active_textures[amf_surf.GetPtr()] = output_tex;
	}

	/* ------------------------------------ */
	/* do actual encode                     */

	amf_encode_base(enc, amf_surf, packet, received_packet);
	return true;

} catch (const char *err) {
	amf_texencode *enc = (amf_texencode *)data;
	error("%s: %s", __FUNCTION__, err);
	return false;

} catch (const amf_error &err) {
	amf_texencode *enc = (amf_texencode *)data;
	error("%s: %s: %ls", __FUNCTION__, err.str, amf_trace->GetResultText(err.res));
	*received_packet = false;
	return false;

} catch (const HRError &err) {
	amf_texencode *enc = (amf_texencode *)data;
	error("%s: %s: 0x%lX", __FUNCTION__, err.str, err.hr);
	*received_packet = false;
	return false;
}
#else
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(handle);
	UNUSED_PARAMETER(pts);
	UNUSED_PARAMETER(lock_key);
	UNUSED_PARAMETER(next_key);
	UNUSED_PARAMETER(packet);
	UNUSED_PARAMETER(received_packet);
	return false;
}
#endif

static bool amf_encode_tex2(void *data, encoder_texture *texture, int64_t pts, uint64_t lock_key, uint64_t *next_key,
			    encoder_packet *packet, bool *received_packet)
try {
	UNUSED_PARAMETER(lock_key);
	UNUSED_PARAMETER(next_key);

	amf_texencode *enc = (amf_texencode *)data;
	return enc->encode(texture, pts, packet, received_packet);

} catch (const char *err) {
	amf_texencode *enc = (amf_texencode *)data;
	error("%s: %s", __FUNCTION__, err);
	*received_packet = false;
	return false;

} catch (const amf_error &err) {
	amf_texencode *enc = (amf_texencode *)data;
	error("%s: %s: %ls", __FUNCTION__, err.str, amf_trace->GetResultText(err.res));
	*received_packet = false;
	return false;
}

static buf_t alloc_buf(amf_fallback *enc)
{
	buf_t buf;
	size_t size;

	if (enc->amf_format == AMF_SURFACE_NV12) {
		size = enc->linesize * enc->cy * 2;
	} else if (enc->amf_format == AMF_SURFACE_RGBA) {
		size = enc->linesize * enc->cy * 4;
	} else if (enc->amf_format == AMF_SURFACE_P010) {
		size = enc->linesize * enc->cy * 2 * 2;
	} else {
		throw "Invalid amf_format";
	}

	buf.resize(size);
	return buf;
}

static buf_t get_buf(amf_fallback *enc)
{
	std::scoped_lock lock(enc->buffers_mutex);
	buf_t buf;

	if (enc->available_buffers.size()) {
		buf = std::move(enc->available_buffers.back());
		enc->available_buffers.pop_back();
	} else {
		buf = alloc_buf(enc);
	}

	return buf;
}

static inline void copy_frame_data(amf_fallback *enc, buf_t &buf, struct encoder_frame *frame)
{
	uint8_t *dst = &buf[0];

	if (enc->amf_format == AMF_SURFACE_NV12 || enc->amf_format == AMF_SURFACE_P010) {
		size_t size = enc->linesize * enc->cy;
		memcpy(&buf[0], frame->data[0], size);
		memcpy(&buf[size], frame->data[1], size / 2);

	} else if (enc->amf_format == AMF_SURFACE_RGBA) {
		memcpy(dst, frame->data[0], enc->linesize * enc->cy);
	}
}

static bool amf_encode_fallback(void *data, struct encoder_frame *frame, struct encoder_packet *packet,
				bool *received_packet)
try {
	amf_fallback *enc = (amf_fallback *)data;
	AMFSurfacePtr amf_surf;
	AMF_RESULT res;

	if (!enc->linesize)
		enc->linesize = frame->linesize[0];

#if __USE_OPENCL
	if (enc->amfCompute) {
		// Copy the frame to an OpenCL surface (instead of host memory),
		// then convert it to Vulkan

		res = enc->amf_context->AllocSurface(amf::AMF_MEMORY_OPENCL, enc->amf_format, enc->cx, enc->cy,
						     &amf_surf);
		if (res != AMF_OK)
			throw amf_error("AllocSurface failed", res);

		amf_size planesCount = amf_surf->GetPlanesCount();
		for (uint8_t i = 0; i < planesCount; i++) {
			amf::AMFPlanePtr plane = amf_surf->GetPlaneAt(i);
			static const amf_size l_origin[] = {0, 0, 0};
			const amf_size l_size[] = {(amf_size)plane->GetWidth(), (amf_size)plane->GetHeight(), 1};

			res = enc->amfCompute->CopyPlaneFromHost(frame->data[i], l_origin, l_size, frame->linesize[i],
								 plane, false);
			if (res != AMF_OK)
				throw amf_error("CopyPlaneFromHost failed", res);
		}

		int64_t last_ts = convert_to_amf_ts(enc, frame->pts - 1);
		int64_t cur_ts = convert_to_amf_ts(enc, frame->pts);
		amf_surf->SetPts(cur_ts);
		amf_surf->SetProperty(L"PTS", frame->pts);
		amf_encode_base(enc, amf_surf, packet, received_packet);
		return true;
	}
#endif

	buf_t buf = get_buf(enc);

	copy_frame_data(enc, buf, frame);

	res = enc->amf_context->CreateSurfaceFromHostNative(enc->amf_format, enc->cx, enc->cy, enc->linesize, 0,
							    &buf[0], &amf_surf, enc);
	if (res != AMF_OK)
		throw amf_error("CreateSurfaceFromHostNative failed", res);

	int64_t last_ts = convert_to_amf_ts(enc, frame->pts - 1);
	int64_t cur_ts = convert_to_amf_ts(enc, frame->pts);

	amf_surf->SetPts(cur_ts);
	amf_surf->SetProperty(L"PTS", frame->pts);

	{
		std::scoped_lock lock(enc->buffers_mutex);
		enc->active_buffers[amf_surf.GetPtr()] = std::move(buf);
	}

	/* ------------------------------------ */
	/* do actual encode                     */

	amf_encode_base(enc, amf_surf, packet, received_packet);
	return true;

} catch (const amf_error &err) {
	amf_fallback *enc = (amf_fallback *)data;
	error("%s: %s: %ls", __FUNCTION__, err.str, amf_trace->GetResultText(err.res));
	*received_packet = false;
	return false;
} catch (const char *err) {
	amf_fallback *enc = (amf_fallback *)data;
	error("%s: %s", __FUNCTION__, err);
	*received_packet = false;
	return false;
}

static bool amf_extra_data(void *data, uint8_t **header, size_t *size)
{
	amf_base *enc = (amf_base *)data;
	if (!enc->header)
		return false;

	*header = (uint8_t *)enc->header->GetNative();
	*size = enc->header->GetSize();
	return true;
}

static void h264_video_info_fallback(void *, struct video_scale_info *info)
{
	switch (info->format) {
	case VIDEO_FORMAT_RGBA:
	case VIDEO_FORMAT_BGRA:
	case VIDEO_FORMAT_BGRX:
		info->format = VIDEO_FORMAT_RGBA;
		break;
	default:
		info->format = VIDEO_FORMAT_NV12;
		break;
	}
}

static void h265_video_info_fallback(void *, struct video_scale_info *info)
{
	switch (info->format) {
	case VIDEO_FORMAT_RGBA:
	case VIDEO_FORMAT_BGRA:
	case VIDEO_FORMAT_BGRX:
		info->format = VIDEO_FORMAT_RGBA;
		break;
	case VIDEO_FORMAT_I010:
	case VIDEO_FORMAT_P010:
		info->format = VIDEO_FORMAT_P010;
		break;
	default:
		info->format = VIDEO_FORMAT_NV12;
	}
}

static void av1_video_info_fallback(void *, struct video_scale_info *info)
{
	switch (info->format) {
	case VIDEO_FORMAT_RGBA:
	case VIDEO_FORMAT_BGRA:
	case VIDEO_FORMAT_BGRX:
		info->format = VIDEO_FORMAT_RGBA;
		break;
	case VIDEO_FORMAT_I010:
	case VIDEO_FORMAT_P010:
		info->format = VIDEO_FORMAT_P010;
		break;
	default:
		info->format = VIDEO_FORMAT_NV12;
	}
}

static bool amf_create_encoder(amf_base *enc)
try {
	AMF_RESULT res;

	/* ------------------------------------ */
	/* get video info                       */

	struct video_scale_info info;
	video_t *video = obs_encoder_video(enc->encoder);
	const struct video_output_info *voi = video_output_get_info(video);
	info.format = voi->format;
	info.colorspace = voi->colorspace;
	info.range = voi->range;

	if (enc->fallback) {
		if (enc->codec == amf_codec_type::AVC)
			h264_video_info_fallback(NULL, &info);
		else if (enc->codec == amf_codec_type::HEVC)
			h265_video_info_fallback(NULL, &info);
		else
			av1_video_info_fallback(NULL, &info);
	}

	enc->cx = obs_encoder_get_width(enc->encoder);
	enc->cy = obs_encoder_get_height(enc->encoder);
	enc->amf_frame_rate = AMFConstructRate(voi->fps_num, voi->fps_den);
	enc->fps_num = (int)voi->fps_num;
	enc->fps_den = (int)voi->fps_den;
	enc->full_range = info.range == VIDEO_RANGE_FULL;

	switch (info.colorspace) {
	case VIDEO_CS_601:
		enc->amf_color_profile = enc->full_range ? AMF_VIDEO_CONVERTER_COLOR_PROFILE_FULL_601
							 : AMF_VIDEO_CONVERTER_COLOR_PROFILE_601;
		enc->amf_primaries = AMF_COLOR_PRIMARIES_SMPTE170M;
		enc->amf_characteristic = AMF_COLOR_TRANSFER_CHARACTERISTIC_SMPTE170M;
		break;
	case VIDEO_CS_DEFAULT:
	case VIDEO_CS_709:
		enc->amf_color_profile = enc->full_range ? AMF_VIDEO_CONVERTER_COLOR_PROFILE_FULL_709
							 : AMF_VIDEO_CONVERTER_COLOR_PROFILE_709;
		enc->amf_primaries = AMF_COLOR_PRIMARIES_BT709;
		enc->amf_characteristic = AMF_COLOR_TRANSFER_CHARACTERISTIC_BT709;
		break;
	case VIDEO_CS_SRGB:
		enc->amf_color_profile = enc->full_range ? AMF_VIDEO_CONVERTER_COLOR_PROFILE_FULL_709
							 : AMF_VIDEO_CONVERTER_COLOR_PROFILE_709;
		enc->amf_primaries = AMF_COLOR_PRIMARIES_BT709;
		enc->amf_characteristic = AMF_COLOR_TRANSFER_CHARACTERISTIC_IEC61966_2_1;
		break;
	case VIDEO_CS_2100_HLG:
		enc->amf_color_profile = enc->full_range ? AMF_VIDEO_CONVERTER_COLOR_PROFILE_FULL_2020
							 : AMF_VIDEO_CONVERTER_COLOR_PROFILE_2020;
		enc->amf_primaries = AMF_COLOR_PRIMARIES_BT2020;
		enc->amf_characteristic = AMF_COLOR_TRANSFER_CHARACTERISTIC_ARIB_STD_B67;
		break;
	case VIDEO_CS_2100_PQ:
		enc->amf_color_profile = enc->full_range ? AMF_VIDEO_CONVERTER_COLOR_PROFILE_FULL_2020
							 : AMF_VIDEO_CONVERTER_COLOR_PROFILE_2020;
		enc->amf_primaries = AMF_COLOR_PRIMARIES_BT2020;
		enc->amf_characteristic = AMF_COLOR_TRANSFER_CHARACTERISTIC_SMPTE2084;
		break;
	}

	switch (info.format) {
	case VIDEO_FORMAT_NV12:
		enc->amf_format = AMF_SURFACE_NV12;
		break;
	case VIDEO_FORMAT_P010:
		enc->amf_format = AMF_SURFACE_P010;
		break;
	case VIDEO_FORMAT_RGBA:
		enc->amf_format = AMF_SURFACE_RGBA;
		break;
	}

	/* ------------------------------------ */
	/* create encoder                       */

	res = amf_factory->CreateContext(&enc->amf_context);
	if (res != AMF_OK)
		throw amf_error("CreateContext failed", res);

#ifdef __linux__
	enc->amf_context1 = AMFContext1Ptr(enc->amf_context);
#endif

	enc->init();

	const wchar_t *codec = nullptr;
	switch (enc->codec) {
	default:
	case (amf_codec_type::AVC):
		codec = AMFVideoEncoderVCE_AVC;
		enc->output_data_type_key = AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE;
		break;
	case (amf_codec_type::HEVC):
		codec = AMFVideoEncoder_HEVC;
		enc->output_data_type_key = AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE;
		break;
	case (amf_codec_type::AV1):
		codec = AMFVideoEncoder_AV1;
		enc->output_data_type_key = AMF_VIDEO_ENCODER_AV1_OUTPUT_FRAME_TYPE;
		break;
	}
	res = amf_factory->CreateComponent(enc->amf_context, codec, &enc->amf_encoder);
	if (res != AMF_OK)
		throw amf_error("CreateComponent failed", res);

	calc_throughput(enc);
	return true;

} catch (const amf_error &err) {
	error("%s: %s: %ls", __FUNCTION__, err.str, amf_trace->GetResultText(err.res));
	return false;
}

static void amf_destroy(void *data)
{
	amf_base *enc = (amf_base *)data;
	delete enc;
}

static void check_texture_encode_capability(obs_encoder_t *encoder, amf_codec_type codec)
{
	obs_video_info ovi;
	obs_get_video_info(&ovi);
	bool avc = amf_codec_type::AVC == codec;
	bool hevc = amf_codec_type::HEVC == codec;
	bool av1 = amf_codec_type::AV1 == codec;

	if (obs_encoder_scaling_enabled(encoder) && !obs_encoder_gpu_scaling_enabled(encoder))
		throw "Encoder scaling is active";

	if (hevc || av1) {
		if (!obs_encoder_video_tex_active(encoder, VIDEO_FORMAT_NV12) &&
		    !obs_encoder_video_tex_active(encoder, VIDEO_FORMAT_P010))
			throw "NV12/P010 textures aren't active";
	} else if (!obs_encoder_video_tex_active(encoder, VIDEO_FORMAT_NV12)) {
		throw "NV12 textures aren't active";
	}

	video_t *video = obs_encoder_video(encoder);
	const struct video_output_info *voi = video_output_get_info(video);
	switch (voi->format) {
	case VIDEO_FORMAT_I010:
	case VIDEO_FORMAT_P010:
		break;
	default:
		switch (voi->colorspace) {
		case VIDEO_CS_2100_PQ:
		case VIDEO_CS_2100_HLG:
			throw "OBS does not support 8-bit output of Rec. 2100";
		}
	}

	if ((avc && !caps[ovi.adapter].supports_avc) || (hevc && !caps[ovi.adapter].supports_hevc) ||
	    (av1 && !caps[ovi.adapter].supports_av1))
		throw "Wrong adapter";
}

#include "texture-amf-opts.hpp"

static void amf_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "bitrate", 2500);
	obs_data_set_default_int(settings, "cqp", 20);
	obs_data_set_default_string(settings, "rate_control", "CBR");
	obs_data_set_default_string(settings, "preset", "quality");
	obs_data_set_default_string(settings, "profile", "high");
	obs_data_set_default_int(settings, "bf", 3);

	obs_data_set_default_bool(settings, "pre_encode", true);
	obs_data_set_default_bool(settings, "adaptive_quality", true);
	obs_data_set_default_string(settings, "pa_lookahead", "medium");
	obs_data_set_default_string(settings, "pa_caq", "medium");
}

static bool on_setting_modified(obs_properties_t *ppts, obs_property_t *p, obs_data_t *settings)
{
	const char *rc = obs_data_get_string(settings, "rate_control");
	bool cqp = astrcmpi(rc, "CQP") == 0;
	bool qvbr = astrcmpi(rc, "QVBR") == 0;

	p = obs_properties_get(ppts, "bitrate");
	obs_property_set_visible(p, !cqp && !qvbr);
	p = obs_properties_get(ppts, "cqp");
	obs_property_set_visible(p, cqp || qvbr);

	bool pre_analysis_available = obs_properties_get(ppts, "pre_analysis") != nullptr;
	bool pre_analysis = pre_analysis_available && obs_data_get_bool(settings, "pre_analysis");

	if (pre_analysis_available) {
		for (const char *name : {"pa_lookahead", "pa_caq"}) {
			p = obs_properties_get(ppts, name);
			obs_property_set_visible(p, pre_analysis);
		}

		// HMQB works with Pre-Analysis off
		p = obs_properties_get(ppts, "hmqb");
		obs_property_set_visible(p, !pre_analysis);

		// TAQ only works with lookahead >= medium
		bool sufficient_lookahead = false;
		if (pre_analysis) {
			const char *lookahead = obs_data_get_string(settings, "pa_lookahead");
			sufficient_lookahead = !strcmp(lookahead, "medium") || !strcmp(lookahead, "long");
		}
		p = obs_properties_get(ppts, "pa_taq");
		obs_property_set_visible(p, sufficient_lookahead);
	}

	// VBAQ only works with RC != CQP, and CAQ/TAQ disabled
	p = obs_properties_get(ppts, "adaptive_quality");
	bool adaptive_quality = !cqp;
	if (adaptive_quality && pre_analysis) {
		bool caq = strcmp("none", obs_data_get_string(settings, "pa_caq"));
		bool taq = obs_property_visible(obs_properties_get(ppts, "pa_taq")) &&
			   strcmp("none", obs_data_get_string(settings, "pa_taq"));
		adaptive_quality = !(caq || taq);
	}
	obs_property_set_visible(p, adaptive_quality);

	return true;
}

static bool amf_get_caps(amf_codec_type codec, AMFCaps **caps)
{
	AMFContextPtr context;
	if (amf_factory->CreateContext(&context) != AMF_OK)
		return false;
	const wchar_t *amf_codec = codec == amf_codec_type::HEVC  ? AMFVideoEncoder_HEVC
				   : codec == amf_codec_type::AV1 ? AMFVideoEncoder_AV1
								  : AMFVideoEncoderVCE_AVC;
#if defined(_WIN32)
	if (context->InitDX11(nullptr, AMF_DX11_1) != AMF_OK)
		return false;
#elif defined(__linux__)
	AMFContext1 *context1 = NULL;
	if (context->QueryInterface(AMFContext1::IID(), (void **)&context1) != AMF_OK)
		return false;
	if (context1->InitVulkan(nullptr) != AMF_OK)
		return false;
#endif
	AMFComponentPtr component;
	if (amf_factory->CreateComponent(context, amf_codec, &component) != AMF_OK)
		return false;
	return component->GetCaps(caps) == AMF_OK;
}

static obs_properties_t *amf_properties_internal(amf_codec_type codec)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *p;

	p = obs_properties_add_list(props, "rate_control", obs_module_text("RateControl"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, "CBR", "CBR");
	obs_property_list_add_string(p, "CQP", "CQP");
	obs_property_list_add_string(p, "VBR", "VBR");
	obs_property_list_add_string(p, "VBR_LAT", "VBR_LAT");
	obs_property_list_add_string(p, "QVBR", "QVBR");
	obs_property_list_add_string(p, "HQVBR", "HQVBR");
	obs_property_list_add_string(p, "HQCBR", "HQCBR");

	obs_property_set_modified_callback(p, on_setting_modified);

	p = obs_properties_add_int(props, "bitrate", obs_module_text("Bitrate"), 50, 100000, 50);
	obs_property_int_set_suffix(p, " Kbps");

	obs_properties_add_int(props, "cqp", obs_module_text("NVENC.CQLevel"), 0,
			       codec == amf_codec_type::AV1 ? 63 : 51, 1);

	p = obs_properties_add_int(props, "keyint_sec", obs_module_text("KeyframeIntervalSec"), 0, 10, 1);
	obs_property_int_set_suffix(p, " s");

	p = obs_properties_add_list(props, "preset", obs_module_text("Preset"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);

#define add_preset(val) obs_property_list_add_string(p, obs_module_text("AMF.Preset." val), val)
	if (amf_codec_type::AV1 == codec) {
		add_preset("highQuality");
	}
	add_preset("quality");
	add_preset("balanced");
	add_preset("speed");
#undef add_preset

	if (amf_codec_type::AVC == codec || amf_codec_type::AV1 == codec) {
		p = obs_properties_add_list(props, "profile", obs_module_text("Profile"), OBS_COMBO_TYPE_LIST,
					    OBS_COMBO_FORMAT_STRING);

#define add_profile(val) obs_property_list_add_string(p, val, val)
		if (amf_codec_type::AVC == codec)
			add_profile("high");
		add_profile("main");
		if (amf_codec_type::AVC == codec)
			add_profile("baseline");
#undef add_profile
	}

	bool bframes = false;
	bool pre_analysis = false;
	AMFCapsPtr caps;
	if (amf_get_caps(codec, &caps)) {
		if (codec != amf_codec_type::HEVC)
			caps->GetProperty(codec == amf_codec_type::AV1 ? AMF_VIDEO_ENCODER_AV1_CAP_BFRAMES
								       : AMF_VIDEO_ENCODER_CAP_BFRAMES,
					  &bframes);
		caps->GetProperty(codec == amf_codec_type::AV1    ? AMF_VIDEO_ENCODER_AV1_CAP_PRE_ANALYSIS
				  : codec == amf_codec_type::HEVC ? AMF_VIDEO_ENCODER_HEVC_CAP_PRE_ANALYSIS
								  : AMF_VIDEO_ENCODER_CAP_PRE_ANALYSIS,
				  &pre_analysis);
	}

	if (bframes)
		obs_properties_add_int(props, "bf", obs_module_text("BFrames"), 0, 5, 1);

	obs_properties_add_bool(props, "low_latency", "Low Latency");
	obs_properties_add_bool(props, "pre_encode", "Pre-Encode Filter");
	obs_properties_add_bool(props, "adaptive_quality",
				codec == amf_codec_type::AV1 ? "Adaptive Quality"
							     : "VBAQ (Variance-Based Adaptive Quality)");
	obs_properties_add_bool(props, "hmqb", "High-Motion Quality Boost");

	if (pre_analysis) {
		p = obs_properties_add_bool(props, "pre_analysis", "Pre-Analysis");
		obs_property_set_modified_callback(p, on_setting_modified);

		p = obs_properties_add_list(props, "pa_lookahead", "Lookahead", OBS_COMBO_TYPE_LIST,
					    OBS_COMBO_FORMAT_STRING);
		obs_property_list_add_string(p, "None", "none");
		obs_property_list_add_string(p, "Short", "short");
		obs_property_list_add_string(p, "Medium", "medium");
		obs_property_list_add_string(p, "Long", "long");
		obs_property_set_modified_callback(p, on_setting_modified);

		p = obs_properties_add_list(props, "pa_caq", "CAQ (Content Adaptive Quantization)", OBS_COMBO_TYPE_LIST,
					    OBS_COMBO_FORMAT_STRING);
		obs_property_list_add_string(p, "None", "none");
		obs_property_list_add_string(p, "Low", "low");
		obs_property_list_add_string(p, "Medium", "medium");
		obs_property_list_add_string(p, "High", "high");
		obs_property_set_modified_callback(p, on_setting_modified);

		p = obs_properties_add_list(props, "pa_taq", "TAQ (Temporal Adaptive Quantization)",
					    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
		obs_property_list_add_string(p, "None", "none");
		obs_property_list_add_string(p, "Mode 1", "mode1");
		obs_property_list_add_string(p, "Mode 2", "mode2");
		obs_property_set_modified_callback(p, on_setting_modified);
	}

	p = obs_properties_add_text(props, "ffmpeg_opts", obs_module_text("AMFOpts"), OBS_TEXT_MULTILINE);
	obs_property_set_long_description(p, obs_module_text("AMFOpts.ToolTip"));

	return props;
}

static obs_properties_t *amf_avc_properties(void *unused)
{
	UNUSED_PARAMETER(unused);
	return amf_properties_internal(amf_codec_type::AVC);
}

static obs_properties_t *amf_hevc_properties(void *unused)
{
	UNUSED_PARAMETER(unused);
	return amf_properties_internal(amf_codec_type::HEVC);
}

static obs_properties_t *amf_av1_properties(void *unused)
{
	UNUSED_PARAMETER(unused);
	return amf_properties_internal(amf_codec_type::AV1);
}

/* ========================================================================= */
/* AVC Implementation                                                        */

static const char *amf_avc_get_name(void *)
{
	return "AMD HW H.264 (AVC)";
}

static inline int get_avc_preset(amf_base *enc, const char *preset)
{
	UNUSED_PARAMETER(enc);
	if (astrcmpi(preset, "quality") == 0)
		return AMF_VIDEO_ENCODER_QUALITY_PRESET_QUALITY;
	else if (astrcmpi(preset, "speed") == 0)
		return AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED;

	return AMF_VIDEO_ENCODER_QUALITY_PRESET_BALANCED;
}

static inline int get_avc_rate_control(const char *rc_str)
{
	if (astrcmpi(rc_str, "cqp") == 0)
		return AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP;
	else if (astrcmpi(rc_str, "cbr") == 0)
		return AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR;
	else if (astrcmpi(rc_str, "vbr") == 0)
		return AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR;
	else if (astrcmpi(rc_str, "vbr_lat") == 0)
		return AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR;
	else if (astrcmpi(rc_str, "qvbr") == 0)
		return AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_QUALITY_VBR;
	else if (astrcmpi(rc_str, "hqvbr") == 0)
		return AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_HIGH_QUALITY_VBR;
	else if (astrcmpi(rc_str, "hqcbr") == 0)
		return AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_HIGH_QUALITY_CBR;

	return AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR;
}

static inline int get_avc_profile(const char *profile)
{
	if (astrcmpi(profile, "baseline") == 0)
		return AMF_VIDEO_ENCODER_PROFILE_BASELINE;
	else if (astrcmpi(profile, "main") == 0)
		return AMF_VIDEO_ENCODER_PROFILE_MAIN;
	else if (astrcmpi(profile, "constrained_baseline") == 0)
		return AMF_VIDEO_ENCODER_PROFILE_CONSTRAINED_BASELINE;
	else if (astrcmpi(profile, "constrained_high") == 0)
		return AMF_VIDEO_ENCODER_PROFILE_CONSTRAINED_HIGH;

	return AMF_VIDEO_ENCODER_PROFILE_HIGH;
}

struct amf_settings {
	int bitrate;
	int qp;
	const char *preset;
	const char *profile;
	const char *rc;
	int bf;
	int keyint_sec;
	int gop_size;
	const char *ffmpeg_opts;
	std::shared_ptr<char[]> condensed_opts;
	const char *level;
	obs_data_t *settings;
};

static amf_settings get_amf_settings(amf_base *enc, obs_data_t *settings)
{
	const char *rc_str = obs_data_get_string(settings, "rate_control");
	int keyint_sec = (int)obs_data_get_int(settings, "keyint_sec");

	amf_settings amf_settings = {
		.bitrate = (int)obs_data_get_int(settings, "bitrate"),
		.qp = (int)obs_data_get_int(settings, "cqp"),
		.preset = obs_data_get_string(settings, "preset"),
		.profile = obs_data_get_string(settings, "profile"),
		.rc = obs_data_get_string(settings, "rate_control"),
		.bf = (int)obs_data_get_int(settings, "bf"),
		.keyint_sec = keyint_sec,
		.gop_size = (keyint_sec) ? keyint_sec * enc->fps_num / enc->fps_den : 250,
		.ffmpeg_opts = obs_data_get_string(settings, "ffmpeg_opts"),
		.settings = settings,
	};

	const char *opts = amf_settings.ffmpeg_opts;
	char *condensed_opts = new char[strlen(opts) + 1];
	obs_data_condense_whitespace(opts, condensed_opts);
	amf_settings.condensed_opts = std::shared_ptr<char[]>(condensed_opts);

	return amf_settings;
}

static bool set_pre_analysis(amf_base *enc, obs_data_t *settings)
{
	if (!enc->pre_analysis_supported)
		return false;

	bool enabled = obs_data_get_bool(settings, "pre_analysis");
	if (!enc->header)
		// For some reason this complains if we're already initialized
		// Hopefully eventually we can update this while encoding
		set_opt(PRE_ANALYSIS_ENABLE, enabled);
	if (!enabled)
		return false;

	const char *lookahead = obs_data_get_string(settings, "pa_lookahead");
	int lookahead_depth = 0;
	if (strcmp(lookahead, "none")) {
		lookahead_depth = !strcmp(lookahead, "long") ? 41 : !strcmp(lookahead, "medium") ? 21 : 11;
		set_amf_property(enc, AMF_PA_LOOKAHEAD_BUFFER_DEPTH, lookahead_depth);
	}

	const char *caq = obs_data_get_string(settings, "pa_caq");
	if (strcmp(caq, "none")) {
		set_amf_property(enc, AMF_PA_PAQ_MODE, AMF_PA_PAQ_MODE_CAQ);
		int strength = !strcmp(caq, "high")     ? AMF_PA_CAQ_STRENGTH_HIGH
			       : !strcmp(caq, "medium") ? AMF_PA_CAQ_STRENGTH_MEDIUM
							: AMF_PA_CAQ_STRENGTH_LOW;
		set_amf_property(enc, AMF_PA_CAQ_STRENGTH, strength);
	}

	if (lookahead_depth >= 21) {
		const char *taq = obs_data_get_string(settings, "pa_taq");
		if (strcmp(taq, "none")) {
			int mode = !strcmp(taq, "mode2") ? AMF_PA_TAQ_MODE_2 : AMF_PA_TAQ_MODE_1;
			set_amf_property(enc, AMF_PA_TAQ_MODE, mode);
		}
	}

	return true;
}

static bool should_enable_adaptive_quality(amf_base *enc, obs_data_t *settings, const char *rc, bool pre_analysis)
{
	if (!strcmp(rc, "cqp") || !obs_data_get_bool(settings, "adaptive_quality"))
		return false;
	if (pre_analysis) {
		int paq, taq;
		get_amf_property(enc, AMF_PA_PAQ_MODE, &paq);
		get_amf_property(enc, AMF_PA_TAQ_MODE, &taq);
		return !(paq || taq);
	}
	return true;
}

// Forward reference
static bool amf_get_level_str(amf_base *enc, amf_int64 level, char const **level_str);

static void amf_apply_opts(amf_base *enc, amf_settings &settings)
{
	char *s = settings.condensed_opts.get();
	if (!(*s))
		return;
	struct obs_options opts = obs_parse_options(s);
	for (size_t i = 0; i < opts.count; i++)
		amf_apply_opt(enc, &opts.options[i]);
	obs_free_options(opts);
}

static void amf_avc_update_data(amf_base *enc, amf_settings &settings)
{
	set_avc_property(enc, QUALITY_PRESET, get_avc_preset(enc, settings.preset));
	set_avc_property(enc, PROFILE, get_avc_profile(settings.profile));

	obs_data_t *obs_settings = settings.settings;
	set_avc_property(enc, LOWLATENCY_MODE, obs_data_get_bool(obs_settings, "low_latency"));
	set_avc_property(enc, PREENCODE_ENABLE, obs_data_get_bool(obs_settings, "pre_encode"));

	int rc = get_avc_rate_control(settings.rc);
	set_avc_property(enc, RATE_CONTROL_METHOD, rc);
	if (rc != AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CONSTANT_QP &&
	    rc != AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_QUALITY_VBR) {
		int bitrate = settings.bitrate * 1000;
		set_avc_property(enc, TARGET_BITRATE, bitrate);
		set_avc_property(enc, PEAK_BITRATE, bitrate);
		set_avc_property(enc, VBV_BUFFER_SIZE, bitrate);

		if (rc == AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR) {
			set_avc_property(enc, FILLER_DATA_ENABLE, true);
		}
	} else {
		int qp = settings.qp;
		set_avc_property(enc, QP_I, qp);
		set_avc_property(enc, QP_P, qp);
		set_avc_property(enc, QP_B, qp);
		set_avc_property(enc, QVBR_QUALITY_LEVEL, qp);
	}

	int gop_size = settings.gop_size;
	set_avc_property(enc, IDR_PERIOD, gop_size);

	bool repeat_headers = obs_data_get_bool(obs_settings, "repeat_headers");
	set_avc_property(enc, HEADER_INSERTION_SPACING, repeat_headers ? gop_size : 0);

	int bf = settings.bf;
	if (enc->bframes_supported) {
		set_avc_property(enc, MAX_CONSECUTIVE_BPICTURES, bf);
		set_avc_property(enc, B_PIC_PATTERN, bf);

		/* AdaptiveMiniGOP is suggested for some types of content such
		 * as those with high motion. This only takes effect if
		 * Pre-Analysis is enabled.
		 */
		set_avc_property(enc, ADAPTIVE_MINIGOP, bf > 0);

		amf_int64 b_frames = 0;
		amf_int64 b_max = 0;

		if (get_avc_property(enc, B_PIC_PATTERN, &b_frames) &&
		    get_avc_property(enc, MAX_CONSECUTIVE_BPICTURES, &b_max))
			enc->dts_offset = b_frames + 1;
		else
			enc->dts_offset = 0;

	} else if (bf != 0) {
		warn("B-Frames set to %d but b-frames are not "
		     "supported by this device",
		     bf);
		settings.bf = 0;
	}

	bool pre_analysis = set_pre_analysis(enc, obs_settings);

	bool vbaq = should_enable_adaptive_quality(enc, obs_settings, settings.rc, pre_analysis);
	set_avc_property(enc, ENABLE_VBAQ, vbaq);

	bool hmqb = !pre_analysis && obs_data_get_bool(obs_settings, "hmqb");
	set_avc_property(enc, HIGH_MOTION_QUALITY_BOOST_ENABLE, hmqb);

	amf_apply_opts(enc, settings);

	/* The ffmpeg_opts just above may have explicitly set the AVC level to a value different than what was
	* determined by amf_set_codec_level(). Query the final AVC level then lookup the matching string. Warn if not
	* found, because ffmpeg_opts is free-form and may have set something bogus.
	*/
	amf_int64 final_level;
	get_avc_property(enc, PROFILE_LEVEL, &final_level);
	if (!amf_get_level_str(enc, final_level, &settings.level))
		warn("AVC level string not found. Level %d may be incorrect.", final_level);
}

static bool amf_avc_update(void *data, obs_data_t *settings)
try {
	amf_base *enc = (amf_base *)data;

	// Is this needed for anything?
	// It ignores the first settings change after start, every time.
	// if (enc->first_update) {
	// 	enc->first_update = false;
	// 	return true;
	// }

	AMFComponentPtr encoder = enc->amf_encoder;

#ifndef OBS_AMF_DISABLE_PROPERTIES
	property_values_t old_values = amf_property_values(encoder, amf_avc_property_types);
#endif

	amf_settings amf_settings = get_amf_settings(enc, settings);
	amf_avc_update_data(enc, amf_settings);
	enc->reinitialize();

#ifndef OBS_AMF_DISABLE_PROPERTIES
	std::stringstream ss;
	amf_print_changed_property_values(ss, encoder, amf_avc_property_types, old_values);
	if (ss.tellp())
		info("updated properties:\n%s", ss.str().c_str());
#endif

	return true;

} catch (const amf_error &err) {
	amf_base *enc = (amf_base *)data;
	error("%s: %s: %ls", __FUNCTION__, err.str, amf_trace->GetResultText(err.res));
	return false;
}

static void amf_set_codec_level(amf_base *enc)
{
	uint64_t luma_pic_size = enc->cx * enc->cy;
	uint64_t luma_sample_rate = luma_pic_size * (enc->fps_num / enc->fps_den);
	std::vector<codec_level_entry> *levels;

	if (enc->codec == amf_codec_type::AVC) {
		levels = &avc_levels;
	} else if (enc->codec == amf_codec_type::HEVC) {
		levels = &hevc_levels;
	} else if (enc->codec == amf_codec_type::AV1) {
		levels = &av1_levels;
	} else {
		blog(LOG_ERROR, "%s: Unknown amf_codec_type", __FUNCTION__);
		return;
	}

	std::vector<codec_level_entry>::const_iterator level_it = levels->begin();

	// First check if the requested sample rate and/or picture size is too large for the maximum level.
	if ((luma_sample_rate > levels->back().max_luma_sample_rate) ||
	    (luma_pic_size > levels->back().max_luma_picture_size)) {
		/* If the calculated sample rate is greater than the highest value supported by the codec, clamp to the
		 * upper limit and log an error.
		 */
		level_it = --(levels->end());
		blog(LOG_ERROR,
		     "%s: Luma sample rate %u or luma pic size %u is greater than maximum "
		     "allowed. Setting to level %s.",
		     __FUNCTION__, luma_sample_rate, luma_pic_size, level_it->level_str);
	} else {
		// Walk the table and find the lowest codec level value suitable for the given luma sample rate.
		while (level_it != levels->end()) {
			if ((luma_sample_rate <= level_it->max_luma_sample_rate) &&
			    (luma_pic_size <= level_it->max_luma_picture_size)) {
				break;
			}
			++level_it;
		}
	}

	// Set the level for the encoder
	if (enc->codec == amf_codec_type::AVC) {
		set_avc_property(enc, PROFILE_LEVEL, level_it->amf_level);
	} else if (enc->codec == amf_codec_type::HEVC) {
		set_hevc_property(enc, PROFILE_LEVEL, level_it->amf_level);
	} else if (enc->codec == amf_codec_type::AV1) {
		set_av1_property(enc, LEVEL, level_it->amf_level);
	}
}

static bool amf_get_level_str(amf_base *enc, amf_int64 level, char const **level_str)
{
	bool found = false;
	std::vector<codec_level_entry> *levels;

	if (enc->codec == amf_codec_type::AVC) {
		levels = &avc_levels;
	} else if (enc->codec == amf_codec_type::HEVC) {
		levels = &hevc_levels;
	} else if (enc->codec == amf_codec_type::AV1) {
		levels = &av1_levels;
	} else {
		blog(LOG_ERROR, "%s: Unknown amf_codec_type", __FUNCTION__);
		return false;
	}

	for (auto level_it = levels->begin(); level_it != levels->end(); ++level_it) {
		if (level == level_it->amf_level) {
			found = true;
			*level_str = level_it->level_str;
			break;
		}
	}

	if (!found) {
		*level_str = "unknown";
	}

	return found;
}

static bool amf_avc_init(void *data, obs_data_t *settings)
{
	amf_base *enc = (amf_base *)data;

	set_avc_property(enc, ENFORCE_HRD, true);
	set_avc_property(enc, DE_BLOCKING_FILTER, true);

	// Determine and set the appropriate AVC level
	amf_set_codec_level(enc);

	amf_settings amf_settings = get_amf_settings(enc, settings);
	amf_avc_update_data(enc, amf_settings);

#if _CHECK_THROUGHPUT
	check_preset_compatibility(enc, preset);
#endif

	const char *ffmpeg_opts = amf_settings.condensed_opts.get();
	if (!(*ffmpeg_opts))
		ffmpeg_opts = "(none)";

	info("settings:\n"
	     "\trate_control: %s\n"
	     "\tbitrate:      %d\n"
	     "\tcqp:          %d\n"
	     "\tkeyint:       %d\n"
	     "\tpreset:       %s\n"
	     "\tprofile:      %s\n"
	     "\tlevel:        %s\n"
	     "\tb-frames:     %d\n"
	     "\twidth:        %d\n"
	     "\theight:       %d\n"
	     "\tparams:       %s",
	     amf_settings.rc, amf_settings.bitrate, amf_settings.qp, amf_settings.gop_size, amf_settings.preset,
	     amf_settings.profile, amf_settings.level, amf_settings.bf, enc->cx, enc->cy, ffmpeg_opts);

	return true;
}

static void amf_avc_create_internal(amf_base *enc, obs_data_t *settings)
{
	AMF_RESULT res;
	AMFVariant p;

	enc->codec = amf_codec_type::AVC;

	if (!amf_create_encoder(enc))
		throw "Failed to create encoder";

#ifndef OBS_AMF_DISABLE_PROPERTIES
	bool show_properties = !getenv("OBS_AMF_DISABLE_PROPERTIES");
#endif
	AMFCapsPtr caps;
	res = enc->amf_encoder->GetCaps(&caps);
	if (res == AMF_OK) {
#ifndef OBS_AMF_DISABLE_PROPERTIES
		if (show_properties) {
			std::stringstream ss;
			ss << "capabilities:";
			amf_print_properties(ss, caps, amf_avc_capability_types);
			info("%s", ss.str().c_str());
		}
#endif

		caps->GetProperty(AMF_VIDEO_ENCODER_CAP_BFRAMES, &enc->bframes_supported);
		caps->GetProperty(AMF_VIDEO_ENCODER_CAP_MAX_THROUGHPUT, &enc->max_throughput);
		caps->GetProperty(AMF_VIDEO_ENCODER_CAP_REQUESTED_THROUGHPUT, &enc->requested_throughput);
		caps->GetProperty(AMF_VIDEO_ENCODER_CAP_ROI, &enc->roi_supported);
		caps->GetProperty(AMF_VIDEO_ENCODER_CAP_PRE_ANALYSIS, &enc->pre_analysis_supported);
	}

	set_avc_property(enc, FRAMESIZE, AMFConstructSize(enc->cx, enc->cy));
	set_avc_property(enc, USAGE, AMF_VIDEO_ENCODER_USAGE_TRANSCODING);
	set_avc_property(enc, CABAC_ENABLE, AMF_VIDEO_ENCODER_UNDEFINED);
	set_avc_property(enc, OUTPUT_COLOR_PROFILE, enc->amf_color_profile);
	set_avc_property(enc, OUTPUT_TRANSFER_CHARACTERISTIC, enc->amf_characteristic);
	set_avc_property(enc, OUTPUT_COLOR_PRIMARIES, enc->amf_primaries);
	set_avc_property(enc, FULL_RANGE_COLOR, enc->full_range);
	set_avc_property(enc, FRAMERATE, enc->amf_frame_rate);

	amf_avc_init(enc, settings);

	res = enc->amf_encoder->Init(enc->amf_format, enc->cx, enc->cy);
	if (res != AMF_OK)
		throw amf_error("AMFComponent::Init failed", res);

#ifndef OBS_AMF_DISABLE_PROPERTIES
	if (show_properties) {
		AMFPropertyStorage *props = enc->amf_encoder;
		std::stringstream ss;
		ss << "active properties:";
		for (const char *category : amf_avc_property_categories)
			amf_print_property_category(ss, props, category, amf_avc_property_types.at(category));
		bool pa_enabled;
		props->GetProperty<bool>(AMF_VIDEO_ENCODER_PRE_ANALYSIS_ENABLE, &pa_enabled);
		if (pa_enabled)
			amf_print_property_category(ss, props, "Pre-Analysis", amf_pa_property_types);
		info("%s", ss.str().c_str());
	}
#endif

	res = enc->amf_encoder->GetProperty(AMF_VIDEO_ENCODER_EXTRADATA, &p);
	if (res == AMF_OK && p.type == AMF_VARIANT_INTERFACE)
		enc->header = AMFBufferPtr(p.pInterface);
}

static void *amf_avc_create_texencode(obs_data_t *settings, obs_encoder_t *encoder)
try {
	check_texture_encode_capability(encoder, amf_codec_type::AVC);

	std::unique_ptr<amf_texencode> enc = std::make_unique<amf_texencode>();
	enc->encoder = encoder;
	enc->encoder_str = "texture-amf-h264";

#ifdef _WIN32
	if (!amf_init_d3d11(enc.get()))
		throw "Failed to create D3D11";
#endif

	amf_avc_create_internal(enc.get(), settings);
	return enc.release();

} catch (const amf_error &err) {
	blog(LOG_ERROR, "[texture-amf-h264] %s: %s: %ls", __FUNCTION__, err.str, amf_trace->GetResultText(err.res));
	return obs_encoder_create_rerouted(encoder, "h264_fallback_amf");

} catch (const char *err) {
	blog(LOG_ERROR, "[texture-amf-h264] %s: %s", __FUNCTION__, err);
	return obs_encoder_create_rerouted(encoder, "h264_fallback_amf");
}

static void *amf_avc_create_fallback(obs_data_t *settings, obs_encoder_t *encoder)
try {
	std::unique_ptr<amf_fallback> enc = std::make_unique<amf_fallback>();
	enc->encoder = encoder;
	enc->encoder_str = "fallback-amf-h264";

	video_t *video = obs_encoder_video(encoder);
	const struct video_output_info *voi = video_output_get_info(video);
	switch (voi->format) {
	case VIDEO_FORMAT_I010:
	case VIDEO_FORMAT_P010: {
		const char *const text = obs_module_text("AMF.10bitUnsupportedAvc");
		obs_encoder_set_last_error(encoder, text);
		throw text;
	}
	case VIDEO_FORMAT_P216:
	case VIDEO_FORMAT_P416: {
		const char *const text = obs_module_text("AMF.16bitUnsupported");
		obs_encoder_set_last_error(encoder, text);
		throw text;
	}
	default:
		switch (voi->colorspace) {
		case VIDEO_CS_2100_PQ:
		case VIDEO_CS_2100_HLG: {
			const char *const text = obs_module_text("AMF.8bitUnsupportedHdr");
			obs_encoder_set_last_error(encoder, text);
			throw text;
		}
		}
	}

	amf_avc_create_internal(enc.get(), settings);
	return enc.release();

} catch (const amf_error &err) {
	blog(LOG_ERROR, "[fallback-amf-h264] %s: %s: %ls", __FUNCTION__, err.str, amf_trace->GetResultText(err.res));
	return nullptr;

} catch (const char *err) {
	blog(LOG_ERROR, "[fallback-amf-h264] %s: %s", __FUNCTION__, err);
	return nullptr;
}

static void register_avc()
{
	struct obs_encoder_info amf_encoder_info = {};
	amf_encoder_info.id = "h264_texture_amf";
	amf_encoder_info.type = OBS_ENCODER_VIDEO;
	amf_encoder_info.codec = "h264";
	amf_encoder_info.get_name = amf_avc_get_name;
	amf_encoder_info.create = amf_avc_create_texencode;
	amf_encoder_info.destroy = amf_destroy;
	amf_encoder_info.update = amf_avc_update;
	amf_encoder_info.encode_texture = amf_encode_tex;
	amf_encoder_info.encode_texture2 = amf_encode_tex2;
	amf_encoder_info.get_defaults = amf_defaults;
	amf_encoder_info.get_properties = amf_avc_properties;
	amf_encoder_info.get_extra_data = amf_extra_data;
	amf_encoder_info.caps = OBS_ENCODER_CAP_PASS_TEXTURE | OBS_ENCODER_CAP_DYN_BITRATE | OBS_ENCODER_CAP_ROI;

	obs_register_encoder(&amf_encoder_info);

	amf_encoder_info.id = "h264_fallback_amf";
	amf_encoder_info.caps = OBS_ENCODER_CAP_INTERNAL | OBS_ENCODER_CAP_DYN_BITRATE | OBS_ENCODER_CAP_ROI;
	amf_encoder_info.encode_texture = nullptr;
	amf_encoder_info.encode_texture2 = nullptr;
	amf_encoder_info.create = amf_avc_create_fallback;
	amf_encoder_info.encode = amf_encode_fallback;
	amf_encoder_info.get_video_info = h264_video_info_fallback;

	obs_register_encoder(&amf_encoder_info);
}

/* ========================================================================= */
/* HEVC Implementation                                                       */

#if ENABLE_HEVC

static const char *amf_hevc_get_name(void *)
{
	return "AMD HW H.265 (HEVC)";
}

static inline int get_hevc_preset(amf_base *enc, const char *preset)
{
	UNUSED_PARAMETER(enc);
	if (astrcmpi(preset, "balanced") == 0)
		return AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_BALANCED;
	else if (astrcmpi(preset, "speed") == 0)
		return AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED;

	return AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_QUALITY;
}

static inline int get_hevc_rate_control(const char *rc_str)
{
	if (astrcmpi(rc_str, "cqp") == 0)
		return AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CONSTANT_QP;
	else if (astrcmpi(rc_str, "vbr_lat") == 0)
		return AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR;
	else if (astrcmpi(rc_str, "vbr") == 0)
		return AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR;
	else if (astrcmpi(rc_str, "cbr") == 0)
		return AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR;
	else if (astrcmpi(rc_str, "qvbr") == 0)
		return AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_QUALITY_VBR;
	else if (astrcmpi(rc_str, "hqvbr") == 0)
		return AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_HIGH_QUALITY_VBR;
	else if (astrcmpi(rc_str, "hqcbr") == 0)
		return AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_HIGH_QUALITY_CBR;

	return AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR;
}

static void amf_hevc_update_data(amf_base *enc, amf_settings &settings)
{
	set_hevc_property(enc, QUALITY_PRESET, get_hevc_preset(enc, settings.preset));

	obs_data_t *obs_settings = settings.settings;
	set_hevc_property(enc, LOWLATENCY_MODE, obs_data_get_bool(obs_settings, "low_latency"));
	set_hevc_property(enc, PREENCODE_ENABLE, obs_data_get_bool(obs_settings, "pre_encode"));

	int rc = get_hevc_rate_control(settings.rc);
	set_hevc_property(enc, RATE_CONTROL_METHOD, rc);
	if (rc != AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CONSTANT_QP &&
	    rc != AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_QUALITY_VBR) {
		int bitrate = settings.bitrate * 1000;
		set_hevc_property(enc, TARGET_BITRATE, bitrate);
		set_hevc_property(enc, PEAK_BITRATE, bitrate);
		set_hevc_property(enc, VBV_BUFFER_SIZE, bitrate);
		set_hevc_property(enc, FILLER_DATA_ENABLE, rc == AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_CBR);
	} else {
		int qp = settings.qp;
		set_hevc_property(enc, QP_I, qp);
		set_hevc_property(enc, QP_P, qp);
		set_hevc_property(enc, QVBR_QUALITY_LEVEL, qp);
	}

	set_hevc_property(enc, GOP_SIZE, settings.gop_size);

	bool pre_analysis = set_pre_analysis(enc, obs_settings);

	bool vbaq = should_enable_adaptive_quality(enc, obs_settings, settings.rc, pre_analysis);
	set_hevc_property(enc, ENABLE_VBAQ, vbaq);

	bool hmqb = !pre_analysis && obs_data_get_bool(obs_settings, "hmqb");
	set_hevc_property(enc, HIGH_MOTION_QUALITY_BOOST_ENABLE, hmqb);

	amf_apply_opts(enc, settings);

	/* The ffmpeg_opts just above may have explicitly set the HEVC level to a value different than what was
	 * determined by amf_set_codec_level(). Query the final HEVC level then lookup the matching string. Warn if not
	 * found, because ffmpeg_opts is free-form and may have set something bogus.
	 */
	amf_int64 final_level;
	get_hevc_property(enc, PROFILE_LEVEL, &final_level);
	if (!amf_get_level_str(enc, final_level, &settings.level))
		warn("HEVC level string not found. Level %d may be incorrect.", final_level);
}

static bool amf_hevc_update(void *data, obs_data_t *settings)
try {
	amf_base *enc = (amf_base *)data;

	// Is this needed for anything?
	// It ignores the first settings change after start, every time.
	// if (enc->first_update) {
	// 	enc->first_update = false;
	// 	return true;
	// }

	AMFComponentPtr encoder = enc->amf_encoder;

#ifndef OBS_AMF_DISABLE_PROPERTIES
	property_values_t old_values = amf_property_values(encoder, amf_hevc_property_types);
#endif

	amf_settings amf_settings = get_amf_settings(enc, settings);
	amf_hevc_update_data(enc, amf_settings);
	enc->reinitialize();

#ifndef OBS_AMF_DISABLE_PROPERTIES
	std::stringstream ss;
	amf_print_changed_property_values(ss, encoder, amf_hevc_property_types, old_values);
	if (ss.tellp())
		info("updated properties:\n%s", ss.str().c_str());
#endif

	return true;

} catch (const amf_error &err) {
	amf_base *enc = (amf_base *)data;
	error("%s: %s: %ls", __FUNCTION__, err.str, amf_trace->GetResultText(err.res));
	return false;
}

static bool amf_hevc_init(void *data, obs_data_t *settings)
{
	amf_base *enc = (amf_base *)data;

	set_hevc_property(enc, ENFORCE_HRD, true);

	// Determine and set the appropriate HEVC level
	amf_set_codec_level(enc);

	amf_settings amf_settings = get_amf_settings(enc, settings);
	amf_hevc_update_data(enc, amf_settings);

#if _CHECK_THROUGHPUT
	check_preset_compatibility(enc, preset);
#endif

	const char *ffmpeg_opts = amf_settings.condensed_opts.get();
	if (!(*ffmpeg_opts))
		ffmpeg_opts = "(none)";

	info("settings:\n"
	     "\trate_control: %s\n"
	     "\tbitrate:      %d\n"
	     "\tcqp:          %d\n"
	     "\tkeyint:       %d\n"
	     "\tpreset:       %s\n"
	     "\tprofile:      %s\n"
	     "\tlevel:        %s\n"
	     "\twidth:        %d\n"
	     "\theight:       %d\n"
	     "\tparams:       %s",
	     amf_settings.rc, amf_settings.bitrate, amf_settings.qp, amf_settings.gop_size, amf_settings.preset,
	     amf_settings.profile, amf_settings.level, enc->cx, enc->cy, ffmpeg_opts);

	return true;
}

static inline bool is_hlg(amf_base *enc)
{
	return enc->amf_characteristic == AMF_COLOR_TRANSFER_CHARACTERISTIC_ARIB_STD_B67;
}

static inline bool is_pq(amf_base *enc)
{
	return enc->amf_characteristic == AMF_COLOR_TRANSFER_CHARACTERISTIC_SMPTE2084;
}

constexpr amf_uint16 amf_hdr_primary(uint32_t num, uint32_t den)
{
	return (amf_uint16)(num * 50000 / den);
}

constexpr amf_uint32 lum_mul = 10000;

constexpr amf_uint32 amf_make_lum(amf_uint32 val)
{
	return val * lum_mul;
}

static void amf_hevc_create_internal(amf_base *enc, obs_data_t *settings)
{
	AMF_RESULT res;
	AMFVariant p;

	enc->codec = amf_codec_type::HEVC;

	if (!amf_create_encoder(enc))
		throw "Failed to create encoder";

#ifndef OBS_AMF_DISABLE_PROPERTIES
	bool show_properties = !getenv("OBS_AMF_DISABLE_PROPERTIES");
#endif
	AMFCapsPtr caps;
	res = enc->amf_encoder->GetCaps(&caps);
	if (res == AMF_OK) {
#ifndef OBS_AMF_DISABLE_PROPERTIES
		if (show_properties) {
			std::stringstream ss;
			ss << "capabilities:";
			amf_print_properties(ss, caps, amf_hevc_capability_types);
			info("%s", ss.str().c_str());
		}
#endif

		caps->GetProperty(AMF_VIDEO_ENCODER_HEVC_CAP_MAX_THROUGHPUT, &enc->max_throughput);
		caps->GetProperty(AMF_VIDEO_ENCODER_HEVC_CAP_REQUESTED_THROUGHPUT, &enc->requested_throughput);
		caps->GetProperty(AMF_VIDEO_ENCODER_HEVC_CAP_ROI, &enc->roi_supported);
		caps->GetProperty(AMF_VIDEO_ENCODER_HEVC_CAP_PRE_ANALYSIS, &enc->pre_analysis_supported);
	}

	const bool is10bit = enc->amf_format == AMF_SURFACE_P010;
	const bool pq = is_pq(enc);
	const bool hlg = is_hlg(enc);
	const bool is_hdr = pq || hlg;
	const char *preset = obs_data_get_string(settings, "preset");

	set_hevc_property(enc, FRAMESIZE, AMFConstructSize(enc->cx, enc->cy));
	set_hevc_property(enc, USAGE, AMF_VIDEO_ENCODER_USAGE_TRANSCODING);
	set_hevc_property(enc, COLOR_BIT_DEPTH, is10bit ? AMF_COLOR_BIT_DEPTH_10 : AMF_COLOR_BIT_DEPTH_8);
	set_hevc_property(enc, PROFILE,
			  is10bit ? AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN_10 : AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN);
	set_hevc_property(enc, OUTPUT_COLOR_PROFILE, enc->amf_color_profile);
	set_hevc_property(enc, OUTPUT_TRANSFER_CHARACTERISTIC, enc->amf_characteristic);
	set_hevc_property(enc, OUTPUT_COLOR_PRIMARIES, enc->amf_primaries);
	set_hevc_property(enc, NOMINAL_RANGE, enc->full_range);
	set_hevc_property(enc, FRAMERATE, enc->amf_frame_rate);

	if (is_hdr) {
		const int hdr_nominal_peak_level = pq ? (int)obs_get_video_hdr_nominal_peak_level() : (hlg ? 1000 : 0);

		AMFBufferPtr buf;
		enc->amf_context->AllocBuffer(AMF_MEMORY_HOST, sizeof(AMFHDRMetadata), &buf);
		AMFHDRMetadata *md = (AMFHDRMetadata *)buf->GetNative();
		md->redPrimary[0] = amf_hdr_primary(17, 25);
		md->redPrimary[1] = amf_hdr_primary(8, 25);
		md->greenPrimary[0] = amf_hdr_primary(53, 200);
		md->greenPrimary[1] = amf_hdr_primary(69, 100);
		md->bluePrimary[0] = amf_hdr_primary(3, 20);
		md->bluePrimary[1] = amf_hdr_primary(3, 50);
		md->whitePoint[0] = amf_hdr_primary(3127, 10000);
		md->whitePoint[1] = amf_hdr_primary(329, 1000);
		md->minMasteringLuminance = 0;
		md->maxMasteringLuminance = amf_make_lum(hdr_nominal_peak_level);
		md->maxContentLightLevel = hdr_nominal_peak_level;
		md->maxFrameAverageLightLevel = hdr_nominal_peak_level;
		set_hevc_property(enc, INPUT_HDR_METADATA, buf);
	}

	amf_hevc_init(enc, settings);

	res = enc->amf_encoder->Init(enc->amf_format, enc->cx, enc->cy);
	if (res != AMF_OK)
		throw amf_error("AMFComponent::Init failed", res);

#ifndef OBS_AMF_DISABLE_PROPERTIES
	if (show_properties) {
		AMFPropertyStorage *props = enc->amf_encoder;
		std::stringstream ss;
		ss << "active properties:";
		for (const char *category : amf_hevc_property_categories)
			amf_print_property_category(ss, props, category, amf_hevc_property_types.at(category));
		bool pa_enabled;
		props->GetProperty<bool>(AMF_VIDEO_ENCODER_HEVC_PRE_ANALYSIS_ENABLE, &pa_enabled);
		if (pa_enabled)
			amf_print_property_category(ss, props, "Pre-Analysis", amf_pa_property_types);
		info("%s", ss.str().c_str());
	}
#endif

	res = enc->amf_encoder->GetProperty(AMF_VIDEO_ENCODER_HEVC_EXTRADATA, &p);
	if (res == AMF_OK && p.type == AMF_VARIANT_INTERFACE)
		enc->header = AMFBufferPtr(p.pInterface);
}

static void *amf_hevc_create_texencode(obs_data_t *settings, obs_encoder_t *encoder)
try {
	check_texture_encode_capability(encoder, amf_codec_type::HEVC);

	std::unique_ptr<amf_texencode> enc = std::make_unique<amf_texencode>();
	enc->encoder = encoder;
	enc->encoder_str = "texture-amf-h265";

#ifdef _WIN32
	if (!amf_init_d3d11(enc.get()))
		throw "Failed to create D3D11";
#endif

	amf_hevc_create_internal(enc.get(), settings);
	return enc.release();

} catch (const amf_error &err) {
	blog(LOG_ERROR, "[texture-amf-h265] %s: %s: %ls", __FUNCTION__, err.str, amf_trace->GetResultText(err.res));
	return obs_encoder_create_rerouted(encoder, "h265_fallback_amf");

} catch (const char *err) {
	blog(LOG_ERROR, "[texture-amf-h265] %s: %s", __FUNCTION__, err);
	return obs_encoder_create_rerouted(encoder, "h265_fallback_amf");
}

static void *amf_hevc_create_fallback(obs_data_t *settings, obs_encoder_t *encoder)
try {
	std::unique_ptr<amf_fallback> enc = std::make_unique<amf_fallback>();
	enc->encoder = encoder;
	enc->encoder_str = "fallback-amf-h265";

	video_t *video = obs_encoder_video(encoder);
	const struct video_output_info *voi = video_output_get_info(video);
	switch (voi->format) {
	case VIDEO_FORMAT_I010:
	case VIDEO_FORMAT_P010:
		break;
	case VIDEO_FORMAT_P216:
	case VIDEO_FORMAT_P416: {
		const char *const text = obs_module_text("AMF.16bitUnsupported");
		obs_encoder_set_last_error(encoder, text);
		throw text;
	}
	default:
		switch (voi->colorspace) {
		case VIDEO_CS_2100_PQ:
		case VIDEO_CS_2100_HLG: {
			const char *const text = obs_module_text("AMF.8bitUnsupportedHdr");
			obs_encoder_set_last_error(encoder, text);
			throw text;
		}
		}
	}

	amf_hevc_create_internal(enc.get(), settings);
	return enc.release();

} catch (const amf_error &err) {
	blog(LOG_ERROR, "[fallback-amf-h265] %s: %s: %ls", __FUNCTION__, err.str, amf_trace->GetResultText(err.res));
	return nullptr;

} catch (const char *err) {
	blog(LOG_ERROR, "[fallback-amf-h265] %s: %s", __FUNCTION__, err);
	return nullptr;
}

static void register_hevc()
{
	struct obs_encoder_info amf_encoder_info = {};
	amf_encoder_info.id = "h265_texture_amf";
	amf_encoder_info.type = OBS_ENCODER_VIDEO;
	amf_encoder_info.codec = "hevc";
	amf_encoder_info.get_name = amf_hevc_get_name;
	amf_encoder_info.create = amf_hevc_create_texencode;
	amf_encoder_info.destroy = amf_destroy;
	amf_encoder_info.update = amf_hevc_update;
	amf_encoder_info.encode_texture = amf_encode_tex;
	amf_encoder_info.encode_texture2 = amf_encode_tex2;
	amf_encoder_info.get_defaults = amf_defaults;
	amf_encoder_info.get_properties = amf_hevc_properties;
	amf_encoder_info.get_extra_data = amf_extra_data;
	amf_encoder_info.caps = OBS_ENCODER_CAP_PASS_TEXTURE | OBS_ENCODER_CAP_DYN_BITRATE | OBS_ENCODER_CAP_ROI;

	obs_register_encoder(&amf_encoder_info);

	amf_encoder_info.id = "h265_fallback_amf";
	amf_encoder_info.caps = OBS_ENCODER_CAP_INTERNAL | OBS_ENCODER_CAP_DYN_BITRATE | OBS_ENCODER_CAP_ROI;
	amf_encoder_info.encode_texture = nullptr;
	amf_encoder_info.encode_texture2 = nullptr;
	amf_encoder_info.create = amf_hevc_create_fallback;
	amf_encoder_info.encode = amf_encode_fallback;
	amf_encoder_info.get_video_info = h265_video_info_fallback;

	obs_register_encoder(&amf_encoder_info);
}

#endif //ENABLE_HEVC

/* ========================================================================= */
/* AV1 Implementation                                                        */

static const char *amf_av1_get_name(void *)
{
	return "AMD HW AV1";
}

static inline int get_av1_preset(amf_base *enc, const char *preset)
{
	UNUSED_PARAMETER(enc);
	if (astrcmpi(preset, "highquality") == 0)
		return AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_HIGH_QUALITY;
	else if (astrcmpi(preset, "quality") == 0)
		return AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_QUALITY;
	else if (astrcmpi(preset, "balanced") == 0)
		return AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_BALANCED;
	else if (astrcmpi(preset, "speed") == 0)
		return AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_SPEED;

	return AMF_VIDEO_ENCODER_AV1_QUALITY_PRESET_BALANCED;
}

static inline int get_av1_rate_control(const char *rc_str)
{
	if (astrcmpi(rc_str, "cqp") == 0)
		return AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_CONSTANT_QP;
	else if (astrcmpi(rc_str, "vbr_lat") == 0)
		return AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR;
	else if (astrcmpi(rc_str, "vbr") == 0)
		return AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR;
	else if (astrcmpi(rc_str, "cbr") == 0)
		return AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_CBR;
	else if (astrcmpi(rc_str, "qvbr") == 0)
		return AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_QUALITY_VBR;
	else if (astrcmpi(rc_str, "hqvbr") == 0)
		return AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_HIGH_QUALITY_VBR;
	else if (astrcmpi(rc_str, "hqcbr") == 0)
		return AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_HIGH_QUALITY_CBR;

	return AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_CBR;
}

static inline int get_av1_profile(const char *profile)
{
	if (astrcmpi(profile, "main") == 0)
		return AMF_VIDEO_ENCODER_AV1_PROFILE_MAIN;

	return AMF_VIDEO_ENCODER_AV1_PROFILE_MAIN;
}

static void amf_av1_update_data(amf_base *enc, amf_settings &settings)
{
	set_av1_property(enc, QUALITY_PRESET, get_av1_preset(enc, settings.preset));
	set_av1_property(enc, PROFILE, get_av1_profile(settings.profile));

	obs_data_t *obs_settings = settings.settings;
	set_av1_property(enc, ENCODING_LATENCY_MODE,
			 obs_data_get_bool(obs_settings, "low_latency")
				 ? AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE_LOWEST_LATENCY
				 : AMF_VIDEO_ENCODER_AV1_ENCODING_LATENCY_MODE_NONE);
	set_av1_property(enc, RATE_CONTROL_PREENCODE, obs_data_get_bool(obs_settings, "pre_encode"));

	int rc = get_av1_rate_control(settings.rc);
	if (rc != AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_CONSTANT_QP &&
	    rc != AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_QUALITY_VBR) {
		int bitrate = settings.bitrate * 1000;
		set_av1_property(enc, TARGET_BITRATE, bitrate);
		set_av1_property(enc, PEAK_BITRATE, bitrate);
		set_av1_property(enc, VBV_BUFFER_SIZE, bitrate);
		set_av1_property(enc, FILLER_DATA, rc == AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_CBR);

		if (rc == AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR ||
		    rc == AMF_VIDEO_ENCODER_AV1_RATE_CONTROL_METHOD_HIGH_QUALITY_VBR)
			set_av1_property(enc, PEAK_BITRATE, bitrate * 1.5);
	} else {
		int64_t qp = settings.qp * 4;
		set_av1_property(enc, QVBR_QUALITY_LEVEL, qp / 4);
		set_av1_property(enc, Q_INDEX_INTRA, qp);
		set_av1_property(enc, Q_INDEX_INTER, qp);
		set_av1_property(enc, Q_INDEX_INTER_B, qp);
	}

	set_av1_property(enc, GOP_SIZE, settings.gop_size);

	int bf = settings.bf;
	if (enc->bframes_supported) {
		set_av1_property(enc, MAX_CONSECUTIVE_BPICTURES, bf);
		set_av1_property(enc, B_PIC_PATTERN, bf);

		/* AdaptiveMiniGOP is suggested for some types of content such
		 * as those with high motion. This only takes effect if
		 * Pre-Analysis is enabled.
		 */
		set_av1_property(enc, ADAPTIVE_MINIGOP, bf > 0);

		amf_int64 b_frames = 0;
		amf_int64 b_max = 0;
		if (get_av1_property(enc, B_PIC_PATTERN, &b_frames) &&
		    get_av1_property(enc, MAX_CONSECUTIVE_BPICTURES, &b_max))
			enc->dts_offset = b_frames + 1;
		else
			enc->dts_offset = 0;

	} else if (bf != 0) {
		warn("B-Frames set to %d but b-frames are not supported by this device", bf);
		settings.bf = 0;
	}

	bool pre_analysis = set_pre_analysis(enc, obs_settings);

	bool aq = should_enable_adaptive_quality(enc, obs_settings, settings.rc, pre_analysis);
	if (aq)
		set_av1_enum(AQ_MODE, CAQ);
	else
		set_av1_enum(AQ_MODE, NONE);

	bool hmqb = !pre_analysis && obs_data_get_bool(obs_settings, "hmqb");
	set_av1_property(enc, HIGH_MOTION_QUALITY_BOOST, hmqb);

	amf_apply_opts(enc, settings);

	/* The ffmpeg_opts just above may have explicitly set the AV1 level to a value different than what was
	 * determined by amf_set_codec_level(). Query the final AV1 level then lookup the matching string. Warn if not
	 * found, because ffmpeg_opts is free-form and may have set something bogus.
	 */
	amf_int64 final_level;
	get_av1_property(enc, LEVEL, &final_level);
	if (!amf_get_level_str(enc, final_level, &settings.level))
		warn("AV1 level string not found. Level %d may be incorrect.", final_level);
}

static bool amf_av1_update(void *data, obs_data_t *settings)
try {
	amf_base *enc = (amf_base *)data;

	// Is this needed for anything?
	// It ignores the first settings change after start, every time.
	// if (enc->first_update) {
	// 	enc->first_update = false;
	// 	return true;
	// }

	amf_settings amf_settings = get_amf_settings(enc, settings);
	amf_av1_update_data(enc, amf_settings);
	enc->reinitialize();

	return true;

} catch (const amf_error &err) {
	amf_base *enc = (amf_base *)data;
	error("%s: %s: %ls", __FUNCTION__, err.str, amf_trace->GetResultText(err.res));
	return false;
}

static bool amf_av1_init(void *data, obs_data_t *settings)
{
	amf_base *enc = (amf_base *)data;

	set_av1_property(enc, ENFORCE_HRD, true);

	// Determine and set the appropriate AV1 level
	amf_set_codec_level(enc);

	amf_settings amf_settings = get_amf_settings(enc, settings);
	amf_av1_update_data(enc, amf_settings);

	char *opts_str = nullptr;
	const char *ffmpeg_opts = obs_data_get_string(settings, "ffmpeg_opts");
	if (ffmpeg_opts && *ffmpeg_opts) {
		opts_str = new char[strlen(ffmpeg_opts) + 1];
		obs_data_condense_whitespace(ffmpeg_opts, opts_str);
		ffmpeg_opts = opts_str;
		struct obs_options opts = obs_parse_options(ffmpeg_opts);
		for (size_t i = 0; i < opts.count; i++) {
			amf_apply_opt(enc, &opts.options[i]);
		}
		obs_free_options(opts);
	} else {
		ffmpeg_opts = "(none)";
	}

#if _CHECK_THROUGHPUT
	check_preset_compatibility(enc, preset);
#endif

	info("settings:\n"
	     "\trate_control: %s\n"
	     "\tbitrate:      %d\n"
	     "\tcqp:          %d\n"
	     "\tkeyint:       %d\n"
	     "\tpreset:       %s\n"
	     "\tprofile:      %s\n"
	     "\tlevel:        %s\n"
	     "\tb-frames:     %d\n"
	     "\twidth:        %d\n"
	     "\theight:       %d\n"
	     "\tparams:       %s",
	     amf_settings.rc, amf_settings.bitrate, amf_settings.qp, amf_settings.gop_size, amf_settings.preset,
	     amf_settings.profile, amf_settings.level, amf_settings.bf, enc->cx, enc->cy, ffmpeg_opts);

	return true;
}

static void amf_av1_create_internal(amf_base *enc, obs_data_t *settings)
{
	enc->codec = amf_codec_type::AV1;

	if (!amf_create_encoder(enc))
		throw "Failed to create encoder";

	AMFCapsPtr caps;
	AMF_RESULT res = enc->amf_encoder->GetCaps(&caps);
	if (res == AMF_OK) {
		caps->GetProperty(AMF_VIDEO_ENCODER_AV1_CAP_BFRAMES, &enc->bframes_supported);
		caps->GetProperty(AMF_VIDEO_ENCODER_AV1_CAP_MAX_THROUGHPUT, &enc->max_throughput);
		caps->GetProperty(AMF_VIDEO_ENCODER_AV1_CAP_REQUESTED_THROUGHPUT, &enc->requested_throughput);
		caps->GetProperty(AMF_VIDEO_ENCODER_AV1_CAP_PRE_ANALYSIS, &enc->pre_analysis_supported);
		/* For some reason there's no specific CAP for AV1, but should always be supported */
		enc->roi_supported = true;
	}

	const bool is10bit = enc->amf_format == AMF_SURFACE_P010;

	set_av1_property(enc, FRAMESIZE, AMFConstructSize(enc->cx, enc->cy));
	set_av1_property(enc, USAGE, AMF_VIDEO_ENCODER_USAGE_TRANSCODING);
	set_av1_property(enc, ALIGNMENT_MODE, AMF_VIDEO_ENCODER_AV1_ALIGNMENT_MODE_NO_RESTRICTIONS);
	set_av1_property(enc, COLOR_BIT_DEPTH, is10bit ? AMF_COLOR_BIT_DEPTH_10 : AMF_COLOR_BIT_DEPTH_8);
	set_av1_property(enc, OUTPUT_COLOR_PROFILE, enc->amf_color_profile);
	set_av1_property(enc, OUTPUT_TRANSFER_CHARACTERISTIC, enc->amf_characteristic);
	set_av1_property(enc, OUTPUT_COLOR_PRIMARIES, enc->amf_primaries);
	set_av1_property(enc, FRAMERATE, enc->amf_frame_rate);

	amf_av1_init(enc, settings);

	res = enc->amf_encoder->Init(enc->amf_format, enc->cx, enc->cy);
	if (res != AMF_OK)
		throw amf_error("AMFComponent::Init failed", res);

	AMFVariant p;
	res = enc->amf_encoder->GetProperty(AMF_VIDEO_ENCODER_AV1_EXTRA_DATA, &p);
	if (res == AMF_OK && p.type == AMF_VARIANT_INTERFACE)
		enc->header = AMFBufferPtr(p.pInterface);
}

static void *amf_av1_create_texencode(obs_data_t *settings, obs_encoder_t *encoder)
try {
	check_texture_encode_capability(encoder, amf_codec_type::AV1);

	std::unique_ptr<amf_texencode> enc = std::make_unique<amf_texencode>();
	enc->encoder = encoder;
	enc->encoder_str = "texture-amf-av1";

#ifdef _WIN32
	if (!amf_init_d3d11(enc.get()))
		throw "Failed to create D3D11";
#endif

	amf_av1_create_internal(enc.get(), settings);
	return enc.release();

} catch (const amf_error &err) {
	blog(LOG_ERROR, "[texture-amf-av1] %s: %s: %ls", __FUNCTION__, err.str, amf_trace->GetResultText(err.res));
	return obs_encoder_create_rerouted(encoder, "av1_fallback_amf");

} catch (const char *err) {
	blog(LOG_ERROR, "[texture-amf-av1] %s: %s", __FUNCTION__, err);
	return obs_encoder_create_rerouted(encoder, "av1_fallback_amf");
}

static void *amf_av1_create_fallback(obs_data_t *settings, obs_encoder_t *encoder)
try {
	std::unique_ptr<amf_fallback> enc = std::make_unique<amf_fallback>();
	enc->encoder = encoder;
	enc->encoder_str = "fallback-amf-av1";

	video_t *video = obs_encoder_video(encoder);
	const struct video_output_info *voi = video_output_get_info(video);
	switch (voi->format) {
	case VIDEO_FORMAT_I010:
	case VIDEO_FORMAT_P010: {
		break;
	}
	case VIDEO_FORMAT_P216:
	case VIDEO_FORMAT_P416: {
		const char *const text = obs_module_text("AMF.16bitUnsupported");
		obs_encoder_set_last_error(encoder, text);
		throw text;
	}
	default:
		switch (voi->colorspace) {
		case VIDEO_CS_2100_PQ:
		case VIDEO_CS_2100_HLG: {
			const char *const text = obs_module_text("AMF.8bitUnsupportedHdr");
			obs_encoder_set_last_error(encoder, text);
			throw text;
		}
		}
	}

	amf_av1_create_internal(enc.get(), settings);
	return enc.release();

} catch (const amf_error &err) {
	blog(LOG_ERROR, "[fallback-amf-av1] %s: %s: %ls", __FUNCTION__, err.str, amf_trace->GetResultText(err.res));
	return nullptr;

} catch (const char *err) {
	blog(LOG_ERROR, "[fallback-amf-av1] %s: %s", __FUNCTION__, err);
	return nullptr;
}

static void amf_av1_defaults(obs_data_t *settings)
{
	amf_defaults(settings);
	obs_data_set_default_int(settings, "bf", 2);
}

static void register_av1()
{
	struct obs_encoder_info amf_encoder_info = {};
	amf_encoder_info.id = "av1_texture_amf";
	amf_encoder_info.type = OBS_ENCODER_VIDEO;
	amf_encoder_info.codec = "av1";
	amf_encoder_info.get_name = amf_av1_get_name;
	amf_encoder_info.create = amf_av1_create_texencode;
	amf_encoder_info.destroy = amf_destroy;
	amf_encoder_info.update = amf_av1_update;
	amf_encoder_info.encode_texture = amf_encode_tex;
	amf_encoder_info.encode_texture2 = amf_encode_tex2;
	amf_encoder_info.get_defaults = amf_av1_defaults;
	amf_encoder_info.get_properties = amf_av1_properties;
	amf_encoder_info.get_extra_data = amf_extra_data;
	amf_encoder_info.caps = OBS_ENCODER_CAP_PASS_TEXTURE | OBS_ENCODER_CAP_DYN_BITRATE | OBS_ENCODER_CAP_ROI;

	obs_register_encoder(&amf_encoder_info);

	amf_encoder_info.id = "av1_fallback_amf";
	amf_encoder_info.caps = OBS_ENCODER_CAP_INTERNAL | OBS_ENCODER_CAP_DYN_BITRATE | OBS_ENCODER_CAP_ROI;
	amf_encoder_info.encode_texture = nullptr;
	amf_encoder_info.encode_texture2 = nullptr;
	amf_encoder_info.create = amf_av1_create_fallback;
	amf_encoder_info.encode = amf_encode_fallback;
	amf_encoder_info.get_video_info = av1_video_info_fallback;

	obs_register_encoder(&amf_encoder_info);
}

/* ========================================================================= */
/* Global Stuff                                                              */

static bool enum_luids(void *param, uint32_t idx, uint64_t luid)
{
	std::stringstream &cmd = *(std::stringstream *)param;
	cmd << " " << std::hex << luid;
	UNUSED_PARAMETER(idx);
	return true;
}

#ifdef _WIN32
#define OBS_AMF_TEST "obs-amf-test.exe"
#else
#define OBS_AMF_TEST "obs-amf-test"
#endif

extern "C" void amf_load(void)
try {
	AMF_RESULT res;
#ifdef _WIN32
	HMODULE amf_module_test;

	/* Check if the DLL is present before running the more expensive */
	/* obs-amf-test.exe, but load it as data so it can't crash us    */
	amf_module_test = LoadLibraryExW(AMF_DLL_NAME, nullptr, LOAD_LIBRARY_AS_DATAFILE);
	if (!amf_module_test)
		throw "No AMF library";
	FreeLibrary(amf_module_test);
#else
	void *amf_module_test = os_dlopen(AMF_DLL_NAMEA);
	if (!amf_module_test)
		throw "No AMF library";
	os_dlclose(amf_module_test);
#endif

	/* ----------------------------------- */
	/* Check for supported codecs          */

	BPtr<char> test_exe = os_get_executable_path_ptr(OBS_AMF_TEST);
	std::stringstream cmd;
	std::string caps_str;

#ifdef _WIN32
	cmd << '"';
	cmd << test_exe;
	cmd << '"';
	enum_graphics_device_luids(enum_luids, &cmd);

	os_process_pipe_t *pp = os_process_pipe_create(cmd.str().c_str(), "r");
	if (!pp)
		throw "Failed to launch the AMF test process I guess";

	for (;;) {
		char data[2048];
		size_t len = os_process_pipe_read(pp, (uint8_t *)data, sizeof(data));
		if (!len)
			break;

		caps_str.append(data, len);
	}

	os_process_pipe_destroy(pp);
#else
	/* os_process_pipe_create() wasn't working on Linux (OBS >= 30.2),
	 * but pipe() seems to work just fine
	 */
	cmd << test_exe;
	FILE *pipe = popen(cmd.str().c_str(), "r");

	if (!pipe)
		throw "Failed to launch the AMF test process I guess";

	char buffer[2048];
	while (fgets(buffer, sizeof buffer, pipe) != nullptr)
		caps_str += buffer;
	pclose(pipe);
#endif

	if (caps_str.empty())
		throw "Seems the AMF test subprocess crashed. "
		      "Better there than here I guess. "
		      "Let's just skip loading AMF then I suppose.";

	ConfigFile config;
	if (config.OpenString(caps_str.c_str()) != 0)
		throw "Failed to open config string";

	const char *error = config_get_string(config, "error", "string");
	if (error)
		throw std::string(error);

	uint32_t adapter_count = (uint32_t)config_num_sections(config);
	bool avc_supported = false;
	bool hevc_supported = false;
	bool av1_supported = false;

	for (uint32_t i = 0; i < adapter_count; i++) {
		std::string section = std::to_string(i);
		adapter_caps &info = caps[i];

		info.is_amd = config_get_bool(config, section.c_str(), "is_amd");
		info.supports_avc = config_get_bool(config, section.c_str(), "supports_avc");
		info.supports_hevc = config_get_bool(config, section.c_str(), "supports_hevc");
		info.supports_av1 = config_get_bool(config, section.c_str(), "supports_av1");

		avc_supported |= info.supports_avc;
		hevc_supported |= info.supports_hevc;
		av1_supported |= info.supports_av1;
	}

	if (!avc_supported && !hevc_supported && !av1_supported)
		throw "Neither AVC, HEVC, nor AV1 are supported by any devices";

	/* ----------------------------------- */
	/* Init AMF                            */

	amf_module = os_dlopen(AMF_DLL_NAMEA);
	if (!amf_module)
		throw "AMF library failed to load";

	AMFInit_Fn init = (AMFInit_Fn)os_dlsym(amf_module, AMF_INIT_FUNCTION_NAME);
	if (!init)
		throw "Failed to get AMFInit address";

	res = init(AMF_FULL_VERSION, &amf_factory);
	if (res != AMF_OK)
		throw amf_error("AMFInit failed", res);

	res = amf_factory->GetTrace(&amf_trace);
	if (res != AMF_OK)
		throw amf_error("GetTrace failed", res);

	AMFQueryVersion_Fn get_ver = (AMFQueryVersion_Fn)os_dlsym(amf_module, AMF_QUERY_VERSION_FUNCTION_NAME);
	if (!get_ver)
		throw "Failed to get AMFQueryVersion address";

	res = get_ver(&amf_version);
	if (res != AMF_OK)
		throw amf_error("AMFQueryVersion failed", res);

#ifndef DEBUG_AMF_STUFF
	amf_trace->EnableWriter(AMF_TRACE_WRITER_DEBUG_OUTPUT, false);
	amf_trace->EnableWriter(AMF_TRACE_WRITER_CONSOLE, false);
#endif

	/* ----------------------------------- */
	/* Register encoders                   */

	if (avc_supported)
		register_avc();
#if ENABLE_HEVC
	if (hevc_supported)
		register_hevc();
#endif
	if (av1_supported)
		register_av1();

} catch (const std::string &str) {
	/* doing debug here because string exceptions indicate the user is
	 * probably not using AMD */
	blog(LOG_DEBUG, "%s: %s", __FUNCTION__, str.c_str());

} catch (const char *str) {
	/* doing debug here because string exceptions indicate the user is
	 * probably not using AMD */
	blog(LOG_DEBUG, "%s: %s", __FUNCTION__, str);

} catch (const amf_error &err) {
	/* doing an error here because it means at least the library has loaded
	 * successfully, so they probably have AMD at this point */
	blog(LOG_ERROR, "%s: %s: 0x%uX", __FUNCTION__, err.str, (uint32_t)err.res);
}

extern "C" void amf_unload(void)
{
	if (amf_module && amf_trace) {
		amf_trace->TraceFlush();
		amf_trace->UnregisterWriter(L"obs_amf_trace_writer");
	}
}
