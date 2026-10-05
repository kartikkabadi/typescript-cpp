// Port of tsc/internal/modulespecifiers/types.go (value types) +
// compare.go (CountPathComponents) + the GetModuleSpecifiers signature from
// specifiers.go. Interface types (SourceFileForSpecifierGeneration,
// CheckerShape, ModuleSpecifierGenerationHost) are mapped to the concrete C++
// types they are always instantiated with (SourceFile / Checker / Program).
#pragma once

#include <string>
#include <vector>

#include "internal/core/types.h"
#include "internal/tspath/tspath.h"

namespace tsc {
class SourceFile;
struct Symbol;
namespace checker {
class Program;
struct Checker;
} // namespace checker
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

// specifiers.go:19 — GetModuleSpecifiers
ModuleSpecifiersResult GetModuleSpecifiers(
	Symbol* moduleSymbol, checker::Checker* checker,
	const CompilerOptions* compilerOptions, SourceFile* importingSourceFile,
	checker::Program* host, const UserPreferences& userPreferences,
	const ModuleSpecifierOptions& options, bool forAutoImports);

} // namespace tsc::modulespecifiers
