// vfsmatch — port of tsc/internal/vfs/vfsmatch/vfsmatch.go over the
// module::ResolutionHost interface (the Go version takes vfs.FS).
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "internal/module/types.h"
#include "internal/stringutil/stringutil.h"
#include "internal/tspath/tspath.h"

namespace tsc::module::vfsmatch {

inline constexpr int UnlimitedDepth = std::numeric_limits<int>::max();

enum class Usage : int8_t { Files = 0, Directories, Exclude };

// IsImplicitGlob — "foo" is implicitly "foo/**/*" when its last component has
// no extension and no glob chars.
inline bool IsImplicitGlob(std::string_view lastPathComponent) {
	return lastPathComponent.find_first_of(".*?") == std::string_view::npos;
}

inline std::string getIncludeBasePath(std::string_view absolute) {
	auto wildcardOffset = absolute.find_first_of("*?");
	if (wildcardOffset == std::string_view::npos) {
		// No "*" or "?" in the path
		if (!tspath::hasExtension(absolute)) {
			return std::string{absolute};
		}
		return std::string{tspath::removeTrailingDirectorySeparator(
		    tspath::getDirectoryPath(absolute))};
	}
	// Go: absolute[:max(LastIndex(absolute[:wildcardOffset], "/"), 0)]
	auto idx = absolute.substr(0, wildcardOffset).find_last_of('/');
	size_t cut = idx == std::string_view::npos ? 0 : idx;
	return std::string{absolute.substr(0, cut)};
}

// getBasePaths — unique non-wildcard base paths amongst the include patterns.
inline std::vector<std::string> getBasePaths(std::string_view path,
                                             const std::vector<std::string>& includes,
                                             bool useCaseSensitiveFileNames) {
	std::vector<std::string> basePaths{std::string{path}};

	if (!includes.empty()) {
		tspath::ComparePathsOptions comparePathsOptions;
		comparePathsOptions.currentDirectory = path;
		comparePathsOptions.useCaseSensitiveFileNames =
		    useCaseSensitiveFileNames;
		auto stringComparer = stringutil::GetStringComparer(
		    !useCaseSensitiveFileNames);

		std::vector<std::string> includeBasePaths;
		for (auto& include : includes) {
			// Check relative paths by converting them to absolute and
			// normalizing in case they escape the base path.
			std::string absolute;
			if (tspath::isRootedDiskPath(include)) {
				absolute = include;
			} else {
				absolute = tspath::normalizePath(
				    tspath::combinePaths(path, {include}));
			}
			includeBasePaths.push_back(getIncludeBasePath(absolute));
		}

		std::stable_sort(includeBasePaths.begin(), includeBasePaths.end(),
		                 [&](const std::string& a, const std::string& b) {
			                 return stringComparer(a, b) < 0;
		                 });

		for (auto& includeBasePath : includeBasePaths) {
			bool allNotContained = true;
			for (auto& basepath : basePaths) {
				if (tspath::containsPath(basepath, includeBasePath,
				                         comparePathsOptions)) {
					allNotContained = false;
					break;
				}
			}
			if (allNotContained) {
				basePaths.push_back(includeBasePath);
			}
		}
	}

	return basePaths;
}

// globPattern — compiled glob pattern for matching file paths.
struct globPattern;

enum class componentKind : int {
	literal,
	wildcard,
	doubleAsterisk,
};

enum class segmentKind : int { literal, star, question };

struct segment {
	segmentKind kind;
	std::string literal;
};

struct component {
	componentKind kind;
	std::string literal;
	std::vector<segment> segments;
	// Include patterns with wildcards skip common package folders
	// (node_modules, etc.)
	bool skipPackageFolders = false;
};

inline bool isHiddenPath(std::string_view name) {
	return !name.empty() && name[0] == '.';
}

inline bool isPackageFolder(std::string_view name) {
	switch (name.size()) {
	case 12:  // len("node_modules")
		return stringutil::EquateStringCaseInsensitive(name, "node_modules");
	case 13:  // len("jspm_packages")
		return stringutil::EquateStringCaseInsensitive(name, "jspm_packages");
	case 16:  // len("bower_components")
		return stringutil::EquateStringCaseInsensitive(name,
		                                             "bower_components");
	}
	return false;
}

inline std::vector<segment> parseSegments(std::string_view s) {
	std::vector<segment> result;
	size_t start = 0;
	for (size_t i = 0; i < s.size(); i++) {
		switch (s[i]) {
		case '*':
		case '?':
			if (i > start) {
				result.push_back(
				    {segmentKind::literal, std::string{s.substr(start, i - start)}});
			}
			if (s[i] == '*') {
				result.push_back({segmentKind::star, {}});
			} else {
				result.push_back({segmentKind::question, {}});
			}
			start = i + 1;
		}
	}
	if (start < s.size()) {
		result.push_back({segmentKind::literal, std::string{s.substr(start)}});
	}
	return result;
}

inline component parseComponent(std::string_view s, bool isInclude) {
	if (s == "**") {
		return component{componentKind::doubleAsterisk, {}, {}, false};
	}
	if (s.find_first_of("*?") == std::string_view::npos) {
		return component{componentKind::literal, std::string{s}, {}, false};
	}
	return component{componentKind::wildcard, {}, parseSegments(s),
	                 isInclude};
}

// nextPathPart — extracts the next path component from s at offset.
inline std::tuple<std::string_view, int, bool> nextPathPartSingle(
    std::string_view s, int offset) {
	if (offset >= static_cast<int>(s.size())) {
		return {"", offset, false};
	}
	if (offset == 0 && !s.empty() && s[0] == '/') {
		return {"", 1, true};
	}
	while (offset < static_cast<int>(s.size()) && s[offset] == '/') {
		offset++;
	}
	if (offset >= static_cast<int>(s.size())) {
		return {"", offset, false};
	}
	auto rest = s.substr(offset);
	auto idx = rest.find('/');
	if (idx != std::string_view::npos) {
		return {rest.substr(0, idx), offset + static_cast<int>(idx), true};
	}
	return {rest, static_cast<int>(s.size()), true};
}

inline std::tuple<std::string_view, int, bool> nextPathPartParts(
    std::string_view prefix, std::string_view suffix, int offset) {
	// Fast paths.
	if (suffix.empty()) {
		return nextPathPartSingle(prefix, offset);
	}
	if (prefix.empty()) {
		return nextPathPartSingle(suffix, offset);
	}

	int totalLen = static_cast<int>(prefix.size() + suffix.size());
	if (offset >= totalLen) {
		return {"", offset, false};
	}

	// Handle leading slash (root of absolute path).
	if (offset == 0 && !prefix.empty() && prefix[0] == '/') {
		return {"", 1, true};
	}

	// Scan within prefix.
	if (offset < static_cast<int>(prefix.size())) {
		while (offset < static_cast<int>(prefix.size()) &&
		       prefix[offset] == '/') {
			offset++;
		}
		if (offset < static_cast<int>(prefix.size())) {
			auto rest = prefix.substr(offset);
			auto idx = rest.find('/');
			// idx is guaranteed >= 0 for the call sites we care about
			// because prefix ends in '/'.
			return {rest.substr(0, idx),
			        offset + static_cast<int>(idx), true};
		}
		// Fall through into suffix region.
	}

	// Scan suffix: it's a single component.
	int sOff = offset - static_cast<int>(prefix.size());
	if (sOff >= static_cast<int>(suffix.size())) {
		return {"", offset, false};
	}
	return {suffix.substr(sOff), totalLen, true};
}

struct globPattern {
	std::vector<component> components;
	bool isExclude = false;
	bool caseSensitive = false;
	bool excludeMinJs = false;

