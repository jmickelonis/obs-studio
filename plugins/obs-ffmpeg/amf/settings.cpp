
#include "settings.hpp"

#include <mutex>
#include <unordered_map>

using std::mutex;
using std::scoped_lock;
using std::unordered_map;

namespace preset {

int getValue(CodecType codec, const char *s)
{
#define PRESET(NAME) AMF_PROPERTY(QUALITY_PRESET_ ## NAME)
#define ITEM(NAME) STR_EQ(s, NAME) ? PRESET(NAME)
	return ITEM(HIGH_QUALITY) : ITEM(QUALITY) : ITEM(SPEED) : PRESET(BALANCED);
#undef PRESET
#undef ITEM
}

} // namespace preset

namespace profile {

namespace avc {

AMF_VIDEO_ENCODER_PROFILE_ENUM getValue(const char *s)
{
#define PROFILE(NAME) AVC_PROPERTY(PROFILE_ ## NAME)
#define ITEM(NAME) STR_EQ(s, NAME) ? PROFILE(NAME)
	return ITEM(MAIN) : ITEM(BASELINE) : ITEM(CONSTRAINED_HIGH) : ITEM(CONSTRAINED_BASELINE) : PROFILE(HIGH);
#undef PROFILE
#undef ITEM
}

} // namespace avc

} // namespace profile

namespace rate_control {

bool isConstantBitrate(const char *value)
{
	return STR_EQ(value, CBR) || STR_EQ(value, HQCBR);
}

bool isQuality(const char *value)
{
	return STR_EQ(value, QVBR) || STR_EQ(value, HQCBR) || STR_EQ(value, HQVBR);
}

bool usesBitrate(const char *value)
{
	return STR_NE(value, CQP) && STR_NE(value, QVBR);
}

int getValue(CodecType codec, const char *s)
{
#define RC(NAME) AMF_PROPERTY(RATE_CONTROL_METHOD_ ## NAME)
#define ITEM(A, B) STR_EQ(s, A) ? RC(B)
	return ITEM(CQP, CONSTANT_QP)
		: ITEM(VBR, PEAK_CONSTRAINED_VBR)
		: ITEM(VBR_LAT, LATENCY_CONSTRAINED_VBR)
		: ITEM(QVBR, QUALITY_VBR)
		: ITEM(HQCBR, HIGH_QUALITY_CBR)
		: ITEM(HQVBR, HIGH_QUALITY_VBR)
		: RC(CBR);
#undef RC
#undef ITEM
}

} // namespace rate_control

namespace pa_caq {

AMF_PA_CAQ_STRENGTH_ENUM getValue(const char *s)
{
#define ITEM(NAME) STR_EQ(s, NAME) ? AMF_PA_CAQ_STRENGTH_ ## NAME
	return ITEM(HIGH) : ITEM(MEDIUM) : AMF_PA_CAQ_STRENGTH_LOW;
#undef ITEM
}

} // namespace pa_caq

namespace pa_lookahead {

int getValue(const char *s)
{
#define ITEM(NAME, VALUE) STR_EQ(s, NAME) ? VALUE
	return ITEM(LONG, 41) : ITEM(MEDIUM, 21) : ITEM(SHORT, 11) : 0;
#undef ITEM
}

} // namespace pa_lookahead

namespace pa_taq {

AMF_PA_TAQ_MODE_ENUM getValue(const char *s)
{
#define ITEM(NAME) STR_EQ(s, NAME) ? AMF_PA_TAQ_ ## NAME
	return ITEM(MODE_2) : ITEM(MODE_1) : AMF_PA_TAQ_MODE_NONE;
#undef ITEM
}

} // namespace pa_taq

void Capabilities::set(CodecType codec, AMFCaps *caps)
{
	if (!caps)
		return;

	preAnalysis = AMF_GET_BOOL_CAP(PRE_ANALYSIS);
	level = AMF_GET_INT_CAP(MAX_LEVEL);
	throughput = AMF_GET_INT_CAP(MAX_THROUGHPUT);
	requestedThroughput = AMF_GET_INT_CAP(REQUESTED_THROUGHPUT);

	switch (codec) {
	case CodecType::AVC:
		bFrames = AVC_GET_BOOL_CAP(BFRAMES);
		roi = AVC_GET_BOOL_CAP(ROI);
		break;
	case CodecType::HEVC:
		bFrames = false;
		roi = HEVC_GET_BOOL_CAP(ROI);
		break;
	case CodecType::AV1:
		bFrames = AV1_GET_BOOL_CAP(BFRAMES);
		roi = true;
		break;
	}
}

