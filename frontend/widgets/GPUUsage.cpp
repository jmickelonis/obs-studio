#include "GPUUsage.hpp"

#include <fstream>
#include <streambuf>

#include <util/platform.h>

using std::ifstream;
using std::istreambuf_iterator;
namespace fs = std::filesystem;
using fs::directory_iterator;

static inline string getFileContents(const string &path)
{
	ifstream file(path);
	if (!file.is_open())
		return "";
	return string(istreambuf_iterator<char>(file), istreambuf_iterator<char>());
}

GPUUsage::GPUUsage(pid_t pid)
{
	path rootDirectory{"/proc"};
	path procDirectory{rootDirectory / std::to_string(pid)};
	fdDirectory = procDirectory / "fd";
	fdInfoDirectory = procDirectory / "fdinfo";

	string fd = "";

	// Check only the first reported DRI device
	for (const auto &child : directory_iterator(fdDirectory)) {
		const path &file = child.path();
		try {
			path link = fs::read_symlink(file);
			if (link.parent_path() == driDirectory) {
				fd = file.filename();
				break;
			}
		} catch (...) {
		}
	}

	if (fd.empty())
		return; // Couldn't find one

	path fdInfoFile = fdInfoDirectory / fd;
	const string s = getFileContents(fdInfoFile.string());

	if (!std::regex_search(s, match, driverPattern) || match[1] != "amdgpu")
		// We currently only support AMDGPU
		return;

	if (!std::regex_search(s, match, pdevPattern))
		return;

	// We'll only use information matching this pdev
	pdev = match[1];
}

void GPUUsage::update()
{
	clientIDs.clear();

	uint64_t timestamp = os_gettime_ns();
	gfx = 0;
	compute = 0;
	enc = 0;
    enc1 = 0;

	for (const auto &child : directory_iterator(fdDirectory)) {
		const path &file = child.path();

		// Ensure it's a link to a DRI device
		try {
			path link = fs::read_symlink(file);
			if (link.parent_path() != driDirectory)
				continue;
		} catch (...) {
			continue;
		}

		path fdInfoFile = fdInfoDirectory / file.filename();
		const string s = getFileContents(fdInfoFile.string());

		if (!std::regex_search(s, match, pdevPattern) || match[1] != pdev)
			// Not our pdev
			continue;

		if (!std::regex_search(s, match, clientIDPattern))
			// No client-id
			continue;

		// Don't count client information more than once
		uint32_t clientID = std::stoul(match[1]);
		if (clientIDs.find(clientID) != clientIDs.end())
			continue;
		clientIDs.insert(clientID);

		auto it = fdInfoMap.find(clientID);
		FDInfo *fdInfoPtr;
		if (it != fdInfoMap.end()) {
			fdInfoPtr = &it->second;
		} else {
			fdInfoMap[clientID] = {};
			fdInfoPtr = &fdInfoMap[clientID];
		}

        parse(*fdInfoPtr, s, timestamp);
	}
}

inline void GPUUsage::parse(FDInfo &fdInfo, const string &s, uint64_t &timestamp)
{
	FDInfo previousValues = fdInfo;

	auto begin = match.suffix().first;
	auto end = s.end();
	while (std::regex_search(begin, end, match, enginePattern)) {
		begin = match.suffix().first;
		const string &engineType = match[1];

		if (engineType == "gfx") {
			uint64_t value = std::stoull(match[2]);
			if (value > previousValues.gfx)
				fdInfo.gfx = value;
		} else if (engineType == "compute") {
			uint64_t value = std::stoull(match[2]);
			if (value > previousValues.compute)
				fdInfo.compute = value;
		} else if (engineType == "enc") {
			uint64_t value = std::stoull(match[2]);
			if (value > previousValues.enc)
				fdInfo.enc = value;
		} else if (engineType == "enc_1") {
			uint64_t value = std::stoull(match[2]);
			if (value > previousValues.enc1)
				fdInfo.enc1 = value;
		}
	}

	fdInfo.timestamp = timestamp;
	if (!previousValues.timestamp)
		return;

	double duration = fdInfo.timestamp - previousValues.timestamp;

	if (previousValues.gfx && fdInfo.gfx > previousValues.gfx)
		gfx += (fdInfo.gfx - previousValues.gfx) / duration;

	if (previousValues.compute && fdInfo.compute > previousValues.compute)
		compute += (fdInfo.compute - previousValues.compute) / duration;

	if (previousValues.enc && fdInfo.enc > previousValues.enc)
		enc += (fdInfo.enc - previousValues.enc) / duration;

	if (previousValues.enc1 && fdInfo.enc1 > previousValues.enc1)
		enc1 += (fdInfo.enc1 - previousValues.enc1) / duration;
}
