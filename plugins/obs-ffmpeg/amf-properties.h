#pragma once

/* Discover AMF encoder capabilities and properties.
 * TODO: Implement HEVC and AV1
 */

#include <list>

#include <AMF/components/VideoEncoderVCE.h>
#include <AMF/components/VideoEncoderHEVC.h>
using namespace amf;

enum AMF_PROPERTY_TYPE {
#define _ITEM(NAME) AMF_PROPERTY_TYPE_##NAME
	// Primitives
	_ITEM(BOOL),
	_ITEM(INT),
	_ITEM(UINT),
	// General
	_ITEM(ACCEL),
	_ITEM(COLOR_BIT_DEPTH),
	_ITEM(COLOR_PRIMARIES),
	_ITEM(COLOR_PROFILE),
	_ITEM(COLOR_TRANSFER_CHARACTERISTIC),
	_ITEM(MEMORY_TYPE),
	_ITEM(RATE),
	_ITEM(RATIO),
	_ITEM(SIZE),
	// AVC
	_ITEM(AVC_CODING),
	_ITEM(AVC_H264_LEVEL),
	_ITEM(AVC_LTR_MODE),
	_ITEM(AVC_OUTPUT_MODE),
	_ITEM(AVC_PICTURE_TRANSFER_MODE),
	_ITEM(AVC_PREENCODE_MODE),
	_ITEM(AVC_PROFILE),
	_ITEM(AVC_QUALITY_PRESET),
	_ITEM(AVC_RATE_CONTROL_METHOD),
	_ITEM(AVC_SCANTYPE),
	_ITEM(AVC_USAGE),
	// HEVC
	_ITEM(HEVC_HEADER_INSERTION_MODE),
	_ITEM(HEVC_LEVEL),
	_ITEM(HEVC_LTR_MODE),
	_ITEM(HEVC_NOMINAL_RANGE),
	_ITEM(HEVC_OUTPUT_MODE),
	_ITEM(HEVC_PICTURE_TRANSFER_MODE),
	_ITEM(HEVC_PROFILE),
	_ITEM(HEVC_QUALITY_PRESET),
	_ITEM(HEVC_RATE_CONTROL_METHOD),
	_ITEM(HEVC_TIER),
	_ITEM(HEVC_USAGE),
	// Pre-Analysis
	_ITEM(PA_ACTIVITY_TYPE),
	_ITEM(PA_CAQ_STRENGTH),
	_ITEM(PA_HIGH_MOTION_QUALITY_BOOST_MODE),
	_ITEM(PA_PAQ_MODE),
	_ITEM(PA_SCENE_CHANGE_DETECTION_SENSITIVITY),
	_ITEM(PA_STATIC_SCENE_DETECTION_SENSITIVITY),
	_ITEM(PA_TAQ_MODE),
#undef _ITEM
};

static const std::map<const wchar_t *, AMF_PROPERTY_TYPE> amf_avc_capability_types{
#define _ITEM(NAME, TYPE) {AMF_VIDEO_ENCODER_CAP_##NAME, AMF_PROPERTY_TYPE_##TYPE}
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
};

static const char *amf_avc_property_categories[] = {
	"Static", "Color Conversion", "Rate Control",  "Picture Control", "Motion Estimation",
	"SVC",    "Feedback",         "Miscellaneous",
};

