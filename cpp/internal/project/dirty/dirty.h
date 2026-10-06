// dirty — port of tsc/internal/project/dirty (map.go + entry.go +
// interfaces.go + mapbuilder.go + box.go + cloneablemap.go + util.go): the
// copy-on-write map machinery the autoimport registry uses to clone bucket
// state. Written for the ls-autoimport slice.
//
// Go generics: Map[K, V Cloneable[V]] — in C++ the clone step dispatches on
// V's shape: pointers clone via `v->Clone()`, values via `v.Clone()`, and
// unordered_map values clone via a shallow copy (CloneableMap).
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "internal/collections/collections.h"

namespace tsc::dirty {

// cloneValue — Go's Cloneable[V].Clone() dispatch.
template <typename V>
inline V cloneValue(const V& v) {
	if constexpr (std::is_pointer_v<V>) {
		return v->Clone();
	} else {
		return v.Clone();
	}
}

// IValue — interfaces.go Value[T]: the read/mutate handle shared by
// MapEntry, SyncMapEntry and lockedEntry. Named `Value` via alias below.
template <typename V>
struct IValue {
	virtual ~IValue() = default;
	virtual V Value() const = 0;
	virtual V Original() const = 0;
	virtual bool Dirty() const = 0;
	virtual void Change(const std::function<void(V&)>& apply) = 0;
	virtual bool ChangeIf(const std::function<bool(const V&)>& cond,
	                      const std::function<void(V&)>& apply) = 0;
	virtual void Delete() = 0;
	virtual void Locked(const std::function<void(IValue*)>& fn) = 0;
};

template <typename V>
using Value = IValue<V>;

template <typename K, typename V>
inline std::unordered_map<K, V> cloneValue(
	const std::unordered_map<K, V>& v) {
	return v; // maps.Clone — shallow copy
}

// mapEntry — entry.go.
template <typename K, typename V>
struct mapEntry {
	K key;
	V original{};
	V value{};
	bool dirty = false;
	bool delete_ = false;

	const K& Key() const { return key; }
	const V& Original() const { return original; }
	// Value — Go returns the zero V on deleted entries.
	V Value() const {
		if (delete_) return V{};
		return value;
	}
	bool Dirty() const { return dirty; }
};

// MapEntry — map.go.
template <typename K, typename V>
struct Map;

template <typename K, typename V>
struct MapEntry : mapEntry<K, V>, IValue<V>,
                  std::enable_shared_from_this<MapEntry<K, V>> {
	Map<K, V>* m = nullptr;

	V Value() const override { return mapEntry<K, V>::Value(); }
	V Original() const override { return this->original; }
	bool Dirty() const override { return this->dirty; }

	// Change — clones the value on first change (copy-on-write), then runs
	// `apply` on the mutable clone.
	void Change(const std::function<void(V&)>& apply) override {
		if (this->delete_) {
			throw std::logic_error("tried to change a deleted entry");
		}
		if (!this->dirty) {
			this->value = cloneValue(this->value);
			this->dirty = true;
			m->dirty[this->key] = this->shared_from_this();
		}
		apply(this->value);
	}

	void Replace(V newValue) {
		if (this->delete_) {
			throw std::logic_error("tried to change a deleted entry");
		}
		if (!this->dirty) {
			this->dirty = true;
			m->dirty[this->key] = this->shared_from_this();
		}
		this->value = std::move(newValue);
	}

	bool ChangeIf(const std::function<bool(const V&)>& cond,
	              const std::function<void(V&)>& apply) override {
		if (cond(this->Value())) {
			Change(apply);
			return true;
		}
		return false;
	}

	void Delete() override {
		if (!this->dirty) {
			m->dirty[this->key] = this->shared_from_this();
		}
		this->delete_ = true;
	}

	// Locked — interfaces.go Value[T].Locked.
	void Locked(const std::function<void(IValue<V>*)>& fn) override {
		fn(this);
	}
};

// Map — map.go. A base map with a dirty overlay; Get/Range hand out
// MapEntry handles that register themselves in `dirty` on first mutation.
template <typename K, typename V>
struct Map {
	std::unordered_map<K, V> base;
	std::unordered_map<K, std::shared_ptr<MapEntry<K, V>>> dirty;

	Map() = default;
	explicit Map(std::unordered_map<K, V> base) : base(std::move(base)) {}

