#pragma once

/* Discover AMF encoder capabilities and properties.
 * TODO: Implement HEVC and AV1
 */

#include <list>

#include <AMF/components/VideoEncoderVCE.h>
using namespace amf;

static enum AMF_PROPERTY_TYPE {
#define _ITEM(NAME) AMF_PROPERTY_TYPE_##NAME
	_ITEM(BOOL),
	_ITEM(INT),
	_ITEM(PROFILE),
	_ITEM(ACCEL),
	_ITEM(USAGE),
	_ITEM(LTR_MODE),
	_ITEM(SCANTYPE),
	_ITEM(PREENCODE_MODE),
	_ITEM(QUALITY_PRESET),
	_ITEM(COLOR_BIT_DEPTH),
	_ITEM(COLOR_PROFILE),
	_ITEM(RATE_CONTROL_METHOD),
	_ITEM(COLOR_TRANSFER_CHARACTERISTIC),
	_ITEM(COLOR_PRIMARIES),
	_ITEM(OUTPUT_MODE),
	_ITEM(SIZE),
	_ITEM(RATIO),
	_ITEM(RATE),
	_ITEM(CODING),
	_ITEM(PICTURE_TRANSFER_MODE),
	_ITEM(MEMORY_TYPE),
	_ITEM(UINT),
	_ITEM(PA_SCENE_CHANGE_DETECTION_SENSITIVITY),
	_ITEM(PA_STATIC_SCENE_DETECTION_SENSITIVITY),
	_ITEM(PA_ACTIVITY_TYPE),
	_ITEM(PA_PAQ_MODE),
	_ITEM(PA_TAQ_MODE),
	_ITEM(PA_HIGH_MOTION_QUALITY_BOOST_MODE),
	_ITEM(PA_CAQ_STRENGTH),
#undef _ITEM
};

static const std::map<const wchar_t *, AMF_PROPERTY_TYPE> amf_pa_property_types{
#define _ITEM(NAME, TYPE) {AMF_PA_##NAME, AMF_PROPERTY_TYPE_##TYPE}
	_ITEM(ENGINE_TYPE, MEMORY_TYPE),
	_ITEM(SCENE_CHANGE_DETECTION_ENABLE, BOOL),
	_ITEM(SCENE_CHANGE_DETECTION_SENSITIVITY, PA_SCENE_CHANGE_DETECTION_SENSITIVITY),
	_ITEM(STATIC_SCENE_DETECTION_ENABLE, BOOL),
	_ITEM(STATIC_SCENE_DETECTION_SENSITIVITY, PA_STATIC_SCENE_DETECTION_SENSITIVITY),
	_ITEM(FRAME_SAD_ENABLE, BOOL),
	_ITEM(ACTIVITY_TYPE, PA_ACTIVITY_TYPE),
	_ITEM(LTR_ENABLE, BOOL),
	_ITEM(LOOKAHEAD_BUFFER_DEPTH, UINT),
	_ITEM(PAQ_MODE, PA_PAQ_MODE),
	_ITEM(TAQ_MODE, PA_TAQ_MODE),
	_ITEM(HIGH_MOTION_QUALITY_BOOST_MODE, PA_HIGH_MOTION_QUALITY_BOOST_MODE),
	_ITEM(INITIAL_QP_AFTER_SCENE_CHANGE, UINT),
	_ITEM(MAX_QP_BEFORE_FORCE_SKIP, UINT),
	_ITEM(CAQ_STRENGTH, PA_CAQ_STRENGTH),
#undef _ITEM
};

static const std::map<const wchar_t *, AMF_PROPERTY_TYPE> amf_avc_capability_types{
#define _ITEM(NAME, TYPE) {AMF_VIDEO_ENCODER_CAP_##NAME, AMF_PROPERTY_TYPE_##TYPE}
	_ITEM(MAX_BITRATE, INT),
	_ITEM(NUM_OF_STREAMS, INT),
	_ITEM(MAX_PROFILE, PROFILE),
	_ITEM(MAX_LEVEL, INT),
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
	"Static", "Input", "Output", "Rate Control", "Picture Control", "Misc",
};