	bool stringsEqual(std::string_view a, std::string_view b) const {
		if (caseSensitive) {
			return a == b;
		}
		return stringutil::EquateStringCaseInsensitive(a, b);
	}

	bool matches(std::string_view path) const {
		return matchPathParts(path, "", 0, 0, false);
	}
	bool matchesParts(std::string_view prefix, std::string_view suffix) const {
		return matchPathParts(prefix, suffix, 0, 0, false);
	}
	bool matchesPrefixParts(std::string_view prefix,
	                        std::string_view suffix) const {
		return matchPathParts(prefix, suffix, 0, 0, true);
	}

	bool patternSatisfied(size_t compIdx) const {
		for (size_t i = compIdx; i < components.size(); i++) {
			if (components[i].kind != componentKind::doubleAsterisk) {
				return false;
			}
		}
		return true;
	}

	bool matchPathParts(std::string_view prefix, std::string_view suffix,
	                    int pathOffset, size_t compIdx, bool prefixOnly) const {
		while (true) {
			auto [pathPart, nextOffset, ok] =
			    nextPathPartParts(prefix, suffix, pathOffset);
			if (!ok) {
				if (prefixOnly) {
					return true;
				}
				return patternSatisfied(compIdx);
			}

			if (compIdx >= components.size()) {
				return isExclude && !prefixOnly;
			}

			const component& comp = components[compIdx];
			switch (comp.kind) {
			case componentKind::doubleAsterisk:
				if (matchPathParts(prefix, suffix, pathOffset, compIdx + 1,
				                   prefixOnly)) {
					return true;
				}
				if (!isExclude &&
				    (isHiddenPath(pathPart) || isPackageFolder(pathPart))) {
					return false;
				}
				pathOffset = nextOffset;
				continue;
			case componentKind::literal:
				if (!stringsEqual(comp.literal, pathPart)) {
					return false;
				}
				break;
			case componentKind::wildcard:
				if (comp.skipPackageFolders && isPackageFolder(pathPart)) {
					return false;
				}
				if (!matchWildcard(comp.segments, pathPart)) {
					return false;
				}
				break;
			}

			pathOffset = nextOffset;
			compIdx++;
		}
	}

