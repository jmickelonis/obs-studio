#include "linux.hpp"

#include <algorithm>
#include <mutex>

#include <EGL/egl.h>

static mutex glMutex;
static GLFunctions *gl;

GLFunctions::GLFunctions()
{
#define GET(NAME) \
	NAME = reinterpret_cast<decltype(NAME)>(eglGetProcAddress("gl" #NAME)); \
	if (!NAME) \
		throw "Failed to resolve gl" #NAME;
	GET(GetError);
	GET(CreateMemoryObjectsEXT);
	GET(DeleteMemoryObjectsEXT);
	GET(ImportMemoryFdEXT);
	GET(IsMemoryObjectEXT);
	GET(MemoryObjectParameterivEXT);
	GET(GenTextures);
	GET(DeleteTextures);
	GET(BindTexture);
	GET(TexParameteri);
	GET(TexStorageMem2DEXT);
	GET(GenSemaphoresEXT);
	GET(DeleteSemaphoresEXT);
	GET(ImportSemaphoreFdEXT);
	GET(IsSemaphoreEXT);
	GET(SignalSemaphoreEXT);
	GET(GenFramebuffers);
	GET(DeleteFramebuffers);
	GET(BindFramebuffer);
	GET(FramebufferTexture2D);
	GET(BlitFramebuffer);
#undef GET
}

static VkInstance createInstance()
{
	static VkApplicationInfo applicationInfo{
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "OBS",
		.apiVersion = VK_API_VERSION_1_2,
	};
	static vector<const char *> extensions{
		VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
		VK_KHR_SURFACE_EXTENSION_NAME,
	};
	static VkInstanceCreateInfo instanceCreateInfo{
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &applicationInfo,
		.enabledExtensionCount = (uint32_t)extensions.size(),
		.ppEnabledExtensionNames = extensions.data(),
	};
	VkInstance instance;
	VK_CHECK(vkCreateInstance(&instanceCreateInfo, nullptr, &instance));
	return instance;
}

static VkPhysicalDevice getPhysicalDevice(VkInstance instance, uint32_t id = 0)
{
	uint32_t count;
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, nullptr));

	vector<VkPhysicalDevice> devices(count);
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, devices.data()));

	VkPhysicalDeviceDriverProperties driverProps{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES,
	};
	VkPhysicalDeviceProperties2 props{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
		.pNext = &driverProps,
	};

	bool requiresProprietary = amfVersion < AMF_MAKE_FULL_VERSION(1, 4, 34, 0);

	for (VkPhysicalDevice device : devices) {
		vkGetPhysicalDeviceProperties2(device, &props);

		if (id && props.properties.deviceID != id)
			continue;

		VkDriverId &driverID = driverProps.driverID;

		if (requiresProprietary) {
			if (driverID == VK_DRIVER_ID_AMD_PROPRIETARY)
				return device;
		} else {
			switch (driverID) {
			case VK_DRIVER_ID_AMD_PROPRIETARY:
			case VK_DRIVER_ID_AMD_OPEN_SOURCE:
			case VK_DRIVER_ID_MESA_RADV:
				return device;
			}
		}
	}

	if (id) {
		stringstream ss;
		ss << "Failed to find Vulkan device with ID 0x" << std::hex << id;
		throw ss.str();
	}

	throw "Failed to find Vulkan device";
}

static vector<VkQueueFamilyProperties2> getQueueFamilies(VkPhysicalDevice device)
{
	uint32_t count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties2(device, &count, nullptr);

	vector<VkQueueFamilyProperties2> result(count);
	vkGetPhysicalDeviceQueueFamilyProperties2(device, &count, result.data());
	return result;
}

VulkanDevice::~VulkanDevice()
{
	if (hDevice) {
		vkDeviceWaitIdle(hDevice);
		vkDestroyDevice(hDevice, nullptr);
		hDevice = nullptr;
	}
	if (hInstance) {
		vkDestroyInstance(hInstance, nullptr);
		hInstance = nullptr;
	}
	hPhysicalDevice = nullptr;
}

