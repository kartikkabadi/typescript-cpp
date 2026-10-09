// Port of tsc/internal/modulespecifiers/util.go — regex exclusion cache,
// path helpers, node_modules path parsing, and ProcessEntrypointEnding.
// Host type (ModuleSpecifierGenerationHost) maps to checker::Program;
// SourceFileForSpecifierGeneration maps to SourceFile.

#include "internal/modulespecifiers/types.h"

#include "internal/checker/checker.h"
#include "internal/collections/collections.h"
#include "internal/gostd/regexp.h"
#include "internal/module/resolver.h"
#include "internal/module/util.h"
#include "internal/packagejson/packagejson.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tspath/tspath.h"

#include <algorithm>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace tsc::modulespecifiers {

namespace {

// --- file-local helpers ------------------------------------------------------

// core.go:867 — core.CompareBooleans (true > false).
// (deduped: replicated file-locally until a core slice lands)
int compareBooleans(bool a, bool b) {
	if (a && !b) {
		return 1;
	} else if (!a && b) {
		return -1;
	}
	return 0;
}

// core.go:715 — core.IndexAfter (despite the name, returns the index of the
// match, not the index after it).
int indexAfter(std::string_view s, std::string_view pattern, int startIndex) {
	auto matched = s.substr(startIndex).find(pattern);
	if (matched == std::string_view::npos) {
		return -1;
	}
	return static_cast<int>(matched) + startIndex;
}

// path.go:1271 — tspath.CompareNumberOfDirectorySeparators.
// (deduped: replicated file-locally until a tspath slice lands)
int compareNumberOfDirectorySeparators(std::string_view path1,
                                       std::string_view path2) {
	auto count1 = std::count(path1.begin(), path1.end(), '/');
	auto count2 = std::count(path2.begin(), path2.end(), '/');
	return count1 < count2 ? -1 : count1 > count2 ? 1 : 0;
}

// strings.TrimSuffix.
std::string trimSuffix(std::string_view s, std::string_view suffix) {
	if (suffix.size() <= s.size() && s.substr(s.size() - suffix.size()) == suffix) {
		return std::string{s.substr(0, s.size() - suffix.size())};
	}
	return std::string{s};
}

} // namespace

// ---------------------------------------------------------------------------
// util.go:19 — regexPatternCacheKey / regexPatternCache
// ---------------------------------------------------------------------------

struct regexPatternCacheKey {
	std::string pattern;
	bool caseInsensitive = false;
	bool operator==(const regexPatternCacheKey&) const = default;
};

struct regexPatternCacheKeyHash {
	size_t operator()(const regexPatternCacheKey& k) const {
		return std::hash<std::string>{}(k.pattern) ^
		       (std::hash<bool>{}(k.caseInsensitive) << 1);
	}
};

// util.go:24 — regexPatternCacheMu + regexPatternCache. Failed compiles are
// cached as nullptr (Go stores nil *regexp.Regexp).
static std::shared_mutex regexPatternCacheMu;
static std::unordered_map<regexPatternCacheKey,
                          std::shared_ptr<gostd::regexp::Regexp>,
                          regexPatternCacheKeyHash>
    regexPatternCache;

// util.go:59 — stringToRegex (defined below; declared for IsExcludedByRegex)
// noinline (Windows): when clang inlines this into IsExcludedByRegex at -O3,
// the regex-ctor call site lands in a terminate-scoped EH state rather than
// inside the inlined try — a bad pattern then std::terminate()s the process
// instead of hitting the catch below (Go just treats it as a failed
// compile). Keeping it out-of-line preserves correct EH attribution.
#if defined(_WIN32)
__declspec(noinline)
#endif
static std::shared_ptr<gostd::regexp::Regexp> stringToRegex(
    std::string pattern);
// util.go:183 — extensionFromPath (defined below)
static std::string extensionFromPath(std::string_view path);

