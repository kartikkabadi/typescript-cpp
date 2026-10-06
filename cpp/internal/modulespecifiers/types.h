// Port of tsc/internal/modulespecifiers/types.go (value types) +
// compare.go (CountPathComponents) + the GetModuleSpecifiers signature from
// specifiers.go. Interface types (SourceFileForSpecifierGeneration,
// CheckerShape, ModuleSpecifierGenerationHost) are mapped to the concrete C++
// types they are always instantiated with (SourceFile / Checker / Program).
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "internal/core/types.h"
#include "internal/tspath/tspath.h"

namespace tsc {
struct SourceFile;
struct Symbol;
namespace checker {
class Program;
class Checker;
} // namespace checker
namespace module {
struct ResolvedEntrypoint;
} // namespace module
} // namespace tsc

namespace tsc::modulespecifiers {

// types.go:25 — ResultKind
enum class ResultKind : uint8_t {
	None = 0,
	NodeModules,
	Paths,
	Redirect,
	Relative,
	Ambient,
};

// types.go:36 — ModuleSpecifiersResult
struct ModuleSpecifiersResult {
	std::vector<std::string> Specifiers;
	ResultKind Kind = ResultKind::None;
	Symbol* AmbientModuleSymbol =
		nullptr; // used to construct an import attributes node, if one is
	             // needed
};

// types.go:42 — ModulePath
struct ModulePath {
	std::string FileName;
	bool IsInNodeModules = false;
	bool IsRedirect = false;
};

// types.go:71 — ImportModuleSpecifierPreference
using ImportModuleSpecifierPreference = std::string;
inline const ImportModuleSpecifierPreference
	ImportModuleSpecifierPreferenceNone{""}; // !!!
inline const ImportModuleSpecifierPreference
	ImportModuleSpecifierPreferenceShortest{"shortest"};
inline const ImportModuleSpecifierPreference
	ImportModuleSpecifierPreferenceProjectRelative{"project-relative"};
inline const ImportModuleSpecifierPreference
	ImportModuleSpecifierPreferenceRelative{"relative"};
inline const ImportModuleSpecifierPreference
	ImportModuleSpecifierPreferenceNonRelative{"non-relative"};

// types.go:81 — ImportModuleSpecifierEndingPreference
using ImportModuleSpecifierEndingPreference = std::string;
inline const ImportModuleSpecifierEndingPreference
	ImportModuleSpecifierEndingPreferenceNone{""}; // !!!
inline const ImportModuleSpecifierEndingPreference
	ImportModuleSpecifierEndingPreferenceAuto{"auto"};
inline const ImportModuleSpecifierEndingPreference
	ImportModuleSpecifierEndingPreferenceMinimal{"minimal"};
inline const ImportModuleSpecifierEndingPreference
	ImportModuleSpecifierEndingPreferenceIndex{"index"};
inline const ImportModuleSpecifierEndingPreference
	ImportModuleSpecifierEndingPreferenceJs{"js"};

// types.go:91 — UserPreferences
struct UserPreferences {
	ImportModuleSpecifierPreference ImportModuleSpecifierPreference;
	ImportModuleSpecifierEndingPreference ImportModuleSpecifierEnding;
	std::vector<std::string> AutoImportSpecifierExcludeRegexes;
};

// types.go:97 — ModuleSpecifierOptions
struct ModuleSpecifierOptions {
	ResolutionMode OverrideImportMode = ResolutionModeNone;
};

// types.go:101 — RelativePreferenceKind
enum class RelativePreferenceKind : uint8_t {
	Relative = 0,
	NonRelative,
	Shortest,
	ExternalNonRelative,
};

// types.go:110 — ModuleSpecifierEnding
enum class ModuleSpecifierEnding : uint8_t {
	Minimal = 0,
	Index,
	JsExtension,
	TsExtension,
};

// types.go:119 — MatchingMode
enum class MatchingMode : uint8_t {
	Exact = 0,
	Directory,
	Pattern,
};

// compare.go:7 — CountPathComponents
inline int CountPathComponents(const std::string& path) {
	size_t initial = 0;
	if (path.size() >= 2 && path.compare(0, 2, "./") == 0) {
		initial = 2;
	}
	int count = 0;
	for (size_t i = initial; i < path.size(); i++) {
		if (path[i] == '/') {
			count++;
		}
	}
	return count;
}

// util.go:263 — NodeModulePathParts
struct NodeModulePathParts {
	int TopLevelNodeModulesIndex = 0;
	int TopLevelPackageNameIndex = 0;
	int PackageRootIndex = 0;
	int FileNameIndex = 0;
};

// specifiers.go:19 — GetModuleSpecifiers
ModuleSpecifiersResult GetModuleSpecifiers(
	Symbol* moduleSymbol, checker::Checker* checker,
	const CompilerOptions* compilerOptions, SourceFile* importingSourceFile,
	checker::Program* host, const UserPreferences& userPreferences,
	const ModuleSpecifierOptions& options, bool forAutoImports);

// specifiers.go:41 — GetModuleSpecifiersWithInfo
ModuleSpecifiersResult GetModuleSpecifiersWithInfo(
	Symbol* moduleSymbol, checker::Checker* checker,
	const CompilerOptions* compilerOptions, SourceFile* importingSourceFile,
	checker::Program* host, const UserPreferences& userPreferences,
	const ModuleSpecifierOptions& options, bool forAutoImports);

// specifiers.go:79 — GetModuleSpecifiersForFileWithInfo
std::pair<std::vector<std::string>, ResultKind>
GetModuleSpecifiersForFileWithInfo(
	SourceFile* importingSourceFile, const std::string& moduleFileName,
	const CompilerOptions* compilerOptions, checker::Program* host,
	const UserPreferences& userPreferences,
	const ModuleSpecifierOptions& options, bool forAutoImports);

// specifiers.go:259 — ContainsNodeModules
bool ContainsNodeModules(const std::string& s);

// specifiers.go:265 — GetEachFileNameOfModule
std::vector<ModulePath> GetEachFileNameOfModule(
	const std::string& importingFileName, const std::string& importedFileName,
	checker::Program* host, bool preferSymlinks);

// specifiers.go:1333 — GetModuleSpecifier
std::string GetModuleSpecifier(const CompilerOptions* compilerOptions,
                               checker::Program* host,
                               SourceFile* importingSourceFile,
                               const std::string& importingSourceFileName,
                               const std::string& oldImportSpecifier,
                               const std::string& toFileName,
                               const ModuleSpecifierOptions& options);

// specifiers.go:1354 — UpdateModuleSpecifier
std::string UpdateModuleSpecifier(const CompilerOptions* compilerOptions,
                                  checker::Program* host,
                                  SourceFile* importingSourceFile,
                                  const std::string& importingSourceFileName,
                                  const std::string& oldImportSpecifier,
                                  const std::string& toFileName,
                                  const UserPreferences& userPreferences,
                                  const ModuleSpecifierOptions& options);

// util.go:42 — PathIsBareSpecifier
bool PathIsBareSpecifier(std::string_view path);

// util.go:46 — IsExcludedByRegex
bool IsExcludedByRegex(const std::string& moduleSpecifier,
                       const std::vector<std::string>& excludes);

// util.go:143 — GetJSExtensionForDeclarationFileExtension
std::string GetJSExtensionForDeclarationFileExtension(
    std::string_view ext);

// util.go:159 — TryGetRealFileNameForNonJSDeclarationFileName
std::string TryGetRealFileNameForNonJSDeclarationFileName(
    const std::string& fileName);

// util.go:279 — GetNodeModulePathParts (Go returns *NodeModulePathParts;
// nullopt on no match)
std::optional<NodeModulePathParts> GetNodeModulePathParts(
    const std::string& fullPath);

// util.go:332 — GetNodeModulesPackageName
std::string GetNodeModulesPackageName(
    const CompilerOptions* compilerOptions, SourceFile* importingSourceFile,
    const std::string& nodeModulesFileName, checker::Program* host,
    const UserPreferences& preferences, const ModuleSpecifierOptions& options);

// util.go:359 — GetPackageNameFromDirectory
std::string GetPackageNameFromDirectory(std::string_view fileOrDirectoryPath);

// util.go:388 — ProcessEntrypointEnding
std::string ProcessEntrypointEnding(
    const module::ResolvedEntrypoint* entrypoint,
    const UserPreferences& prefs, checker::Program* host,
    const CompilerOptions* options, SourceFile* importingSourceFile,
    const std::vector<ModuleSpecifierEnding>& allowedEndings);

// preferences.go:147 — GetAllowedEndingsInPreferredOrder
std::vector<ModuleSpecifierEnding> GetAllowedEndingsInPreferredOrder(
    const UserPreferences& prefs, checker::Program* host,
    const CompilerOptions* compilerOptions, SourceFile* importingSourceFile,
    const std::string& oldImportSpecifier,
    ResolutionMode syntaxImpliedNodeFormat);

// --- package-internal (Go unexported) decls shared across the per-file TUs --

// specifiers.go:162 — Info
struct Info {
	bool UseCaseSensitiveFileNames = false;
	std::string ImportingSourceFileName;
	std::string SourceDirectory;
};

// specifiers.go:168 — getInfo
Info getInfo(const std::string& importingSourceFileName,
             checker::Program* host);

// specifiers.go:180 — getAllModulePaths
std::vector<ModulePath> getAllModulePaths(
    const Info& info, const std::string& importedFileName,
    checker::Program* host, const CompilerOptions* compilerOptions,
    const UserPreferences& preferences, const ModuleSpecifierOptions& options);

// specifiers.go:748 — tryGetModuleNameAsNodeModule
std::string tryGetModuleNameAsNodeModule(
    const ModulePath& pathObj, const Info& info,
    SourceFile* importingSourceFile, checker::Program* host,
    const CompilerOptions* options, const UserPreferences& userPreferences,
    bool packageNameOnly, ResolutionMode overrideMode);

// preferences.go:141 — ModuleSpecifierPreferences
struct ModuleSpecifierPreferences {
	RelativePreferenceKind relativePreference =
	    RelativePreferenceKind::Shortest;
	std::function<std::vector<ModuleSpecifierEnding>(ResolutionMode)>
	    getAllowedEndingsInPreferredOrder;
	std::vector<std::string> excludeRegexes;
};

// preferences.go:213 — getModuleSpecifierPreferences
ModuleSpecifierPreferences getModuleSpecifierPreferences(
    const UserPreferences& prefs, checker::Program* host,
    const CompilerOptions* compilerOptions, SourceFile* importingSourceFile,
    const std::string& oldImportSpecifier);

// util.go:29 — comparePathsByRedirect
int comparePathsByRedirect(const ModulePath& a, const ModulePath& b,
                           bool useCaseSensitiveFileNames);

// util.go:136 — ensurePathIsNonModuleName
std::string ensurePathIsNonModuleName(std::string_view path);

// util.go:174 — getJSExtensionForFile
std::string getJSExtensionForFile(std::string_view fileName,
                                  const CompilerOptions* options);

// util.go:194 — tryGetAnyFileFromPath
bool tryGetAnyFileFromPath(checker::Program* host, const std::string& path);

// util.go:214 — getPathsRelativeToRootDirs
std::vector<std::string> getPathsRelativeToRootDirs(
    const std::string& path, const std::vector<std::string>& rootDirs,
    bool useCaseSensitiveFileNames);

// util.go:225 — isPathRelativeToParent
bool isPathRelativeToParent(std::string_view path);

// util.go:229 — getRelativePathIfInSameVolume
std::string getRelativePathIfInSameVolume(const std::string& path,
                                          const std::string& directoryPath,
                                          bool useCaseSensitiveFileNames);

// util.go:240 — packageJsonPathsAreEqual
bool packageJsonPathsAreEqual(const std::string& a, const std::string& b,
                              const tspath::ComparePathsOptions& options);

// util.go:250 — prefersTsExtension
bool prefersTsExtension(const std::vector<ModuleSpecifierEnding>& allowedEndings);

// util.go:259 — replaceFirstStar
std::string replaceFirstStar(std::string_view s, std::string_view replacement);

} // namespace tsc::modulespecifiers
