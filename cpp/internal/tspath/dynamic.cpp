// Port of tsc/internal/tspath/dynamic.go.
#include "internal/tspath/tspath.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tsc::tspath {

const std::string_view DynamicURIFileNamePrefix = "^/~ts-uri~/";

namespace {

const std::string_view dynamicURIPathSegmentEscapePrefix =
	"~ts-uri-escape~";
const std::string_view dynamicURIModuleSpecifierEscapePrefix =
	"~ts-uri-spec~";
const std::string_view dynamicURINoPathEscapePrefix = "~ts-uri-no-path~";

// hex.EncodeToString.
std::string hexEncode(std::string_view data) {
	static const char* digits = "0123456789abcdef";
	std::string out;
	out.reserve(data.size() * 2);
	for (unsigned char c : data) {
		out.push_back(digits[c >> 4]);
		out.push_back(digits[c & 0xf]);
	}
	return out;
}

// hex.DecodeString; empty on error. Paired with a length check — hex
// input here is always even-length by construction, and the empty result
// only occurs on error since the decoded payload always has content.
bool hexDecode(std::string_view encoded, std::string& out) {
	auto nib = [](char c) -> int {
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	};
	if (encoded.size() % 2 != 0) return false;
	out.clear();
	out.reserve(encoded.size() / 2);
	for (size_t i = 0; i + 1 < encoded.size(); i += 2) {
		int hi = nib(encoded[i]);
		int lo = nib(encoded[i + 1]);
		if (hi < 0 || lo < 0) return false;
		out.push_back(static_cast<char>((hi << 4) | lo));
	}
	return true;
}

// utf8.Valid.
bool isValidUtf8(std::string_view s) {
	size_t i = 0;
	while (i < s.size()) {
		unsigned char b0 = static_cast<unsigned char>(s[i]);
		if (b0 < 0x80) {
			i++;
			continue;
		}
		int need;
		char32_t lo, hi;
		if (b0 >= 0xc2 && b0 <= 0xdf) {
			need = 1;
			lo = 0x80, hi = 0x7ff;
		} else if (b0 >= 0xe0 && b0 <= 0xef) {
			need = 2;
			lo = 0x800, hi = 0xffff;
		} else if (b0 >= 0xf0 && b0 <= 0xf4) {
			need = 3;
			lo = 0x10000, hi = 0x10ffff;
		} else {
			return false; // 0xc0/0xc1 overlong, >0xf4, or stray continuation
		}
		if (i + need >= s.size()) return false;
		char32_t cp = b0 & (0x7f >> (need + 1));
		for (int k = 1; k <= need; k++) {
			unsigned char bk = static_cast<unsigned char>(s[i + k]);
			if ((bk & 0xc0) != 0x80) return false;
			cp = (cp << 6) | (bk & 0x3f);
		}
		if (cp < lo || cp > hi) return false; // overlong or out of range
		if (cp >= 0xd800 && cp <= 0xdfff) return false; // surrogate
		i += need + 1;
	}
	return true;
}

std::pair<std::string_view, std::string_view> cut(std::string_view s,
	std::string_view sep, bool& found) {
	auto pos = s.find(sep);
	if (pos == std::string_view::npos) {
		found = false;
		return {s, {}};
	}
	found = true;
	return {s.substr(0, pos), s.substr(pos + sep.size())};
}

std::pair<std::string_view, bool> cutPrefix(std::string_view s,
	std::string_view prefix) {
	if (s.substr(0, prefix.size()) == prefix) {
		return {s.substr(prefix.size()), true};
	}
	return {s, false};
}

std::pair<std::string_view, std::string_view> splitDynamicURIFileExtension(
	std::string_view segment) {
	auto slash = segment.find_last_of('\\');
	std::string_view baseName = segment.substr(
		slash == std::string_view::npos ? 0 : slash + 1);
	std::string_view extension = getDeclarationFileExtension(baseName);
	if (extension.empty()) {
		extension = getAnyExtensionFromPath(baseName, nullptr /*extensions*/,
			false /*caseSensitive*/);
	}
	if (!extension.empty()) {
		segment = segment.substr(0, segment.size() - extension.size());
	}
	return {segment, extension};
}

std::string forceEncodeDynamicURIPathSegmentImpl(std::string_view segment,
	bool preserveExtension) {
	std::string_view extension;
	if (preserveExtension && segment != "." && segment != "..") {
		auto [seg, ext] = splitDynamicURIFileExtension(segment);
		segment = seg;
		extension = ext;
	}
	std::string out(dynamicURIPathSegmentEscapePrefix);
	out += hexEncode(segment);
	out += "~";
	out += extension;
	return out;
}

std::string forceEncodeDynamicURIPathSegmentWithSuffixImpl(
	std::string_view segment, std::string_view suffix) {
	auto [seg, extension] = splitDynamicURIFileExtension(segment);
	std::string payload(seg);
	payload += '\0';
	payload += suffix;
	return std::string(dynamicURIPathSegmentEscapePrefix) +
	       hexEncode(payload) + "~" + std::string(extension);
}

bool dynamicURIPathSegmentNeedsEncoding(std::string_view segment) {
	return segment.empty() || segment == "." || segment == ".." ||
	       segment.substr(0, dynamicURIPathSegmentEscapePrefix.size()) ==
			dynamicURIPathSegmentEscapePrefix ||
	       segment.substr(0, dynamicURIModuleSpecifierEscapePrefix.size()) ==
			dynamicURIModuleSpecifierEscapePrefix ||
	       segment.substr(0, dynamicURINoPathEscapePrefix.size()) ==
			dynamicURINoPathEscapePrefix ||
	       segment.find('\\') != std::string_view::npos;
}

bool dynamicURIPathNeedsEncoding(std::string_view path) {
	for (;;) {
		bool found;
		auto [segment, rest] = cut(path, "/", found);
		if (dynamicURIPathSegmentNeedsEncoding(segment)) {
			return true;
		}
		if (!found) {
			return false;
		}
		path = rest;
	}
}

std::string encodeDynamicURIPathSegment(std::string_view segment,
	bool preserveExtension) {
	if (dynamicURIPathSegmentNeedsEncoding(segment)) {
		return forceEncodeDynamicURIPathSegmentImpl(segment, preserveExtension);
	}
	return std::string(segment);
}

std::string encodeDynamicURIPathImpl(std::string_view path,
	bool preserveFinalExtension) {
	if (!dynamicURIPathNeedsEncoding(path)) {
		return std::string(path);
	}
	std::string result;
	result.reserve(path.size() + dynamicURIPathSegmentEscapePrefix.size());
	for (;;) {
		bool found;
		auto [segment, rest] = cut(path, "/", found);
		result += encodeDynamicURIPathSegment(segment,
			preserveFinalExtension && !found);
		if (!found) return result;
		result += '/';
		path = rest;
	}
}

std::string encodeDynamicURIPathRootAwareImpl(std::string_view path,
	bool preserveFinalExtension) {
	std::string encoded = encodeDynamicURIPathImpl(path, preserveFinalExtension);
	if (!pathIsAbsolute(encoded)) {
		return encoded;
	}
	bool found;
	auto [first, rest] = cut(encoded, "/", found);
	if (!found) {
		return forceEncodeDynamicURIPathSegmentImpl(first,
			preserveFinalExtension);
	}
	return forceEncodeDynamicURIPathSegmentImpl(first, false) + "/" +
	       std::string(rest);
}

std::string encodeDynamicRelativeURIPathImpl(std::string_view path,
	bool preserveFinalExtension) {
	return encodeDynamicURIPathRootAwareImpl(path, preserveFinalExtension);
}

std::string encodeDynamicModuleSpecifierSegment(std::string_view segment,
	bool preserveExtension) {
	auto [encoded, ok] = cutPrefix(segment,
		dynamicURIModuleSpecifierEscapePrefix);
	if (ok) {
		std::string physical =
			std::string(dynamicURIPathSegmentEscapePrefix) +
			std::string(encoded);
		if (decodeDynamicURIPathSegment(physical) != physical) {
			return physical;
		}
	}
	if (segment.substr(0, dynamicURIPathSegmentEscapePrefix.size()) ==
		dynamicURIPathSegmentEscapePrefix) {
		return forceEncodeDynamicURIPathSegmentImpl(segment,
			preserveExtension);
	}
	return std::string(segment);
}

std::string encodeDynamicModuleSpecifierImpl(std::string_view specifier,
	bool preserveFinalExtension) {
	if (specifier.find(dynamicURIPathSegmentEscapePrefix) ==
			std::string_view::npos &&
	    specifier.find(dynamicURIModuleSpecifierEscapePrefix) ==
			std::string_view::npos) {
		return std::string(specifier);
	}
	std::string result;
	result.reserve(specifier.size() +
	               dynamicURIPathSegmentEscapePrefix.size());
	for (;;) {
		auto separator = specifier.find_first_of("/\\");
		if (separator == std::string_view::npos) {
			result += encodeDynamicModuleSpecifierSegment(specifier,
				preserveFinalExtension);
			return result;
		}
		result += encodeDynamicModuleSpecifierSegment(
			specifier.substr(0, separator), false);
		result += specifier[separator];
		specifier = specifier.substr(separator + 1);
	}
}

std::vector<std::string_view> split(std::string_view s, char sep) {
	std::vector<std::string_view> out;
	size_t start = 0;
	for (;;) {
		auto pos = s.find(sep, start);
		if (pos == std::string_view::npos) {
			out.emplace_back(s.substr(start));
			return out;
		}
		out.emplace_back(s.substr(start, pos - start));
		start = pos + 1;
	}
}

std::string join(const std::vector<std::string_view>& parts,
	std::string_view sep) {
	std::string out;
	for (size_t i = 0; i < parts.size(); i++) {
		if (i) out += sep;
		out += parts[i];
	}
	return out;
}

} // namespace