// util.go:29 — comparePathsByRedirect
int comparePathsByRedirect(const ModulePath& a, const ModulePath& b,
                           bool useCaseSensitiveFileNames) {
	// Redirects sort first, matching Strada's compareBooleans(b.isRedirect, a.isRedirect).
	if (int c = compareBooleans(b.IsRedirect, a.IsRedirect); c != 0) {
		return c;
	}
	if (int c = compareNumberOfDirectorySeparators(a.FileName, b.FileName);
	    c != 0) {
		return c;
	}
	// Strada relies on Map insertion order to break remaining ties deterministically;
	// Go maps are unordered, so compare paths to keep the ordering stable.
	return tspath::comparePaths(
	    a.FileName, b.FileName,
	    tspath::ComparePathsOptions{useCaseSensitiveFileNames, {}});
}

// util.go:42 — PathIsBareSpecifier
bool PathIsBareSpecifier(std::string_view path) {
	return !tspath::pathIsAbsolute(path) && !tspath::pathIsRelative(path);
}

// util.go:46 — IsExcludedByRegex
bool IsExcludedByRegex(const std::string& moduleSpecifier,
                       const std::vector<std::string>& excludes) {
	for (auto& pattern : excludes) {
		auto re = stringToRegex(pattern);
		if (re == nullptr) {
			continue;
		}
		// RE2 (like Go's regexp.Regexp) is safe for concurrent searches on a
		// shared object — no lock needed (std::regex required one on MSVC).
		if (re->MatchString(moduleSpecifier)) {
			return true;
		}
	}
	return false;
}

// util.go:59 — stringToRegex. Go compiles "(?i:" + pattern + ")" via
// regexp.Compile; the identical string is compiled by RE2 here, so the
// accept/reject decision is byte-identical to Go's (RE2 rejects what Go
// rejects — look-around, backrefs, unsupported escapes — and vice versa).
static std::shared_ptr<gostd::regexp::Regexp> stringToRegex(
    std::string pattern) {
	bool caseInsensitive = false;

	if (pattern.size() > 2 && pattern[0] == '/') {
		auto lastSlash = pattern.rfind('/');
		if (lastSlash != std::string::npos && lastSlash > 0) {
			bool hasUnescapedMiddleSlash = false;
			for (size_t i = 1; i < lastSlash; i++) {
				if (pattern[i] == '/' && (i == 0 || pattern[i - 1] != '\\')) {
					hasUnescapedMiddleSlash = true;
					break;
				}
			}

			if (!hasUnescapedMiddleSlash) {
				auto flags = pattern.substr(lastSlash + 1);
				pattern = pattern.substr(1, lastSlash - 1);

				for (char flag : flags) {
					switch (flag) {
					case 'i':
						caseInsensitive = true;
					}
				}
			}
		}
	}
	regexPatternCacheKey key{pattern, caseInsensitive};

	{
		std::shared_lock lock(regexPatternCacheMu);
		auto it = regexPatternCache.find(key);
		if (it != regexPatternCache.end()) {
			return it->second;
		}
	}

	std::unique_lock lock(regexPatternCacheMu);

	auto it = regexPatternCache.find(key);
	if (it != regexPatternCache.end()) {
		return it->second;
	}

	if (regexPatternCache.size() > 1000) {
		regexPatternCache.clear();
	}

	// strings.Clone — C++ strings are always owned copies.

	// Go: compilePattern = "(?i:" + pattern + ")" for caseInsensitive.
	// gostd::regexp::Regexp compiles it with RE2 — a bad pattern throws
	// (MustCompile analog), which is Go's failed Compile -> cached nil.
	std::string compilePattern =
	    caseInsensitive ? "(?i:" + pattern + ")" : pattern;
	try {
		auto compiled =
		    std::make_shared<gostd::regexp::Regexp>(compilePattern);
		regexPatternCache[key] = compiled;
		return compiled;
	} catch (const std::exception&) {
		regexPatternCache[key] = nullptr;
		return nullptr;
	}
}

/**
 * Ensures a path is either absolute (prefixed with `/` or `c:`) or dot-relative (prefixed
 * with `./` or `../`) so as not to be confused with an unprefixed module name.
 *
 * ```ts
 * ensurePathIsNonModuleName("/path/to/file.ext") === "/path/to/file.ext"
 * ensurePathIsNonModuleName("./path/to/file.ext") === "./path/to/file.ext"
 * ensurePathIsNonModuleName("../path/to/file.ext") === "../path/to/file.ext"
 * ensurePathIsNonModuleName("path/to/file.ext") === "./path/to/file.ext"
 * ```
 *
 */
