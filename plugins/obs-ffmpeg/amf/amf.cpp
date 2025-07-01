
#include "amf.hpp"

AMFException::AMFException(const char *message, AMF_RESULT result)
	: message(message),
	  result(result),
	  resultText(amfTrace->GetResultText(result))
{
}

const char *AMFException::what()
{
	return message;
}

bool getCaps(CodecType codec, AMFCaps **caps)
{
	AMFContextPtr context;
	if (AMF_FAILED(amfFactory->CreateContext(&context)))
		return false;
#ifdef _WIN32
	if (AMF_FAILED(context->InitDX11(nullptr, AMF_DX11_1)))
		return false;
#elif defined(__linux__)
	AMFContext1Ptr context1 = AMFContext1Ptr(context);
	if (AMF_FAILED(context1->InitVulkan(nullptr)))
		return false;
#endif
	const wchar_t *id = getEncoderID(codec);
	AMFComponentPtr component;
	if (AMF_FAILED(amfFactory->CreateComponent(context, id, &component)))
		return false;
	return AMF_SUCCEEDED(component->GetCaps(caps));
}

const wchar_t *getEncoderID(CodecType codec)
{
	switch (codec) {
	case CodecType::AVC:
		return AMFVideoEncoderVCE_AVC;
	case CodecType::HEVC:
		return AMFVideoEncoder_HEVC;
	case CodecType::AV1:
		return AMFVideoEncoder_AV1;
	}
}

bool getBool(AMFPropertyStorage *storage, const wchar_t *name)
{
	bool value = false;
	storage->GetProperty(name, &value);
	return value;
}

amf_int64 getInt(AMFPropertyStorage *storage, const wchar_t *name, amf_int64 value)
{
	storage->GetProperty(name, &value);
	return value;
}
