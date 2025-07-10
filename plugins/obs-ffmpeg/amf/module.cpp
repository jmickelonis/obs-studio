
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
uint64_t amfVersion = 0;

bool AdapterCapabilities::supports(CodecType codec)
{
	switch (codec) {
	case CodecType::AVC:
	default:
		return avc;
	case CodecType::HEVC:
		return hevc;
	case CodecType::AV1:
		return av1;
	}
}

uint32_t getDeviceID(CodecType codec, uint32_t requestedID)
{
	if (!requestedID) {
		// If no device in settings, just use the first available
		for (AdapterCapabilities &info : caps)
			if (info.supports(codec))
				return info.deviceID;
		return 0;
	}

	uint32_t id = 0;

	// Try to match the requested ID
	// If not found, it'll still end up using the first available
	for (auto it = caps.rbegin(); it != caps.rend(); ++it) {
		if (!it->supports(codec))
			continue;
		id = it->deviceID;
		if (id == requestedID)
			break;
	}

	return id;
}

#define LIST_STRING(TEXT, VALUE) obs_property_list_add_string(prop, TEXT, VALUE)
#define LIST_STRING_CAPITALIZED(VALUE) \
	{ \
		char *text = strdup(VALUE); \
		text[0] = toupper(text[0]); \
		obs_property_list_add_string(prop, text, VALUE); \
		free(text); \
	}