shared_ptr<VulkanDevice> createDevice(AMFContext1Ptr context, uint32_t id, const vector<const char *> &otherExtensions)
{
	shared_ptr<VulkanDevice> devicePtr(new VulkanDevice{});
	VulkanDevice &device = *devicePtr.get();
	device.cbSizeof = sizeof(AMFVulkanDevice);

	VkInstance instance = createInstance();
	device.hInstance = instance;

	VkPhysicalDevice physicalDevice = getPhysicalDevice(instance, id);
	device.hPhysicalDevice = physicalDevice;

	vector<VkQueueFamilyProperties2> queueFamilies = getQueueFamilies(physicalDevice);
	unsigned int queueFamilyCount = queueFamilies.size();

	static const int REQUIRED_QUEUE_FLAGS = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT |
						VK_QUEUE_VIDEO_DECODE_BIT_KHR;
	vector<VkDeviceQueueCreateInfo> queueCreateInfos;

	for (unsigned int i = 0; i < queueFamilyCount; i++) {
		VkQueueFamilyProperties &props = queueFamilies.at(i).queueFamilyProperties;
		VkQueueFlags &queueFlags = props.queueFlags;

		if (!(queueFlags & REQUIRED_QUEUE_FLAGS))
			// Don't create queues not needed by us or AMF (like compute, encode, etc)
			continue;

		static const float PRIORITY = 1.0;
		VkDeviceQueueCreateInfo info = {
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = i,
			.queueCount = 1,
			.pQueuePriorities = &PRIORITY,
		};
		queueCreateInfos.push_back(info);
	}

	amf_size extensionCount = 0;
	AMF_CHECK(context->GetVulkanDeviceExtensions(&extensionCount, nullptr), "GetVulkanDeviceExtensions failed");
	vector<const char *> extensions(extensionCount);
	AMF_CHECK(context->GetVulkanDeviceExtensions(&extensionCount, extensions.data()),
		  "GetVulkanDeviceExtensions failed");

	extensions.reserve(extensionCount + otherExtensions.size());
	for (const char *name : otherExtensions)
		extensions.push_back(name);

	VkDeviceCreateInfo deviceInfo{
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount = (uint32_t)queueCreateInfos.size(),
		.pQueueCreateInfos = queueCreateInfos.data(),
		.enabledExtensionCount = (uint32_t)extensions.size(),
		.ppEnabledExtensionNames = extensions.data(),
	};
	VK_CHECK(vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device.hDevice));

	return devicePtr;
}

TextureEncoder::TextureEncoder(obs_encoder_t *encoder, CodecType codec, VideoInfo &videoInfo, string name,
			       uint32_t deviceID)
	: Encoder(encoder, codec, videoInfo, name, deviceID)
{
	switch (videoInfo.format) {
	case AMF_SURFACE_NV12:
		vkFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
		break;
	case AMF_SURFACE_P010:
		vkFormat = VK_FORMAT_G16_B16R16_2PLANE_420_UNORM;
		break;
	default:
		throw "Unsupported AMF_SURFACE_FORMAT";
	}
}

TextureEncoder::~TextureEncoder()
{
	if (!vkDevice)
		return;

	vkDeviceWaitIdle(vkDevice);
	vkDestroyCommandPool(vkDevice, vkCommandPool, nullptr);
	vkDestroyFence(vkDevice, vkFence, nullptr);

	obs_enter_graphics();
	const Plane *planes = planesPtr.get();
	for (int i = planeCount; i-- > 0;) {
		auto &plane = planes[i];
		gl->DeleteMemoryObjectsEXT(1, &plane.glMemory);
		gl->DeleteTextures(1, &plane.glTexture);
		gl->DeleteFramebuffers(1, &plane.glFBO);
		vkFreeMemory(vkDevice, plane.vkMemory, nullptr);
		vkDestroyImage(vkDevice, plane.vkImage, nullptr);
	}
	gl->DeleteSemaphoresEXT(1, &glSemaphore);
	vkDestroySemaphore(vkDevice, vkSemaphore, nullptr);
	for (auto &item : readFBOs)
		gl->DeleteFramebuffers(1, &item.second);
	obs_leave_graphics();

	terminate();
}

