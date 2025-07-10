
#include "windows.hpp"

TextureEncoder::TextureEncoder(obs_encoder_t *encoder, CodecType codec, VideoInfo &videoInfo, string name,
			       uint32_t deviceID)
	: Encoder(encoder, codec, videoInfo, name, deviceID)
{
}

TextureEncoder::~TextureEncoder() {}

bool TextureEncoder::encode(uint32_t handle, int64_t pts, uint64_t lockKey, uint64_t *nextKey, encoder_packet *packet,
			    bool *receivedPacket)
{
	return false;
}

void AMF_STD_CALL TextureEncoder::OnSurfaceDataRelease(AMFSurface *surface) {}
