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
#include <unordered_map>
#include <unordered_set>
#include <utility>

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
struct MapEntry : mapEntry<K, V>,
                  std::enable_shared_from_this<MapEntry<K, V>> {
	Map<K, V>* m = nullptr;

	// Change — clones the value on first change (copy-on-write), then runs
	// `apply` on the mutable clone.
	void Change(const std::function<void(V&)>& apply) {
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
	              const std::function<void(V&)>& apply) {
		if (cond(this->Value())) {
			Change(apply);
			return true;
		}
		return false;
	}

	void Delete() {
		if (!this->dirty) {
			m->dirty[this->key] = this->shared_from_this();
		}
		this->delete_ = true;
	}

	// Locked — interfaces.go Value[T].Locked.
	void Locked(const std::function<void(MapEntry*)>& fn) { fn(this); }
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
struct Box {
	T original{};
	T value{};
	bool dirty = false;
	bool delete_ = false;

	Box() = default;
	explicit Box(T original) : original(original), value(original) {}

	T Value() const {
		if (delete_) return T{};
		return value;
	}
	const T& Original() const { return original; }
	bool Dirty() const { return dirty; }

	void Set(T v) {
		value = std::move(v);
		delete_ = false;
		dirty = true;
	}

	void Change(const std::function<void(T&)>& apply) {
		if (!dirty) {
			value = cloneValue(value);
			dirty = true;
		}
		apply(value);
	}

	bool ChangeIf(const std::function<bool(const T&)>& cond,
	              const std::function<void(T&)>& apply) {
		if (cond(value)) {
			Change(apply);
			return true;
		}
		return false;
	}

	void Delete() { delete_ = true; }

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

} // namespace tsc::dirty
