// Port of tsc/internal/diagnostics — DiagnosticCategory, DiagnosticMessage,
// message formatting ({0} placeholders).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tsc {

// From ast/ast.h — re-declared here so this header does not need to include
// ast.h (which itself includes this header).
[[noreturn]] void tscUnreachable(const char* message);

namespace locale {
class Locale;
}

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

// Category.Name — diagnostics.go:27
inline std::string_view categoryName(DiagnosticCategory category) {
	switch (category) {
	case DiagnosticCategory::Warning:
		return "warning";
	case DiagnosticCategory::Error:
		return "error";
	case DiagnosticCategory::Suggestion:
		return "suggestion";
	case DiagnosticCategory::Message:
		return "message";
	}
	tscUnreachable("Unhandled diagnostic category");
}

using Key = std::string;

// keyToMessage — diagnostics.go:75 (most diagnostics carry a message pointer,
// so only build the lookup when a key is used).
const DiagnosticMessage* keyToMessage(std::string_view key);

// Format — diagnostics.go:129 (renamed: `tsc::format` is a namespace).
// Replaces {N} placeholders; panics on an out-of-range index like Go.
std::string formatText(std::string_view text, const std::vector<std::string>& args);

// getLocalizedMessages — diagnostics.go:101. Localized message tables are
// generated data from diagnostics/loc_generated.go; none are ported yet, so
// every lookup currently finds nothing (English).
const std::unordered_map<Key, std::string>* getLocalizedMessages(
	const locale::Locale& loc);

// Localize — diagnostics.go:82
std::string localize(const locale::Locale& loc, const DiagnosticMessage* message,
                     Key key, const std::vector<std::string>& args);

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
