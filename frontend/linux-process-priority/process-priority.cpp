
/* Allows process priority to be set on Linux without root.
 * This is important because if OBS itself has elevated privileges,
 * it won't be able to capture displays and windows through PipeWire.
 * Uses pkexec to elevate privileges if the direct attempt fails.
 * Use setcap to allow the binary to adjust nice levels:
 * $ sudo setcap 'cap_sys_nice=+ep' ./obs-process-priority
 */

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <list>
#include <sstream>
#include <stdexcept>
#include <stdio.h>
#include <sys/resource.h>

using namespace std;
using namespace std::filesystem;

static string get_pkexec_path(const path root)
{
	path p = root / "pkexec";
	return exists(p) ? p.string() : "";
}

/* Tries to find pkexec from the system path. */
static string find_pkexec_path()
{
	string s = getenv("PATH");
	size_t i = 0, j;
	while ((j = s.find(':', i)) != string::npos) {
		string item = s.substr(i, j - i);
		i = j + 1;
		string p = get_pkexec_path(item);
		if (!p.empty())
			return p;
	}
	return get_pkexec_path("/usr/bin");
}

static string get_helper_command()
{
	string p = find_pkexec_path();
	return !p.empty() ? "\"" + p + "\"" : "";
}

static string HELPER_COMMAND = get_helper_command();
static int ERROR = -1;

static bool parse_int(const char *c, int &i)
{
	string s = c;
	try {
		size_t pos;
		i = stoi(s, &pos);
		if (pos == s.size())
			return true;
	} catch (invalid_argument const &ex) {
	} catch (out_of_range const &ex) {
		cerr << "Int out of range: " << s << endl;
	}
	cerr << "Invalid int: " << s << endl;
	return false;
}

static string run(const string cmd, int &res)
{
	FILE *pipe = popen(cmd.c_str(), "r");

	if (!pipe) {
		cerr << "Command failed: " << cmd << endl;
		res = ERROR;
		return "";
	}

	stringstream ss;
	char buffer[128];
	while (fgets(buffer, sizeof buffer, pipe) != NULL)
		ss << buffer;
	res = pclose(pipe);

	string s = ss.str();
	s.erase(remove(s.begin(), s.end(), '\n'), s.cend());
	return s;
}

static void set_single_priority(const int id, const int priority, const bool try_direct = true)
{
	cout << "Setting priority " << priority << " for PID: " << id << endl;
	if (try_direct) {
		int res = setpriority(PRIO_PROCESS, id, priority);
		if (!res)
			return;
	}
	// Use the helper to request elevation and use renice
	stringstream ss;
	ss << HELPER_COMMAND << " renice " << priority << " " << id;
	system(ss.str().c_str());
}

static void set_multiple_priority(const string exe, const int priority)
{
	// Get all process IDs using pidof
	stringstream ss;
	ss << "pidof \"" << exe << "\"";
	int res;
	string output = run(ss.str(), res);
	if (res) {
		cerr << "No processes found for " << exe << endl;
		return;
	}

	// Parse the response into a list of ints
	list<int> ids;
	istringstream iss(output);
	string id_str;
	while (getline(iss, id_str, ' '))
		ids.push_back(stoi(id_str));

	if (ids.size() == 1) {
		// There's only 1 ID
		set_single_priority(ids.front(), priority);
		return;
	}

	cout << "Setting priority " << priority << " for IDs:";
	for (int id : ids)
		cout << " " << id;
	cout << endl;

	// Keep track of the IDs that couldn't be changed directly
	list<int> error_ids;
	for (int id : ids) {
		int res = setpriority(PRIO_PROCESS, id, priority);
		if (res)
			error_ids.push_back(id);
	}

	if (error_ids.empty())
		return;

	// Change the remaining IDs
	
	if (error_ids.size() == 1) {
		set_single_priority(error_ids.front(), priority, false);
		return;
	}

	ss.str("");
	ss.clear();
	ss << HELPER_COMMAND << " bash -c 'for id in";
	for (int id : error_ids)
		ss << " " << id;
	ss << "; do renice " << priority << " $id; done'";
	system(ss.str().c_str());
}

int main(const int argc, const char *argv[])
{
	if (argc < 2 || argc > 3) {
		cerr << "Usage: obs-process-priority <priority> [id]" << endl;
		return ERROR;
	}

	if (HELPER_COMMAND.empty()) {
		cerr << "Could not find pkexec binary!" << endl;
		return ERROR;
	}

	int priority;
	if (!parse_int(argv[1], priority))
		return ERROR;

	path directory = canonical(argv[0]).parent_path();

	if (argc == 2) {
		stringstream ss;
		ss << "\"" << directory.string() << "/obs\"";
		set_multiple_priority(ss.str(), priority);
		return 0;
	}

	int id;
	if (!parse_int(argv[2], id))
		return ERROR;

	stringstream ss;
	ss << "readlink -f /proc/" << id << "/exe";
	int res;
	string process_path = run(ss.str(), res);

	if (res) {
		cerr << "Could not find binary path for PID " << id << endl;
		return res;
	}

	path process_directory = canonical(process_path).parent_path();

	if (process_directory != directory) {
		cerr << "Process does not appear to belong to OBS: " << process_path << endl;
		return ERROR;
	}

	set_single_priority(id, priority);
	return 0;
}
