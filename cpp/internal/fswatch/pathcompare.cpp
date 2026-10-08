// pathcompare.go + pathkey.go + canonicalize_other.go — port of
// tsc/internal/fswatch's path comparison and key machinery.

#include "internal/fswatch/fswatch.h"

#include "internal/stringutil/stringutil.h"

#include <algorithm>
#include <cstring>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <unistd.h>
#endif

namespace tsc::fswatch {

#ifdef __APPLE__
// --- canonicalize_darwin.go + fsevents_darwin_ffi.go -------------------------
// (build tag: darwin — CFString provides NFC and case folding)

namespace {

// isASCII — fsevents_darwin_ffi.go.
bool isASCII(std::string_view s) {
	for (char c : s) {
		if (static_cast<unsigned char>(c) >= 0x80) {
			return false;
		}
	}
	return true;
}

// cfStringToUTF8 — fsevents_darwin_ffi.go's cfStringToGo.
std::string cfStringToUTF8(CFStringRef cf) {
	if (cf == nullptr) {
		return "";
	}
	CFIndex len = CFStringGetLength(cf);
	CFIndex used = 0;
	CFStringGetBytes(cf, CFRangeMake(0, len), kCFStringEncodingUTF8, '?',
	                 false, nullptr, 0, &used);
	std::string out(static_cast<size_t>(used), '\0');
	CFStringGetBytes(cf, CFRangeMake(0, len), kCFStringEncodingUTF8, '?',
	                 false, reinterpret_cast<UInt8*>(out.data()),
	                 static_cast<CFIndex>(out.size()), &used);
	out.resize(static_cast<size_t>(used));
	return out;
}

CFStringRef cfStringCreateUTF8(std::string_view s) {
	std::string cstr{s};
	return CFStringCreateWithCString(nullptr, cstr.c_str(),
	                                 kCFStringEncodingUTF8);
}

} // namespace

// normalizeNFC — fsevents_darwin_ffi.go:319. Returns s in Unicode NFC.
// ASCII inputs are returned unchanged. If any step fails (invalid UTF-8
// from a corrupt path), the original string is returned so the caller
// still sees *something* rather than nothing.
std::string normalizeNFC(std::string_view s) {
	if (isASCII(s)) {
		return std::string{s};
	}
	CFStringRef src = cfStringCreateUTF8(s);
	if (src == nullptr) {
		return std::string{s};
	}
	CFMutableStringRef mut = CFStringCreateMutableCopy(nullptr, 0, src);
	CFRelease(src);
	if (mut == nullptr) {
		return std::string{s};
	}
	CFStringNormalize(mut, kCFStringNormalizationFormC);
	std::string normalized = cfStringToUTF8(mut);
	CFRelease(mut);
	if (normalized.empty()) {
		return std::string{s};
	}
	return normalized;
}

// foldNativePath — fsevents_darwin_ffi.go:200. A comparison form, never a
// displayed or opened path. Case folding expands sharp s and ligatures
// without making diacritics, dotless i, circled letters, or character
// widths interchangeable.
std::string foldNativePath(std::string_view s) {
	if (isASCII(s)) {
		std::string out;
		stringutil::toLowerASCII(s, out);
		return out;
	}
	if (!validUtf8(s) || s.find('\0') != std::string_view::npos) {
		return "";
	}
	CFStringRef src = cfStringCreateUTF8(s);
	if (src == nullptr) {
		TSC_UNREACHABLE(
		    "fswatch: cannot create CFString for path folding");
	}
	CFMutableStringRef mut = CFStringCreateMutableCopy(nullptr, 0, src);
	CFRelease(src);
	if (mut == nullptr) {
		TSC_UNREACHABLE(
		    "fswatch: cannot copy CFString for path folding");
	}
	// Normalize before folding as well: a decomposed capital I with
	// dot must have the same comparison form as precomposed dotted
	// capital I.
	CFStringNormalize(mut, kCFStringNormalizationFormC);
	CFStringFold(mut, kCFCompareCaseInsensitive, nullptr);
	CFStringNormalize(mut, kCFStringNormalizationFormC);
	std::string folded = cfStringToUTF8(mut);
	CFRelease(mut);
	if (folded.empty()) {
		TSC_UNREACHABLE("fswatch: cannot extract folded CFString");
	}
	return folded;
}

// canonicalizePath — canonicalize_darwin.go:15.
std::string canonicalizePath(const std::string& p) { return normalizeNFC(p); }

// watcher.pathComparer — canonicalize_darwin.go:17-24.
std::pair<fswatch::pathComparer, gostd::Error>
watcher::pathComparer(const std::string& dir) const {
	if (watcherName != "fsevents" && watcherName != "kqueue") {
		return {fswatch::pathComparer{}, nullptr};
	}
	auto [c, err] = PathComparerForPath(dir);
	return {c.comparer, err};
}

// PathComparerForPath — canonicalize_darwin.go:27-36. Queries an existing
// path's volume rather than assuming every mounted volume is
// case-insensitive.
std::pair<PathComparer, gostd::Error>
PathComparerForPath(const std::string& path) {
	constexpr int pcCaseSensitive = _PC_CASE_SENSITIVE; // 11
	long sensitive = ::pathconf(path.c_str(), pcCaseSensitive);
	if (sensitive < 0) {
		return {PathComparer{},
		        osPathError("pathconf", path, errno)};
	}
	PathComparer c;
	c.comparer.ignoreCase = (sensitive == 0);
	return {c, nullptr};
}

#else
// --- canonicalize_other.go ---------------------------------------------------
// (build tag: !(darwin && (amd64 || arm64)) — the Linux variant)

[[noreturn]] void foldNativePathPanic() {
	TSC_UNREACHABLE(
	    "fswatch: native path folding is only available on Darwin");
}

std::string foldNativePath(std::string_view) {
	foldNativePathPanic();
}

// canonicalizePath is a no-op on platforms whose watchers report paths
// using the same bytes the caller provided. See canonicalize_darwin.go
// for the rationale on macOS.
std::string canonicalizePath(const std::string& p) { return p; }

// watcher.pathComparer — canonicalize_other.go:16-18.
std::pair<fswatch::pathComparer, gostd::Error>
watcher::pathComparer(const std::string& dir) const {
	return {fswatch::pathComparer{}, nullptr};
}

// PathComparerForPath returns exact comparison on platforms without native
// Darwin watch aliases. It does not inspect the host filesystem.
std::pair<PathComparer, gostd::Error>
PathComparerForPath(const std::string& path) {
	return {PathComparer{}, nullptr};
}
#endif

// --- pathcompare.go:14-51 ----------------------------------------------------

comparisonPath pathComparer::prepare(const std::string& path) const {
	comparisonPath p;
	p.path = path;
	if (ignoreCase && nativePathFolding) {
		p.fold();
	}
	return p;
}

std::string comparisonPath::fold() {
	if (!ready) {
		if (cache != nullptr) {
			auto it = cache->find(path);
			if (it != cache->end()) {
				folded = it->second;
				ready = true;
				return folded;
			}
		}
		folded = foldNativePath(path);
		ready = true;
		if (cache != nullptr) {
			// Go lazily allocates the map; ours is always materialized.
			(*cache)[path] = folded;
		}
	}
	return folded;
}

// --- pathcompare.go:53-159 ----------------------------------------------------

// suffix returns the part of path below root, respecting directory
// boundaries.
std::pair<std::string, bool>
pathComparer::suffix(const std::string& root, const std::string& path) const {
	comparisonPath p;
	p.path = path;
	comparisonPath r;
	r.path = root;
	return suffixPrepared(r, &p);
}

std::pair<std::string, bool>
pathComparer::suffixPrepared(comparisonPath root, comparisonPath* path) const {
	if (isInDirectoryOrSelf(root.path, path->path)) {
		return {path->path.substr(root.path.size()), true};
	}
	if (!ignoreCase || root.path.empty()) {
		return {"", false};
	}
	auto res = pathSuffixASCII(root.path, path->path);
	if (!res.unicode) {
		return {std::move(res.suffix), res.ok};
	}
	return suffixUnicode(root, path);
}

std::pair<std::string, bool>
pathComparer::suffixUnicode(comparisonPath root, comparisonPath* path) const {
	if (!nativePathFolding) {
		return pathSuffixFoldUnicode(root.path, path->path);
	}
	std::string a = root.fold();
	std::string b = path->fold();
	if (a.empty() || b.empty()) {
		// CFString cannot represent invalid UTF-8. Retain the simple-fold
		// behavior for malformed paths rather than truncating or losing
		// bytes.
		return pathSuffixFoldUnicode(root.path, path->path);
	}
	if (!isInDirectoryOrSelf(a, b)) {
		return {"", false};
	}
	if (a == b) {
		return {"", true};
	}
	// Folding and canonical normalization preserve separators, but not byte
	// lengths. Find the matching boundary in the original event, not its
	// fold.
	size_t offset = 0;
	size_t separators =
	    std::count(root.path.begin(), root.path.end(), '/');
	bool trailingSeparator = root.path[root.path.size() - 1] == '/';
	if (!trailingSeparator) {
		separators++;
	}
	for (size_t i = 0; i < separators; i++) {
		size_t idx = path->path.find('/', offset);
		if (idx == std::string::npos) {
			TSC_UNREACHABLE(
			    "fswatch: folded path lost a directory boundary");
		}
		offset = idx + 1;
	}
	if (trailingSeparator) {
		return {path->path.substr(offset), true};
	}
	return {path->path.substr(offset - 1), true};
}

// The third result requests Unicode comparison; an ASCII rejection must not
// reject an expanding alias just because the other spelling is ASCII.
pathSuffixASCIIResult pathSuffixASCII(std::string_view root,
                                      std::string_view path) {
	size_t i = 0;
	// Skip shared prefixes a word at a time, which is common when routing an
	// event past sibling watches. String slice comparisons do not allocate.
	while (i + 8 <= root.size() && i + 8 <= path.size() &&
	       std::memcmp(root.data() + i, path.data() + i, 8) == 0) {
		i += 8;
	}
	for (; i < root.size() && i < path.size(); i++) {
		uint8_t a = static_cast<uint8_t>(root[i]);
		uint8_t b = static_cast<uint8_t>(path[i]);
		if (a >= tsc::kRuneSelf || b >= tsc::kRuneSelf) {
			return {"", false, true};
		}
		if (a == b) {
			continue;
		}
		a |= 0x20;
		b |= 0x20;
		if (a != b || a < 'a' || a > 'z') {
			return {"", false, false};
		}
	}
	if (i == root.size() &&
	    (i == path.size() || path[i] == '/')) {
		return {std::string(path.substr(i)), true, false};
	}
	return {"", false,
	        (i < root.size() &&
	         static_cast<uint8_t>(root[i]) >= tsc::kRuneSelf) ||
	            (i < path.size() &&
	             static_cast<uint8_t>(path[i]) >= tsc::kRuneSelf)};
}

// Comparing the remaining components avoids assuming case-equivalent UTF-8
// strings have the same byte length (for example, s and long s).
std::pair<std::string, bool> pathSuffixFoldUnicode(std::string_view root,
                                                   std::string_view path) {
	for (;;) {
		// strings.Cut(root, "/")
		size_t rootCut = root.find('/');
		std::string_view rootPart =
		    rootCut == std::string_view::npos ? root : root.substr(0, rootCut);
		std::string_view rootRest;
		bool rootMore = rootCut != std::string_view::npos;
		if (rootMore) {
			rootRest = root.substr(rootCut + 1);
		}
		size_t pathCut = path.find('/');
		std::string_view pathPart =
		    pathCut == std::string_view::npos ? path : path.substr(0, pathCut);
		std::string_view pathRest;
		bool pathMore = pathCut != std::string_view::npos;
		if (pathMore) {
			pathRest = path.substr(pathCut + 1);
		}

		if (!equalFold(rootPart, pathPart)) {
			return {"", false};
		}
		if (!rootMore) {
			if (pathMore) {
				return {std::string(path.substr(pathPart.size())), true};
			}
			return {"", true};
		}
		if (!pathMore) {
			return {"", false};
		}
		root = rootRest;
		path = pathRest;
	}
}

bool pathComparer::contains(const std::string& root,
                            const std::string& path) const {
	auto [_, ok] = suffix(root, path);
	return ok;
}

std::pair<std::string, bool>
pathComparer::rebase(const std::string& path, const std::string& from,
                     const std::string& to) const {
	comparisonPath p;
	p.path = path;
	comparisonPath f;
	f.path = from;
	return rebasePrepared(&p, f, to);
}

std::pair<std::string, bool>
pathComparer::rebasePrepared(comparisonPath* path, comparisonPath from,
                             const std::string& to) const {
	if (isInDirectoryOrSelf(from.path, path->path)) {
		return {rebasePath(path->path, from.path, to), true};
	}
	if (!ignoreCase || from.path.empty()) {
		return {"", false};
	}
	std::string suffix;
	bool ok;
	auto res = pathSuffixASCII(from.path, path->path);
	if (res.unicode) {
		auto p = suffixUnicode(from, path);
		suffix = std::move(p.first);
		ok = p.second;
	} else {
		suffix = std::move(res.suffix);
		ok = res.ok;
	}
	if (!ok) {
		return {"", false};
	}
	return {joinPathSuffix(to, suffix), true};
}

// --- pathkey.go -----------------------------------------------------------------

// Key returns a watch-only comparison key, not a filesystem path or a
// compiler identity. Native Darwin comparers use the same CoreFoundation
// folding as the watcher. Other comparers preserve bytes, including
// malformed UTF-8.
std::string PathComparer::key(const std::string& path) const {
	if (!comparer.ignoreCase || !nativePathFolding) {
		return path;
	}
	if (path.find('\0') != std::string::npos) {
		return path;
	}
	if (std::string folded = foldNativePath(path); !folded.empty()) {
		return folded;
	}
	// Invalid UTF-8 and NUL-containing names are opaque, not Unicode
	// aliases.
	return path;
}

// Rebase replaces a matching directory prefix while preserving the spelling
// and byte boundaries of the remaining event path.
std::pair<std::string, bool> PathComparer::rebase(const std::string& path,
                                                  const std::string& from,
                                                  const std::string& to) const {
	if (!validUtf8(path) || !validUtf8(from) ||
	    path.find('\0') != std::string::npos ||
	    from.find('\0') != std::string::npos) {
		return fswatch::pathComparer{}.rebase(path, from, to);
	}
	return comparer.rebase(path, from, to);
}

} // namespace tsc::fswatch
