#include "platform-x11-process-priority.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <unistd.h>
#include <sys/resource.h>

using namespace std;
using namespace std::filesystem;

static string get_kdesu_path(const path root)
{
	path lib = root / "lib";
	path p = lib / "x86_64-linux-gnu/libexec/kf6/kdesu";
	if (exists(p))
		return p.string();
	p = lib / "libexec/kf6/kdesu";
	if (exists(p))
		return p.string();
	return "";
}

/* Tries to find kdesu from the system path. */
static string find_kdesu_path()
{
	string s = getenv("PATH");
	size_t i = 0, j;
	while ((j = s.find(':', i)) != string::npos) {
		string item = s.substr(i, j - i);
		i = j + 1;
		path root = path(item).parent_path();
		string p = get_kdesu_path(root);
		if (p != "")
			return p;
	}
	return get_kdesu_path("/usr");
}

static string KDESU = "";

/* Allows process priority to be set on Linux without root.
 * Uses kdesu to elevate privileges if the direct attempt fails.
 * Use setcap to allow the binary to adjust nice levels:
 * $ sudo setcap 'cap_sys_nice=ep' obs
 */
void setProcessPriority(int priority)
{
	int id = getpid();

	// First, try directly
	cout << "Setting priority " << priority << " for PID: " << id << endl;
	int res = setpriority(PRIO_PROCESS, id, priority);
	if (!res)
		return;

	// Failed (can only lower priority without root, not raise)
	// Use kdesu to request elevation and use renice
	if (KDESU.empty())
		KDESU = find_kdesu_path();
	ostringstream ss;
	ss << KDESU << " -- renice " << priority << " " << id;
	system(ss.str().c_str());
}
