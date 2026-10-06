#pragma once

// extendedconfigcache.go — port of
// tsc/internal/execute/tsc/extendedconfigcache.go: the CLI's single-session
// tsoptions::ExtendedConfigCache implementation.

#include <memory>
#include <mutex>
#include <string_view>
#include <utility>

#include "internal/collections/collections.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tspath/tspath.h"

namespace tsc::execute::tsc {

// extendedConfigCacheEntry — extendedconfigcache.go:19.
struct extendedConfigCacheEntry {
	std::unique_ptr<tsoptions::ExtendedConfigCacheEntry> entry;
	std::mutex mu;
};

// ExtendedConfigCache — extendedconfigcache.go:14. A minimal implementation
// of tsoptions.ExtendedConfigCache. It is concurrency-safe, but stores
// cached entries permanently. This implementation should not be used for
// long-running processes where configuration changes over the course of
// multiple compilations.
struct ExtendedConfigCache : tsoptions::ExtendedConfigCache {
	collections::SyncMap<tspath::Path,
	                     std::shared_ptr<extendedConfigCacheEntry>>
	    m;

	// GetExtendedConfig — extendedconfigcache.go:25.
	tsoptions::ExtendedConfigCacheEntry* GetExtendedConfig(
	    std::string_view fileName, const tspath::Path& path,
	    const std::vector<tspath::Path>& resolutionStack,
	    tsoptions::ParseConfigHost* host) override;

	// loadOrStoreNewLockedEntry — extendedconfigcache.go:36. The returned
	// entry's mutex is locked.
	std::pair<extendedConfigCacheEntry*, bool>
	loadOrStoreNewLockedEntry(const tspath::Path& path);
};

}  // namespace tsc::execute::tsc
