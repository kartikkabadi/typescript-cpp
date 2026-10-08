// tspath — minimal port (extension helpers)
#pragma once
#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "internal/stringutil/stringutil.h"

namespace tsc {
// From ast/ast.h — re-declared so this header need not include ast.h.
[[noreturn]] void tscUnreachable(const char* message);
}

namespace tsc::tspath {

inline std::string_view getBaseFileName(std::string_view path) {
	size_t i = path.find_last_of("/\\");
	return i == std::string_view::npos ? path : path.substr(i + 1);
}

inline bool endsWith(std::string_view s, std::string_view suffix) {
	return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

inline std::string_view getDeclarationFileExtension(std::string_view fileName) {
	std::string_view base = getBaseFileName(fileName);
	for (auto ext : {".d.ts", ".d.cts", ".d.mts"}) {
		if (endsWith(base, ext)) return {base.end() - std::string_view(ext).size(), base.end()};
	}
	if (endsWith(base, ".ts")) {
		auto idx = base.find(".d.");
		if (idx != std::string_view::npos) return base.substr(idx);
	}
	return {};
}

inline bool isDeclarationFileName(std::string_view fileName) {
	return !getDeclarationFileExtension(fileName).empty();
}

// path.go: IsVolumeCharacter / getFileUrlVolumeSeparatorEnd /
// GetEncodedRootLength / IsRootedDiskPath / PathIsRelative /
// IsExternalModuleNameRelative

inline bool isVolumeCharacter(char ch) {
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

// path.go:50 — IsDynamicFileName: true for dynamic/virtual file names
// ("^/untitled/...").
inline bool isDynamicFileName(std::string_view fileName) {
	return fileName.size() >= 2 && fileName[0] == '^' && fileName[1] == '/';
}

// path.go:1143 — SplitVolumePath: splits a leading DOS volume ("c:") from
// the rest of the path.
inline std::tuple<std::string, std::string_view, bool>
splitVolumePath(std::string_view path) {
	if (path.size() >= 2 && isVolumeCharacter(path[0]) && path[1] == ':') {
		std::string volume;
		volume += (char)std::tolower((unsigned char)path[0]);
		volume += path[1];
		return {volume, path.substr(2), true};
	}
	return {"", path, false};
}

inline int getFileUrlVolumeSeparatorEnd(std::string_view url, int start) {
	if ((int)url.size() <= start) {
		return -1;
	}
	char ch0 = url[start];
	if (ch0 == ':') {
		return start + 1;
	}
	if (ch0 == '%' && (int)url.size() > start + 2 && url[start + 1] == '3') {
		char ch2 = url[start + 2];
		if (ch2 == 'a' || ch2 == 'A') {
			return start + 3;
		}
	}
	return -1;
}

inline constexpr std::string_view urlSchemeSeparator = "://";

// Returns the encoded root length of a path. A positive result is the number
// of chars in the root; a negative result (~x) marks a URL root whose decoded
// length is -x-1... mirroring Go's sign-flipped URL encoding, though callers
// here only compare > 0.
inline int getEncodedRootLength(std::string_view path) {
	int ln = (int)path.size();
	if (ln == 0) {
		return 0;
	}
	char ch0 = path[0];

	// POSIX or UNC
	if (ch0 == '/' || ch0 == '\\') {
		if (ln == 1 || path[1] != ch0) {
			return 1;  // POSIX: "/" (or non-normalized "\")
		}
		int offset = 2;
		auto p1 = path.find(ch0, offset);
		if (p1 == std::string_view::npos) {
			return ln;  // UNC: "//server" or "\\server"
		}
		return (int)p1 + 1;  // UNC: "//server/" or "\\server\"
	}

	// DOS
	if (isVolumeCharacter(ch0) && ln > 1 && path[1] == ':') {
		if (ln == 2) {
			return 2;  // DOS: "c:" (but not "c:d")
		}
		char ch2 = path[2];
		if (ch2 == '/' || ch2 == '\\') {
			return 3;  // DOS: "c:/" or "c:\"
		}
	}

	// Untitled paths (e.g., "^/untitled/ts-nul-authority/Untitled-1")
	if (ch0 == '^' && ln > 1 && path[1] == '/') {
		return 2;  // Untitled: "^/"
	}

	// URL
	auto schemeEnd = path.find(urlSchemeSeparator);
	if (schemeEnd != std::string_view::npos) {
		int authorityStart = (int)schemeEnd + (int)urlSchemeSeparator.size();
		// Go: strings.Index(path[authorityStart:], "/") is RELATIVE to the
		// substring; find(pos) is absolute — it IS authorityEnd.
		auto authorityLength = path.find('/', authorityStart);
		if (authorityLength != std::string_view::npos) {
			// URL: "file:///", "file://server/", "file://server/path"
			int authorityEnd = (int)authorityLength;

			// For local "file" URLs, include the leading DOS volume (if
			// present). Per https://www.ietf.org/rfc/rfc1738.txt, a host of
			// "" or "localhost" is a special case interpreted as "the
			// machine from which the URL is being interpreted".
			std::string_view scheme = path.substr(0, schemeEnd);
			std::string_view authority = path.substr(
				authorityStart, authorityEnd - authorityStart);
			if (scheme == "file" &&
			    (authority.empty() || authority == "localhost") &&
			    ((int)path.size() > authorityEnd + 2) &&
			    isVolumeCharacter(path[authorityEnd + 1])) {
				int volumeSeparatorEnd = getFileUrlVolumeSeparatorEnd(
					path, authorityEnd + 2);
				if (volumeSeparatorEnd != -1) {
					if (volumeSeparatorEnd == (int)path.size()) {
						// URL: "file:///c:", "file://localhost/c:",
						// "file:///c$3a", "file://localhost/c%3a"
						// but not "file:///c:d" or "file:///c%3ad"
						return ~volumeSeparatorEnd;
					}
					if (path[volumeSeparatorEnd] == '/') {
						// URL: "file:///c:/", "file://localhost/c:/",
						// "file:///c%3a/", "file://localhost/c%3a/"
						return ~(volumeSeparatorEnd + 1);
					}
				}
			}
			return ~(authorityEnd + 1);  // URL: "file://server/",
			                             // "http://server/"
		}
		return ~ln;  // URL: "file://server", "http://server"
	}

	// relative
	return 0;
}

inline int getRootLength(std::string_view path) {
	int rootLength = getEncodedRootLength(path);
	return rootLength < 0 ? ~rootLength : rootLength;
}

inline bool isRootedDiskPath(std::string_view path) {
	return getEncodedRootLength(path) > 0;
}

// === slice: api ===
// IsDiskPathRoot — path.go:43. Determines whether a path consists only of a
// path root.
inline bool isDiskPathRoot(std::string_view path) {
	int rootLength = getEncodedRootLength(path);
	return rootLength > 0 &&
	       static_cast<size_t>(rootLength) == path.size();
}
// === end slice: api ===

// === slice: vfs ===
// isUrl — path.go IsUrl: path starts with a URL scheme (e.g. http://).
inline bool isUrl(std::string_view path) {
	return getEncodedRootLength(path) < 0;
}

inline bool pathIsRelative(std::string_view path) {
	// True if path is ".", "..", or starts with "./", "../", ".\", or "..\".
	if (path == "." || path == "..") {
		return true;
	}
	if (path.size() >= 2 && path[0] == '.' &&
	    (path[1] == '/' || path[1] == '\\')) {
		return true;
	}
	if (path.size() >= 3 && path[0] == '.' && path[1] == '.' &&
	    (path[2] == '/' || path[2] == '\\')) {
		return true;
	}
	return false;
}

inline bool isExternalModuleNameRelative(std::string_view moduleName) {
	// TypeScript 1.0 spec (April 2014): 11.2.1
	// An external module name is "relative" if the first term is "." or "..".
	// Update: We also consider a path like `C:\foo.ts` "relative" because we
	// do not search for it in `node_modules` or treat it as an ambient
	// module.
	return pathIsRelative(moduleName) || isRootedDiskPath(moduleName);
}

} // namespace tsc::tspath

namespace tsc::tspath {

// extension.go — extension constants and tables.
inline constexpr std::string_view extensionTs = ".ts";
inline constexpr std::string_view extensionTsx = ".tsx";
inline constexpr std::string_view extensionDts = ".d.ts";
inline constexpr std::string_view extensionJs = ".js";
inline constexpr std::string_view extensionJsx = ".jsx";
inline constexpr std::string_view extensionJson = ".json";
inline constexpr std::string_view extensionMjs = ".mjs";
inline constexpr std::string_view extensionMts = ".mts";
inline constexpr std::string_view extensionDmts = ".d.mts";
inline constexpr std::string_view extensionCjs = ".cjs";
inline constexpr std::string_view extensionCts = ".cts";
inline constexpr std::string_view extensionDcts = ".d.cts";
inline constexpr std::string_view extensionTsBuildInfo = ".tsbuildinfo";

inline const std::vector<std::string_view> supportedTSExtensionsFlat = {
	extensionTs, extensionTsx, extensionDts, extensionCts, extensionDcts,
	extensionMts, extensionDmts};
inline const std::vector<std::string_view> extensionsToRemove = {
	extensionDts, extensionDmts, extensionDcts, extensionMjs, extensionMts,
	extensionCjs, extensionCts, extensionTs, extensionJs, extensionTsx,
	extensionJsx, extensionJson};
inline const std::vector<std::string_view> supportedTSExtensionsForExtractExtension = {
	extensionDts, extensionDcts, extensionDmts, extensionTs, extensionTsx,
	extensionMts, extensionCts};

inline bool extensionIsTs(std::string_view ext) {
	return ext == extensionTs || ext == extensionTsx || ext == extensionDts ||
	       ext == extensionMts || ext == extensionDmts || ext == extensionCts ||
	       ext == extensionDcts ||
	       (ext.size() >= 7 && ext.substr(0, 3) == ".d." &&
	        ext.substr(ext.size() - 3) == ".ts");
}

inline bool fileExtensionIs(std::string_view path, std::string_view extension) {
	return path.size() > extension.size() && endsWith(path, extension);
}

inline bool fileExtensionIsOneOf(std::string_view path,
                                 const std::vector<std::string_view>& extensions) {
	for (auto ext : extensions) {
		if (fileExtensionIs(path, ext)) return true;
	}
	return false;
}

inline std::string_view tryGetExtensionFromPath(std::string_view p) {
	for (auto ext : extensionsToRemove) {
		if (fileExtensionIs(p, ext)) return ext;
	}
	return {};
}

inline std::string_view tryExtractTSExtension(std::string_view fileName) {
	for (auto ext : supportedTSExtensionsForExtractExtension) {
		if (fileExtensionIs(fileName, ext)) return ext;
	}
	return {};
}

inline bool hasTSFileExtension(std::string_view path) {
	return fileExtensionIsOneOf(path, supportedTSExtensionsFlat);
}

inline bool hasExtension(std::string_view fileName) {
	return getBaseFileName(fileName).find('.') != std::string_view::npos;
}

inline std::string_view removeExtension(std::string_view path,
                                        std::string_view extension) {
	return path.substr(0, path.size() - extension.size());
}

// equateStringCaseInsensitive — stringutil.EquateStringCaseInsensitive
// (strings.EqualFold, full Unicode case folding).
inline bool equateStringCaseInsensitive(std::string_view a, std::string_view b) {
	return stringutil::EquateStringCaseInsensitive(a, b);
}

inline std::function<bool(std::string_view, std::string_view)>
getStringEqualityComparer(bool ignoreCase) {
	if (ignoreCase) {
		return [](std::string_view a, std::string_view b) {
			return equateStringCaseInsensitive(a, b);
		};
	}
	return [](std::string_view a, std::string_view b) { return a == b; };
}

inline std::string_view tryGetExtensionFromPathWorker(
    std::string_view path, std::string_view extension,
    const std::function<bool(std::string_view, std::string_view)>& eq) {
	std::string ext{extension};
	if (ext.empty() || ext[0] != '.') {
		ext = "." + ext;
	}
	if (path.size() >= ext.size() && path[path.size() - ext.size()] == '.') {
		std::string_view pathExtension = path.substr(path.size() - ext.size());
		if (eq(pathExtension, ext)) {
			return pathExtension;
		}
	}
	return {};
}

inline std::string_view getAnyExtensionFromPath(
    std::string_view path, const std::vector<std::string_view>* extensions,
    bool ignoreCase) {
	if (extensions != nullptr && !extensions->empty()) {
		for (auto ext : *extensions) {
			auto matched = tryGetExtensionFromPathWorker(
			    path, ext, getStringEqualityComparer(ignoreCase));
			if (!matched.empty()) return matched;
		}
		return {};
	}
	std::string_view baseFileName = getBaseFileName(path);
	auto extensionIndex = baseFileName.find_last_of('.');
	if (extensionIndex != std::string_view::npos) {
		return baseFileName.substr(extensionIndex);
	}
	return {};
}

// path.go — separators, normalization, relative paths.
inline bool hasTrailingDirectorySeparator(std::string_view path) {
	return !path.empty() &&
	       (path.back() == '/' || path.back() == '\\');
}

inline std::string_view removeTrailingDirectorySeparator(std::string_view path) {
	if (hasTrailingDirectorySeparator(path)) {
		return path.substr(0, path.size() - 1);
	}
	return path;
}

// CompareNumberOfDirectorySeparators — path.go:1271.
inline int compareNumberOfDirectorySeparators(std::string_view path1,
                                              std::string_view path2) {
	int a = static_cast<int>(std::count(path1.begin(), path1.end(), '/'));
	int b = static_cast<int>(std::count(path2.begin(), path2.end(), '/'));
	if (a < b) {
		return -1;
	}
	if (a > b) {
		return 1;
	}
	return 0;
}

inline std::string removeTrailingDirectorySeparators(std::string_view path) {
	while (hasTrailingDirectorySeparator(path)) {
		path = path.substr(0, path.size() - 1);
	}
	return std::string(path);
}

inline std::string ensureTrailingDirectorySeparator(std::string_view path) {
	if (!hasTrailingDirectorySeparator(path)) {
		return std::string(path) + "/";
	}
	return std::string(path);
}

inline std::string normalizeSlashes(std::string_view path) {
	std::string result{path};
	for (auto& c : result) {
		if (c == '\\') c = '/';
	}
	return result;
}

inline std::string combinePaths(std::string_view firstPath,
                                const std::vector<std::string_view>& paths) {
	std::string result = normalizeSlashes(firstPath);
	for (auto p : paths) {
		if (p.empty()) continue;
		auto trailingPath = normalizeSlashes(p);
		if (result.empty() || getRootLength(trailingPath) != 0) {
			result = trailingPath;
		} else {
			if (!hasTrailingDirectorySeparator(result)) {
				result += '/';
			}
			result += trailingPath;
		}
	}
	return result;
}

inline std::vector<std::string_view> pathComponents(std::string_view path,
                                                    int rootLength) {
	std::string_view root = path.substr(0, rootLength);
	std::vector<std::string_view> rest;
	std::string_view tail = path.substr(rootLength);
	size_t start = 0;
	while (true) {
		auto idx = tail.find('/', start);
		if (idx == std::string_view::npos) {
			rest.push_back(tail.substr(start));
			break;
		}
		rest.push_back(tail.substr(start, idx - start));
		start = idx + 1;
	}
	if (!rest.empty() && rest.back().empty()) {
		rest.pop_back();
	}
	rest.insert(rest.begin(), root);
	return rest;
}

// getPathComponents — Go returns substrings of the combined path; copy
// them since `combined` is a local temporary.
inline std::vector<std::string> getPathComponents(
    std::string_view path, std::string_view currentDirectory) {
	auto combined = combinePaths(currentDirectory, {path});
	auto views = pathComponents(combined, getRootLength(combined));
	return {views.begin(), views.end()};
}

inline std::vector<std::string> reducePathComponents(
    const std::vector<std::string>& components) {
	if (components.empty()) return {};
	std::vector<std::string> reduced{components[0]};
	for (size_t i = 1; i < components.size(); i++) {
		auto& component = components[i];
		if (component.empty() || component == ".") continue;
		if (component == "..") {
			if (reduced.size() > 1) {
				if (reduced.back() != "..") {
					reduced.pop_back();
					continue;
				}
			} else if (!reduced[0].empty()) {
				continue;
			}
		}
		reduced.push_back(component);
	}
	return reduced;
}

inline std::string getPathFromPathComponents(
    const std::vector<std::string_view>& pathComponents) {
	if (pathComponents.empty()) return "";
	std::string root{pathComponents[0]};
	if (!root.empty()) {
		root = ensureTrailingDirectorySeparator(root);
	}
	for (size_t i = 1; i < pathComponents.size(); i++) {
		if (i > 1) root += '/';
		root += pathComponents[i];
	}
	return root;
}

// path.go: hasRelativePathSegment — ".", "..", "./", "../", "/.", "/..",
// "//", "/./" or "/../".
inline bool hasRelativePathSegment(std::string_view p) {
	size_t n = p.size();
	if (n == 0) return false;
	if (p == "." || p == "..") return true;
	if (p[0] == '.') {
		if (n >= 2 && p[1] == '/') return true;
		if (n >= 3 && p[1] == '.' && p[2] == '/') return true;
	}
	if (p[n - 1] == '.') {
		if (n >= 2 && p[n - 2] == '/') return true;
		if (n >= 3 && p[n - 2] == '.' && p[n - 3] == '/') return true;
	}
	bool prevSlash = false;
	int segLen = 0;
	int dotCount = 0;
	for (size_t i = 0; i < n; i++) {
		char c = p[i];
		if (c == '/') {
			if (prevSlash) return true;
			if ((segLen == 1 && dotCount == 1) ||
			    (segLen == 2 && dotCount == 2)) {
				return true;
			}
			prevSlash = true;
			segLen = 0;
			dotCount = 0;
			continue;
		}
		if (c == '.') {
			if (dotCount >= 0) dotCount++;
		} else {
			dotCount = -1;
		}
		segLen++;
		prevSlash = false;
	}
	return (segLen == 1 && dotCount == 1) || (segLen == 2 && dotCount == 2);
}

inline std::pair<std::string, bool> simpleNormalizePath(std::string_view path) {
	if (!hasRelativePathSegment(path)) {
		return {std::string(path), true};
	}
	std::string simplified;
	simplified.reserve(path.size());
	for (size_t i = 0; i < path.size();) {
		if (i + 2 < path.size() && path[i] == '/' && path[i + 1] == '.' &&
		    path[i + 2] == '/') {
			simplified += '/';
			i += 3;
		} else {
			simplified += path[i++];
		}
	}
	std::string_view trimmed = simplified;
	if (trimmed.substr(0, 2) == "./") trimmed = trimmed.substr(2);
	if (trimmed != path && !hasRelativePathSegment(trimmed) &&
	    !(trimmed != simplified && trimmed.substr(0, 1) == "/")) {
		return {std::string(trimmed), true};
	}
	return {"", false};
}

inline std::string getNormalizedAbsolutePath(std::string_view fileName,
                                             std::string_view currentDirectory) {
	std::string file;
	int rootLength = getRootLength(fileName);
	if (rootLength == 0 && !currentDirectory.empty()) {
		file = combinePaths(currentDirectory, {fileName});
	} else {
		// CombinePaths normalizes slashes, so not necessary in other branch
		file = normalizeSlashes(fileName);
	}
	rootLength = getRootLength(file);

	if (auto [simpleNormalized, ok] = simpleNormalizePath(file); ok) {
		size_t length = simpleNormalized.size();
		if ((int)length > rootLength) {
			return std::string(
			    removeTrailingDirectorySeparator(simpleNormalized));
		}
		if ((int)length == rootLength && rootLength != 0) {
			return ensureTrailingDirectorySeparator(simpleNormalized);
		}
		return simpleNormalized;
	}

	size_t length = file.size();
	std::string_view root = std::string_view(file).substr(0, rootLength);
	// `normalized` is only initialized once `file` is determined to be
	// non-normalized. `changed` is set at the same time.
	bool changed = false;
	std::string normalized;
	size_t segmentStart = 0;
	size_t index = rootLength;
	size_t normalizedUpTo = index;
	bool seenNonDotDotSegment = rootLength != 0;
	while (index < length) {
		// At beginning of segment
		segmentStart = index;
		char ch = file[index];
		while (ch == '/') {
			index++;
			if (index < length) {
				ch = file[index];
			} else {
				break;
			}
		}
		if (index > segmentStart) {
			// Seen superfluous separator
			if (!changed) {
				size_t cut = segmentStart >= 1 ? segmentStart - 1 : 0;
				if (rootLength > (int)cut) cut = rootLength;
				normalized = file.substr(0, cut);
				changed = true;
			}
			if (index == length) {
				break;
			}
			segmentStart = index;
		}
		// Past any superfluous separators
		size_t segmentEnd = file.find('/', index + 1);
		if (segmentEnd == std::string::npos) {
			segmentEnd = length;
		}
		size_t segmentLength = segmentEnd - segmentStart;
		if (segmentLength == 1 && file[index] == '.') {
			// "." segment (skip)
			if (!changed) {
				normalized = file.substr(0, normalizedUpTo);
				changed = true;
			}
		} else if (segmentLength == 2 && file[index] == '.' &&
		           file[index + 1] == '.') {
			// ".." segment
			if (!seenNonDotDotSegment) {
				if (changed) {
					if ((int)normalized.size() == rootLength) {
						normalized += "..";
					} else {
						normalized += "/..";
					}
				} else {
					normalizedUpTo = index + 2;
				}
			} else if (!changed) {
				if (normalizedUpTo >= 1) {
					auto lastSep = std::string_view(file)
					                   .substr(0, normalizedUpTo - 1)
					                   .find_last_of('/');
					size_t cut = lastSep == std::string_view::npos
					                 ? 0
					                 : lastSep;
					if (rootLength > (int)cut) cut = rootLength;
					normalized = file.substr(0, cut);
				} else {
					normalized = file.substr(0, normalizedUpTo);
				}
				changed = true;
				seenNonDotDotSegment =
				    ((int)normalized.size() != rootLength ||
				     rootLength != 0) &&
				    normalized != ".." &&
				    !(normalized.size() >= 3 &&
				      normalized.substr(normalized.size() - 3) == "/..");
			} else {
				auto lastSlash = normalized.find_last_of('/');
				if (lastSlash != std::string::npos) {
					size_t cut = lastSlash;
					if (rootLength > (int)cut) cut = rootLength;
					normalized = normalized.substr(0, cut);
				} else {
					normalized = std::string(root);
				}
				seenNonDotDotSegment =
				    ((int)normalized.size() != rootLength ||
				     rootLength != 0) &&
				    normalized != ".." &&
				    !(normalized.size() >= 3 &&
				      normalized.substr(normalized.size() - 3) == "/..");
			}
		} else if (changed) {
			if ((int)normalized.size() != rootLength) {
				normalized += "/";
			}
			seenNonDotDotSegment = true;
			normalized += file.substr(segmentStart, segmentLength);
		} else {
			seenNonDotDotSegment = true;
			normalizedUpTo = segmentEnd;
		}
		index = segmentEnd + 1;
	}
	if (changed) {
		return normalized;
	}
	if (length > (size_t)rootLength) {
		return removeTrailingDirectorySeparators(file);
	}
	if ((int)length == rootLength) {
		return ensureTrailingDirectorySeparator(file);
	}
	return file;
}

inline std::string normalizePath(std::string_view path) {
	auto p = normalizeSlashes(path);
	if (auto [normalized, ok] = simpleNormalizePath(p); ok) {
		return normalized;
	}
	auto normalized = getNormalizedAbsolutePath(p, "");
	if (!normalized.empty() && hasTrailingDirectorySeparator(p)) {
		normalized = ensureTrailingDirectorySeparator(normalized);
	}
	return normalized;
}

inline std::string getDirectoryPath(std::string_view path) {
	auto p = normalizeSlashes(path);
	int rootLength = getRootLength(p);
	if (rootLength == (int)p.size()) {
		return p;
	}
	auto trimmed = removeTrailingDirectorySeparator(p);
	auto lastSep = trimmed.find_last_of('/');
	size_t end = lastSep == std::string_view::npos ? 0 : lastSep;
	if ((int)end < rootLength) end = rootLength;
	return std::string(trimmed.substr(0, end));
}

// path.go: ToFileNameLowerCase — ASCII fast path; non-ASCII lowercases all
// runes except U+0130.
inline std::string toFileNameLowerCase(std::string_view fileName) {
	bool ascii = true;
	bool needsLower = false;
	for (char c : fileName) {
		if ((uint8_t)c >= 0x80) {
			ascii = false;
			break;
		}
		if (c >= 'A' && c <= 'Z') needsLower = true;
	}
	if (ascii) {
		if (!needsLower) return std::string(fileName);
		std::string b{fileName};
		for (auto& c : b) {
			if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
		}
		return b;
	}
	// Non-ASCII: fold each UTF-8 rune except U+0130 — Go: strings.Map(func(r)
	// rune { if r == IWithDot { return r }; return unicode.ToLower(r) }).
	std::string result;
	result.reserve(fileName.size());
	for (size_t i = 0; i < fileName.size();) {
		int w = 0;
		char32_t r = decodeUtf8Rune(fileName.substr(i), &w);
		if (w <= 0) {
			w = 1;
		}
		if (r == 0x0130) {  // IWithDot — left unchanged by Go.
			result.append(fileName.substr(i, w));
		} else {
			char buf[4];
			result.append(buf,
			              encodeUtf8Rune(stringutil::toLowerRune(r), buf));
		}
		i += w;
	}
	return result;
}

using Path = std::string;

inline Path toPath(std::string_view fileName, std::string_view basePath,
                   bool useCaseSensitiveFileNames) {
	std::string nonCanonicalizedPath;
	if (isRootedDiskPath(fileName)) {
		nonCanonicalizedPath = normalizePath(fileName);
	} else {
		nonCanonicalizedPath =
		    getNormalizedAbsolutePath(fileName, basePath);
	}
	if (useCaseSensitiveFileNames) {
		return nonCanonicalizedPath;
	}
	return toFileNameLowerCase(nonCanonicalizedPath);
}

struct ComparePathsOptions {
	bool useCaseSensitiveFileNames{};
	std::string currentDirectory{};

	std::function<bool(std::string_view, std::string_view)> equalityComparer()
	    const {
		return getStringEqualityComparer(!useCaseSensitiveFileNames);
	}
};

// === slice: ls-autoimport ===
// IsDynamicFileName — path.go:48 lives above (canonical port).

inline bool pathIsAbsolute(std::string_view path) {
	return getEncodedRootLength(path) != 0;
}

inline std::string ensurePathIsNonModuleName(std::string_view path) {
	if (!pathIsAbsolute(path) && !pathIsRelative(path)) {
		return "./" + std::string(path);
	}
	return std::string(path);
}

inline std::vector<std::string> getPathComponentsRelativeTo(
    std::string_view from, std::string_view to,
    const ComparePathsOptions& options) {
	auto fromComponents = reducePathComponents(
	    getPathComponents(from, options.currentDirectory));
	auto toComponents = reducePathComponents(
	    getPathComponents(to, options.currentDirectory));

	size_t start = 0;
	size_t maxCommonComponents =
	    std::min(fromComponents.size(), toComponents.size());
	auto stringEqualer = options.equalityComparer();
	for (; start < maxCommonComponents; start++) {
		auto fromComponent = fromComponents[start];
		auto toComponent = toComponents[start];
		if (start == 0) {
			if (!equateStringCaseInsensitive(fromComponent, toComponent)) {
				break;
			}
		} else {
			if (!stringEqualer(fromComponent, toComponent)) {
				break;
			}
		}
	}

	if (start == 0) {
		std::vector<std::string> result;
		for (auto c : toComponents) result.emplace_back(c);
		return result;
	}

	size_t numDotDotSlashes = fromComponents.size() - start;
	std::vector<std::string> result;
	result.emplace_back("");
	for (size_t i = 0; i < numDotDotSlashes; i++) {
		result.emplace_back("..");
	}
	for (size_t i = start; i < toComponents.size(); i++) {
		result.emplace_back(toComponents[i]);
	}
	return result;
}

inline std::string getRelativePathFromDirectory(
    std::string_view fromDirectory, std::string_view to,
    const ComparePathsOptions& options) {
	if ((getRootLength(fromDirectory) > 0) != (getRootLength(to) > 0)) {
		// path.go: panic("paths must either both be absolute or both be relative")
		__builtin_trap();
	}
	auto pcs = getPathComponentsRelativeTo(fromDirectory, to, options);
	std::vector<std::string_view> views(pcs.begin(), pcs.end());
	return getPathFromPathComponents(views);
}

inline std::string getRelativePathFromFile(std::string_view from,
                                           std::string_view to,
                                           const ComparePathsOptions& options) {
	return ensurePathIsNonModuleName(getRelativePathFromDirectory(
	    getDirectoryPath(from), to, options));
}

// GetDeclarationEmitExtensionForPath — extension.go:137
inline std::string_view getDeclarationEmitExtensionForPath(std::string_view path) {
	if (fileExtensionIsOneOf(path, {extensionMjs, extensionMts})) {
		return extensionDmts;
	}
	if (fileExtensionIsOneOf(path, {extensionCjs, extensionCts})) {
		return extensionDcts;
	}
	if (fileExtensionIsOneOf(path, {extensionTs, extensionTsx, extensionJs,
	                                extensionJsx})) {
		return extensionDts;
	}
	std::string_view ext = getAnyExtensionFromPath(path, nullptr, false);
	if (!ext.empty()) {
		// ".d" + ext + ".ts" — needs a heap string, so the two special cases
		// above are kept as string_view; callers use the std::string overload.
		return {};
	}
	return extensionDts;
}

// GetDeclarationEmitExtensionForPath — extension.go:137 (custom-extension case)
inline std::string getDeclarationEmitExtensionForPathString(std::string_view path) {
	std::string_view result = getDeclarationEmitExtensionForPath(path);
	if (!result.empty()) {
		return std::string(result);
	}
	std::string_view ext = getAnyExtensionFromPath(path, nullptr, false);
	if (!ext.empty()) {
		return ".d" + std::string(ext) + ".ts";
	}
	return std::string(extensionDts);
}

// GetRelativePathToDirectoryOrUrl — path.go:829
inline std::string getRelativePathToDirectoryOrUrl(
    std::string_view directoryPathOrUrl,
    std::string_view relativeOrAbsolutePath, bool isAbsolutePathAnUrl,
    const ComparePathsOptions& options) {
	auto pathComponents = getPathComponentsRelativeTo(
	    directoryPathOrUrl, relativeOrAbsolutePath, options);
	std::string firstComponent = pathComponents[0];
	if (isAbsolutePathAnUrl && isRootedDiskPath(firstComponent)) {
		std::string prefix;
		if (firstComponent[0] == '/') {
			prefix = "file://";
		} else {
			prefix = "file:///";
		}
		pathComponents[0] = prefix + firstComponent;
	}
	std::vector<std::string_view> views(pathComponents.begin(),
	                                    pathComponents.end());
	return getPathFromPathComponents(views);
}

// === slice: module — additional helpers the resolver needs ===

// extension.go — additional extension tables.
inline const std::vector<std::string_view> supportedDeclarationExtensions = {
	extensionDts, extensionDcts, extensionDmts};
inline const std::vector<std::string_view> supportedTSImplementationExtensions = {
	extensionTs, extensionTsx, extensionMts, extensionCts};
inline const std::vector<std::string_view> supportedJSExtensionsFlat = {
	extensionJs, extensionJsx, extensionMjs, extensionCjs};
inline const std::vector<std::vector<std::string_view>> allSupportedExtensions = {
	{extensionTs, extensionTsx, extensionDts, extensionJs, extensionJsx},
	{extensionCts, extensionDcts, extensionCjs},
	{extensionMts, extensionDmts, extensionMjs}};
inline const std::vector<std::vector<std::string_view>> supportedTSExtensions = {
	{extensionTs, extensionTsx, extensionDts},
	{extensionCts, extensionDcts},
	{extensionMts, extensionDmts}};
inline const std::vector<std::vector<std::string_view>> supportedJSExtensions = {
	{extensionJs, extensionJsx}, {extensionMjs}, {extensionCjs}};
inline const std::vector<std::vector<std::string_view>>
	allSupportedExtensionsWithJson = [] {
		auto v = allSupportedExtensions;
		v.push_back({extensionJson});
		return v;
	}();
inline const std::vector<std::vector<std::string_view>>
	supportedTSExtensionsWithJson = [] {
		auto v = supportedTSExtensions;
		v.push_back({extensionJson});
		return v;
	}();
inline const std::vector<std::string_view> supportedTSExtensionsWithJsonFlat = {
	extensionTs, extensionTsx, extensionDts, extensionCts, extensionDcts,
	extensionMts, extensionDmts, extensionJson};
inline const std::vector<std::string_view>
    extensionsNotSupportingExtensionlessResolution = {
	extensionMts, extensionDmts, extensionMjs, extensionCts,
	extensionDcts, extensionCjs};

// RemoveFileExtension — removes any known extension even with multiple dots.
inline std::string_view removeFileExtension(std::string_view path) {
	for (auto ext : extensionsToRemove) {
		if (path.size() >= ext.size() && endsWith(path, ext)) {
			return path.substr(0, path.size() - ext.size());
		}
	}
	return path;
}

// RemoveAnyFileExtension — removeFileExtension then any other extension.
inline std::string_view removeAnyFileExtension(std::string_view path) {
	if (auto withoutExtension = removeFileExtension(path);
	    withoutExtension != path) {
		return withoutExtension;
	}
	if (auto extension = getAnyExtensionFromPath(path, nullptr, false);
	    !extension.empty()) {
		return removeExtension(path, extension);
	}
	return path;
}

inline bool hasImplementationTSFileExtension(std::string_view path) {
	return fileExtensionIsOneOf(path, supportedTSImplementationExtensions) &&
	       !isDeclarationFileName(path);
}

inline bool hasJSFileExtension(std::string_view path) {
	return fileExtensionIsOneOf(path, supportedJSExtensionsFlat);
}

inline bool hasJSONFileExtension(std::string_view path) {
	return fileExtensionIs(path, extensionJson);
}

inline bool extensionIsOneOf(std::string_view ext,
                             const std::vector<std::string_view>& extensions) {
	for (auto e : extensions) {
		if (e == ext) return true;
	}
	return false;
}

// ChangeAnyExtension — change extension to `ext` if path has one of
// `extensions`.
inline std::string changeAnyExtension(
    std::string_view path, std::string_view ext,
    const std::vector<std::string_view>& extensions, bool ignoreCase) {
	auto pathext = getAnyExtensionFromPath(path, &extensions, ignoreCase);
	if (!pathext.empty()) {
		std::string result{path.substr(0, path.size() - pathext.size())};
		if (ext.empty()) return result;
		if (ext[0] != '.') result += '.';
		result += ext;
		return result;
	}
	return std::string{path};
}

inline std::string changeExtension(std::string_view path,
                                   std::string_view newExtension) {
	return changeAnyExtension(path, newExtension, extensionsToRemove, false);
}

// ChangeFullExtension — like changeAnyExtension, but declaration file
// extensions are recognized and replaced starting from the `.d`.
inline std::string changeFullExtension(std::string_view path,
                                       std::string_view newExtension) {
	auto declarationExtension = getDeclarationFileExtension(path);
	if (!declarationExtension.empty()) {
		std::string ext{newExtension};
		if (ext.empty() || ext[0] != '.') ext = "." + ext;
		return std::string{path.substr(
			       0, path.size() - declarationExtension.size())} +
		       ext;
	}
	return changeExtension(path, newExtension);
}

// GetPossibleOriginalInputExtensionForExtension — declaration/JS extensions
// to the TS input extensions that may have produced them.
// C++: returns owned strings — the `.d.x.ts` branch synthesizes an extension
// that cannot be a view into `path` (Go returns a []string that escapes
// safely into the GC heap).
inline std::vector<std::string>
getPossibleOriginalInputExtensionForExtension(std::string_view path) {
	if (fileExtensionIsOneOf(
		path, {extensionDmts, extensionMjs, extensionMts})) {
		return {std::string{extensionMts}, std::string{extensionMjs}};
	}
	if (fileExtensionIsOneOf(
		path, {extensionDcts, extensionCjs, extensionCts})) {
		return {std::string{extensionCts}, std::string{extensionCjs}};
	}
	// Handle any custom .d.x.ts extension (e.g., .d.json.ts -> .json,
	// .d.css.ts -> .css)
	if (auto ext = getDeclarationFileExtension(path);
	    !ext.empty() && ext != extensionDts) {
		auto inner = ext.substr(3, ext.size() - 3 - 3);  // ".d." .. ".ts"
		return {"." + std::string{inner}};
	}
	return {std::string{extensionTsx}, std::string{extensionTs},
	        std::string{extensionJsx}, std::string{extensionJs}};
}

// GetLongestExtensionFromPath — longest matching extension from `extensions`.
inline std::string_view getLongestExtensionFromPath(
    std::string_view path, const std::vector<std::string_view>& extensions,
    bool ignoreCase) {
	path = removeTrailingDirectorySeparator(path);
	auto comparer = getStringEqualityComparer(ignoreCase);
	std::string_view longest;
	for (auto extension : extensions) {
		if (extension.size() > longest.size()) {
			if (auto matched = tryGetExtensionFromPathWorker(path, extension,
			                                                   comparer);
			    !matched.empty()) {
				longest = matched;
			}
		}
	}
	return longest;
}

// path.go — GetNormalizedPathComponents: path components relative to
// currentDirectory, with the root component at index 0.
inline std::vector<std::string> getNormalizedPathComponentsFromCombined(
    std::string_view path);  // fwd

inline std::vector<std::string> getNormalizedPathComponents(
    std::string_view path, std::string_view currentDirectory) {
	auto combined = combinePaths(currentDirectory, {path});
	return getNormalizedPathComponentsFromCombined(combined);
}

inline std::vector<std::string> getNormalizedPathComponentsFromCombined(
    std::string_view path) {
	auto rootLength = getRootLength(path);
	// Always include the root component (empty string for relative paths).
	std::vector<std::string> components;
	components.emplace_back(path.substr(0, rootLength));

	for (size_t i = rootLength; i < path.size();) {
		// Skip directory separators (handles consecutive separators and
		// trailing '/').
		while (i < path.size() && path[i] == '/') i++;
		if (i >= path.size()) break;

		size_t start = i;
		while (i < path.size() && path[i] != '/') i++;
		auto component = path.substr(start, i - start);

		if (component.empty() || component == ".") continue;
		if (component == "..") {
			if (components.size() > 1) {
				if (components.back() != "..") {
					components.pop_back();
					continue;
				}
			} else if (!components[0].empty()) {
				// If this is an absolute path, we can't go above the root.
				continue;
			}
		}

		components.emplace_back(component);
	}

	return components;
}

// ResolvePath — combines and resolves paths; `.` and `..` are resolved;
// trailing directory separators preserved.
inline std::string resolvePath(std::string_view path,
                               const std::vector<std::string_view>& paths) {
	std::string combinedPath;
	if (!paths.empty()) {
		combinedPath = combinePaths(path, paths);
	} else {
		combinedPath = normalizeSlashes(path);
	}
	return normalizePath(combinedPath);
}

// GetCanonicalFileName — canonicalizes a file name per
// useCaseSensitiveFileNames.
inline std::string getCanonicalFileName(std::string_view fileName,
                                        bool useCaseSensitiveFileNames) {
	if (useCaseSensitiveFileNames) {
		return std::string{fileName};
	}
	return toFileNameLowerCase(fileName);
}

// trimRuneCount — path.go:641. Returns the suffix of s after skipping up to
// runeCount runes, clamping to the end of s if it has fewer runes.
inline std::string trimRuneCount(std::string_view s, int runeCount) {
	size_t i = 0;
	for (int n = 0; n < runeCount; n++) {
		if (i >= s.size()) {
			break;
		}
		int width = 0;
		decodeUtf8Rune(s.substr(i), &width);
		i += width;
	}
	return std::string(s.substr(i));
}

// TrimFilePathPrefix — path.go:629. Removes prefix from the start of path,
// honoring useCaseSensitiveFileNames the same way GetCanonicalFileName does.
// Returns (remainder, true) if path starts with prefix; otherwise (path,
// false). Must not slice by byte length: case-folding can change UTF-8 byte
// length without changing rune count (e.g. the Kelvin sign).
inline std::pair<std::string, bool> trimFilePathPrefix(
    std::string_view path, std::string_view prefix,
    bool useCaseSensitiveFileNames) {
	if (useCaseSensitiveFileNames) {
		if (path.starts_with(prefix)) {
			return {std::string(path.substr(prefix.size())), true};
		}
		return {std::string(path), false};
	}
	std::string canonicalPrefix =
	    getCanonicalFileName(prefix, /*useCaseSensitiveFileNames*/ false);
	if (!getCanonicalFileName(path, false).starts_with(canonicalPrefix)) {
		return {std::string(path), false};
	}
	int runeCount = 0;
	for (size_t i = 0; i < canonicalPrefix.size();) {
		int width = 0;
		decodeUtf8Rune(std::string_view(canonicalPrefix).substr(i), &width);
		i += width;
		runeCount++;
	}
	return {trimRuneCount(path, runeCount), true};
}


// comparePaths — path.go ComparePaths.
inline int comparePaths(std::string_view a, std::string_view b,
                        const ComparePathsOptions& options) {
	auto as = combinePaths(options.currentDirectory, {a});
	auto bs = combinePaths(options.currentDirectory, {b});
	a = as;
	b = bs;

	if (a == b) return 0;
	if (a.empty()) return -1;
	if (b.empty()) return 1;

	// Shortcut if the root segments differ: no need for path reduction.
	auto aRoot = a.substr(0, getRootLength(a));
	auto bRoot = b.substr(0, getRootLength(b));
	auto result = stringutil::CompareStringsCaseInsensitive(aRoot, bRoot);
	if (result != 0) return result;

	// Shortcut if there are no relative path segments in the non-root
	// portion.
	auto aRest = a.substr(aRoot.size());
	auto bRest = b.substr(bRoot.size());
	auto comparer = stringutil::GetStringComparer(
	    !options.useCaseSensitiveFileNames);
	if (!hasRelativePathSegment(aRest) && !hasRelativePathSegment(bRest)) {
		return comparer(aRest, bRest);
	}

	// The path contains a relative path segment. Normalize the paths and
	// perform a slower component-by-component comparison.
	auto aComponents = reducePathComponents(getPathComponents(a, ""));
	auto bComponents = reducePathComponents(getPathComponents(b, ""));
	size_t sharedLength = std::min(aComponents.size(), bComponents.size());
	for (size_t i = 1; i < sharedLength; i++) {
		result = comparer(aComponents[i], bComponents[i]);
		if (result != 0) return result;
	}
	return aComponents.size() < bComponents.size()   ? -1
	       : aComponents.size() > bComponents.size() ? 1
	                                                : 0;
}

// containsPath — whether child is contained within (or equal to) parent.
inline bool containsPath(std::string_view parent, std::string_view child,
                         const ComparePathsOptions& options) {
	auto ps = combinePaths(options.currentDirectory, {parent});
	auto cs = combinePaths(options.currentDirectory, {child});
	parent = ps;
	child = cs;
	if (parent.empty() || child.empty()) return false;
	if (parent == child) return true;
	auto parentComponents =
	    reducePathComponents(getPathComponents(parent, ""));
	auto childComponents =
	    reducePathComponents(getPathComponents(child, ""));
	(void)ps;
	(void)cs;
	if (childComponents.size() < parentComponents.size()) return false;

	auto componentComparer = options.equalityComparer();
	for (size_t i = 0; i < parentComponents.size(); i++) {
		bool equal;
		if (i == 0) {
			equal = equateStringCaseInsensitive(parentComponents[i],
			                                    childComponents[i]);
		} else {
			equal = componentComparer(parentComponents[i],
			                          childComponents[i]);
		}
		if (!equal) return false;
	}

	return true;
}

// ForEachAncestorDirectory — calls `callback` on `directory` and each
// ancestor; returns the first (result, true) outcome.
template <typename T>
inline std::pair<T, bool> forEachAncestorDirectory(
    std::string_view directory,
    const std::function<std::pair<T, bool>(std::string_view)>& callback) {
	std::string dir{directory};
	while (true) {
		auto [result, stop] = callback(dir);
		if (stop) {
			return {std::move(result), true};
		}

		auto parentPath = getDirectoryPath(dir);
		if (parentPath == dir) {
			return {T{}, false};
		}

		dir = std::string{parentPath};
	}
}

// ForEachAncestorDirectoryStoppingAtGlobalCache — stops at the global cache
// location.
template <typename T>
inline T forEachAncestorDirectoryStoppingAtGlobalCache(
    std::string_view globalCacheLocation, std::string_view directory,
    const std::function<std::pair<T, bool>(std::string_view)>& callback) {
	auto [result, _] = forEachAncestorDirectory<T>(
	    directory, [&](std::string_view ancestorDirectory) {
		    auto [result, stop] = callback(ancestorDirectory);
		    if (stop || ancestorDirectory == globalCacheLocation) {
			    return std::pair{std::move(result), true};
		    }
		    return std::pair{std::move(result), false};
	    });
	return std::move(result);
}

// path.go: GetNormalizedAbsolutePathWithoutRoot (program slice).
inline std::string getNormalizedAbsolutePathWithoutRoot(
    std::string_view fileName, std::string_view currentDirectory) {
	std::string absolutePath =
	    getNormalizedAbsolutePath(fileName, currentDirectory);
	int rootLength = getRootLength(absolutePath);
	return absolutePath.substr(rootLength);
}

// ConvertToRelativePath — path.go:821.
inline std::string convertToRelativePath(
    std::string_view absoluteOrRelativePath,
    const ComparePathsOptions& options) {
	if (!isRootedDiskPath(absoluteOrRelativePath)) {
		return std::string(absoluteOrRelativePath);
	}
	return getRelativePathToDirectoryOrUrl(
	    options.currentDirectory, absoluteOrRelativePath, false, options);
}

// === slice: project ===

// pathContainsPath — path.go:1088 `(p Path) ContainsPath(child Path)`. Both
// paths are canonicalized tspath.Path values (from toPath).
inline bool pathContainsPath(const Path& p, const Path& child) {
	if (p.empty()) {
		return false;
	}
	return p == child ||
	       (child.size() > p.size() && child.compare(0, p.size(), p) == 0 &&
	        (p.back() == '/' || child[p.size()] == '/'));
}


// getCommonParentsWorker — path.go. Recursive core of GetCommonParents:
// walks component groups left to right, fanning out into per-head groups
// whenever the groups diverge before minComponents.
inline std::vector<std::vector<std::string>> getCommonParentsWorker(
    const std::vector<std::vector<std::string>>& componentGroups,
    int minComponents, const ComparePathsOptions& options) {
	if (componentGroups.empty()) {
		return {};
	}
	// Determine the maximum depth we can consider
	size_t maxDepth = componentGroups[0].size();
	for (size_t i = 1; i < componentGroups.size(); i++) {
		if (componentGroups[i].size() < maxDepth) {
			maxDepth = componentGroups[i].size();
		}
	}

	auto equality = options.equalityComparer();
	for (size_t lastCommonIndex = 0; lastCommonIndex < maxDepth;
	     lastCommonIndex++) {
		const std::string& candidate = componentGroups[0][lastCommonIndex];
		for (size_t j = 1; j < componentGroups.size(); j++) {
			const auto& comps = componentGroups[j];
			if (!equality(candidate, comps[lastCommonIndex])) { // divergence
				if (static_cast<int>(lastCommonIndex) < minComponents) {
					// Not enough components, we need to fan out
					struct Group {
						std::vector<std::string> head;
						std::vector<std::vector<std::string>> tails;
					};
					std::vector<Path> orderedGroups;
					std::unordered_map<Path, Group> newGroups;
					for (const auto& g : componentGroups) {
						Path key = toPath(g[lastCommonIndex],
						                  options.currentDirectory,
						                  options.useCaseSensitiveFileNames);
						if (newGroups.find(key) == newGroups.end()) {
							orderedGroups.push_back(key);
						}
						auto& group = newGroups[key];
						group.head.assign(
						    g.begin(),
						    g.begin() + lastCommonIndex + 1);
						group.tails.emplace_back(
						    g.begin() + lastCommonIndex + 1, g.end());
					}
					std::sort(orderedGroups.begin(), orderedGroups.end());
					std::vector<std::vector<std::string>> result;
					result.reserve(newGroups.size());
					for (const auto& key : orderedGroups) {
						const auto& group = newGroups[key];
						auto subResults = getCommonParentsWorker(
						    group.tails,
						    minComponents -
						        static_cast<int>(lastCommonIndex + 1),
						    options);
						for (const auto& sr : subResults) {
							if (sr.empty()) {
								result.push_back(group.head);
							} else {
								std::vector<std::string> concat =
								    group.head;
								concat.insert(concat.end(), sr.begin(),
								              sr.end());
								result.push_back(std::move(concat));
							}
						}
					}
					return result;
				}
				return {std::vector<std::string>(
				    componentGroups[0].begin(),
				    componentGroups[0].begin() + lastCommonIndex)};
			}
		}
	}

	return {std::vector<std::string>(componentGroups[0].begin(),
	                                 componentGroups[0].begin() + maxDepth)};
}

// getCommonParents — path.go:1158.
//	/a/b/c/d, /a/b/c/e, /a/b/f/g, /x/y  =>  /
//	/a/b/c/d, /a/b/c/e, /a/b/f/g, /x/y  (minComponents: 2)  =>  /a/b, /x/y
//	c:/a/b/c/d, d:/a/b/c/d =>  c:/a/b/c/d, d:/a/b/c/d
inline std::pair<std::vector<std::string>,
               std::unordered_set<std::string>>
getCommonParents(
    const std::vector<std::string>& paths, int minComponents,
    const std::function<std::vector<std::string>(std::string_view,
                                                 std::string_view)>&
        getPathComponents,
    const ComparePathsOptions& options) {
	if (minComponents < 1) {
		tscUnreachable("minComponents must be at least 1");
	}
	if (paths.empty()) {
		return {{}, {}};
	}
	if (paths.size() == 1) {
		if (reducePathComponents(
		        getPathComponents(paths[0], options.currentDirectory))
		        .size() < static_cast<size_t>(minComponents)) {
			return {{}, {paths[0]}};
		}
		return {paths, {}};
	}

	std::unordered_set<std::string> ignored;
	std::vector<std::vector<std::string>> pathComponents;
	pathComponents.reserve(paths.size());
	for (const auto& path : paths) {
		auto components = reducePathComponents(
		    getPathComponents(path, options.currentDirectory));
		if (components.size() < static_cast<size_t>(minComponents)) {
			ignored.insert(path);
		} else {
			pathComponents.push_back(std::move(components));
		}
	}

	auto results = getCommonParentsWorker(pathComponents, minComponents,
	                                      options);
	std::vector<std::string> resultPaths(results.size());
	for (size_t i = 0; i < results.size(); i++) {
		std::vector<std::string_view> views(results[i].begin(),
		                                    results[i].end());
		resultPaths[i] = getPathFromPathComponents(views);
	}

	return {resultPaths, ignored};
}

// === slice: project ===
// ContainsIgnoredPath — ignoredpaths.go:11.
inline bool containsIgnoredPath(std::string_view path) {
	static const std::vector<std::string_view> ignoredPaths = {
	    "/node_modules/.", "/.git", ".#",
	};
	for (auto pattern : ignoredPaths) {
		if (path.find(pattern) != std::string_view::npos) {
			return true;
		}
	}
	return false;
}
// === end slice: project ===

}  // namespace tsc::tspath
