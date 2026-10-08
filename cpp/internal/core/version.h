// version.go — port of tsc/internal/core/version.go.
#pragma once

#include <string>
#include <string_view>

namespace tsc {

// This is a var in Go so it can be overridden by ldflags.
inline std::string_view version() { return "7.1.0-dev"; }

inline std::string_view versionMajorMinor() {
	static const std::string value = [] {
		auto v = version();
		bool seenMajor = false;
		size_t i = 0;
		for (; i < v.size(); i++) {
			if (v[i] == '.') {
				if (seenMajor) break;
				seenMajor = true;
			}
		}
		if (i == v.size()) {
			fprintf(stderr, "tsc internal error: invalid version string\n");
			abort();
		}
		return std::string{v.substr(0, i)};
	}();
	return value;
}

}  // namespace tsc
