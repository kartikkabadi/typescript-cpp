#pragma once

// Port of tsc/internal/module/types.go + minimal packagejson seam —
// ported with the program slice.
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/core/types.h"
#include "internal/tspath/tspath.h"

namespace tsc::module {

// --- packagejson shim (program slice): only the fields the check path reads ---
struct PackageJsonContents {
	std::string type;
	std::string name;
	std::string version;
	std::string types;
	std::string typings;
	std::string main;
};

// InfoCacheEntry — package.json cache entry. `exists == false` mirrors Go's
// composite entry meaning "no package.json was found at this path".
struct InfoCacheEntry {
	std::string packageDirectory;
	bool exists{};
	PackageJsonContents contents;

	bool Exists() const { return exists; }
	const PackageJsonContents* GetContents() const {
		return exists ? &contents : nullptr;
	}
};

// types.go: ResolutionHost
struct ResolutionHost {
	virtual ~ResolutionHost() = default;
	virtual bool fileExists(std::string_view fileName) = 0;
	virtual bool directoryExists(std::string_view directory) = 0;
	virtual bool readFile(std::string_view fileName, std::string& out) = 0;
	virtual std::vector<std::string> getAccessibleDirectories(
	    std::string_view directory) = 0;
	virtual std::string GetCurrentDirectory() = 0;
	virtual bool useCaseSensitiveFileNames() = 0;
};

// types.go: ModeAwareCacheKey
struct ModeAwareCacheKey {
	std::string name;
	ResolutionMode mode{};

	bool operator==(const ModeAwareCacheKey& other) const {
		return name == other.name && mode == other.mode;
	}
};

struct ModeAwareCacheKeyHash {
	size_t operator()(const ModeAwareCacheKey& k) const {
		return std::hash<std::string>()(k.name) * 31 +
		       std::hash<int>()(static_cast<int>(k.mode));
	}
};

template <class T>
using ModeAwareCache =
    std::unordered_map<ModeAwareCacheKey, T, ModeAwareCacheKeyHash>;

// types.go: ResolvedProjectReference — absent in the program slice; kept as a
// nullable opaque interface.
struct ResolvedProjectReference {
	virtual ~ResolvedProjectReference() = default;
	virtual std::string ConfigName() const = 0;
	virtual const CompilerOptions* CompilerOptions() const = 0;
};

// types.go: NodeResolutionFeatures
using NodeResolutionFeatures = int32_t;
inline constexpr NodeResolutionFeatures NodeResolutionFeaturesImports = 1 << 0;
inline constexpr NodeResolutionFeatures NodeResolutionFeaturesSelfName = 1 << 1;
inline constexpr NodeResolutionFeatures NodeResolutionFeaturesExports = 1 << 2;
inline constexpr NodeResolutionFeatures NodeResolutionFeaturesExportsPatternTrailers = 1 << 3;
inline constexpr NodeResolutionFeatures NodeResolutionFeaturesImportsPatternRoot = 1 << 4;

inline constexpr NodeResolutionFeatures NodeResolutionFeaturesNone = 0;
inline constexpr NodeResolutionFeatures NodeResolutionFeaturesAll =
    NodeResolutionFeaturesImports | NodeResolutionFeaturesSelfName |
    NodeResolutionFeaturesExports | NodeResolutionFeaturesExportsPatternTrailers |
    NodeResolutionFeaturesImportsPatternRoot;
inline constexpr NodeResolutionFeatures NodeResolutionFeaturesNode16Default =
    NodeResolutionFeaturesImports | NodeResolutionFeaturesSelfName |
    NodeResolutionFeaturesExports | NodeResolutionFeaturesExportsPatternTrailers;
inline constexpr NodeResolutionFeatures NodeResolutionFeaturesNodeNextDefault =
    NodeResolutionFeaturesAll;
inline constexpr NodeResolutionFeatures NodeResolutionFeaturesBundlerDefault =
    NodeResolutionFeaturesImports | NodeResolutionFeaturesSelfName |
    NodeResolutionFeaturesExports | NodeResolutionFeaturesExportsPatternTrailers |
    NodeResolutionFeaturesImportsPatternRoot;

// types.go: PackageId
struct PackageId {
	std::string name;
	std::string subModuleName;
	std::string version;
	std::string peerDependencies;

