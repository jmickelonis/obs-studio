
#include <regex>
#include <string>
#include "string.h"
#include "obs-data.h"

using namespace std;

static regex spaces("\\s+");
static regex strip("^\\s+(.*)\\s+$");

extern "C" void obs_data_condense_whitespace(const char *in, char *out);

void obs_data_condense_whitespace(const char *in, char *out)
{
	string s = regex_replace(in, spaces, " ");
	s = regex_replace(s, strip, "$1");
	strcpy(out, s.c_str());
}