bool isEncodedDynamicFileName(std::string_view path) {
	return path.substr(0, DynamicURIFileNamePrefix.size()) ==
		DynamicURIFileNamePrefix;
}

std::string canonicalDynamicURIPath(std::string_view path) {
	if (isEncodedDynamicFileName(path) &&
	    static_cast<size_t>(getRootLength(path)) == path.size() &&
	    !hasTrailingDirectorySeparator(path)) {
		return std::string(path) + '/';
	}
	return std::string(path);
}

std::string encodeDynamicURIPath(std::string_view path) {
	return encodeDynamicURIPathRootAwareImpl(path, true);
}

std::string encodeDynamicURIPathWithSuffix(std::string_view path,
	std::string_view suffix) {
	if (suffix.empty()) {
		return encodeDynamicURIPath(path);
	}
	auto slash = path.find_last_of('/');
	std::string before;
	if (slash != std::string_view::npos) {
		before = encodeDynamicURIDirectoryPath(path.substr(0, slash)) + "/";
	}
	return before + forceEncodeDynamicURIPathSegmentWithSuffixImpl(
		path.substr(slash == std::string_view::npos ? 0 : slash + 1),
		suffix);
}

std::string encodeDynamicURIDirectoryPath(std::string_view path) {
	return encodeDynamicURIPathRootAwareImpl(path, false);
}

