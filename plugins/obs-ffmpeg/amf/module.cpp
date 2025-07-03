
#include "module.hpp"
#include "fallback.hpp"

#ifdef _WIN32
#include "windows.hpp"
#else
#include "linux.hpp"
#endif

#include <util/platform.h>
#include <util/util.hpp>

static vector<AdapterCapabilities> caps;

AMFFactory *amfFactory = nullptr;
AMFTrace *amfTrace = nullptr;

bool adapterSupports(CodecType codec)
{
	obs_video_info ovi;
	obs_get_video_info(&ovi);

	try {
		AdapterCapabilities &info = caps.at(ovi.adapter);

		switch (codec) {
		case CodecType::AVC:
			return info.avc;
		case CodecType::HEVC:
			return info.hevc;
		case CodecType::AV1:
			return info.av1;
		}
	} catch (out_of_range) {
	}

	return false;
}

obs_properties_t *createProperties(void *, void *typeData)
{
	EncoderType *type = (EncoderType *)typeData;
	CodecType codec = type->codec;
	const Capabilities &caps = *getCapabilities(codec);

	obs_properties_t *props = obs_properties_create();
	obs_property_t *prop;

	// Adds a list item, using its capitalized value as text
	auto addCapitalizedItem = [&](const char *value) {
		char *text = strdup(value);
		text[0] = toupper(text[0]);
		obs_property_list_add_string(prop, text, value);
		free(text);
	};

#define BOOL(NAME, ...) prop = obs_properties_add_bool(props, settings::NAME, __VA_ARGS__)
#define INT(NAME, ...) prop = obs_properties_add_int(props, settings::NAME, __VA_ARGS__)
#define LIST(NAME, ...) prop = obs_properties_add_list(props, settings::NAME, __VA_ARGS__, OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING)
#define LIST_STRING(TEXT, VALUE) obs_property_list_add_string(prop, TEXT, VALUE)
#define setModifiedCallback() obs_property_set_modified_callback2(prop, onPropertyModified, typeData)

	LIST(RATE_CONTROL, obs_module_text("RateControl"));
#define ITEM(NAME) LIST_STRING(rate_control::NAME, rate_control::NAME)
	ITEM(CBR);
	ITEM(CQP);
	ITEM(VBR);
	ITEM(VBR_LAT);
	if (caps.preAnalysis) {
		ITEM(QVBR);
		ITEM(HQCBR);
		ITEM(HQVBR);
	}
#undef ITEM
	setModifiedCallback();

	INT(BITRATE, obs_module_text("Bitrate"), 50, 100000, 50);
	obs_property_int_set_suffix(prop, " Kbps");

	obs_module_t *module = obs_get_module("obs-x264");
	const char *text = "Use Custom Buffer Size";
	obs_module_get_locale_string(module, "CustomBufsize", &text);
	BOOL(USE_BUFFER_SIZE, text);
	setModifiedCallback();
	text = "Buffer Size";
	obs_module_get_locale_string(module, "BufferSize", &text);
	INT(BUFFER_SIZE, text, 0, 100000, 1);
	obs_property_int_set_suffix(prop, " Kbps");

	INT(QP, "QP", 0, codec == CodecType::AV1 ? 63 : 51, 1);

	INT(KEY_FRAME_INTERVAL, obs_module_text("KeyframeIntervalSec"), 0, 10, 1);
	obs_property_int_set_suffix(prop, " s");

	LIST(PRESET, obs_module_text("Preset"));

	auto getPresetText = [](const char *const name) {
		string s = string("AMF.Preset.") + name;
		return obs_module_text(s.c_str());
	};
#define ITEM(NAME) LIST_STRING(getPresetText(preset::NAME), preset::NAME)
	ITEM(HIGH_QUALITY);
	ITEM(QUALITY);
	ITEM(BALANCED);
	ITEM(SPEED);
#undef ITEM

	if (codec == CodecType::AVC) {
		LIST(PROFILE, obs_module_text("Profile"));
#define ITEM(NAME) addCapitalizedItem(profile::NAME)
		ITEM(HIGH);
		ITEM(MAIN);
		ITEM(BASELINE);
#undef ITEM
	}

	LIST(LEVEL, obs_module_text("Level"));
	addCapitalizedItem("auto");
	for (const Level &level : getLevels(codec)) {
		if (level.value > caps.level)
			break;
		LIST_STRING(level.name, level.name);
	}

	if (caps.bFrames) {
		INT(B_FRAMES, obs_module_text("BFrames"), 0, 5, 1);
		if (caps.preAnalysis)
			setModifiedCallback();
	}

	BOOL(LOW_LATENCY, "Low Latency");
	BOOL(PRE_ENCODE, "Rate Control Pre-Analysis");
	BOOL(ADAPTIVE_QUANTIZATION, codec == CodecType::AV1 ? "Content Adaptive Quantization (CAQ)"
							    : "Variance-Based Adaptive Quantization (VBAQ)");
	BOOL(HIGH_MOTION_QUALITY_BOOST, "High-Motion Quality Boost");

	if (caps.preAnalysis) {
		BOOL(PRE_ANALYSIS, "Pre-Analysis");
		setModifiedCallback();

		if (caps.bFrames)
			BOOL(DYNAMIC_B_FRAMES, "Dynamic B-Frames");

		LIST(PA_LOOKAHEAD, "Lookahead");
#define ITEM(NAME) addCapitalizedItem(pa_lookahead::NAME)
		ITEM(NONE);
		ITEM(SHORT);
		ITEM(MEDIUM);
		ITEM(LONG);
#undef ITEM
		setModifiedCallback();

		LIST(PA_AQ, "Adaptive Quantization");
		addCapitalizedItem("none");
		if (codec != CodecType::AV1)
			LIST_STRING("Variance-Based (VBAQ)", pa_aq::VBAQ);
		LIST_STRING("Content (CAQ)", pa_aq::CAQ);
		LIST_STRING("Temporal (TAQ)", pa_aq::TAQ);
		setModifiedCallback();

		LIST(PA_CAQ, "CAQ Strength");
#define ITEM(NAME) addCapitalizedItem(pa_caq::NAME)
		ITEM(LOW);
		ITEM(MEDIUM);
		ITEM(HIGH);
#undef ITEM

		LIST(PA_TAQ, "TAQ Mode");
		LIST_STRING("1", pa_taq::MODE_1);
		LIST_STRING("2", pa_taq::MODE_2);
	}

	prop = obs_properties_add_text(props, settings::OPTIONS, obs_module_text("AMFOpts"), OBS_TEXT_MULTILINE);
	obs_property_set_long_description(prop, obs_module_text("AMFOpts.ToolTip"));

#undef BOOL
#undef INT
#undef LIST
#undef LIST_STRING
#undef setModifiedCallback
	return props;
}

