
#include "amf-encoder-windows.hpp"

using d3dtex_t = ComPtr<ID3D11Texture2D>;

struct TextureEncoder : amf_base, public AMFSurfaceObserver {
	volatile bool destroying = false;

	std::vector<handle_tex> input_textures;

	std::mutex textures_mutex;
	std::vector<d3dtex_t> available_textures;
	std::unordered_map<AMFSurface *, d3dtex_t> active_textures;

	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;

	inline TextureEncoder() : amf_base(false) {}
	~TextureEncoder() { os_atomic_set_bool(&destroying, true); }

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

static HMODULE get_lib(const char *lib)
{
	HMODULE mod = GetModuleHandleA(lib);
	if (mod)
		return mod;

	return LoadLibraryA(lib);
}

#define AMD_VENDOR_ID 0x1002

typedef HRESULT(WINAPI *CREATEDXGIFACTORY1PROC)(REFIID, void **);

static bool amf_init_d3d11(TextureEncoder *enc)
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

static void add_output_tex(TextureEncoder *enc, ComPtr<ID3D11Texture2D> &output_tex, ID3D11Texture2D *from)
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

static inline bool get_available_tex(TextureEncoder *enc, ComPtr<ID3D11Texture2D> &output_tex)
{
	std::scoped_lock lock(enc->textures_mutex);
	if (enc->available_textures.size()) {
		output_tex = enc->available_textures.back();
		enc->available_textures.pop_back();
		return true;
	}

	return false;
}

static inline void get_output_tex(TextureEncoder *enc, ComPtr<ID3D11Texture2D> &output_tex, ID3D11Texture2D *from)
{
	if (!get_available_tex(enc, output_tex))
		add_output_tex(enc, output_tex, from);
}

static void get_tex_from_handle(TextureEncoder *enc, uint32_t handle, IDXGIKeyedMutex **km_out,
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

// On create
if (!amf_init_d3d11(enc.get()))
	throw "Failed to create D3D11";

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
	error("%s: %s: %ls", __FUNCTION__, err.str, amfTrace->GetResultText(err.res));
	*received_packet = false;
	return false;

} catch (const HRError &err) {
	amf_texencode *enc = (amf_texencode *)data;
	error("%s: %s: 0x%lX", __FUNCTION__, err.str, err.hr);
	*received_packet = false;
	return false;
}