shared_ptr<VulkanDevice> Encoder::createDevice()
{
	return ::createDevice(amfContext1, deviceID);
}

shared_ptr<VulkanDevice> TextureEncoder::createDevice()
{
	vector<const char *> otherExtensions = {
		VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
		VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
		VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME,
		VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME,
	};
	shared_ptr<VulkanDevice> result = ::createDevice(amfContext1, deviceID, otherExtensions);

	vkDevice = result->hDevice;
	vkPhysicalDevice = result->hPhysicalDevice;

#define GET(NAME) \
	NAME = reinterpret_cast<decltype(NAME)>(vkGetDeviceProcAddr(vkDevice, #NAME)); \
	if (!NAME) \
		throw "Failed to resolve " #NAME;
	GET(vkGetMemoryFdKHR);
	GET(vkGetSemaphoreFdKHR);
#undef GET

	if (!gl) {
		scoped_lock lock(glMutex);
		if (!gl)
			gl = new GLFunctions;
	}

	return result;
}

bool TextureEncoder::encode(encoder_texture *texture, int64_t pts, encoder_packet *packet, bool *receivedPacket)
{
	if (!texture)
		throw "Encode failed: bad texture handle";

	if (!vkCommandPool)
		createTextures(texture);

	obs_enter_graphics();
	const Plane *planes = planesPtr.get();
	for (int i = planeCount; i-- > 0;) {
		GLuint fbo = getReadFBO(texture->tex[i]);
		auto &plane = planes[i];
		GL_CHECK(gl->BindFramebuffer(GL_READ_FRAMEBUFFER, fbo));
		GL_CHECK(gl->BindFramebuffer(GL_DRAW_FRAMEBUFFER, plane.glFBO));
		GL_CHECK(gl->BlitFramebuffer(0, 0, plane.w, plane.h, 0, 0, plane.w, plane.h, GL_COLOR_BUFFER_BIT,
					     GL_NEAREST));
		GL_CHECK(gl->BindFramebuffer(GL_READ_FRAMEBUFFER, 0));
		GL_CHECK(gl->BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0));
	}
	GL_CHECK(gl->SignalSemaphoreEXT(glSemaphore, 0, 0, planeCount, glTexturesPtr.get(), glDstLayoutsPtr.get()));
	obs_leave_graphics();

	AMFSurfacePtr surface;
	AMF_CHECK(amfContext1->AllocSurfaceEx(AMF_MEMORY_VULKAN, videoInfo.format, width, height,
					      AMF_SURFACE_USAGE_DEFAULT, AMF_MEMORY_CPU_LOCAL, &surface),
		  "AllocSurfaceEx failed");

	VkCommandBuffer copyCommandBuffer = getCopyCommandBuffer(surface);
	vkCopySubmitInfo.pCommandBuffers = &copyCommandBuffer;
	VK_CHECK(vkQueueSubmit(vkQueue, 1, &vkCopySubmitInfo, vkFence));
	waitForFence();

	surface->SetPts(timestampToAMF(pts));
	surface->SetProperty(L"PTS", pts);

	submit(surface, packet, receivedPacket);
	return true;
}

void TextureEncoder::createTextures(encoder_texture *from)
{
	planeCount = 0;
	for (int i = 0; i < 4; i++) {
		if (!from->tex[i]) {
			planeCount = i;
			break;
		}
	}

	Plane *planes = new Plane[planeCount];
	planesPtr = unique_ptr<Plane[]>(planes);
	GLenum *glDstLayouts = new GLenum[planeCount];
	glDstLayoutsPtr = unique_ptr<GLenum[]>(glDstLayouts);
	GLuint *glTextures = new GLuint[planeCount];
	glTexturesPtr = unique_ptr<GLuint[]>(glTextures);

	// Should always be the first queue index
	vkGetDeviceQueue(vkDevice, 0, 0, &vkQueue);

	VkCommandPoolCreateInfo poolInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.queueFamilyIndex = 0,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
	};
	VK_CHECK(vkCreateCommandPool(vkDevice, &poolInfo, nullptr, &vkCommandPool));

	VkCommandBuffer vkCommandBuffer;
	allocateCommandBuffer(vkCommandBuffer);

	beginCommandBuffer(vkCommandBuffer);
	for (int i = planeCount; i-- > 0;) {
		auto &plane = planes[i];

		obs_enter_graphics();
		auto tex = from->tex[i];
		auto gsFormat = gs_texture_get_color_format(tex);
		plane.w = gs_texture_get_width(tex);
		plane.h = gs_texture_get_height(tex);
		obs_leave_graphics();

		VkFormat vkColorFormat;
		GLenum glColorFormat;

		switch (gsFormat) {
		case GS_R8:
			vkColorFormat = VK_FORMAT_R8_UNORM;
			glColorFormat = GL_R8;
			break;
		case GS_R16:
			vkColorFormat = VK_FORMAT_R16_UNORM;
			glColorFormat = GL_R16;
			break;
		case GS_R8G8:
			vkColorFormat = VK_FORMAT_R8G8_UNORM;
			glColorFormat = GL_RG8;
			break;
		case GS_RG16:
			vkColorFormat = VK_FORMAT_R16G16_UNORM;
			glColorFormat = GL_RG16;
			break;
		default:
			throw "Unsupported color format";
		}

		VkExternalMemoryImageCreateInfo externalImageInfo{
			.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
			.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
		};
		VkImageCreateInfo imageInfo{
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.pNext = &externalImageInfo,
			.imageType = VK_IMAGE_TYPE_2D,
			.format = vkColorFormat,
			.extent =
				{
					.width = plane.w,
					.height = plane.h,
					.depth = 1,
				},
			.arrayLayers = 1,
			.mipLevels = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.tiling = VK_IMAGE_TILING_OPTIMAL,
			.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		};
		VK_CHECK(vkCreateImage(vkDevice, &imageInfo, nullptr, &plane.vkImage));

		VkMemoryRequirements memoryRequirements;
		vkGetImageMemoryRequirements(vkDevice, plane.vkImage, &memoryRequirements);

		VkExportMemoryAllocateInfo exportMemoryAllocateInfo{
			.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
			.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
		};
		VkMemoryDedicatedAllocateInfo dedicatedAllocateInfo{
			.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
			.image = plane.vkImage,
			.pNext = &exportMemoryAllocateInfo,
		};
		VkMemoryAllocateInfo memoryAllocInfo{
			.pNext = &dedicatedAllocateInfo,
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = memoryRequirements.size,
			.memoryTypeIndex = getMemoryTypeIndex(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
							      memoryRequirements.memoryTypeBits),
		};
		VK_CHECK(vkAllocateMemory(vkDevice, &memoryAllocInfo, nullptr, &plane.vkMemory));
		VK_CHECK(vkBindImageMemory(vkDevice, plane.vkImage, plane.vkMemory, 0));

		VkImageMemoryBarrier memoryBarrier{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.image = plane.vkImage,
			.subresourceRange =
				{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.layerCount = 1,
					.levelCount = 1,
				},
		};
		vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				     0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);

		memoryBarrier.oldLayout = memoryBarrier.newLayout;
		memoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
		vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				     0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);

		// Import memory
		VkMemoryGetFdInfoKHR memFdInfo{
			.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
			.memory = plane.vkMemory,
			.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
		};
		int fd = -1;
		VK_CHECK(vkGetMemoryFdKHR(vkDevice, &memFdInfo, &fd));

		obs_enter_graphics();

		GL_CHECK(gl->CreateMemoryObjectsEXT(1, &plane.glMemory));
		GLint dedicated = GL_TRUE;
		GL_CHECK(gl->MemoryObjectParameterivEXT(plane.glMemory, GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated));
		GL_CHECK(gl->ImportMemoryFdEXT(plane.glMemory, memoryAllocInfo.allocationSize,
					       GL_HANDLE_TYPE_OPAQUE_FD_EXT, fd));

		GL_CHECK(gl->GenTextures(1, &plane.glTexture));
		GL_CHECK(gl->BindTexture(GL_TEXTURE_2D, plane.glTexture));
		GL_CHECK(gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_TILING_EXT, GL_OPTIMAL_TILING_EXT));
		GL_CHECK(gl->TexStorageMem2DEXT(GL_TEXTURE_2D, 1, glColorFormat, imageInfo.extent.width,
						imageInfo.extent.height, plane.glMemory, 0));

		GL_CHECK(gl->GenFramebuffers(1, &plane.glFBO));
		GL_CHECK(gl->BindFramebuffer(GL_FRAMEBUFFER, plane.glFBO));
		GL_CHECK(gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, plane.glTexture,
						  0));
		GL_CHECK(gl->BindFramebuffer(GL_FRAMEBUFFER, 0));

		bool imported = gl->IsMemoryObjectEXT(plane.glMemory);

		obs_leave_graphics();

		if (!imported)
			throw "OpenGL texture import failed";

		glTextures[i] = plane.glTexture;
		glDstLayouts[i] = GL_LAYOUT_TRANSFER_SRC_EXT;
	}

	VkExportSemaphoreCreateInfo exportSemaphoreInfo{
		.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
		.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT,
	};
	VkSemaphoreCreateInfo semaphoreInfo{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.pNext = &exportSemaphoreInfo,
	};
	VK_CHECK(vkCreateSemaphore(vkDevice, &semaphoreInfo, nullptr, &vkSemaphore));

	endCommandBuffer(vkCommandBuffer);

	VkFenceCreateInfo fenceInfo{
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
	};
	VK_CHECK(vkCreateFence(vkDevice, &fenceInfo, nullptr, &vkFence));
	VkSubmitInfo submitInfo{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &vkCommandBuffer,
	};
	VK_CHECK(vkQueueSubmit(vkQueue, 1, &submitInfo, vkFence));
	waitForFence();

	vkFreeCommandBuffers(vkDevice, vkCommandPool, 1, &vkCommandBuffer);

	// Import semaphores
	VkSemaphoreGetFdInfoKHR semFdInfo{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
		.semaphore = vkSemaphore,
		.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT,
	};

	int fd;
	VK_CHECK(vkGetSemaphoreFdKHR(vkDevice, &semFdInfo, &fd));

	obs_enter_graphics();
	GL_CHECK(gl->GenSemaphoresEXT(1, &glSemaphore));
	GL_CHECK(gl->ImportSemaphoreFdEXT(glSemaphore, GL_HANDLE_TYPE_OPAQUE_FD_EXT, fd));
	bool imported = gl->IsSemaphoreEXT(glSemaphore);
	obs_leave_graphics();

	if (!imported)
		throw "OpenGL semaphore import failed";

	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	vkCopySubmitInfo = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &vkSemaphore,
		.pWaitDstStageMask = &waitStage,
		.commandBufferCount = 1,
	};
}