static const std::map<const char *, std::map<const wchar_t *, AMF_PROPERTY_TYPE>> amf_avc_property_types{
#define _ITEM(NAME, TYPE) {AMF_VIDEO_ENCODER_##NAME, AMF_PROPERTY_TYPE_##TYPE}
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
	{"Feedback",
	 {
		 _ITEM(PSNR_FEEDBACK, BOOL),
		 _ITEM(SSIM_FEEDBACK, BOOL),
		 _ITEM(BLOCK_QP_FEEDBACK, BOOL),
		 _ITEM(STATISTICS_FEEDBACK, BOOL),
	 }},
	{"Miscellaneous",
	 {
		 // EXTRADATA
		 _ITEM(SCANTYPE, AVC_SCANTYPE),
		 _ITEM(QUALITY_PRESET, AVC_QUALITY_PRESET),
		 _ITEM(FULL_RANGE_COLOR, BOOL),
		 _ITEM(PICTURE_TRANSFER_MODE, AVC_PICTURE_TRANSFER_MODE),
		 _ITEM(QUERY_TIMEOUT, INT),
		 _ITEM(INPUT_QUEUE_SIZE, INT),
		 _ITEM(OUTPUT_MODE, AVC_OUTPUT_MODE),
		 _ITEM(MEMORY_TYPE, MEMORY_TYPE),
	 }},
#undef _ITEM
};

static const std::map<const wchar_t *, AMF_PROPERTY_TYPE> amf_hevc_capability_types{
#define _ITEM(NAME, TYPE) {AMF_VIDEO_ENCODER_HEVC_CAP_##NAME, AMF_PROPERTY_TYPE_##TYPE}
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
};

static const char *amf_hevc_property_categories[] = {
	"Static",           "Rate Control", "Picture Control", "Motion Estimation",
	"Color Conversion", "SVC",          "Feedback",        "Miscellaneous",
};

static const std::map<const char *, std::map<const wchar_t *, AMF_PROPERTY_TYPE>> amf_hevc_property_types{
#define _ITEM(NAME, TYPE) {AMF_VIDEO_ENCODER_HEVC_##NAME, AMF_PROPERTY_TYPE_##TYPE}
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
	{"Feedback",
	 {
		 _ITEM(STATISTICS_FEEDBACK, BOOL),
		 _ITEM(PSNR_FEEDBACK, BOOL),
		 _ITEM(SSIM_FEEDBACK, BOOL),
		 _ITEM(BLOCK_QP_FEEDBACK, BOOL),
	 }},
	{"Miscellaneous",
	 {
		 _ITEM(QUALITY_PRESET, HEVC_QUALITY_PRESET),
		 // EXTRADATA
		 _ITEM(PICTURE_TRANSFER_MODE, HEVC_PICTURE_TRANSFER_MODE),
		 _ITEM(QUERY_TIMEOUT, INT),
		 _ITEM(INPUT_QUEUE_SIZE, INT),
		 _ITEM(OUTPUT_MODE, HEVC_OUTPUT_MODE),
		 _ITEM(MEMORY_TYPE, MEMORY_TYPE),
		 _ITEM(MULTI_HW_INSTANCE_ENCODE, BOOL),
	 }},
#undef _ITEM
};

