// Port of tsc/internal/sourcemap/util.go
#include "internal/sourcemap/sourcemap.h"
#include "internal/stringutil/stringutil.h"

namespace tsc::sourcemap {
namespace {

// unicode.IsSpace — Unicode White_Space property exactly as Go's
// unicode.IsSpace reports it.
bool unicodeIsSpace(char32_t r) {
	switch (r) {
	case U'\t': case U'\n': case U'\v': case U'\f': case U'\r': case U' ':
	case 0x85:  // NEL
	case 0xA0:  // NBSP
	case 0x1680:
	case 0x2028:
	case 0x2029:
	case 0x202F:
	case 0x205F:
	case 0x3000:
		return true;
	}
	return r >= 0x2000 && r <= 0x200A;
}

// strings.TrimLeftFunc — trims leading runes while pred holds.
std::string_view trimLeftFunc(std::string_view s, bool (*pred)(char32_t)) {
	size_t i = 0;
	while (i < s.size()) {
		int width;
		char32_t r = decodeUtf8Rune(s.substr(i), &width);
		if (!pred(r)) {
			break;
		}
		i += static_cast<size_t>(width);
	}
	return s.substr(i);
}

// strings.TrimRightFunc — trims trailing runes while pred holds.
std::string_view trimRightFunc(std::string_view s, bool (*pred)(char32_t)) {
	while (!s.empty()) {
		int width;
		char32_t r = decodeLastUtf8Rune(s, &width);
		if (!pred(r)) {
			break;
		}
		s = s.substr(0, s.size() - static_cast<size_t>(width));
	}
	return s;
}

} // namespace

// TryGetSourceMappingURL — util.go:11. Tries to find the sourceMappingURL
// comment at the end of a file.
std::string TryGetSourceMappingURL(ECMALineInfo* lineInfo) {
	if (lineInfo != nullptr) {
		for (int index = lineInfo->LineCount() - 1; index >= 0; index--) {
			std::string_view line = lineInfo->LineText(index);
			line = trimLeftFunc(line, unicodeIsSpace);
			line = trimRightFunc(line, isLineBreak);
			if (line.empty()) {
				continue;
			}
			if (line.size() < 4 || !line.starts_with("//") ||
			    (line[2] != '#' && line[2] != '@') || line[3] != ' ') {
				break;
			}
			std::string_view url = line.substr(4);
			if (url.starts_with("sourceMappingURL=")) {
				url = url.substr(std::string_view("sourceMappingURL=").size());
				return std::string(trimRightFunc(url, unicodeIsSpace));
			}
		}
	}
	return "";
}

} // namespace tsc::sourcemap