// util.go:136 — ensurePathIsNonModuleName
std::string ensurePathIsNonModuleName(std::string_view path) {
	if (PathIsBareSpecifier(path)) {
		return "./" + std::string{path};
	}
	return std::string{path};
}

// util.go:143 — GetJSExtensionForDeclarationFileExtension
std::string GetJSExtensionForDeclarationFileExtension(std::string_view ext) {
	if (ext == tspath::extensionDts) {
		return std::string{tspath::extensionJs};
	}
	if (ext == tspath::extensionDmts) {
		return std::string{tspath::extensionMjs};
	}
	if (ext == tspath::extensionDcts) {
		return std::string{tspath::extensionCjs};
	}
	// .d.json.ts and the like
	return std::string{
	    ext.substr(2 /*len(".d")*/, ext.size() - 2 - tspath::extensionTs.size())};
}

// util.go:159 — TryGetRealFileNameForNonJSDeclarationFileName remaps files
// like `foo.d.json.ts` or `foo.module.d.css.ts` back to their real non-JS
// names.
std::string TryGetRealFileNameForNonJSDeclarationFileName(
    const std::string& fileName) {
	auto baseName = tspath::getBaseFileName(fileName);
	// Ends with .ts, contains ".d.", and is NOT a standard .d.ts file
	if (fileName.size() < tspath::extensionTs.size() ||
	    fileName.compare(fileName.size() - tspath::extensionTs.size(),
	                     tspath::extensionTs.size(), tspath::extensionTs) != 0 ||
	    baseName.find(".d.") == std::string_view::npos ||
	    (baseName.size() >= tspath::extensionDts.size() &&
	     baseName.compare(baseName.size() - tspath::extensionDts.size(),
	                      tspath::extensionDts.size(), tspath::extensionDts) ==
	         0)) {
		return "";
	}
	auto noExtension = tspath::removeExtension(fileName, tspath::extensionTs);
	auto lastDotIndex = noExtension.rfind('.');
	auto ext = noExtension.substr(lastDotIndex);
	auto dIndex = noExtension.find(".d.");
	auto before = dIndex == std::string_view::npos
	                  ? noExtension
	                  : noExtension.substr(0, dIndex);
	return std::string{before} + std::string{ext};
}

// util.go:174 — getJSExtensionForFile
std::string getJSExtensionForFile(std::string_view fileName,
                                  const CompilerOptions* options) {
	auto result = module::TryGetJSExtensionForFile(fileName, *options);
	if (result.empty()) {
		TSC_UNREACHABLE(("Extension " + extensionFromPath(fileName) +
		                 " is unsupported:: FileName:: " + std::string{fileName})
		                    .c_str());
	}
	return std::string{result};
}

/**
 * Gets the extension from a path.
 * Path must have a valid extension.
 */
// util.go:186 — extensionFromPath
std::string extensionFromPath(std::string_view path) {
	auto ext = tspath::tryGetExtensionFromPath(path);
	if (ext.empty()) {
		TSC_UNREACHABLE(
		    ("File " + std::string{path} + " has unknown extension.").c_str());
	}
	return std::string{ext};
}

// util.go:194 — tryGetAnyFileFromPath
bool tryGetAnyFileFromPath(checker::Program* host, const std::string& path) {
	// !!! TODO: shouldn't this use readdir instead of fileexists for perf?
	// We check all js, `node` and `json` extensions in addition to TS, since
	// node module resolution would also choose those over the directory
	CompilerOptions options;
	options.AllowJs = Tristate::True;
	auto extGroups =
	    tsoptions::getSupportedExtensions(&options, {".node", ".json"});
	for (auto& exts : extGroups) {
		for (auto e : exts) {
			auto fullPath = path + std::string{e};
			if (host->FileExists(tspath::getNormalizedAbsolutePath(
			        fullPath, host->GetCurrentDirectory()))) {
				return true;
			}
		}
	}
	return false;
}

// util.go:214 — getPathsRelativeToRootDirs
std::vector<std::string> getPathsRelativeToRootDirs(
    const std::string& path, const std::vector<std::string>& rootDirs,
    bool useCaseSensitiveFileNames) {
	std::vector<std::string> results;
	for (auto& rootDir : rootDirs) {
		auto relativePath = getRelativePathIfInSameVolume(path, rootDir,
		                                                  useCaseSensitiveFileNames);
		if (!isPathRelativeToParent(relativePath)) {
			results.push_back(relativePath);
		}
	}
	return results;
}

