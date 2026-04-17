
#include "encoder.hpp"

#if __OBS_AMF_SHOW_PROPERTIES
#include "properties.hpp"
#endif

#include <obs-avc.h>
#include <obs-module.h>
#include <opts-parser.h>
#include <util/platform.h>

VideoInfo::VideoInfo(obs_encoder_t *encoder, CodecType codec)
{
	video_t *video = obs_encoder_video(encoder);
	const video_output_info *voi = video_output_get_info(video);

	switch (voi->format) {

	case VIDEO_FORMAT_NV12:
		format = AMF_SURFACE_NV12;
		colorBitDepth = AMF_COLOR_BIT_DEPTH_8;
		break;

	case VIDEO_FORMAT_P010:
		if (codec == CodecType::AVC) {
			const char *const text = obs_module_text("AMF.10bitUnsupportedAvc");
			obs_encoder_set_last_error(encoder, text);
			throw text;
		}
		format = AMF_SURFACE_P010;
		colorBitDepth = AMF_COLOR_BIT_DEPTH_10;
		break;

	case VIDEO_FORMAT_P216:
	case VIDEO_FORMAT_P416: {
		const char *const text = obs_module_text("AMF.16bitUnsupported");
		obs_encoder_set_last_error(encoder, text);
		throw text;
	}

	case VIDEO_FORMAT_BGRA:
		format = AMF_SURFACE_BGRA;
		colorBitDepth = AMF_COLOR_BIT_DEPTH_8;
		break;

	default: {
		stringstream ss;
		ss << "Unsupported format: " << get_video_format_name(voi->format);
		string s = ss.str();
		obs_encoder_set_last_error(encoder, s.c_str());
		throw "Unsupported format";
	}
	}

	fullRangeColor = voi->range == VIDEO_RANGE_FULL;

	switch (voi->colorspace) {

	case VIDEO_CS_601:
		colorProfile = fullRangeColor ? AMF_VIDEO_CONVERTER_COLOR_PROFILE_FULL_601
					      : AMF_VIDEO_CONVERTER_COLOR_PROFILE_601;
		colorPrimaries = AMF_COLOR_PRIMARIES_SMPTE170M;
		colorTransferCharacteristic = AMF_COLOR_TRANSFER_CHARACTERISTIC_SMPTE170M;
		break;

	case VIDEO_CS_709:
	case VIDEO_CS_DEFAULT:
		colorProfile = fullRangeColor ? AMF_VIDEO_CONVERTER_COLOR_PROFILE_FULL_709
					      : AMF_VIDEO_CONVERTER_COLOR_PROFILE_709;
		colorPrimaries = AMF_COLOR_PRIMARIES_BT709;
		colorTransferCharacteristic = AMF_COLOR_TRANSFER_CHARACTERISTIC_BT709;
		break;

	case VIDEO_CS_SRGB:
		colorProfile = fullRangeColor ? AMF_VIDEO_CONVERTER_COLOR_PROFILE_FULL_709
					      : AMF_VIDEO_CONVERTER_COLOR_PROFILE_709;
		colorPrimaries = AMF_COLOR_PRIMARIES_BT709;
		colorTransferCharacteristic = AMF_COLOR_TRANSFER_CHARACTERISTIC_IEC61966_2_1;
		break;

	case VIDEO_CS_2100_HLG:
		colorProfile = fullRangeColor ? AMF_VIDEO_CONVERTER_COLOR_PROFILE_FULL_2020
					      : AMF_VIDEO_CONVERTER_COLOR_PROFILE_2020;
		colorPrimaries = AMF_COLOR_PRIMARIES_BT2020;
		colorTransferCharacteristic = AMF_COLOR_TRANSFER_CHARACTERISTIC_ARIB_STD_B67;
		break;

	case VIDEO_CS_2100_PQ:
		colorProfile = fullRangeColor ? AMF_VIDEO_CONVERTER_COLOR_PROFILE_FULL_2020
					      : AMF_VIDEO_CONVERTER_COLOR_PROFILE_2020;
		colorPrimaries = AMF_COLOR_PRIMARIES_BT2020;
		colorTransferCharacteristic = AMF_COLOR_TRANSFER_CHARACTERISTIC_SMPTE2084;
		break;

	default:
		throw "Unsupported colorspace";
	}

	if (colorBitDepth == AMF_COLOR_BIT_DEPTH_8 && colorPrimaries == AMF_COLOR_PRIMARIES_BT2020) {
		const char *const text = obs_module_text("AMF.8bitUnsupportedHdr");
		obs_encoder_set_last_error(encoder, text);
		throw text;
	}

	frameRate = AMFConstructRate(voi->fps_num, voi->fps_den);
}

template<typename T> inline T VideoInfo::multiplyByFrameRate(T value) const
{
	return value * frameRate.num / frameRate.den;
}

