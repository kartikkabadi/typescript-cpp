// === dep decls — owned by lsp (ls/lsconv) ===
// Faithful port of linemap.go + FileNameToDocumentURI (converters.go); see
// lsconv.h.
#include "internal/ls/lsconv/lsconv.h"

#include <algorithm>

#include "internal/bundled/bundled.h"
#include "internal/gostd/gostd.h"
#include "internal/tspath/tspath.h"

namespace tsc::lsconv {

// DiagnosticToLSPPull (converters.go:459) — dep-stub: port lands with the
// lsconv slice.
lsproto::Diagnostic* DiagnosticToLSPPull(
    gostd::Context ctx, Converters* converters, Diagnostic* diagnostic,
    bool reportStyleChecksAsWarnings) {
	TSC_UNREACHABLE("DiagnosticToLSPPull — lsconv slice");
}

// ComputeLSPLineStarts (linemap.go:19) — like core.ComputeLineStarts, but only
// considers "\n", "\r", and "\r\n" as line breaks, and reports when the text
// is ASCII-only.
LSPLineMap* ComputeLSPLineStarts(const std::string& text) {
	auto* lineMap = new LSPLineMap;
	auto& lineStarts = lineMap->LineStarts;
	lineStarts.reserve(std::count(text.begin(), text.end(), '\n') + 1);
	bool asciiOnly = true;

	int textLen = (int)text.size();
	int pos = 0;
	int lineStart = 0;
	while (pos < textLen) {
		uint8_t b = (uint8_t)text[pos];
		if (b < 0x80) { // utf8.RuneSelf
			pos++;
			switch (b) {
			case '\r':
				if (pos < textLen && text[pos] == '\n') {
					pos++;
				}
				[[fallthrough]];
			case '\n':
				lineStarts.push_back(lineStart);
				lineStart = pos;
			}
		} else {
			// utf8.DecodeRuneInString size
			int size = 1;
			if (b >= 0xF0) size = 4;
			else if (b >= 0xE0) size = 3;
			else if (b >= 0xC0) size = 2;
			pos += size;
			asciiOnly = false;
		}
	}
	lineStarts.push_back(lineStart);

	lineMap->AsciiOnly = asciiOnly;
	return lineMap;
}

// ComputeIndexOfLineStart (linemap.go:56) — port of
// computeLineOfPosition(lineStarts, position, lowerBound?).
int LSPLineMap::ComputeIndexOfLineStart(int32_t targetPos) const {
	// slices.BinarySearchFunc — first index where LineStarts[i] >= targetPos.
	int lo = 0, hi = (int)LineStarts.size();
	bool found = false;
	while (lo < hi) {
		int mid = lo + (hi - lo) / 2;
		if (LineStarts[mid] < targetPos) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	int lineNumber = lo;
	if (lineNumber < (int)LineStarts.size() && LineStarts[lineNumber] == targetPos) {
		found = true;
	}
	if (!found && lineNumber > 0) {
		// If the actual position was not found, the binary search returns where
		// the target line start would be inserted if the target was in the
		// slice. We want the index of the previous line start, so subtract 1.
		lineNumber = lineNumber - 1;
	}
	return lineNumber;
}

namespace {

// extraEscapeReplacer (converters.go:307) composed over url.PathEscape: every
// non-unreserved byte is %-escaped, uppercase hex (the composition leaves only
// RFC 3986 unreserved characters).
bool isUnreservedURI(uint8_t c) {
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
	       (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
	       c == '~';
}

std::string escapeURIPart(std::string_view part) {
	static const char* hex = "0123456789ABCDEF";
	std::string out;
	out.reserve(part.size());
	for (uint8_t c : part) {
		if (isUnreservedURI(c)) {
			out += (char)c;
		} else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 0xF];
		}
	}
	return out;
}

// tspath.SplitVolumePath (path.go:1143).
std::tuple<std::string, std::string, bool> splitVolumePath(const std::string& path) {
	if (path.size() >= 2 && tspath::isVolumeCharacter(path[0]) && path[1] == ':') {
		std::string vol = path.substr(0, 2);
		vol[0] = (char)tolower((unsigned char)vol[0]);
		return {vol, path.substr(2), true};
	}
	return {"", path, false};
}

} // namespace

// FileNameToDocumentURI (converters.go:332).
lsproto::DocumentUri FileNameToDocumentURI(const std::string& fileName) {
	if (bundled::IsBundled(fileName)) {
		return lsproto::DocumentUri(fileName);
	}
	if (fileName.rfind("^/", 0) == 0) { // tspath.IsDynamicFileName
		std::string_view rest = std::string_view(fileName).substr(2);
		auto slash1 = rest.find('/');
		if (slash1 == std::string_view::npos) {
			TSC_UNREACHABLE(("invalid file name: " + fileName).c_str());
		}
		std::string_view scheme = rest.substr(0, slash1);
		std::string_view rest2 = rest.substr(slash1 + 1);
		auto slash2 = rest2.find('/');
		if (slash2 == std::string_view::npos) {
			TSC_UNREACHABLE(("invalid file name: " + fileName).c_str());
		}
		std::string_view authority = rest2.substr(0, slash2);
		std::string_view path = rest2.substr(slash2 + 1);
		if (authority == "ts-nul-authority") {
			return lsproto::DocumentUri(std::string(scheme) + ":" + std::string(path));
		}
		return lsproto::DocumentUri(std::string(scheme) + "://" + std::string(authority) +
		                            "/" + std::string(path));
	}

	auto [volume, filePart, _ok] = splitVolumePath(fileName);
	std::string escapedVolume;
	if (!volume.empty()) {
		escapedVolume = "/" + escapeURIPart(volume);
	}

	std::string_view fp = filePart;
	if (fp.size() >= 2 && fp[0] == '/' && fp[1] == '/') {
		fp = fp.substr(2); // strings.TrimPrefix(fileName, "//")
	}

	// Split on '/', escape each part (PathEscape ∘ extraEscape = unreserved-only),
	// rejoin.
	std::string result = "file://" + escapedVolume;
	size_t segStart = 0;
	while (true) {
		auto slash = fp.find('/', segStart);
		std::string_view part = fp.substr(segStart, slash == std::string_view::npos
		                                                    ? std::string_view::npos
		                                                    : slash - segStart);
		result += escapeURIPart(part);
		if (slash == std::string_view::npos) break;
		result += '/';
		segStart = slash + 1;
	}
	return lsproto::DocumentUri(result);
}

} // namespace tsc::lsconv
