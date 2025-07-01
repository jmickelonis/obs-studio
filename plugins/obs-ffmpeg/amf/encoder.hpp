#pragma once

#include "settings.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include <obs.h>

using namespace amf;
using namespace std;

#define __OBS_AMF_SHOW_PROPERTIES true
#ifdef OBS_AMF_DISABLE_PROPERTIES
#define __OBS_AMF_SHOW_PROPERTIES false
#endif

#ifdef _WIN32
#define MEMORY_TYPE AMF_MEMORY_DX11
#else
#define MEMORY_TYPE AMF_MEMORY_VULKAN
#endif

struct VideoInfo {

	AMF_SURFACE_FORMAT format;
	bool fullRangeColor;
	AMF_COLOR_BIT_DEPTH_ENUM colorBitDepth;
	AMF_COLOR_PRIMARIES_ENUM colorPrimaries;
	AMF_VIDEO_CONVERTER_COLOR_PROFILE_ENUM colorProfile;
	AMF_COLOR_TRANSFER_CHARACTERISTIC_ENUM colorTransferCharacteristic;
	AMFRate frameRate;

	VideoInfo(CodecType codec, obs_encoder_t *encoder);

	template<typename T> T multiplyByFrameRate(T value) const;
};

struct ROI {

	const uint32_t mbSize;
	const uint32_t mbWidth;
	const uint32_t mbHeight;
	const uint32_t pitch;
	const AMFSurfacePtr surface;
	const uint32_t bufferSize;
	uint32_t *const buffer;
	uint32_t increment = 0;

	void update(obs_encoder_roi *data);
};

class Encoder {

public:
	const CodecType codec;
	obs_encoder_t *const encoder;
	const string name;
	const uint32_t width;
	const uint32_t height;

	Encoder(CodecType codec, obs_encoder_t *encoder, VideoInfo &videoInfo, string name);
	virtual ~Encoder();

	template<typename... Args> void log(int level, const char *format, Args... args)
	{
		stringstream ss;
		ss << "[" << name << ": '" << obs_encoder_get_name(encoder) << "'] " << format;
		string s = ss.str();
		blog(level, s.c_str(), args...);
	}

	template<typename... Args> void error(const char *format, Args... args) { log(LOG_ERROR, format, args...); }
	template<typename... Args> void warn(const char *format, Args... args) { log(LOG_WARNING, format, args...); }
	template<typename... Args> void info(const char *format, Args... args) { log(LOG_INFO, format, args...); }
	template<typename... Args> void debug(const char *format, Args... args) { log(LOG_DEBUG, format, args...); }

	void initialize(obs_data_t *data);
	void updateSettings(obs_data_t *data);
	bool getExtraData(uint8_t **data, size_t *size);

protected:
	const VideoInfo videoInfo;

	AMFContextPtr amfContext;
	AMFContext1Ptr amfContext1;

#ifdef _WIN32
	virtual void *getDX11Device() { return nullptr; }
#else
	virtual void *getVulkanDevice() { return nullptr; }
#endif
	void submit(AMFSurfacePtr &surface, encoder_packet *packet, bool *receivedPacket);
	int64_t timestampToAMF(int64_t ts);
	int64_t timestampToOBS(int64_t ts);

	virtual void terminate();
	virtual void terminateEncoder();

#ifdef __linux__
	virtual void onReceivePacket(const int64_t &ts) {}
#endif
	virtual void onReinitialize(bool full) {}

private:
	const wchar_t *outputDataTypeProperty;

	Capabilities capabilities;
	int64_t dtsOffset = 0;
	deque<AMFDataPtr> queryQueue;
	unique_ptr<ROI> roi;
	deque<AMFSurfacePtr> submitQueue;
	thread worker;
	condition_variable workerCondition;
	exception_ptr workerException;
	mutex workerMutex;
	volatile bool workerRunning = false;

	AMFComponentPtr amfEncoder;
	AMFBufferPtr extraData;
	AMFBufferPtr packetData;

#if __OBS_AMF_SHOW_PROPERTIES
	bool showProperties;
#endif

	template<typename T> bool getProperty(const wchar_t *name, T *value);
	template<typename T> void setProperty(const wchar_t *name, const T &value);

	void createEncoder(obs_data_t *data, bool init);
	void initializeAVC();
	void initializeHEVC();
	void initializeAV1();

	void update(Settings &settings, const char *opts, bool init);
	void updateAVC(Settings &settings);
	void updateHEVC(Settings &settings);
	void updateAV1(Settings &settings);
	int getLevel(const Levels &levels, obs_data_t *data);
	bool setPreAnalysis(Settings &settings);
	void applyOpts(const char *s);

	void workerStart();
	void workerRun();
	void workerStop();

	bool createROI();
	void updateROI(AMFSurfacePtr &surface);
	void receivePacket(AMFDataPtr &data, encoder_packet *packet);
};