inline GLuint TextureEncoder::getReadFBO(gs_texture *tex)
{
	auto it = readFBOs.find(tex);
	if (it != readFBOs.end())
		return it->second;
	GLuint *obj = static_cast<GLuint *>(gs_texture_get_obj(tex));
	GLuint fbo;
	GL_CHECK(gl->GenFramebuffers(1, &fbo));
	GL_CHECK(gl->BindFramebuffer(GL_FRAMEBUFFER, fbo));
	GL_CHECK(gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *obj, 0));
	readFBOs.insert({tex, fbo});
	return fbo;
}

inline VkCommandBuffer TextureEncoder::getCopyCommandBuffer(AMFSurfacePtr &surface)
{
	VkImage vkImage = ((AMFVulkanView *)surface->GetPlaneAt(0)->GetNative())->pSurface->hImage;
	auto it = copyCommandBuffers.find(vkImage);

	if (it != copyCommandBuffers.end())
		return it->second;

	VkCommandBuffer buffer;
	allocateCommandBuffer(buffer);
	beginCommandBuffer(buffer);

	const Plane *planes = planesPtr.get();

	VkImageMemoryBarrier memoryBarriers[planeCount];
	for (int i = planeCount; i-- > 0;) {
		memoryBarriers[i] = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.image = planes[i].vkImage,
			.subresourceRange =
				{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.layerCount = 1,
					.levelCount = 1,
				},
			.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL,
		};
	}
	vkCmdPipelineBarrier(buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
			     0, nullptr, planeCount, memoryBarriers);

	VkImageCopy imageCopy{
		.srcSubresource =
			{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.layerCount = 1,
			},
		.dstSubresource =
			{
				.layerCount = 1,
			},
		.extent =
			{
				.depth = 1,
			},
	};
	for (int i = planeCount; i-- > 0;) {
		auto &plane = planes[i];
		auto &extent = imageCopy.extent;
		extent.width = plane.w;
		extent.height = plane.h;
		imageCopy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT * (i + 1);
		vkCmdCopyImage(buffer, plane.vkImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vkImage,
			       VK_IMAGE_LAYOUT_GENERAL, 1, &imageCopy);

		auto &barrier = memoryBarriers[i];
		barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		barrier.dstAccessMask = 0;
		barrier.srcQueueFamilyIndex = 0;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
	}
	vkCmdPipelineBarrier(buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0,
			     nullptr, 0, nullptr, planeCount, memoryBarriers);

	endCommandBuffer(buffer);
	copyCommandBuffers[vkImage] = buffer;
	return buffer;
}