	std::string PackageName() const {
		if (!subModuleName.empty()) {
			return name + "/" + subModuleName;
		}
		return name;
	}

	std::string String() const {
		return PackageName() + "@" + version + peerDependencies;
	}
};

inline bool operator==(const PackageId& a, const PackageId& b) {
	return a.name == b.name && a.subModuleName == b.subModuleName &&
	       a.version == b.version &&
	       a.peerDependencies == b.peerDependencies;
}

// Hash for map[PackageId]... keys (deduplicatePackages bookkeeping).
struct PackageIdHash {
	size_t operator()(const PackageId& p) const {
		std::hash<std::string> h;
		size_t seed = h(p.name);
		seed ^= h(p.subModuleName) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
		seed ^= h(p.version) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
		seed ^=
		    h(p.peerDependencies) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
		return seed;
	}
};

// types.go: ResolvedModule
struct ResolvedModule {
	std::vector<Diagnostic*> resolutionDiagnostics;
	std::string resolvedFileName;
	std::string originalPath;
	std::string extension;
	bool resolvedUsingTsExtension{};
	bool resolvedUsingExtraExtensions{};
	PackageId packageId;
	bool isExternalLibraryImport{};
	std::string alternateResult;

	bool IsResolved() const { return !resolvedFileName.empty(); }
};

// types.go: ResolvedTypeReferenceDirective
struct ResolvedTypeReferenceDirective {
	std::vector<Diagnostic*> resolutionDiagnostics;
	bool primary{};
	std::string resolvedFileName;
	std::string originalPath;
	PackageId packageId;
	bool isExternalLibraryImport{};

