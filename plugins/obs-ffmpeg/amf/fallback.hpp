#pragma once

#include "encoder.hpp"

#include <mutex>
#include <unordered_map>

using HostBufferPtr = shared_ptr<uint8_t[]>;

class FallbackEncoder : public Encoder, public AMFSurfaceObserver {

public:
	FallbackEncoder(obs_encoder_t *encoder, CodecType codec, VideoInfo &videoInfo, string name, uint32_t deviceID);
	virtual ~FallbackEncoder();

	void encode(encoder_frame *frame, encoder_packet *packet, bool *receivedPacket);

private:
	uint32_t frameSize = 0;
	uint32_t lineSize;
	amf_size planeCount;
	vector<pair<unsigned int, unsigned int>> planeSizes;

	mutex bufferMutex;
	vector<HostBufferPtr> buffers;
	unordered_map<AMFSurface *, HostBufferPtr> activeBuffers;
	volatile bool destroying = false;

	HostBufferPtr getBuffer(encoder_frame *frame);

	virtual void onReinitialize() override;
	void AMF_STD_CALL OnSurfaceDataRelease(AMFSurface *surface) override;
};
