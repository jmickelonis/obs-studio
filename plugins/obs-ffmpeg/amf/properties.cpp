
#include "properties.hpp"

#include <codecvt>
#include <list>
#include <locale>
#include <mutex>
#include <unordered_map>

using std::mutex;
using std::scoped_lock;
using std::unordered_map;
using std::wstring;
using std::wstring_convert;
using std::codecvt_utf8;

bool PropertyNameComparator::operator()(const wchar_t *a, const wchar_t *b) const
{
	return wcscmp(a, b) < 0;
}

static CodecProperties *createAVCProperties()
{
	return new CodecProperties{
		CodecType::AVC,
		{
			"Static",
			"Color Conversion",
			"Rate Control",
			"Picture Control",
			"Motion Estimation",
			"SVC",
			"Miscellaneous",
		},
		{
#define _ITEM(NAME, TYPE) { AVC_PROPERTY(NAME), PropertyType::TYPE }
			{"Static",
			 {
				 _ITEM(USAGE, AVC_USAGE),
				 _ITEM(INSTANCE_INDEX, INT),
				 _ITEM(PROFILE, AVC_PROFILE),
				 _ITEM(PROFILE_LEVEL, AVC_H264_LEVEL),
				 _ITEM(MAX_LTR_FRAMES, INT),
				 _ITEM(LTR_MODE, AVC_LTR_MODE),
				 _ITEM(LOWLATENCY_MODE, BOOL),
				 _ITEM(FRAMESIZE, SIZE),
				 _ITEM(ASPECT_RATIO, RATIO),
				 _ITEM(MAX_NUM_REFRAMES, INT),
				 _ITEM(MAX_CONSECUTIVE_BPICTURES, INT),
				 _ITEM(ADAPTIVE_MINIGOP, BOOL),
				 _ITEM(PRE_ANALYSIS_ENABLE, BOOL),
				 _ITEM(COLOR_BIT_DEPTH, COLOR_BIT_DEPTH),
				 _ITEM(MAX_NUM_TEMPORAL_LAYERS, INT),
				 _ITEM(ENABLE_SMART_ACCESS_VIDEO, BOOL),
			 }},
			{"Color Conversion",
			 {
				 _ITEM(INPUT_COLOR_PROFILE, COLOR_PROFILE),
				 _ITEM(INPUT_TRANSFER_CHARACTERISTIC, COLOR_TRANSFER_CHARACTERISTIC),
				 _ITEM(INPUT_COLOR_PRIMARIES, COLOR_PRIMARIES),
				 _ITEM(OUTPUT_COLOR_PROFILE, COLOR_PROFILE),
				 _ITEM(OUTPUT_TRANSFER_CHARACTERISTIC, COLOR_TRANSFER_CHARACTERISTIC),
				 _ITEM(OUTPUT_COLOR_PRIMARIES, COLOR_PRIMARIES),
			 }},
			{"Rate Control",
			 {
				 _ITEM(TARGET_BITRATE, INT),
				 _ITEM(PEAK_BITRATE, INT),
				 _ITEM(RATE_CONTROL_METHOD, AVC_RATE_CONTROL_METHOD),
				 _ITEM(RATE_CONTROL_SKIP_FRAME_ENABLE, BOOL),
				 _ITEM(MIN_QP, INT),
				 _ITEM(MAX_QP, INT),
				 _ITEM(QP_I, INT),
				 _ITEM(QP_P, INT),
				 _ITEM(QP_B, INT),
				 _ITEM(QVBR_QUALITY_LEVEL, INT),
				 _ITEM(FRAMERATE, RATE),
				 _ITEM(VBV_BUFFER_SIZE, INT),
				 _ITEM(INITIAL_VBV_BUFFER_FULLNESS, INT),
				 _ITEM(ENFORCE_HRD, BOOL),
				 _ITEM(MAX_AU_SIZE, INT),
				 _ITEM(B_PIC_DELTA_QP, INT),
				 _ITEM(REF_B_PIC_DELTA_QP, INT),
				 _ITEM(PREENCODE_ENABLE, AVC_PREENCODE_MODE),
				 _ITEM(FILLER_DATA_ENABLE, BOOL),
				 _ITEM(ENABLE_VBAQ, BOOL),
			 }},
			{"Picture Control",
			 {
				 _ITEM(HEADER_INSERTION_SPACING, INT),
				 _ITEM(IDR_PERIOD, INT),
				 _ITEM(INTRA_PERIOD, INT),
				 _ITEM(DE_BLOCKING_FILTER, BOOL),
				 _ITEM(INTRA_REFRESH_NUM_MBS_PER_SLOT, INT),
				 _ITEM(SLICES_PER_FRAME, INT),
				 _ITEM(B_PIC_PATTERN, INT),
				 _ITEM(B_REFERENCE_ENABLE, BOOL),
				 _ITEM(CABAC_ENABLE, AVC_CODING),
				 _ITEM(HIGH_MOTION_QUALITY_BOOST_ENABLE, BOOL),
			 }},
			{"Motion Estimation",
			 {
				 _ITEM(MOTION_HALF_PIXEL, BOOL),
				 _ITEM(MOTION_QUARTERPIXEL, BOOL),
			 }},
			{"SVC",
			 {
				 _ITEM(NUM_TEMPORAL_ENHANCMENT_LAYERS, INT),
			 }},
			{"Miscellaneous",
			 {
				 _ITEM(SCANTYPE, AVC_SCANTYPE),
				 _ITEM(QUALITY_PRESET, AVC_QUALITY_PRESET),
				 _ITEM(FULL_RANGE_COLOR, BOOL),
				 _ITEM(PICTURE_TRANSFER_MODE, AVC_PICTURE_TRANSFER_MODE),
				 _ITEM(QUERY_TIMEOUT, INT),
				 _ITEM(INPUT_QUEUE_SIZE, INT),
				 _ITEM(OUTPUT_MODE, AVC_OUTPUT_MODE),
			 }},
#undef _ITEM
		},
		{
#define _ITEM(NAME, TYPE) { AVC_CAP(NAME), PropertyType::TYPE }
			_ITEM(MAX_BITRATE, INT),
			_ITEM(NUM_OF_STREAMS, INT),
			_ITEM(MAX_PROFILE, AVC_PROFILE),
			_ITEM(MAX_LEVEL, AVC_H264_LEVEL),
			_ITEM(BFRAMES, BOOL),
			_ITEM(MIN_REFERENCE_FRAMES, INT),
			_ITEM(MAX_REFERENCE_FRAMES, INT),
			_ITEM(MAX_TEMPORAL_LAYERS, INT),
			_ITEM(FIXED_SLICE_MODE, BOOL),
			_ITEM(NUM_OF_HW_INSTANCES, INT),
			_ITEM(COLOR_CONVERSION, ACCEL),
			_ITEM(PRE_ANALYSIS, BOOL),
			_ITEM(ROI, BOOL),
			_ITEM(MAX_THROUGHPUT, INT),
			_ITEM(REQUESTED_THROUGHPUT, INT),
			_ITEM(QUERY_TIMEOUT_SUPPORT, BOOL),
			_ITEM(SUPPORT_SLICE_OUTPUT, BOOL),
			_ITEM(SUPPORT_SMART_ACCESS_VIDEO, BOOL),
#undef _ITEM
		}};
}

