#pragma once

#include "amf.hpp"

#include <obs-module.h>

struct EncoderType {
	const char *name;
	const char *id;
	CodecType codec;
};

struct AdapterCapabilities {
	const bool avc;
	const bool hevc;
	const bool av1;
};

bool adapterSupports(CodecType codec);

obs_properties_t *createProperties(void *, void *typeData);
void setPropertyDefaults(obs_data_t *data, void *);
bool onPropertyModified(void *typeData, obs_properties_t *props, obs_property_t *prop, obs_data_t *data);

const char *getName(void *typeData);
void *createTextureEncoder(obs_data_t *data, obs_encoder_t *encoder);
void *createFallbackEncoder(obs_data_t *data, obs_encoder_t *encoder);

#ifdef _WIN32
bool encodeTexture(void *encData, uint32_t handle, int64_t pts, uint64_t lockKey, uint64_t *nextKey,
		   encoder_packet *packet, bool *receivedPacket);
#else
bool encodeTexture2(void *encData, encoder_texture *texture, int64_t pts, uint64_t, uint64_t *, encoder_packet *packet,
		    bool *receivedPacket);
#endif
bool encodeFallback(void *encData, struct encoder_frame *frame, struct encoder_packet *packet, bool *receivedPacket);

bool getExtraData(void *encData, uint8_t **header, size_t *size);
bool updateSettings(void *encData, obs_data_t *data);
void destroy(void *encData);
