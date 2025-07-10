
#include "fallback.hpp"

#include <util/threading.h>

FallbackEncoder::FallbackEncoder(obs_encoder_t *encoder, CodecType codec, VideoInfo &videoInfo, string name,
				 uint32_t deviceID)
	: Encoder(encoder, codec, videoInfo, name, deviceID)
{
}

FallbackEncoder::~FallbackEncoder()
{
	os_atomic_set_bool(&destroying, true);
	terminate();
}

void FallbackEncoder::encode(encoder_frame *frame, struct encoder_packet *packet, bool *receivedPacket)
{
	if (!frameSize) {
		lineSize = frame->linesize[0];

		// Allocate a temporary surface so we can query plane information
		AMFSurfacePtr surface;
		AMF_CHECK(amfContext->AllocSurface(MEMORY_TYPE, videoInfo.format, width, height, &surface),
			  "AllocSurface failed");

		frameSize = 0;
		planeCount = surface->GetPlanesCount();
		planeSizes.reserve(planeCount);
		for (amf_size i = 0; i < planeCount; i++) {
			AMFPlane *plane = surface->GetPlaneAt(i);
			uint32_t size = plane->GetWidth() * plane->GetHeight() * plane->GetPixelSizeInBytes();
			planeSizes.push_back(make_pair(frameSize, size));
			frameSize += size;
		}
	}

	HostBufferPtr buffer = getBuffer(frame);
	uint8_t *data = buffer.get();
	int offset = 0;
	for (amf_size i = planeCount; i-- > 0;) {
		auto &size = planeSizes.at(i);
		memcpy(&data[size.first], frame->data[i], size.second);
	}

	AMFSurfacePtr surface;
	AMF_CHECK(amfContext->CreateSurfaceFromHostNative(videoInfo.format, width, height, lineSize, 0, data, &surface,
							  this),
		  "CreateSurfaceFromHostNative failed");

	int64_t &pts = frame->pts;
	surface->SetPts(timestampToAMF(pts));
	surface->SetProperty(L"PTS", pts);

	{
		scoped_lock lock(bufferMutex);
		activeBuffers[surface.GetPtr()] = buffer;
	}

	submit(surface, packet, receivedPacket);
}

inline HostBufferPtr FallbackEncoder::getBuffer(encoder_frame *frame)
{
	{
		scoped_lock lock(bufferMutex);

		if (buffers.size()) {
			HostBufferPtr buffer = buffers.back();
			buffers.pop_back();
			return buffer;
		}
	}

	return shared_ptr<uint8_t[]>(new uint8_t[frameSize]);
}

void FallbackEncoder::onReinitialize()
{
	scoped_lock lock(bufferMutex);

	for (auto &pair : activeBuffers)
		buffers.push_back(pair.second);
	activeBuffers.clear();
}

void AMF_STD_CALL FallbackEncoder::OnSurfaceDataRelease(AMFSurface *surface)
{
	if (os_atomic_load_bool(&destroying))
		return;

	scoped_lock lock(bufferMutex);

	auto it = activeBuffers.find(surface);
	if (it != activeBuffers.end()) {
		buffers.push_back(it->second);
		activeBuffers.erase(it);
	}
}