static CodecProperties *createHEVCProperties()
{
	return new CodecProperties{
		CodecType::HEVC,
		{
			"Static",
			"Rate Control",
			"Picture Control",
			"Motion Estimation",
			"Color Conversion",
			"SVC",
			"Miscellaneous",
		},
		{
#define _ITEM(NAME, TYPE) { HEVC_PROPERTY(NAME), PropertyType::TYPE }
			{"Static",
			 {
				 _ITEM(USAGE, HEVC_USAGE),
				 _ITEM(INSTANCE_INDEX, INT),
				 _ITEM(PROFILE, HEVC_PROFILE),
				 _ITEM(TIER, HEVC_TIER),
				 _ITEM(PROFILE_LEVEL, HEVC_LEVEL),
				 _ITEM(MAX_LTR_FRAMES, INT),
				 _ITEM(LTR_MODE, HEVC_LTR_MODE),
				 _ITEM(MAX_NUM_REFRAMES, INT),
				 _ITEM(LOWLATENCY_MODE, BOOL),
				 _ITEM(FRAMESIZE, SIZE),
				 _ITEM(ASPECT_RATIO, RATIO),
				 _ITEM(PRE_ANALYSIS_ENABLE, BOOL),
				 _ITEM(MAX_NUM_TEMPORAL_LAYERS, INT),
				 _ITEM(NOMINAL_RANGE, HEVC_NOMINAL_RANGE),
				 _ITEM(ENABLE_SMART_ACCESS_VIDEO, BOOL),
			 }},
			{"Rate Control",
			 {
				 _ITEM(TARGET_BITRATE, INT),
				 _ITEM(PEAK_BITRATE, INT),
				 _ITEM(RATE_CONTROL_METHOD, HEVC_RATE_CONTROL_METHOD),
				 _ITEM(QVBR_QUALITY_LEVEL, INT),
				 _ITEM(RATE_CONTROL_SKIP_FRAME_ENABLE, BOOL),
				 _ITEM(MIN_QP_I, INT),
				 _ITEM(MAX_QP_I, INT),
				 _ITEM(MIN_QP_P, INT),
				 _ITEM(MAX_QP_P, INT),
				 _ITEM(QP_I, INT),
				 _ITEM(QP_P, INT),
				 _ITEM(FRAMERATE, RATE),
				 _ITEM(VBV_BUFFER_SIZE, INT),
				 _ITEM(INITIAL_VBV_BUFFER_FULLNESS, INT),
				 _ITEM(ENFORCE_HRD, BOOL),
				 _ITEM(PREENCODE_ENABLE, BOOL),
				 _ITEM(ENABLE_VBAQ, BOOL),
				 _ITEM(FILLER_DATA_ENABLE, BOOL),
				 _ITEM(HIGH_MOTION_QUALITY_BOOST_ENABLE, BOOL),
			 }},
			{"Picture Control",
			 {
				 _ITEM(MAX_AU_SIZE, INT),
				 _ITEM(HEADER_INSERTION_MODE, HEVC_HEADER_INSERTION_MODE),
				 _ITEM(GOP_SIZE, INT),
				 _ITEM(NUM_GOPS_PER_IDR, INT),
				 _ITEM(DE_BLOCKING_FILTER_DISABLE, BOOL),
				 _ITEM(SLICES_PER_FRAME, INT),
				 _ITEM(INTRA_REFRESH_NUM_CTBS_PER_SLOT, INT),
			 }},
			{"Motion Estimation",
			 {
				 _ITEM(MOTION_HALF_PIXEL, BOOL),
				 _ITEM(MOTION_QUARTERPIXEL, BOOL),
			 }},
			{"Color Conversion",
			 {
				 _ITEM(COLOR_BIT_DEPTH, COLOR_BIT_DEPTH),
				 _ITEM(INPUT_COLOR_PROFILE, COLOR_PROFILE),
				 _ITEM(INPUT_TRANSFER_CHARACTERISTIC, COLOR_TRANSFER_CHARACTERISTIC),
				 _ITEM(INPUT_COLOR_PRIMARIES, COLOR_PRIMARIES),
				 _ITEM(OUTPUT_COLOR_PROFILE, COLOR_PROFILE),
				 _ITEM(OUTPUT_TRANSFER_CHARACTERISTIC, COLOR_TRANSFER_CHARACTERISTIC),
				 _ITEM(OUTPUT_COLOR_PRIMARIES, COLOR_PRIMARIES),
			 }},
			{"SVC",
			 {
				 _ITEM(NUM_TEMPORAL_LAYERS, INT),
			 }},
			{"Miscellaneous",
			 {
				 _ITEM(QUALITY_PRESET, HEVC_QUALITY_PRESET),
				 _ITEM(PICTURE_TRANSFER_MODE, HEVC_PICTURE_TRANSFER_MODE),
				 _ITEM(QUERY_TIMEOUT, INT),
				 _ITEM(INPUT_QUEUE_SIZE, INT),
				 _ITEM(OUTPUT_MODE, HEVC_OUTPUT_MODE),
				 _ITEM(MULTI_HW_INSTANCE_ENCODE, BOOL),
			 }},
#undef _ITEM
		},
		{
#define _ITEM(NAME, TYPE) { HEVC_CAP(NAME), PropertyType::TYPE }
			_ITEM(MAX_BITRATE, INT),
			_ITEM(NUM_OF_STREAMS, INT),
			_ITEM(MAX_PROFILE, HEVC_PROFILE),
			_ITEM(MAX_TIER, HEVC_TIER),
			_ITEM(MAX_LEVEL, HEVC_LEVEL),
			_ITEM(MIN_REFERENCE_FRAMES, INT),
			_ITEM(MAX_REFERENCE_FRAMES, INT),
			_ITEM(MAX_TEMPORAL_LAYERS, INT),
			_ITEM(NUM_OF_HW_INSTANCES, INT),
			_ITEM(COLOR_CONVERSION, ACCEL),
			_ITEM(PRE_ANALYSIS, BOOL),
			_ITEM(ROI, BOOL),
			_ITEM(MAX_THROUGHPUT, INT),
			_ITEM(REQUESTED_THROUGHPUT, INT),
			_ITEM(QUERY_TIMEOUT_SUPPORT, BOOL),
			_ITEM(SUPPORT_SLICE_OUTPUT, BOOL),
			_ITEM(SUPPORT_SMART_ACCESS_VIDEO, BOOL),
#undef _ITEM
		}};
}