obs_properties_t *createProperties(void *, void *typeData)
{
	EncoderType *type = (EncoderType *)typeData;
	CodecType codec = type->codec;

	obs_properties_t *props = obs_properties_create();
	obs_property_t *prop;

#define BOOL(NAME, ...) prop = obs_properties_add_bool(props, settings::NAME, __VA_ARGS__)
#define INT(NAME, ...) prop = obs_properties_add_int(props, settings::NAME, __VA_ARGS__)
#define LIST(NAME, ...) prop = obs_properties_add_list(props, settings::NAME, __VA_ARGS__, OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING)
#define setModifiedCallback() obs_property_set_modified_callback2(prop, onPropertyModified, typeData)

	prop = obs_properties_add_list(props, settings::DEVICE, "Device", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	for (AdapterCapabilities &adapterCaps : caps)
		if (adapterCaps.supports(codec))
			obs_property_list_add_int(prop, adapterCaps.device, adapterCaps.deviceID);
	setModifiedCallback();

	LIST(RATE_CONTROL, obs_module_text("RateControl"));
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
#define ITEM(NAME) LIST_STRING_CAPITALIZED(profile::NAME)
		ITEM(HIGH);
		ITEM(MAIN);
		ITEM(BASELINE);
#undef ITEM
	}

	LIST(LEVEL, obs_module_text("Level"));

	INT(B_FRAMES, obs_module_text("BFrames"), 0, 5, 1);
	setModifiedCallback();

	BOOL(LOW_LATENCY, "Low Latency");
	BOOL(PRE_ENCODE, "Rate Control Pre-Analysis");
	BOOL(ADAPTIVE_QUANTIZATION, codec == CodecType::AV1 ? "Content Adaptive Quantization (CAQ)"
							    : "Variance-Based Adaptive Quantization (VBAQ)");
	BOOL(HIGH_MOTION_QUALITY_BOOST, "High-Motion Quality Boost");

	BOOL(PRE_ANALYSIS, "Pre-Analysis");
	setModifiedCallback();

	BOOL(DYNAMIC_B_FRAMES, "Dynamic B-Frames");

	LIST(PA_LOOKAHEAD, "Lookahead");
#define ITEM(NAME) LIST_STRING_CAPITALIZED(pa_lookahead::NAME)
	ITEM(NONE);
	ITEM(SHORT);
	ITEM(MEDIUM);
	ITEM(LONG);
#undef ITEM
	setModifiedCallback();

	LIST(PA_AQ, "Adaptive Quantization");
	setModifiedCallback();

	LIST(PA_CAQ, "CAQ Strength");
#define ITEM(NAME) LIST_STRING_CAPITALIZED(pa_caq::NAME)
	ITEM(LOW);
	ITEM(MEDIUM);
	ITEM(HIGH);
#undef ITEM

	LIST(PA_TAQ, "TAQ Mode");
	LIST_STRING("1", pa_taq::MODE_1);
	LIST_STRING("2", pa_taq::MODE_2);

	prop = obs_properties_add_text(props, settings::OPTIONS, obs_module_text("AMFOpts"), OBS_TEXT_MULTILINE);
	obs_property_set_long_description(prop, obs_module_text("AMFOpts.ToolTip"));

#undef BOOL
#undef INT
#undef LIST
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
	STRING(LEVEL, settings::AUTO);
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

	using namespace settings;

	uint32_t device = (uint32_t)obs_data_get_int(data, settings::DEVICE);
	const Capabilities &capabilities = *getCapabilities(device, codec);

	const char *changedProperty = obs_property_name(prop);
	bool updated = false;

	auto setVisible = [&](const char *name, bool visible) {
		obs_property_t *prop = obs_properties_get(props, name);
		if (visible == obs_property_visible(prop))
			return;
		obs_property_set_visible(prop, visible);
		updated = true;
	};

	const bool &preAnalysisCap = capabilities.preAnalysis;

	if (STR_EQ(changedProperty, settings::DEVICE)) {
		setVisible(B_FRAMES, capabilities.bFrames);

		int expectedCount = preAnalysisCap ? 7 : 4;
		prop = obs_properties_get(props, RATE_CONTROL);
		if (obs_property_list_item_count(prop) != expectedCount) {
			// Rebuild the Rate Control list
			obs_property_list_clear(prop);
#define ITEM(NAME) LIST_STRING(rate_control::NAME, rate_control::NAME)
			ITEM(CBR);
			ITEM(CQP);
			ITEM(VBR);
			ITEM(VBR_LAT);
			if (preAnalysisCap) {
				ITEM(QVBR);
				ITEM(HQCBR);
				ITEM(HQVBR);
			}
#undef ITEM
			updated = true;
		}

		// Figure out how many levels we support
		int levelCount = 0;
		const Levels &levels = getLevels(codec);
		const amf_int64 &maxLevel = capabilities.level;
		for (const Level &level : levels) {
			if (maxLevel && level.value > maxLevel)
				break;
			levelCount++;
		}

		prop = obs_properties_get(props, LEVEL);
		if (obs_property_list_item_count(prop) != levelCount + 1) {
			// Rebuild the Level list
			obs_property_list_clear(prop);
			LIST_STRING_CAPITALIZED(AUTO);
			for (int i = 0; i < levelCount; i++) {
				const Level &level = levels.at(i);
				LIST_STRING(level.name, level.name);
			}
			updated = true;
		}
	}

	Settings settings(capabilities, data);

	bool &showBitrate = settings.bitrateSupported;
	setVisible(BITRATE, showBitrate);
	setVisible(USE_BUFFER_SIZE, showBitrate);
	setVisible(BUFFER_SIZE, showBitrate && settings.useBufferSize);
	setVisible(QP, !showBitrate);

	bool &preAnalysis = settings.preAnalysis;
	setVisible(PRE_ENCODE, settings.preEncodeSupported);
	setVisible(ADAPTIVE_QUANTIZATION, settings.aqSupported && !preAnalysis);
	setVisible(HIGH_MOTION_QUALITY_BOOST, settings.hmqbSupported);
	setVisible(PRE_ANALYSIS, preAnalysisCap && !settings.isQuality);
	setVisible(DYNAMIC_B_FRAMES, settings.bFrames > 0 && preAnalysis);
	setVisible(PA_LOOKAHEAD, preAnalysis);
	setVisible(PA_AQ, preAnalysis);

	const char *aq = settings.paAQ;

	if (preAnalysis) {
		bool showVBAQ = codec != CodecType::AV1 && settings.aqSupported;
		bool showTAQ = settings.paTAQSupported;

		// Figure out how many AQ values we support
		int expectedCount = 2;
		if (showVBAQ)
			expectedCount++;
		if (showTAQ)
			expectedCount++;

		prop = obs_properties_get(props, PA_AQ);
		if (obs_property_list_item_count(prop) != expectedCount) {
			// Rebuild the AQ list
			obs_property_list_clear(prop);
			LIST_STRING_CAPITALIZED(pa_aq::NONE);
			if (showVBAQ)
				LIST_STRING("Variance-Based (VBAQ)", pa_aq::VBAQ);
			LIST_STRING("Content (CAQ)", pa_aq::CAQ);
			if (showTAQ)
				LIST_STRING("Temporal (TAQ)", pa_aq::TAQ);

			if (!showVBAQ && STR_EQ(aq, pa_aq::VBAQ) || !showTAQ && STR_EQ(aq, pa_aq::TAQ)) {
				// Change to CAQ when the selected item disappears
				aq = pa_aq::CAQ;
				obs_data_set_string(data, PA_AQ, aq);
			}

			updated = true;
		}
	}

	setVisible(PA_CAQ, preAnalysis && STR_EQ(aq, pa_aq::CAQ));
	setVisible(PA_TAQ, preAnalysis && STR_EQ(aq, pa_aq::TAQ));

	return updated;
}

#undef LIST_STRING
#undef LIST_STRING_CAPITALIZED

static void logEncoderError(const string name, const char *func)
{
	wstringstream ss;

	try {
		rethrow_exception(current_exception());
	} catch (const char *s) {
		ss << s;
	} catch (const string &s) {
		ss << s.c_str();
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
		VideoInfo videoInfo(encoder, codec);

		allowFallback = true;

		if (obs_encoder_scaling_enabled(encoder) && !obs_encoder_gpu_scaling_enabled(encoder))
			throw "Encoder scaling is active";

		if (videoInfo.format == AMF_SURFACE_BGRA)
			throw "Cannot use textures with BGRA format";

		uint32_t deviceID = getDeviceID(codec, (uint32_t)obs_data_get_int(data, settings::DEVICE));

		// Workaround alert:
		// For some reason, on Linux, using multiple texture encoders at once doesn't work
		// until a Vulkan AMFComponent has been created and destroyed.
		// This does exactly that, plus caches the capability information.
		getCapabilities(deviceID, codec);

		unique_ptr<TextureEncoder> enc = make_unique<TextureEncoder>(encoder, codec, videoInfo, name, deviceID);
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
		VideoInfo videoInfo(encoder, codec);
		uint32_t deviceID = getDeviceID(codec, (uint32_t)obs_data_get_int(data, settings::DEVICE));
		unique_ptr<FallbackEncoder> enc =
			make_unique<FallbackEncoder>(encoder, codec, videoInfo, name, deviceID);
		enc->initialize(data);
		return enc.release();

	} catch (...) {
		logEncoderError(name, __func__);
	}

	return nullptr;
}