Settings::Settings(const Capabilities &capabilities, obs_data_t *data)
{
	memset(this, 0, sizeof(Settings));
	this->data = data;

	preset = obs_data_get_string(data, settings::PRESET);
	profile = obs_data_get_string(data, settings::PROFILE);

	rateControl = obs_data_get_string(data, settings::RATE_CONTROL);
	isConstantBitrate = rate_control::isConstantBitrate(rateControl);
	isQuality = rate_control::isQuality(rateControl);

	bitrateSupported = rate_control::usesBitrate(rateControl);
	if (bitrateSupported) {
		bitrate = (int)obs_data_get_int(data, settings::BITRATE) * 1000;
		useBufferSize = obs_data_get_bool(data, settings::USE_BUFFER_SIZE);
	} else {
		qp = (int)obs_data_get_int(data, settings::QP);
	}

	keyFrameInterval = (int)obs_data_get_int(data, settings::KEY_FRAME_INTERVAL);
	if (!keyFrameInterval)
		keyFrameInterval = 4;

	if (capabilities.bFrames)
		bFrames = (int)obs_data_get_int(data, settings::B_FRAMES);

	// Pre-Encode messes up quality rate control modes
	preEncodeSupported = !isQuality;

	// AQ only works with RC != CQP
	aqSupported = STR_NE(rateControl, rate_control::CQP);

	// QVBR, HQCBR, and HQVBR force Pre-Analysis
	preAnalysis = capabilities.preAnalysis && (isQuality || obs_data_get_bool(data, settings::PRE_ANALYSIS));

	// HMQB works with Pre-Analysis off
	hmqbSupported = !preAnalysis;

	if (preAnalysis) {
		// Adaptive MiniGOP works with B-Frames and Pre-Analysis on
		dynamicBFrames = bFrames > 0 && obs_data_get_bool(data, settings::DYNAMIC_B_FRAMES);

		paAQ = obs_data_get_string(data, settings::PA_AQ);
		paLookahead = obs_data_get_string(data, settings::PA_LOOKAHEAD);

		// TAQ only works with lookahead >= medium
		paTAQSupported = STR_EQ(paLookahead, pa_lookahead::MEDIUM) || STR_EQ(paLookahead, pa_lookahead::LONG);
	}
}

int Settings::getBufferSize()
{
	return useBufferSize ? (int)obs_data_get_int(data, settings::BUFFER_SIZE) * 1000 : bitrate;
}

shared_ptr<char[]> getUserOptions(obs_data_t *data)
{
	const char *s = obs_data_get_string(data, settings::OPTIONS);
	char *condensed = new char[strlen(s) + 1];
	obs_data_condense_whitespace(s, condensed);
	return shared_ptr<char[]>(condensed);
}

static mutex cacheMutex;
static unordered_map<uint32_t, unordered_map<CodecType, const Capabilities *>> capabilitiesCache;

void cacheCapabilities(uint32_t deviceID, CodecType codec, Capabilities &capabilities)
{
	scoped_lock lock(cacheMutex);

	auto it = capabilitiesCache.find(deviceID);
	if (it == capabilitiesCache.end())
		capabilitiesCache[deviceID] = unordered_map<CodecType, const Capabilities *>();

	unordered_map<CodecType, const Capabilities *> &deviceCapabilities = capabilitiesCache[deviceID];
	deviceCapabilities[codec] = new Capabilities(capabilities);
}

const Capabilities *getCapabilities(uint32_t deviceID, CodecType codec, bool load)
{
	scoped_lock lock(cacheMutex);

	auto cacheIterator = capabilitiesCache.find(deviceID);
	if (cacheIterator == capabilitiesCache.end()) {
		if (!load)
			return nullptr;
		capabilitiesCache[deviceID] = unordered_map<CodecType, const Capabilities *>();
	}

	unordered_map<CodecType, const Capabilities *> &deviceCapabilities = capabilitiesCache[deviceID];

	auto it = deviceCapabilities.find(codec);
	if (it != deviceCapabilities.end())
		return it->second;

	if (!load)
		return nullptr;

	Capabilities *capabilities = new Capabilities{};
	AMFCapsPtr caps;
	if (getCaps(deviceID, codec, &caps))
		capabilities->set(codec, caps);
	deviceCapabilities[codec] = capabilities;
	return capabilities;
}

Levels::Levels(initializer_list<Level> init) : vector(init) {}

const Level *const Levels::get(const char *name) const
{
	for (const Level &level : *this)
		if (STR_EQ(level.name, name))
			return &level;
	return nullptr;
}

const Level *const Levels::get(int value) const
{
	for (const Level &level : *this)
		if (level.value == value)
			return &level;
	return nullptr;
}

static unordered_map<CodecType, const Levels *> levelsCache;