	bool IsResolved() const { return !resolvedFileName.empty(); }
};

// types.go: extensions flags
using extensions = int32_t;
inline constexpr extensions extensionsTypeScript = 1 << 0;
inline constexpr extensions extensionsJavaScript = 1 << 1;
inline constexpr extensions extensionsDeclaration = 1 << 2;
inline constexpr extensions extensionsJson = 1 << 3;
inline constexpr extensions extensionsImplementationFiles =
    extensionsTypeScript | extensionsJavaScript;

inline std::vector<std::string_view> extensionsArray(extensions e) {
	std::vector<std::string_view> result;
	if (e & extensionsTypeScript) {
		for (auto ext : tspath::supportedTSImplementationExtensions)
			result.push_back(ext);
	}
	if (e & extensionsJavaScript) {
		for (auto ext : tspath::supportedJSExtensionsFlat)
			result.push_back(ext);
	}
	if (e & extensionsDeclaration) {
		for (auto ext : tspath::supportedDeclarationExtensions)
			result.push_back(ext);
	}
	if (e & extensionsJson) {
		result.push_back(tspath::extensionJson);
	}
	return result;
}

// resolver.go: DiagAndArgs
struct DiagAndArgs {
	const DiagnosticMessage* message;
	std::vector<std::string> args;
};

// resolver.go: Resolver interface (program slice subset)
struct Resolver {
	virtual ~Resolver() = default;
	virtual ResolvedModule* ResolveModuleName(
	    const std::string& moduleName, const std::string& containingFile,
	    ResolutionMode resolutionMode,
	    ResolvedProjectReference* redirectedReference) = 0;
	virtual ResolvedModule* ResolveModuleNameFromDirectory(
	    const std::string& moduleName, const std::string& containingDirectory,
	    ResolutionMode resolutionMode) = 0;
	virtual ResolvedTypeReferenceDirective* ResolveTypeReferenceDirective(
	    const std::string& typeReferenceDirectiveName,
	    const std::string& containingFile, ResolutionMode resolutionMode,
	    ResolvedProjectReference* redirectedReference) = 0;
	virtual InfoCacheEntry* GetPackageScopeForPath(
	    const std::string& directory) = 0;
	virtual ResolvedModule* ResolvePackageDirectory(
	    const std::string& moduleName, const std::string& containingFile,
	    ResolutionMode resolutionMode,
	    ResolvedProjectReference* redirectedReference) = 0;
};

// --- util.go / resolver.go module helpers ---

// module.go: InferredTypesContainingFile
inline constexpr std::string_view InferredTypesContainingFile =
    "__inferred type names__.ts";

// module.go: MangleScopedPackageName — `@foo/bar` -> `foo__bar`
inline std::string mangleScopedPackageName(std::string_view packageName) {
	if (!packageName.empty() && packageName[0] == '@') {
		auto separator = packageName.find('/');
		if (separator != std::string_view::npos && separator + 1 < packageName.size()) {
			return std::string(packageName.substr(1, separator - 1)) + "__" +
			       std::string(packageName.substr(separator + 1));
		}
	}
	return std::string(packageName);
}

// module.go: GetTypesPackageName
inline std::string getTypesPackageName(std::string_view packageName) {
	return "@types/" + tsc::module::mangleScopedPackageName(packageName);
}

// module.go: ParsePackageName — returns {packageName, rest}
inline std::pair<std::string, std::string> parsePackageName(std::string_view packageName) {
	// A scoped package name starts with '@', e.g. "@scope/name/sub/path".
	if (!packageName.empty() && packageName[0] == '@') {
		auto separator = packageName.find('/');
		if (separator != std::string_view::npos &&
		    separator + 1 < packageName.size()) {
			auto rest = packageName.find('/', separator + 1);
			if (rest != std::string_view::npos) {
				return {std::string(packageName.substr(0, rest)),
				        std::string(packageName.substr(rest + 1))};
			}
			return {std::string(packageName), ""};
		}
		return {std::string(packageName), ""};
	}
	auto separator = packageName.find('/');
	if (separator != std::string_view::npos) {
		return {std::string(packageName.substr(0, separator)),
		        std::string(packageName.substr(separator + 1))};
	}
	return {std::string(packageName), ""};
}

// util.go: ParseNodeModuleFromPath — returns package id info or nullopt-ish
struct NodeModuleFromPath {
	std::string topLevelPackageName;
	std::string topLevelNodeModulesRoot;
	std::string packageRootName;
	std::string fileName;
};

// Simplified port of util.go ParseNodeModuleFromPath (non-normalized-only
// branches kept; symlink variants omitted — no node_modules in this slice).
inline bool parseNodeModuleFromPath(std::string_view resolved, bool isFolder,
                                    NodeModuleFromPath& out) {
	// Walk up from the resolved path looking for "/node_modules/".
	constexpr std::string_view marker = "/node_modules/";
	auto pos = resolved.rfind(marker);
	if (pos == std::string_view::npos) {
		// Also handle path starting with node_modules (no leading slash).
		if (resolved.substr(0, marker.size() - 1) ==
		    marker.substr(1)) {
			pos = 0;
		} else {
			return false;
		}
	}
	auto nodeModulesStart = pos + marker.size();
	auto packageEnd = resolved.find('/', nodeModulesStart);
	std::string_view packageName;
	if (packageEnd == std::string_view::npos) {
		if (!isFolder) return false;
		packageName = resolved.substr(nodeModulesStart);
	} else {
		packageName = resolved.substr(nodeModulesStart, packageEnd - nodeModulesStart);
	}
	if (!packageName.empty() && packageName[0] == '@' &&
	    packageEnd != std::string_view::npos) {
		// Scoped package: take one more segment.
		auto subEnd = resolved.find('/', packageEnd + 1);
		if (subEnd == std::string_view::npos) {
			if (!isFolder) return false;
			packageName = resolved.substr(nodeModulesStart);
		} else {
			packageName =
			    resolved.substr(nodeModulesStart, subEnd - nodeModulesStart);
		}
	}
	out.topLevelPackageName = std::string(packageName);
	out.topLevelNodeModulesRoot =
	    std::string(resolved.substr(0, pos + marker.size()));
	out.packageRootName =
	    out.topLevelNodeModulesRoot + out.topLevelPackageName;
	out.fileName = std::string(resolved);
	return true;
}

}  // namespace tsc::module
