#include <iostream>
#include <sstream>

#include <d3d11.h>

#include <AMF/core/Factory.h>
#include <AMF/components/VideoEncoderVCE.h>
#include <AMF/components/VideoEncoderHEVC.h>
#include <AMF/components/VideoEncoderAV1.h>

#include <util/windows/ComPtr.hpp>

using namespace amf;
using namespace std;

#ifdef _MSC_VER
extern "C" __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
#endif

#define WIN_CHECK(FN, MSG) \
	{ \
		HRESULT res = FN; \
		if (FAILED(res)) \
			throw MSG; \
	}
#define WIN_FAILED(FN) FN < 0

#define AMF_CHECK(FN, MSG) \
	{ \
		AMF_RESULT res = FN; \
		if (res != AMF_OK) \
			throw MSG; \
	}
#define AMF_SUCCEEDED(FN) FN == AMF_OK
#define AMF_FAILED(FN) FN != AMF_OK

static AMFFactory *amfFactory;

static bool hasEncoder(AMFContextPtr &amfContext, const wchar_t *id)
{
	AMFComponentPtr encoder;
	return AMF_SUCCEEDED(amfFactory->CreateComponent(amfContext, id, &encoder));
}

int main(int argc, char *argv[])
{
	wstringstream ss;

	try {
		HMODULE amfModule = LoadLibraryW(AMF_DLL_NAME);
		if (!amfModule)
			throw "Failed to load AMF lib";

		auto amfInit = (AMFInit_Fn)GetProcAddress(amfModule, AMF_INIT_FUNCTION_NAME);
		if (!amfInit)
			throw "Failed to get init func";

		AMF_CHECK(amfInit(AMF_FULL_VERSION, &amfFactory), "AMFInit failed");

		auto field = [&](const char *name, const char *value) {
			ss << name << "=" << value << endl;
		};
		auto wideField = [&](const char *name, const WCHAR *value) {
			ss << name << "=" << value << endl;
		};
		auto intField = [&](const char *name, int value) {
			ss << name << "=" << value << endl;
		};
		auto boolField = [&](const char *name, bool value) {
			field(name, value ? "true" : "false");
		};
		auto error = [&](const char *s) {
			field("error", s);
		};

		ComPtr<IDXGIFactory> dxgiFactory;
		WIN_CHECK(CreateDXGIFactory1(__uuidof(IDXGIFactory), (void **)&dxgiFactory),
			  "CreateDXGIFactory1 failed");

		ComPtr<IDXGIAdapter> dxgiAdapter;
		uint32_t index = 0;
		for (uint32_t index = 0; true; index++) {
			if (WIN_FAILED(dxgiFactory->EnumAdapters(index, &dxgiAdapter)))
				break;

			if (ss.tellp())
				ss << endl;
			ss << "[" << index << "]" << endl;

			ComPtr<ID3D11Device> device;
			ComPtr<ID3D11DeviceContext> context;
			if (WIN_FAILED(D3D11CreateDevice(dxgiAdapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
							 D3D11_SDK_VERSION, &device, nullptr, &context))) {
				error("D3D11CreateDevice failed");
				continue;
			}

			DXGI_ADAPTER_DESC desc;
			if (WIN_FAILED(dxgiAdapter->GetDesc(&desc))) {
				error("GetDesc failed");
				continue;
			}

			UINT vendorID = desc.VendorId;
			bool isAMD = vendorID == 0x1002;

			wideField("device", desc.Description);
			intField("device_id", desc.DeviceId);
			intField("vendor_id", vendorID);
			boolField("is_amd", isAMD);

			if (!isAMD)
				continue;

			AMFContextPtr amfContext;
			if (AMF_FAILED(amfFactory->CreateContext(&amfContext))) {
				error("CreateContext failed");
				continue;
			}

			if (AMF_FAILED(amfContext->InitDX11(device))) {
				error("InitDX11 failed");
				continue;
			}

			boolField("supports_avc", hasEncoder(amfContext, AMFVideoEncoderVCE_AVC));
			boolField("supports_hevc", hasEncoder(amfContext, AMFVideoEncoder_HEVC));
			boolField("supports_av1", hasEncoder(amfContext, AMFVideoEncoder_AV1));
		}
	} catch (const char *text) {
		if (ss.tellp())
			ss << endl;
		cout << "[error]\nstring=" << text << endl;
	}

	wcout << ss.str();
	return 0;
}