static const std::map<const char *, std::map<const wchar_t *, AMF_PROPERTY_TYPE>> amf_avc_property_types{
#define _ITEM(NAME, TYPE) {AMF_VIDEO_ENCODER_##NAME, AMF_PROPERTY_TYPE_##TYPE}
	{"Static",
	 {
		 _ITEM(INSTANCE_INDEX, INT),
		 _ITEM(FRAMESIZE, SIZE),
		 // AMF_VIDEO_ENCODER_EXTRADATA
		 _ITEM(USAGE, USAGE),
		 _ITEM(PROFILE, PROFILE),
		 _ITEM(PROFILE_LEVEL, INT),
		 _ITEM(MAX_LTR_FRAMES, INT),
		 _ITEM(LTR_MODE, LTR_MODE),
		 _ITEM(SCANTYPE, SCANTYPE),
		 _ITEM(MAX_NUM_REFRAMES, INT),
		 _ITEM(MAX_CONSECUTIVE_BPICTURES, INT),
		 _ITEM(ADAPTIVE_MINIGOP, BOOL),
		 _ITEM(ASPECT_RATIO, RATIO),
		 _ITEM(FULL_RANGE_COLOR, BOOL),
		 _ITEM(LOWLATENCY_MODE, BOOL),
		 _ITEM(PRE_ANALYSIS_ENABLE, BOOL),
		 _ITEM(RATE_CONTROL_PREANALYSIS_ENABLE, PREENCODE_MODE),
		 _ITEM(RATE_CONTROL_METHOD, RATE_CONTROL_METHOD),
		 _ITEM(QVBR_QUALITY_LEVEL, INT),
		 _ITEM(MAX_NUM_TEMPORAL_LAYERS, INT),
		 _ITEM(QUALITY_PRESET, QUALITY_PRESET),
		 _ITEM(COLOR_BIT_DEPTH, COLOR_BIT_DEPTH),
	 }},
	{"Input",
	 {
		 _ITEM(INPUT_COLOR_PROFILE, COLOR_PROFILE),
		 _ITEM(INPUT_TRANSFER_CHARACTERISTIC, COLOR_TRANSFER_CHARACTERISTIC),
		 _ITEM(INPUT_COLOR_PRIMARIES, COLOR_PRIMARIES),
		 // AMF_VIDEO_ENCODER_INPUT_HDR_METADATA
	 }},
	{"Output",
	 {
		 _ITEM(OUTPUT_COLOR_PROFILE, COLOR_PROFILE),
		 _ITEM(OUTPUT_TRANSFER_CHARACTERISTIC, COLOR_TRANSFER_CHARACTERISTIC),
		 _ITEM(OUTPUT_COLOR_PRIMARIES, COLOR_PRIMARIES),
		 // AMF_VIDEO_ENCODER_OUTPUT_HDR_METADATA
		 _ITEM(OUTPUT_MODE, OUTPUT_MODE),
	 }},
	{"Rate Control",
	 {
		 _ITEM(FRAMERATE, RATE),
		 _ITEM(B_PIC_DELTA_QP, INT),
		 _ITEM(REF_B_PIC_DELTA_QP, INT),
		 _ITEM(ENFORCE_HRD, BOOL),
		 _ITEM(FILLER_DATA_ENABLE, BOOL),
		 _ITEM(ENABLE_VBAQ, BOOL),
		 _ITEM(HIGH_MOTION_QUALITY_BOOST_ENABLE, BOOL),
		 _ITEM(VBV_BUFFER_SIZE, INT),
		 _ITEM(INITIAL_VBV_BUFFER_FULLNESS, INT),
		 _ITEM(MAX_AU_SIZE, INT),
		 _ITEM(MIN_QP, INT),
		 _ITEM(MAX_QP, INT),
		 _ITEM(QP_I, INT),
		 _ITEM(QP_P, INT),
		 _ITEM(QP_B, INT),
		 _ITEM(TARGET_BITRATE, INT),
		 _ITEM(PEAK_BITRATE, INT),
		 _ITEM(RATE_CONTROL_SKIP_FRAME_ENABLE, BOOL),
	 }},
	{"Picture Control",
	 {
		 _ITEM(HEADER_INSERTION_SPACING, INT),
		 _ITEM(B_PIC_PATTERN, INT),
		 _ITEM(DE_BLOCKING_FILTER, BOOL),
		 _ITEM(B_REFERENCE_ENABLE, BOOL),
		 _ITEM(IDR_PERIOD, INT),
		 _ITEM(INTRA_PERIOD, INT),
		 _ITEM(INTRA_REFRESH_NUM_MBS_PER_SLOT, INT),
		 _ITEM(SLICES_PER_FRAME, INT),
		 _ITEM(CABAC_ENABLE, CODING),
		 _ITEM(MOTION_HALF_PIXEL, BOOL),
		 _ITEM(MOTION_QUARTERPIXEL, BOOL),
		 _ITEM(NUM_TEMPORAL_ENHANCMENT_LAYERS, INT),
		 _ITEM(PICTURE_TRANSFER_MODE, PICTURE_TRANSFER_MODE),
	 }},
	{"Misc",
	 {
		 _ITEM(QUERY_TIMEOUT, INT),
		 _ITEM(MEMORY_TYPE, MEMORY_TYPE),
		 _ITEM(ENABLE_SMART_ACCESS_VIDEO, BOOL),
		 _ITEM(INPUT_QUEUE_SIZE, INT),
	 }},
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

	case AMF_PROPERTY_TYPE_PA_ACTIVITY_TYPE:
#define _ITEM(NAME) {AMF_PA_ACTIVITY_##NAME, #NAME}
		amf_property_value_string_enum(ss, storage, name, "AMF_PA_ACTIVITY",
					       {
						       _ITEM(Y),
						       _ITEM(YUV),
					       });
#undef _ITEM
		break;

	case AMF_PROPERTY_TYPE_PA_CAQ_STRENGTH:
#define _ITEM(NAME) {AMF_PA_CAQ_STRENGTH_##NAME, #NAME}
		amf_property_value_string_enum(ss, storage, name, "AMF_PA_CAQ_STRENGTH",
					       {
						       _ITEM(LOW),
						       _ITEM(MEDIUM),
						       _ITEM(HIGH),
					       });
#undef _ITEM
		break;

	case AMF_PROPERTY_TYPE_PA_HIGH_MOTION_QUALITY_BOOST_MODE:
#define _ITEM(NAME) {AMF_PA_HIGH_MOTION_QUALITY_BOOST_MODE_##NAME, #NAME}
		amf_property_value_string_enum(ss, storage, name, "AMF_PA_HIGH_MOTION_QUALITY_BOOST_MODE",
					       {
						       _ITEM(NONE),
						       _ITEM(AUTO),
					       });
#undef _ITEM
		break;

	case AMF_PROPERTY_TYPE_PA_PAQ_MODE:
#define _ITEM(NAME) {AMF_PA_PAQ_MODE_##NAME, #NAME}
		amf_property_value_string_enum(ss, storage, name, "AMF_PA_PAQ_MODE",
					       {
						       _ITEM(NONE),
						       _ITEM(CAQ),
					       });
#undef _ITEM
		break;

	case AMF_PROPERTY_TYPE_PA_SCENE_CHANGE_DETECTION_SENSITIVITY:
#define _ITEM(NAME) {AMF_PA_SCENE_CHANGE_DETECTION_SENSITIVITY_##NAME, #NAME}
		amf_property_value_string_enum(ss, storage, name, "AMF_PA_SCENE_CHANGE_DETECTION_SENSITIVITY",
					       {
						       _ITEM(LOW),
						       _ITEM(MEDIUM),
						       _ITEM(HIGH),
					       });
#undef _ITEM
		break;

	case AMF_PROPERTY_TYPE_PA_STATIC_SCENE_DETECTION_SENSITIVITY:
#define _ITEM(NAME) {AMF_PA_STATIC_SCENE_DETECTION_SENSITIVITY_##NAME, #NAME}
		amf_property_value_string_enum(ss, storage, name, "AMF_PA_STATIC_SCENE_DETECTION_SENSITIVITY",
					       {
						       _ITEM(LOW),
						       _ITEM(MEDIUM),
						       _ITEM(HIGH),
					       });
#undef _ITEM
		break;

	case AMF_PROPERTY_TYPE_PA_TAQ_MODE:
#define _ITEM(NAME) {AMF_PA_TAQ_MODE_##NAME, #NAME}
		amf_property_value_string_enum(ss, storage, name, "AMF_PA_TAQ_MODE",
					       {
						       _ITEM(NONE),
						       _ITEM(1),
						       _ITEM(2),
					       });
#undef _ITEM
		break;

	case AMF_PROPERTY_TYPE_ACCEL:
#define _ITEM(NAME) {AMF_ACCEL_##NAME, #NAME}
		amf_property_value_string_enum(ss, storage, name, "AMF_ACCEL",
					       {
						       _ITEM(NOT_SUPPORTED),
						       _ITEM(HARDWARE),
						       _ITEM(GPU),
					       });
#undef _ITEM
		break;

	case AMF_PROPERTY_TYPE_CODING:
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_##NAME, #NAME}
		amf_property_value_string_enum(ss, storage, name, "AMF_VIDEO_ENCODER",
					       {
						       _ITEM(UNDEFINED),
						       _ITEM(CABAC),
						       _ITEM(CALV),
					       });
#undef _ITEM
		break;

	case AMF_PROPERTY_TYPE_COLOR_BIT_DEPTH:
#define _ITEM(NAME) {AMF_COLOR_BIT_DEPTH_##NAME, #NAME}
		amf_property_value_string_enum(ss, storage, name, "AMF_COLOR_BIT_DEPTH",
					       {
						       _ITEM(UNDEFINED),
						       _ITEM(8),
						       _ITEM(10),
					       });
#undef _ITEM
		break;

	case AMF_PROPERTY_TYPE_COLOR_PRIMARIES:
#define _ITEM(NAME) {AMF_COLOR_PRIMARIES_##NAME, #NAME}
		amf_property_value_string_enum(ss, storage, name, "AMF_COLOR_PRIMARIES",
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
					       });
