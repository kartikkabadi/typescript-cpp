// Port of tsc/internal/modulespecifiers/specifiers.go — module specifier
// generation (ambient modules, node_modules paths, package.json
// exports/imports, tsconfig "paths", redirects, symlinks).
// Host type (ModuleSpecifierGenerationHost) maps to checker::Program;
// SourceFileForSpecifierGeneration maps to SourceFile; CheckerShape maps to
// checker::Checker.

#include "internal/modulespecifiers/types.h"

#include "internal/ast/ast.h"
#include "internal/ast/symbol.h"
#include "internal/checker/checker.h"
#include "internal/collections/collections.h"
#include "internal/core/pattern.h"
#include "internal/module/resolver.h"
#include "internal/module/types.h"
#include "internal/module/util.h"
#include "internal/outputpaths/outputpaths.h"
#include "internal/packagejson/packagejson.h"
#include "internal/stringutil/stringutil.h"
#include "internal/symlinks/knownsymlinks.h"
#include "internal/tspath/tspath.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tsc::modulespecifiers {

namespace {

// --- file-local helpers ------------------------------------------------------

// ast/utilities.go:3620 — ast.GetNonAugmentationDeclaration.
// (deduped: replicated in checker_nodebuilder.cpp / checker_moduletarget.cpp)
Node* getNonAugmentationDeclaration(Symbol* symbol) {
	for (Node* d : symbol->declarations) {
		if (!isExternalModuleAugmentation(d) &&
		    !isGlobalScopeAugmentation(d)) {
			return d;
		}
	}
	return nullptr;
}

// ast/utilities.go:3613 — ast.GetSourceFileOfModule.
// (deduped: replicated in checker_nodebuilder.cpp / checker_moduletarget.cpp)
SourceFile* getSourceFileOfModule(Symbol* module) {
	Node* declaration = module->valueDeclaration;
	if (declaration == nullptr) {
		declaration = getNonAugmentationDeclaration(module);
	}
	return getSourceFileOfNode(declaration);
}

// path.go:1257 — tspath.StartsWithDirectory.
// (deduped: replicated file-locally until a tspath slice lands)
bool startsWithDirectory(std::string_view fileName,
                         std::string_view directoryName,
                         bool useCaseSensitiveFileNames) {
	if (directoryName.empty()) {
		return false;
	}

	auto canonicalFileName =
	    tspath::getCanonicalFileName(fileName, useCaseSensitiveFileNames);
	auto canonicalDirectoryName = tspath::getCanonicalFileName(
	    directoryName, useCaseSensitiveFileNames);
	if (!canonicalDirectoryName.empty() &&
	    canonicalDirectoryName.back() == '/') {
		canonicalDirectoryName.pop_back();
	}
	if (!canonicalDirectoryName.empty() &&
	    canonicalDirectoryName.back() == '\\') {
		canonicalDirectoryName.pop_back();
	}

	return std::string_view{canonicalFileName}.starts_with(
	           canonicalDirectoryName + "/") ||
	       std::string_view{canonicalFileName}.starts_with(
	           canonicalDirectoryName + "\\");
}

// path.go:1088 — (Path).ContainsPath. Path values are already rooted,
// reduced, and case-canonicalized, so this is a simple string prefix check.
// (deduped: replicated file-locally until a tspath slice lands)
bool pathContainsPath(const tspath::Path& p, const tspath::Path& child) {
	if (p.empty()) {
		return false;
	}
	return p == child || (child.size() > p.size() &&
	                      child.compare(0, p.size(), p) == 0 &&
	                      (p.back() == '/' || child[p.size()] == '/'));
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

// core.go:190 — core.Some.
template <typename T, typename F>
bool someSlice(const std::vector<T>& v, const F& f) {
	for (auto& x : v) {
		if (f(x)) return true;
	}
	return false;
}

// core.go:199 — core.Every.
template <typename T, typename F>
bool everySlice(const std::vector<T>& v, const F& f) {
	for (auto& x : v) {
		if (!f(x)) return false;
	}
	return true;
}

// slices.Index — position of `e` or -1.
template <typename T>
int indexOf(const std::vector<T>& v, const T& e) {
	auto it = std::find(v.begin(), v.end(), e);
	return it == v.end() ? -1
	                     : static_cast<int>(std::distance(v.begin(), it));
}

// strings.Cut — (before, after, found) on the first `sep`.
inline std::pair<std::string_view, std::string_view> cut(
    std::string_view s, std::string_view sep, bool& found) {
	auto i = s.find(sep);
	found = i != std::string_view::npos;
	if (!found) {
		return {s, ""};
	}
	return {s.substr(0, i), s.substr(i + sep.size())};
}

// strings.TrimSuffix.
std::string trimSuffix(std::string_view s, std::string_view suffix) {
	if (suffix.size() <= s.size() &&
	    s.substr(s.size() - suffix.size()) == suffix) {
		return std::string{s.substr(0, s.size() - suffix.size())};
	}
	return std::string{s};
}

// specifiers.go:252 — containsIgnoredPath. Local helper that duplicates
// tspath.ContainsIgnoredPath for performance.
bool containsIgnoredPath(const std::string& s) {
	return s.find("/node_modules/.") != std::string::npos ||
	       s.find("/.git") != std::string::npos ||
	       s.find(".#") != std::string::npos;
}

} // namespace

// specifiers.go:107 — ambientModuleInfo
struct ambientModuleInfo {
	std::string name;
	Symbol* symbol = nullptr;
};

// specifiers.go:830 — pkgJsonDirAttemptResult
struct pkgJsonDirAttemptResult {
	std::string moduleFileToTry;
	std::string packageRootPath;
	bool blockedByExports = false;
	bool verbatimFromExports = false;
};

// specifiers.go:1089 — specPair
struct specPair {
	ModuleSpecifierEnding ending;
	std::string value;
};

// --- forward decls (Go order preserved below) --------------------------------

ambientModuleInfo tryGetModuleNameFromAmbientModule(Symbol* moduleSymbol,
                                                    checker::Checker* checker);
std::vector<ModulePath> getAllModulePathsWorker(
    const Info& info, const std::string& importedFileName,
    checker::Program* host, const CompilerOptions* compilerOptions,
    const ModuleSpecifierOptions& options);
std::pair<std::vector<std::string>, ResultKind> computeModuleSpecifiers(
    const std::vector<ModulePath>& modulePaths,
    const CompilerOptions* compilerOptions, SourceFile* importingSourceFile,
    checker::Program* host, const UserPreferences& userPreferences,
    const ModuleSpecifierOptions& options, bool forAutoImport);
std::string getLocalModuleSpecifier(
    const std::string& moduleFileName, const Info& info,
    const CompilerOptions* compilerOptions, checker::Program* host,
    ResolutionMode importMode, const ModuleSpecifierPreferences& preferences,
    bool pathsOnly);
std::string processEnding(
    const std::string& fileName,
    const std::vector<ModuleSpecifierEnding>& allowedEndings,
    const CompilerOptions* options, checker::Program* host);
std::string tryGetModuleNameFromRootDirs(
    const std::vector<std::string>& rootDirs, const std::string& moduleFileName,
    const std::string& sourceDirectory,
    const std::vector<ModuleSpecifierEnding>& allowedEndings,
    const CompilerOptions* compilerOptions, checker::Program* host);
pkgJsonDirAttemptResult tryDirectoryWithPackageJson(
    const NodeModulePathParts& parts, const ModulePath& pathObj,
    SourceFile* importingSourceFile, checker::Program* host,
    ResolutionMode overrideMode, const CompilerOptions* options,
    const std::vector<ModuleSpecifierEnding>& allowedEndings);
std::string tryGetModuleNameFromExports(
    const CompilerOptions* options, checker::Program* host,
    const std::string& targetFilePath, const std::string& packageDirectory,
    const std::string& packageName,
    const packagejson::ExportsOrImports& exports,
    const std::vector<std::string>& conditions);
std::string tryGetModuleNameFromPackageJsonImports(
    const std::string& moduleFileName, const std::string& sourceDirectory,
    const CompilerOptions* options, checker::Program* host,
    ResolutionMode importMode, bool preferTsExtension);
std::string tryGetModuleNameFromPaths(
    const std::string& relativeToBaseUrl,
    const std::vector<std::pair<std::string, std::vector<std::string>>>& paths,
    const std::vector<ModuleSpecifierEnding>& allowedEndings,
    const std::string& baseDirectory, checker::Program* host,
    const CompilerOptions* compilerOptions);
std::string tryGetModuleNameFromPaths(
    const std::string& relativeToBaseUrl,
    const collections::OrderedMap<std::string, std::vector<std::string>>* paths,
    const std::vector<ModuleSpecifierEnding>& allowedEndings,
    const std::string& baseDirectory, checker::Program* host,
    const CompilerOptions* compilerOptions);
bool validateEnding(const specPair& c, const std::string& relativeToBaseUrl,
                    const CompilerOptions* compilerOptions,
                    checker::Program* host);
std::string tryGetModuleNameFromExportsOrImports(
    const CompilerOptions* options, checker::Program* host,
    const std::string& targetFilePath, const std::string& packageDirectory,
    const std::string& packageName,
    const packagejson::ExportsOrImports& exports,
    const std::vector<std::string>& conditions, MatchingMode mode,
    bool isImports, bool preferTsExtension);
std::string getModuleSpecifierWithPreferences(
    const CompilerOptions* compilerOptions, checker::Program* host,
    SourceFile* importingSourceFile,
    const std::string& importingSourceFileName,
    const std::string& oldImportSpecifier, const std::string& toFileName,
    const UserPreferences& userPreferences,
    const ModuleSpecifierOptions& options);

// ---------------------------------------------------------------------------
// specifiers.go:19 — GetModuleSpecifiers
// ---------------------------------------------------------------------------

ModuleSpecifiersResult GetModuleSpecifiers(
    Symbol* moduleSymbol, checker::Checker* checker,
    const CompilerOptions* compilerOptions, SourceFile* importingSourceFile,
    checker::Program* host, const UserPreferences& userPreferences,
    const ModuleSpecifierOptions& options, bool forAutoImports) {
	return GetModuleSpecifiersWithInfo(
	    moduleSymbol, checker, compilerOptions, importingSourceFile, host,
	    userPreferences, options, forAutoImports);
}

// specifiers.go:41 — GetModuleSpecifiersWithInfo
ModuleSpecifiersResult GetModuleSpecifiersWithInfo(
    Symbol* moduleSymbol, checker::Checker* checker,
    const CompilerOptions* compilerOptions, SourceFile* importingSourceFile,
    checker::Program* host, const UserPreferences& userPreferences,
    const ModuleSpecifierOptions& options, bool forAutoImports) {
	auto ambient = tryGetModuleNameFromAmbientModule(moduleSymbol, checker);
	if (!ambient.name.empty()) {
		if (forAutoImports &&
		    IsExcludedByRegex(ambient.name,
		                      userPreferences.AutoImportSpecifierExcludeRegexes)) {
			return ModuleSpecifiersResult{{}, ResultKind::Ambient,
			                              ambient.symbol};
		}
		return ModuleSpecifiersResult{{ambient.name}, ResultKind::Ambient,
		                              ambient.symbol};
	}

	auto* moduleSourceFile = getSourceFileOfModule(moduleSymbol);
	if (moduleSourceFile == nullptr) {
		return ModuleSpecifiersResult{};
	}

	// Use original source file name when file is from project reference output
	auto moduleFileName =
	    host->GetSourceOfProjectReferenceIfOutputIncluded(moduleSourceFile);

	auto [specifiers, kind] = GetModuleSpecifiersForFileWithInfo(
	    importingSourceFile, moduleFileName, compilerOptions, host,
	    userPreferences, options, forAutoImports);
	return ModuleSpecifiersResult{specifiers, kind, nullptr};
}

// specifiers.go:79 — GetModuleSpecifiersForFileWithInfo
std::pair<std::vector<std::string>, ResultKind>
GetModuleSpecifiersForFileWithInfo(
    SourceFile* importingSourceFile, const std::string& moduleFileName,
    const CompilerOptions* compilerOptions, checker::Program* host,
    const UserPreferences& userPreferences,
    const ModuleSpecifierOptions& options, bool forAutoImports) {
	auto modulePaths = getAllModulePathsWorker(
	    getInfo(host->GetSourceOfProjectReferenceIfOutputIncluded(
	                importingSourceFile),
	            host),
	    moduleFileName, host, compilerOptions, options);

	return computeModuleSpecifiers(modulePaths, compilerOptions,
	                               importingSourceFile, host, userPreferences,
	                               options, forAutoImports);
}

// specifiers.go:112 — tryGetModuleNameFromAmbientModule
ambientModuleInfo tryGetModuleNameFromAmbientModule(Symbol* moduleSymbol,
                                                    checker::Checker* checker) {
	for (auto* decl : moduleSymbol->declarations) {
		if (isModuleWithStringLiteralName(decl) &&
		    (!isModuleAugmentationExternal(decl) ||
		     !tspath::isExternalModuleNameRelative(decl->name()->text()))) {
			return ambientModuleInfo{std::string{decl->name()->text()},
			                         moduleSymbol};
		}
	}

	// the module could be a namespace, which is export through "export=" from an ambient module.
	/**
	 * declare module "m" {
	 *     namespace ns {
	 *         class c {}
	 *     }
	 *     export = ns;
	 * }
	 */
	// `import {c} from "m";` is valid, in which case, `moduleSymbol` is "ns",
	// but the module name should be "m"
	for (auto* d : moduleSymbol->declarations) {
		if (!isModuleDeclaration(d)) {
			continue;
		}

		auto* possibleContainer =
		    findAncestor(d, [](Node* n) { return isModuleWithStringLiteralName(n); });
		if (possibleContainer == nullptr ||
		    possibleContainer->parent == nullptr ||
		    !isSourceFile(possibleContainer->parent)) {
			continue;
		}

		auto* sym = [&]() -> Symbol* {
			auto& exports = possibleContainer->symbol()->exports;
			auto it = exports.find(InternalSymbolNameExportEquals);
			return it != exports.end() ? it->second : nullptr;
		}();
		if (sym == nullptr) {
			continue;
		}
		auto* exportAssignmentDecl = sym->valueDeclaration;
		if (exportAssignmentDecl == nullptr ||
		    exportAssignmentDecl->kind != Kind::ExportAssignment) {
			continue;
		}
		auto* exportSymbol =
		    checker->GetSymbolAtLocation(exportAssignmentDecl->expression());
		if (exportSymbol == nullptr) {
			continue;
		}
		if ((exportSymbol->flags & SymbolFlagsAlias) != 0) {
			exportSymbol = checker->GetAliasedSymbol(exportSymbol);
		}
		// TODO: Possible strada bug - isn't this insufficient in the presence
		// of merge symbols?
		if (exportSymbol == d->symbol()) {
			return ambientModuleInfo{
			    std::string{possibleContainer->name()->text()},
			    possibleContainer->symbol()};
		}
	}
	return ambientModuleInfo{};
}

// ---------------------------------------------------------------------------
// specifiers.go:168 — getInfo (declared in types.h)
// ---------------------------------------------------------------------------

Info getInfo(const std::string& importingSourceFileName,
             checker::Program* host) {
	auto sourceDirectory = tspath::getDirectoryPath(importingSourceFileName);
	return Info{host->UseCaseSensitiveFileNames(), importingSourceFileName,
	            sourceDirectory};
}

// specifiers.go:180 — getAllModulePaths
std::vector<ModulePath> getAllModulePaths(
    const Info& info, const std::string& importedFileName,
    checker::Program* host, const CompilerOptions* compilerOptions,
    const UserPreferences& preferences, const ModuleSpecifierOptions& options) {
	// !!! use new cache model
	// importingFilePath := tspath.ToPath(info.ImportingSourceFileName, host.GetCurrentDirectory(), host.UseCaseSensitiveFileNames());
	// importedFilePath := tspath.ToPath(importedFileName, host.GetCurrentDirectory(), host.UseCaseSensitiveFileNames());
	// cache := host.getModuleSpecifierCache();
	// if (cache != nil) {
	//     cached := cache.get(importingFilePath, importedFilePath, preferences, options);
	//     if (cached.modulePaths) {return cached.modulePaths;}
	// }
	auto modulePaths = getAllModulePathsWorker(info, importedFileName, host,
	                                           compilerOptions, options);
	// if (cache != nil) {
	//     cache.setModulePaths(importingFilePath, importedFilePath, preferences, options, modulePaths);
	// }
	return modulePaths;
}

// specifiers.go:203 — getAllModulePathsWorker
std::vector<ModulePath> getAllModulePathsWorker(
    const Info& info, const std::string& importedFileName,
    checker::Program* host, const CompilerOptions* /*compilerOptions*/,
    const ModuleSpecifierOptions& /*options*/) {
	std::unordered_map<std::string, ModulePath> allFileNames;
	auto paths = GetEachFileNameOfModule(info.ImportingSourceFileName,
	                                     importedFileName, host, true);
	for (auto& p : paths) {
		allFileNames[p.FileName] = p;
	}

	bool useCaseSensitiveFileNames = info.UseCaseSensitiveFileNames;
	auto comparePaths = [useCaseSensitiveFileNames](const ModulePath& a,
	                                                const ModulePath& b) {
		return comparePathsByRedirect(a, b, useCaseSensitiveFileNames);
	};

	// Sort by paths closest to importing file Name directory
	std::vector<ModulePath> sortedPaths;
	sortedPaths.reserve(paths.size());
	for (auto directory = info.SourceDirectory; !allFileNames.empty();) {
		auto directoryStart =
		    tspath::ensureTrailingDirectorySeparator(directory);
		std::vector<ModulePath> pathsInDirectory;
		for (auto it = allFileNames.begin(); it != allFileNames.end();) {
			if (it->first.compare(0, directoryStart.size(), directoryStart) ==
			    0) {
				pathsInDirectory.push_back(it->second);
				it = allFileNames.erase(it);
			} else {
				++it;
			}
		}
		if (!pathsInDirectory.empty()) {
			std::sort(pathsInDirectory.begin(), pathsInDirectory.end(),
			          [&](const ModulePath& a, const ModulePath& b) {
				          return comparePaths(a, b) < 0;
			          });
			sortedPaths.insert(sortedPaths.end(), pathsInDirectory.begin(),
			                   pathsInDirectory.end());
		}
		auto newDirectory = tspath::getDirectoryPath(directory);
		if (newDirectory == directory) {
			break;
		}
		directory = newDirectory;
	}
	if (!allFileNames.empty()) {
		std::vector<ModulePath> remainingPaths;
		remainingPaths.reserve(allFileNames.size());
		for (auto& kv : allFileNames) {
			remainingPaths.push_back(kv.second);
		}
		std::sort(remainingPaths.begin(), remainingPaths.end(),
		          [&](const ModulePath& a, const ModulePath& b) {
			          return comparePaths(a, b) < 0;
		          });
		sortedPaths.insert(sortedPaths.end(), remainingPaths.begin(),
		                   remainingPaths.end());
	}
	return sortedPaths;
}

// specifiers.go:259 — ContainsNodeModules checks if a path contains the
// node_modules directory.
bool ContainsNodeModules(const std::string& s) {
	return s.find("/node_modules/") != std::string::npos;
}

// specifiers.go:265 — GetEachFileNameOfModule returns all possible file paths
// for a module, including symlink alternatives. This function handles symlink
// resolution and provides multiple path options for module resolution.
std::vector<ModulePath> GetEachFileNameOfModule(
    const std::string& importingFileName, const std::string& importedFileName,
    checker::Program* host, bool preferSymlinks) {
	auto cwd = host->GetCurrentDirectory();
	auto importedPath = tspath::toPath(importedFileName, cwd,
	                                   host->UseCaseSensitiveFileNames());
	std::string referenceRedirect;
	auto* outputAndReference =
	    host->GetProjectReferenceFromSource(importedPath);
	if (outputAndReference != nullptr && !outputAndReference->outputDts.empty()) {
		referenceRedirect = outputAndReference->outputDts;
	}

	auto redirects = host->GetRedirectTargets(importedPath);
	std::vector<std::string> importedFileNames;
	importedFileNames.reserve(2 + redirects.size());
	if (!referenceRedirect.empty()) {
		importedFileNames.push_back(referenceRedirect);
	}
	importedFileNames.push_back(importedFileName);
	importedFileNames.insert(importedFileNames.end(), redirects.begin(),
	                         redirects.end());
	std::vector<std::string> targets;
	targets.reserve(importedFileNames.size());
	for (auto& f : importedFileNames) {
		targets.push_back(tspath::getNormalizedAbsolutePath(f, cwd));
	}
	bool shouldFilterIgnoredPaths =
	    !everySlice(targets, [](const std::string& t) {
		    return containsIgnoredPath(t);
	    });

	std::vector<ModulePath> results;
	results.reserve(2);
	if (!preferSymlinks) {
		for (auto& p : targets) {
			if (!(shouldFilterIgnoredPaths && containsIgnoredPath(p))) {
				results.push_back(
				    ModulePath{p, ContainsNodeModules(p),
				               referenceRedirect == p});
			}
		}
	}

	auto* symlinkCache = host->GetSymlinkCache();
	auto fullImportedFileName =
	    tspath::getNormalizedAbsolutePath(importedFileName, cwd);
	if (symlinkCache != nullptr) {
		tspath::forEachAncestorDirectoryStoppingAtGlobalCache<bool>(
		    host->GetGlobalTypingsCacheLocation(),
		    tspath::getDirectoryPath(fullImportedFileName),
		    [&](std::string_view realPathDirectory) -> std::pair<bool, bool> {
			    auto symlinkSetEntry =
			        symlinkCache->DirectoriesByRealpath()->Load(
			            tspath::ensureTrailingDirectorySeparator(
			                tspath::toPath(realPathDirectory, cwd,
			                               host->UseCaseSensitiveFileNames())));
			    if (!symlinkSetEntry.second) {
				    return {false, false};
			    } // Continue to ancestor directory
			    auto symlinkSet = symlinkSetEntry.first;

			    // Don't want to a package to globally import from itself
			    // (importNameCodeFix_symlink_own_package.ts)
			    if (startsWithDirectory(importingFileName, realPathDirectory,
			                            host->UseCaseSensitiveFileNames())) {
				    return {false, true}; // Stop search, each ancestor
				                          // directory will also hit this condition
			    }

			    for (auto& target : targets) {
				    if (!startsWithDirectory(target, realPathDirectory,
				                             host->UseCaseSensitiveFileNames())) {
					    continue;
				    }

				    auto relative = tspath::getRelativePathFromDirectory(
				        realPathDirectory, target,
				        tspath::ComparePathsOptions{
				            host->UseCaseSensitiveFileNames(), cwd});
				    symlinkSet->Range(
				        [&](const std::string& symlinkDirectory) {
					        auto option = tspath::resolvePath(
					            symlinkDirectory, {relative});
					        results.push_back(ModulePath{
					            option, ContainsNodeModules(option),
					            target == referenceRedirect});
					        shouldFilterIgnoredPaths = true; // We found a
					        // non-ignored path in symlinks, so we can reject
					        // ignored-path realpaths
					        return true;
				        });
			    }

			    return {false, false};
		    });
	}

	if (preferSymlinks) {
		for (auto& p : targets) {
			if (!(shouldFilterIgnoredPaths && containsIgnoredPath(p))) {
				results.push_back(
				    ModulePath{p, ContainsNodeModules(p),
				               referenceRedirect == p});
			}
		}
	}

	return results;
}

// specifiers.go:364 — computeModuleSpecifiers
std::pair<std::vector<std::string>, ResultKind> computeModuleSpecifiers(
    const std::vector<ModulePath>& modulePaths,
    const CompilerOptions* compilerOptions, SourceFile* importingSourceFile,
    checker::Program* host, const UserPreferences& userPreferences,
    const ModuleSpecifierOptions& options, bool forAutoImport) {
	auto info = getInfo(importingSourceFile->FileName(), host);
	auto preferences =
	    getModuleSpecifierPreferences(userPreferences, host, compilerOptions,
	                                  importingSourceFile, "");

	std::string existingSpecifier;
	for (auto& modulePath : modulePaths) {
		auto targetPath = tspath::toPath(modulePath.FileName,
		                                 host->GetCurrentDirectory(),
		                                 info.UseCaseSensitiveFileNames);
		Node* existingImport = nullptr;
		for (auto* importSpecifier : importingSourceFile->imports) {
			auto* resolvedModule = host->GetResolvedModuleFromModuleSpecifier(
			    importingSourceFile, importSpecifier);
			// Go's ResolvedModule.IsResolved is nil-safe.
			if (resolvedModule != nullptr && resolvedModule->IsResolved() &&
			    tspath::toPath(resolvedModule->ResolvedFileName,
			                   host->GetCurrentDirectory(),
			                   info.UseCaseSensitiveFileNames) == targetPath) {
				existingImport = importSpecifier;
				break;
			}
		}
		if (existingImport != nullptr) {
			if (preferences.relativePreference ==
			        RelativePreferenceKind::NonRelative &&
			    tspath::pathIsRelative(existingImport->text())) {
				// If the preference is for non-relative and the module
				// specifier is relative, ignore it
				continue;
			}
			auto existingMode = host->GetModeForUsageLocation(
			    importingSourceFile, existingImport);
			auto targetMode = options.OverrideImportMode;
			if (targetMode == ResolutionModeNone) {
				targetMode =
				    host->GetDefaultResolutionModeForFile(importingSourceFile);
			}
			if (existingMode != targetMode &&
			    existingMode != ResolutionModeNone &&
			    targetMode != ResolutionModeNone) {
				// If the candidate import mode doesn't match the mode we're
				// generating for, don't consider it
				continue;
			}
			existingSpecifier = std::string{existingImport->text()};
			break;
		}
	}

	if (!existingSpecifier.empty()) {
		return {{existingSpecifier}, ResultKind::None};
	}

	bool importedFileIsInNodeModules = someSlice(
	    modulePaths, [](const ModulePath& p) { return p.IsInNodeModules; });

	// Module specifier priority:
	//   1. "Bare package specifiers" (e.g. "@foo/bar") resulting from a path
	//      through node_modules to a package.json's "types" entry
	//   2. Specifiers generated using "paths" from tsconfig
	//   3. Non-relative specfiers resulting from a path through node_modules
	//      (e.g. "@foo/bar/path/to/file")
	//   4. Relative paths
	std::vector<std::string> pathsSpecifiers;
	std::vector<std::string> redirectPathsSpecifiers;
	std::vector<std::string> nodeModulesSpecifiers;
	std::vector<std::string> relativeSpecifiers;

	for (auto& modulePath : modulePaths) {
		std::string specifier;
		if (modulePath.IsInNodeModules) {
			specifier = tryGetModuleNameAsNodeModule(
			    modulePath, info, importingSourceFile, host, compilerOptions,
			    userPreferences, /*packageNameOnly*/ false,
			    options.OverrideImportMode);
		}
		if (!specifier.empty() &&
		    !(forAutoImport &&
		      IsExcludedByRegex(specifier, preferences.excludeRegexes))) {
			nodeModulesSpecifiers.push_back(specifier);
			if (modulePath.IsRedirect) {
				// If we got a specifier for a redirect, it was a bare package
				// specifier (e.g. "@foo/bar", not "@foo/bar/path/to/file").
				// No other specifier will be this good, so stop looking.
				return {nodeModulesSpecifiers, ResultKind::NodeModules};
			}
		}

		auto importMode = options.OverrideImportMode;
		if (importMode == ResolutionModeNone) {
			importMode =
			    host->GetDefaultResolutionModeForFile(importingSourceFile);
		}
		auto local = getLocalModuleSpecifier(
		    modulePath.FileName, info, compilerOptions, host, importMode,
		    preferences,
		    /*pathsOnly*/ modulePath.IsRedirect || !specifier.empty());
		if (local.empty() ||
		    (forAutoImport &&
		     IsExcludedByRegex(local, preferences.excludeRegexes))) {
			continue;
		}
		if (modulePath.IsRedirect) {
			redirectPathsSpecifiers.push_back(local);
		} else if (PathIsBareSpecifier(local)) {
			if (ContainsNodeModules(local)) {
				// We could be in this branch due to inappropriate use of
				// `baseUrl`, not intentional `paths` usage. It's impossible
				// to reason about where to prioritize baseUrl-generated
				// module specifiers, but if they contain `/node_modules/`,
				// they're going to trigger a portability error, so *at
				// least* don't prioritize those.
				relativeSpecifiers.push_back(local);
			} else {
				pathsSpecifiers.push_back(local);
			}
		} else if (forAutoImport || !importedFileIsInNodeModules ||
		           modulePath.IsInNodeModules) {
			// Why this extra conditional, not just an `else`? If some path to
			// the file contained 'node_modules', but we can't create a
			// non-relative specifier (e.g. "@foo/bar/path/to/file"), that
			// means we had to go through a *sibling's* node_modules, not one
			// we can access directly. If some path to the file was in
			// node_modules but another was not, this likely indicates that we
			// have a monorepo structure with symlinks. In this case, the
			// non-nodeModules path is probably the realpath, e.g.
			// "../bar/path/to/file", but a relative path to another package
			// in a monorepo is probably not portable. So, the module
			// specifier we actually go with will be the relative path
			// through node_modules, so that the declaration emitter can
			// produce a portability error. (See
			// declarationEmitReexportedSymlinkReference3)
			relativeSpecifiers.push_back(local);
		}
	}

	if (!pathsSpecifiers.empty()) {
		return {pathsSpecifiers, ResultKind::Paths};
	}
	if (!redirectPathsSpecifiers.empty()) {
		return {redirectPathsSpecifiers, ResultKind::Redirect};
	}
	if (!nodeModulesSpecifiers.empty()) {
		return {nodeModulesSpecifiers, ResultKind::NodeModules};
	}
	return {relativeSpecifiers, ResultKind::Relative};
}

// specifiers.go:490 — getLocalModuleSpecifier
std::string getLocalModuleSpecifier(
    const std::string& moduleFileName, const Info& info,
    const CompilerOptions* compilerOptions, checker::Program* host,
    ResolutionMode importMode, const ModuleSpecifierPreferences& preferences,
    bool pathsOnly) {
	auto& paths = compilerOptions->Paths;
	auto& rootDirs = compilerOptions->RootDirs;

	if (pathsOnly && paths.empty()) {
		return "";
	}

	auto& sourceDirectory = info.SourceDirectory;

	auto allowedEndings =
	    preferences.getAllowedEndingsInPreferredOrder(importMode);
	std::string relativePath;
	if (!rootDirs.empty()) {
		relativePath = tryGetModuleNameFromRootDirs(
		    rootDirs, moduleFileName, sourceDirectory, allowedEndings,
		    compilerOptions, host);
	}
	if (relativePath.empty()) {
		relativePath = processEnding(
		    ensurePathIsNonModuleName(tspath::getRelativePathFromDirectory(
		        sourceDirectory, moduleFileName,
		        tspath::ComparePathsOptions{host->UseCaseSensitiveFileNames(),
		                                    host->GetCurrentDirectory()})),
		    allowedEndings, compilerOptions, host);
	}

	if ((paths.empty() && !compilerOptions->GetResolvePackageJsonImports()) ||
	    preferences.relativePreference == RelativePreferenceKind::Relative) {
		if (pathsOnly) {
			return "";
		}
		return relativePath;
	}

	auto root = compilerOptions->GetPathsBasePath(host->GetCurrentDirectory());
	auto baseDirectory =
	    tspath::getNormalizedAbsolutePath(root, host->GetCurrentDirectory());
	auto relativeToBaseUrl = getRelativePathIfInSameVolume(
	    moduleFileName, baseDirectory, host->UseCaseSensitiveFileNames());
	if (relativeToBaseUrl.empty()) {
		if (pathsOnly) {
			return "";
		}
		return relativePath;
	}

	std::string fromPackageJsonImports;
	if (!pathsOnly) {
		fromPackageJsonImports = tryGetModuleNameFromPackageJsonImports(
		    moduleFileName, sourceDirectory, compilerOptions, host, importMode,
		    prefersTsExtension(allowedEndings));
	}

	std::string fromPaths;
	if ((pathsOnly || fromPackageJsonImports.empty()) && !paths.empty()) {
		fromPaths = tryGetModuleNameFromPaths(relativeToBaseUrl, paths,
		                                      allowedEndings, baseDirectory,
		                                      host, compilerOptions);
	}

	if (pathsOnly) {
		return fromPaths;
	}

	std::string maybeNonRelative;
	if (!fromPackageJsonImports.empty()) {
		maybeNonRelative = fromPackageJsonImports;
	} else {
		maybeNonRelative = fromPaths;
	}
	if (maybeNonRelative.empty()) {
		return relativePath;
	}

	bool relativeIsExcluded =
	    IsExcludedByRegex(relativePath, preferences.excludeRegexes);
	bool nonRelativeIsExcluded =
	    IsExcludedByRegex(maybeNonRelative, preferences.excludeRegexes);
	if (!relativeIsExcluded && nonRelativeIsExcluded) {
		return relativePath;
	}
	if (relativeIsExcluded && !nonRelativeIsExcluded) {
		return maybeNonRelative;
	}

	if (preferences.relativePreference == RelativePreferenceKind::NonRelative &&
	    !tspath::pathIsRelative(maybeNonRelative)) {
		return maybeNonRelative;
	}

	if (preferences.relativePreference ==
	        RelativePreferenceKind::ExternalNonRelative &&
	    !tspath::pathIsRelative(maybeNonRelative)) {
		tspath::Path projectDirectory;
		if (!compilerOptions->ConfigFilePath.empty()) {
			projectDirectory =
			    tspath::toPath(tspath::getDirectoryPath(
			                       compilerOptions->ConfigFilePath),
			                   host->GetCurrentDirectory(),
			                   host->UseCaseSensitiveFileNames());
		} else {
			projectDirectory = tspath::toPath(host->GetCurrentDirectory(),
			                                  host->GetCurrentDirectory(),
			                                  host->UseCaseSensitiveFileNames());
		}
		auto canonicalSourceDirectory =
		    tspath::toPath(sourceDirectory, host->GetCurrentDirectory(),
		                   host->UseCaseSensitiveFileNames());
		auto modulePath = tspath::toPath(moduleFileName, projectDirectory,
		                                 host->UseCaseSensitiveFileNames());

		bool sourceIsInternal =
		    pathContainsPath(projectDirectory, canonicalSourceDirectory);
		bool targetIsInternal = pathContainsPath(projectDirectory, modulePath);
		if ((sourceIsInternal && !targetIsInternal) ||
		    (!sourceIsInternal && targetIsInternal)) {
			// 1. The import path crosses the boundary of the
			//    tsconfig.json-containing directory.
			//
			//      src/
			//        tsconfig.json
			//        index.ts -------
			//      lib/              | (path crosses tsconfig.json)
			//        imported.ts <---
			//
			return maybeNonRelative;
		}

		auto nearestTargetPackageJson =
		    host->GetNearestAncestorDirectoryWithPackageJson(
		        tspath::getDirectoryPath(modulePath));
		auto nearestSourcePackageJson =
		    host->GetNearestAncestorDirectoryWithPackageJson(sourceDirectory);

		if (!packageJsonPathsAreEqual(
		        nearestTargetPackageJson, nearestSourcePackageJson,
		        tspath::ComparePathsOptions{host->UseCaseSensitiveFileNames(),
		                                    host->GetCurrentDirectory()})) {
			// 2. The importing and imported files are part of different
			//    packages.
			//
			//      packages/a/
			//        package.json
			//        index.ts --------
			//      packages/b/        | (path crosses package.json)
			//        package.json     |
			//        component.ts <---
			//
			return maybeNonRelative;
		}

		return relativePath;
	}

	// Prefer a relative import over a baseUrl import if it has fewer
	// components.
	if (isPathRelativeToParent(maybeNonRelative) ||
	    CountPathComponents(relativePath) <
	        CountPathComponents(maybeNonRelative)) {
		return relativePath;
	}
	return maybeNonRelative;
}

// specifiers.go:641 — processEnding
std::string processEnding(
    const std::string& fileName,
    const std::vector<ModuleSpecifierEnding>& allowedEndings,
    const CompilerOptions* options, checker::Program* host) {
	if (tspath::fileExtensionIsOneOf(
	        fileName, {tspath::extensionJson, tspath::extensionMjs,
	                   tspath::extensionCjs})) {
		return fileName;
	}

	auto noExtension = tspath::removeFileExtension(fileName);
	if (fileName == noExtension) {
		return fileName;
	}

	int jsPriority =
	    indexOf(allowedEndings, ModuleSpecifierEnding::JsExtension);
	int tsPriority =
	    indexOf(allowedEndings, ModuleSpecifierEnding::TsExtension);
	if (tspath::fileExtensionIsOneOf(
	        fileName, {tspath::extensionMts, tspath::extensionCts}) &&
	    tsPriority != -1 && tsPriority < jsPriority) {
		return fileName;
	}
	if (tspath::fileExtensionIsOneOf(
	        fileName, {tspath::extensionDmts, tspath::extensionDcts})) {
		auto inputExt = tspath::getDeclarationFileExtension(fileName);
		auto ext = GetJSExtensionForDeclarationFileExtension(inputExt);
		return std::string{tspath::removeExtension(fileName, inputExt)} + ext;
	}
	if (tspath::fileExtensionIsOneOf(
	        fileName, {tspath::extensionMts, tspath::extensionCts})) {
		return std::string{noExtension} + getJSExtensionForFile(fileName, options);
	}
	if (!tspath::fileExtensionIsOneOf(fileName, {tspath::extensionDts}) &&
	    tspath::fileExtensionIsOneOf(fileName, {tspath::extensionTs}) &&
	    fileName.find(".d.") != std::string::npos) {
		// `foo.d.json.ts` and the like - remap back to `foo.json`
		if (auto result =
		        TryGetRealFileNameForNonJSDeclarationFileName(fileName);
		    !result.empty()) {
			return result;
		}
	}

	switch (allowedEndings[0]) {
	case ModuleSpecifierEnding::Minimal: {
		auto withoutIndex =
		    trimSuffix(std::string{noExtension}, "/index");
		if (host != nullptr && withoutIndex != noExtension &&
		    tryGetAnyFileFromPath(host, withoutIndex)) {
			// Can't remove index if there's a file by the same name as the
			// directory. Probably more callers should pass `host` so we can
			// determine this?
			return std::string{noExtension};
		}
		return withoutIndex;
	}
	case ModuleSpecifierEnding::Index:
		return std::string{noExtension};
	case ModuleSpecifierEnding::JsExtension:
		return std::string{noExtension} +
		       getJSExtensionForFile(fileName, options);
	case ModuleSpecifierEnding::TsExtension:
		// For now, we don't know if this import is going to be type-only,
		// which means we don't know if a .d.ts extension is valid, so use no
		// extension or a .js extension
		if (tspath::isDeclarationFileName(fileName)) {
			int extensionlessPriority = -1;
			for (size_t i = 0; i < allowedEndings.size(); i++) {
				auto e = allowedEndings[i];
				if (e == ModuleSpecifierEnding::Minimal ||
				    e == ModuleSpecifierEnding::Index) {
					extensionlessPriority = static_cast<int>(i);
					break;
				}
			}
			if (extensionlessPriority != -1 &&
			    extensionlessPriority < jsPriority) {
				return std::string{noExtension};
			}
			return std::string{noExtension} +
			       getJSExtensionForFile(fileName, options);
		}
		return fileName;
	default:
		TSC_UNREACHABLE("AssertNever");
	}
}

// specifiers.go:712 — tryGetModuleNameFromRootDirs
std::string tryGetModuleNameFromRootDirs(
    const std::vector<std::string>& rootDirs, const std::string& moduleFileName,
    const std::string& sourceDirectory,
    const std::vector<ModuleSpecifierEnding>& allowedEndings,
    const CompilerOptions* compilerOptions, checker::Program* host) {
	auto normalizedTargetPaths = getPathsRelativeToRootDirs(
	    moduleFileName, rootDirs, host->UseCaseSensitiveFileNames());
	if (normalizedTargetPaths.empty()) {
		return "";
	}

	auto normalizedSourcePaths = getPathsRelativeToRootDirs(
	    sourceDirectory, rootDirs, host->UseCaseSensitiveFileNames());
	std::string shortest;
	int shortestSepCount = 0;
	for (auto& sourcePath : normalizedSourcePaths) {
		for (auto& targetPath : normalizedTargetPaths) {
			auto candidate = ensurePathIsNonModuleName(
			    tspath::getRelativePathFromDirectory(
			        sourcePath, targetPath,
			        tspath::ComparePathsOptions{
			            host->UseCaseSensitiveFileNames(),
			            host->GetCurrentDirectory()}));
			int candidateSepCount =
			    static_cast<int>(std::count(candidate.begin(), candidate.end(), '/'));
			if (shortest.empty() || candidateSepCount < shortestSepCount) {
				shortest = candidate;
				shortestSepCount = candidateSepCount;
			}
		}
	}

	if (shortest.empty()) {
		return "";
	}
	return processEnding(shortest, allowedEndings, compilerOptions, host);
}

// specifiers.go:748 — tryGetModuleNameAsNodeModule
std::string tryGetModuleNameAsNodeModule(
    const ModulePath& pathObj, const Info& info,
    SourceFile* importingSourceFile, checker::Program* host,
    const CompilerOptions* options, const UserPreferences& userPreferences,
    bool packageNameOnly, ResolutionMode overrideMode) {
	auto parts = GetNodeModulePathParts(pathObj.FileName);
	if (!parts.has_value()) {
		return "";
	}

	// Simplify the full file path to something that can be resolved by Node.
	auto preferences =
	    getModuleSpecifierPreferences(userPreferences, host, options,
	                                  importingSourceFile, "");
	auto allowedEndings = preferences.getAllowedEndingsInPreferredOrder(
	    ResolutionModeNone);

	bool caseSensitive = host->UseCaseSensitiveFileNames();
	auto moduleSpecifier = pathObj.FileName;
	bool isPackageRootPath = false;
	if (!packageNameOnly) {
		int packageRootIndex = parts->PackageRootIndex;
		std::string moduleFileName;
		while (true) {
			// If the module could be imported by a directory name, use that
			// directory's name
			auto pkgJsonResults = tryDirectoryWithPackageJson(
			    *parts, pathObj, importingSourceFile, host, overrideMode,
			    options, allowedEndings);
			auto moduleFileToTry = pkgJsonResults.moduleFileToTry;
			auto packageRootPath = pkgJsonResults.packageRootPath;
			auto blockedByExports = pkgJsonResults.blockedByExports;
			auto verbatimFromExports = pkgJsonResults.verbatimFromExports;
			if (blockedByExports) {
				return ""; // File is under this package.json, but is not
				           // publicly exported - there's no way to name it
				           // via `node_modules` resolution
			}
			if (verbatimFromExports) {
				return moduleFileToTry;
			}
			//}
			if (!packageRootPath.empty()) {
				moduleSpecifier = packageRootPath;
				isPackageRootPath = true;
				break;
			}
			if (moduleFileName.empty()) {
				moduleFileName = moduleFileToTry;
			}
			// try with next level of directory
			packageRootIndex =
			    indexAfter(pathObj.FileName, "/", packageRootIndex + 1);
			if (packageRootIndex == -1) {
				moduleSpecifier =
				    processEnding(moduleFileName, allowedEndings, options, host);
				break;
			}
		}
	}

	if (pathObj.IsRedirect && !isPackageRootPath) {
		return "";
	}

	auto globalTypingsCacheLocation = host->GetGlobalTypingsCacheLocation();
	// Get a path that's relative to node_modules or the importing file's path
	// if node_modules folder is in this folder or any of its parent folders,
	// no need to keep it.
	auto pathToTopLevelNodeModules =
	    moduleSpecifier.substr(0, parts->TopLevelNodeModulesIndex);

	if (!stringutil::HasPrefix(info.SourceDirectory,
	                           pathToTopLevelNodeModules, caseSensitive) ||
	    (!globalTypingsCacheLocation.empty() &&
	     stringutil::HasPrefix(globalTypingsCacheLocation,
	                           pathToTopLevelNodeModules, caseSensitive))) {
		return "";
	}

	// If the module was found in @types, get the actual Node package name
	auto nodeModulesDirectoryName =
	    moduleSpecifier.substr(parts->TopLevelPackageNameIndex + 1);
	return module::GetPackageNameFromTypesPackageName(nodeModulesDirectoryName);
}

// specifiers.go:837 — tryDirectoryWithPackageJson
pkgJsonDirAttemptResult tryDirectoryWithPackageJson(
    const NodeModulePathParts& parts, const ModulePath& pathObj,
    SourceFile* importingSourceFile, checker::Program* host,
    ResolutionMode overrideMode, const CompilerOptions* options,
    const std::vector<ModuleSpecifierEnding>& allowedEndings) {
	auto rootIdx = parts.PackageRootIndex;
	if (rootIdx == -1) {
		rootIdx = static_cast<int>(pathObj.FileName.size()); // TODO: possible
		// strada bug? -1 in js slice removes characters from the end, in go
		// it panics - js behavior seems unwanted here?
	}
	auto packageRootPath = pathObj.FileName.substr(0, rootIdx);
	auto packageJsonPath =
	    tspath::combinePaths(packageRootPath, {"package.json"});
	auto moduleFileToTry = pathObj.FileName;
	bool maybeBlockedByTypesVersions = false;
	auto packageJson = host->GetPackageJsonInfo(packageJsonPath);
	if (packageJson == nullptr) {
		// No package.json exists; an index.js will still resolve as the
		// package name
		auto fileName =
		    moduleFileToTry.substr(parts.PackageRootIndex + 1);
		if (fileName == "index.d.ts" || fileName == "index.js" ||
		    fileName == "index.ts" || fileName == "index.tsx") {
			return pkgJsonDirAttemptResult{moduleFileToTry, packageRootPath,
			                               false, false};
		} else {
			return pkgJsonDirAttemptResult{moduleFileToTry, "", false,
			                               false};
		}
	}

	auto importMode = overrideMode;
	if (importMode == ResolutionModeNone) {
		importMode =
		    host->GetDefaultResolutionModeForFile(importingSourceFile);
	}

	auto* packageJsonContent = packageJson->GetContents();
	if (options->GetResolvePackageJsonExports()) {
		// The package name that we found in node_modules could be different
		// from the package name in the package.json content via url/filepath
		// dependency specifiers. We need to use the actual directory name, so
		// don't look at `packageJsonContent.name` here.
		auto nodeModulesDirectoryName =
		    packageRootPath.substr(parts.TopLevelPackageNameIndex + 1);
		auto packageName = module::GetPackageNameFromTypesPackageName(
		    nodeModulesDirectoryName);

		// Determine resolution mode for package.json exports condition
		// matching. TypeScript's tryDirectoryWithPackageJson uses the
		// importing file's mode (moduleSpecifiers.ts:1257), but this causes
		// incorrect exports resolution. We fix this by checking the target
		// file's extension using the logic from
		// getImpliedNodeFormatForEmitWorker (program.ts:4827-4838).
		// .cjs/.cts/.d.cts → CommonJS → "require" condition
		// .mjs/.mts/.d.mts → ESM → "import" condition
		if (tspath::fileExtensionIsOneOf(
		        pathObj.FileName,
		        {tspath::extensionCjs, tspath::extensionCts,
		         tspath::extensionDcts})) {
			importMode = ResolutionModeCommonJS;
		} else if (tspath::fileExtensionIsOneOf(
		               pathObj.FileName,
		               {tspath::extensionMjs, tspath::extensionMts,
		                tspath::extensionDmts})) {
			importMode = ResolutionModeESM;
		}

		auto conditions = module::GetConditions(*options, importMode);

		std::string fromExports;
		if (packageJsonContent != nullptr &&
		    packageJsonContent->Exports.type !=
		        packagejson::JSONValueType::NotPresent) {
			fromExports = tryGetModuleNameFromExports(
			    options, host, pathObj.FileName, packageRootPath, packageName,
			    packageJsonContent->Exports, conditions);
		}
		if (!fromExports.empty()) {
			return pkgJsonDirAttemptResult{fromExports, "", false,
			                               true};
		}
		if (packageJsonContent != nullptr &&
		    packageJsonContent->Exports.type !=
		        packagejson::JSONValueType::NotPresent) {
			return pkgJsonDirAttemptResult{pathObj.FileName, "", true,
			                               false};
		}
	}

	packagejson::VersionPaths* versionPaths = nullptr;
	if (packageJsonContent != nullptr &&
	    packageJsonContent->TypesVersions.type ==
	        packagejson::JSONValueType::Object) {
		versionPaths = packageJsonContent->GetVersionPaths({});
	}
	if (versionPaths != nullptr && versionPaths->GetPaths() != nullptr) {
		auto subModuleName =
		    pathObj.FileName.substr(packageRootPath.size() + 1);
		auto fromPaths = tryGetModuleNameFromPaths(
		    subModuleName, versionPaths->GetPaths(), allowedEndings,
		    packageRootPath, host, options);
		if (fromPaths.empty()) {
			maybeBlockedByTypesVersions = true;
		} else {
			moduleFileToTry =
			    tspath::combinePaths(packageRootPath, {fromPaths});
		}
	}
	// If the file is the main module, it can be imported by the package name
	std::string mainFileRelative = "index.js";
	if (packageJsonContent != nullptr) {
		if (packageJsonContent->Typings.Valid) {
			mainFileRelative = packageJsonContent->Typings.Value;
		} else if (packageJsonContent->Types.Valid) {
			mainFileRelative = packageJsonContent->Types.Value;
		} else if (packageJsonContent->Main.Valid) {
			mainFileRelative = packageJsonContent->Main.Value;
		}
	}

	// Go evaluates MatchPatternOrExact lazily behind maybeBlockedByTypesVersions;
	// the lambda preserves the short-circuit (versionPaths is non-null
	// whenever maybeBlockedByTypesVersions is true).
	bool matchedKeyInTypesVersions = maybeBlockedByTypesVersions && [&] {
		auto matched = module::MatchPatternOrExact(
		    module::TryParsePatterns(versionPaths->GetPaths()).get(),
		    mainFileRelative);
		return !(matched.text.empty() && matched.starIndex == -1);
	}();
	if (!mainFileRelative.empty() && !matchedKeyInTypesVersions) {
		// The 'main' file is also subject to mapping through typesVersions,
		// and we couldn't come up with a path explicitly through
		// typesVersions, so if it matches a key in typesVersions now, it's
		// not reachable. (The only way this can happen is if some file in a
		// package that's not resolvable from outside the package got pulled
		// into the program anyway, e.g. transitively through a file that *is*
		// reachable. It happens very easily in fourslash tests though, since
		// every test file listed gets included. See
		// importNameCodeFix_typesVersions.ts for an example.)
		auto mainExportFile = tspath::toPath(
		    mainFileRelative, packageRootPath, host->UseCaseSensitiveFileNames());
		auto compareOpt =
		    tspath::ComparePathsOptions{host->UseCaseSensitiveFileNames(),
		                                host->GetCurrentDirectory()};
		if (tspath::comparePaths(
		        std::string{tspath::removeFileExtension(mainExportFile)},
		        std::string{tspath::removeFileExtension(moduleFileToTry)},
		        compareOpt) == 0) {
			// ^ An arbitrary removal of file extension for this comparison
			// is almost certainly wrong
			return pkgJsonDirAttemptResult{moduleFileToTry, packageRootPath,
			                               false, false};
		} else if ((packageJsonContent == nullptr ||
		            packageJsonContent->Type.Value != "module") &&
		           !tspath::fileExtensionIsOneOf(
		               moduleFileToTry,
		               tspath::extensionsNotSupportingExtensionlessResolution) &&
		           stringutil::HasPrefix(moduleFileToTry, mainExportFile,
		                                 host->UseCaseSensitiveFileNames()) &&
		           tspath::comparePaths(
		               tspath::getDirectoryPath(moduleFileToTry),
		               tspath::removeTrailingDirectorySeparator(mainExportFile),
		               compareOpt) == 0 &&
		           tspath::removeFileExtension(
		               tspath::getBaseFileName(moduleFileToTry)) == "index") {
			// if mainExportFile is a directory, which contains
			// moduleFileToTry, we just try index file example
			// mainExportFile: `pkg/lib` and moduleFileToTry: `pkg/lib/index`,
			// we can use packageRootPath but this behavior is deprecated for
			// packages with "type": "module", so we only do this for packages
			// without "type": "module" and make sure that the extension on
			// index.{???} is something that supports omitting the extension
			return pkgJsonDirAttemptResult{moduleFileToTry, packageRootPath,
			                               false, false};
		}
	}

	return pkgJsonDirAttemptResult{moduleFileToTry, "", false, false};
}

// specifiers.go:981 — tryGetModuleNameFromExports
std::string tryGetModuleNameFromExports(
    const CompilerOptions* options, checker::Program* host,
    const std::string& targetFilePath, const std::string& packageDirectory,
    const std::string& packageName,
    const packagejson::ExportsOrImports& exports,
    const std::vector<std::string>& conditions) {
	if (exports.IsSubpaths()) {
		// sub-mappings
		// 3 cases:
		// * directory mappings (legacyish, key ends with / (technically
		//   allows index/extension resolution under cjs mode))
		// * pattern mappings (contains a *)
		// * exact mappings (no *, does not end with /)
		auto* obj = exports.AsObject();
		for (auto& k : obj->Keys()) {
			auto* subk = obj->Get(k).first;
			auto subPackageName = tspath::getNormalizedAbsolutePath(
			    tspath::combinePaths(packageName, {k}), "");
			auto mode = MatchingMode::Exact;
			if (!k.empty() && k.back() == '/') {
				mode = MatchingMode::Directory;
			} else if (k.find('*') != std::string::npos) {
				mode = MatchingMode::Pattern;
			}
			auto result = tryGetModuleNameFromExportsOrImports(
			    options, host, targetFilePath, packageDirectory,
			    subPackageName, *subk, conditions, mode, /*isImports*/ false,
			    /*preferTsExtension*/ false);
			if (!result.empty()) {
				return result;
			}
		}
	}
	return tryGetModuleNameFromExportsOrImports(
	    options, host, targetFilePath, packageDirectory, packageName, exports,
	    conditions, MatchingMode::Exact,
	    /*isImports*/ false,
	    /*preferTsExtension*/ false);
}

// specifiers.go:1024 — tryGetModuleNameFromPackageJsonImports
std::string tryGetModuleNameFromPackageJsonImports(
    const std::string& moduleFileName, const std::string& sourceDirectory,
    const CompilerOptions* options, checker::Program* host,
    ResolutionMode importMode, bool preferTsExtension) {
	if (!options->GetResolvePackageJsonImports()) {
		return "";
	}

	auto ancestorDirectoryWithPackageJson =
	    host->GetNearestAncestorDirectoryWithPackageJson(sourceDirectory);
	if (ancestorDirectoryWithPackageJson.empty()) {
		return "";
	}
	auto packageJsonPath = tspath::combinePaths(
	    ancestorDirectoryWithPackageJson, {"package.json"});

	auto info = host->GetPackageJsonInfo(packageJsonPath);
	if (info == nullptr) {
		return "";
	}

	auto& imports = info->GetContents()->Imports;
	switch (imports.type) {
	case packagejson::JSONValueType::NotPresent:
	case packagejson::JSONValueType::Array:
	case packagejson::JSONValueType::String:
		return ""; // not present or invalid for imports
	case packagejson::JSONValueType::Object: {
		auto conditions = module::GetConditions(*options, importMode);
		auto* top = imports.AsObject();
		for (auto& k : top->Keys()) {
			auto* value = top->Get(k).first;
			if (k == "#" || k == "#/" || k.empty() || k[0] != '#') {
				continue; // invalid imports entry
			}
			if (k.size() >= 2 && k.compare(0, 2, "#/") == 0 &&
			    options->GetModuleResolutionKind() !=
			        ModuleResolutionKind::NodeNext &&
			    options->GetModuleResolutionKind() !=
			        ModuleResolutionKind::Bundler) {
				continue; // "#/" imports keys are only valid in
				          // nodenext/bundler
			}
			auto mode = MatchingMode::Exact;
			if (!k.empty() && k.back() == '/') {
				mode = MatchingMode::Directory;
			} else if (k.find('*') != std::string::npos) {
				mode = MatchingMode::Pattern;
			}
			auto result = tryGetModuleNameFromExportsOrImports(
			    options, host, moduleFileName,
			    ancestorDirectoryWithPackageJson, k, *value, conditions,
			    mode, true, preferTsExtension);
			if (!result.empty()) {
				return result;
			}
		}
	} break;
	default:
		break;
	}

	return "";
}

// specifiers.go:1094 — tryGetModuleNameFromPaths. Go takes
// *collections.OrderedMap[string, []string]; the two call sites hand us
// either CompilerOptions::Paths (vector of pairs) or
// packagejson::VersionPaths::GetPaths() (OrderedMap), so the worker takes
// materialized entries and the OrderedMap overload adapts.
std::string tryGetModuleNameFromPaths(
    const std::string& relativeToBaseUrl,
    const std::vector<std::pair<std::string, std::vector<std::string>>>& paths,
    const std::vector<ModuleSpecifierEnding>& allowedEndings,
    const std::string& baseDirectory, checker::Program* host,
    const CompilerOptions* compilerOptions) {
	bool caseSensitive = host->UseCaseSensitiveFileNames();
	for (auto& entry : paths) {
		auto& key = entry.first;
		auto& values = entry.second;
		for (auto& patternText : values) {
			auto normalized = tspath::normalizePath(patternText);
			auto pattern = getRelativePathIfInSameVolume(normalized,
			                                             baseDirectory,
			                                             caseSensitive);
			if (pattern.empty()) {
				pattern = normalized;
			}
			bool ok;
			auto [prefix, suffix] = cut(pattern, "*", ok);

			// In module resolution, if `pattern` itself has an extension, a
			// file with that extension is looked up directly, meaning a '.ts'
			// or '.d.ts' extension is allowed to resolve. This is distinct
			// from the case where a '*' substitution causes a module
			// specifier to have an extension, i.e. the extension comes from
			// the module specifier in a JS/TS file and matches the '*'. For
			// example:
			//
			// Module Specifier      | Path Mapping (key: [pattern]) | Interpolation       | Resolution Action
			// ---------------------->------------------------------->--------------------->---------------------------------------------------------------
			// import "@app/foo"    -> "@app/*": ["./src/app/*.ts"] -> "./src/app/foo.ts" -> tryFile("./src/app/foo.ts") || [continue resolution algorithm]
			// import "@app/foo.ts" -> "@app/*": ["./src/app/*"]    -> "./src/app/foo.ts" -> [continue resolution algorithm]
			//
			// (https://github.com/microsoft/TypeScript/blob/ad4ded80e1d58f0bf36ac16bea71bc10d9f09895/src/compiler/moduleNameResolver.ts#L2509-L2516)
			//
			// The interpolation produced by both scenarios is identical, but
			// only in the former, where the extension is encoded in the path
			// mapping rather than in the module specifier, will we prioritize
			// a file lookup on the interpolation result. (In fact, currently,
			// the latter scenario will necessarily fail since no resolution
			// mode recognizes '.ts' as a valid extension for a module
			// specifier.)
			//
			// Here, this means we need to be careful about whether we
			// generate a match from the target filename (typically with a
			// .ts extension) or the possible relative module specifiers
			// representing that file:
			//
			// Filename            | Relative Module Specifier Candidates         | Path Mapping                 | Filename Result    | Module Specifier Results
			// --------------------<----------------------------------------------<------------------------------<-------------------||----------------------------
			// dist/haha.d.ts      <- dist/haha, dist/haha.js                     <- "@app/*": ["./dist/*.d.ts"] <- @app/haha        || (none)
			// dist/haha.d.ts      <- dist/haha, dist/haha.js                     <- "@app/*": ["./dist/*"]      <- (none)           || @app/haha, @app/haha.js
			// dist/foo/index.d.ts <- dist/foo, dist/foo/index, dist/foo/index.js <- "@app/*": ["./dist/*.d.ts"] <- @app/foo/index   || (none)
			// dist/foo/index.d.ts <- dist/foo, dist/foo/index, dist/foo/index.js <- "@app/*": ["./dist/*"]      <- (none)           || @app/foo, @app/foo/index, @app/foo/index.js
			// dist/wow.js.js      <- dist/wow.js, dist/wow.js.js                 <- "@app/*": ["./dist/*.js"]   <- @app/wow.js      || @app/wow, @app/wow.js
			//
			// The "Filename Result" can be generated only if `pattern` has an
			// extension. Care must be taken that the list of relative module
			// specifiers to run the interpolation (a) is actually valid for
			// the module resolution mode, (b) takes into account the
			// existence of other files (e.g. 'dist/wow.js' cannot refer to
			// 'dist/wow.js.js' if 'dist/wow.js' exists) and (c) that they are
			// ordered by preference. The last row shows that the filename
			// result and module specifier results are not mutually exclusive.
			// Note that the filename result is a higher priority in module
			// resolution, but as long criteria (b) above is met, I don't
			// think its result needs to be the highest priority result in
			// module specifier generation. I have included it last, as it's
			// difficult to tell exactly where it should be sorted among the
			// others for a particular value of `importModuleSpecifierEnding`.

			std::vector<specPair> candidates;
			for (auto ending : allowedEndings) {
				auto result =
				    processEnding(relativeToBaseUrl, {ending},
				                  compilerOptions, host);
				candidates.push_back(specPair{ending, result});
			}
			if (!tspath::tryGetExtensionFromPath(pattern).empty()) {
				candidates.push_back(
				    specPair{ModuleSpecifierEnding::JsExtension,
				             relativeToBaseUrl});
			}

			if (ok) {
				for (auto& c : candidates) {
					auto& value = c.value;
					if (value.size() >= prefix.size() + suffix.size() &&
					    stringutil::HasPrefix(value, prefix, caseSensitive) && // TODO: possible strada bug: these are not case-switched in strada
					    stringutil::HasSuffix(value, suffix, caseSensitive) &&
					    validateEnding(c, relativeToBaseUrl, compilerOptions,
					                   host)) {
						auto matchedStar = value.substr(
						    prefix.size(),
						    value.size() - prefix.size() - suffix.size());
						if (!tspath::pathIsRelative(matchedStar)) {
							return replaceFirstStar(key, matchedStar);
						}
					}
				}
			} else if (someSlice(candidates, [&](const specPair& c) {
				           return c.ending !=
				                      ModuleSpecifierEnding::Minimal &&
				                  pattern == c.value;
			           }) ||
			           someSlice(candidates, [&](const specPair& c) {
				           return c.ending ==
				                      ModuleSpecifierEnding::Minimal &&
				                  pattern == c.value &&
				                  validateEnding(c, relativeToBaseUrl,
				                                 compilerOptions, host);
			           })) {
				return key;
			}
		}
	}
	return "";
}

std::string tryGetModuleNameFromPaths(
    const std::string& relativeToBaseUrl,
    const collections::OrderedMap<std::string, std::vector<std::string>>* paths,
    const std::vector<ModuleSpecifierEnding>& allowedEndings,
    const std::string& baseDirectory, checker::Program* host,
    const CompilerOptions* compilerOptions) {
	std::vector<std::pair<std::string, std::vector<std::string>>> entries;
	entries.reserve(paths->Size());
	for (auto& key : paths->Keys()) {
		auto* value = paths->Get(key).first;
		entries.emplace_back(key, *value);
	}
	return tryGetModuleNameFromPaths(relativeToBaseUrl, entries, allowedEndings,
	                                 baseDirectory, host, compilerOptions);
}

// specifiers.go:1193 — validateEnding
bool validateEnding(const specPair& c, const std::string& relativeToBaseUrl,
                    const CompilerOptions* compilerOptions,
                    checker::Program* host) {
	// Optimization: `removeExtensionAndIndexPostFix` can query the file
	// system (a good bit) if `ending` is `Minimal`, the basename is 'index',
	// and a `host` is provided. To avoid that until it's unavoidable, we ran
	// the function with no `host` above. Only here, after we've checked that
	// the minimal ending is indeed a match (via the length and prefix/suffix
	// checks / `some` calls), do we check that the host-validated result is
	// consistent with the answer we got before. If it's not, it falls back to
	// the `ModuleSpecifierEnding.Index` result, which should already be in
	// the list of candidates if `Minimal` was. (Note: the assumption here is
	// that every module resolution mode that supports dropping extensions
	// also supports dropping `/index`. Like literally everything else in this
	// file, this logic needs to be updated if that's not true in some future
	// module resolution mode.)
	return c.ending != ModuleSpecifierEnding::Minimal ||
	       c.value == processEnding(relativeToBaseUrl, {c.ending},
	                                compilerOptions, host);
}

// specifiers.go:1204 — tryGetModuleNameFromExportsOrImports
std::string tryGetModuleNameFromExportsOrImports(
    const CompilerOptions* options, checker::Program* host,
    const std::string& targetFilePath, const std::string& packageDirectory,
    const std::string& packageName,
    const packagejson::ExportsOrImports& exports,
    const std::vector<std::string>& conditions, MatchingMode mode,
    bool isImports, bool preferTsExtension) {
	switch (exports.type) {
	case packagejson::JSONValueType::NotPresent:
		return "";
	case packagejson::JSONValueType::String: {
		const auto& strValue = exports.str;

		// possible strada bug? Always uses compilerOptions of the host
		// project, not those applicable to the targeted package.json!
		std::string outputFile;
		std::string declarationFile;
		if (isImports) {
			outputFile = outputpaths::GetOutputJSFileNameWorker(
			    targetFilePath, options, host);
			declarationFile =
			    outputpaths::GetOutputDeclarationFileNameWorker(
			        targetFilePath, options, host);
		}

		auto pathOrPattern = tspath::getNormalizedAbsolutePath(
		    tspath::combinePaths(packageDirectory, {strValue}), "");
		std::string extensionSwappedTarget;
		if (tspath::hasTSFileExtension(targetFilePath)) {
			extensionSwappedTarget =
			    std::string{tspath::removeFileExtension(targetFilePath)} +
			    std::string{module::TryGetJSExtensionForFile(targetFilePath,
			                                               *options)};
		}
		bool canTryTsExtension = preferTsExtension &&
		                         tspath::hasImplementationTSFileExtension(
		                             targetFilePath);

		auto compareOpts = tspath::ComparePathsOptions{
		    host->UseCaseSensitiveFileNames(), host->GetCurrentDirectory()};

		switch (mode) {
		case MatchingMode::Exact:
			if ((!extensionSwappedTarget.empty() &&
			     tspath::comparePaths(extensionSwappedTarget, pathOrPattern,
			                          compareOpts) == 0) ||
			    tspath::comparePaths(targetFilePath, pathOrPattern,
			                         compareOpts) == 0 ||
			    (!outputFile.empty() &&
			     tspath::comparePaths(outputFile, pathOrPattern,
			                          compareOpts) == 0) ||
			    (!declarationFile.empty() &&
			     tspath::comparePaths(declarationFile, pathOrPattern,
			                          compareOpts) == 0)) {
				return packageName;
			}
			break;
		case MatchingMode::Directory:
			if (canTryTsExtension &&
			    tspath::containsPath(targetFilePath, pathOrPattern,
			                         compareOpts)) {
				auto fragment = tspath::getRelativePathFromDirectory(
				    pathOrPattern, targetFilePath, compareOpts);
				return tspath::getNormalizedAbsolutePath(
				    tspath::combinePaths(
				        tspath::combinePaths(packageName, {strValue}),
				        {fragment}),
				    "");
			}
			if (!extensionSwappedTarget.empty() &&
			    tspath::containsPath(pathOrPattern, extensionSwappedTarget,
			                         compareOpts)) {
				auto fragment = tspath::getRelativePathFromDirectory(
				    pathOrPattern, extensionSwappedTarget, compareOpts);
				return tspath::getNormalizedAbsolutePath(
				    tspath::combinePaths(
				        tspath::combinePaths(packageName, {strValue}),
				        {fragment}),
				    "");
			}
			if (!canTryTsExtension &&
			    tspath::containsPath(pathOrPattern, targetFilePath,
			                         compareOpts)) {
				auto fragment = tspath::getRelativePathFromDirectory(
				    pathOrPattern, targetFilePath, compareOpts);
				return tspath::getNormalizedAbsolutePath(
				    tspath::combinePaths(
				        tspath::combinePaths(packageName, {strValue}),
				        {fragment}),
				    "");
			}
			if (!outputFile.empty() &&
			    tspath::containsPath(pathOrPattern, outputFile,
			                         compareOpts)) {
				auto fragment = tspath::getRelativePathFromDirectory(
				    pathOrPattern, outputFile, compareOpts);
				return tspath::combinePaths(packageName, {fragment});
			}
			if (!declarationFile.empty() &&
			    tspath::containsPath(pathOrPattern, declarationFile,
			                         compareOpts)) {
				auto fragment = tspath::getRelativePathFromDirectory(
				    pathOrPattern, declarationFile, compareOpts);
				auto jsExtension =
				    getJSExtensionForFile(declarationFile, options);
				auto fragmentWithJsExtension =
				    tspath::changeExtension(fragment, jsExtension);
				return tspath::combinePaths(packageName,
				                            {fragmentWithJsExtension});
			}
			break;
		case MatchingMode::Pattern: {
			bool _found;
			auto [leadingSlice, trailingSlice] =
			    cut(pathOrPattern, "*", _found);
			bool caseSensitive = host->UseCaseSensitiveFileNames();
			if (canTryTsExtension &&
			    stringutil::HasPrefixAndSuffixWithoutOverlap(
			        targetFilePath, leadingSlice, trailingSlice,
			        caseSensitive)) {
				auto starReplacement = targetFilePath.substr(
				    leadingSlice.size(),
				    targetFilePath.size() - leadingSlice.size() -
				        trailingSlice.size());
				return replaceFirstStar(packageName, starReplacement);
			}
			if (!extensionSwappedTarget.empty() &&
			    stringutil::HasPrefixAndSuffixWithoutOverlap(
			        extensionSwappedTarget, leadingSlice, trailingSlice,
			        caseSensitive)) {
				auto starReplacement = extensionSwappedTarget.substr(
				    leadingSlice.size(),
				    extensionSwappedTarget.size() - leadingSlice.size() -
				        trailingSlice.size());
				return replaceFirstStar(packageName, starReplacement);
			}
			if (!canTryTsExtension &&
			    stringutil::HasPrefixAndSuffixWithoutOverlap(
			        targetFilePath, leadingSlice, trailingSlice,
			        caseSensitive)) {
				auto starReplacement = targetFilePath.substr(
				    leadingSlice.size(),
				    targetFilePath.size() - leadingSlice.size() -
				        trailingSlice.size());
				return replaceFirstStar(packageName, starReplacement);
			}
			if (!outputFile.empty() &&
			    stringutil::HasPrefixAndSuffixWithoutOverlap(
			        outputFile, leadingSlice, trailingSlice, caseSensitive)) {
				auto starReplacement = outputFile.substr(
				    leadingSlice.size(),
				    outputFile.size() - leadingSlice.size() -
				        trailingSlice.size());
				return replaceFirstStar(packageName, starReplacement);
			}
			if (!declarationFile.empty() &&
			    stringutil::HasPrefixAndSuffixWithoutOverlap(
			        declarationFile, leadingSlice, trailingSlice,
			        caseSensitive)) {
				auto starReplacement = declarationFile.substr(
				    leadingSlice.size(),
				    declarationFile.size() - leadingSlice.size() -
				        trailingSlice.size());
				auto substituted =
				    replaceFirstStar(packageName, starReplacement);
				auto jsExtension = module::TryGetJSExtensionForFile(
				    declarationFile, *options);
				if (!jsExtension.empty()) {
					return tspath::changeFullExtension(substituted,
					                                   jsExtension);
				}
			}
			break;
		}
		}
		return "";
	}
	case packagejson::JSONValueType::Array: {
		auto* arr = exports.AsArray();
		for (auto& e : *arr) {
			auto result = tryGetModuleNameFromExportsOrImports(
			    options, host, targetFilePath, packageDirectory, packageName,
			    e, conditions, mode, isImports, preferTsExtension);
			if (!result.empty()) {
				return result;
			}
		}
		break;
	}
	case packagejson::JSONValueType::Object: {
		// conditional mapping
		auto* obj = exports.AsObject();
		for (auto& key : obj->Keys()) {
			auto* value = obj->Get(key).first;
			if (key == "default" ||
			    std::find(conditions.begin(), conditions.end(), key) !=
			        conditions.end() ||
			    (std::find(conditions.begin(), conditions.end(), "types") !=
			         conditions.end() &&
			     module::IsApplicableVersionedTypesKey(key))) {
				auto result = tryGetModuleNameFromExportsOrImports(
				    options, host, targetFilePath, packageDirectory,
				    packageName, *value, conditions, mode, isImports,
				    preferTsExtension);
				if (!result.empty()) {
					return result;
				}
			}
		}
		break;
	}
	case packagejson::JSONValueType::Null:
		return "";
	default:
		break;
	}
	return "";
}

// specifiers.go:1333 — GetModuleSpecifier. `importingSourceFile` and
// `importingSourceFileName`? Why not just use `importingSourceFile.path`?
// Because when this is called by the declaration emitter,
// `importingSourceFile` is the implementation file, but
// `importingSourceFileName` and `toFileName` refer to declaration files (the
// former to the one currently being produced; the latter to the one being
// imported). We need an implementation file just to get its
// `impliedNodeFormat` and to detect certain preferences from existing import
// module specifiers.
std::string GetModuleSpecifier(const CompilerOptions* compilerOptions,
                               checker::Program* host,
                               SourceFile* importingSourceFile,
                               const std::string& importingSourceFileName,
                               const std::string& oldImportSpecifier,
                               const std::string& toFileName,
                               const ModuleSpecifierOptions& options) {
	return getModuleSpecifierWithPreferences(
	    compilerOptions, host, importingSourceFile, importingSourceFileName,
	    oldImportSpecifier, toFileName, UserPreferences{}, options);
}

// specifiers.go:1354 — UpdateModuleSpecifier
std::string UpdateModuleSpecifier(const CompilerOptions* compilerOptions,
                                  checker::Program* host,
                                  SourceFile* importingSourceFile,
                                  const std::string& importingSourceFileName,
                                  const std::string& oldImportSpecifier,
                                  const std::string& toFileName,
                                  const UserPreferences& userPreferences,
                                  const ModuleSpecifierOptions& options) {
	return getModuleSpecifierWithPreferences(
	    compilerOptions, host, importingSourceFile, importingSourceFileName,
	    oldImportSpecifier, toFileName, userPreferences, options);
}

// specifiers.go:1376 — getModuleSpecifierWithPreferences
std::string getModuleSpecifierWithPreferences(
    const CompilerOptions* compilerOptions, checker::Program* host,
    SourceFile* importingSourceFile,
    const std::string& importingSourceFileName,
    const std::string& oldImportSpecifier, const std::string& toFileName,
    const UserPreferences& userPreferences,
    const ModuleSpecifierOptions& options) {
	auto info = getInfo(importingSourceFileName, host);
	auto modulePaths = getAllModulePaths(info, toFileName, host,
	                                     compilerOptions, userPreferences,
	                                     options);
	auto preferences =
	    getModuleSpecifierPreferences(userPreferences, host, compilerOptions,
	                                  importingSourceFile, oldImportSpecifier);

	auto resolutionMode = options.OverrideImportMode;
	if (resolutionMode == ResolutionModeNone) {
		resolutionMode =
		    host->GetDefaultResolutionModeForFile(importingSourceFile);
	}

	for (auto& modulePath : modulePaths) {
		if (auto firstDefined = tryGetModuleNameAsNodeModule(
		        modulePath, info, importingSourceFile, host, compilerOptions,
		        userPreferences, false /*packageNameOnly*/,
		        options.OverrideImportMode);
		    !firstDefined.empty()) {
			return firstDefined;
		}
	}

	return getLocalModuleSpecifier(toFileName, info, compilerOptions, host,
	                               resolutionMode, preferences, false);
}

} // namespace tsc::modulespecifiers

// ---------------------------------------------------------------------------
// dep stubs — removed when the owner slice lands
// ---------------------------------------------------------------------------

namespace tsc::outputpaths {

// outputpaths.go:101 — GetOutputJSFileNameWorker.
std::string GetOutputJSFileNameWorker(const std::string& /*inputFileName*/,
                                      const CompilerOptions* /*options*/,
                                      checker::Program* /*host*/) {
	TSC_UNREACHABLE("GetOutputJSFileNameWorker — modulespecifiers dep");
}

// outputpaths.go:108 — GetOutputDeclarationFileNameWorker.
std::string GetOutputDeclarationFileNameWorker(
    const std::string& /*inputFileName*/, const CompilerOptions* /*options*/,
    checker::Program* /*host*/) {
	TSC_UNREACHABLE("GetOutputDeclarationFileNameWorker — modulespecifiers dep");
}

} // namespace tsc::outputpaths