static CodecProperties *createAV1Properties()
{
	return new CodecProperties{
		CodecType::AV1,
		{
			"Static",
			"Rate Control",
			"Picture Control",
			"Configuration",
			"Color Conversion",
			"SVC",
			"Miscellaneous",
		},
		{
#define _ITEM(NAME, TYPE) { AV1_PROPERTY(NAME), PropertyType::TYPE }
			{"Static",
			 {
				 _ITEM(USAGE, AV1_USAGE),
				 _ITEM(PROFILE, AV1_PROFILE),
				 _ITEM(LEVEL, AV1_LEVEL),
				 _ITEM(MAX_LTR_FRAMES, INT),
				 _ITEM(TILES_PER_FRAME, INT),
				 _ITEM(LTR_MODE, AV1_LTR_MODE),
				 _ITEM(MAX_NUM_REFRAMES, INT),
				 _ITEM(MAX_CONSECUTIVE_BPICTURES, INT),
				 _ITEM(ADAPTIVE_MINIGOP, BOOL),
				 _ITEM(ENCODING_LATENCY_MODE, AV1_ENCODING_LATENCY_MODE),
				 _ITEM(FRAMESIZE, SIZE),
				 _ITEM(ALIGNMENT_MODE, AV1_ALIGNMENT_MODE),
				 _ITEM(PRE_ANALYSIS_ENABLE, BOOL),
				 _ITEM(MAX_NUM_TEMPORAL_LAYERS, INT),
				 _ITEM(NOMINAL_RANGE, BOOL),
				 _ITEM(ENABLE_SMART_ACCESS_VIDEO, BOOL),
			 }},
			{"Rate Control",
			 {
				 _ITEM(TARGET_BITRATE, INT),
				 _ITEM(PEAK_BITRATE, INT),
				 _ITEM(RATE_CONTROL_METHOD, AV1_RATE_CONTROL_METHOD),
				 _ITEM(QVBR_QUALITY_LEVEL, INT),
				 _ITEM(RATE_CONTROL_SKIP_FRAME, BOOL),
				 _ITEM(MIN_Q_INDEX_INTRA, INT),
				 _ITEM(MAX_Q_INDEX_INTRA, INT),
				 _ITEM(MIN_Q_INDEX_INTER, INT),
				 _ITEM(MAX_Q_INDEX_INTER, INT),
				 _ITEM(MIN_Q_INDEX_INTER_B, INT),
				 _ITEM(MAX_Q_INDEX_INTER_B, INT),
				 _ITEM(Q_INDEX_INTRA, INT),
				 _ITEM(Q_INDEX_INTER, INT),
				 _ITEM(Q_INDEX_INTER_B, INT),
				 _ITEM(FRAMERATE, RATE),
				 _ITEM(VBV_BUFFER_SIZE, INT),
				 _ITEM(INITIAL_VBV_BUFFER_FULLNESS, INT),
				 _ITEM(ENFORCE_HRD, BOOL),
				 _ITEM(RATE_CONTROL_PREENCODE, BOOL),
				 _ITEM(AQ_MODE, AV1_AQ_MODE),
				 _ITEM(FILLER_DATA, BOOL),
				 _ITEM(HIGH_MOTION_QUALITY_BOOST, BOOL),
			 }},
			{"Picture Control",
			 {
				 _ITEM(MAX_COMPRESSED_FRAME_SIZE, INT),
				 _ITEM(HEADER_INSERTION_MODE, AV1_HEADER_INSERTION_MODE),
				 _ITEM(SWITCH_FRAME_INSERTION_MODE, AV1_SWITCH_FRAME_INSERTION_MODE),
				 _ITEM(SWITCH_FRAME_INTERVAL, INT),
				 _ITEM(GOP_SIZE, INT),
				 _ITEM(CDEF_MODE, AV1_CDEF_MODE),
				 _ITEM(INTRA_REFRESH_MODE, AV1_INTRA_REFRESH_MODE),
				 _ITEM(INTRAREFRESH_STRIPES, INT),
				 _ITEM(B_PIC_PATTERN, INT),
			 }},
			{"Configuration",
			 {
				 _ITEM(SCREEN_CONTENT_TOOLS, BOOL),
				 _ITEM(PALETTE_MODE, BOOL),
				 _ITEM(FORCE_INTEGER_MV, BOOL),
				 _ITEM(ORDER_HINT, BOOL),
				 _ITEM(FRAME_ID, BOOL),
				 _ITEM(TILE_GROUP_OBU, BOOL),
				 _ITEM(ERROR_RESILIENT_MODE, BOOL),
				 _ITEM(COLOR_BIT_DEPTH, COLOR_BIT_DEPTH),
				 _ITEM(CDF_UPDATE, BOOL),
				 _ITEM(CDF_FRAME_END_UPDATE_MODE, AV1_CDF_FRAME_END_UPDATE_MODE),
			 }},
			{"Color Conversion",
			 {
				 _ITEM(INPUT_COLOR_PROFILE, COLOR_PROFILE),
				 _ITEM(INPUT_TRANSFER_CHARACTERISTIC, COLOR_TRANSFER_CHARACTERISTIC),
				 _ITEM(INPUT_COLOR_PRIMARIES, COLOR_PRIMARIES),
				 _ITEM(OUTPUT_COLOR_PROFILE, COLOR_PROFILE),
				 _ITEM(OUTPUT_TRANSFER_CHARACTERISTIC, COLOR_TRANSFER_CHARACTERISTIC),
				 _ITEM(OUTPUT_COLOR_PRIMARIES, COLOR_PRIMARIES),
			 }},
			{"SVC",
			 {
				 _ITEM(NUM_TEMPORAL_LAYERS, INT),
			 }},
			{"Miscellaneous",
			 {
				 _ITEM(QUALITY_PRESET, HEVC_QUALITY_PRESET),
				 _ITEM(QUERY_TIMEOUT, INT),
				 _ITEM(INPUT_QUEUE_SIZE, INT),
				 _ITEM(OUTPUT_MODE, AV1_OUTPUT_MODE),
				 _ITEM(MULTI_HW_INSTANCE_ENCODE, BOOL),
			 }},
#undef _ITEM
		},
		{
#define _ITEM(NAME, TYPE) { AV1_CAP(NAME), PropertyType::TYPE }
			_ITEM(NUM_OF_HW_INSTANCES, INT),
			_ITEM(MAX_THROUGHPUT, INT),
			_ITEM(REQUESTED_THROUGHPUT, INT),
			_ITEM(COLOR_CONVERSION, ACCEL),
			_ITEM(PRE_ANALYSIS, BOOL),
			_ITEM(MAX_BITRATE, INT),
			_ITEM(MAX_PROFILE, AV1_PROFILE),
			_ITEM(MAX_LEVEL, AV1_LEVEL),
			_ITEM(MAX_NUM_TEMPORAL_LAYERS, INT),
			_ITEM(MAX_NUM_LTR_FRAMES, INT),
			_ITEM(SUPPORT_TILE_OUTPUT, BOOL),
			_ITEM(WIDTH_ALIGNMENT_FACTOR, INT),
			_ITEM(HEIGHT_ALIGNMENT_FACTOR, INT),
			_ITEM(BFRAMES, BOOL),
			_ITEM(SUPPORT_SMART_ACCESS_VIDEO, BOOL),
#undef _ITEM
		}};
}

