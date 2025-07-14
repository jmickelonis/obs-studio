#pragma once

#include <cstdint>
#include <filesystem>
#include <regex>
#include <set>
#include <string>
#include <unistd.h>
#include <unordered_map>

using std::regex;
using std::set;
using std::smatch;
using std::string;
using std::unordered_map;
using std::filesystem::path;

namespace regex_constants = std::regex_constants;

struct FDInfo {
	uint64_t gfx;
	uint64_t compute;
	uint64_t enc;
	uint64_t enc1;
	uint64_t timestamp;
};

class GPUUsage {
public:
    string pdev = "";

    double gfx;
    double compute;
    double enc;
    double enc1;

    GPUUsage(pid_t pid);

	void update();

private:
	path driDirectory{"/dev/dri"};
	path fdDirectory;
	path fdInfoDirectory;
    regex driverPattern{R"(^drm-driver:\s+(\S+)$)", regex_constants::multiline};
    regex clientIDPattern{R"(^drm-client-id:\s+(\d+)$)", regex_constants::multiline};
    regex pdevPattern{R"(^drm-pdev:\s+(\S+)$)", regex_constants::multiline};
    regex enginePattern{R"(^drm-engine-(\S+):\s+(\d+) ns$)", regex_constants::multiline};
    smatch match;
    set<uint32_t> clientIDs;
    unordered_map<uint32_t, FDInfo> fdInfoMap;

    void parse(FDInfo &fdInfo, const string &s, uint64_t &timestamp);
};