void setPropertyDefaults(obs_data_t *data, void *)
{
#define SET(TYPE, NAME, VALUE) obs_data_set_default_ ## TYPE(data, settings::NAME, VALUE)
#define BOOL(NAME, VALUE) SET(bool, NAME, VALUE)
#define INT(NAME, VALUE) SET(int, NAME, VALUE)
#define STRING(NAME, VALUE) SET(string, NAME, VALUE)
	BOOL(ADAPTIVE_QUANTIZATION, true);
	BOOL(DYNAMIC_B_FRAMES, true);
	BOOL(PRE_ENCODE, true);
	INT(B_FRAMES, 2);
	INT(BITRATE, 2500);
	INT(BUFFER_SIZE, 2500);
	INT(QP, 20);
	STRING(LEVEL, "auto");
	STRING(PRESET, preset::BALANCED);
	STRING(PROFILE, profile::HIGH);
	STRING(RATE_CONTROL, rate_control::CBR);
	STRING(PA_AQ, pa_aq::CAQ);
	STRING(PA_CAQ, pa_caq::MEDIUM);
	STRING(PA_LOOKAHEAD, pa_lookahead::MEDIUM);
	STRING(PA_TAQ, pa_taq::MODE_1);
#undef SET
#undef BOOL
#undef INT
#undef STRING
}

