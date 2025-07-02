#include "linux.hpp"

#include <algorithm>

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

static VkPhysicalDevice getDevice(VkInstance instance)
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
	for (VkPhysicalDevice device : devices) {
		vkGetPhysicalDeviceProperties2(device, &props);
		if (driverProps.driverID == VK_DRIVER_ID_MESA_RADV ||
		    driverProps.driverID == VK_DRIVER_ID_AMD_OPEN_SOURCE ||
		    driverProps.driverID == VK_DRIVER_ID_AMD_PROPRIETARY) {
			return device;
		}
	}

	throw "Failed to find Vulkan device";
}

static vector<const char *> getExtensions(AMFContext1Ptr amfContext, VkPhysicalDevice device)
{
	// Start with what we want
	vector<const char *> want{
		VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
		VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
		VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME,
		VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME,
	};
	// Add what AMF wants
	amf_size wantCount = 0;
	AMF_CHECK(amfContext->GetVulkanDeviceExtensions(&wantCount, nullptr), "GetVulkanDeviceExtensions failed");
	want.resize(want.size() + wantCount);
	AMF_CHECK(amfContext->GetVulkanDeviceExtensions(&wantCount, &want[want.size() - wantCount]),
		  "GetVulkanDeviceExtensions failed");

	uint32_t count;
	VK_CHECK(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr));
	vector<VkExtensionProperties> extensions(count);
	VK_CHECK(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()));

	vector<const char *> result;
	for (const char *name : want) {
		auto it = find_if(extensions.begin(), extensions.end(),
				  [name](VkExtensionProperties e) { return STR_EQ(e.extensionName, name); });
		if (it != extensions.end())
			result.push_back(name);
	}
	return result;
}

static uint32_t getQueueFamilyIndex(VkPhysicalDevice device)
{
	uint32_t count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties2(device, &count, nullptr);

	vector<VkQueueFamilyProperties2> items(count);
	vkGetPhysicalDeviceQueueFamilyProperties2(device, &count, items.data());

	for (uint32_t i = 0; i < count; i++) {
		VkQueueFamilyProperties &props = items.at(i).queueFamilyProperties;
		if (props.queueFlags & VK_QUEUE_GRAPHICS_BIT)
			return i;
	}

	throw "Could not find queue family that supports graphics";
}

TextureEncoder::TextureEncoder(CodecType codec, obs_encoder_t *encoder, VideoInfo &videoInfo, string name)
	: Encoder(codec, encoder, videoInfo, name)
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

	vkInstance = nullptr;
	vkDevice = nullptr;

	try {
		vkInstance = createInstance();
		vkPhysicalDevice = getDevice(vkInstance);

		vector<const char *> extensions = getExtensions(amfContext1, vkPhysicalDevice);

		float queuePriority = 1.0;
		VkDeviceQueueCreateInfo queueCreateInfo{
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = getQueueFamilyIndex(vkPhysicalDevice),
			.queueCount = 1,
			.pQueuePriorities = &queuePriority,
		};

		VkDeviceCreateInfo deviceInfo{
			.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			.queueCreateInfoCount = 1,
			.pQueueCreateInfos = &queueCreateInfo,
			.enabledExtensionCount = (uint32_t)extensions.size(),
			.ppEnabledExtensionNames = extensions.data(),
		};
		VK_CHECK(vkCreateDevice(vkPhysicalDevice, &deviceInfo, nullptr, &vkDevice));

		amfVulkanDevice = {
			.cbSizeof = sizeof(AMFVulkanDevice),
			.hInstance = vkInstance,
			.hPhysicalDevice = vkPhysicalDevice,
			.hDevice = vkDevice,
		};

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
	} catch (...) {
		if (vkDevice)
			vkDestroyDevice(vkDevice, nullptr);
		if (vkInstance)
			vkDestroyInstance(vkInstance, nullptr);
		throw;
	}
}

