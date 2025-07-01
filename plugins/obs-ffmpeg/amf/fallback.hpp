#pragma once

#include "encoder.hpp"

#include <unordered_map>

using HostBufferPtr = shared_ptr<uint8_t[]>;

class FallbackEncoder : public Encoder, public AMFSurfaceObserver {

public:
	FallbackEncoder(CodecType codec, obs_encoder_t *encoder, VideoInfo &videoInfo, string name);
	virtual ~FallbackEncoder();

	void encode(encoder_frame *frame, encoder_packet *packet, bool *receivedPacket);

private:
	uint32_t frameSize = 0;
	uint32_t lineSize;
	unsigned int planeCount;
	vector<pair<unsigned int, unsigned int>> planeSizes;

	mutex bufferMutex;
	vector<HostBufferPtr> buffers;
	unordered_map<AMFSurface *, HostBufferPtr> activeBuffers;
	volatile bool destroying = false;

	HostBufferPtr getBuffer(encoder_frame *frame);

	virtual void onReinitialize(bool full) override;
	void AMF_STD_CALL OnSurfaceDataRelease(AMFSurface *surface) override;
};