// util.go:225 — isPathRelativeToParent
bool isPathRelativeToParent(std::string_view path) {
	return path.size() >= 2 && path[0] == '.' && path[1] == '.';
}

// util.go:229 — getRelativePathIfInSameVolume
std::string getRelativePathIfInSameVolume(const std::string& path,
                                          const std::string& directoryPath,
                                          bool useCaseSensitiveFileNames) {
	auto relativePath = getRelativePathToDirectoryOrUrl(
	    directoryPath, path, false,
	    tspath::ComparePathsOptions{useCaseSensitiveFileNames, directoryPath});
	if (tspath::isRootedDiskPath(relativePath)) {
		return "";
	}
	return relativePath;
}

// util.go:240 — packageJsonPathsAreEqual
bool packageJsonPathsAreEqual(const std::string& a, const std::string& b,
                              const tspath::ComparePathsOptions& options) {
	if (a == b) {
		return true;
	}
	if (a.empty() || b.empty()) {
		return false;
	}
	return tspath::comparePaths(a, b, options) == 0;
}

// util.go:250 — prefersTsExtension
bool prefersTsExtension(
    const std::vector<ModuleSpecifierEnding>& allowedEndings) {
	auto indexOf = [](const std::vector<ModuleSpecifierEnding>& v,
	                  ModuleSpecifierEnding e) {
		auto it = std::find(v.begin(), v.end(), e);
		return it == v.end() ? -1
		                     : static_cast<int>(std::distance(v.begin(), it));
	};
	int jsPriority = indexOf(allowedEndings, ModuleSpecifierEnding::JsExtension);
	int tsPriority = indexOf(allowedEndings, ModuleSpecifierEnding::TsExtension);
	if (tsPriority > -1) {
		return tsPriority < jsPriority;
	}
	return false;
}

// util.go:259 — replaceFirstStar
std::string replaceFirstStar(std::string_view s, std::string_view replacement) {
	auto i = s.find('*');
	if (i == std::string_view::npos) {
		return std::string{s};
	}
	return std::string{s.substr(0, i)} + std::string{replacement} +
	       std::string{s.substr(i + 1)};
}

// ---------------------------------------------------------------------------
// util.go:270 — nodeModulesPathParseState
// ---------------------------------------------------------------------------

enum class nodeModulesPathParseState : uint8_t {
	nodeModulesPathParseStateBeforeNodeModules = 0,
	nodeModulesPathParseStateNodeModules,
	nodeModulesPathParseStateScope,
	nodeModulesPathParseStatePackageContent,
};

// util.go:279 — GetNodeModulePathParts
std::optional<NodeModulePathParts> GetNodeModulePathParts(
    const std::string& fullPath) {
	// If fullPath can't be valid module file within node_modules, returns undefined.
	// Example of expected pattern: /base/path/node_modules/[@scope/otherpackage/@otherscope/node_modules/]package/[subdirectory/]file.js
	// Returns indices:                       ^            ^                                                      ^             ^

	int topLevelNodeModulesIndex = 0;
	int topLevelPackageNameIndex = 0;
	int packageRootIndex = 0;

	int partStart = 0;
	int partEnd = 0;
	auto state = nodeModulesPathParseState::nodeModulesPathParseStateBeforeNodeModules;

	std::string_view sv{fullPath};
	while (partEnd >= 0) {
		partStart = partEnd;
		partEnd = indexAfter(sv, "/", partStart + 1);
		switch (state) {
		case nodeModulesPathParseState::nodeModulesPathParseStateBeforeNodeModules:
			if (sv.substr(partStart).starts_with("/node_modules/")) {
				topLevelNodeModulesIndex = partStart;
				topLevelPackageNameIndex = partEnd;
				state = nodeModulesPathParseState::
				    nodeModulesPathParseStateNodeModules;
			}
			break;
		case nodeModulesPathParseState::nodeModulesPathParseStateNodeModules:
		case nodeModulesPathParseState::nodeModulesPathParseStateScope:
			if (state == nodeModulesPathParseState::
			                 nodeModulesPathParseStateNodeModules &&
			    sv[partStart + 1] == '@') {
				state = nodeModulesPathParseState::
				    nodeModulesPathParseStateScope;
			} else {
				packageRootIndex = partEnd;
				state = nodeModulesPathParseState::
				    nodeModulesPathParseStatePackageContent;
			}
			break;
		case nodeModulesPathParseState::nodeModulesPathParseStatePackageContent:
			if (sv.substr(partStart).starts_with("/node_modules/")) {
				state = nodeModulesPathParseState::
				    nodeModulesPathParseStateNodeModules;
			} else {
				state = nodeModulesPathParseState::
				    nodeModulesPathParseStatePackageContent;
			}
			break;
		}
	}

	int fileNameIndex = partStart;

	if (static_cast<int>(state) >
	    static_cast<int>(nodeModulesPathParseState::
	                         nodeModulesPathParseStateNodeModules)) {
		return NodeModulePathParts{topLevelNodeModulesIndex,
		                           topLevelPackageNameIndex, packageRootIndex,
		                           fileNameIndex};
	}
	return std::nullopt;
}