bool onPropertyModified(void *typeData, obs_properties_t *props, obs_property_t *prop, obs_data_t *data)
{
	EncoderType *type = (EncoderType *)typeData;
	CodecType codec = type->codec;

	const Capabilities &capabilities = *getCapabilities(codec);
	Settings settings(capabilities, data);

	bool updated = false;

	auto setVisible = [&](const char *name, bool visible) {
		obs_property_t *prop = obs_properties_get(props, name);
		if (visible == obs_property_visible(prop))
			return;
		obs_property_set_visible(prop, visible);
		updated = true;
	};

	using namespace settings;

	bool showBitrate = settings.bitrateSupported;
	setVisible(BITRATE, showBitrate);
	setVisible(USE_BUFFER_SIZE, showBitrate);
	setVisible(BUFFER_SIZE, showBitrate && settings.useBufferSize);
	setVisible(QP, !showBitrate);

	if (capabilities.preAnalysis) {
		setVisible(PRE_ENCODE, settings.preEncodeSupported);
		setVisible(ADAPTIVE_QUANTIZATION, settings.aqSupported && !settings.preAnalysis);
		setVisible(HIGH_MOTION_QUALITY_BOOST, settings.hmqbSupported);
		setVisible(PRE_ANALYSIS, !settings.isQuality);
		setVisible(DYNAMIC_B_FRAMES, settings.bFrames > 0 && settings.preAnalysis);
		setVisible(PA_LOOKAHEAD, settings.preAnalysis);
		setVisible(PA_AQ, settings.preAnalysis);

		const char *aq = settings.paAQ;

		if (settings.preAnalysis) {
			obs_property_t *prop = obs_properties_get(props, PA_AQ);
			bool showVBAQ = codec != CodecType::AV1 && settings.aqSupported;
			bool showTAQ = settings.paTAQSupported;
			int previousCount = obs_property_list_item_count(prop);

			obs_property_list_clear(prop);
			obs_property_list_add_string(prop, "None", pa_aq::NONE);
			if (showVBAQ)
				obs_property_list_add_string(prop, "Variance-Based (VBAQ)", pa_aq::VBAQ);
			obs_property_list_add_string(prop, "Content (CAQ)", pa_aq::CAQ);
			if (showTAQ)
				obs_property_list_add_string(prop, "Temporal (TAQ)", pa_aq::TAQ);

			if (obs_property_list_item_count(prop) != previousCount) {
				updated = true;

				if (!showVBAQ && STR_EQ(aq, pa_aq::VBAQ) || !showTAQ && STR_EQ(aq, pa_aq::TAQ)) {
					// Change to CAQ when the selected item disappears
					aq = pa_aq::CAQ;
					obs_data_set_string(data, PA_AQ, aq);
				}
			}
		}

		setVisible(PA_CAQ, settings.preAnalysis && STR_EQ(aq, pa_aq::CAQ));
		setVisible(PA_TAQ, settings.preAnalysis && STR_EQ(aq, pa_aq::TAQ));
	}

	return updated;
}

static void logEncoderError(const string name, const char *func)
{
	wstringstream ss;

	try {
		rethrow_exception(current_exception());
	} catch (const char *s) {
		ss << s;
	} catch (const AMFException &e) {
		ss << e.message << " (" << e.resultText << ")";
	} catch (const exception &e) {
		ss << e.what();
	} catch (...) {
		ss << "Unknown error";
	}

	wstring s = ss.str();
	blog(LOG_ERROR, "[%s] [%s::%s] %ls", name.c_str(), __FILE_NAME__, func, s.c_str());
}

static void logEncoderError(Encoder *enc, const char *func)
{
	logEncoderError(enc->name, func);
}

const char *getName(void *typeData)
{
	EncoderType *type = (EncoderType *)typeData;
	return type->name;
}

void *createTextureEncoder(obs_data_t *data, obs_encoder_t *encoder)
{
	EncoderType *type = (EncoderType *)obs_encoder_get_type_data(encoder);

	stringstream ss;
	ss << "texture-amf-" << type->id;
	string name = ss.str();

	bool allowFallback = false;

	try {
		CodecType codec = type->codec;

		if (!adapterSupports(codec))
			throw "Wrong adapter";

		VideoInfo videoInfo(codec, encoder);

		allowFallback = true;

		if (obs_encoder_scaling_enabled(encoder) && !obs_encoder_gpu_scaling_enabled(encoder))
			throw "Encoder scaling is active";

		if (videoInfo.format == AMF_SURFACE_BGRA)
			throw "Cannot use textures with BGRA format";

		// Workaround alert:
		// For some reason, on Linux, using multiple texture encoders at once doesn't work
		// until a Vulkan AMFComponent has been created and destroyed.
		// This does exactly that, plus caches the capability information.
		getCapabilities(codec);

		unique_ptr<TextureEncoder> enc = make_unique<TextureEncoder>(codec, encoder, videoInfo, name);
		enc->initialize(data);
		return enc.release();

	} catch (...) {
		logEncoderError(name, __func__);
	}

	if (!allowFallback)
		return nullptr;

	ss.str("");
	ss << type->id << "_fallback_amf";
	return obs_encoder_create_rerouted(encoder, ss.str().c_str());
}

void *createFallbackEncoder(obs_data_t *data, obs_encoder_t *encoder)
{
	EncoderType *type = (EncoderType *)obs_encoder_get_type_data(encoder);

	stringstream ss;
	ss << "fallback-amf-" << type->id;
	string name = ss.str();

	try {
		CodecType codec = type->codec;
		if (!adapterSupports(codec))
			throw "Wrong adapter";
		VideoInfo videoInfo(codec, encoder);
		unique_ptr<FallbackEncoder> enc = make_unique<FallbackEncoder>(codec, encoder, videoInfo, name);
		enc->initialize(data);
		return enc.release();

	} catch (...) {
		logEncoderError(name, __func__);
	}

	return nullptr;
}

