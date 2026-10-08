// module/cache.go — port of tsc/internal/module/cache.go.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/collections/collections.h"
#include "internal/core/types.h"
#include "internal/module/types.h"
#include "internal/packagejson/packagejson.h"

namespace tsc::module {

struct ParsedPatterns;

template <typename T>
using ModeAwareCache =
    std::unordered_map<ModeAwareCacheKey, T, ModeAwareCacheKeyHash>;

// moduleResolutionCacheKey — cache.go.
struct moduleResolutionCacheKey {
	std::string containingDirectory;
	std::string moduleName;
	ResolutionMode resolutionMode = ResolutionModeNone;
	std::string redirectConfigName;

	bool operator==(const moduleResolutionCacheKey&) const = default;
};

struct moduleResolutionCacheKeyHash {
	size_t operator()(const moduleResolutionCacheKey& k) const {
		size_t h = std::hash<std::string>{}(k.containingDirectory);
		h = h * 31 + std::hash<std::string>{}(k.moduleName);
		h = h * 31 + std::hash<int>{}(static_cast<int>(k.resolutionMode));
		h = h * 31 + std::hash<std::string>{}(k.redirectConfigName);
		return h;
	}
};

struct moduleResolutionCache {
	collections::SyncMap<moduleResolutionCacheKey,
	                     std::shared_ptr<ResolvedModule>,
	                     moduleResolutionCacheKeyHash>
	    cache;

	std::pair<std::shared_ptr<ResolvedModule>, bool> Get(
	    const moduleResolutionCacheKey& key) {
		return cache.Load(key);
	}

	void Set(const moduleResolutionCacheKey& key,
	         std::shared_ptr<ResolvedModule> value) {
		cache.LoadOrStore(key, std::move(value));
	}
};

// typeRefDirectiveResolutionCacheKey — cache.go.
struct typeRefDirectiveResolutionCacheKey {
	std::string containingDirectory;
	std::string typeReferenceName;
	ResolutionMode resolutionMode = ResolutionModeNone;
	std::string redirectConfigName;
	bool fromInferredTypesContainingFile = false;

	bool operator==(const typeRefDirectiveResolutionCacheKey&) const =
	    default;
};

struct typeRefDirectiveResolutionCacheKeyHash {
	size_t operator()(const typeRefDirectiveResolutionCacheKey& k) const {
		size_t h = std::hash<std::string>{}(k.containingDirectory);
		h = h * 31 + std::hash<std::string>{}(k.typeReferenceName);
		h = h * 31 + std::hash<int>{}(static_cast<int>(k.resolutionMode));
		h = h * 31 + std::hash<std::string>{}(k.redirectConfigName);
		h = h * 31 + (k.fromInferredTypesContainingFile ? 1 : 0);
		return h;
	}
};

struct typeRefDirectiveResolutionCache {
	collections::SyncMap<typeRefDirectiveResolutionCacheKey,
	                     std::shared_ptr<ResolvedTypeReferenceDirective>,
	                     typeRefDirectiveResolutionCacheKeyHash>
	    cache;

	std::pair<std::shared_ptr<ResolvedTypeReferenceDirective>, bool> Get(
	    const typeRefDirectiveResolutionCacheKey& key) {
		return cache.Load(key);
	}

	void Set(const typeRefDirectiveResolutionCacheKey& key,
	         std::shared_ptr<ResolvedTypeReferenceDirective> value) {
		cache.Store(key, std::move(value));
	}
};

// parsedPatternsCache — keyed by the path-mappings object identity (Go keys
// on the *OrderedMap pointer). `parsed` is the freshly-computed value to
// store when the key isn't present (LoadOrStore semantics).
struct parsedPatternsCache {
	collections::SyncMap<const void*, std::shared_ptr<ParsedPatterns>> cache;

	std::shared_ptr<ParsedPatterns> Get(
	    const void* pathMappings, std::shared_ptr<ParsedPatterns> parsed);
};

// cache.go:63 — ResolutionData: the resolution-scoped data that outlives
// an individual DefaultResolver instance (a5c43c4d54 — Program options
// lifetimes). The module/typeref/parsedPatterns caches stay per-resolver.
struct ResolutionData : std::enable_shared_from_this<ResolutionData> {
	const CompilerOptions* compilerOptions{};
	std::string typingsLocation;
	std::string projectName;
	std::vector<std::string> extraExtensions;

	std::shared_ptr<packagejson::InfoCache> packageJsonInfoCache;

	ResolutionData() = default;
	ResolutionData(const CompilerOptions* compilerOptions_,
	               std::string typingsLocation_, std::string projectName_,
	               std::vector<std::string> extraExtensions_,
	               std::shared_ptr<packagejson::InfoCache> cache_)
	    : compilerOptions(compilerOptions_),
	      typingsLocation(std::move(typingsLocation_)),
	      projectName(std::move(projectName_)),
	      extraExtensions(std::move(extraExtensions_)),
	      packageJsonInfoCache(std::move(cache_)) {}

	// Clone — Go copies the package-json cache table without copying its
	// entries.
	std::shared_ptr<ResolutionData> Clone() {
		return std::make_shared<ResolutionData>(
		    compilerOptions, typingsLocation, projectName, extraExtensions,
		    packageJsonInfoCache->Clone());
	}

	void PackageJsonCacheEntries(
	    const std::function<bool(
	        const std::string&,
	        std::shared_ptr<packagejson::InfoCacheEntry>)>& f) {
		packageJsonInfoCache->Range(f);
	}

	// resolver.go:371 — a fresh DefaultResolver over this data (new
	// per-resolver caches; the package-json cache table is shared).
	class DefaultResolver* NewResolver(ResolutionHost* host);
};

inline std::string getRedirectConfigName(
    const ResolvedProjectReference* redirect) {
	if (redirect == nullptr) {
		return "";
	}
	return redirect->ConfigName();
}

}  // namespace tsc::module
