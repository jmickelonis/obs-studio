
#include "windows.hpp"

#include <d3d11_1.h>

#include <util/threading.h>

typedef HRESULT(WINAPI *CREATEDXGIFACTORY1PROC)(REFIID, void **);

static HMODULE getLib(const char *lib)
{
	HMODULE mod = GetModuleHandleA(lib);
	return mod ? mod : LoadLibraryA(lib);
}

DirectXDevice createDevice(uint32_t id)
{
	HMODULE dxgi = getLib("DXGI.dll");
	HMODULE d3d11 = getLib("D3D11.dll");

	if (!(dxgi && d3d11))
		throw "Couldn't get D3D11/DXGI libraries";

	CREATEDXGIFACTORY1PROC CreateDXGIFactory1 = (CREATEDXGIFACTORY1PROC)GetProcAddress(dxgi, "CreateDXGIFactory1");
	PFN_D3D11_CREATE_DEVICE D3D11CreateDevice = (PFN_D3D11_CREATE_DEVICE)GetProcAddress(d3d11, "D3D11CreateDevice");

	if (!(CreateDXGIFactory1 && D3D11CreateDevice))
		throw "Failed to load D3D11/DXGI procedures";

	ComPtr<IDXGIFactory> dxgiFactory;
	WIN_CHECK(CreateDXGIFactory1(__uuidof(IDXGIFactory2), (void **)&dxgiFactory), "CreateDXGIFactory1 failed");

	ComPtr<IDXGIAdapter> dxgiAdapter;
	uint32_t index = 0;
	for (uint32_t index = 0; true; index++) {
		if (WIN_FAILED(dxgiFactory->EnumAdapters(index, &dxgiAdapter)))
			break;

		DXGI_ADAPTER_DESC desc;
		if (WIN_FAILED(dxgiAdapter->GetDesc(&desc)))
			continue;

		if (id && desc.DeviceId != id)
			continue;

		if (desc.VendorId != 0x1002)
			throw "AMF is trying to initialize on a non-AMD adapter";

		DirectXDevice device;
		WIN_CHECK(D3D11CreateDevice(dxgiAdapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
					    D3D11_SDK_VERSION, &device.device, nullptr, &device.context),
			  "D3D11CreateDevice failed");
		return device;
	}

	if (id) {
		stringstream ss;
		ss << "Failed to find D3D11 device with ID 0x" << std::hex << id;
		throw ss.str();
	}

	throw "Failed to find D3D11 device";
}

TextureEncoder::TextureEncoder(obs_encoder_t *encoder, CodecType codec, VideoInfo &videoInfo, string name,
			       uint32_t deviceID)
	: Encoder(encoder, codec, videoInfo, name, deviceID)
{
}

TextureEncoder::~TextureEncoder()
{
	os_atomic_set_bool(&destroying, true);
	terminate();
}

void TextureEncoder::encode(uint32_t handle, int64_t pts, uint64_t lockKey, uint64_t *nextKey, encoder_packet *packet,
			    bool *receivedPacket)
{
	if (handle == GS_INVALID_HANDLE) {
		*nextKey = lockKey;
		throw "Bad texture handle";
	}

	InputTexturePtr inputTexture = getInputTexture(handle);
	TexturePtr &texture = inputTexture->texture;
	ComPtr<IDXGIKeyedMutex> &mutex = inputTexture->mutex;

	TexturePtr outputTexture = getOutputTexture(texture);
	mutex->AcquireSync(lockKey, INFINITE);
	dxContext->CopyResource((ID3D11Resource *)outputTexture.Get(), (ID3D11Resource *)texture.Get());
	dxContext->Flush();
	mutex->ReleaseSync(*nextKey);

	AMFSurfacePtr surface;
	AMF_CHECK(amfContext->CreateSurfaceFromDX11Native(outputTexture, &surface, this),
		  "CreateSurfaceFromDX11Native failed");

	surface->SetPts(timestampToAMF(pts));
	surface->SetProperty(L"PTS", pts);

	{
		scoped_lock lock(textureMutex);
		activeTextures[surface.GetPtr()] = outputTexture;
	}

	submit(surface, packet, receivedPacket);
}

inline InputTexturePtr TextureEncoder::getInputTexture(uint32_t handle)
{
	auto it = inputTextures.find(handle);
	if (it != inputTextures.end())
		return it->second;

	TexturePtr texture;

	WIN_CHECK(dxDevice->OpenSharedResource((HANDLE)(uintptr_t)handle, __uuidof(ID3D11Resource),
						 (void **)&texture),
		  "OpenSharedResource failed");
	texture->SetEvictionPriority(DXGI_RESOURCE_PRIORITY_MAXIMUM);

	ComQIPtr<IDXGIKeyedMutex> mutex(texture);
	if (!mutex)
		throw "QueryInterface(IDXGIKeyedMutex) failed";

	InputTexturePtr texturePtr = InputTexturePtr(new InputTexture{texture, mutex.Detach()});
	inputTextures[handle] = texturePtr;
	return texturePtr;
}

inline TexturePtr TextureEncoder::getOutputTexture(Texture *from)
{
	{
		scoped_lock lock(textureMutex);
		if (availableTextures.size()) {
			TexturePtr texture = availableTextures.back();
			availableTextures.pop_back();
			return texture;
		}
	}

	D3D11_TEXTURE2D_DESC desc;
	from->GetDesc(&desc);
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	desc.MiscFlags = 0;

	TexturePtr texture;
	WIN_CHECK(dxDevice->CreateTexture2D(&desc, nullptr, &texture), "Failed to create texture");
	return texture;
}

void TextureEncoder::onReinitialize()
{
	scoped_lock lock(textureMutex);

	for (auto &pair : activeTextures)
		availableTextures.push_back(pair.second);
	activeTextures.clear();
}

void AMF_STD_CALL TextureEncoder::OnSurfaceDataRelease(AMFSurface *surface)
{
	if (os_atomic_load_bool(&destroying))
		return;

	scoped_lock lock(textureMutex);

	auto it = activeTextures.find(surface);
	if (it != activeTextures.end()) {
		availableTextures.push_back(it->second);
		activeTextures.erase(it);
	}
}