#ifdef _WIN32
bool encodeTexture(void *data, uint32_t handle, int64_t pts, uint64_t lock_key, uint64_t *next_key,
		   encoder_packet *packet, bool *received_packet)
try {
	amf_texencode *enc = (amf_texencode *)data;
	return enc->encode(handle, pts, lock_key, next_key, packet, received_packet);

} catch (const char *err) {
	amf_texencode *enc = (amf_texencode *)data;
	error("%s: %s", __FUNCTION__, err);
	return false;

} catch (const amf_error &err) {
	amf_texencode *enc = (amf_texencode *)data;
	error("%s: %s: %ls", __FUNCTION__, err.str, err.res_text);
	*received_packet = false;
	return false;

} catch (const HRError &err) {
	amf_texencode *enc = (amf_texencode *)data;
	error("%s: %s: 0x%lX", __FUNCTION__, err.str, err.hr);
	*received_packet = false;
	return false;
}
#else
bool encodeTexture2(void *encData, encoder_texture *texture, int64_t pts, uint64_t, uint64_t *, encoder_packet *packet,
		    bool *receivedPacket)
{
	TextureEncoder *enc = (TextureEncoder *)encData;
	try {
		return enc->encode(texture, pts, packet, receivedPacket);
	} catch (...) {
		logEncoderError(enc, __func__);
		return false;
	}
}
#endif

bool encodeFallback(void *encData, struct encoder_frame *frame, struct encoder_packet *packet, bool *receivedPacket)
{
	FallbackEncoder *enc = (FallbackEncoder *)encData;
	try {
		enc->encode(frame, packet, receivedPacket);
		return true;
	} catch (...) {
		logEncoderError(enc, __func__);
		return false;
	}
}

bool getExtraData(void *encData, uint8_t **header, size_t *size)
{
	Encoder *enc = (Encoder *)encData;
	return enc->getExtraData(header, size);
}

bool updateSettings(void *encData, obs_data_t *data)
{
	Encoder *enc = (Encoder *)encData;
	try {
		enc->updateSettings(data);
		return true;

	} catch (...) {
		logEncoderError(enc, __func__);
		return false;
	}
}

void destroy(void *encData)
{
	Encoder *enc = (Encoder *)encData;
	delete enc;
}

static void registerEncoder(const char *codec, EncoderType &type)
{
	static uint32_t CAPS = OBS_ENCODER_CAP_DYN_BITRATE | OBS_ENCODER_CAP_ROI;

	auto getID = [&](const char *t) {
		stringstream ss;
		ss << type.id << "_" << t << "_amf";
		string s = ss.str();
		int length = s.length();
		char *id = (char *)malloc(length + 1);
		strncpy(id, s.c_str(), length);
		id[length] = '\0';
		return id;
	};

	auto getTypeData = [&] {
		EncoderType *data = (EncoderType *)malloc(sizeof(EncoderType));
		*data = type;
		return data;
	};

	obs_encoder_info info = {
		.type = OBS_ENCODER_VIDEO,
		.caps = OBS_ENCODER_CAP_PASS_TEXTURE | CAPS,
		.id = getID("texture"),
		.codec = codec,
		.type_data = getTypeData(),
		.free_type_data = free,
		.get_name = getName,
		.create = createTextureEncoder,
		.destroy = destroy,
#ifdef _WIN32
		.encode_texture = encodeTexture,
#else
		.encode_texture2 = encodeTexture2,
#endif
		.get_defaults2 = setPropertyDefaults,
		.get_extra_data = getExtraData,
		.get_properties2 = createProperties,
		.update = updateSettings,
	};
	obs_register_encoder(&info);

	info.caps = OBS_ENCODER_CAP_INTERNAL | CAPS;
	info.id = getID("fallback");
	info.type_data = getTypeData();
	info.create = createFallbackEncoder;
	info.encode = encodeFallback;
#ifdef _WIN32
	info.encode_texture = nullptr,
#else
	info.encode_texture2 = nullptr,
#endif
	obs_register_encoder(&info);
}

#ifdef _WIN32
static bool enum_luids(void *param, uint32_t idx, uint64_t luid)
{
	stringstream &cmd = *(stringstream *)param;
	cmd << " " << hex << luid;
	UNUSED_PARAMETER(idx);
	return true;
}
#endif

