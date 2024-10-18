
/* Allows process priority to be set on Linux without root.
 * Uses kdesu to elevate privileges if the direct attempt fails.
 * Use setcap to allow the binary to adjust nice levels:
 * $ sudo setcap 'cap_sys_nice=ep' ./obs-process-priority
 */

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <stdio.h>
#include <sys/resource.h>

namespace fs = std::filesystem;


static bool parse_int(char* c, int& out) {
    std::string s = c;
    try {
        std::size_t pos;
        int i = std::stoi(s, &pos);
        if (pos < s.size()) {
            std::cerr << "Invalid int: " << s << std::endl;
        }
        else {
            out = i;
            return true;
        }
    } catch (std::invalid_argument const &ex) {
        std::cerr << "Invalid int: " << s << std::endl;
    } catch (std::out_of_range const &ex) {
        std::cerr << "Int out of range: " << s << std::endl;
    }
    return false;
}

static int ERROR = -1;

int main(int argc, char *argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: obs-process-priority <pid> <priority>" << std::endl;
        return ERROR;
    }

    int pid;
    if (!parse_int(argv[1], pid))
        return ERROR;

    int priority;
    if (!parse_int(argv[2], priority))
        return ERROR;

    std::ostringstream ss;
    ss << "readlink -f /proc/" << pid << "/exe";
    std::string cmd = ss.str();

    char buffer[128];
    FILE* pipe = popen(cmd.c_str(), "r");

    if (!pipe) {
        std::cerr << "Command failed: " << cmd << std::endl;
        return ERROR;
    }

    ss.str("");
    ss.clear();
    while (fgets(buffer, sizeof buffer, pipe) != NULL)
        ss << buffer;

    std::string process_path = ss.str();
    process_path.erase(std::remove(process_path.begin(), process_path.end(), '\n'), process_path.cend());

    if (process_path == "") {
        std::cerr << "Could not find binary path for PID " << pid << std::endl;
        return ERROR;
    }

    fs::path directory = fs::canonical(argv[0]).parent_path();
    fs::path process_directory = fs::canonical(process_path).parent_path();

    if (process_directory != directory) {
        std::cerr << "Process does not appear to belong to OBS: " << process_path << std::endl;
        return ERROR;
    }

    std::cout << "Setting priority for PID " << pid << " to " << priority << std::endl;

    int res = setpriority(PRIO_PROCESS, pid, priority);
    if (res) {
        // Attempt failed
		// Use kdesu to request elevation and use renice
        ss.str("");
        ss.clear();
		ss << "/opt/kde/lib/x86_64-linux-gnu/libexec/kf6/kdesu -- renice -n " << priority << " -p " << pid;
		std::string s = ss.str();
		res = system(s.c_str());
    }
    return res;
}