#undef _ITEM
		break;

	case AMF_PROPERTY_TYPE_COLOR_PROFILE:
#define _ITEM(NAME) {AMF_VIDEO_CONVERTER_COLOR_PROFILE_##NAME, #NAME}
		amf_property_value_string_enum(ss, storage, name, "AMF_VIDEO_CONVERTER_COLOR_PROFILE",
					       {
						       _ITEM(UNKNOWN),
						       _ITEM(601),
						       _ITEM(709),
						       _ITEM(2020),
						       _ITEM(FULL_601),
						       _ITEM(FULL_709),
						       _ITEM(FULL_2020),
					       });
#undef _ITEM
		break;

	case AMF_PROPERTY_TYPE_COLOR_TRANSFER_CHARACTERISTIC:
#define _ITEM(NAME) {AMF_COLOR_TRANSFER_CHARACTERISTIC_##NAME, #NAME}
		amf_property_value_string_enum(
			ss, storage, name, "AMF_COLOR_TRANSFER_CHARACTERISTIC",
			{
				_ITEM(UNDEFINED),  _ITEM(BT709),        _ITEM(UNSPECIFIED),  _ITEM(RESERVED),
				_ITEM(GAMMA22),    _ITEM(GAMMA28),      _ITEM(SMPTE170M),    _ITEM(SMPTE240M),
				_ITEM(LINEAR),     _ITEM(LOG),          _ITEM(LOG_SQRT),     _ITEM(IEC61966_2_4),
				_ITEM(BT1361_ECG), _ITEM(IEC61966_2_1), _ITEM(BT2020_10),    _ITEM(BT2020_12),
				_ITEM(SMPTE2084),  _ITEM(SMPTE428),     _ITEM(ARIB_STD_B67),
			});