// util.go:332 — GetNodeModulesPackageName
std::string GetNodeModulesPackageName(
    const CompilerOptions* compilerOptions, SourceFile* importingSourceFile,
    const std::string& nodeModulesFileName, checker::Program* host,
    const UserPreferences& preferences, const ModuleSpecifierOptions& options) {
	// !!! | FutureSourceFile
	auto info = getInfo(importingSourceFile->FileName(), host);
	auto modulePaths = getAllModulePaths(info, nodeModulesFileName, host,
	                                     compilerOptions, preferences, options);
	for (auto& modulePath : modulePaths) {
		if (auto result = tryGetModuleNameAsNodeModule(
		        modulePath, info, importingSourceFile, host, compilerOptions,
		        preferences, true /*packageNameOnly*/,
		        options.OverrideImportMode);
		    !result.empty()) {
			return result;
		}
	}
	return "";
}

// util.go:350 — allKeysStartWithDot (unused in Go as well — kept for
// fidelity)
static bool allKeysStartWithDot(
    const collections::OrderedMap<std::string, packagejson::ExportsOrImports>*
        obj) {
	for (auto& k : obj->Keys()) {
		if (k.empty() || k[0] != '.') {
			return false;
		}
	}
	return true;
}

// util.go:359 — GetPackageNameFromDirectory
std::string GetPackageNameFromDirectory(std::string_view fileOrDirectoryPath) {
	auto i = fileOrDirectoryPath.rfind("/node_modules/");
	if (i == std::string_view::npos) {
		return "";
	}
	auto basename = fileOrDirectoryPath.substr(i + 14 /*len("/node_modules/")*/);

	if (basename[0] == '.') {
		return "";
	}

	auto nextSlash = basename.find('/');
	if (nextSlash == std::string_view::npos) {
		return std::string{basename};
	}

	if (basename[0] != '@' || nextSlash == basename.size() - 1) {
		return std::string{basename.substr(0, nextSlash)};
	}

	auto secondSlash = basename.substr(nextSlash + 1).find('/');
	if (secondSlash == std::string_view::npos) {
		return std::string{basename};
	}

	return std::string{basename.substr(0, nextSlash + 1 + secondSlash)};
}