TextureEncoder::~TextureEncoder()
{
	vkDeviceWaitIdle(vkDevice);
	clearBuffers();
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

	vkDestroyDevice(vkDevice, nullptr);
	vkDestroyInstance(vkInstance, nullptr);
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

	BufferPtr bufferPtr = getBuffer();
	Buffer &buffer = *bufferPtr;
	vkCopySubmitInfo.pCommandBuffers = &buffer.copyCommandBuffer;
	VK_CHECK(vkQueueSubmit(vkQueue, 1, &vkCopySubmitInfo, vkFence));
	VK_CHECK(vkWaitForFences(vkDevice, 1, &vkFence, VK_TRUE, UINT64_MAX));

	if (!buffer.surface)
		AMF_CHECK(amfContext1->CreateSurfaceFromVulkanNative(&buffer.vulkanSurface, &buffer.surface, nullptr),
			  "CreateSurfaceFromVulkanNative failed");

	AMFSurfacePtr &surface = buffer.surface;
	surface->SetPts(timestampToAMF(pts));
	surface->SetProperty(L"PTS", pts);
	buffer.ts = pts;
	activeBuffers.push_back(bufferPtr);

	submit(surface, packet, receivedPacket);
	return true;
}

void *TextureEncoder::getVulkanDevice()
{
	return &amfVulkanDevice;
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

	vkGetDeviceQueue(vkDevice, 0, 0, &vkQueue);
	VkCommandPoolCreateInfo poolInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.queueFamilyIndex = 0,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
	};
	VK_CHECK(vkCreateCommandPool(vkDevice, &poolInfo, nullptr, &vkCommandPool));
	allocateCommandBuffer(&vkCommandBuffer);

	beginCommandBuffer();
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

	endCommandBuffer();

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
	VK_CHECK(vkWaitForFences(vkDevice, 1, &vkFence, VK_TRUE, UINT64_MAX));

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

BufferPtr TextureEncoder::getBuffer()
{
	if (availableBuffers.size()) {
		BufferPtr buffer = availableBuffers.back();
		availableBuffers.pop_back();
		return buffer;
	}

	BufferPtr bufferPtr = make_shared<Buffer>();
	Buffer &buffer = *bufferPtr;

	VkImage vkImage = nullptr;
	VkDeviceMemory vkMemory = nullptr;
	VkCommandBuffer copyCommandBuffer = nullptr;

	try {
		VkImageCreateInfo imageInfo{
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.imageType = VK_IMAGE_TYPE_2D,
			.format = vkFormat,
			.extent =
				{
					.width = width,
					.height = height,
					.depth = 1,
				},
			.arrayLayers = 1,
			.mipLevels = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.tiling = VK_IMAGE_TILING_LINEAR,
			.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,
		};
		VK_CHECK(vkCreateImage(vkDevice, &imageInfo, nullptr, &vkImage));

		VkMemoryRequirements memoryRequirements;
		vkGetImageMemoryRequirements(vkDevice, vkImage, &memoryRequirements);
		VkMemoryAllocateInfo memoryAllocateInfo{
			.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			.allocationSize = memoryRequirements.size,
			.memoryTypeIndex = getMemoryTypeIndex(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
							      memoryRequirements.memoryTypeBits),
		};
		VK_CHECK(vkAllocateMemory(vkDevice, &memoryAllocateInfo, nullptr, &vkMemory));
		VK_CHECK(vkBindImageMemory(vkDevice, vkImage, vkMemory, 0));

		beginCommandBuffer();
		VkImageMemoryBarrier memoryBarrier{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.image = vkImage,
			.subresourceRange =
				{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.layerCount = 1,
					.levelCount = 1,
				},
			.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
		};
		vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				     0, 0, nullptr, 0, nullptr, 1, &memoryBarrier);
		submitCommandBuffer();

		allocateCommandBuffer(&copyCommandBuffer);
		beginCommandBuffer(&copyCommandBuffer);

		Plane *planes = planesPtr.get();

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
		vkCmdPipelineBarrier(copyCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, planeCount,
				     memoryBarriers);

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
			vkCmdCopyImage(copyCommandBuffer, plane.vkImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vkImage,
				       VK_IMAGE_LAYOUT_GENERAL, 1, &imageCopy);

			auto &barrier = memoryBarriers[i];
			barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			barrier.dstAccessMask = 0;
			barrier.srcQueueFamilyIndex = 0;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
		}
		vkCmdPipelineBarrier(copyCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
				     VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, planeCount,
				     memoryBarriers);

		VK_CHECK(vkEndCommandBuffer(copyCommandBuffer));

		AMFVulkanSurface &vulkanSurface = buffer.vulkanSurface;
		vulkanSurface = {
			.cbSizeof = sizeof(AMFVulkanSurface),
			.hImage = vkImage,
			.hMemory = vkMemory,
			.iSize = (amf_int64)memoryAllocateInfo.allocationSize,
			.eFormat = vkFormat,
			.iWidth = (amf_int32)width,
			.iHeight = (amf_int32)height,
			.eUsage = AMF_SURFACE_USAGE_TRANSFER_DST | AMF_SURFACE_USAGE_NOSYNC |
				  AMF_SURFACE_USAGE_NO_TRANSITION,
			.eAccess = AMF_MEMORY_CPU_LOCAL,
		};

		buffer.copyCommandBuffer = copyCommandBuffer;
		buffers.push_back(bufferPtr);
		return bufferPtr;
	} catch (...) {
		// Something went wrong
		// Clean up after ourselves and re-throw
		if (copyCommandBuffer)
			vkFreeCommandBuffers(vkDevice, vkCommandPool, 1, &copyCommandBuffer);
		if (vkMemory)
			vkFreeMemory(vkDevice, vkMemory, nullptr);
		if (vkImage)
			vkDestroyImage(vkDevice, vkImage, nullptr);
		throw;
	}
}

