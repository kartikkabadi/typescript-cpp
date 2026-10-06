// Port of tsc/internal/testutil/testutil.go.
#include "internal/testutil/testutil.h"

#include <cstdlib>
#include <mutex>

namespace tsc::testutil {

namespace {

// strconv.ParseBool — Go accepts 1,t,T,TRUE,true,True,0,f,F,FALSE,false,False.
std::pair<bool, bool> parseBool(const std::string& v) {
	if (v == "1" || v == "t" || v == "T" || v == "true" || v == "TRUE" ||
	    v == "True") {
		return {true, true};
	}
	if (v == "0" || v == "f" || v == "F" || v == "false" || v == "FALSE" ||
	    v == "False") {
		return {false, true};
	}
	return {false, false};
}

// testProgramIsSingleThreaded — testutil.go:37 (sync.OnceValue). There is
// no C++ race detector, so `!race.Enabled` is true.
bool computeTestProgramIsSingleThreaded() {
	if (const char* v = std::getenv("TS_TEST_PROGRAM_SINGLE_THREADED");
	    v != nullptr && *v != '\0') {
		if (auto [b, ok] = parseBool(v); ok) {
			return b;
		}
	}
	return true;
}

}  // namespace

// TestProgramIsSingleThreaded — testutil.go:44.
bool TestProgramIsSingleThreaded() {
	static const bool value = computeTestProgramIsSingleThreaded();
	return value;
}

}  // namespace tsc::testutil
