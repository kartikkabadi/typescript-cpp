// module/cache.go — port of tsc/internal/module/cache.go.
// === slice: module ===

#include "internal/module/cache.h"
#include "internal/module/resolver.h"

namespace tsc::module {

// parsedPatternsCache::Get — cache.go.
std::shared_ptr<ParsedPatterns> parsedPatternsCache::Get(
    const void* pathMappings, std::shared_ptr<ParsedPatterns> parsed) {
	auto [patterns, ok] = cache.Load(pathMappings);
	if (!ok) {
		auto [p, _] = cache.LoadOrStore(pathMappings, std::move(parsed));
		return p;
	}
	return patterns;
}

}  // namespace tsc::module
