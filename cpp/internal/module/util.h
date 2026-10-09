// module/util.go — port of tsc/internal/module/util.go.
#pragma once

#include <string>
#include <string_view>

#include "internal/core/types.h"
#include "internal/core/version.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/module/types.h"
#include "internal/semver/semver.h"

namespace tsc {
struct SourceFile;
}

namespace tsc::module {

inline const semver::Version& typeScriptVersion() {
	static const semver::Version v = semver::MustParse(tsc::version());
	return v;
}

inline constexpr std::string_view InferredTypesContainingFile =
    "__inferred type names__.ts";

// util.go — IsApplicableVersionedTypesKey: "types@<range>" matching the
// compiler's own version.
bool IsApplicableVersionedTypesKey(std::string_view key);

// NodeModulePackageRootForFile/NodeModulePackageRootForDirectory —
// node_modules package dir prefix, or "" (util.go).
std::string NodeModulePackageRootForFile(std::string_view resolved);
std::string NodeModulePackageRootForDirectory(std::string_view resolved);
std::string parseNodeModulePackageRoot(std::string_view resolved,
                                       bool isDirectory);

// ParsePackageName — splits "@scope/name/rest" or "name/rest".
std::pair<std::string_view, std::string_view> ParsePackageName(
    std::string_view moduleName);

// MangleScopedPackageName — "@scope/name" -> "scope__name".
std::string MangleScopedPackageName(std::string_view packageName);

// UnmangleScopedPackageName — "scope__name" -> "@scope/name".
std::string UnmangleScopedPackageName(std::string_view packageName);

// GetTypesPackageName — "name" -> "@types/mangled-name".
std::string GetTypesPackageName(std::string_view packageName);

// GetPackageNameFromTypesPackageName — inverse of GetTypesPackageName.
std::string GetPackageNameFromTypesPackageName(
    std::string_view mangledName);

// ComparePatternKeys — sort order for export/import pattern keys.
int ComparePatternKeys(std::string_view a, std::string_view b);

// GetResolutionDiagnostic — the diagnostic reported when a resolved file's
// extension isn't acceptable under current options, or nullptr.
const DiagnosticMessage* GetResolutionDiagnostic(
    const CompilerOptions& options, const ResolvedModule* resolvedModule,
    const SourceFile* file);

// TryGetJSExtensionForFile — TS/JS/DTS extension to output JS extension, or
// "".
std::string_view TryGetJSExtensionForFile(std::string_view fileName,
                                          const CompilerOptions& options);

}  // namespace tsc::module
