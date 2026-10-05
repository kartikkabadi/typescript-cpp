// Port of tsc/internal/diagnostics — DiagnosticCategory, DiagnosticMessage,
// message formatting ({0} placeholders).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tsc {

enum class DiagnosticCategory : int32_t {
	Warning = 0,
	Error = 1,
	Suggestion = 2,
	Message = 3,
};

struct DiagnosticMessage {
	int32_t code;
	DiagnosticCategory category;
	const char* key;
	const char* text;
	bool reportsUnnecessary = false;
	bool elidedInCompatibilityPyramid = false;
	bool reportsDeprecated = false;

	bool ReportsUnnecessary() const { return reportsUnnecessary; }
	bool ElidedInCompatibilityPyramid() const { return elidedInCompatibilityPyramid; }
	bool ReportsDeprecated() const { return reportsDeprecated; }
};

// Substitute {0}, {1}, ... in the message text with the given args.
inline std::string formatDiagnosticMessage(const DiagnosticMessage& msg,
                                           const std::vector<std::string>& args) {
	std::string out;
	std::string_view t(msg.text);
	for (size_t i = 0; i < t.size();) {
		if (t[i] == '{' && i + 2 < t.size() && t[i + 2] == '}' &&
		    t[i + 1] >= '0' && t[i + 1] <= '9') {
			size_t arg = t[i + 1] - '0';
			if (arg < args.size()) {
				out += args[arg];
			} else {
				out += t.substr(i, 3);
			}
			i += 3;
		} else {
			out += t[i];
			i += 1;
		}
	}
	return out;
}

struct SourceFile;
struct Diagnostic;

}  // namespace tsc

#include "internal/diagnostics/messages_generated.h"