	// Get — returns the entry for key (a detached clean entry for base
	// values; it registers itself into dirty on Change/Delete).
	std::pair<std::shared_ptr<MapEntry<K, V>>, bool> Get(const K& key) {
		if (auto it = dirty.find(key); it != dirty.end()) {
			if (it->second->delete_) {
				return {nullptr, false};
			}
			return {it->second, true};
		}
		auto it = base.find(key);
		if (it == base.end()) {
			return {nullptr, false};
		}
		auto entry = std::make_shared<MapEntry<K, V>>();
		entry->m = this;
		entry->key = key;
		entry->original = it->second;
		entry->value = it->second;
		return {entry, true};
	}

	// Add — sets a new entry without checking the base map. The entry is
	// considered dirty (a fresh value; it will not be cloned before
	// changing).
	void Add(const K& key, V value) {
		auto entry = std::make_shared<MapEntry<K, V>>();
		entry->m = this;
		entry->key = key;
		entry->value = std::move(value);
		entry->dirty = true;
		dirty[key] = std::move(entry);
	}

	void Change(const K& key, const std::function<void(V&)>& apply) {
		auto [entry, ok] = Get(key);
		if (ok) {
			entry->Change(apply);
		} else {
			throw std::logic_error("tried to change a non-existent entry");
		}
	}

	bool TryDelete(const K& key) {
		auto [entry, ok] = Get(key);
		if (ok) {
			entry->Delete();
			return true;
		}
		return false;
	}

	void Delete(const K& key) {
		if (!TryDelete(key)) {
			throw std::logic_error("tried to delete a non-existent entry");
		}
	}

	// Range — dirty entries first (skipping deletes), then base entries not
	// seen in dirty. Iterates until fn returns false.
	void Range(const std::function<bool(MapEntry<K, V>*)>& fn) {
		std::unordered_set<K> seenInDirty;
		for (auto& [k, entry] : dirty) {
			seenInDirty.insert(entry->key);
			if (!entry->delete_ && !fn(entry.get())) {
				return;
			}
		}
		for (auto& [key, value] : base) {
			if (seenInDirty.count(key)) {
				continue; // already processed in dirty entries
			}
			auto entry = std::make_shared<MapEntry<K, V>>();
			entry->m = this;
			entry->key = key;
			entry->original = value;
			entry->value = value;
			if (!fn(entry.get())) {
				return;
			}
		}
	}

	void Clear() {
		dirty.clear();
		base.clear();
	}

	// Finalize — (result, changed): base unchanged => {base, false}.
	std::pair<std::unordered_map<K, V>, bool> Finalize() {
		if (dirty.empty()) {
			return {base, false};
		}
		std::unordered_map<K, V> result = base;
		for (auto& [key, entry] : dirty) {
			if (entry->delete_) {
				result.erase(key);
			} else {
				result[key] = entry->value;
			}
		}
		return {std::move(result), true};
	}
};

// NewMap — map.go.
template <typename K, typename V>
inline Map<K, V>* newMap(std::unordered_map<K, V> base) {
	return new Map<K, V>(std::move(base));
}

// MapBuilder — mapbuilder.go. Unlike Map, values are never cloned: `dirty`
// holds builder-form values produced by `toBuilder` and finalized by
// `build`.
template <typename K, typename VBase, typename VBuilder>
struct MapBuilder {
	std::unordered_map<K, VBase> base;
	std::unordered_map<K, VBuilder> dirty;
	std::unordered_set<K> deleted;
	std::function<VBuilder(const VBase&)> toBuilder;
	std::function<VBase(const VBuilder&)> build;

	void Set(const K& key, VBuilder value) {
		dirty[key] = std::move(value);
		deleted.erase(key);
	}

	void Delete(const K& key) {
		deleted.insert(key);
		dirty.erase(key);
	}

	void Clear() {
		dirty.clear();
		for (auto& [key, _] : base) {
			deleted.insert(key);
		}
	}

	bool Has(const K& key) const {
		if (deleted.count(key)) {
			return false;
		}
		if (dirty.count(key)) {
			return true;
		}
		return base.count(key) != 0;
	}