	bool matchWildcard(const std::vector<segment>& segs,
	                   std::string_view s) const {
		// Include patterns: wildcards at start cannot match hidden files.
		if (!isExclude && !segs.empty() && isHiddenPath(s) &&
		    (segs[0].kind == segmentKind::star ||
		     segs[0].kind == segmentKind::question)) {
			return false;
		}

		// Fast path: single * followed by literal suffix (e.g., "*.ts").
		if (segs.size() == 2 && segs[0].kind == segmentKind::star &&
		    segs[1].kind == segmentKind::literal) {
			auto sfx = segs[1].literal;
			if (s.size() < sfx.size() ||
			    !stringsEqual(sfx, s.substr(s.size() - sfx.size()))) {
				return false;
			}
			return shouldIncludeMinJs(s, segs);
		}

		return matchSegments(segs, s) && shouldIncludeMinJs(s, segs);
	}

	// matchSegments — iterative wildcard matching; O(n*m) with only the
	// last star position tracked for backtracking.
	bool matchSegments(const std::vector<segment>& segs,
	                   std::string_view s) const {
		size_t segIdx = 0, sIdx = 0;
		long starSegIdx = -1;
		size_t starSIdx = 0;

		while (sIdx < s.size()) {
			if (segIdx < segs.size()) {
				const segment& seg = segs[segIdx];
				switch (seg.kind) {
				case segmentKind::literal: {
					size_t end = sIdx + seg.literal.size();
					if (end <= s.size() &&
					    stringsEqual(seg.literal,
					                 s.substr(sIdx, seg.literal.size()))) {
						sIdx = end;
						segIdx++;
						continue;
					}
					break;
				}
				case segmentKind::question:
					if (s[sIdx] != '/') {
						int size;
						decodeUtf8Rune(s.substr(sIdx), &size);
						sIdx += size;
						segIdx++;
						continue;
					}
					break;
				case segmentKind::star:
					// Record star position for backtracking, then try
					// matching zero chars.
					starSegIdx = static_cast<long>(segIdx);
					starSIdx = sIdx;
					segIdx++;
					continue;
				}
			}

			// Current segment didn't match. Backtrack to last star if
			// possible.
			if (starSegIdx >= 0 && starSIdx < s.size() &&
			    s[starSIdx] != '/') {
				int size;
				decodeUtf8Rune(s.substr(starSIdx), &size);
				starSIdx += size;
				sIdx = starSIdx;
				segIdx = static_cast<size_t>(starSegIdx) + 1;
				continue;
			}

			return false;
		}

		// Consume any trailing stars.
		while (segIdx < segs.size() &&
		       segs[segIdx].kind == segmentKind::star) {
			segIdx++;
		}
		return segIdx >= segs.size();
	}

