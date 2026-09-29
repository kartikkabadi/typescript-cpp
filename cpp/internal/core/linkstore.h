// Port of tsc/internal/core/linkstore.go.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace tsc {

// LinkStore: map-like store of lazily-created values keyed by K.
template <class K, class V>
struct LinkStore {
	std::unordered_map<K, V*> entries;
	Arena* arena{};

	explicit LinkStore(Arena* a = nullptr) : arena(a) {}

	V* Get(K key) {
		auto it = entries.find(key);
		if (it != entries.end()) {
			return it->second;
		}
		V* value = arena->alloc<V>();
		entries.emplace(key, value);
		return value;
	}

	bool Has(K key) const { return entries.find(key) != entries.end(); }

	V* TryGet(K key) const {
		auto it = entries.find(key);
		return it != entries.end() ? it->second : nullptr;
	}
};

// PagedLinkStore: sparse-array-like structure for storing elements keyed by dense
// uint64 keys. Elements are stored in fixed-size pages of 256 entries; page indices
// below maxPageCount go into a direct-indexed page list, higher ones into a map.
template <class V>
struct PagedLinkStore {
	static constexpr int pageShift = 8;
	static constexpr int pageSize = 1 << pageShift;
	static constexpr uint64_t pageMask = pageSize - 1;
	static constexpr uint64_t maxPageCount = 65536;

	using Page = std::array<V, pageSize>;

	std::unordered_map<uint64_t, Page*> pageMap;
	std::vector<Page*> pageList;
	// Pages are heap-allocated and live as long as the checker (same lifetime
	// model as Go's arena-backed store).

	V* Get(uint64_t key) {
		uint64_t pageIndex = key >> pageShift;
		Page* page;
		if (pageIndex < maxPageCount) {
			if (pageIndex >= pageList.size()) {
				pageList.resize(pageIndex + 1, nullptr);
			}
			page = pageList[pageIndex];
			if (!page) {
				page = new Page();
				pageList[pageIndex] = page;
			}
		} else {
			auto it = pageMap.find(pageIndex);
			if (it == pageMap.end()) {
				page = new Page();
				pageMap.emplace(pageIndex, page);
			} else {
				page = it->second;
			}
		}
		return &(*page)[key & pageMask];
	}

	V* TryGet(uint64_t key) const {
		uint64_t pageIndex = key >> pageShift;
		Page* page = nullptr;
		if (pageIndex < maxPageCount) {
			if (pageIndex < pageList.size()) {
				page = pageList[pageIndex];
			}
		} else {
			auto it = pageMap.find(pageIndex);
			if (it != pageMap.end()) {
				page = it->second;
			}
		}
		if (!page) {
			return nullptr;
		}
		return &(*page)[key & pageMask];
	}

	bool Has(uint64_t key) const { return TryGet(key) != nullptr; }
};

} // namespace tsc
