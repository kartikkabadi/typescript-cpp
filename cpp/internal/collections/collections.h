// collections — port of tsc/internal/collections (the subset used by the
// module slice): OrderedMap, SyncMap, Set.
#pragma once

#include <algorithm>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tsc::collections {

// OrderedMap — insertion-ordered map mirroring
// tsc/internal/collections/ordered_map.go. Keys keep insertion order;
// re-Setting an existing key overwrites the value without moving it.
template <typename K, typename V>
struct OrderedMap {
	std::vector<K> keys;
	std::unordered_map<K, V> mp;

	OrderedMap() = default;
	explicit OrderedMap(size_t hint) { keys.reserve(hint); mp.reserve(hint); }

	void Set(const K& key, V value) {
		if (mp.find(key) == mp.end()) {
			keys.push_back(key);
		}
		mp[key] = std::move(value);
	}

	std::pair<V*, bool> Get(const K& key) {
		auto it = mp.find(key);
		if (it == mp.end()) {
			return {nullptr, false};
		}
		return {&it->second, true};
	}
	std::pair<const V*, bool> Get(const K& key) const {
		auto it = mp.find(key);
		if (it == mp.end()) {
			return {nullptr, false};
		}
		return {&it->second, true};
	}

	// GetOrZero — returns the value or a default-constructed V.
	V GetOrZero(const K& key) const {
		auto it = mp.find(key);
		if (it == mp.end()) {
			return V{};
		}
		return it->second;
	}

	std::pair<std::pair<K, V>, bool> EntryAt(int index) const {
		if (index < 0 || index >= static_cast<int>(keys.size())) {
			return {{K{}, V{}}, false};
		}
		const K& key = keys[index];
		return {{key, mp.at(key)}, true};
	}

	bool Has(const K& key) const { return mp.find(key) != mp.end(); }

	// Delete — mirrors Go: returns (old value, true) when present.
	std::pair<V, bool> Delete(const K& key) {
		auto it = mp.find(key);
		if (it == mp.end()) {
			return {V{}, false};
		}
		V v = std::move(it->second);
		mp.erase(it);
		auto ki = std::find(keys.begin(), keys.end(), key);
		if (ki != keys.end()) keys.erase(ki);
		return {std::move(v), true};
	}

	size_t Size() const { return mp.size(); }

	// Keys/Entries iterate in insertion order. Go enumerates new keys added
	// during iteration; index-based loops reproduce that.
	const std::vector<K>& Keys() const { return keys; }

	OrderedMap Clone() const { return *this; }
};

// SyncMap — mutex-guarded map mirroring tsc/internal/collections/sync_map.go.
// LoadOrStore keeps the first stored value for a key (Go semantics).
// Go's SyncMap isn't movable; C++ move ops exist only so containing structs
// (e.g. module::caches) can be move-assigned like Go pointer copies.
template <typename K, typename V, typename Hash = std::hash<K>>
struct SyncMap {
private:
	mutable std::mutex mu;
	std::unordered_map<K, V, Hash> mp;

public:
	SyncMap() = default;
	SyncMap(const SyncMap&) = delete;
	SyncMap& operator=(const SyncMap&) = delete;
	SyncMap(SyncMap&& o) noexcept {
		std::lock_guard<std::mutex> lock(o.mu);
		mp = std::move(o.mp);
	}
	SyncMap& operator=(SyncMap&& o) noexcept {
		if (this != &o) {
			std::scoped_lock lock(mu, o.mu);
			mp = std::move(o.mp);
		}
		return *this;
	}

	std::pair<V, bool> Load(const K& key) {
		std::lock_guard<std::mutex> lock(mu);
		auto it = mp.find(key);
		if (it == mp.end()) {
			return {V{}, false};
		}
		return {it->second, true};
	}

	void Store(const K& key, V value) {
		std::lock_guard<std::mutex> lock(mu);
		mp[key] = std::move(value);
	}

	// LoadOrStore — returns (stored value, true) if present, else stores and
	// returns (value, false).
	std::pair<V, bool> LoadOrStore(const K& key, V value) {
		std::lock_guard<std::mutex> lock(mu);
		auto it = mp.find(key);
		if (it != mp.end()) {
			return {it->second, true};
		}
		mp.emplace(key, value);
		return {value, false};
	}

	bool Delete(const K& key) {
		std::lock_guard<std::mutex> lock(mu);
		return mp.erase(key) != 0;
	}

	// Range — Go's SyncMap.Range iterates until f returns false.
	void Range(const std::function<bool(const K&, const V&)>& f) {
		std::lock_guard<std::mutex> lock(mu);
		for (auto& kv : mp) {
			if (!f(kv.first, kv.second)) return;
		}
	}

	size_t Size() {
		std::lock_guard<std::mutex> lock(mu);
		return mp.size();
	}
};

// === slice: modulespecifiers ===
// SyncSet — mirrors tsc/internal/collections/syncset.go (mutex-guarded set
// built on SyncMap; the bool value stands in for Go's struct{}).
template <typename T>
struct SyncSet {
private:
	SyncMap<T, bool> m;

public:
	bool Has(const T& key) { return m.Load(key).second; }

	void Add(const T& key) { AddIfAbsent(key); }

	// AddIfAbsent — returns true if the key was not already present
	// (opposite of the return value of LoadOrStore).
	bool AddIfAbsent(const T& key) {
		auto [_, loaded] = m.LoadOrStore(key, false);
		return !loaded;
	}

	bool Delete(const T& key) { return m.Delete(key); }

	// Range — iterates until fn returns false.
	void Range(const std::function<bool(const T&)>& fn) {
		m.Range([&](const T& key, const bool&) { return fn(key); });
	}

	// Size — approximate count (may race with concurrent modification).
	size_t Size() { return m.Size(); }

	bool IsEmpty() { return Size() == 0; }

	std::vector<T> ToSlice() {
		std::vector<T> arr;
		arr.reserve(Size());
		Range([&](const T& key) {
			arr.push_back(key);
			return true;
		});
		return arr;
	}
};

// Set — mirrors tsc/internal/collections/set.go.
template <typename T>
struct Set {
private:
	std::unordered_set<T> items;

public:
	Set() = default;
	Set(std::initializer_list<T> init) : items(init) {}

	bool Has(const T& key) const { return items.count(key) != 0; }
	void Add(const T& key) { items.insert(key); }
	void Delete(const T& key) { items.erase(key); }
	size_t Size() const { return items.size(); }

	const std::unordered_set<T>& Keys() const { return items; }

	Set Clone() const { return *this; }

	void AddRange(const std::vector<T>& v) {
		for (auto& x : v) items.insert(x);
	}
};

}  // namespace tsc::collections
