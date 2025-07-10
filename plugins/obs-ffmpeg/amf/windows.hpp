#pragma once

#include "encoder.hpp"

#include <mutex>
#include <unordered_map>

#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi.h>

#include <util/windows/ComPtr.hpp>
#include <util/windows/HRError.hpp>
#include <util/windows/device-enum.h>

using std::mutex;
using std::unordered_map;

using d3dtex_t = ComPtr<ID3D11Texture2D>;

struct handle_tex {
	uint32_t handle;
	ComPtr<ID3D11Texture2D> tex;
	ComPtr<IDXGIKeyedMutex> km;
};

class TextureEncoder : public Encoder, public AMFSurfaceObserver {

public:
	volatile bool destroying = false;

	vector<handle_tex> input_textures;

	mutex textures_mutex;
	vector<d3dtex_t> available_textures;
	unordered_map<AMFSurface *, d3dtex_t> active_textures;

	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;

	TextureEncoder(obs_encoder_t *encoder, CodecType codec, VideoInfo &videoInfo, string name, uint32_t deviceID);
	virtual ~TextureEncoder();

	bool encode(uint32_t handle, int64_t pts, uint64_t lockKey, uint64_t *nextKey, encoder_packet *packet,
		    bool *receivedPacket);
	void AMF_STD_CALL OnSurfaceDataRelease(AMFSurface *surface) override;
};