	std::unordered_map<K, VBase> Build() {
		if (dirty.empty() && deleted.empty()) {
			return base;
		}
		std::unordered_map<K, VBase> result = base;
		for (auto& key : deleted) {
			result.erase(key);
		}
		for (auto& [key, value] : dirty) {
			result[key] = build(value);
		}
		return result;
	}
};

// NewMapBuilder — mapbuilder.go.
template <typename K, typename VBase, typename VBuilder>
inline MapBuilder<K, VBase, VBuilder>* newMapBuilder(
	std::unordered_map<K, VBase> base,
	std::function<VBuilder(const VBase&)> toBuilder,
	std::function<VBase(const VBuilder&)> build) {
	auto* mb = new MapBuilder<K, VBase, VBuilder>();
	mb->base = std::move(base);
	mb->toBuilder = std::move(toBuilder);
	mb->build = std::move(build);
	return mb;
}

// Box — box.go.
template <typename T>
struct Box : IValue<T> {
	T original{};
	T value{};
	bool dirty = false;
	bool delete_ = false;

	Box() = default;
	explicit Box(T original) : original(original), value(original) {}

	T Value() const override {
		if (delete_) return T{};
		return value;
	}
	T Original() const override { return original; }
	bool Dirty() const override { return dirty; }

	void Set(T v) {
		value = std::move(v);
		delete_ = false;
		dirty = true;
	}

	void Change(const std::function<void(T&)>& apply) override {
		if (!dirty) {
			value = cloneValue(value);
			dirty = true;
		}
		apply(value);
	}

	bool ChangeIf(const std::function<bool(const T&)>& cond,
	              const std::function<void(T&)>& apply) override {
		if (cond(value)) {
			Change(apply);
			return true;
		}
		return false;
	}

	void Delete() override { delete_ = true; }

	// Locked — box.go (no mutex; just calls fn(b)).
	void Locked(const std::function<void(IValue<T>*)>& fn) override {
		fn(this);
	}

	std::pair<T, bool> Finalize() { return {Value(), dirty || delete_}; }
};

template <typename T>
inline Box<T>* newBox(T original) {
	return new Box<T>(std::move(original));
}

// CloneableMap — cloneablemap.go (a map whose Clone is a shallow copy).
template <typename K, typename V>
struct CloneableMap : std::unordered_map<K, V> {
	CloneableMap Clone() const { return *this; }
};

// CloneMapIfNil — util.go: dirty's map when nil falls back to a clone of
// original's map (empty when both nil).
template <typename K, typename V, typename T, typename F>
inline std::unordered_map<K, V> cloneMapIfNil(T* dirtyObj, T* original,
                                              F getMap) {
	auto dirtyMap = dirtyObj != nullptr ? getMap(dirtyObj) : nullptr;
	if (dirtyMap == nullptr) {
		if (original == nullptr) {
			return {};
		}
		auto originalMap = getMap(original);
		if (originalMap == nullptr) {
			return {};
		}
		return *originalMap; // maps.Clone
	}
	return *dirtyMap;
}

// === slice: project ===
// syncmap.go — mutex-guarded variant of Map. Entries are shared_ptr'd
// (Go GC): the dirty collections.SyncMap owns dirty entries; clean base
// entries are handed out as fresh shared_ptrs that register themselves in
// dirty on first mutation. proxyFor forwarding preserves Go's race
// resolution when two handles target the same key.

template <typename K, typename V>
struct SyncMap;

template <typename K, typename V>
struct SyncMapEntry : mapEntry<K, V>, IValue<V>,
                      std::enable_shared_from_this<SyncMapEntry<K, V>> {
	SyncMap<K, V>* m = nullptr;
	mutable std::mutex mu;
	// proxyFor is set when this entry loses a race to become the dirty entry
	// for a value. Since two goroutines hold a reference to two entries that
	// may try to mutate the same underlying value, all mutations are routed
	// through the one that actually exists in the dirty map.
	std::shared_ptr<SyncMapEntry> proxyFor;

	V valueLocked() const {
		if (this->delete_) {
			return V{};
		}
		return this->value;
	}

	V Value() const override {
		std::lock_guard<std::mutex> lk(mu);
		if (proxyFor != nullptr) {
			return proxyFor->Value();
		}
		return valueLocked();
	}

	V Original() const override { return this->original; }

	bool Dirty() const override {
		std::lock_guard<std::mutex> lk(mu);
		if (proxyFor != nullptr) {
			return proxyFor->Dirty();
		}
		return this->dirty;
	}

	void Locked(const std::function<void(IValue<V>*)>& fn) override;

	void Change(const std::function<void(V&)>& apply) override {
		std::lock_guard<std::mutex> lk(mu);
		if (proxyFor != nullptr) {
			proxyFor->Change(apply);
			return;
		}
		changeLocked(apply);
	}

	void changeLocked(const std::function<void(V&)>& apply) {
		if (this->dirty) {
			apply(this->value);
			return;
		}
		auto [entry, loaded] =
			m->dirty.LoadOrStore(this->key, this->shared_from_this());
		std::unique_lock<std::mutex> entryLock(entry->mu, std::defer_lock);
		if (loaded) {
			entryLock.lock();
		}
		if (!entry->dirty) {
			entry->value = cloneValue(entry->value);
			entry->dirty = true;
		}
		if (loaded) {
			proxyFor = entry;
			this->value = entry->value;
			this->dirty = true;
			this->delete_ = entry->delete_;
		}
		apply(entry->value);
	}

	bool ChangeIf(const std::function<bool(const V&)>& cond,
	              const std::function<void(V&)>& apply) override {
		std::lock_guard<std::mutex> lk(mu);
		if (proxyFor != nullptr) {
			return proxyFor->ChangeIf(cond, apply);
		}
		if (cond(this->value)) {
			changeLocked(apply);
			return true;
		}
		return false;
	}

	void Delete() override {
		std::lock_guard<std::mutex> lk(mu);
		if (proxyFor != nullptr) {
			proxyFor->Delete();
			return;
		}
		if (this->dirty) {
			this->delete_ = true;
			return;
		}
		auto [entry, loaded] =
			m->dirty.LoadOrStore(this->key, this->shared_from_this());
		if (loaded) {
			std::lock_guard<std::mutex> el(entry->mu);
			this->delete_ = true;
		} else {
			entry->delete_ = true;
		}
	}

	void deleteLocked() {
		if (this->dirty) {
			this->delete_ = true;
			return;
		}
		auto [entry, loaded] =
			m->dirty.LoadOrStore(this->key, this->shared_from_this());
		if (loaded) {
			std::lock_guard<std::mutex> el(entry->mu);
			proxyFor = entry;
			this->value = entry->value;
			this->delete_ = true;
			this->dirty = entry->dirty;
		}
		entry->delete_ = true;
	}

	void DeleteIf(const std::function<bool(const V&)>& cond) {
		std::lock_guard<std::mutex> lk(mu);
		if (proxyFor != nullptr) {
			proxyFor->DeleteIf(cond);
			return;
		}
		if (cond(this->value)) {
			deleteLocked();
		}
	}
};

// lockedEntry — the Value[V] handed to Locked callbacks while the entry's
// mutex is held.
template <typename K, typename V>
struct lockedEntry : IValue<V> {
	SyncMapEntry<K, V>* e;

