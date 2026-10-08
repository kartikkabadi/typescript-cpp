#pragma once

// refcountcache.go — RefCountCache: parse-once values guarded by refcounts.

#include <functional>
#include <memory>
#include <mutex>
#include <utility>

#include "internal/ast/ast.h" // tscUnreachable
#include "internal/collections/collections.h"
#include "internal/gostd/gostd.h"

namespace tsc::project {

// refCountCacheEntry — refcountcache.go.
template <typename V>
struct refCountCacheEntry {
	std::mutex mu;
	V value;
	int refCount = 0;
};

// RefCountCacheOptions — refcountcache.go.
struct RefCountCacheOptions {
	// DisableDeletion prevents entries from being removed from the cache.
	// Used for testing.
	bool DisableDeletion = false;
};

// RefCountCache — refcountcache.go. `Hash` selects the unordered_map key
// hash for non-std::hashable Go-comparable keys.
template <typename K, typename V, typename AcquireArgs,
          typename Hash = std::hash<K>>
struct RefCountCache {
	RefCountCacheOptions Options;
	collections::SyncMap<K, std::shared_ptr<refCountCacheEntry<V>>, Hash>
	    entries;

	std::function<V(const K&, const AcquireArgs&)> parse;

	RefCountCache(const RefCountCacheOptions& options,
	                std::function<V(const K&, const AcquireArgs&)> parse)
	    : Options(options), parse(std::move(parse)) {}

	// Acquire retrieves or creates a cache entry for the given identity and
	// hash. If an entry exists with matching identity and hash, its refcount
	// is incremented and the cached value is returned. Otherwise, parse() is
	// called to create the value, which is stored and returned with refcount
	// 1.
	//
	// The caller is responsible for calling Deref when done with the value.
	V Acquire(const K& identity, const AcquireArgs& acquireArgs) {
		auto [entry, loaded] = loadOrStoreNewLockedEntry(identity);
		struct UnlockGuard {
			refCountCacheEntry<V>* e;
			~UnlockGuard() { e->mu.unlock(); }
		} guard{entry.get()};
		if (!loaded) {
			// New entry - parse the value
			entry->value = parse(identity, acquireArgs);
			return entry->value;
		}
		return entry->value;
	}

	bool Has(const K& identity) {
		auto [_, ok] = entries.Load(identity);
		return ok;
	}

	// AcquireOrError retrieves an existing entry (incrementing its refcount)
	// or produces a new one via produce. If produce returns an error, no
	// entry is stored and the error is returned, so callers can cache only
	// successful results. produce runs while holding the new entry's lock, so
	// concurrent acquisitions of the same identity that miss serialize on it.
	//
	// The caller is responsible for calling Deref when a value is returned
	// without error.
	std::pair<V, gostd::Error> AcquireOrError(
		const K& identity, std::function<std::pair<V, gostd::Error>()> produce) {
		auto [entry, loaded] = loadOrStoreNewLockedEntry(identity);
		struct UnlockGuard {
			refCountCacheEntry<V>* e;
			~UnlockGuard() { e->mu.unlock(); }
		} guard{entry.get()};
		if (loaded) {
			return {entry->value, gostd::Error{}};
		}
		auto [value, err] = produce();
		if (err != nullptr) {
			// Undo the speculative entry so failures are not cached.
			entry->refCount = 0;
			entries.Delete(identity);
			return {value, err};
		}
		entry->value = value;
		return {value, gostd::Error{}};
	}

	// Ref increments the reference count for an existing entry.
	// Panics if the entry does not exist.
	void Ref(const K& identity) {
		auto [entry, ok] = entries.Load(identity);
		if (!ok) {
			TSC_UNREACHABLE("cache entry not found");
		}
		entry->mu.lock();
		struct UnlockGuard {
			refCountCacheEntry<V>* e;
			~UnlockGuard() { e->mu.unlock(); }
		} guard{entry.get()};
		if (entry->refCount <= 0 && !Options.DisableDeletion) {
			// Entry was deleted while we were acquiring the lock
			auto [newEntry, _loaded] = loadOrStoreNewLockedEntry(identity);
			struct UnlockGuard2 {
				refCountCacheEntry<V>* e;
				~UnlockGuard2() { e->mu.unlock(); }
			} guard2{newEntry.get()};
			newEntry->value = entry->value;
			return;
		}
		entry->refCount++;
	}

	// Deref decrements the reference count for an entry.
	// When the refcount reaches zero, the entry is removed from the cache
	// (unless DisableDeletion is set).
	void Deref(const K& identity) {
		auto [entry, ok] = entries.Load(identity);
		if (!ok) {
			return;
		}
		entry->mu.lock();
		struct UnlockGuard {
			refCountCacheEntry<V>* e;
			~UnlockGuard() { e->mu.unlock(); }
		} guard{entry.get()};
		entry->refCount--;
		if (entry->refCount <= 0 && !Options.DisableDeletion) {
			entries.Delete(identity);
		}
	}

	// loadOrStoreNewLockedEntry loads an existing entry or creates a new
	// one. The returned entry's mutex is locked and its refCount is
	// incremented (or initialized to 1 in the case of a new entry).
	std::pair<std::shared_ptr<refCountCacheEntry<V>>, bool>
	loadOrStoreNewLockedEntry(const K& key) {
		auto entry = std::make_shared<refCountCacheEntry<V>>();
		entry->refCount = 1;
		entry->mu.lock();
		auto [existing, loaded] = entries.LoadOrStore(key, entry);
		if (loaded) {
			entry->mu.unlock();
			existing->mu.lock();
			if (existing->refCount <= 0 && !Options.DisableDeletion) {
				// Existing entry was deleted while we were acquiring the lock
				existing->mu.unlock();
				return loadOrStoreNewLockedEntry(key);
			}
			existing->refCount++;
			return {existing, true};
		}
		return {entry, false};
	}
};

// NewRefCountCache — refcountcache.go.
template <typename K, typename V, typename AcquireArgs,
          typename Hash = std::hash<K>>
RefCountCache<K, V, AcquireArgs, Hash>* newRefCountCache(
	const RefCountCacheOptions& options,
	std::function<V(const K&, const AcquireArgs&)> parse) {
	return new RefCountCache<K, V, AcquireArgs, Hash>(options,
	                                                std::move(parse));
}

} // namespace tsc::project