ROI::~ROI()
{
	delete[] buffer;
}

inline void ROI::update(obs_encoder_roi *dataPtr)
{
	obs_encoder_roi &data = *dataPtr;

	// AMF does not support negative priority
	if (data.priority < 0)
		return;

	// Importance value range is 0..10
	amf_uint32 priority = (amf_uint32)(data.priority * 10);

	uint32_t left = data.left / mbSize;
	uint32_t right = min((data.right - 1) / mbSize, width);
	uint32_t top = data.top / mbSize;
	uint32_t bottom = min((data.bottom - 1) / mbSize, height);

	unsigned int yOffset;
	for (uint32_t y = top; y <= bottom; y++) {
		yOffset = y * pitch;
		for (uint32_t x = left; x <= right; x++)
			buffer[yOffset + x] = priority;
	}
}

static void enumROICallback(void *param, obs_encoder_roi *data)
{
	ROI *roi = (ROI *)param;
	roi->update(data);
}

Encoder::Encoder(obs_encoder_t *encoder, CodecType codec, VideoInfo &videoInfo, string name, uint32_t deviceID)
	: encoder(encoder),
	  codec(codec),
	  videoInfo(videoInfo),
	  name(name),
	  deviceID(deviceID),
	  width(obs_encoder_get_width(encoder)),
	  height(obs_encoder_get_height(encoder))
{
	AMF_CHECK(amfFactory->CreateContext(&amfContext), "CreateContext failed");
	amfContext1 = AMFContext1Ptr(amfContext);

	switch (codec) {
	case CodecType::AVC:
	default:
		outputDataTypeProperty = AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE;
		break;
	case CodecType::HEVC:
		outputDataTypeProperty = AMF_VIDEO_ENCODER_HEVC_OUTPUT_DATA_TYPE;
		break;
	case CodecType::AV1:
		outputDataTypeProperty = AMF_VIDEO_ENCODER_AV1_OUTPUT_FRAME_TYPE;
		break;
	}

#if __OBS_AMF_SHOW_PROPERTIES
	showProperties = !getenv("OBS_AMF_DISABLE_PROPERTIES");
#endif

	capabilities = {};
}

Encoder::~Encoder()
{
	terminate();
}

void Encoder::initialize(obs_data_t *data)
{
	createEncoder(data, true);
}

void Encoder::updateSettings(obs_data_t *data)
{
	// This is called with blank data after a connection attempt fails,
	// and if we proceed, we'll end up deadlocked during the drain process
	if (!obs_encoder_active(encoder))
		return;

	uint32_t deviceID = (uint32_t)obs_data_get_int(data, settings::DEVICE);
	if (deviceID && deviceID != this->deviceID) {
		info("Ignoring settings update for other device (0x%d)", deviceID);
		return;
	}

	bool preAnalysis;
	AMF_GET(PRE_ANALYSIS_ENABLE, &preAnalysis);

#if __OBS_AMF_SHOW_PROPERTIES
	const CodecProperties &properties = getCodecProperties(codec);
	PropertyValues oldValues = getPropertyValues(amfEncoder, properties);
#endif

	onReinitialize();

	// Drain the existing output data,
	// saving it all to the queue so we don't lose anything
	AMF_CHECK(amfEncoder->Drain(), "Drain failed");
	AMF_RESULT res;
	AMFDataPtr dataPtr;
	bool draining = true;
	while (draining) {
		res = amfEncoder->QueryOutput(&dataPtr);
		switch (res) {
		case AMF_OK:
			queryQueue.push_back(dataPtr);
			continue;
		case AMF_REPEAT:
			continue;
		case AMF_EOF:
			draining = false;
			break;
		default:
			throw AMFException("Drain failed", res);
		}
	}

	// Terminate the existing encoder and make a new one
	terminateEncoder();
	createEncoder(data, false);

#if __OBS_AMF_SHOW_PROPERTIES
	PropertyValues values = getPropertyValues(amfEncoder, properties);
	stringstream ss;
	printChangedPropertyValues(ss, oldValues, values);
	if (ss.tellp())
		info("updated properties:\n%s", ss.str().c_str());
#endif
}

bool Encoder::getExtraData(uint8_t **data, size_t *size)
{
	if (!extraData)
		return false;

	*data = (uint8_t *)extraData->GetNative();
	*size = extraData->GetSize();
	return true;
}

