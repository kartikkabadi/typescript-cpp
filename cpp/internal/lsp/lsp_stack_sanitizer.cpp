// stack_sanitizer.go — port of tsc/internal/lsp/stack_sanitizer.go.
#include "internal/lsp/lsp.h"

#include <sstream>

#include "internal/gostd/regexp.h"

namespace tsc::lsp {

// genericSecretKeywordRegex — stack_sanitizer.go:16. Some error trackers
// apply a generic regex to scrub secrets from telemetry, which redacts
// things like "Signature" in TypeScript stack frames, making the frame
// unreadable. Replace the keyword with the keyword plus a marker we strip on
// the dashboard, preserving the parts the regex actually emits (`(`, `[`,
// `.`, `|`); reverse by removing the marker (replace `X_X` with the empty
// string) on the dashboard.
static const gostd::regexp::Regexp& genericSecretKeywordRegex() {
	static const gostd::regexp::Regexp re(
		"(?i)(key|token|signature|sig|pwd)([([.|])");
	return re;
}

// defeatGenericSecretRegex — stack_sanitizer.go:19.
std::string defeatGenericSecretRegex(const std::string& s) {
	return genericSecretKeywordRegex().ReplaceAllString(s, "${1}X_X${2}");
}

// sanitizeStackTrace — stack_sanitizer.go:23.
std::string sanitizeStackTrace(const std::string& stack) {
	// TODO: should we just look for the first '(' and
	// just strip everything before the prior newline?
	auto startIndex = stack.find("runtime/debug.Stack()");
	if (startIndex == std::string::npos) {
		return "";
	}
	auto rest = std::string_view(stack).substr(startIndex);

	std::string result;

	// strings.Lines — iterate lines (without their trailing newlines; a
	// final newline does not produce a trailing empty line).
	size_t lineNum = 0;
	size_t pos = 0;
	while (pos < rest.size()) {
		size_t nl = rest.find('\n', pos);
		std::string_view line = nl == std::string_view::npos
		                            ? rest.substr(pos)
		                            : rest.substr(pos, nl - pos);

		if (lineNum > 0) {
			result += '\n';
		}
		lineNum++;

		size_t i = 0;
		// Skip whitespace
		while (i < line.size()) {
			if (line[i] != ' ' && line[i] != '\t') {
				break;
			}
			i++;
		}

		result += line.substr(0, i);

		line = line.substr(i);

		auto ourModuleIndex = line.find("TypeScript/tsc/");
		if (ourModuleIndex != std::string_view::npos) {
			line = line.substr(ourModuleIndex);
			writeSanitizedModuleOrPath(std::string(line), &result);
		} else {
			result += "(REDACTED FRAME)";
		}
		if (nl == std::string_view::npos) {
			break;
		}
		pos = nl + 1;
	}

	return defeatGenericSecretRegex(result);
}

// writeSanitizedModuleOrPath — stack_sanitizer.go:64.
void writeSanitizedModuleOrPath(const std::string& lineIn,
                                std::string* result) {
	// We don't expect things like \r, but it doesn't hurt to trim just in
	// case.
	std::string_view line = lineIn;
	// strings.TrimSpace
	{
		size_t b = line.find_first_not_of(" \t\n\v\f\r");
		size_t e = line.find_last_not_of(" \t\n\v\f\r");
		line = b == std::string_view::npos ? "" : line.substr(b, e - b + 1);
	}

	if (auto plusHex = line.find(" +0x"); plusHex != std::string_view::npos) {
		line = line.substr(0, plusHex);
	} else if (auto inGoroutine = line.rfind(" in goroutine ");
	           inGoroutine != std::string_view::npos) {
		line = line.substr(0, inGoroutine);
	}

	// strings.Split(line, "/")
	size_t segmentIndex = 0;
	size_t spos = 0;
	while (true) {
		size_t slash = line.find('/', spos);
		std::string_view segment = slash == std::string_view::npos
		                               ? line.substr(spos)
		                               : line.substr(spos, slash - spos);
		if (segmentIndex > 0) {
			*result += "|>";
		}
		segmentIndex++;

		// See if the string ends with ), and strip out all the arguments.
		if (!segment.empty() && segment.back() == ')') {
			auto openParenIndex = segment.rfind('(');
			if (openParenIndex == std::string_view::npos) {
				// Closing parenthesis, but no opening - bail out.
				*result += "???";
			} else {
				segment = segment.substr(0, openParenIndex);
				*result += segment;
				*result += "()";
			}
		} else {
			*result += segment;
		}

		if (slash == std::string_view::npos) {
			break;
		}
		spos = slash + 1;
	}
}

} // namespace tsc::lsp
