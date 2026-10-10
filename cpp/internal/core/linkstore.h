// Port of tsc/internal/core/linkstore.go.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "internal/core/arena.h"

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

inline constexpr int LinkPageShift = 8;
inline constexpr int LinkPageSize = 1 << LinkPageShift;
inline constexpr uint64_t LinkPageMask = LinkPageSize - 1;

// PagedLinkStore implements a sparse-array-like structure for storing elements
// keyed by dense uint64 keys. Elements are allocated in an arena, element
// references are stored in fixed-size pages of 256 entries, and an index of
// pages is maintained in a growable list.
template <class V>
struct PagedLinkStore {
	using Page = std::array<V*, LinkPageSize>;

	std::vector<Page*> pages;
	Arena arena;
	// Pages are heap-allocated and live as long as the store (same lifetime
	// model as Go's GC-backed store).

	V* Get(uint64_t key) {
		uint64_t pageIndex = key >> LinkPageShift;
		if (pageIndex >= pages.size()) {
			// Grow the length of the list to pageIndex+1
			pages.resize(pageIndex + 1, nullptr);
		}
		Page* page = pages[pageIndex];
		if (page == nullptr) {
			page = new Page(); // value-init zeroes every slot
			pages[pageIndex] = page;
		}
		V* link = (*page)[key & LinkPageMask];
		if (link == nullptr) {
			link = arena.alloc<V>();
			(*page)[key & LinkPageMask] = link;
		}
		return link;
	}

	bool Has(uint64_t key) const { return TryGet(key) != nullptr; }

	V* TryGet(uint64_t key) const {
		uint64_t pageIndex = key >> LinkPageShift;
		if (pageIndex < pages.size()) {
			if (Page* page = pages[pageIndex]; page != nullptr) {
				return (*page)[key & LinkPageMask];
			}
		}
		return nullptr;
	}
};

} // namespace tsc