void Encoder::submit(AMFSurfacePtr &surface, encoder_packet *packet, bool *receivedPacket)
{
	if (capabilities.roi)
		updateROI(surface);

	AMF_RESULT res;
	AMFDataPtr data;
	uint64_t startTime = os_gettime_ns();
	bool submitting = true;

LOOP:
	while (submitting) {
		res = amfEncoder->SubmitInput(surface);

		switch (res) {
		case AMF_OK:
		case AMF_NEED_MORE_INPUT:
			submitting = false;
			break;
		case AMF_INPUT_FULL: {
			os_sleep_ms(1);
			if (os_gettime_ns() - startTime >= 5'000'000'000ULL)
				// Time out after 5 seconds of full input
				throw AMFException("SubmitInput timed out", res);
			break;
		}
		default:
			throw AMFException("SubmitInput failed", res);
		}

		while (true) {
			res = amfEncoder->QueryOutput(&data);

			switch (res) {
			case AMF_OK:
				queryQueue.push_back(data);
				continue;
			case AMF_REPEAT:
				goto LOOP;
			default:
				throw AMFException("QueryOutput failed", res);
			}
		}
	}

	if (!queryQueue.size())
		return;

	data = queryQueue.front();
	queryQueue.pop_front();
	receivePacket(data, packet);
	*receivedPacket = true;
}

int64_t Encoder::timestampToAMF(int64_t ts)
{
	return ts * AMF_SECOND / (int64_t)videoInfo.frameRate.den;
}

int64_t Encoder::timestampToOBS(int64_t ts)
{
	return ts * (int64_t)videoInfo.frameRate.den / AMF_SECOND;
}

void Encoder::terminate()
{
	terminateEncoder();
	queryQueue.clear();
	if (amfContext)
		amfContext->Terminate();
}

void Encoder::terminateEncoder()
{
	if (amfEncoder)
		amfEncoder->Terminate();
	roi.reset();
}

template<typename T> bool Encoder::getProperty(const wchar_t *name, T *value)
{
	return AMF_SUCCEEDED(amfEncoder->GetProperty(name, value));
}

template<typename T> void Encoder::setProperty(const wchar_t *name, const T &value)
{
	AMF_RESULT result = amfEncoder->SetProperty(name, value);
	if (result != AMF_OK)
		error("Failed to set property '%ls': %ls", name, amfTrace->GetResultText(result));
}

void Encoder::createEncoder(obs_data_t *data, bool init)
{
	if (init) {
#ifdef _WIN32
		DirectXDevice device = createDevice(deviceID);
		dxDevice = device.device;
		dxContext = device.context;
		AMF_CHECK(amfContext->InitDX11(dxDevice, AMF_DX11_1), "InitDX11 failed");
#else
		vulkanDevice = createDevice();
		AMF_CHECK(amfContext1->InitVulkan(vulkanDevice.get()), "InitVulkan failed");
#endif
	}

	const wchar_t *id;
	const wchar_t *extraDataProperty;

	switch (codec) {
	case CodecType::AVC:
	default:
		id = AMFVideoEncoderVCE_AVC;
		extraDataProperty = AMF_VIDEO_ENCODER_EXTRADATA;
		break;
	case CodecType::HEVC:
		id = AMFVideoEncoder_HEVC;
		extraDataProperty = AMF_VIDEO_ENCODER_HEVC_EXTRADATA;
		break;
	case CodecType::AV1:
		id = AMFVideoEncoder_AV1;
		extraDataProperty = AMF_VIDEO_ENCODER_AV1_EXTRA_DATA;
		break;
	}

	AMF_CHECK(amfFactory->CreateComponent(amfContext, id, &amfEncoder), "CreateComponent failed");

	if (init) {
		AMFCapsPtr caps;
		const Capabilities *cachedCapabilities = getCapabilities(deviceID, codec, false);

		if (cachedCapabilities) {
			// Capabilities were cached; just copy them
			capabilities = *cachedCapabilities;
		} else if (AMF_SUCCEEDED(amfEncoder->GetCaps(&caps))) {
			// Process the caps and cache them for later
			capabilities.set(codec, caps);
			cacheCapabilities(deviceID, codec, capabilities);
		}

		if (capabilities.preAnalysis && videoInfo.format != AMF_SURFACE_NV12) {
			capabilities.preAnalysis = false;
			warn("Pre-Analysis has been disabled as it requires NV12");
		}

#if __OBS_AMF_SHOW_PROPERTIES
		if (showProperties) {
			if (cachedCapabilities)
				amfEncoder->GetCaps(&caps);

			if (caps) {
				stringstream ss;
				ss << "capabilities:";
				const CodecProperties &properties = getCodecProperties(codec);
				printProperties(ss, caps, properties.capabilities, 1);
				info(ss.str().c_str());
			}
		}
#endif
	}

	Settings settings(capabilities, data);

	if (init) {
		if (settings.isQuality && !capabilities.preAnalysis) {
			// Quality RC methods require Pre-Analysis
			// Instead of hoping for sane fallback settings, just abort
			stringstream ss;
			ss << "Rate Control method \"" << settings.rateControl
			   << "\" requires Pre-Analysis, which is not supported by this system or configuration.";
			string s = ss.str();
			obs_encoder_set_last_error(encoder, s.c_str());
			throw "Unsupported Rate Control method";
		}
	}

	switch (codec) {
	case CodecType::AVC:
		initializeAVC();
		break;
	case CodecType::HEVC:
		initializeHEVC();
		break;
	case CodecType::AV1:
		initializeAV1();
		break;
	}

	shared_ptr<char[]> opts = getUserOptions(data);
	update(settings, opts.get(), init);

	AMF_CHECK(amfEncoder->Init(videoInfo.format, width, height), "AMFComponent::Init failed");

	AMFVariant variant;
	if (AMF_SUCCEEDED(amfEncoder->GetProperty(extraDataProperty, &variant)) &&
	    variant.type == AMF_VARIANT_INTERFACE)
		extraData = AMFBufferPtr(variant.pInterface);

#if __OBS_AMF_SHOW_PROPERTIES
	if (init && showProperties) {
		const CodecProperties &properties = getCodecProperties(codec);
		stringstream ss;
		ss << "active properties:";
		printProperties(ss, amfEncoder, properties, 1);
		info(ss.str().c_str());
	}
#endif
}

void Encoder::initializeAVC()
{
#define SET AVC_SET
	SET(FRAMESIZE, AMFConstructSize(width, height));
	SET(FRAMERATE, videoInfo.frameRate);
	SET(FULL_RANGE_COLOR, videoInfo.fullRangeColor);
	SET(INPUT_COLOR_PRIMARIES, videoInfo.colorPrimaries);
	SET(INPUT_COLOR_PROFILE, videoInfo.colorProfile);
	SET(INPUT_TRANSFER_CHARACTERISTIC, videoInfo.colorTransferCharacteristic);
	SET(OUTPUT_COLOR_PRIMARIES, videoInfo.colorPrimaries);
	SET(OUTPUT_COLOR_PROFILE, videoInfo.colorProfile);
	SET(OUTPUT_TRANSFER_CHARACTERISTIC, videoInfo.colorTransferCharacteristic);
	SET(ENFORCE_HRD, true);
	SET(DE_BLOCKING_FILTER, true);
#undef SET
}

void Encoder::initializeHEVC()
{
	AMF_COLOR_TRANSFER_CHARACTERISTIC_ENUM colorTransferCharacteristic = videoInfo.colorTransferCharacteristic;
	const bool pq = colorTransferCharacteristic == AMF_COLOR_TRANSFER_CHARACTERISTIC_SMPTE2084;
	const bool hlg = colorTransferCharacteristic == AMF_COLOR_TRANSFER_CHARACTERISTIC_ARIB_STD_B67;
	const bool hdr = pq || hlg;

#define SET HEVC_SET
	SET(FRAMESIZE, AMFConstructSize(width, height));
	SET(FRAMERATE, videoInfo.frameRate);
	SET(NOMINAL_RANGE, videoInfo.fullRangeColor);
	SET(INPUT_COLOR_PRIMARIES, videoInfo.colorPrimaries);
	SET(INPUT_COLOR_PROFILE, videoInfo.colorProfile);
	SET(INPUT_TRANSFER_CHARACTERISTIC, videoInfo.colorTransferCharacteristic);
	SET(OUTPUT_COLOR_PRIMARIES, videoInfo.colorPrimaries);
	SET(OUTPUT_COLOR_PROFILE, videoInfo.colorProfile);
	SET(OUTPUT_TRANSFER_CHARACTERISTIC, colorTransferCharacteristic);
	SET(COLOR_BIT_DEPTH, videoInfo.colorBitDepth);
	SET(ENFORCE_HRD, true);
	SET(PROFILE, videoInfo.colorBitDepth == AMF_COLOR_BIT_DEPTH_10 ? AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN_10
								       : AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN);
	if (hdr) {
		AMFBufferPtr buffer;
		AMF_CHECK(amfContext->AllocBuffer(AMF_MEMORY_HOST, sizeof(AMFHDRMetadata), &buffer),
			  "AllocBuffer failed");
		AMFHDRMetadata &md = *(AMFHDRMetadata *)buffer->GetNative();
#define HDR_PRIMARY(NUM, DEN) (amf_uint16)(NUM * 50000 / DEN)
		md.redPrimary[0] = HDR_PRIMARY(17, 25);
		md.redPrimary[1] = HDR_PRIMARY(8, 25);
		md.greenPrimary[0] = HDR_PRIMARY(53, 200);
		md.greenPrimary[1] = HDR_PRIMARY(69, 100);
		md.bluePrimary[0] = HDR_PRIMARY(3, 20);
		md.bluePrimary[1] = HDR_PRIMARY(3, 50);
		md.whitePoint[0] = HDR_PRIMARY(3127, 10000);
		md.whitePoint[1] = HDR_PRIMARY(329, 1000);
#undef HDR_PRIMARY
		int peakLevel = pq ? (int)obs_get_video_hdr_nominal_peak_level() : hlg ? 1000 : 0;
		md.minMasteringLuminance = 0;
		md.maxMasteringLuminance = peakLevel * 10000;
		md.maxContentLightLevel = peakLevel;
		md.maxFrameAverageLightLevel = peakLevel;
		SET(INPUT_HDR_METADATA, buffer);
	}
#undef SET
}

void Encoder::initializeAV1()
{
#define SET AV1_SET
	SET(FRAMESIZE, AMFConstructSize(width, height));
	SET(FRAMERATE, videoInfo.frameRate);
	SET(NOMINAL_RANGE, videoInfo.fullRangeColor);
	SET(INPUT_COLOR_PRIMARIES, videoInfo.colorPrimaries);
	SET(INPUT_COLOR_PROFILE, videoInfo.colorProfile);
	SET(INPUT_TRANSFER_CHARACTERISTIC, videoInfo.colorTransferCharacteristic);
	SET(OUTPUT_COLOR_PRIMARIES, videoInfo.colorPrimaries);
	SET(OUTPUT_COLOR_PROFILE, videoInfo.colorProfile);
	SET(OUTPUT_TRANSFER_CHARACTERISTIC, videoInfo.colorTransferCharacteristic);
	SET(COLOR_BIT_DEPTH, videoInfo.colorBitDepth);
	SET(ENFORCE_HRD, true);
	AV1_SET_ENUM(ALIGNMENT_MODE, NO_RESTRICTIONS);
#undef SET
}

void Encoder::update(Settings &settings, const char *opts, bool init)
{
	AMF_SET(RATE_CONTROL_METHOD, rate_control::getValue(codec, settings.rateControl));
	AMF_SET(QUALITY_PRESET, preset::getValue(codec, settings.preset));

	const wchar_t *gopSizeProperty;
	const wchar_t *levelProperty;

	switch (codec) {
	case CodecType::AVC:
	default:
		gopSizeProperty = AVC_PROPERTY(IDR_PERIOD);
		levelProperty = AVC_PROPERTY(PROFILE_LEVEL);
		updateAVC(settings);
		break;
	case CodecType::HEVC:
		gopSizeProperty = HEVC_PROPERTY(GOP_SIZE);
		levelProperty = HEVC_PROPERTY(PROFILE_LEVEL);
		updateHEVC(settings);
		break;
	case CodecType::AV1:
		gopSizeProperty = AV1_PROPERTY(GOP_SIZE);
		levelProperty = AV1_PROPERTY(LEVEL);
		updateAV1(settings);
		break;
	}

	int gopSize = videoInfo.multiplyByFrameRate(settings.keyFrameInterval);
	setProperty(gopSizeProperty, gopSize);

	const Levels &levels = getLevels(codec);
	int level = getLevel(levels, settings.data);
	setProperty(levelProperty, level);

	setPreAnalysis(settings);
	applyOpts(opts);

	// Look up the final level (may have been changed in user options)
	getProperty(levelProperty, &level);
	const Level *levelInfo = levels.get(level);
	if (!levelInfo)
		warn("Level information not found (%d)", level);

	if (!init)
		return;

	stringstream ss;

	auto field = [&](const char *name) -> stringstream & {
		ss << "\n\t" << name << ": ";
		for (int i = 0; i < 12 - strlen(name); i++)
			ss << " ";
		return ss;
	};

	field("rate_control") << settings.rateControl;
	if (settings.bitrateSupported) {
		field("bitrate") << settings.bitrate / 1000;
		if (settings.useBufferSize)
			field("buffer_size") << settings.getBufferSize() / 1000;
	} else {
		field("qp") << settings.qp;
	}
	field("keyint") << gopSize;
	field("preset") << settings.preset;
	field("profile") << settings.profile;
	field("level") << (levelInfo ? levelInfo->name : "Unknown");
	if (capabilities.bFrames)
		field("b-frames") << settings.bFrames;
	field("width") << width;
	field("height") << height;
	field("params") << ((*opts) ? opts : "(none)");

	info("settings:%s", ss.str().c_str());
}

void Encoder::updateAVC(Settings &settings)
{
#define GET AVC_GET
#define SET AVC_SET

	bool filler = false;
	if (settings.bitrateSupported) {
		int bitrate = settings.bitrate;
		SET(TARGET_BITRATE, bitrate);
		SET(PEAK_BITRATE, bitrate);
		SET(VBV_BUFFER_SIZE, settings.getBufferSize());
		filler = settings.isConstantBitrate;
	} else {
		int qp = settings.qp;
		SET(QP_I, qp);
		SET(QP_P, qp);
		SET(QP_B, qp);
		SET(QVBR_QUALITY_LEVEL, qp);
	}
	SET(FILLER_DATA_ENABLE, filler);

	if (capabilities.bFrames) {
		int bf = settings.bFrames;
		SET(B_REFERENCE_ENABLE, bf > 0);
		SET(MAX_CONSECUTIVE_BPICTURES, bf);
		SET(B_PIC_PATTERN, bf);
		SET(ADAPTIVE_MINIGOP, settings.dynamicBFrames);

		amf_int64 bFrames;
		if (GET(B_PIC_PATTERN, &bFrames))
			dtsOffset = bFrames + 1;
		else
			dtsOffset = 0;
	}

	obs_data_t *data = settings.data;
	SET(PROFILE, profile::avc::getValue(settings.profile));
	SET(ENABLE_VBAQ,
	    settings.aqSupported && (settings.preAnalysis ? STR_EQ(settings.paAQ, pa_aq::VBAQ)
							  : obs_data_get_bool(data, settings::ADAPTIVE_QUANTIZATION)));
	SET(HIGH_MOTION_QUALITY_BOOST_ENABLE,
	    settings.hmqbSupported && obs_data_get_bool(data, settings::HIGH_MOTION_QUALITY_BOOST));
	SET(LOWLATENCY_MODE, obs_data_get_bool(data, settings::LOW_LATENCY));
	SET(PREENCODE_ENABLE, settings.preEncodeSupported && obs_data_get_bool(data, settings::PRE_ENCODE));

#undef GET
#undef SET
}

void Encoder::updateHEVC(Settings &settings)
{
#define GET HEVC_GET
#define SET HEVC_SET

	bool filler = false;
	if (settings.bitrateSupported) {
		int bitrate = settings.bitrate;
		SET(TARGET_BITRATE, bitrate);
		SET(PEAK_BITRATE, bitrate);
		SET(VBV_BUFFER_SIZE, settings.getBufferSize());
		filler = settings.isConstantBitrate;
	} else {
		int qp = settings.qp;
		SET(QP_I, qp);
		SET(QP_P, qp);
		SET(QVBR_QUALITY_LEVEL, qp);
	}
	SET(FILLER_DATA_ENABLE, filler);

	obs_data_t *data = settings.data;
	SET(ENABLE_VBAQ,
	    settings.aqSupported && (settings.preAnalysis ? STR_EQ(settings.paAQ, pa_aq::VBAQ)
							  : obs_data_get_bool(data, settings::ADAPTIVE_QUANTIZATION)));
	SET(HIGH_MOTION_QUALITY_BOOST_ENABLE,
	    settings.hmqbSupported && obs_data_get_bool(data, settings::HIGH_MOTION_QUALITY_BOOST));
	SET(LOWLATENCY_MODE, obs_data_get_bool(data, settings::LOW_LATENCY));
	SET(PREENCODE_ENABLE, settings.preEncodeSupported && obs_data_get_bool(data, settings::PRE_ENCODE));

#undef GET
#undef SET
}

void Encoder::updateAV1(Settings &settings)
{
#define GET AV1_GET
#define SET AV1_SET
#define SET_ENUM AV1_SET_ENUM

	bool filler = false;
	if (settings.bitrateSupported) {
		int bitrate = settings.bitrate;
		SET(TARGET_BITRATE, bitrate);
		SET(PEAK_BITRATE, bitrate);
		SET(VBV_BUFFER_SIZE, settings.getBufferSize());
		filler = settings.isConstantBitrate;
	} else {
		int64_t qp = settings.qp * 4;
		SET(QVBR_QUALITY_LEVEL, qp / 4);
		SET(Q_INDEX_INTRA, qp);
		SET(Q_INDEX_INTER, qp);
		SET(Q_INDEX_INTER_B, qp);
	}
	SET(FILLER_DATA, filler);

	if (capabilities.bFrames) {
		int bf = settings.bFrames;
		SET(MAX_CONSECUTIVE_BPICTURES, bf);
		SET(B_PIC_PATTERN, bf);
		SET(ADAPTIVE_MINIGOP, settings.dynamicBFrames);

		amf_int64 bFrames;
		if (GET(B_PIC_PATTERN, &bFrames))
			dtsOffset = bFrames + 1;
		else
			dtsOffset = 0;
	}

	obs_data_t *data = settings.data;
	SET(PROFILE, AMF_VIDEO_ENCODER_AV1_PROFILE_MAIN);
	SET(HIGH_MOTION_QUALITY_BOOST,
	    settings.hmqbSupported && obs_data_get_bool(data, settings::HIGH_MOTION_QUALITY_BOOST));
	SET(RATE_CONTROL_PREENCODE, settings.preEncodeSupported && obs_data_get_bool(data, settings::PRE_ENCODE));

	if (obs_data_get_bool(data, settings::LOW_LATENCY))
		SET_ENUM(ENCODING_LATENCY_MODE, LOWEST_LATENCY);
	else
		SET_ENUM(ENCODING_LATENCY_MODE, NONE);

	if (settings.aqSupported && (settings.preAnalysis ? STR_EQ(settings.paAQ, pa_aq::CAQ)
							  : obs_data_get_bool(data, settings::ADAPTIVE_QUANTIZATION)))
		SET_ENUM(AQ_MODE, CAQ);
	else
		SET_ENUM(AQ_MODE, NONE);

#undef GET
#undef SET
#undef SET_ENUM
}

int Encoder::getLevel(const Levels &levels, obs_data_t *data)
{
	uint64_t size = width * height;
	uint64_t rate = videoInfo.multiplyByFrameRate(size);
	int maxLevel = (int)capabilities.level;

	const char *name = obs_data_get_string(data, settings::LEVEL);
	if (STR_NE(name, settings::AUTO)) {
		const Level *info = levels.get(name);
		if (info) {
			int level = info->value;
			if (!maxLevel || level <= maxLevel) {
				if (size > info->size || rate > info->rate)
					warn("Sample rate (%dx%d@%d) is too high for level %s", width, height,
					     videoInfo.frameRate.num / videoInfo.frameRate.den, name);
				return level;
			}

			warn("Level not supported (%s); auto-detecting instead", name);
		} else {
			warn("Level not found (%s); auto-detecting instead", name);
		}
	}

	const Level *highest = &levels.back();

	if (maxLevel && maxLevel < highest->value)
		highest = levels.get(maxLevel);

	if (size > highest->size || rate > highest->rate) {
		warn("Sample rate (%dx%d@%d) is too high for maximum supported level (%s)", width, height,
		     videoInfo.frameRate.num / videoInfo.frameRate.den, highest->name);
		return highest->value;
	}

	int value = 0;
	// Prefer higher levels that have identical values to the one before
	for (auto it = levels.rbegin(); it != levels.rend(); ++it) {
		if (size > it->size || rate > it->rate)
			break;
		value = it->value;
	}
	return value;
}

bool Encoder::setPreAnalysis(Settings &settings)
{
	if (!capabilities.preAnalysis)
		return false;

	obs_data_t *data = settings.data;

	bool enabled = settings.preAnalysis;
	bool wasEnabled;
	AMF_GET(PRE_ANALYSIS_ENABLE, &wasEnabled);
	if (enabled != wasEnabled)
		AMF_SET(PRE_ANALYSIS_ENABLE, enabled);

	if (!enabled) {
		if (!capabilities.roi) {
			// Re-enable ROI if it's available
			const Capabilities *cachedCapabilities = getCapabilities(deviceID, codec);
			capabilities.roi = cachedCapabilities->roi;
		}
		return false;
	}

	roi.reset();

	setProperty(AMF_PA_ENGINE_TYPE, MEMORY_TYPE);
	setProperty(AMF_PA_LOOKAHEAD_BUFFER_DEPTH, pa_lookahead::getValue(settings.paLookahead));

	AMF_PA_PAQ_MODE_ENUM paqMode = AMF_PA_PAQ_MODE_NONE;
	AMF_PA_CAQ_STRENGTH_ENUM caqStrength = AMF_PA_CAQ_STRENGTH_LOW;
	AMF_PA_TAQ_MODE_ENUM taqMode = AMF_PA_TAQ_MODE_NONE;

	const char *aq = settings.paAQ;
	if (STR_EQ(aq, pa_aq::TAQ)) {
		if (settings.paTAQSupported) {
			const char *taq = obs_data_get_string(data, settings::PA_TAQ);
			taqMode = pa_taq::getValue(taq);
		} else {
			warn("TAQ is not available; using CAQ instead");
			aq = pa_aq::CAQ;
		}
	}
	if (STR_EQ(aq, pa_aq::CAQ)) {
		paqMode = AMF_PA_PAQ_MODE_CAQ;
		const char *caq = obs_data_get_string(data, settings::PA_CAQ);
		caqStrength = pa_caq::getValue(caq);
	}

	setProperty(AMF_PA_PAQ_MODE, paqMode);
	setProperty(AMF_PA_CAQ_STRENGTH, caqStrength);
	setProperty(AMF_PA_TAQ_MODE, taqMode);

	return true;
}

void Encoder::applyOpts(const char *s)
{
	if (!(*s))
		return;
	obs_options opts = obs_parse_options(s);
	obs_option *opt = opts.options;
	wstringstream ss;
	for (size_t i = 0; i < opts.count; i++) {
		if (ss.tellp())
			ss.str(L"");
		ss << opt->name;
		const wstring name = ss.str();
		setProperty(name.c_str(), opt->value);
		opt++;
	}
	obs_free_options(opts);
}

inline void Encoder::updateROI(AMFSurfacePtr &surface)
{
	if (!obs_encoder_has_roi(encoder)) {
		if (roi)
			// We had an ROI at one point; clear out everything
			roi.reset();
		return;
	}

	if (!roi) {
		bool preAnalysisEnabled = false;
		AMF_GET(PRE_ANALYSIS_ENABLE, &preAnalysisEnabled);

		if (preAnalysisEnabled) {
			// Temporarily disable ROI (it cannot work with PA)
			capabilities.roi = false;
			warn("Region-of-interest (ROI) is not available while Pre-Analysis is active");
			return;
		}

		uint32_t mbSize = codec == CodecType::AVC ? 16 : 64;
		roi.reset(new ROI{
			mbSize,
			(width + mbSize - 1) / mbSize,
			(height + mbSize - 1) / mbSize,
			AMF_PROPERTY(ROI_DATA),
		});
	}

	ROI &roi = *this->roi;

	AMFSurfacePtr roiSurface;
	AMF_CHECK(amfContext1->AllocSurfaceEx(AMF_MEMORY_HOST, AMF_SURFACE_GRAY32, roi.width, roi.height,
					      AMF_SURFACE_USAGE_DEFAULT | AMF_SURFACE_USAGE_LINEAR,
					      AMF_MEMORY_CPU_READ | AMF_MEMORY_CPU_WRITE, &roiSurface),
		  "AllocSurfaceEx failed");

	AMFPlane &plane = *roiSurface->GetPlaneAt(0);
	uint32_t increment = obs_encoder_get_roi_increment(encoder);

	if (!roi.buffer) {
		// Need to consult the surface for the h pitch value
		amf_int32 pitch = plane.GetHPitch();

		roi.bufferSize = pitch * roi.height;
		roi.buffer = new uint32_t[roi.bufferSize];
		roi.pitch = pitch / 4;

		updateROIData(increment);
	} else if (increment != roi.increment) {
		updateROIData(increment);
	}

	memcpy((void *)plane.GetNative(), roi.buffer, roi.bufferSize);
	surface->SetProperty(roi.propertyName, roiSurface);
}

inline void Encoder::updateROIData(uint32_t &increment)
{
	ROI &roi = *this->roi;
	roi.increment = increment;
	memset(roi.buffer, 0, roi.bufferSize);
	obs_encoder_enum_roi(encoder, enumROICallback, &roi);
}

void Encoder::receivePacket(AMFDataPtr &data, encoder_packet *packetPtr)
{
	// Need to hold on to this reference,
	// or the underlying data will be collected too soon
	packetData = AMFBufferPtr(data);

	encoder_packet &packet = *packetPtr;
	data->GetProperty(L"PTS", &packet.pts);

	uint64_t type;
	AMF_CHECK(data->GetProperty(outputDataTypeProperty, &type),
		  "Failed to GetProperty(): encoder output data type");

	switch (codec) {
	case CodecType::AVC:
	case CodecType::HEVC:
		switch (type) {
		case AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_IDR:
			packet.priority = OBS_NAL_PRIORITY_HIGHEST;
			break;
		case AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_I:
		case AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_P:
			packet.priority = OBS_NAL_PRIORITY_HIGH;
			break;
		default:
			packet.priority = OBS_NAL_PRIORITY_LOW;
			break;
		}
		break;
	case CodecType::AV1:
		switch (type) {
		case AMF_VIDEO_ENCODER_AV1_OUTPUT_FRAME_TYPE_KEY:
			packet.priority = OBS_NAL_PRIORITY_HIGHEST;
			break;
		case AMF_VIDEO_ENCODER_AV1_OUTPUT_FRAME_TYPE_INTRA_ONLY:
			packet.priority = OBS_NAL_PRIORITY_HIGH;
			break;
		case AMF_VIDEO_ENCODER_AV1_OUTPUT_FRAME_TYPE_SWITCH:
		case AMF_VIDEO_ENCODER_AV1_OUTPUT_FRAME_TYPE_SHOW_EXISTING:
			packet.priority = OBS_NAL_PRIORITY_DISPOSABLE;
			break;
		default:
			packet.priority = OBS_NAL_PRIORITY_LOW;
			break;
		}
		break;
	}

	packet.data = (uint8_t *)packetData->GetNative();
	packet.size = packetData->GetSize();
	packet.type = OBS_ENCODER_VIDEO;
	packet.dts = timestampToOBS(data->GetPts());
	packet.keyframe = type == AMF_VIDEO_ENCODER_OUTPUT_DATA_TYPE_IDR;

	if (dtsOffset)
		packet.dts -= dtsOffset;
}