#undef _ITEM
		break;

	case AMF_PROPERTY_TYPE_LTR_MODE:
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_LTR_MODE_##NAME, #NAME}
		amf_property_value_string_enum(ss, storage, name, "AMF_VIDEO_ENCODER_LTR_MODE",
					       {
						       _ITEM(RESET_UNUSED),
						       _ITEM(KEEP_UNUSED),
					       });
#undef _ITEM
		break;

	case AMF_PROPERTY_TYPE_MEMORY_TYPE:
#define _ITEM(NAME) {AMF_MEMORY_##NAME, #NAME}
		amf_property_value_string_enum(ss, storage, name, "AMF_MEMORY",
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
					       });
#undef _ITEM
		break;

	case AMF_PROPERTY_TYPE_OUTPUT_MODE:
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_OUTPUT_MODE_##NAME, #NAME}
		amf_property_value_string_enum(ss, storage, name, "AMF_VIDEO_ENCODER_OUTPUT_MODE",
					       {
						       _ITEM(FRAME),
						       _ITEM(SLICE),
					       });
#undef _ITEM
		break;

	case AMF_PROPERTY_TYPE_PICTURE_TRANSFER_MODE:
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_PICTURE_TRANSFER_MODE_##NAME, #NAME}
		amf_property_value_string_enum(ss, storage, name, "AMF_VIDEO_ENCODER_PICTURE_TRANSFER_MODE",
					       {
						       _ITEM(OFF),
						       _ITEM(ON),
					       });