std::string encodeDynamicRelativeURIPath(std::string_view path) {
	return encodeDynamicRelativeURIPathImpl(path, true);
}

std::string encodeDynamicRelativeURIDirectoryPath(std::string_view path) {
	return encodeDynamicRelativeURIPathImpl(path, false);
}

std::string forceEncodeDynamicURIPathSegment(std::string_view segment,
	bool preserveExtension) {
	return forceEncodeDynamicURIPathSegmentImpl(segment, preserveExtension);
}

std::string encodeDynamicModuleSpecifier(std::string_view specifier) {
	return encodeDynamicModuleSpecifierImpl(specifier, true);
}

std::string encodeDynamicDirectorySpecifier(std::string_view specifier) {
	return encodeDynamicModuleSpecifierImpl(specifier, false);
}

std::string dynamicURIPathToModuleSpecifier(std::string_view path) {
	if (path.find(dynamicURIPathSegmentEscapePrefix) ==
		std::string_view::npos) {
		return std::string(path);
	}
	auto segments = split(path, '/');
	std::vector<std::string> encoded;
	encoded.reserve(segments.size());
	for (std::string_view segment : segments) {
		if (segment.substr(0, dynamicURIPathSegmentEscapePrefix.size()) ==
				dynamicURIPathSegmentEscapePrefix &&
		    decodeDynamicURIPathSegment(segment) != segment) {
			encoded.emplace_back(
				std::string(dynamicURIModuleSpecifierEscapePrefix) +
				std::string(segment.substr(
					dynamicURIPathSegmentEscapePrefix.size())));
		} else {
			encoded.emplace_back(segment);
		}
	}
	std::vector<std::string_view> views(encoded.begin(), encoded.end());
	return join(views, "/");
}