void TextureEncoder::clearBuffers()
{
	for (BufferPtr &bufferPtr : buffers) {
		Buffer &buffer = *bufferPtr.get();
		AMFVulkanSurface &vulkanSurface = buffer.vulkanSurface;
		vkFreeCommandBuffers(vkDevice, vkCommandPool, 1, &buffer.copyCommandBuffer);
		vkFreeMemory(vkDevice, vulkanSurface.hMemory, nullptr);
		vkDestroyImage(vkDevice, vulkanSurface.hImage, nullptr);
	}
	activeBuffers.clear();
	availableBuffers.clear();
	buffers.clear();
}

void TextureEncoder::allocateCommandBuffer(VkCommandBuffer *buffer)
{
	if (!buffer)
		buffer = &vkCommandBuffer;
	VkCommandBufferAllocateInfo info{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandPool = vkCommandPool,
		.commandBufferCount = 1,
	};
	VK_CHECK(vkAllocateCommandBuffers(vkDevice, &info, buffer));
}

void TextureEncoder::beginCommandBuffer(VkCommandBuffer *buffer)
{
	if (!buffer)
		buffer = &vkCommandBuffer;
	static VkCommandBufferBeginInfo info{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};
	VK_CHECK(vkBeginCommandBuffer(*buffer, &info));
}

void TextureEncoder::endCommandBuffer(VkCommandBuffer *buffer)
{
	if (!buffer)
		buffer = &vkCommandBuffer;
	VK_CHECK(vkEndCommandBuffer(*buffer));
}

void TextureEncoder::submitCommandBuffer(VkCommandBuffer *buffer)
{
	if (!buffer)
		buffer = &vkCommandBuffer;
	endCommandBuffer(buffer);
	VkSubmitInfo info{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = buffer,
	};
	VK_CHECK(vkQueueSubmit(vkQueue, 1, &info, nullptr));
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

/* Using Vulkan, we cache/re-use surfaces, so OnSurfaceDataRelease is never called.
 * Instead, we match input/output packets to decide what's available.
 */
void TextureEncoder::onReceivePacket(const int64_t &ts)
{
	while (!activeBuffers.empty()) {
		auto &buffer = activeBuffers.front();
		if (buffer->ts > ts)
			break;
		activeBuffers.pop_front();
		availableBuffers.push_back(buffer);
	}
}

void TextureEncoder::onReinitialize(bool full)
{
	for (BufferPtr &buffer : activeBuffers)
		availableBuffers.push_back(buffer);
	activeBuffers.clear();
}