#undef _ITEM
		break;

	case AMF_PROPERTY_TYPE_PREENCODE_MODE:
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_PREENCODE_##NAME, #NAME}
		amf_property_value_string_enum(ss, storage, name, "AMF_VIDEO_ENCODER_PREENCODE_MODE",
					       {
						       _ITEM(DISABLED),
						       _ITEM(ENABLED),
					       });
#undef _ITEM
		break;

	case AMF_PROPERTY_TYPE_PROFILE:
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_PROFILE_##NAME, #NAME}
		amf_property_value_string_enum(ss, storage, name, "AMF_VIDEO_ENCODER_PROFILE",
					       {
						       _ITEM(BASELINE),
						       _ITEM(MAIN),
						       _ITEM(HIGH),
						       _ITEM(CONSTRAINED_BASELINE),
						       _ITEM(CONSTRAINED_HIGH),
					       });
#undef _ITEM
		break;

	case AMF_PROPERTY_TYPE_QUALITY_PRESET:
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_QUALITY_PRESET_##NAME, #NAME}
		amf_property_value_string_enum(ss, storage, name, "AMF_VIDEO_ENCODER_QUALITY_PRESET",
					       {
						       _ITEM(BALANCED),
						       _ITEM(SPEED),
						       _ITEM(QUALITY),
					       });
#undef _ITEM
		break;

	case AMF_PROPERTY_TYPE_RATE_CONTROL_METHOD:
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_##NAME, #NAME}
		amf_property_value_string_enum(ss, storage, name, "AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD",
					       {
						       _ITEM(UNKNOWN),
						       _ITEM(CONSTANT_QP),
						       _ITEM(CBR),
						       _ITEM(PEAK_CONSTRAINED_VBR),
						       _ITEM(LATENCY_CONSTRAINED_VBR),
						       _ITEM(QUALITY_VBR),
						       _ITEM(HIGH_QUALITY_VBR),
						       _ITEM(HIGH_QUALITY_CBR),
					       });
#undef _ITEM
		break;

	case AMF_PROPERTY_TYPE_SCANTYPE:
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_SCANTYPE_##NAME, #NAME}
		amf_property_value_string_enum(ss, storage, name, "AMF_VIDEO_ENCODER_SCANTYPE",
					       {
						       _ITEM(PROGRESSIVE),
						       _ITEM(INTERLACED),
					       });
#undef _ITEM
		break;

	case AMF_PROPERTY_TYPE_USAGE:
#define _ITEM(NAME) {AMF_VIDEO_ENCODER_USAGE_##NAME, #NAME}
		amf_property_value_string_enum(ss, storage, name, "AMF_VIDEO_ENCODER_USAGE",
					       {
						       _ITEM(TRANSCODING),
						       _ITEM(ULTRA_LOW_LATENCY),
						       _ITEM(LOW_LATENCY),
						       _ITEM(WEBCAM),
						       _ITEM(HIGH_QUALITY),
						       _ITEM(LOW_LATENCY_HIGH_QUALITY),
					       });
#undef _ITEM
		break;

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