const Levels &getLevels(CodecType codec)
{
	scoped_lock lock(cacheMutex);

	auto it = levelsCache.find(codec);
	if (it != levelsCache.end())
		return *it->second;

	Levels *levels;

	switch (codec) {

	case CodecType::AVC:
#define LEVEL(NAME, VALUE, SIZE, RATE) \
	{ #NAME, AMF_H264_LEVEL__ ## VALUE, SIZE * 256, (uint64_t)RATE * 256 }
		levels = new Levels{
			// clang-format off
			LEVEL(    1,    1,       99,       1'485  ),
			LEVEL(  1.1,  1_1,      396,       3'000  ),
			LEVEL(  1.2,  1_2,      396,       6'000  ),
			LEVEL(  1.3,  1_3,      396,      11'880  ),
			LEVEL(    2,    2,      396,      11'880  ),
			LEVEL(  2.1,  2_1,      792,      19'800  ),
			LEVEL(  2.2,  2_2,    1'620,      20'250  ),
			LEVEL(    3,    3,    1'620,      40'500  ),
			LEVEL(  3.1,  3_1,    3'600,     108'000  ),
			LEVEL(  3.2,  3_2,    5'120,     216'000  ),
			LEVEL(    4,    4,    8'192,     245'760  ),
			LEVEL(  4.1,  4_1,    8'192,     245'760  ),
			LEVEL(  4.2,  4_2,    8'704,     522'240  ),
			LEVEL(    5,    5,   22'080,     589'824  ),
			LEVEL(  5.1,  5_1,   36'864,     983'040  ),
			LEVEL(  5.2,  5_2,   36'864,   2'073'600  ),
			LEVEL(    6,    6,  139'264,   4'177'920  ),
			LEVEL(  6.1,  6_1,  139'264,   8'355'840  ),
			LEVEL(  6.2,  6_2,  139'264,  16'711'680  ),
			// clang-format on
		};
#undef LEVEL
		break;

	case CodecType::HEVC:
#define LEVEL(NAME, VALUE, SIZE, RATE) \
	{ #NAME, AMF_LEVEL_ ## VALUE, SIZE, RATE }
		levels = new Levels{
			// clang-format off
			LEVEL(    1,    1,      36'864,        552'960  ),
			LEVEL(    2,    2,     122'880,      3'686'400  ),
			LEVEL(  2.1,  2_1,     245'760,      7'372'800  ),
			LEVEL(    3,    3,     552'960,     16'588'800  ),
			LEVEL(  3.1,  3_1,     983'040,     33'177'600  ),
			LEVEL(    4,    4,   2'228'224,     66'846'720  ),
			LEVEL(  4.1,  4_1,   2'228'224,    133'693'440  ),
			LEVEL(    5,    5,   8'912'896,    267'386'880  ),
			LEVEL(  5.1,  5_1,   8'912'896,    534'773'760  ),
			LEVEL(  5.2,  5_2,   8'912'896,  1'069'547'520  ),
			LEVEL(    6,    6,  35'651'584,  1'069'547'520  ),
			LEVEL(  6.1,  6_1,  35'651'584,  2'139'095'040  ),
			LEVEL(  6.2,  6_2,  35'651'584,  4'278'190'080  ),
			// clang-format on
		};
#undef LEVEL
		break;

	case CodecType::AV1:
#define LEVEL(MAJOR, MINOR, SIZE, RATE) \
	{ #MAJOR "." #MINOR, AMF_VIDEO_ENCODER_AV1_LEVEL_ ## MAJOR ## _ ## MINOR, SIZE, RATE }
		levels = new Levels{
			// clang-format off
			LEVEL(  2, 0,     147'456,      5'529'600  ),
			LEVEL(  2, 1,     278'784,     10'454'400  ),
			LEVEL(  3, 0,     665'856,     24'969'600  ),
			LEVEL(  3, 1,   1'065'024,     39'938'400  ),
			LEVEL(  4, 0,   2'359'296,     77'856'768  ),
			LEVEL(  4, 1,   2'359'296,    155'713'536  ),
			LEVEL(  5, 0,   8'912'896,    273'715'200  ),
			LEVEL(  5, 1,   8'912'896,    547'430'400  ),
			LEVEL(  5, 2,   8'912'896,  1'094'860'800  ),
			LEVEL(  5, 3,   8'912'896,  1'176'502'272  ),
			LEVEL(  6, 0,  35'651'584,  1'176'502'272  ),
			LEVEL(  6, 1,  35'651'584,  2'189'721'600  ),
			LEVEL(  6, 2,  35'651'584,  4'379'443'200  ),
			LEVEL(  6, 3,  35'651'584,  4'706'009'088  ),
			// clang-format on
		};
#undef LEVEL
		break;
	}

	levelsCache[codec] = levels;
	return *levels;
}
