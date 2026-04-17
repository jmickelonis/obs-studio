#pragma once

/* Discover AMF encoder capabilities and properties.
 */

#include "amf.hpp"

#include <map>
#include <sstream>
#include <string>
#include <vector>

using std::map;
using std::string;
using std::stringstream;
using std::vector;

enum class PropertyType {
	// Primitives
	BOOL,
	INT,
	UINT,
	// General
	ACCEL,
	COLOR_BIT_DEPTH,
	COLOR_PRIMARIES,
	COLOR_PROFILE,
	COLOR_TRANSFER_CHARACTERISTIC,
	MEMORY_TYPE,
	RATE,
	RATIO,
	SIZE,
	// Pre-Analysis
	PA_ACTIVITY_TYPE,
	PA_CAQ_STRENGTH,
	PA_HIGH_MOTION_QUALITY_BOOST_MODE,
	PA_PAQ_MODE,
	PA_SCENE_CHANGE_DETECTION_SENSITIVITY,
	PA_STATIC_SCENE_DETECTION_SENSITIVITY,
	PA_TAQ_MODE,
	// AVC
	AVC_CODING,
	AVC_H264_LEVEL,
	AVC_LTR_MODE,
	AVC_OUTPUT_MODE,
	AVC_PICTURE_TRANSFER_MODE,
	AVC_PREENCODE_MODE,
	AVC_PROFILE,
	AVC_QUALITY_PRESET,
	AVC_RATE_CONTROL_METHOD,
	AVC_SCANTYPE,
	AVC_USAGE,
	// HEVC
	HEVC_HEADER_INSERTION_MODE,
	HEVC_LEVEL,
	HEVC_LTR_MODE,
	HEVC_NOMINAL_RANGE,
	HEVC_OUTPUT_MODE,
	HEVC_PICTURE_TRANSFER_MODE,
	HEVC_PROFILE,
	HEVC_QUALITY_PRESET,
	HEVC_RATE_CONTROL_METHOD,
	HEVC_TIER,
	HEVC_USAGE,
	// AV1
	AV1_ALIGNMENT_MODE,
	AV1_AQ_MODE,
	AV1_CDEF_MODE,
	AV1_CDF_FRAME_END_UPDATE_MODE,
	AV1_ENCODING_LATENCY_MODE,
	AV1_HEADER_INSERTION_MODE,
	AV1_INTRA_REFRESH_MODE,
	AV1_LEVEL,
	AV1_LTR_MODE,
	AV1_OUTPUT_MODE,
	AV1_PROFILE,
	AV1_QUALITY_PRESET,
	AV1_RATE_CONTROL_METHOD,
	AV1_SWITCH_FRAME_INSERTION_MODE,
	AV1_USAGE,
};

struct PropertyNameComparator {
	bool operator()(const wchar_t *a, const wchar_t *b) const;
};

using PropertyTypes = map<const wchar_t *, PropertyType, PropertyNameComparator>;
using PropertyValues = map<const wchar_t *, string, PropertyNameComparator>;
using CategorizedPropertyTypes = map<string, PropertyTypes>;

struct CodecProperties {
	const CodecType codec;
	const vector<const char *> categories;
	const CategorizedPropertyTypes properties;
	const PropertyTypes capabilities;
};

const CodecProperties &getCodecProperties(CodecType codec);
const PropertyTypes &getPreAnalysisProperties();

void printProperties(stringstream &ss, AMFPropertyStorage *storage, const PropertyTypes &properties,
		     unsigned int indent = 0);
void printProperties(stringstream &ss, AMFPropertyStorage *storage, const char *category,
		     const PropertyTypes &properties, unsigned int indent = 0);
void printProperties(stringstream &ss, AMFPropertyStorage *storage, const CodecProperties &properties,
		     unsigned int indent = 0);

PropertyValues getPropertyValues(AMFPropertyStorage *storage, const CodecProperties &properties);
void printChangedPropertyValues(stringstream &ss, PropertyValues &from, PropertyValues &to, unsigned int indent = 1);
