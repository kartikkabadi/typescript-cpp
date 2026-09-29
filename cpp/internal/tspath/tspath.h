// tspath — minimal port (extension helpers)
#pragma once
#include <string>
#include <string_view>

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
