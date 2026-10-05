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

struct caches {
	std::shared_ptr<packagejson::InfoCache> packageJsonInfoCache;

	moduleResolutionCache moduleResolutionCache_;
	typeRefDirectiveResolutionCache typeRefDirectiveResolutionCache_;

	// Cached representations for `core.CompilerOptions.paths`, keyed by the
	// path mappings themselves. This does not handle other path patterns
	// such as `typesVersions`.
	parsedPatternsCache parsedPatternsForPaths;
};

inline caches newCaches(std::string_view currentDirectory,
                        bool useCaseSensitiveFileNames,
                        const CompilerOptions* options) {
	caches c;
	c.packageJsonInfoCache = std::make_shared<packagejson::InfoCache>(
	    currentDirectory, useCaseSensitiveFileNames);
	return c;  // move (SyncMap members are move-only)
}

inline std::string getRedirectConfigName(
    const ResolvedProjectReference* redirect) {
	if (redirect == nullptr) {
		return "";
	}
	return redirect->ConfigName();
}

}  // namespace tsc::module
