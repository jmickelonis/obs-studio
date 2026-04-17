#pragma once

#include "encoder.hpp"

#include <mutex>
#include <unordered_map>

using std::mutex;
using std::unordered_map;

using Texture = ID3D11Texture2D;
using TexturePtr = ComPtr<ID3D11Texture2D>;

struct InputTexture {
	TexturePtr texture;
	ComPtr<IDXGIKeyedMutex> mutex;
};
using InputTexturePtr = shared_ptr<InputTexture>;

class TextureEncoder : public Encoder, public AMFSurfaceObserver {

public:
	TextureEncoder(obs_encoder_t *encoder, CodecType codec, VideoInfo &videoInfo, string name, uint32_t deviceID);
	virtual ~TextureEncoder();

	void encode(uint32_t handle, int64_t pts, uint64_t lockKey, uint64_t *nextKey, encoder_packet *packet,
		    bool *receivedPacket);

private:
	volatile bool destroying = false;
	unordered_map<uint32_t, InputTexturePtr> inputTextures;
	mutex textureMutex;
	vector<TexturePtr> availableTextures;
	unordered_map<AMFSurface *, TexturePtr> activeTextures;

	InputTexturePtr getInputTexture(uint32_t handle);
	TexturePtr getOutputTexture(Texture *from);

	void AMF_STD_CALL OnSurfaceDataRelease(AMFSurface *surface) override;
	virtual void onReinitialize() override;
};

#define WIN_CHECK(FN, MSG) \
	{ \
		HRESULT res = FN; \
		if (FAILED(res)) \
			throw MSG; \
	}
#define WIN_FAILED(FN) FN < 0