std::string encodeDynamicLogicalModuleSpecifier(std::string_view specifier) {
	if (specifier.empty()) {
		return "";
	}
	bool trailingSeparator = hasTrailingDirectorySeparator(specifier);
	if (trailingSeparator) {
		specifier = removeTrailingDirectorySeparator(specifier);
	}
	std::string encoded = dynamicURIPathToModuleSpecifier(
		encodeDynamicRelativeURIPath(specifier));
	if (trailingSeparator) {
		encoded += '/';
	}
	return encoded;
}

std::string encodeDynamicURINoPath(std::string_view suffix) {
	return std::string(dynamicURINoPathEscapePrefix) + hexEncode(suffix) +
	       "~";
}

std::pair<std::string, bool> decodeDynamicURINoPath(std::string_view path) {
	auto [encoded, ok] = cutPrefix(path, dynamicURINoPathEscapePrefix);
	if (!ok) return {"", false};
	bool found;
	auto [hexPart, rest] = cut(encoded, "~", found);
	if (!found || !rest.empty()) return {"", false};
	std::string decoded;
	if (!hexDecode(hexPart, decoded) || !isValidUtf8(decoded)) {
		return {"", false};
	}
	return {decoded, true};
}

std::pair<std::string, bool> tryDecodeDynamicURIPathSegment(
	std::string_view segment) {
	auto [encoded, ok] = cutPrefix(segment,
		dynamicURIPathSegmentEscapePrefix);
	if (!ok) {
		if (dynamicURIPathSegmentNeedsEncoding(segment)) {
			return {"", false};
		}
		return {std::string(segment), true};
	}
	auto tilde = encoded.find('~');
	if (tilde == std::string_view::npos) {
		return {"", false};
	}
	std::string_view hexPart = encoded.substr(0, tilde);
	std::string_view extension = encoded.substr(tilde + 1);
	std::string decoded;
	if (!hexDecode(hexPart, decoded) || !isValidUtf8(decoded)) {
		return {"", false};
	}
	auto nul = decoded.find('\0');
	if (nul != std::string::npos) {
		return {decoded.substr(0, nul) + std::string(extension) +
		        decoded.substr(nul + 1), true};
	}
	return {decoded + std::string(extension), true};
}

std::string decodeDynamicURIPathSegment(std::string_view segment) {
	auto [decoded, ok] = tryDecodeDynamicURIPathSegment(segment);
	if (!ok) {
		return std::string(segment);
	}
	return decoded;
}

std::string decodeDynamicURIPath(std::string_view path) {
	if (path.find(dynamicURIPathSegmentEscapePrefix) ==
		std::string_view::npos) {
		return std::string(path);
	}
	auto segments = split(path, '/');
	std::vector<std::string> decoded;
	decoded.reserve(segments.size());
	for (std::string_view segment : segments) {
		decoded.emplace_back(decodeDynamicURIPathSegment(segment));
	}
	std::vector<std::string_view> views(decoded.begin(), decoded.end());
	return join(views, "/");
}

std::pair<std::string, bool> tryDecodeDynamicURIPath(std::string_view path) {
	auto segments = split(path, '/');
	std::vector<std::string> decoded;
	decoded.reserve(segments.size());
	for (std::string_view segment : segments) {
		auto [d, ok] = tryDecodeDynamicURIPathSegment(segment);
		if (!ok) {
			return {"", false};
		}
		decoded.emplace_back(d);
	}
	std::vector<std::string_view> views(decoded.begin(), decoded.end());
	return {join(views, "/"), true};
}

std::pair<std::string, bool> decodeDynamicURIPathForDisk(
	std::string_view path) {
	std::string decoded = decodeDynamicURIPath(path);
	if (pathIsAbsolute(decoded)) {
		return {"", false};
	}
	std::string_view remaining = decoded;
	for (;;) {
		bool found;
		auto [segment, rest] = cut(remaining, "/", found);
		if (segment.empty()) {
			if (!found) {
				return {decoded, true};
			}
			return {"", false};
		}
		if (segment == "." || segment == ".." ||
		    segment.find('\\') != std::string_view::npos) {
			return {"", false};
		}
		if (!found) {
			return {decoded, true};
		}
		remaining = rest;
	}
}

} // namespace tsc::tspath