// util.go:388 — ProcessEntrypointEnding processes a pre-computed module
// specifier from a package.json exports entrypoint according to the
// entrypoint's Ending type and the user's preferred endings.
std::string ProcessEntrypointEnding(
    const module::ResolvedEntrypoint* entrypoint, const UserPreferences& prefs,
    checker::Program* host, const CompilerOptions* options,
    SourceFile* importingSourceFile,
    const std::vector<ModuleSpecifierEnding>& allowedEndings_) {
	std::string specifier = entrypoint->ModuleSpecifier;
	if (entrypoint->Ending_ == module::Ending::Fixed) {
		return specifier;
	}

	std::vector<ModuleSpecifierEnding> allowedEndings = allowedEndings_;
	if (allowedEndings.empty()) {
		allowedEndings = GetAllowedEndingsInPreferredOrder(
		    prefs, host, options, importingSourceFile, "",
		    host->GetDefaultResolutionModeForFile(importingSourceFile));
	}

	auto preferredEnding = allowedEndings[0];

	// Handle declaration file extensions
	auto dtsExtension = tspath::getDeclarationFileExtension(specifier);
	if (!dtsExtension.empty()) {
		switch (preferredEnding) {
		case ModuleSpecifierEnding::TsExtension:
		case ModuleSpecifierEnding::JsExtension: {
			// Map .d.ts -> .js, .d.mts -> .mjs, .d.cts -> .cjs
			auto jsExtension =
			    GetJSExtensionForDeclarationFileExtension(dtsExtension);
			return tspath::changeAnyExtension(
			    specifier, jsExtension, {dtsExtension}, false);
		}
		case ModuleSpecifierEnding::Minimal:
		case ModuleSpecifierEnding::Index:
			if (entrypoint->Ending_ == module::Ending::Changeable) {
				// .d.mts/.d.cts must keep an extension; rewrite to .mjs/.cjs
				// instead of dropping
				if (dtsExtension == tspath::extensionDts) {
					specifier = std::string{tspath::removeExtension(
					    specifier, dtsExtension)};
					if (preferredEnding == ModuleSpecifierEnding::Minimal) {
						specifier = trimSuffix(specifier, "/index");
					}
					return specifier;
				}
				auto jsExtension =
				    GetJSExtensionForDeclarationFileExtension(dtsExtension);
				return tspath::changeAnyExtension(specifier, jsExtension,
				                                  {dtsExtension}, false);
			}
			// EndingExtensionChangeable - can only change extension, not
			// remove it
			return tspath::changeAnyExtension(
			    specifier, GetJSExtensionForDeclarationFileExtension(dtsExtension),
			    {dtsExtension}, false);
		}
		return specifier;
	}

	// Handle .ts/.tsx/.mts/.cts extensions
	if (tspath::fileExtensionIsOneOf(
	        specifier, {tspath::extensionTs, tspath::extensionTsx,
	                    tspath::extensionMts, tspath::extensionCts})) {
		switch (preferredEnding) {
		case ModuleSpecifierEnding::TsExtension:
			return specifier;
		case ModuleSpecifierEnding::JsExtension:
			if (auto jsExtension =
			        module::TryGetJSExtensionForFile(specifier, *options);
			    !jsExtension.empty()) {
				return std::string{tspath::removeFileExtension(specifier)} +
				       std::string{jsExtension};
			}
			return specifier;
		case ModuleSpecifierEnding::Minimal:
		case ModuleSpecifierEnding::Index:
			if (entrypoint->Ending_ == module::Ending::Changeable) {
				specifier =
				    std::string{tspath::removeFileExtension(specifier)};
				if (preferredEnding == ModuleSpecifierEnding::Minimal) {
					specifier = trimSuffix(specifier, "/index");
				}
				return specifier;
			}
			// EndingExtensionChangeable - can only change extension, not
			// remove it
			if (auto jsExtension =
			        module::TryGetJSExtensionForFile(specifier, *options);
			    !jsExtension.empty()) {
				return std::string{tspath::removeFileExtension(specifier)} +
				       std::string{jsExtension};
			}
			return specifier;
		}
		return specifier;
	}

	// Handle .js/.jsx/.mjs/.cjs extensions
	if (tspath::fileExtensionIsOneOf(
	        specifier, {tspath::extensionJs, tspath::extensionJsx,
	                    tspath::extensionMjs, tspath::extensionCjs})) {
		switch (preferredEnding) {
		case ModuleSpecifierEnding::TsExtension:
		case ModuleSpecifierEnding::JsExtension:
			return specifier;
		case ModuleSpecifierEnding::Minimal:
		case ModuleSpecifierEnding::Index:
			if (entrypoint->Ending_ == module::Ending::Changeable) {
				specifier =
				    std::string{tspath::removeFileExtension(specifier)};
				if (preferredEnding == ModuleSpecifierEnding::Minimal) {
					specifier = trimSuffix(specifier, "/index");
				}
				return specifier;
			}
			// EndingExtensionChangeable - keep the extension
			return specifier;
		}
		return specifier;
	}

	// For other extensions (like .json), return as-is
	return specifier;
}

} // namespace tsc::modulespecifiers