extern "C" void amf_load(void)
{
	void *module = nullptr;

	try {

		// Check if the library is present before running the more expensive test process
#ifdef _WIN32
		// Load it as data so it can't crash us
		HMODULE moduleTest = LoadLibraryExW(AMF_DLL_NAME, nullptr, LOAD_LIBRARY_AS_DATAFILE);
		if (!moduleTest)
			throw "AMF library not found";
		FreeLibrary(moduleTest);
#else
		module = os_dlopen(AMF_DLL_NAMEA);
		if (!module)
			throw "AMF library not found";
#endif

		// Check for supported codecs
#ifdef _WIN32
#define OBS_AMF_TEST "obs-amf-test.exe"
#else
#define OBS_AMF_TEST "obs-amf-test"
#endif
		BPtr<char> testPath = os_get_executable_path_ptr(OBS_AMF_TEST);
		stringstream ss;
		static int bufferSize = 2048;

#ifdef _WIN32
		ss << '"';
		ss << testPath;
		ss << '"';
		enum_graphics_device_luids(enum_luids, &ss);
#else
		ss << testPath;
#endif

		// os_process_pipe_create() wasn't working at one point,
		// but pipe() seems to work just fine
		FILE *pipe = popen(ss.str().c_str(), "r");

		if (!pipe)
			throw "Failed to launch the AMF test process";

		ss.str("");
		char buffer[bufferSize];
		while (fgets(buffer, bufferSize, pipe) != nullptr)
			ss << buffer;
		pclose(pipe);

		if (!ss.tellp())
			throw "The AMF test subprocess crashed; not loading AMF";

		ConfigFile config;
		if (config.OpenString(ss.str().c_str()) != 0)
			throw "Failed to open AMF config string";

		const char *error = config_get_string(config, "error", "string");
		if (error)
			throw error;

		bool avc = false;
		bool hevc = false;
		bool av1 = false;

		size_t adapterCount = config_num_sections(config);
		caps.reserve(adapterCount);
		for (size_t i = 0; i < adapterCount; i++) {
			string sectionString = to_string(i);
			const char *section = sectionString.c_str();

#define SUPPORTS(NAME) config_get_bool(config, section, "supports_" #NAME)
			AdapterCapabilities info{SUPPORTS(avc), SUPPORTS(hevc), SUPPORTS(av1)};
#undef SUPPORTS
			caps.push_back(info);

			avc |= info.avc;
			hevc |= info.hevc;
			av1 |= info.av1;
		}

		if (!avc && !hevc && !av1) {
			caps.clear();
			throw "Neither AVC, HEVC, nor AV1 are supported by any devices";
		}

		// Initialize AMF

		if (!module) {
			module = os_dlopen(AMF_DLL_NAMEA);
			if (!module)
				throw "AMF library failed to load";
		}

		AMFInit_Fn init = (AMFInit_Fn)os_dlsym(module, AMF_INIT_FUNCTION_NAME);
		if (!init)
			throw "Failed to get AMFInit address";

		AMF_CHECK(init(AMF_FULL_VERSION, &amfFactory), "AMFInit failed");
		AMF_CHECK(amfFactory->GetTrace(&amfTrace), "GetTrace failed");

#ifndef DEBUG_AMF_STUFF
		amfTrace->EnableWriter(AMF_TRACE_WRITER_DEBUG_OUTPUT, false);
		amfTrace->EnableWriter(AMF_TRACE_WRITER_CONSOLE, false);
#endif

		// Register encoders
		if (avc) {
			EncoderType type = {
				.name = "AMD HW H.264 (AVC)",
				.id = "h264",
				.codec = CodecType::AVC,
			};
			registerEncoder("h264", type);
		}
#if ENABLE_HEVC
		if (hevc) {
			EncoderType type = {
				.name = "AMD HW H.265 (HEVC)",
				.id = "h265",
				.codec = CodecType::HEVC,
			};
			registerEncoder("hevc", type);
		}
#endif
		if (av1) {
			EncoderType type = {
				.name = "AMD HW AV1",
				.id = "av1",
				.codec = CodecType::AV1,
			};
			registerEncoder("av1", type);
		}

		// Everything's kosher!
		return;

	} catch (const char *s) {
		// Probably not using AMD
		blog(LOG_DEBUG, "[%s::%s] %s", __FILE_NAME__, __func__, s);

	} catch (const AMFException &e) {
		// Probably have AMD at this point
		blog(LOG_ERROR, "[%s::%s] %s (%ls)", __FILE_NAME__, __func__, e.message, e.resultText);
	}

	if (module)
		os_dlclose(module);
}

extern "C" void amf_unload(void)
{
	if (amfTrace)
		amfTrace->TraceFlush();
}
