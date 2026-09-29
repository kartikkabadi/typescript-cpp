// tspath — minimal port (extension helpers)
#pragma once
#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

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
		auto authorityLength = path.find('/', authorityStart);
		if (authorityLength != std::string_view::npos) {
			// URL: "file:///", "file://server/", "file://server/path"
			int authorityEnd = authorityStart + (int)authorityLength;

			// For local "file" URLs, include the leading DOS volume (if
			// present). Per https://www.ietf.org/rfc/rfc1738.txt, a host of
			// "" or "localhost" is a special case interpreted as "the
			// machine from which the URL is being interpreted".
			std::string_view scheme = path.substr(0, schemeEnd);
			std::string_view authority =
				path.substr(authorityStart, authorityLength);
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

inline bool equateStringCaseInsensitive(std::string_view a, std::string_view b) {
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); i++) {
		auto lower = [](char c) -> char {
			return (c >= 'A' && c <= 'Z') ? c + 32 : c;
		};
		if (lower(a[i]) != lower(b[i])) return false;
	}
	return true;
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

inline std::vector<std::string_view> getPathComponents(
    std::string_view path, std::string_view currentDirectory) {
	auto combined = combinePaths(currentDirectory, {path});
	return pathComponents(combined, getRootLength(combined));
}

inline std::vector<std::string_view> reducePathComponents(
    const std::vector<std::string_view>& components) {
	if (components.empty()) return {};
	std::vector<std::string_view> reduced{components[0]};
	for (size_t i = 1; i < components.size(); i++) {
		auto component = components[i];
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
	// Non-ASCII: fold each UTF-8 rune except U+0130. Cases needing wide
	// lowercasing are rare in file names; decode and ASCII-fold the common
	// subset, pass other multi-byte sequences through.
	std::string result;
	result.reserve(fileName.size());
	for (size_t i = 0; i < fileName.size(); i++) {
		char c = fileName[i];
		if ((uint8_t)c < 0x80) {
			result += (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
		} else {
			result += c;
		}
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

}  // namespace tsc::tspath