static const std::map<const wchar_t *, AMF_PROPERTY_TYPE> amf_pa_property_types{
#define _ITEM(NAME, TYPE) {AMF_PA_##NAME, AMF_PROPERTY_TYPE_##TYPE}
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

static void amf_property_value_string_enum(std::ostringstream &ss, const AMFPropertyStorage *storage,
					   const wchar_t *name, const std::string prefix,
					   const std::map<int, std::string> strings)
{
	amf_int64 value;
	storage->GetProperty<amf_int64>(name, &value);
	ss << value << " (" << prefix << "_";
	try {
		ss << strings.at(value);
	} catch (const std::out_of_range) {
		ss << "???";
	}
	ss << ")";
}

static void amf_property_value_string(std::ostringstream &ss, const AMFPropertyStorage *storage, const wchar_t *name,
				      const AMF_PROPERTY_TYPE type)
{
	switch (type) {
#define _ENUM(TYPE, PREFIX, ...)                                                         \
	case AMF_PROPERTY_TYPE_##TYPE:                                                   \
		amf_property_value_string_enum(ss, storage, name, #PREFIX, __VA_ARGS__); \
		break;

// General
#define _ITEM(NAME) {AMF_ACCEL_##NAME, #NAME}
		_ENUM(ACCEL, AMF_ACCEL,
		      {
			      _ITEM(NOT_SUPPORTED),
			      _ITEM(HARDWARE),
			      _ITEM(GPU),
		      })
#define _ITEM(NAME) {AMF_COLOR_BIT_DEPTH_##NAME, #NAME}
		_ENUM(COLOR_BIT_DEPTH, AMF_COLOR_BIT_DEPTH,
		      {
			      _ITEM(UNDEFINED),
			      _ITEM(8),
			      _ITEM(10),
		      })
#define _ITEM(NAME) {AMF_COLOR_PRIMARIES_##NAME, #NAME}
		_ENUM(COLOR_PRIMARIES, AMF_COLOR_PRIMARIES,
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
#define _ITEM(NAME) {AMF_VIDEO_CONVERTER_COLOR_PROFILE_##NAME, #NAME}
		_ENUM(COLOR_PROFILE, AMF_VIDEO_CONVERTER_COLOR_PROFILE,
		      {
			      _ITEM(UNKNOWN),
			      _ITEM(601),
			      _ITEM(709),
			      _ITEM(2020),
			      _ITEM(FULL_601),
			      _ITEM(FULL_709),
			      _ITEM(FULL_2020),
		      })
#define _ITEM(NAME) {AMF_COLOR_TRANSFER_CHARACTERISTIC_##NAME, #NAME}
		_ENUM(COLOR_TRANSFER_CHARACTERISTIC, AMF_COLOR_TRANSFER_CHARACTERISTIC,
		      {
			      _ITEM(UNDEFINED),  _ITEM(BT709),        _ITEM(UNSPECIFIED),  _ITEM(RESERVED),
			      _ITEM(GAMMA22),    _ITEM(GAMMA28),      _ITEM(SMPTE170M),    _ITEM(SMPTE240M),
			      _ITEM(LINEAR),     _ITEM(LOG),          _ITEM(LOG_SQRT),     _ITEM(IEC61966_2_4),
			      _ITEM(BT1361_ECG), _ITEM(IEC61966_2_1), _ITEM(BT2020_10),    _ITEM(BT2020_12),
			      _ITEM(SMPTE2084),  _ITEM(SMPTE428),     _ITEM(ARIB_STD_B67),
		      })
#define _ITEM(NAME) {AMF_MEMORY_##NAME, #NAME}
		_ENUM(MEMORY_TYPE, AMF_MEMORY,
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
			      _ITEM(LAST),
		      })

// AVC
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_##NAME, #NAME}
		_ENUM(AVC_CODING, AMF_VIDEO_ENCODER,
		      {
			      _ITEM(UNDEFINED),
			      _ITEM(CABAC),
			      _ITEM(CALV),
		      })
#define _ITEM(NAME) {AMF_H264_LEVEL__##NAME, #NAME}
		_ENUM(AVC_H264_LEVEL, AMF_H264_LEVEL_,
		      {
			      _ITEM(1),   _ITEM(1_1), _ITEM(1_2), _ITEM(1_3), _ITEM(2),   _ITEM(2_1), _ITEM(2_2),
			      _ITEM(3),   _ITEM(3_1), _ITEM(3_2), _ITEM(4),   _ITEM(4_1), _ITEM(4_2), _ITEM(5),
			      _ITEM(5_1), _ITEM(5_2), _ITEM(6),   _ITEM(6_1), _ITEM(6_2),
		      })
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_LTR_MODE_##NAME, #NAME}
		_ENUM(AVC_LTR_MODE, AMF_VIDEO_ENCODER_LTR_MODE,
		      {
			      _ITEM(RESET_UNUSED),
			      _ITEM(KEEP_UNUSED),
		      })
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_OUTPUT_MODE_##NAME, #NAME}
		_ENUM(AVC_OUTPUT_MODE, AMF_VIDEO_ENCODER_OUTPUT_MODE,
		      {
			      _ITEM(FRAME),
			      _ITEM(SLICE),
		      })
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_PICTURE_TRANSFER_MODE_##NAME, #NAME}
		_ENUM(AVC_PICTURE_TRANSFER_MODE, AMF_VIDEO_ENCODER_PICTURE_TRANSFER_MODE,
		      {
			      _ITEM(OFF),
			      _ITEM(ON),
		      })
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_PREENCODE_##NAME, #NAME}
		_ENUM(AVC_PREENCODE_MODE, AMF_VIDEO_ENCODER_PREENCODE,
		      {
			      _ITEM(DISABLED),
			      _ITEM(ENABLED),
		      })
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_PROFILE_##NAME, #NAME}
		_ENUM(AVC_PROFILE, AMF_VIDEO_ENCODER_PROFILE,
		      {
			      _ITEM(BASELINE),
			      _ITEM(MAIN),
			      _ITEM(HIGH),
			      _ITEM(CONSTRAINED_BASELINE),
			      _ITEM(CONSTRAINED_HIGH),
		      })
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_QUALITY_PRESET_##NAME, #NAME}
		_ENUM(AVC_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET,
		      {
			      _ITEM(BALANCED),
			      _ITEM(SPEED),
			      _ITEM(QUALITY),
		      })
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_##NAME, #NAME}
		_ENUM(AVC_RATE_CONTROL_METHOD, AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD,
		      {
			      _ITEM(UNKNOWN),
			      _ITEM(CONSTANT_QP),
			      _ITEM(CBR),
			      _ITEM(PEAK_CONSTRAINED_VBR),
			      _ITEM(LATENCY_CONSTRAINED_VBR),
			      _ITEM(QUALITY_VBR),
			      _ITEM(HIGH_QUALITY_VBR),
			      _ITEM(HIGH_QUALITY_CBR),
		      })
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_SCANTYPE_##NAME, #NAME}
		_ENUM(AVC_SCANTYPE, AMF_VIDEO_ENCODER_SCANTYPE,
		      {
			      _ITEM(PROGRESSIVE),
			      _ITEM(INTERLACED),
		      })
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_USAGE_##NAME, #NAME}
		_ENUM(AVC_USAGE, AMF_VIDEO_ENCODER_USAGE,
		      {
			      _ITEM(TRANSCODING),
			      _ITEM(ULTRA_LOW_LATENCY),
			      _ITEM(LOW_LATENCY),
			      _ITEM(WEBCAM),
			      _ITEM(HIGH_QUALITY),
			      _ITEM(LOW_LATENCY_HIGH_QUALITY),
		      })

// HEVC
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_HEVC_HEADER_INSERTION_MODE_##NAME, #NAME}
		_ENUM(HEVC_HEADER_INSERTION_MODE, AMF_VIDEO_ENCODER_HEVC_HEADER_INSERTION_MODE,
		      {
			      _ITEM(NONE),
			      _ITEM(GOP_ALIGNED),
			      _ITEM(IDR_ALIGNED),
			      _ITEM(SUPPRESSED),
		      })
#define _ITEM(NAME) {AMF_LEVEL_##NAME, #NAME}
		_ENUM(HEVC_LEVEL, AMF_LEVEL,
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
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_HEVC_LTR_MODE_##NAME, #NAME}
		_ENUM(HEVC_LTR_MODE, AMF_VIDEO_ENCODER_HEVC_LTR_MODE,
		      {
			      _ITEM(RESET_UNUSED),
			      _ITEM(KEEP_UNUSED),
		      })
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_HEVC_NOMINAL_RANGE_##NAME, #NAME}
		_ENUM(HEVC_NOMINAL_RANGE, AMF_VIDEO_ENCODER_HEVC_NOMINAL_RANGE,
		      {
			      _ITEM(STUDIO),
			      _ITEM(FULL),
		      })
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_HEVC_OUTPUT_MODE_##NAME, #NAME}
		_ENUM(HEVC_OUTPUT_MODE, AMF_VIDEO_ENCODER_HEVC_OUTPUT_MODE,
		      {
			      _ITEM(FRAME),
			      _ITEM(SLICE),
		      })
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_HEVC_PICTURE_TRANSFER_MODE_##NAME, #NAME}
		_ENUM(HEVC_PICTURE_TRANSFER_MODE, AMF_VIDEO_ENCODER_HEVC_PICTURE_TRANSFER_MODE,
		      {
			      _ITEM(OFF),
			      _ITEM(ON),
		      })
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_HEVC_PROFILE_##NAME, #NAME}
		_ENUM(HEVC_PROFILE, AMF_VIDEO_ENCODER_HEVC_PROFILE,
		      {
			      _ITEM(MAIN),
			      _ITEM(MAIN_10),
		      })
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_##NAME, #NAME}
		_ENUM(HEVC_QUALITY_PRESET, AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET,
		      {
			      _ITEM(QUALITY),
			      _ITEM(BALANCED),
			      _ITEM(SPEED),
		      })
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_##NAME, #NAME}
		_ENUM(HEVC_RATE_CONTROL_METHOD, AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD,
		      {
			      _ITEM(UNKNOWN),
			      _ITEM(CONSTANT_QP),
			      _ITEM(LATENCY_CONSTRAINED_VBR),
			      _ITEM(PEAK_CONSTRAINED_VBR),
			      _ITEM(CBR),
			      _ITEM(QUALITY_VBR),
			      _ITEM(HIGH_QUALITY_VBR),
			      _ITEM(HIGH_QUALITY_CBR),
		      })
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_HEVC_TIER_##NAME, #NAME}
		_ENUM(HEVC_TIER, AMF_VIDEO_ENCODER_HEVC_TIER,
		      {
			      _ITEM(MAIN),
			      _ITEM(HIGH),
		      })
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_HEVC_USAGE_##NAME, #NAME}
		_ENUM(HEVC_USAGE, AMF_VIDEO_ENCODER_HEVC_USAGE,
		      {
			      _ITEM(TRANSCODING),
			      _ITEM(ULTRA_LOW_LATENCY),
			      _ITEM(LOW_LATENCY),
			      _ITEM(WEBCAM),
			      _ITEM(HIGH_QUALITY),
			      _ITEM(LOW_LATENCY_HIGH_QUALITY),
		      })

// Pre-Analysis
#define _ITEM(NAME) {AMF_PA_ACTIVITY_##NAME, #NAME}
		_ENUM(PA_ACTIVITY_TYPE, AMF_PA_ACTIVITY,
		      {
			      _ITEM(Y),
			      _ITEM(YUV),
		      })
#define _ITEM(NAME) {AMF_PA_CAQ_STRENGTH_##NAME, #NAME}
		_ENUM(PA_CAQ_STRENGTH, AMF_PA_CAQ_STRENGTH,
		      {
			      _ITEM(LOW),
			      _ITEM(MEDIUM),
			      _ITEM(HIGH),
		      })
#define _ITEM(NAME) {AMF_PA_HIGH_MOTION_QUALITY_BOOST_MODE_##NAME, #NAME}
		_ENUM(PA_HIGH_MOTION_QUALITY_BOOST_MODE, AMF_PA_HIGH_MOTION_QUALITY_BOOST_MODE,
		      {
			      _ITEM(NONE),
			      _ITEM(AUTO),
		      })
#define _ITEM(NAME) {AMF_PA_PAQ_MODE_##NAME, #NAME}
		_ENUM(PA_PAQ_MODE, AMF_PA_PAQ_MODE,
		      {
			      _ITEM(NONE),
			      _ITEM(CAQ),
		      })
#define _ITEM(NAME) {AMF_PA_SCENE_CHANGE_DETECTION_SENSITIVITY_##NAME, #NAME}
		_ENUM(PA_SCENE_CHANGE_DETECTION_SENSITIVITY, AMF_PA_SCENE_CHANGE_DETECTION_SENSITIVITY,
		      {
			      _ITEM(LOW),
			      _ITEM(MEDIUM),
			      _ITEM(HIGH),
		      })
#define _ITEM(NAME) {AMF_PA_STATIC_SCENE_DETECTION_SENSITIVITY_##NAME, #NAME}
		_ENUM(PA_STATIC_SCENE_DETECTION_SENSITIVITY, AMF_PA_STATIC_SCENE_DETECTION_SENSITIVITY,
		      {
			      _ITEM(LOW),
			      _ITEM(MEDIUM),
			      _ITEM(HIGH),
		      })
#define _ITEM(NAME) {AMF_PA_TAQ_MODE_##NAME, #NAME}
		_ENUM(PA_TAQ_MODE, AMF_PA_TAQ_MODE,
		      {
			      _ITEM(NONE),
			      _ITEM(1),
			      _ITEM(2),
		      })

#undef _ENUM
#undef _ITEM

	// General
	case AMF_PROPERTY_TYPE_RATE: {
		AMFRate value;
		storage->GetProperty<AMFRate>(name, &value);
		ss << "{" << value.num << ", " << value.den << "}";
	} break;
	case AMF_PROPERTY_TYPE_RATIO: {
		AMFRatio value;
		storage->GetProperty<AMFRatio>(name, &value);
		ss << "{" << value.num << ", " << value.den << "}";
	} break;
	case AMF_PROPERTY_TYPE_SIZE: {
		AMFSize value;
		storage->GetProperty<AMFSize>(name, &value);
		ss << "{" << value.width << ", " << value.height << "}";
	} break;

	// Primitives
	case AMF_PROPERTY_TYPE_INT: {
		amf_int64 value;
		storage->GetProperty<amf_int64>(name, &value);
		ss << value;
	} break;
	case AMF_PROPERTY_TYPE_UINT: {
		amf_uint64 value;
		storage->GetProperty<amf_uint64>(name, &value);
		ss << value;
	} break;
	case AMF_PROPERTY_TYPE_BOOL:
	default: {
		bool value;
		storage->GetProperty<bool>(name, &value);
		ss << (value ? "true" : "false");
	} break;
	}
}

static std::string amf_property_string(const AMFPropertyStorage *storage, const wchar_t *name,
				       const AMF_PROPERTY_TYPE type)
{
	std::ostringstream ss;
	for (int i = 0; i < wcslen(name); i++)
		ss << (char)name[i];
	ss << ": ";
	amf_property_value_string(ss, storage, name, type);
	return ss.str();
}

static std::list<std::string> amf_property_strings(const AMFPropertyStorage *storage,
						   const std::map<const wchar_t *, AMF_PROPERTY_TYPE> properties)
{
	std::list<std::string> strings;
	for (auto const &[name, type] : properties)
		strings.push_back(amf_property_string(storage, name, type));
	strings.sort();
	return strings;
}

static void amf_print_properties(std::ostringstream &ss, const AMFPropertyStorage *storage,
				 const std::map<const wchar_t *, AMF_PROPERTY_TYPE> properties, uint indent = 1)
{
	std::list<std::string> strings = amf_property_strings(storage, properties);
	for (std::string s : strings) {
		if (ss.tellp())
			ss << std::endl;
		for (int i = 0; i < indent; i++)
			ss << "\t";
		ss << s;
	}
}

static void amf_print_property_category(std::ostringstream &ss, const AMFPropertyStorage *storage, const char *category,
					const std::map<const wchar_t *, AMF_PROPERTY_TYPE> properties, uint indent = 1)
{
	if (ss.tellp())
		ss << std::endl;
	for (int i = 0; i < indent; i++)
		ss << "\t";
	ss << category << ":";
	amf_print_properties(ss, storage, properties, indent + 1);
}