	explicit lockedEntry(SyncMapEntry<K, V>* e) : e(e) {}

	V Value() const override { return e->valueLocked(); }
	V Original() const override { return e->original; }
	bool Dirty() const override { return e->dirty; }
	void Change(const std::function<void(V&)>& apply) override {
		e->changeLocked(apply);
	}
	bool ChangeIf(const std::function<bool(const V&)>& cond,
	              const std::function<void(V&)>& apply) override {
		if (cond(e->valueLocked())) {
			e->changeLocked(apply);
			return true;
		}
		return false;
	}
	void Delete() override { e->deleteLocked(); }
	void Locked(const std::function<void(IValue<V>*)>& fn) override {
		fn(this);
	}
};

template <typename K, typename V>
inline void SyncMapEntry<K, V>::Locked(
	const std::function<void(IValue<V>*)>& fn) {
	std::lock_guard<std::mutex> lk(mu);
	if (proxyFor != nullptr) {
		proxyFor->Locked(fn);
		return;
	}
	lockedEntry<K, V> le{this};
	fn(&le);
}

template <typename K, typename V>
struct FinalizationHooks {
	std::function<void(const K& key, V value)> OnDelete;
	std::function<void(const K& key, V oldValue, V newValue)> OnChange;
	std::function<void(const K& key, V value)> OnAdd;
};

// SyncMap — syncmap.go.
template <typename K, typename V>
struct SyncMap {
	std::unordered_map<K, V> base;
	collections::SyncMap<K, std::shared_ptr<SyncMapEntry<K, V>>> dirty;

	std::pair<std::shared_ptr<SyncMapEntry<K, V>>, bool> Load(const K& key) {
		if (auto [entry, ok] = dirty.Load(key); ok) {
			std::lock_guard<std::mutex> lk(entry->mu);
			if (entry->delete_) {
				return {nullptr, false};
			}
			return {entry, true};
		}
		auto it = base.find(key);
		if (it == base.end()) {
			return {nullptr, false};
		}
		auto e = std::make_shared<SyncMapEntry<K, V>>();
		e->m = this;
		e->key = key;
		e->original = it->second;
		e->value = it->second;
		return {e, true};
	}

