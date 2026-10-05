// vfsmatch.h — port of tsc/internal/vfs/vfsmatch/vfsmatch.go: the glob
// matching algorithm over vfs::FS (see tsc/MATCHING_ALGORITHM.md).
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "internal/ast/ast.h" // TSC_UNREACHABLE
#include "internal/stringutil/stringutil.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfs.h"

namespace tsc::vfs::vfsmatch {

// Usage — vfsmatch.go:19-25.
enum class Usage : int8_t { Files = 0, Directories, Exclude };

// Usage.String() — stringer_generated.go.
std::string usageString(Usage u);

// UnlimitedDepth can be passed as the depth argument to indicate there is
// no depth limit.
inline constexpr int UnlimitedDepth = std::numeric_limits<int>::max();

// IsImplicitGlob checks if a path component is implicitly a glob. An
// "includes" path "foo" is implicitly a glob "foo/** /*" (without the
// space) if its last component has no extension, and does not contain any
// glob characters itself.
inline bool isImplicitGlob(std::string_view lastPathComponent) {
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
	// absolute[:max(LastIndex(absolute[:wildcardOffset], "/"), 0)]
	auto idx = absolute.substr(0, wildcardOffset).find_last_of('/');
	size_t cut = idx == std::string_view::npos ? 0 : idx;
	return std::string{absolute.substr(0, cut)};
}

// getBasePaths computes the unique non-wildcard base paths amongst the
// provided include patterns.
inline std::vector<std::string> getBasePaths(
    std::string_view path, const std::vector<std::string>& includes,
    bool useCaseSensitiveFileNames) {
	// Storage for our results in the form of literal paths (e.g. the paths
	// as written by the user).
	std::vector<std::string> basePaths{std::string{path}};

	if (!includes.empty()) {
		tspath::ComparePathsOptions comparePathsOptions;
		comparePathsOptions.currentDirectory = std::string{path};
		comparePathsOptions.useCaseSensitiveFileNames =
		    useCaseSensitiveFileNames;
		auto stringComparer =
		    stringutil::GetStringComparer(!useCaseSensitiveFileNames);

		// Storage for literal base paths amongst the include patterns.
		std::vector<std::string> includeBasePaths;
		for (auto& include : includes) {
			// We also need to check the relative paths by converting
			// them to absolute and normalizing in case they escape the
			// base path (e.g "..\somedirectory")
			std::string absolute;
			if (tspath::isRootedDiskPath(include)) {
				absolute = include;
			} else {
				absolute = tspath::normalizePath(
				    tspath::combinePaths(path, {include}));
			}
			// Append the literal and canonical candidate base paths.
			includeBasePaths.push_back(getIncludeBasePath(absolute));
		}

		// Sort the offsets array using either the literal or canonical
		// path representations.
		std::stable_sort(includeBasePaths.begin(),
		                 includeBasePaths.end(),
		                 [&](const std::string& a, const std::string& b) {
			                 return stringComparer(a, b) < 0;
		                 });

		// Iterate over each include base path and include unique base
		// paths that are not a subpath of an existing base path.
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

// componentKind — vfsmatch.go component kinds.
enum class componentKind : int {
	literal,        // exact match (e.g., "src")
	wildcard,       // contains * or ? (e.g., "*.ts")
	doubleAsterisk, // ** matches zero or more directories
};

// segmentKind — vfsmatch.go segment kinds.
enum class segmentKind : int {
	literal,  // exact text
	star,     // * matches any chars except /
	question, // ? matches single char except /
};

// segment is a piece of a wildcard component.
// Example: "*.ts" becomes [segStar, segLiteral(".ts")]
struct segment {
	segmentKind kind;
	std::string literal; // only for segLiteral
};

// component is a single path segment in a glob pattern.
// Examples: "src" (literal), "*" (wildcard), "*.ts" (wildcard), "**"
// (recursive)
struct component {
	componentKind kind;
	std::string literal;         // for kindLiteral: the exact match
	std::vector<segment> segments; // for kindWildcard: parsed pattern
	// Include patterns with wildcards skip common package folders
	// (node_modules, etc.)
	bool skipPackageFolders = false;
};

// isHiddenPath checks if a path component is hidden (starts with dot).
inline bool isHiddenPath(std::string_view name) {
	return !name.empty() && name[0] == '.';
}

// isPackageFolder checks if name is a common package folder (node_modules,
// etc.)
inline bool isPackageFolder(std::string_view name) {
	switch (name.size()) {
	case 12: // len("node_modules")
		return stringutil::EquateStringCaseInsensitive(name,
		                                               "node_modules");
	case 13: // len("jspm_packages")
		return stringutil::EquateStringCaseInsensitive(name,
		                                               "jspm_packages");
	case 16: // len("bower_components")
		return stringutil::EquateStringCaseInsensitive(
		    name, "bower_components");
	}
	return false;
}

// parseSegments breaks "*.ts" into [segStar, segLiteral(".ts")].
inline std::vector<segment> parseSegments(std::string_view s) {
	std::vector<segment> result;
	size_t start = 0;
	for (size_t i = 0; i < s.size(); i++) {
		switch (s[i]) {
		case '*':
		case '?':
			if (i > start) {
				result.push_back(
				    {segmentKind::literal,
				     std::string{s.substr(start, i - start)}});
			}
			if (s[i] == '*') {
				result.push_back({segmentKind::star, {}});
			} else {
				result.push_back({segmentKind::question, {}});
			}
			start = i + 1;
			break;
		}
	}
	if (start < s.size()) {
		result.push_back(
		    {segmentKind::literal, std::string{s.substr(start)}});
	}
	return result;
}

// parseComponent converts a path segment string into a component.
inline component parseComponent(std::string_view s, bool isInclude) {
	if (s == "**") {
		return component{componentKind::doubleAsterisk, {}, {}, false};
	}
	if (s.find_first_of("*?") == std::string_view::npos) {
		return component{componentKind::literal, std::string{s}, {},
		                 false};
	}
	return component{componentKind::wildcard, {}, parseSegments(s),
	                 isInclude};
}

// nextPathPartSingle extracts the next path component from s at offset.
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
		return {rest.substr(0, idx), offset + static_cast<int>(idx),
		        true};
	}
	return {rest, static_cast<int>(s.size()), true};
}

inline std::tuple<std::string_view, int, bool> nextPathPartParts(
    std::string_view prefix, std::string_view suffix, int offset) {
	// Fast paths: keep the hot single-string scan tight.
	if (suffix.empty()) {
		return nextPathPartSingle(prefix, offset);
	}
	if (prefix.empty()) {
		return nextPathPartSingle(suffix, offset);
	}

	// For matchFilesNoRegex call sites, prefix is a directory path ending
	// in '/', and suffix is a single entry name (no '/').

	int totalLen = static_cast<int>(prefix.size() + suffix.size());
	if (offset >= totalLen) {
		return {"", offset, false};
	}

	// Handle leading slash (root of absolute path)
	if (offset == 0 && prefix[0] == '/') {
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

// globPattern is a compiled glob pattern for matching file paths without
// regex.
struct globPattern {
	std::vector<component> components;
	bool isExclude = false; // exclude patterns have different match rules
	bool caseSensitive = false;
	bool excludeMinJs = false; // for "files" patterns, exclude .min.js

	// stringsEqual compares strings with appropriate case sensitivity.
	bool stringsEqual(std::string_view a, std::string_view b) const {
		if (caseSensitive) {
			return a == b;
		}
		return stringutil::EquateStringCaseInsensitive(a, b);
	}

	// matches returns true if path matches this pattern.
	bool matches(std::string_view path) const {
		return matchPathParts(path, "", 0, 0, false);
	}

	// matchesParts returns true if prefix+suffix matches this pattern.
	bool matchesParts(std::string_view prefix,
	                  std::string_view suffix) const {
		return matchPathParts(prefix, suffix, 0, 0, false);
	}

	// matchesPrefixParts returns true if files under prefix+suffix could
	// match.
	bool matchesPrefixParts(std::string_view prefix,
	                        std::string_view suffix) const {
		return matchPathParts(prefix, suffix, 0, 0, true);
	}

	// matchPathParts is like matchPath, but operates on a virtual path
	// formed by prefix+suffix. Offsets are in the combined string.
	bool matchPathParts(std::string_view prefix, std::string_view suffix,
	                    int pathOffset, size_t compIdx,
	                    bool prefixOnly) const {
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
				if (matchPathParts(prefix, suffix, pathOffset,
				                   compIdx + 1, prefixOnly)) {
					return true;
				}
				if (!isExclude &&
				    (isHiddenPath(pathPart) ||
				     isPackageFolder(pathPart))) {
					return false;
				}
				pathOffset = nextOffset;
				continue;
			case componentKind::literal:
				if (comp.skipPackageFolders &&
				    isPackageFolder(pathPart)) {
					TSC_UNREACHABLE(
					    "unreachable: literal components "
					    "never have skipPackageFolders");
				}
				if (!stringsEqual(comp.literal, pathPart)) {
					return false;
				}
				break;
			case componentKind::wildcard:
				if (comp.skipPackageFolders &&
				    isPackageFolder(pathPart)) {
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

	// patternSatisfied checks if remaining pattern components can match
	// empty input. For both include and exclude patterns, only trailing
	// "**" components may match nothing.
	bool patternSatisfied(size_t compIdx) const {
		for (size_t i = compIdx; i < components.size(); i++) {
			if (components[i].kind != componentKind::doubleAsterisk) {
				return false;
			}
		}
		return true;
	}

	// matchWildcard matches a path component against wildcard segments.
	bool matchWildcard(const std::vector<segment>& segs,
	                   std::string_view s) const {
		// Include patterns: wildcards at start cannot match hidden files
		if (!isExclude && !segs.empty() && isHiddenPath(s) &&
		    (segs[0].kind == segmentKind::star ||
		     segs[0].kind == segmentKind::question)) {
			return false;
		}

		// Fast path: single * followed by literal suffix (e.g., "*.ts")
		if (segs.size() == 2 && segs[0].kind == segmentKind::star &&
		    segs[1].kind == segmentKind::literal) {
			auto& sfx = segs[1].literal;
			if (s.size() < sfx.size() ||
			    !stringsEqual(sfx, s.substr(s.size() - sfx.size()))) {
				return false;
			}
			return shouldIncludeMinJs(s, segs);
		}

		return matchSegments(segs, s) && shouldIncludeMinJs(s, segs);
	}

	// matchSegments matches segments against s using an iterative
	// algorithm. O(n*m) — only the last star position is tracked for
	// backtracking.
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
					    stringsEqual(
					        seg.literal,
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
						tsc::decodeUtf8Rune(s.substr(sIdx),
						                    &size);
						sIdx += size;
						segIdx++;
						continue;
					}
					break;
				case segmentKind::star:
					// Record star position for backtracking, then
					// try matching zero chars.
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
				// Star consumes one more character (rune), retry from
				// segment after star.
				int size;
				tsc::decodeUtf8Rune(s.substr(starSIdx), &size);
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

		// Preserve legacy behavior:
		// - When matching is case-sensitive, only the exact ".min.js"
		//   suffix is excluded by default.
		// - When matching is case-insensitive, any casing variant is
		//   excluded by default.
		if (!hasMinJsSuffix(filename)) {
			return true;
		}
		// Allow when the user's pattern explicitly references the .min.
		// suffix.
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
		// Avoid allocating via ToLower; compare suffix
		// case-insensitively.
		return stringutil::EquateStringCaseInsensitive(
		    filename.substr(filename.size() - minJs.size()), minJs);
	}

	bool patternMentionsMinSuffix(
	    const std::vector<segment>& segs) const {
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

// compileGlobPattern compiles a glob spec (e.g., "src/**/*.ts") into a
// pattern. Returns (pattern, false) if the pattern would match nothing.
inline std::pair<globPattern, bool> compileGlobPattern(
    std::string_view spec, std::string_view basePath, Usage usage,
    bool caseSensitive) {
	auto parts = tspath::getNormalizedPathComponents(spec, basePath);

	// "src/**" without a filename matches nothing (for include patterns)
	if (usage != Usage::Exclude && !parts.empty() &&
	    parts.back() == "**") {
		return {globPattern{}, false};
	}

	// Normalize root: "/home/" -> "/home"
	parts[0] =
	    std::string{tspath::removeTrailingDirectorySeparator(parts[0])};

	// Directories implicitly match all files: "src" -> "src/**/*"
	if (isImplicitGlob(parts.back())) {
		parts.push_back("**");
		parts.push_back("*");
	}

	globPattern p;
	p.isExclude = usage == Usage::Exclude;
	p.caseSensitive = caseSensitive;
	p.excludeMinJs = usage == Usage::Files;
	p.components.reserve(parts.size());

	for (auto& part : parts) {
		p.components.push_back(
		    parseComponent(part, usage != Usage::Exclude));
	}
	return {p, true};
}

// globMatcher combines include and exclude patterns for file matching.
struct globMatcher {
	std::vector<globPattern> includes;
	std::vector<globPattern> excludes;
	// hadIncludes is true if include specs were provided (even if none
	// compiled).
	bool hadIncludes = false;

	// matchesFileParts checks if prefix+suffix matches against the glob
	// patterns. Returns (index of matching include pattern, true) on
	// match, (0, false) if not.
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

	// matchesDirectoryParts checks if files under the directory
	// prefix+suffix could match any pattern.
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

inline globMatcher newGlobMatcher(
    const std::vector<std::string>& includeSpecs,
    const std::vector<std::string>& excludeSpecs,
    std::string_view basePath, bool caseSensitive, Usage usage) {
	globMatcher m;
	m.hadIncludes = !includeSpecs.empty();
	m.includes.reserve(includeSpecs.size());
	m.excludes.reserve(excludeSpecs.size());

	for (auto& spec : includeSpecs) {
		if (auto [p, ok] = compileGlobPattern(spec, basePath, usage,
		                                      caseSensitive);
		    ok) {
			m.includes.push_back(std::move(p));
		}
	}
	for (auto& spec : excludeSpecs) {
		if (auto [p, ok] = compileGlobPattern(spec, basePath,
		                                      Usage::Exclude,
		                                      caseSensitive);
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

// globVisitor traverses directories matching files against glob patterns.
struct globVisitor {
	vfs::FS* host;
	const globMatcher* fileMatcher;
	const globMatcher* directoryMatcher;
	const std::vector<std::string_view>* extensions;
	bool useCaseSensitiveFileNames;
	std::unordered_set<std::string> visited;
	std::vector<std::vector<std::string>> results;

	// visit walks a directory tree, collecting files that match the glob
	// patterns. resolvedRealPath, when non-empty, is the already-resolved
	// real path for this directory (computed incrementally from the
	// parent). When empty, Realpath is called to resolve symlinks.
	void visit(std::string_view path, std::string_view absolutePath,
	           int depth, std::string_view resolvedRealPath) {
		// Detect symlink cycles
		std::string realPath;
		if (!resolvedRealPath.empty()) {
			realPath = std::string{resolvedRealPath};
		} else {
			realPath = host->Realpath(std::string{absolutePath});
		}
		auto canonicalPath = tspath::getCanonicalFileName(
		    realPath, useCaseSensitiveFileNames);
		if (visited.count(canonicalPath)) {
			return;
		}
		visited.insert(canonicalPath);

		auto entries = host->GetAccessibleEntries(
		    std::string{absolutePath});

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
			if (!directoryMatcher->matchesDirectoryParts(absPrefix,
			                                           dir)) {
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
				// else: symlink directory; leave childRealPath
				// empty to force Realpath call.
			}
			// If symlinks is nullopt, the FS doesn't track symlinks;
			// leave childRealPath empty to call Realpath (preserving
			// old behavior).
			visit(pathPrefix + dir, absDir, depth, childRealPath);
		}
	}
};

inline std::vector<std::string> matchFiles(
    std::string_view path, const std::vector<std::string_view>& extensions,
    const std::vector<std::string>& excludes,
    const std::vector<std::string>& includes,
    bool useCaseSensitiveFileNames, std::string_view currentDirectory,
    int depth, vfs::FS* host) {
	auto normalizedPath = tspath::normalizePath(path);
	auto normalizedCwd = tspath::normalizePath(currentDirectory);
	auto absolutePath =
	    tspath::combinePaths(normalizedCwd, {normalizedPath});

	auto fileMatcher =
	    newGlobMatcher(includes, excludes, absolutePath,
	                   useCaseSensitiveFileNames, Usage::Files);
	auto directoryMatcher =
	    newGlobMatcher(includes, excludes, absolutePath,
	                   useCaseSensitiveFileNames, Usage::Directories);

	globVisitor v;
	v.host = host;
	v.fileMatcher = &fileMatcher;
	v.directoryMatcher = &directoryMatcher;
	v.extensions = &extensions;
	v.useCaseSensitiveFileNames = useCaseSensitiveFileNames;
	v.results.resize(
	    std::max(fileMatcher.includes.size(), size_t{1}));

	for (auto& basePath : getBasePaths(normalizedPath, includes,
	                                   useCaseSensitiveFileNames)) {
		v.visit(basePath,
		        tspath::combinePaths(normalizedCwd, {basePath}),
		        depth, "");
	}

	// Fast path: a single include bucket (or no includes) doesn't need
	// flattening.
	if (v.results.size() == 1) {
		return std::move(v.results[0]);
	}
	// core.Flatten.
	std::vector<std::string> out;
	for (auto& bucket : v.results) {
		out.insert(out.end(), bucket.begin(), bucket.end());
	}
	return out;
}

// ReadDirectory — vfsmatch.go ReadDirectory.
inline std::vector<std::string> ReadDirectory(
    vfs::FS* host, std::string_view currentDir, std::string_view path,
    const std::vector<std::string_view>& extensions,
    const std::vector<std::string>& excludes,
    const std::vector<std::string>& includes, int depth) {
	return matchFiles(path, extensions, excludes, includes,
	                  host->UseCaseSensitiveFileNames(), currentDir,
	                  depth, host);
}

// SpecMatcher wraps multiple glob patterns for matching paths.
struct SpecMatcher {
	std::vector<globPattern> patterns;

	// MatchString returns true if any pattern matches the path.
	bool MatchString(std::string_view path) const {
		for (auto& p : patterns) {
			if (p.matches(path)) return true;
		}
		return false;
	}

	// MatchIndex returns the index of the first matching pattern, or -1.
	int MatchIndex(std::string_view path) const {
		for (size_t i = 0; i < patterns.size(); i++) {
			if (patterns[i].matches(path)) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}
};

// NewSpecMatcher creates a matcher for one or more glob specs. Returns
// nullptr when no spec compiles.
inline std::unique_ptr<SpecMatcher> NewSpecMatcher(
    const std::vector<std::string>& specs, std::string_view basePath,
    Usage usage, bool useCaseSensitiveFileNames) {
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
	return std::make_unique<SpecMatcher>(
	    SpecMatcher{std::move(patterns)});
}

} // namespace tsc::vfs::vfsmatch
