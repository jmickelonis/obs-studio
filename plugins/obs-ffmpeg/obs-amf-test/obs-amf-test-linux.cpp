
#include <vulkan/vulkan.hpp>

#include <AMF/core/Factory.h>
#include <AMF/core/Trace.h>
#include <AMF/core/VulkanAMF.h>
#include <AMF/components/VideoEncoderVCE.h>
#include <AMF/components/VideoEncoderHEVC.h>
#include <AMF/components/VideoEncoderAV1.h>

#include <iostream>
#include <sstream>
#include <vector>

using namespace amf;
using namespace std;

#define VK_CHECK(FN, MSG) \
	{ \
		VkResult res = FN; \
		if (res != VK_SUCCESS) \
			throw MSG; \
	}

#define AMF_CHECK(FN, MSG) \
	{ \
		AMF_RESULT res = FN; \
		if (res != AMF_OK) \
			throw MSG; \
	}
#define AMF_SUCCEEDED(FN) FN == AMF_OK
#define AMF_FAILED(FN) FN != AMF_OK

static VkInstance createInstance()
{
	VkApplicationInfo appInfo{
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "obs-amf-test",
		.apiVersion = VK_API_VERSION_1_2,
	};
	vector<const char *> extensions{
		VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
		VK_KHR_SURFACE_EXTENSION_NAME,
	};
	VkInstanceCreateInfo info{
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &appInfo,
		.enabledExtensionCount = (uint32_t)extensions.size(),
		.ppEnabledExtensionNames = extensions.data(),
	};
	VkInstance instance;
	VK_CHECK(vkCreateInstance(&info, nullptr, &instance), "Failed to initialize Vulkan");
	return instance;
}

static vector<VkPhysicalDevice> getPhysicalDevices(VkInstance instance)
{
	uint32_t count;
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, nullptr), "Failed to get Vulkan device count");

	if (!count)
		throw "No Vulkan devices were found";

	vector<VkPhysicalDevice> result(count);
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, result.data()), "Failed to enumerate Vulkan devices");
	return result;
}

static AMFFactory *amfFactory;

static vector<const char *> getExtensions(AMFContext1Ptr amfContext, VkPhysicalDevice device)
{
	amf_size wantCount = 0;
	amfContext->GetVulkanDeviceExtensions(&wantCount, nullptr);
	vector<const char *> want(wantCount);
	amfContext->GetVulkanDeviceExtensions(&wantCount, want.data());

	uint32_t count = 0;
	vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
	vector<VkExtensionProperties> extensions(count);
	vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());

	vector<const char *> result;
	for (const char *name : want) {
		auto it = find_if(extensions.begin(), extensions.end(),
				  [name](VkExtensionProperties e) { return !strcmp(e.extensionName, name); });
		if (it != extensions.end())
			result.push_back(name);
	}
	return result;
}

static VkDevice createDevice(AMFContext1Ptr amfContext, VkPhysicalDevice physicalDevice)
{
	vector<const char *> extensions = getExtensions(amfContext, physicalDevice);

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, &queueFamilyCount, nullptr);

	vector<VkQueueFamilyProperties2> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, &queueFamilyCount, queueFamilies.data());

	vector<VkDeviceQueueCreateInfo> queueCreateInfos;
	for (unsigned int i = 0; i < queueFamilyCount; i++) {
		VkQueueFamilyProperties &queueFamilyProps = queueFamilies.at(i).queueFamilyProperties;
		static const float PRIORITY = 1.0;
		VkDeviceQueueCreateInfo queueCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = i,
			.queueCount = 1,
			.pQueuePriorities = &PRIORITY,
		};
		queueCreateInfos.push_back(queueCreateInfo);
	}

	VkDeviceCreateInfo createInfo{
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount = (uint32_t)queueCreateInfos.size(),
		.pQueueCreateInfos = queueCreateInfos.data(),
		.enabledExtensionCount = (uint32_t)extensions.size(),
		.ppEnabledExtensionNames = extensions.data(),
	};
	VkDevice device = nullptr;
	vkCreateDevice(physicalDevice, &createInfo, nullptr, &device);
	return device;
}

static bool hasEncoder(AMFContextPtr &amfContext, const wchar_t *id)
{
	AMFComponentPtr encoder;
	return AMF_SUCCEEDED(amfFactory->CreateComponent(amfContext, id, &encoder));
}