	std::pair<std::shared_ptr<SyncMapEntry<K, V>>, bool>
	LoadOrStore(const K& key, V value) {
		// Check for existence in the base map first so the sync map access is
		// atomic.
		if (auto it = base.find(key); it != base.end()) {
			V baseValue = it->second;
			if (auto [d, ok] = dirty.Load(key); ok) {
				std::lock_guard<std::mutex> lk(d->mu);
				if (d->delete_) {
					return {nullptr, false};
				}
				return {d, true};
			}
			auto e = std::make_shared<SyncMapEntry<K, V>>();
			e->m = this;
			e->key = key;
			e->original = baseValue;
			e->value = baseValue;
			return {e, true};
		}
		auto newEntry = std::make_shared<SyncMapEntry<K, V>>();
		newEntry->m = this;
		newEntry->key = key;
		newEntry->value = std::move(value);
		newEntry->dirty = true;
		auto [entry, loaded] = dirty.LoadOrStore(key, newEntry);
		if (loaded) {
			std::lock_guard<std::mutex> lk(entry->mu);
			if (entry->delete_) {
				return {nullptr, false};
			}
		}
		return {entry, loaded};
	}

	void Delete(const K& key) {
		auto newEntry = std::make_shared<SyncMapEntry<K, V>>();
		newEntry->m = this;
		newEntry->key = key;
		auto it = base.find(key);
		if (it != base.end()) {
			newEntry->original = it->second;
		}
		newEntry->delete_ = true;
		auto [entry, loaded] = dirty.LoadOrStore(key, newEntry);
		if (loaded) {
			entry->Delete();
		}
	}

	void Range(
		const std::function<
			bool(const std::shared_ptr<SyncMapEntry<K, V>>&)>& fn) {
		std::unordered_set<K> seenInDirty;
		dirty.Range([&](const K& key,
		                const std::shared_ptr<SyncMapEntry<K, V>>& entry) {
			seenInDirty.insert(key);
			entry->mu.lock();
			bool deleted = entry->delete_;
			entry->mu.unlock();
			if (!deleted && !fn(entry)) {
				return false;
			}
			return true;
		});
		for (auto& [key, value] : base) {
			if (seenInDirty.count(key)) {
				continue; // already processed in dirty entries
			}
			auto e = std::make_shared<SyncMapEntry<K, V>>();
			e->m = this;
			e->key = key;
			e->original = value;
			e->value = value;
			if (!fn(e)) {
				break;
			}
		}
	}

	std::pair<std::unordered_map<K, V>, bool> finalize(
		const FinalizationHooks<K, V>& hooks) {
		bool changed = false;
		std::unordered_map<K, V> result = base;
		auto ensureCloned = [&] {
			if (!changed) {
				result = base; // maps.Clone equivalent (value copy)
				changed = true;
			}
		};

		dirty.Range([&](const K& key,
		                const std::shared_ptr<SyncMapEntry<K, V>>& entry) {
			std::lock_guard<std::mutex> lk(entry->mu);
			if (entry->delete_) {
				ensureCloned();
				if (hooks.OnDelete != nullptr) {
					hooks.OnDelete(key, entry->value);
				}
				result.erase(key);
			} else if (entry->dirty) {
				ensureCloned();
				if (hooks.OnChange != nullptr || hooks.OnAdd != nullptr) {
					if (base.count(key)) {
						if (hooks.OnChange != nullptr) {
							hooks.OnChange(key, entry->original,
							               entry->value);
						}
					} else if (hooks.OnAdd != nullptr) {
						hooks.OnAdd(key, entry->value);
					}
				}
				result[key] = entry->value;
			}
			return true;
		});
		return {std::move(result), changed};
	}

	std::pair<std::unordered_map<K, V>, bool> Finalize() {
		return finalize(FinalizationHooks<K, V>{});
	}

	std::pair<std::unordered_map<K, V>, bool> FinalizeWith(
		const FinalizationHooks<K, V>& hooks) {
		return finalize(hooks);
	}
};

// NewSyncMap — syncmap.go.
template <typename K, typename V>
inline SyncMap<K, V>* newSyncMap(std::unordered_map<K, V> base) {
	auto* m = new SyncMap<K, V>();
	m->base = std::move(base);
	return m;
}
// === end slice: project ===

} // namespace tsc::dirty