	bool shouldIncludeMinJs(std::string_view filename,
	                        const std::vector<segment>& segs) const {
		if (!excludeMinJs) {
			return true;
		}
		// - case-sensitive: only exact ".min.js" is excluded.
		// - case-insensitive: any casing variant is excluded.
		if (!hasMinJsSuffix(filename)) {
			return true;
		}
		if (patternMentionsMinSuffix(segs)) {
			return true;
		}
		return false;
	}

	bool hasMinJsSuffix(std::string_view filename) const {
		if (caseSensitive) {
			return filename.ends_with(".min.js");
		}
		constexpr std::string_view minJs = ".min.js";
		if (filename.size() < minJs.size()) {
			return false;
		}
		return stringutil::EquateStringCaseInsensitive(
		    filename.substr(filename.size() - minJs.size()), minJs);
	}

	bool patternMentionsMinSuffix(const std::vector<segment>& segs) const {
		for (auto& seg : segs) {
			if (seg.kind != segmentKind::literal) {
				continue;
			}
			std::string lit = seg.literal;
			if (!caseSensitive) {
				std::transform(lit.begin(), lit.end(), lit.begin(),
				               [](unsigned char c) {
					               return static_cast<char>(
					                   std::tolower(c));
				               });
			}
			if (lit.find(".min.js") != std::string::npos ||
			    lit.find(".min.") != std::string::npos) {
				return true;
			}
		}
		return false;
	}
};

// compileGlobPattern — compiles a glob spec; returns (pattern, false) if it
// would match nothing.
inline std::pair<globPattern, bool> compileGlobPattern(std::string_view spec,
                                                     std::string_view basePath,
                                                     Usage usage,
                                                     bool caseSensitive) {
	auto parts = tspath::getNormalizedPathComponents(spec, basePath);
	if (!parts.empty() &&
	    tspath::isEncodedDynamicFileName(parts[0])) {
		caseSensitive = true;
	}

	// "src/**" without a filename matches nothing (for include patterns).
	if (usage != Usage::Exclude && !parts.empty() && parts.back() == "**") {
		return {{}, false};
	}

	// Normalize root: "/home/" -> "/home".
	parts[0] = std::string{
	    tspath::removeTrailingDirectorySeparator(parts[0])};
	if (!parts.empty() &&
	    tspath::isEncodedDynamicFileName(parts[0])) {
		// Encoded dynamic roots carry the scheme/authority inside the
		// first component — split it so each segment matches on its
		// own (vfsmatch.go:155).
		std::vector<std::string> rootParts;
		size_t start = 0;
		while (true) {
			auto slash = parts[0].find('/', start);
			if (slash == std::string::npos) {
				rootParts.push_back(parts[0].substr(start));
				break;
			}
			rootParts.push_back(parts[0].substr(start, slash - start));
			start = slash + 1;
		}
		rootParts.insert(rootParts.end(),
		                 std::next(parts.begin()), parts.end());
		parts = std::move(rootParts);
	}

	// Directories implicitly match all files: "src" -> "src/**/*".
	if (!parts.empty() && IsImplicitGlob(parts.back())) {
		parts.push_back("**");
		parts.push_back("*");
	}

	globPattern p;
	p.isExclude = usage == Usage::Exclude;
	p.caseSensitive = caseSensitive;
	p.excludeMinJs = usage == Usage::Files;
	p.components.reserve(parts.size());

	for (auto& part : parts) {
		p.components.push_back(parseComponent(part, usage != Usage::Exclude));
	}
	return {p, true};
}

// globMatcher — combined include + exclude patterns.
struct globMatcher {
	std::vector<globPattern> includes;
	std::vector<globPattern> excludes;
	bool hadIncludes = false;

