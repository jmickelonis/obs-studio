#pragma once

/* AMF texture encoding on Linux based on work by David Rosca:
   https://github.com/nowrep/obs-studio */

#include "encoder.hpp"

#include <unordered_map>

#include <GL/glcorearb.h>
#include <GL/glext.h>
#include <AMF/core/VulkanAMF.h>

struct GLFunctions {

	PFNGLGETERRORPROC GetError;
	PFNGLCREATEMEMORYOBJECTSEXTPROC CreateMemoryObjectsEXT;
	PFNGLDELETEMEMORYOBJECTSEXTPROC DeleteMemoryObjectsEXT;
	PFNGLIMPORTMEMORYFDEXTPROC ImportMemoryFdEXT;
	PFNGLISMEMORYOBJECTEXTPROC IsMemoryObjectEXT;
	PFNGLMEMORYOBJECTPARAMETERIVEXTPROC MemoryObjectParameterivEXT;
	PFNGLGENTEXTURESPROC GenTextures;
	PFNGLDELETETEXTURESPROC DeleteTextures;
	PFNGLBINDTEXTUREPROC BindTexture;
	PFNGLTEXPARAMETERIPROC TexParameteri;
	PFNGLTEXSTORAGEMEM2DEXTPROC TexStorageMem2DEXT;
	PFNGLGENSEMAPHORESEXTPROC GenSemaphoresEXT;
	PFNGLDELETESEMAPHORESEXTPROC DeleteSemaphoresEXT;
	PFNGLIMPORTSEMAPHOREFDEXTPROC ImportSemaphoreFdEXT;
	PFNGLISSEMAPHOREEXTPROC IsSemaphoreEXT;
	PFNGLSIGNALSEMAPHOREEXTPROC SignalSemaphoreEXT;
	PFNGLGENFRAMEBUFFERSPROC GenFramebuffers;
	PFNGLDELETEFRAMEBUFFERSPROC DeleteFramebuffers;
	PFNGLBINDFRAMEBUFFERPROC BindFramebuffer;
	PFNGLFRAMEBUFFERTEXTURE2DPROC FramebufferTexture2D;
	PFNGLBLITFRAMEBUFFERPROC BlitFramebuffer;

	GLFunctions();
};

#define GL_CHECK(FN) \
	{ \
		FN; \
		int res = gl->GetError(); \
		if (res != GL_NO_ERROR) { \
			blog(LOG_ERROR, "[%s:%d] OpenGL error %d", __FILE_NAME__, __LINE__, res); \
			throw "OpenGL error"; \
		} \
	}

#define VK_CHECK(FN) \
	{ \
		VkResult res = FN; \
		if (res != VK_SUCCESS) { \
			blog(LOG_ERROR, "[%s:%d] Vulkan error %d", __FILE_NAME__, __LINE__, res); \
			throw "Vulkan error"; \
		} \
	}

struct Buffer {
	AMFVulkanSurface vulkanSurface = {};
	AMFSurfacePtr surface;
	VkCommandBuffer copyCommandBuffer;
	int64_t ts;
};
typedef shared_ptr<Buffer> BufferPtr;

struct Plane {
	uint32_t w;
	uint32_t h;
	VkImage vkImage;
	VkDeviceMemory vkMemory;
	GLuint glMemory;
	GLuint glTexture;
	GLuint glFBO;
};

class TextureEncoder : public Encoder {

public:
	TextureEncoder(CodecType codec, obs_encoder_t *encoder, VideoInfo &videoInfo, string name);
	virtual ~TextureEncoder();

	bool encode(encoder_texture *texture, int64_t pts, encoder_packet *packet, bool *received_packet);

private:
	VkInstance vkInstance;
	VkDevice vkDevice;
	VkPhysicalDevice vkPhysicalDevice;
	PFN_vkGetMemoryFdKHR vkGetMemoryFdKHR;
	PFN_vkGetSemaphoreFdKHR vkGetSemaphoreFdKHR;
	AMFVulkanDevice amfVulkanDevice;

	VkFormat vkFormat;
	VkSubmitInfo vkCopySubmitInfo;

	VkCommandBuffer vkCommandBuffer = nullptr;
	VkCommandPool vkCommandPool = nullptr;
	VkFence vkFence = nullptr;
	VkQueue vkQueue = nullptr;
	VkSemaphore vkSemaphore = nullptr;

	GLuint glSemaphore = 0;

	unsigned int planeCount = 0;
	unique_ptr<Plane[]> planesPtr;
	unique_ptr<GLenum[]> glDstLayoutsPtr;
	unique_ptr<GLuint[]> glTexturesPtr;
	unordered_map<gs_texture *, GLuint> readFBOs;

	vector<BufferPtr> buffers;
	vector<BufferPtr> availableBuffers;
	deque<BufferPtr> activeBuffers;

	virtual void *getVulkanDevice() override;
	void createTextures(encoder_texture *from);
	GLuint getReadFBO(gs_texture *tex);
	BufferPtr getBuffer();
	void clearBuffers();
	void allocateCommandBuffer(VkCommandBuffer *buffer);
	void beginCommandBuffer(VkCommandBuffer *buffer = nullptr);
	void endCommandBuffer(VkCommandBuffer *buffer = nullptr);
	void submitCommandBuffer(VkCommandBuffer *buffer = nullptr);
	uint32_t getMemoryTypeIndex(VkMemoryPropertyFlags properties, uint32_t typeBits);

	virtual void onReceivePacket(const int64_t &ts) override;
	virtual void onReinitialize(bool full) override;
};
