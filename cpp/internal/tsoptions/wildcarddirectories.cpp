// Port of tsc/internal/tsoptions/wildcarddirectories.go.
#include "internal/tsoptions/tsoptions.h"

#include <algorithm>

#include "internal/module/vfsmatch.h"
#include "internal/tspath/tspath.h"

namespace tsc::tsoptions {

namespace {

// strings.CutLast — splits around the last occurrence of sep.
bool cutLast(std::string_view s, std::string_view sep, std::string_view& before,
             std::string_view& after) {
	auto idx = s.rfind(sep);
	if (idx == std::string_view::npos) {
		before = s;
		after = {};
		return false;
	}
	before = s.substr(0, idx);
	after = s.substr(idx + sep.size());
	return true;
}

// toCanonicalKey — wildcarddirectories.go:85.
std::string toCanonicalKey(std::string_view path,
                           bool useCaseSensitiveFileNames) {
	if (useCaseSensitiveFileNames) {
		return std::string(path);
	}
	return tspath::toFileNameLowerCase(path);
}

}  // namespace

// getWildcardDirectories — wildcarddirectories.go:10.
std::unordered_map<std::string, bool> getWildcardDirectories(
    const std::vector<std::string>& include,
    const std::vector<std::string>& exclude,
    const tspath::ComparePathsOptions& comparePathsOptions) {
	// We watch a directory recursively if it contains a wildcard anywhere in a directory segment
	// of the pattern:
	//
	//  /a/b/**/d   - Watch /a/b recursively to catch changes to any d in any subfolder recursively
	//  /a/b/*/d    - Watch /a/b recursively to catch any d in any immediate subfolder, even if a new subfolder is added
	//  /a/b        - Watch /a/b recursively to catch changes to anything in any recursive subfoler
	//
	// We watch a directory without recursion if it contains a wildcard in the file segment of
	// the pattern:
	//
	//  /a/b/*      - Watch /a/b directly to catch any new file
	//  /a/b/a?z    - Watch /a/b directly to catch any new file matching a?z

	if (include.empty()) {
		return {};
	}

	std::unique_ptr<module::vfsmatch::SpecMatcher> excludeMatcher(
	    module::vfsmatch::NewSpecMatcher(
	        exclude, comparePathsOptions.currentDirectory,
	        module::vfsmatch::Usage::Exclude,
	        comparePathsOptions.useCaseSensitiveFileNames));

	std::unordered_map<std::string, bool> wildcardDirectories;
	std::unordered_map<std::string, std::string> wildCardKeyToPath;

	std::vector<std::string> recursiveKeys;

	for (const auto& file : include) {
		std::string spec = tspath::normalizePath(tspath::combinePaths(
		    comparePathsOptions.currentDirectory, {file}));
		if (excludeMatcher != nullptr && excludeMatcher->MatchString(spec)) {
			continue;
		}

		std::unique_ptr<wildcardDirectoryMatch> match(
		    getWildcardDirectoryFromSpec(
		        spec, comparePathsOptions.useCaseSensitiveFileNames));
		if (match != nullptr) {
			const std::string& key = match->Key;
			const std::string& path = match->Path;
			bool recursive = match->Recursive;

			auto it = wildCardKeyToPath.find(key);
			bool existsPath = it != wildCardKeyToPath.end();
			bool existingRecursive = false;

			if (existsPath) {
				existingRecursive = wildcardDirectories[it->second];
			}

			if (!existsPath || (!existingRecursive && recursive)) {
				const std::string& pathToUse =
				    existsPath ? it->second : path;
				wildcardDirectories[pathToUse] = recursive;

				if (!existsPath) {
					wildCardKeyToPath[key] = path;
				}

				if (recursive) {
					recursiveKeys.push_back(key);
				}
			}
		}

		// Remove any subpaths under an existing recursively watched
		// directory
		for (auto it = wildcardDirectories.begin();
		     it != wildcardDirectories.end();) {
			const std::string& path = it->first;
			bool erased = false;
			for (const auto& recursiveKey : recursiveKeys) {
				std::string k = toCanonicalKey(
				    path, comparePathsOptions.useCaseSensitiveFileNames);
				if (k != recursiveKey && tspath::containsPath(recursiveKey, k,
				                         comparePathsOptions)) {
					it = wildcardDirectories.erase(it);
					erased = true;
					break;
				}
			}
			if (!erased) {
				++it;
			}
		}
	}

	return wildcardDirectories;
}

// getWildcardDirectoryFromSpec — wildcarddirectories.go:99.
wildcardDirectoryMatch* getWildcardDirectoryFromSpec(
    std::string_view spec, bool useCaseSensitiveFileNames) {
	// Find the first occurrence of a wildcard character
	auto firstWildcard = spec.find_first_of("*?");
	if (firstWildcard != std::string_view::npos) {
		// Find the last directory separator before the wildcard
		auto lastSepBeforeWildcard =
		    spec.substr(0, firstWildcard).rfind('/');
		if (lastSepBeforeWildcard != std::string_view::npos) {
			std::string_view path = spec.substr(0, lastSepBeforeWildcard);
			auto lastDirectorySeparatorIndex = spec.rfind('/');

			// Determine if this should be watched recursively:
			// recursive if the wildcard appears in a directory segment
			// (not just the final file segment)
			bool recursive = firstWildcard < lastDirectorySeparatorIndex;

			return new wildcardDirectoryMatch{
			    .Key =
			        toCanonicalKey(path, useCaseSensitiveFileNames),
			    .Path = std::string(path),
			    .Recursive = recursive,
			};
		}
	}

	std::string_view before, lastSegment;
	if (cutLast(spec, "/", before, lastSegment)) {
		if (module::vfsmatch::IsImplicitGlob(lastSegment)) {
			std::string path = std::string(
			    tspath::removeTrailingDirectorySeparator(spec));
			return new wildcardDirectoryMatch{
			    .Key =
			        toCanonicalKey(path, useCaseSensitiveFileNames),
			    .Path = path,
			    .Recursive = true,
			};
		}
	}

	return nullptr;
}

}  // namespace tsc::tsoptions
