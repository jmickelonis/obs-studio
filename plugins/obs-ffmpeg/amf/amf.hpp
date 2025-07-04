
#pragma once

#include <exception>

#include <AMF/components/VideoEncoderVCE.h>
#include <AMF/components/VideoEncoderHEVC.h>
#include <AMF/components/VideoEncoderAV1.h>
#include <AMF/core/Context.h>
#include <AMF/core/Factory.h>
#include <AMF/core/Trace.h>

#ifdef __linux__
#include <memory>
#include <vector>
#include <AMF/core/VulkanAMF.h>
using std::shared_ptr;
using std::vector;
#endif

using namespace amf;

#ifndef __FILE_NAME__
#define __FILE_NAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

extern AMFFactory *amfFactory;
extern AMFTrace *amfTrace;
extern amf_uint64 amfVersion;

enum class CodecType {
	AVC,
	HEVC,
	AV1,
};

class AMFException : public std::exception {

public:
	const char *const message;
	const AMF_RESULT result;
	const wchar_t *const resultText;

	AMFException(const char *message, AMF_RESULT result);
	virtual const char *what();
};

#ifdef __linux__

struct VulkanDevice : public AMFVulkanDevice {
	~VulkanDevice();
};

shared_ptr<VulkanDevice> createDevice(AMFContext1Ptr context, uint32_t id, const vector<const char *> &extensions = {});

#endif

bool getCaps(uint32_t deviceID, CodecType codec, AMFCaps **caps);
const wchar_t *getEncoderID(CodecType codec);

bool getBool(AMFPropertyStorage *storage, const wchar_t *name);
amf_int64 getInt(AMFPropertyStorage *storage, const wchar_t *name, amf_int64 value = -1);

#define AMF_CHECK(FUNCTION, ERROR) \
	{ \
		AMF_RESULT result = FUNCTION; \
		if (result != AMF_OK) \
			throw AMFException(ERROR, result); \
	}
#define AMF_SUCCEEDED(FUNCTION) FUNCTION == AMF_OK
#define AMF_FAILED(FUNCTION) FUNCTION != AMF_OK

#define AVC_PROPERTY(NAME) AMF_VIDEO_ENCODER_ ## NAME
#define AVC_GET(NAME, VALUE) getProperty(AVC_PROPERTY(NAME), VALUE)
#define AVC_GET_BOOL(NAME) getBool(storage, AVC_PROPERTY(NAME))
#define AVC_GET_INT(NAME) getInt(storage, AVC_PROPERTY(NAME))
#define AVC_SET(NAME, VALUE) setProperty(AVC_PROPERTY(NAME), VALUE)
#define AVC_SET_ENUM(NAME, VALUE) AVC_SET(NAME, AVC_PROPERTY(NAME ## _ ## VALUE))

#define HEVC_PROPERTY(NAME) AMF_VIDEO_ENCODER_HEVC_ ## NAME
#define HEVC_GET(NAME, VALUE) getProperty(HEVC_PROPERTY(NAME), VALUE)
#define HEVC_GET_BOOL(NAME) getBool(storage, HEVC_PROPERTY(NAME))
#define HEVC_GET_INT(NAME) getInt(storage, HEVC_PROPERTY(NAME))
#define HEVC_SET(NAME, VALUE) setProperty(HEVC_PROPERTY(NAME), VALUE)
#define HEVC_SET_ENUM(NAME, VALUE) HEVC_SET(NAME, HEVC_PROPERTY(NAME ## _ ## VALUE))

#define AV1_PROPERTY(NAME) AMF_VIDEO_ENCODER_AV1_ ## NAME
#define AV1_GET(NAME, VALUE) getProperty(AV1_PROPERTY(NAME), VALUE)
#define AV1_GET_BOOL(NAME) getBool(storage, AV1_PROPERTY(NAME))
#define AV1_GET_INT(NAME) getInt(storage, AV1_PROPERTY(NAME))
#define AV1_SET(NAME, VALUE) setProperty(AV1_PROPERTY(NAME), VALUE)
#define AV1_SET_ENUM(NAME, VALUE) AV1_SET(NAME, AV1_PROPERTY(NAME ## _ ## VALUE))

#define AMF_PROPERTY(NAME) \
	((codec == CodecType::AV1) ? AV1_PROPERTY(NAME) \
		: (codec == CodecType::HEVC) ? HEVC_PROPERTY(NAME) \
		: AVC_PROPERTY(NAME))
#define AMF_GET(NAME, VALUE) getProperty(AMF_PROPERTY(NAME), VALUE)
#define AMF_GET_BOOL(NAME) getBool(storage, AMF_PROPERTY(NAME))
#define AMF_GET_INT(NAME) getInt(storage, AMF_PROPERTY(NAME))
#define AMF_SET(NAME, VALUE) setProperty(AMF_PROPERTY(NAME), VALUE)
#define AMF_SET_ENUM(NAME, VALUE) AMF_SET(NAME, AMF_PROPERTY(NAME ## _ ## VALUE))

#define AVC_CAP(NAME) AVC_PROPERTY(CAP_ ## NAME)
#define AVC_GET_CAP(NAME, VALUE) caps->GetProperty(AVC_CAP(NAME), VALUE)
#define AVC_GET_BOOL_CAP(NAME) getBool(caps, AVC_CAP(NAME))
#define AVC_GET_INT_CAP(NAME) getInt(caps, AVC_CAP(NAME))

#define HEVC_CAP(NAME) HEVC_PROPERTY(CAP_ ## NAME)
#define HEVC_GET_CAP(NAME, VALUE) caps->GetProperty(HEVC_CAP(NAME), VALUE)
#define HEVC_GET_BOOL_CAP(NAME) getBool(caps, HEVC_CAP(NAME))
#define HEVC_GET_INT_CAP(NAME) getInt(caps, HEVC_CAP(NAME))

#define AV1_CAP(NAME) AV1_PROPERTY(CAP_ ## NAME)
#define AV1_GET_CAP(NAME, VALUE) caps->GetProperty(AV1_CAP(NAME), VALUE)
#define AV1_GET_BOOL_CAP(NAME) getBool(caps, AV1_CAP(NAME))
#define AV1_GET_INT_CAP(NAME) getInt(caps, AV1_CAP(NAME))

#define AMF_CAP(NAME) AMF_PROPERTY(CAP_ ## NAME)
#define AMF_GET_CAP(NAME, VALUE) caps->GetProperty(AMF_CAP(NAME), VALUE)
#define AMF_GET_BOOL_CAP(NAME) getBool(caps, AMF_CAP(NAME))
#define AMF_GET_INT_CAP(NAME) getInt(caps, AMF_CAP(NAME))