static mutex propertiesMutex;
static unordered_map<CodecType, CodecProperties *> codecProperties;

const CodecProperties &getCodecProperties(CodecType codec)
{
	scoped_lock lock(propertiesMutex);

	auto it = codecProperties.find(codec);
	if (it != codecProperties.end())
		return *it->second;

	CodecProperties *(*create)();
	switch (codec) {
	case CodecType::AVC:
	default:
		create = createAVCProperties;
		break;
	case CodecType::HEVC:
		create = createHEVCProperties;
		break;
	case CodecType::AV1:
		create = createAV1Properties;
		break;
	}

	CodecProperties *properties = create();
	codecProperties[codec] = properties;
	return *properties;
}

static PropertyTypes *preAnalysisProperties;

const PropertyTypes &getPreAnalysisProperties()
{
	scoped_lock lock(propertiesMutex);
	if (preAnalysisProperties)
		return *preAnalysisProperties;

	preAnalysisProperties = new PropertyTypes{
#define _ITEM(NAME, TYPE) { AMF_PA_ ## NAME, PropertyType::TYPE }
		_ITEM(ENGINE_TYPE, MEMORY_TYPE),
		_ITEM(ACTIVITY_TYPE, PA_ACTIVITY_TYPE),
		_ITEM(SCENE_CHANGE_DETECTION_ENABLE, BOOL),
		_ITEM(SCENE_CHANGE_DETECTION_SENSITIVITY, PA_SCENE_CHANGE_DETECTION_SENSITIVITY),
		_ITEM(STATIC_SCENE_DETECTION_ENABLE, BOOL),
		_ITEM(STATIC_SCENE_DETECTION_SENSITIVITY, PA_STATIC_SCENE_DETECTION_SENSITIVITY),
		_ITEM(INITIAL_QP_AFTER_SCENE_CHANGE, UINT),
		_ITEM(MAX_QP_BEFORE_FORCE_SKIP, UINT),
		_ITEM(CAQ_STRENGTH, PA_CAQ_STRENGTH),
		_ITEM(FRAME_SAD_ENABLE, BOOL),
		_ITEM(LTR_ENABLE, BOOL),
		_ITEM(LOOKAHEAD_BUFFER_DEPTH, UINT),
		_ITEM(PAQ_MODE, PA_PAQ_MODE),
		_ITEM(TAQ_MODE, PA_TAQ_MODE),
		_ITEM(HIGH_MOTION_QUALITY_BOOST_MODE, PA_HIGH_MOTION_QUALITY_BOOST_MODE),
#undef _ITEM
	};
	return *preAnalysisProperties;
}

static string nameToString(const wchar_t *name)
{
	wstring_convert<codecvt_utf8<wchar_t>> converter;
	return converter.to_bytes(name);
}

template<typename T> static T getProperty(const AMFPropertyStorage *storage, const wchar_t *name)
{
	T value;
	AMF_CHECK(storage->GetProperty(name, &value), "GetProperty failed");
	return value;
}

static string enumValueToString(const AMFPropertyStorage *storage, const wchar_t *name, const char *prefix,
				const map<amf_int64, string> strings)
{
	amf_int64 value = getProperty<amf_int64>(storage, name);
	stringstream ss;
	ss << value << " (" << prefix << "_";
	try {
		ss << strings.at(value);
	} catch (const std::out_of_range) {
		ss << "???";
	}
	ss << ")";
	return ss.str();
}