int main(void)
{
	stringstream ss;

	try {
		VkInstance instance = createInstance();
		vector<VkPhysicalDevice> physicalDevices = getPhysicalDevices(instance);

		void *amfModule = dlopen(AMF_DLL_NAMEA, RTLD_LAZY);
		if (!amfModule)
			throw "Failed to load AMF lib";

		auto amfInit = (AMFInit_Fn)dlsym(amfModule, AMF_INIT_FUNCTION_NAME);
		if (!amfInit)
			throw "Failed to get init func";

		AMF_CHECK(amfInit(AMF_FULL_VERSION, &amfFactory), "AMFInit failed");

		AMFQueryVersion_Fn getVersion = (AMFQueryVersion_Fn)dlsym(amfModule, AMF_QUERY_VERSION_FUNCTION_NAME);
		if (!getVersion)
			throw "Failed to get AMFQueryVersion address";

		amf_uint64 amfVersion;
		AMF_CHECK(getVersion(&amfVersion), "AMFQueryVersion failed");

		AMFTrace *amfTrace;
		AMF_CHECK(amfFactory->GetTrace(&amfTrace), "GetTrace failed");
		amfTrace->EnableWriter(AMF_TRACE_WRITER_DEBUG_OUTPUT, false);
		amfTrace->EnableWriter(AMF_TRACE_WRITER_CONSOLE, false);

		VkPhysicalDeviceDriverProperties driverProps{
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES,
		};
		VkPhysicalDeviceProperties2 deviceProps{
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
			.pNext = &driverProps,
		};

		auto field = [&](const char *name, const char *value) {
			ss << name << "=" << value << "\n";
		};
		auto intField = [&](const char *name, int value) {
			ss << name << "=" << value << "\n";
		};
		auto boolField = [&](const char *name, bool value) {
			field(name, value ? "true" : "false");
		};
		auto error = [&](const char *s) {
			field("error", s);
		};

		for (int i = 0; i < physicalDevices.size(); i++) {
			VkPhysicalDevice &physicalDevice = physicalDevices.at(i);
			vkGetPhysicalDeviceProperties2(physicalDevice, &deviceProps);

			VkPhysicalDeviceProperties &props = deviceProps.properties;
			uint32_t vendorID = props.vendorID;
			bool isAMD = vendorID == 0x1002;

			if (ss.tellp())
				ss << "\n";
			ss << "[" << i << "]\n";
			field("device", props.deviceName);
			intField("device_id", props.deviceID);
			intField("vendor_id", vendorID);
			boolField("is_amd", isAMD);
			field("driver", driverProps.driverName);

			if (!isAMD)
				continue;

			VkDriverId &driverID = driverProps.driverID;

			if (amfVersion < AMF_MAKE_FULL_VERSION(1, 4, 34, 0)) {
				if (driverID != VK_DRIVER_ID_AMD_PROPRIETARY) {
					error("Not using AMD's proprietary driver");
					continue;
				}
			} else {
				switch (driverID) {
				case VK_DRIVER_ID_AMD_PROPRIETARY:
				case VK_DRIVER_ID_AMD_OPEN_SOURCE:
				case VK_DRIVER_ID_MESA_RADV:
					break;
				default:
					error("Not using Mesa/RADV or AMD's driver");
					continue;
				}
			}

			AMFContextPtr amfContext;
			if (AMF_FAILED(amfFactory->CreateContext(&amfContext))) {
				error("CreateContext failed");
				continue;
			}

			AMFContext1Ptr amfContext1 = AMFContext1Ptr(amfContext);
			VkDevice device = createDevice(amfContext1, physicalDevice);

			if (!device) {
				error("vkCreateDevice failed");
				continue;
			}

			AMFVulkanDevice amfVulkanDevice{
				.cbSizeof = sizeof(AMFVulkanDevice),
				.hInstance = instance,
				.hPhysicalDevice = physicalDevice,
				.hDevice = device,
			};

			if (AMF_FAILED(amfContext1->InitVulkan(&amfVulkanDevice))) {
				error("InitVulkan failed");
				continue;
			}

			boolField("supports_avc", hasEncoder(amfContext, AMFVideoEncoderVCE_AVC));
			boolField("supports_hevc", hasEncoder(amfContext, AMFVideoEncoder_HEVC));
			boolField("supports_av1", hasEncoder(amfContext, AMFVideoEncoder_AV1));
		}
	} catch (const char *text) {
		if (ss.tellp())
			ss << "\n";
		ss << "[error]\nstring=" << text << "\n";
	}

	cout << ss.str();
	return 0;
}
