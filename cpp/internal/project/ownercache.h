#pragma once

// ownercache.go — OwnerCache: like RefCountCache, but each entry tracks the
// set of its owners instead of a count.

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <utility>

#include "internal/ast/ast.h" // tscUnreachable
#include "internal/collections/collections.h"

namespace tsc::project {

// ownerCacheEntry — ownercache.go.
template <typename V>
struct ownerCacheEntry {
	std::mutex mu;
	V value;
	std::unordered_set<uint64_t> owners;
};

// OwnerCache is like RefCountCache, but each entry tracks the set of its
// owners instead of a count. We use this to associate extended config cache
// entries with each snapshot that contains them, since the same config can
// be Acquired multiple times during config parsing while only appearing once
// in the ParsedCommandLine's list of extended files. When updating this
// code, check if the same changes should be made to RefCountCache as well.
template <typename K, typename V, typename LoadArgs>
struct OwnerCache {
	collections::SyncMap<K, std::shared_ptr<ownerCacheEntry<V>>> entries;

	std::function<bool(const K&, const V&, const LoadArgs&)> isExpired;
	std::function<V(const K&, const LoadArgs&)> parse;

	OwnerCache(std::function<V(const K&, const LoadArgs&)> parse,
	           std::function<bool(const K&, const V&, const LoadArgs&)> isExpired)
	    : isExpired(std::move(isExpired)), parse(std::move(parse)) {}

	V LoadAndAcquire(const K& identity, uint64_t owner,
	                 const LoadArgs& loadArgs) {
		auto [entry, loaded] = loadOrStoreLockedEntry(identity);
		struct UnlockGuard {
			ownerCacheEntry<V>* e;
			~UnlockGuard() { e->mu.unlock(); }
		} guard{entry.get()};
		if (!loaded || (isExpired != nullptr &&
		                isExpired(identity, entry->value, loadArgs))) {
			entry->value = parse(identity, loadArgs);
		}
		entry->owners.insert(owner);
		return entry->value;
	}

	void Acquire(const K& identity, uint64_t owner, const V& value) {
		auto [entry, loaded] = loadOrStoreLockedEntry(identity);
		struct UnlockGuard {
			ownerCacheEntry<V>* e;
			~UnlockGuard() { e->mu.unlock(); }
		} guard{entry.get()};
		if (!loaded) {
			entry->value = value;
		}
		entry->owners.insert(owner);
	}

	// AddOwner adds an owner to an existing live entry. The entry must exist
	// and have at least one current owner; callers must ensure the entry is
	// kept alive (e.g. via snapshot ref counting).
	void AddOwner(const K& identity, uint64_t owner) {
		auto [entry, ok] = entries.Load(identity);
		if (!ok) {
			TSC_UNREACHABLE("OwnerCache.AddOwner: entry not found");
		}
		entry->mu.lock();
		struct UnlockGuard {
			ownerCacheEntry<V>* e;
			~UnlockGuard() { e->mu.unlock(); }
		} guard{entry.get()};
		if (entry->owners.empty()) {
			TSC_UNREACHABLE("OwnerCache.AddOwner: entry has no owners");
		}
		entry->owners.insert(owner);
	}

	bool Has(const K& identity) {
		auto [_, ok] = entries.Load(identity);
		return ok;
	}

	void Release(const K& identity, uint64_t owner) {
		auto [entry, ok] = entries.Load(identity);
		if (!ok) {
			return;
		}
		entry->mu.lock();
		struct UnlockGuard {
			ownerCacheEntry<V>* e;
			~UnlockGuard() { e->mu.unlock(); }
		} guard{entry.get()};
		entry->owners.erase(owner);
		if (entry->owners.empty()) {
			entries.Delete(identity);
		}
	}

	// loadOrStoreLockedEntry loads an existing entry or creates a new one.
	// The returned entry's mutex is locked.
	std::pair<std::shared_ptr<ownerCacheEntry<V>>, bool>
	loadOrStoreLockedEntry(const K& key) {
		auto entry = std::make_shared<ownerCacheEntry<V>>();
		entry->mu.lock();
		auto [existing, loaded] = entries.LoadOrStore(key, entry);
		if (loaded) {
			entry->mu.unlock();
			existing->mu.lock();
			if (existing->owners.empty()) {
				existing->mu.unlock();
				return loadOrStoreLockedEntry(key);
			}
			return {existing, true};
		}
		return {entry, false};
	}
};

// NewOwnerCache — ownercache.go.
template <typename K, typename V, typename LoadArgs>
OwnerCache<K, V, LoadArgs>* newOwnerCache(
	std::function<V(const K&, const LoadArgs&)> parse,
	std::function<bool(const K&, const V&, const LoadArgs&)> isExpired) {
	return new OwnerCache<K, V, LoadArgs>(std::move(parse),
	                                      std::move(isExpired));
}

} // namespace tsc::project