#ifdef _WIN32
bool encodeTexture(void *encData, uint32_t handle, int64_t pts, uint64_t lockKey, uint64_t *nextKey,
		   encoder_packet *packet, bool *receivedPacket)
{
	TextureEncoder *enc = (TextureEncoder *)encData;
	try {
		enc->encode(handle, pts, lockKey, nextKey, packet, receivedPacket);
		return true;
	} catch (...) {
		logEncoderError(enc, __func__);
		return false;
	}
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
		size_t length = s.length();
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

	obs_encoder_info info{};
	info.id = getID("texture");
	info.type = OBS_ENCODER_VIDEO;
	info.codec = codec;
	info.get_name = getName;
	info.create = createTextureEncoder;
	info.destroy = destroy;
	info.update = updateSettings;
	info.get_extra_data = getExtraData;
	info.type_data = getTypeData();
	info.free_type_data = free;
	info.caps = OBS_ENCODER_CAP_PASS_TEXTURE | CAPS;
	info.get_defaults2 = setPropertyDefaults;
	info.get_properties2 = createProperties;
#ifdef _WIN32
	info.encode_texture = encodeTexture;
#else
	info.encode_texture2 = encodeTexture2;
#endif
	obs_register_encoder(&info);

	info.id = getID("fallback");
	info.create = createFallbackEncoder;
	info.encode = encodeFallback;
	info.type_data = getTypeData();
	info.caps = OBS_ENCODER_CAP_INTERNAL | CAPS;
#ifdef _WIN32
	info.encode_texture = nullptr,
#else
	info.encode_texture2 = nullptr,
#endif
	obs_register_encoder(&info);
}

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
		static constexpr int bufferSize = 2048;

		ss << testPath;

		// os_process_pipe_create() wasn't working at one point,
		// but pipe() seems to work just fine
#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif
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

		bool anyAVC = false;
		bool anyHEVC = false;
		bool anyAV1 = false;

		size_t adapterCount = config_num_sections(config);
		caps.reserve(adapterCount);
		for (size_t i = 0; i < adapterCount; i++) {
			string sectionString = to_string(i);
			const char *section = sectionString.c_str();

			if (!config_get_bool(config, section, "is_amd"))
				continue;

#define SUPPORTS(NAME) config_get_bool(config, section, "supports_" #NAME)
			bool avc = SUPPORTS(avc);
			bool hevc = SUPPORTS(hevc);
			bool av1 = SUPPORTS(av1);
			if (!(avc || hevc || av1))
				continue;
#undef SUPPORTS

			const char *device = strdup(config_get_string(config, section, "device"));
			uint32_t deviceID = (uint32_t)config_get_int(config, section, "device_id");
			AdapterCapabilities info{device, deviceID, avc, hevc, av1};
			caps.push_back(info);

			anyAVC |= avc;
			anyHEVC |= hevc;
			anyAV1 |= av1;
		}

		if (caps.empty())
			throw "Neither AVC, HEVC, nor AV1 are supported by any devices";

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

		AMFQueryVersion_Fn getVersion = (AMFQueryVersion_Fn)os_dlsym(module, AMF_QUERY_VERSION_FUNCTION_NAME);
		if (!getVersion)
			throw "Failed to get AMFQueryVersion address";

		AMF_CHECK(getVersion(&amfVersion), "AMFQueryVersion failed");

		blog(LOG_INFO, "Loaded AMF v%d.%d.%d.%d", AMF_GET_MAJOR_VERSION(amfVersion),
		     AMF_GET_MINOR_VERSION(amfVersion), AMF_GET_SUBMINOR_VERSION(amfVersion),
		     AMF_GET_BUILD_VERSION(amfVersion));

		// Register encoders
		if (anyAVC) {
			EncoderType type{};
			type.name = "AMD HW H.264 (AVC)";
			type.id = "h264";
			type.codec = CodecType::AVC;
			registerEncoder("h264", type);
		}
#if ENABLE_HEVC
		if (anyHEVC) {
			EncoderType type{};
			type.name = "AMD HW H.265 (HEVC)";
			type.id = "h265";
			type.codec = CodecType::HEVC;
			registerEncoder("hevc", type);
		}
#endif
		if (anyAV1) {
			EncoderType type = {};
			type.name = "AMD HW AV1";
			type.id = "av1";
			type.codec = CodecType::AV1;
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