	// matchesFileParts — (include index, true) on match, (0, false) if not.
	std::pair<int, bool> matchesFileParts(std::string_view prefix,
	                                      std::string_view suffix) const {
		for (auto& e : excludes) {
			if (e.matchesParts(prefix, suffix)) {
				return {0, false};
			}
		}
		if (includes.empty()) {
			if (hadIncludes) {
				return {0, false};
			}
			return {0, true};
		}
		for (size_t i = 0; i < includes.size(); i++) {
			if (includes[i].matchesParts(prefix, suffix)) {
				return {static_cast<int>(i), true};
			}
		}
		return {0, false};
	}

	bool matchesDirectoryParts(std::string_view prefix,
	                           std::string_view suffix) const {
		for (auto& e : excludes) {
			if (e.matchesParts(prefix, suffix)) {
				return false;
			}
		}
		if (includes.empty()) {
			return !hadIncludes;
		}
		for (auto& i : includes) {
			if (i.matchesPrefixParts(prefix, suffix)) {
				return true;
			}
		}
		return false;
	}
};

inline globMatcher newGlobMatcher(const std::vector<std::string>& includeSpecs,
                                  const std::vector<std::string>& excludeSpecs,
                                  std::string_view basePath,
                                  bool caseSensitive, Usage usage) {
	globMatcher m;
	m.hadIncludes = !includeSpecs.empty();

	for (auto& spec : includeSpecs) {
		if (auto [p, ok] =
		        compileGlobPattern(spec, basePath, usage, caseSensitive);
		    ok) {
			m.includes.push_back(std::move(p));
		}
	}
	for (auto& spec : excludeSpecs) {
		if (auto [p, ok] = compileGlobPattern(spec, basePath,
		                                      Usage::Exclude, caseSensitive);
		    ok) {
			m.excludes.push_back(std::move(p));
		}
	}
	return m;
}

inline std::string ensureTrailingSlash(std::string_view s) {
	std::string r{s};
	if (!r.empty() && r.back() != '/') {
		r += '/';
	}
	return r;
}

// globVisitor — walks a directory tree, collecting matching files.
struct globVisitor {
	ResolutionHost* host;
	const globMatcher* fileMatcher;
	const globMatcher* directoryMatcher;
	const std::vector<std::string_view>* extensions;
	bool useCaseSensitiveFileNames;
	std::unordered_set<std::string> visited;
	std::vector<std::vector<std::string>> results;