void TextureEncoder::allocateCommandBuffer(VkCommandBuffer &buffer)
{
	VkCommandBufferAllocateInfo info{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandPool = vkCommandPool,
		.commandBufferCount = 1,
	};
	VK_CHECK(vkAllocateCommandBuffers(vkDevice, &info, &buffer));
}

void TextureEncoder::beginCommandBuffer(VkCommandBuffer &buffer)
{
	static VkCommandBufferBeginInfo info{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};
	VK_CHECK(vkBeginCommandBuffer(buffer, &info));
}

void TextureEncoder::endCommandBuffer(VkCommandBuffer &buffer)
{
	VK_CHECK(vkEndCommandBuffer(buffer));
}

void TextureEncoder::waitForFence()
{
	VK_CHECK(vkWaitForFences(vkDevice, 1, &vkFence, VK_TRUE, UINT64_MAX));
	VK_CHECK(vkResetFences(vkDevice, 1, &vkFence));
}

uint32_t TextureEncoder::getMemoryTypeIndex(VkMemoryPropertyFlags properties, uint32_t typeBits)
{
	VkPhysicalDeviceMemoryProperties prop;
	vkGetPhysicalDeviceMemoryProperties(vkPhysicalDevice, &prop);
	for (uint32_t i = 0; i < prop.memoryTypeCount; i++)
		if ((prop.memoryTypes[i].propertyFlags & properties) == properties && typeBits & (1 << i))
			return i;
	return 0xFFFFFFFF;
}

void TextureEncoder::onReinitialize()
{
	for (auto &[vkImage, buffer] : copyCommandBuffers)
		vkFreeCommandBuffers(vkDevice, vkCommandPool, 1, &buffer);
	copyCommandBuffers.clear();
}