static string valueToString(const AMFPropertyStorage *storage, const wchar_t *name, const PropertyType type)
{
	switch (type) {
#define _ENUM(TYPE, PREFIX, ...) \
	case PropertyType::TYPE: \
		return enumValueToString(storage, name, PREFIX, __VA_ARGS__);

#define _LOW_MEDIUM_HIGH_ITEMS \
	{ \
		_ITEM(LOW), \
		_ITEM(MEDIUM), \
		_ITEM(HIGH), \
	}

#define _LTR_MODE_ITEMS \
	{ \
		_ITEM(RESET_UNUSED), \
		_ITEM(KEEP_UNUSED), \
	}

#define _OFF_ON_ITEMS \
	{ \
		_ITEM(OFF), \
		_ITEM(ON), \
	}

#define _QUALITY_ITEMS \
	{ \
		_ITEM(HIGH_QUALITY), \
		_ITEM(QUALITY), \
		_ITEM(BALANCED), \
		_ITEM(SPEED), \
	}

#define _RATE_CONTROL_ITEMS \
	{ \
		_ITEM(UNKNOWN), \
		_ITEM(CONSTANT_QP), \
		_ITEM(CBR), \
		_ITEM(PEAK_CONSTRAINED_VBR), \
		_ITEM(LATENCY_CONSTRAINED_VBR), \
		_ITEM(QUALITY_VBR), \
		_ITEM(HIGH_QUALITY_VBR), \
		_ITEM(HIGH_QUALITY_CBR), \
	}

#define _USAGE_ITEMS \
	{ \
		_ITEM(TRANSCODING), \
		_ITEM(ULTRA_LOW_LATENCY), \
		_ITEM(LOW_LATENCY), \
		_ITEM(WEBCAM), \
		_ITEM(HIGH_QUALITY), \
		_ITEM(LOW_LATENCY_HIGH_QUALITY), \
	}

// General
#define _ITEM(NAME) { AMF_ACCEL_ ## NAME, #NAME }
		_ENUM(ACCEL, "AMF_ACCEL",
		      {
			      _ITEM(NOT_SUPPORTED),
			      _ITEM(HARDWARE),
			      _ITEM(GPU),
		      })
#undef _ITEM
#define _ITEM(NAME) { AMF_COLOR_BIT_DEPTH_ ## NAME, #NAME }
		_ENUM(COLOR_BIT_DEPTH, "AMF_COLOR_BIT_DEPTH",
		      {
			      _ITEM(UNDEFINED),
			      _ITEM(8),
			      _ITEM(10),
		      })
#undef _ITEM
#define _ITEM(NAME) { AMF_COLOR_PRIMARIES_ ## NAME, #NAME }
		_ENUM(COLOR_PRIMARIES, "AMF_COLOR_PRIMARIES",
		      {
			      _ITEM(UNDEFINED),
			      _ITEM(BT709),
			      _ITEM(UNSPECIFIED),
			      _ITEM(RESERVED),
			      _ITEM(BT470M),
			      _ITEM(BT470BG),
			      _ITEM(SMPTE170M),
			      _ITEM(SMPTE240M),
			      _ITEM(FILM),
			      _ITEM(BT2020),
			      _ITEM(SMPTE428),
			      _ITEM(SMPTE431),
			      _ITEM(SMPTE432),
			      _ITEM(JEDEC_P22),
			      _ITEM(CCCS),
		      })
#undef _ITEM
#define _ITEM(NAME) { AMF_VIDEO_CONVERTER_COLOR_PROFILE_ ## NAME, #NAME }
		_ENUM(COLOR_PROFILE, "AMF_VIDEO_CONVERTER_COLOR_PROFILE",
		      {
			      _ITEM(UNKNOWN),
			      _ITEM(601),
			      _ITEM(709),
			      _ITEM(2020),
			      _ITEM(FULL_601),
			      _ITEM(FULL_709),
			      _ITEM(FULL_2020),
		      })
#undef _ITEM
#define _ITEM(NAME) { AMF_COLOR_TRANSFER_CHARACTERISTIC_ ## NAME, #NAME }
		_ENUM(COLOR_TRANSFER_CHARACTERISTIC, "AMF_COLOR_TRANSFER_CHARACTERISTIC",
		      {
			      _ITEM(UNDEFINED),  _ITEM(BT709),        _ITEM(UNSPECIFIED),  _ITEM(RESERVED),
			      _ITEM(GAMMA22),    _ITEM(GAMMA28),      _ITEM(SMPTE170M),    _ITEM(SMPTE240M),
			      _ITEM(LINEAR),     _ITEM(LOG),          _ITEM(LOG_SQRT),     _ITEM(IEC61966_2_4),
			      _ITEM(BT1361_ECG), _ITEM(IEC61966_2_1), _ITEM(BT2020_10),    _ITEM(BT2020_12),
			      _ITEM(SMPTE2084),  _ITEM(SMPTE428),     _ITEM(ARIB_STD_B67),
		      })
#undef _ITEM
#define _ITEM(NAME) { AMF_MEMORY_ ## NAME, #NAME }
		_ENUM(MEMORY_TYPE, "AMF_MEMORY",
		      {
			      _ITEM(UNKNOWN),
			      _ITEM(HOST),
			      _ITEM(DX9),
			      _ITEM(DX11),
			      _ITEM(OPENCL),
			      _ITEM(OPENGL),
			      _ITEM(XV),
			      _ITEM(GRALLOC),
			      _ITEM(COMPUTE_FOR_DX9),
			      _ITEM(COMPUTE_FOR_DX11),
			      _ITEM(VULKAN),
			      _ITEM(DX12),
		      })
#undef _ITEM

// AVC
#define NAME(NAME) "AMF_VIDEO_ENCODER_" #NAME
#define VALUE AVC_PROPERTY
#define _ITEM(NAME) { VALUE(USAGE_ ## NAME), #NAME }
		_ENUM(AVC_USAGE, NAME(USAGE), _USAGE_ITEMS)
#undef _ITEM
#define _ITEM(NAME) { VALUE(PROFILE_ ## NAME), #NAME }
		_ENUM(AVC_PROFILE, NAME(PROFILE),
		      {
			      _ITEM(UNKNOWN),
			      _ITEM(BASELINE),
			      _ITEM(MAIN),
			      _ITEM(HIGH),
			      _ITEM(CONSTRAINED_BASELINE),
			      _ITEM(CONSTRAINED_HIGH),
		      })
#undef _ITEM
#define _ITEM(NAME) { AMF_H264_LEVEL__ ## NAME, #NAME }
		_ENUM(AVC_H264_LEVEL, "AMF_H264_LEVEL_",
		      {
			      _ITEM(1),   _ITEM(1_1), _ITEM(1_2), _ITEM(1_3), _ITEM(2),   _ITEM(2_1), _ITEM(2_2),
			      _ITEM(3),   _ITEM(3_1), _ITEM(3_2), _ITEM(4),   _ITEM(4_1), _ITEM(4_2), _ITEM(5),
			      _ITEM(5_1), _ITEM(5_2), _ITEM(6),   _ITEM(6_1), _ITEM(6_2),
		      })
#undef _ITEM
#define _ITEM(NAME) { VALUE(SCANTYPE_ ## NAME), #NAME }
		_ENUM(AVC_SCANTYPE, NAME(SCANTYPE),
		      {
			      _ITEM(PROGRESSIVE),
			      _ITEM(INTERLACED),
		      })
#undef _ITEM
#define _ITEM(NAME) { VALUE(RATE_CONTROL_METHOD_ ## NAME), #NAME }
		_ENUM(AVC_RATE_CONTROL_METHOD, NAME(RATE_CONTROL_METHOD), _RATE_CONTROL_ITEMS)
#undef _ITEM
#define _ITEM(NAME) { VALUE(QUALITY_PRESET_ ## NAME), #NAME }
		_ENUM(AVC_QUALITY_PRESET, NAME(QUALITY_PRESET), _QUALITY_ITEMS)
#undef _ITEM
#define _ITEM(NAME) { VALUE(PREENCODE_ ## NAME), #NAME }
		_ENUM(AVC_PREENCODE_MODE, NAME(PREENCODE),
		      {
			      _ITEM(DISABLED),
			      _ITEM(ENABLED),
		      })
#undef _ITEM
#define _ITEM(NAME) { VALUE(NAME), #NAME }
		_ENUM(AVC_CODING, "AMF_VIDEO_ENCODER",
		      {
			      _ITEM(UNDEFINED),
			      _ITEM(CABAC),
			      _ITEM(CALV),
		      })
#undef _ITEM
#define _ITEM(NAME) { VALUE(PICTURE_TRANSFER_MODE_ ## NAME), #NAME }
		_ENUM(AVC_PICTURE_TRANSFER_MODE, NAME(PICTURE_TRANSFER_MODE), _OFF_ON_ITEMS)
#undef _ITEM
#define _ITEM(NAME) { VALUE(LTR_MODE_ ## NAME), #NAME }
		_ENUM(AVC_LTR_MODE, NAME(LTR_MODE), _LTR_MODE_ITEMS)
#undef _ITEM
#define _ITEM(NAME) { VALUE(OUTPUT_MODE_ ## NAME), #NAME }
		_ENUM(AVC_OUTPUT_MODE, NAME(OUTPUT_MODE),
		      {
			      _ITEM(FRAME),
			      _ITEM(SLICE),
		      })
#undef _ITEM
#undef NAME
#undef VALUE

// AV1
#define NAME(NAME) "AMF_VIDEO_ENCODER_AV1_" #NAME
#define VALUE AV1_PROPERTY
#define _ITEM(NAME) { VALUE(ENCODING_LATENCY_MODE_ ## NAME), #NAME }
		_ENUM(AV1_ENCODING_LATENCY_MODE, NAME(ENCODING_LATENCY_MODE),
		      {
			      _ITEM(NONE),
			      _ITEM(POWER_SAVING_REAL_TIME),
			      _ITEM(REAL_TIME),
			      _ITEM(LOWEST_LATENCY),
		      })
#undef _ITEM
#define _ITEM(NAME) { VALUE(USAGE_ ## NAME), #NAME }
		_ENUM(AV1_USAGE, NAME(USAGE), _USAGE_ITEMS)
#undef _ITEM
#define _ITEM(NAME) { VALUE(PROFILE_ ## NAME), #NAME }
		_ENUM(AV1_PROFILE, NAME(PROFILE),
		      {
			      _ITEM(MAIN),
		      })
#undef _ITEM
#define _ITEM(NAME) { VALUE(LEVEL_ ## NAME), #NAME }
		_ENUM(AV1_LEVEL, NAME(LEVEL),
		      {
			      _ITEM(2_0), _ITEM(2_1), _ITEM(2_2), _ITEM(2_3), _ITEM(3_0), _ITEM(3_1),
			      _ITEM(3_2), _ITEM(3_3), _ITEM(4_0), _ITEM(4_1), _ITEM(4_2), _ITEM(4_3),
			      _ITEM(5_0), _ITEM(5_1), _ITEM(5_2), _ITEM(5_3), _ITEM(6_0), _ITEM(6_1),
			      _ITEM(6_2), _ITEM(6_3), _ITEM(7_0), _ITEM(7_1), _ITEM(7_2), _ITEM(7_3),
		      })
#undef _ITEM
#define _ITEM(NAME) { VALUE(RATE_CONTROL_METHOD_ ## NAME), #NAME }
		_ENUM(AV1_RATE_CONTROL_METHOD, NAME(RATE_CONTROL_METHOD), _RATE_CONTROL_ITEMS)
#undef _ITEM
#define _ITEM(NAME) { VALUE(ALIGNMENT_MODE_ ## NAME), #NAME }
		_ENUM(AV1_ALIGNMENT_MODE, NAME(ALIGNMENT_MODE),
		      {
			      _ITEM(64X16_ONLY),
			      _ITEM(64X16_1080P_CODED_1082),
			      _ITEM(NO_RESTRICTIONS),
			      _ITEM(8X2_ONLY),
		      })
#undef _ITEM
#define _ITEM(NAME) { VALUE(QUALITY_PRESET_ ## NAME), #NAME }
		_ENUM(AV1_QUALITY_PRESET, NAME(QUALITY_PRESET), _QUALITY_ITEMS)
#undef _ITEM
#define _ITEM(NAME) { VALUE(HEADER_INSERTION_MODE_ ## NAME), #NAME }
		_ENUM(AV1_HEADER_INSERTION_MODE, NAME(HEADER_INSERTION_MODE),
		      {
			      _ITEM(NONE),
			      _ITEM(GOP_ALIGNED),
			      _ITEM(KEY_FRAME_ALIGNED),
			      _ITEM(SUPPRESSED),
		      })
#undef _ITEM
#define _ITEM(NAME) { VALUE(SWITCH_FRAME_INSERTION_MODE_ ## NAME), #NAME }
		_ENUM(AV1_SWITCH_FRAME_INSERTION_MODE, NAME(SWITCH_FRAME_INSERTION_MODE),
		      {
			      _ITEM(NONE),
			      _ITEM(FIXED_INTERVAL),
		      })
#undef _ITEM
#define _ITEM(NAME) { VALUE(CDEF_ ## NAME), #NAME }
		_ENUM(AV1_CDEF_MODE, NAME(CDEF_MODE),
		      {
			      _ITEM(DISABLE),
			      _ITEM(ENABLE_DEFAULT),
		      })
#undef _ITEM
#define _ITEM(NAME) { VALUE(CDF_FRAME_END_UPDATE_MODE_ ## NAME), #NAME }
		_ENUM(AV1_CDF_FRAME_END_UPDATE_MODE, NAME(CDF_FRAME_END_UPDATE_MODE),
		      {
			      _ITEM(DISABLE),
			      _ITEM(ENABLE_DEFAULT),
		      })
#undef _ITEM
#define _ITEM(NAME) { VALUE(AQ_MODE_ ## NAME), #NAME }
		_ENUM(AV1_AQ_MODE, NAME(AQ_MODE),
		      {
			      _ITEM(NONE),
			      _ITEM(CAQ),
		      })
#undef _ITEM
#define _ITEM(NAME) { VALUE(INTRA_REFRESH_MODE__ ## NAME), #NAME }
		_ENUM(AV1_INTRA_REFRESH_MODE, NAME(INTRA_REFRESH_MODE),
		      {
			      _ITEM(DISABLED),
			      _ITEM(GOP_ALIGNED),
			      _ITEM(CONTINUOUS),
		      })
#undef _ITEM
#define _ITEM(NAME) { VALUE(LTR_MODE_ ## NAME), #NAME }
		_ENUM(AV1_LTR_MODE, NAME(LTR_MODE), _LTR_MODE_ITEMS)
#undef _ITEM
#define _ITEM(NAME) { VALUE(OUTPUT_MODE_ ## NAME), #NAME }
		_ENUM(AV1_OUTPUT_MODE, NAME(OUTPUT_MODE),
		      {
			      _ITEM(FRAME),
			      _ITEM(TILE),
		      })
#undef _ITEM
#undef NAME
#undef VALUE

// HEVC
#define NAME(NAME) "AMF_VIDEO_ENCODER_HEVC_" #NAME
#define VALUE HEVC_PROPERTY
#define _ITEM(NAME) { VALUE(USAGE_ ## NAME), #NAME }
		_ENUM(HEVC_USAGE, NAME(USAGE), _USAGE_ITEMS)
#undef _ITEM
#define _ITEM(NAME) { VALUE(PROFILE_ ## NAME), #NAME }
		_ENUM(HEVC_PROFILE, NAME(PROFILE),
		      {
			      _ITEM(MAIN),
			      _ITEM(MAIN_10),
		      })
#undef _ITEM
#define _ITEM(NAME) { VALUE(TIER_ ## NAME), #NAME }
		_ENUM(HEVC_TIER, NAME(TIER),
		      {
			      _ITEM(MAIN),
			      _ITEM(HIGH),
		      })
#undef _ITEM
#define _ITEM(NAME) { AMF_LEVEL_ ## NAME, #NAME }
		_ENUM(HEVC_LEVEL, "AMF_LEVEL",
		      {
			      _ITEM(1),
			      _ITEM(2),
			      _ITEM(2_1),
			      _ITEM(3),
			      _ITEM(3_1),
			      _ITEM(4),
			      _ITEM(4_1),
			      _ITEM(5),
			      _ITEM(5_1),
			      _ITEM(5_2),
			      _ITEM(6),
			      _ITEM(6_1),
			      _ITEM(6_2),
		      })
#undef _ITEM
#define _ITEM(NAME) { VALUE(RATE_CONTROL_METHOD_ ## NAME), #NAME }
		_ENUM(HEVC_RATE_CONTROL_METHOD, NAME(RATE_CONTROL_METHOD), _RATE_CONTROL_ITEMS)
#undef _ITEM
#define _ITEM(NAME) { VALUE(QUALITY_PRESET_ ## NAME), #NAME }
		_ENUM(HEVC_QUALITY_PRESET, NAME(QUALITY_PRESET), _QUALITY_ITEMS)
#undef _ITEM
#define _ITEM(NAME) { VALUE(HEADER_INSERTION_MODE_ ## NAME), #NAME }
		_ENUM(HEVC_HEADER_INSERTION_MODE, NAME(HEADER_INSERTION_MODE),
		      {
			      _ITEM(NONE),
			      _ITEM(GOP_ALIGNED),
			      _ITEM(IDR_ALIGNED),
			      _ITEM(SUPPRESSED),
		      })
#undef _ITEM
#define _ITEM(NAME) { VALUE(PICTURE_TRANSFER_MODE_ ## NAME), #NAME }
		_ENUM(HEVC_PICTURE_TRANSFER_MODE, NAME(PICTURE_TRANSFER_MODE), _OFF_ON_ITEMS)
#undef _ITEM
#define _ITEM(NAME) { VALUE(NOMINAL_RANGE_ ## NAME), #NAME }
		_ENUM(HEVC_NOMINAL_RANGE, NAME(NOMINAL_RANGE),
		      {
			      _ITEM(STUDIO),
			      _ITEM(FULL),
		      })
#undef _ITEM
#define _ITEM(NAME) { VALUE(LTR_MODE_ ## NAME), #NAME }
		_ENUM(HEVC_LTR_MODE, NAME(LTR_MODE), _LTR_MODE_ITEMS)
#undef _ITEM
#define _ITEM(NAME) { VALUE(OUTPUT_MODE_ ## NAME), #NAME }
		_ENUM(HEVC_OUTPUT_MODE, NAME(OUTPUT_MODE),
		      {
			      _ITEM(FRAME),
			      _ITEM(SLICE),
		      })
#undef _ITEM
#undef NAME
#undef VALUE

// Pre-Analysis
#define _ITEM(NAME) { AMF_PA_ACTIVITY_ ## NAME, #NAME }
		_ENUM(PA_ACTIVITY_TYPE, "AMF_PA_ACTIVITY",
		      {
			      _ITEM(Y),
			      _ITEM(YUV),
		      })
#undef _ITEM
#define _ITEM(NAME) { AMF_PA_CAQ_STRENGTH_ ## NAME, #NAME }
		_ENUM(PA_CAQ_STRENGTH, "AMF_PA_CAQ_STRENGTH", _LOW_MEDIUM_HIGH_ITEMS)
#undef _ITEM
#define _ITEM(NAME) { AMF_PA_HIGH_MOTION_QUALITY_BOOST_MODE_ ## NAME, #NAME }
		_ENUM(PA_HIGH_MOTION_QUALITY_BOOST_MODE, "AMF_PA_HIGH_MOTION_QUALITY_BOOST_MODE",
		      {
			      _ITEM(NONE),
			      _ITEM(AUTO),
		      })
#undef _ITEM
#define _ITEM(NAME) { AMF_PA_PAQ_MODE_ ## NAME, #NAME }
		_ENUM(PA_PAQ_MODE, "AMF_PA_PAQ_MODE",
		      {
			      _ITEM(NONE),
			      _ITEM(CAQ),
		      })
#undef _ITEM
#define _ITEM(NAME) { AMF_PA_SCENE_CHANGE_DETECTION_SENSITIVITY_ ## NAME, #NAME }
		_ENUM(PA_SCENE_CHANGE_DETECTION_SENSITIVITY, "AMF_PA_SCENE_CHANGE_DETECTION_SENSITIVITY",
		      _LOW_MEDIUM_HIGH_ITEMS)
#undef _ITEM
#define _ITEM(NAME) { AMF_PA_STATIC_SCENE_DETECTION_SENSITIVITY_ ## NAME, #NAME }
		_ENUM(PA_STATIC_SCENE_DETECTION_SENSITIVITY, "AMF_PA_STATIC_SCENE_DETECTION_SENSITIVITY",
		      _LOW_MEDIUM_HIGH_ITEMS)
#undef _ITEM
#define _ITEM(NAME) { AMF_PA_TAQ_MODE_ ## NAME, #NAME }
		_ENUM(PA_TAQ_MODE, "AMF_PA_TAQ_MODE",
		      {
			      _ITEM(NONE),
			      _ITEM(1),
			      _ITEM(2),
		      })
#undef _ITEM

#undef _ENUM
#undef _LOW_MEDIUM_HIGH_ITEMS
#undef _LTR_MODE_ITEMS
#undef _OFF_ON_ITEMS
#undef _QUALITY_ITEMS
#undef _RATE_CONTROL_ITEMS
#undef _USAGE_ITEMS

	// General
	case PropertyType::RATE: {
		AMFRate value = getProperty<AMFRate>(storage, name);
		stringstream ss;
		ss << "{" << value.num << ", " << value.den << "}";
		return ss.str();
	}
	case PropertyType::RATIO: {
		AMFRatio value = getProperty<AMFRatio>(storage, name);
		stringstream ss;
		ss << "{" << value.num << ", " << value.den << "}";
		return ss.str();
	}
	case PropertyType::SIZE: {
		AMFSize value = getProperty<AMFSize>(storage, name);
		stringstream ss;
		ss << "{" << value.width << ", " << value.height << "}";
		return ss.str();
	}

	// Primitives
	case PropertyType::INT:
	default: {
		amf_int64 value = getProperty<amf_int64>(storage, name);
		return std::to_string(value);
	}
	case PropertyType::UINT: {
		amf_uint64 value = getProperty<amf_uint64>(storage, name);
		return std::to_string(value);
	}
	case PropertyType::BOOL: {
		bool value = getProperty<bool>(storage, name);
		return value ? "true" : "false";
	}
	}
}