	// resolvedRealPath, when non-empty, is the already-resolved real path
	// for this directory (computed incrementally from the parent). When
	// empty, Realpath is called to resolve symlinks.
	void visit(std::string_view path, std::string_view absolutePath, int depth,
	           std::string_view resolvedRealPath) {
		// Detect symlink cycles.
		std::string realPath;
		if (!resolvedRealPath.empty()) {
			realPath = std::string{resolvedRealPath};
		} else {
			realPath = host->Realpath(absolutePath);
		}
		auto canonicalPath = tspath::getCanonicalFileName(
		    realPath, useCaseSensitiveFileNames);
		if (visited.count(canonicalPath)) {
			return;
		}
		visited.insert(canonicalPath);

		auto entries = host->GetAccessibleEntries(absolutePath);

		auto pathPrefix = ensureTrailingSlash(path);
		auto absPrefix = ensureTrailingSlash(absolutePath);

		for (auto& file : entries.files) {
			if (!extensions->empty() &&
			    !tspath::fileExtensionIsOneOf(file, *extensions)) {
				continue;
			}
			if (auto [idx, ok] =
			        fileMatcher->matchesFileParts(absPrefix, file);
			    ok) {
				results[idx].push_back(pathPrefix + file);
			}
		}

		if (depth != UnlimitedDepth) {
			depth--;
			if (depth == 0) {
				return;
			}
		}

		for (auto& dir : entries.directories) {
			if (!directoryMatcher->matchesDirectoryParts(absPrefix, dir)) {
				continue;
			}
			auto absDir = absPrefix + dir;
			std::string childRealPath;
			if (entries.symlinks.has_value()) {
				if (!entries.symlinks->count(dir)) {
					// Non-symlink directory: compute realpath
					// incrementally.
					childRealPath =
					    tspath::combinePaths(realPath, {dir});
				}
				// else: symlink directory; leave childRealPath empty to
				// force a Realpath call.
			}
			// If symlinks is nullopt, the FS doesn't track symlinks;
			// leave childRealPath empty to call Realpath.
			visit(pathPrefix + dir, absDir, depth, childRealPath);
		}
	}
};

inline std::vector<std::string> matchFiles(
    std::string_view path, const std::vector<std::string_view>& extensions,
    const std::vector<std::string>& excludes,
    const std::vector<std::string>& includes, bool useCaseSensitiveFileNames,
    std::string_view currentDirectory, int depth, ResolutionHost* host) {
	auto normalizedPath = tspath::normalizePath(path);
	auto normalizedCwd = tspath::normalizePath(currentDirectory);
	auto absolutePath =
	    tspath::combinePaths(normalizedCwd, {normalizedPath});

	auto fileMatcher = newGlobMatcher(includes, excludes, absolutePath,
	                                  useCaseSensitiveFileNames,
	                                  Usage::Files);
	auto directoryMatcher = newGlobMatcher(includes, excludes, absolutePath,
	                                       useCaseSensitiveFileNames,
	                                       Usage::Directories);

	globVisitor v;
	v.host = host;
	v.fileMatcher = &fileMatcher;
	v.directoryMatcher = &directoryMatcher;
	v.extensions = &extensions;
	v.useCaseSensitiveFileNames = useCaseSensitiveFileNames;
	v.results.resize(std::max(fileMatcher.includes.size(), size_t{1}));

	for (auto& basePath :
	     getBasePaths(normalizedPath, includes, useCaseSensitiveFileNames)) {
		v.visit(basePath, tspath::combinePaths(normalizedCwd, {basePath}),
		        depth, "");
	}

	// Fast path: a single include bucket doesn't need flattening.
	if (v.results.size() == 1) {
		return std::move(v.results[0]);
	}
	std::vector<std::string> out;
	for (auto& bucket : v.results) {
		out.insert(out.end(), bucket.begin(), bucket.end());
	}
	return out;
}

// ReadDirectory — vfsmatch.go.
inline std::vector<std::string> ReadDirectory(
    ResolutionHost* host, std::string_view currentDir, std::string_view path,
    const std::vector<std::string_view>& extensions,
    const std::vector<std::string>& excludes,
    const std::vector<std::string>& includes, int depth) {
	return matchFiles(path, extensions, excludes, includes,
	                  host->UseCaseSensitiveFileNames(), currentDir, depth,
	                  host);
}

// SpecMatcher — wraps multiple glob patterns for matching paths.
struct SpecMatcher {
	std::vector<globPattern> patterns;

	bool MatchString(std::string_view path) const {
		for (auto& p : patterns) {
			if (p.matches(path)) return true;
		}
		return false;
	}

	int MatchIndex(std::string_view path) const {
		for (size_t i = 0; i < patterns.size(); i++) {
			if (patterns[i].matches(path)) return static_cast<int>(i);
		}
		return -1;
	}
};

inline SpecMatcher* NewSpecMatcher(const std::vector<std::string>& specs,
                                   std::string_view basePath, Usage usage,
                                   bool useCaseSensitiveFileNames) {
	if (specs.empty()) {
		return nullptr;
	}
	std::vector<globPattern> patterns;
	patterns.reserve(specs.size());
	for (auto& spec : specs) {
		if (auto [p, ok] = compileGlobPattern(spec, basePath, usage,
		                                      useCaseSensitiveFileNames);
		    ok) {
			patterns.push_back(std::move(p));
		}
	}
	if (patterns.empty()) {
		return nullptr;
	}
	return new SpecMatcher{std::move(patterns)};
}

}  // namespace tsc::module::vfsmatch
