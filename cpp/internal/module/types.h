// module/types.go — port of tsc/internal/module/types.go.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/core/types.h"
#include "internal/tspath/tspath.h"

namespace tsc::packagejson {
struct InfoCacheEntry;
struct InfoCache;
}  // namespace tsc::packagejson

namespace tsc::module {

// ResolutionHost — replaces Go's ResolutionHost{FS() vfs.FS,
// GetCurrentDirectory()}. Implemented over the real filesystem
// (std::filesystem) by SystemResolutionHost in host.h.
struct ResolutionHost {
	virtual ~ResolutionHost() = default;

	virtual bool FileExists(std::string_view path) = 0;
	virtual bool DirectoryExists(std::string_view path) = 0;
	virtual std::optional<std::string> ReadFile(std::string_view path) = 0;
	virtual std::string Realpath(std::string_view path) = 0;
	virtual std::string GetCurrentDirectory() = 0;
	virtual bool UseCaseSensitiveFileNames() = 0;

	// GetAccessibleEntries — vfs.FS.GetAccessibleEntries: directory listing
	// used by glob matching and automatic type directive discovery.
	// symlinks = names of entries that were symlinks on disk; nullopt means
	// symlink information is not available (vfs.Entries.Symlinks nil).
	struct AccessibleEntries {
		std::vector<std::string> files;
		std::vector<std::string> directories;
		std::optional<std::unordered_set<std::string>> symlinks;
	};
	virtual AccessibleEntries GetAccessibleEntries(
	    std::string_view path) = 0;
};

struct ResolvedProjectReference {
	virtual ~ResolvedProjectReference() = default;
	virtual std::string ConfigName() const = 0;
	virtual const tsc::CompilerOptions* CompilerOptions() const = 0;
};

// ModeAwareCacheKey — cache.go.
struct ModeAwareCacheKey {
	std::string Name;
	ResolutionMode Mode = ResolutionModeNone;

	bool operator==(const ModeAwareCacheKey&) const = default;
};

struct ModeAwareCacheKeyHash {
	size_t operator()(const ModeAwareCacheKey& k) const {
		return std::hash<std::string>{}(k.Name) ^
		       (std::hash<int>{}(static_cast<int>(k.Mode)) << 1);
	}
};

// NodeResolutionFeatures — bitflags.
using NodeResolutionFeatures = int32_t;
inline constexpr NodeResolutionFeatures NodeResolutionFeaturesImports = 1 << 0;
inline constexpr NodeResolutionFeatures NodeResolutionFeaturesSelfName = 1 << 1;
inline constexpr NodeResolutionFeatures NodeResolutionFeaturesExports = 1 << 2;
inline constexpr NodeResolutionFeatures NodeResolutionFeaturesExportsPatternTrailers =
    1 << 3;
// allowing `#/` root imports in package.json imports field
// not supported until mass adoption - https://github.com/nodejs/node/pull/60864
inline constexpr NodeResolutionFeatures NodeResolutionFeaturesImportsPatternRoot =
    1 << 4;

inline constexpr NodeResolutionFeatures NodeResolutionFeaturesNone = 0;
inline constexpr NodeResolutionFeatures NodeResolutionFeaturesAll =
    NodeResolutionFeaturesImports | NodeResolutionFeaturesSelfName |
    NodeResolutionFeaturesExports |
    NodeResolutionFeaturesExportsPatternTrailers |
    NodeResolutionFeaturesImportsPatternRoot;
inline constexpr NodeResolutionFeatures NodeResolutionFeaturesNode16Default =
    NodeResolutionFeaturesImports | NodeResolutionFeaturesSelfName |
    NodeResolutionFeaturesExports |
    NodeResolutionFeaturesExportsPatternTrailers;
inline constexpr NodeResolutionFeatures NodeResolutionFeaturesNodeNextDefault =
    NodeResolutionFeaturesAll;
inline constexpr NodeResolutionFeatures NodeResolutionFeaturesBundlerDefault =
    NodeResolutionFeaturesAll;

// PackageId — identifies the package a resolved file belongs to.
struct PackageId {
	std::string Name;
	std::string SubModuleName;
	std::string Version;
	std::string PeerDependencies;

	std::string String() const {
		return PackageName() + "@" + Version + PeerDependencies;
	}
	std::string PackageName() const {
		if (!SubModuleName.empty()) {
			return Name + "/" + SubModuleName;
		}
		return Name;
	}
};

// ResolvedModule — full module resolution result. checker's tsc::ResolvedModule
// (checker.h) is a slim consumer-facing struct; toCheckerResolvedModule()
// converts.
struct ResolvedModule {
	std::vector<Diagnostic*> ResolutionDiagnostics;
	std::string ResolvedFileName;
	std::string OriginalPath;
	std::string Extension;
	bool ResolvedUsingTsExtension = false;
	bool ResolvedUsingExtraExtensions = false;
	PackageId PackageId;
	bool IsExternalLibraryImport = false;
	std::string AlternateResult;

	bool IsResolved() const { return !ResolvedFileName.empty(); }
};

struct ResolvedTypeReferenceDirective {
	std::vector<Diagnostic*> ResolutionDiagnostics;
	bool Primary = false;
	std::string ResolvedFileName;
	std::string OriginalPath;
	PackageId PackageId;
	bool IsExternalLibraryImport = false;

	bool IsResolved() const { return !ResolvedFileName.empty(); }
};

// extensions — bitflags of file kinds to probe.
using extensions = int32_t;
inline constexpr extensions extensionsTypeScript = 1 << 0;
inline constexpr extensions extensionsJavaScript = 1 << 1;
inline constexpr extensions extensionsDeclaration = 1 << 2;
inline constexpr extensions extensionsJson = 1 << 3;

inline constexpr extensions extensionsImplementationFiles =
    extensionsTypeScript | extensionsJavaScript;

inline std::string extensionsString(extensions e) {
	std::string result;
	auto add = [&](const char* s) {
		if (!result.empty()) result += ", ";
		result += s;
	};
	if (e & extensionsTypeScript) add("TypeScript");
	if (e & extensionsJavaScript) add("JavaScript");
	if (e & extensionsDeclaration) add("Declaration");
	if (e & extensionsJson) add("JSON");
	return result;
}

inline std::vector<std::string_view> extensionsArray(extensions e) {
	std::vector<std::string_view> result;
	if (e & extensionsTypeScript) {
		result.insert(result.end(),
		              tspath::supportedTSImplementationExtensions.begin(),
		              tspath::supportedTSImplementationExtensions.end());
	}
	if (e & extensionsJavaScript) {
		result.insert(result.end(),
		              tspath::supportedJSExtensionsFlat.begin(),
		              tspath::supportedJSExtensionsFlat.end());
	}
	if (e & extensionsDeclaration) {
		result.insert(result.end(),
		              tspath::supportedDeclarationExtensions.begin(),
		              tspath::supportedDeclarationExtensions.end());
	}
	if (e & extensionsJson) {
		result.push_back(tspath::extensionJson);
	}
	return result;
}

}  // namespace tsc::module