static string propertyToString(const AMFPropertyStorage *storage, const wchar_t *name, const PropertyType type)
{
	stringstream ss;
	ss << nameToString(name);
	ss << ": ";
	ss << valueToString(storage, name, type);
	return ss.str();
}

void printProperties(stringstream &ss, AMFPropertyStorage *storage, const PropertyTypes &properties,
		     unsigned int indent)
{
	string s;

	for (auto const &[name, type] : properties) {
		try {
			s = propertyToString(storage, name, type);
		} catch (AMFException) {
			continue;
		}

		if (ss.tellp())
			ss << "\n";
		for (unsigned int i = 0; i < indent; i++)
			ss << "\t";
		ss << s;
	}
}

void printProperties(stringstream &ss, AMFPropertyStorage *storage, const char *category,
		     const PropertyTypes &properties, unsigned int indent)
{
	if (ss.tellp())
		ss << "\n";
	for (unsigned int i = 0; i < indent; i++)
		ss << "\t";
	ss << category << ":";
	printProperties(ss, storage, properties, indent + 1);
}

void printProperties(stringstream &ss, AMFPropertyStorage *storage, const CodecProperties &properties,
		     unsigned int indent)
{
	for (const char *category : properties.categories)
		printProperties(ss, storage, category, properties.properties.at(category), indent);
	CodecType codec = properties.codec;
	bool paEnabled = getBool(storage, AMF_PROPERTY(PRE_ANALYSIS_ENABLE));
	if (paEnabled)
		printProperties(ss, storage, "Pre-Analysis", getPreAnalysisProperties(), indent);
}

static void getPropertyValues(AMFPropertyStorage *storage, const PropertyTypes &properties, PropertyValues &out)
{
	for (auto const &[name, type] : properties) {
		try {
			out[name] = valueToString(storage, name, type);
		} catch (AMFException) {
		}
	}
}

PropertyValues getPropertyValues(AMFPropertyStorage *storage, const CodecProperties &properties)
{
	PropertyValues values;
	for (auto const &[category, types] : properties.properties)
		getPropertyValues(storage, types, values);
	const PropertyTypes &paProperties = getPreAnalysisProperties();
	getPropertyValues(storage, paProperties, values);
	return values;
}

void printChangedPropertyValues(stringstream &ss, PropertyValues &from, PropertyValues &to, unsigned int indent)
{
	for (auto const &[name, value] : to) {
		try {
			if (value == from[name])
				continue;
		} catch (std::out_of_range) {
		}
		if (ss.tellp())
			ss << "\n";
		for (unsigned int i = 0; i < indent; i++)
			ss << "\t";
		ss << nameToString(name);
		ss << ": ";
		ss << value;
	}
}
