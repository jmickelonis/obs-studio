
#pragma once

#include "amf.hpp"

#include <memory>
#include <vector>

#include <obs.h>

using std::initializer_list;
using std::shared_ptr;
using std::vector;

#define STR_EQ(A, B) !strcmp(A, B)
#define STR_NE(A, B) strcmp(A, B)

namespace settings {
const char *const ADAPTIVE_QUANTIZATION = "aq";
const char *const B_FRAMES = "bf";
const char *const BITRATE = "bitrate";
const char *const BUFFER_SIZE = "buffer_size";
const char *const DYNAMIC_B_FRAMES = "dynamic_bf";
const char *const HIGH_MOTION_QUALITY_BOOST = "hmqb";
const char *const KEY_FRAME_INTERVAL = "keyint_sec";
const char *const LEVEL = "level";
const char *const LOW_LATENCY = "low_latency";
const char *const PRE_ANALYSIS = "pre_analysis";
const char *const PRE_ENCODE = "pre_encode";
const char *const PRESET = "preset";
const char *const PROFILE = "profile";
const char *const QP = "cqp";
const char *const RATE_CONTROL = "rate_control";
const char *const USE_BUFFER_SIZE = "use_bufsize";
const char *const PA_AQ = "pa_aq";
const char *const PA_CAQ = "pa_caq";
const char *const PA_LOOKAHEAD = "pa_lookahead";
const char *const PA_TAQ = "pa_taq";
const char *const OPTIONS = "ffmpeg_opts";
} // namespace settings

namespace preset {

const char *const HIGH_QUALITY = "highQuality";
const char *const QUALITY = "quality";
const char *const BALANCED = "balanced";
const char *const SPEED = "speed";

int getValue(CodecType codec, const char *s);

} // namespace preset

namespace profile {

const char *const HIGH = "high";
const char *const MAIN = "main";
const char *const BASELINE = "baseline";
const char *const CONSTRAINED_HIGH = "constrained_high";
const char *const CONSTRAINED_BASELINE = "constrained_baseline";

namespace avc {

AMF_VIDEO_ENCODER_PROFILE_ENUM getValue(const char *s);

}

} // namespace profile

namespace rate_control {

#define ITEM(NAME) const char* const NAME = #NAME
ITEM(CBR);
ITEM(CQP);
ITEM(VBR);
ITEM(VBR_LAT);
ITEM(QVBR);
ITEM(HQCBR);
ITEM(HQVBR);
#undef ITEM

bool isConstantBitrate(const char *value);
bool isQuality(const char *value);
bool usesBitrate(const char *value);

int getValue(CodecType codec, const char *s);

} // namespace rate_control

namespace pa_aq {
const char *const NONE = "none";
const char *const VBAQ = "vbaq";
const char *const CAQ = "caq";
const char *const TAQ = "taq";
} // namespace pa_aq

namespace pa_caq {

const char *const LOW = "low";
const char *const MEDIUM = "medium";
const char *const HIGH = "high";

AMF_PA_CAQ_STRENGTH_ENUM getValue(const char *s);

} // namespace pa_caq

namespace pa_lookahead {

const char *const NONE = "none";
const char *const SHORT = "short";
const char *const MEDIUM = "medium";
const char *const LONG = "long";

int getValue(const char *s);

} // namespace pa_lookahead

namespace pa_taq {

const char *const MODE_1 = "mode1";
const char *const MODE_2 = "mode2";

AMF_PA_TAQ_MODE_ENUM getValue(const char *s);

} // namespace pa_taq

struct Capabilities {

	bool bFrames : 1;
	bool preAnalysis : 1;
	bool roi : 1;
	amf_int64 level;
	amf_int64 throughput;
	amf_int64 requestedThroughput;

	void set(CodecType codec, AMFCaps *caps);
};

struct Settings {

	obs_data_t *data;

	int bFrames;
	int bitrate;
	bool dynamicBFrames;
	int keyFrameInterval;
	const char *preset;
	const char *profile;
	int qp;
	const char *rateControl;
	bool useBufferSize;

	bool preAnalysis;
	const char *paAQ;
	const char *paLookahead;

	bool isConstantBitrate;
	bool isQuality;

	bool aqSupported;
	bool bitrateSupported;
	bool hmqbSupported;
	bool preEncodeSupported;
	bool paTAQSupported;

	Settings(const Capabilities &capabilities, obs_data_t *data);

	int getBufferSize();
};

shared_ptr<char[]> getUserOptions(obs_data_t *data);

void cacheCapabilities(CodecType codec, Capabilities &capabilities);
const Capabilities *getCapabilities(CodecType codec, bool load = true);

struct Level {
	const char *const name;
	const int value;
	const uint32_t size;
	const uint64_t rate;
};

class Levels : public vector<Level> {
public:
	Levels(initializer_list<Level> init);

	const Level *const get(const char *name) const;
	const Level *const get(int value) const;
};

const Levels &getLevels(CodecType codec);
