// extendedconfigcache.go — port of
// tsc/internal/execute/tsc/extendedconfigcache.go.

#include "internal/execute/tsc/extendedconfigcache.h"

namespace tsc::execute::tsc {

// GetExtendedConfig — extendedconfigcache.go:25. Implements
// tsoptions.ExtendedConfigCache.
tsoptions::ExtendedConfigCacheEntry* ExtendedConfigCache::GetExtendedConfig(
    std::string_view fileName, const tspath::Path& path,
    const std::vector<tspath::Path>& resolutionStack,
    tsoptions::ParseConfigHost* host) {
	auto [entry, loaded] = loadOrStoreNewLockedEntry(path);
	if (!loaded) {
		entry->entry.reset(tsoptions::ParseExtendedConfig(
		    fileName, path, resolutionStack, host, this));
	}
	auto* result = entry->entry.get();
	entry->mu.unlock();
	return result;
}

// loadOrStoreNewLockedEntry — extendedconfigcache.go:36. Loads an existing
// entry or creates a new one. The returned entry's mutex is locked.
std::pair<extendedConfigCacheEntry*, bool>
ExtendedConfigCache::loadOrStoreNewLockedEntry(const tspath::Path& path) {
	auto entry = std::make_shared<extendedConfigCacheEntry>();
	entry->mu.lock();
	auto [existing, loaded] = m.LoadOrStore(path, entry);
	if (loaded) {
		entry->mu.unlock();
		existing->mu.lock();
		return {existing.get(), true};
	}
	return {entry.get(), false};
}

}  // namespace tsc::execute::tsc
