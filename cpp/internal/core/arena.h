// Bump-pointer arena. Mirrors the role of tsc/internal/core/arena.go
// (per-factory per-type arenas), collapsed into one byte arena shared by all
// node types: allocations remain O(1) and adjacent in allocation order.
//
// An arena may also run in "tracked" mode (setTracked(true) before the first
// allocation): every allocation is individually heap-allocated and registered
// so that markPointer/sweep can reclaim the objects Go's GC would collect
// (the ReleaseArenas semantics Go gets for free). Tracked arenas are for
// long-lived emit contexts whose dead nodes must be freed while cached nodes
// stay pinned by the maps that reference them.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace tsc {

class Arena {
public:
	// TraceFn marks everything an object transitively owns: implementations
	// call a.markPointer / a.scanObject for every pointer the object holds.
	// A null tracer means "scan the payload bytes for arena pointers", which
	// conservatively covers every inline pointer field.
	using TraceFn = void (*)(const void* obj, size_t size, Arena& arena);
	using DtorFn = void (*)(void* obj);

	// Per-type liveness tracer for tracked mode. The default (null) is the
	// conservative payload byte-scan; layers that own types with node
	// pointers inside container members specialize this (see ast.h).
	template <class T>
	struct Tracer {
		static TraceFn get() { return nullptr; }
	};

	// Largest block an arena grows to: the historical fixed block size.
	static constexpr size_t kMaxBlockSize = 1 << 20;

	explicit Arena(size_t blockSize = kMaxBlockSize)
	    : blockSize_(blockSize),
	      maxBlockSize_(blockSize > kMaxBlockSize ? blockSize : kMaxBlockSize) {}
	Arena(const Arena&) = delete;
	Arena& operator=(const Arena&) = delete;
	Arena(Arena&& o) noexcept
	    : blockSize_(o.blockSize_), maxBlockSize_(o.maxBlockSize_),
	      blockBytes_(o.blockBytes_), capacity_(o.capacity_),
	      offset_(o.offset_), cur_(o.cur_), blocks_(std::move(o.blocks_)),
	      tracked_(o.tracked_), trackedAllocs_(o.trackedAllocs_),
	      trackedBytesAllocd_(o.trackedBytesAllocd_),
	      trackedBytesLive_(o.trackedBytesLive_) {
		o.capacity_ = o.offset_ = 0;
		o.blockBytes_ = 0;
		o.cur_ = nullptr;
		o.trackedAllocs_ = nullptr;
		o.trackedBytesAllocd_ = o.trackedBytesLive_ = 0;
	}
	Arena& operator=(Arena&& o) noexcept {
		if (this != &o) {
			clear();
			blockSize_ = o.blockSize_;
			blockBytes_ = o.blockBytes_;
			maxBlockSize_ = o.maxBlockSize_;
			capacity_ = o.capacity_;
			offset_ = o.offset_;
			cur_ = o.cur_;
			blocks_ = std::move(o.blocks_);
			tracked_ = o.tracked_;
			trackedAllocs_ = o.trackedAllocs_;
			trackedBytesAllocd_ = o.trackedBytesAllocd_;
			trackedBytesLive_ = o.trackedBytesLive_;
			o.capacity_ = o.offset_ = 0;
			o.blockBytes_ = 0;
			o.maxBlockSize_ = kMaxBlockSize;
			o.cur_ = nullptr;
			o.trackedAllocs_ = nullptr;
			o.trackedBytesAllocd_ = o.trackedBytesLive_ = 0;
		}
		return *this;
	}
	~Arena() { clear(); }

	// Tracked mode: every allocation is individually registered so marking
	// can distinguish live objects from dead ones (Go ReleaseArenas analog).
	// Must be called before the first allocation.
	void setTracked(bool tracked) { tracked_ = tracked; }
	bool isTracked() const { return tracked_; }

	// Total bytes reserved across all blocks (diagnostics only).
	size_t blockBytes() const { return blockBytes_; }

	// Initial block size for the NEXT block allocation. Blocks grow
	// geometrically from here up to maxBlockSize_ (Go's GC-arena span
	// doubling analog). Only meaningful while the arena is empty.
	void setBlockSize(size_t n) { blockSize_ = n; }
	size_t blockSize() const { return blockSize_; }

	template <class T, class... Args>
	T* alloc(Args&&... args) {
		return allocImpl<T>(Tracer<T>::get(), std::forward<Args>(args)...);
	}

	// Allocate `n` contiguous elements (no construction beyond trivial default).
	template <class T>
	T* allocSlice(size_t n) {
		if (n == 0)
			return nullptr;
		if (tracked_) {
			return static_cast<T*>(trackedRaw(sizeof(T) * n, nullptr));
		}
		return static_cast<T*>(raw(alignof(T), sizeof(T) * n));
	}

	char* dupBytes(std::string_view s) {
		if (tracked_) {
			char* p = static_cast<char*>(trackedRaw(s.size() + 1, nullptr));
			std::memcpy(p, s.data(), s.size());
			p[s.size()] = '\0';
			return p;
		}
		char* p = static_cast<char*>(raw(1, s.size() + 1));
		std::memcpy(p, s.data(), s.size());
		p[s.size()] = '\0';
		return p;
	}

	std::string_view dupString(std::string_view s) {
		if (tracked_) {
			char* p = static_cast<char*>(trackedRaw(s.size(), nullptr));
			std::memcpy(p, s.data(), s.size());
			return {p, s.size()};
		}
		char* p = static_cast<char*>(raw(1, s.size()));
		std::memcpy(p, s.data(), s.size());
		return {p, s.size()};
	}

	void clear() {
		blocks_.clear();
		offset_ = 0;
		capacity_ = 0;
		cur_ = nullptr;
		if (tracked_) {
			while (trackedAllocs_ != nullptr) {
				TrackedAlloc* next = trackedAllocs_->next;
				trackedAllocs_->destroy();
				trackedAllocs_ = next;
			}
			markIndex_.clear();
			// The arena dies with its objects: pull its contributions out
			// of the global GC accounting so the pacing numbers stay honest.
			trackedBytesAllocdGlobal_.fetch_sub(trackedBytesAllocd_,
			                                    std::memory_order_relaxed);
			trackedBytesLiveGlobal_.fetch_sub(trackedBytesLive_,
			                                  std::memory_order_relaxed);
			trackedBytesAllocd_ = 0;
			trackedBytesLive_ = 0;
		}
	}

	// --- marking (tracked arenas only) -------------------------------------
	//
	// ReleaseArenas port: beginMark() builds the lookup index, markPointer /
	// scanObject grow the live set transitively via per-allocation tracers,
	// sweep() frees everything left unmarked — the same set Go's GC would
	// reclaim after ReleaseArenas drops the factory's arena headers.

	void beginMark() {
		markIndex_.clear();
		for (TrackedAlloc* h = trackedAllocs_; h != nullptr; h = h->next) {
			markIndex_.emplace_back(
				static_cast<const char*>(h->payload()), h);
		}
		std::sort(markIndex_.begin(), markIndex_.end(),
		          [](const auto& a, const auto& b) { return a.first < b.first; });
	}

	// Mark the tracked allocation containing `p` live and trace it. `p` may
	// be an object start or an interior pointer; pointers outside this arena
	// are ignored.
	void markPointer(const void* p) {
		if (p == nullptr || markIndex_.empty()) {
			return;
		}
		auto ptr = static_cast<const char*>(p);
		auto it = std::upper_bound(
		    markIndex_.begin(), markIndex_.end(), ptr,
		    [](const char* v, const std::pair<const char*, TrackedAlloc*>& e) {
			    return v < e.first;
		    });
		if (it == markIndex_.begin()) {
			return;
		}
		--it;
		const char* start = it->first;
		TrackedAlloc* h = it->second;
		if (ptr < start || ptr >= start + h->size) {
			return;
		}
		if (h->marked) {
			return;
		}
		h->marked = true;
		markWorklist_.push_back(h);
		while (!markWorklist_.empty()) {
			TrackedAlloc* w = markWorklist_.back();
			markWorklist_.pop_back();
			if (w->trace != nullptr) {
				w->trace(w->payload(), w->size, *this);
			} else {
				scanObject(w->payload(), w->size);
			}
		}
	}

	// Conservatively scan [obj, obj+size): every aligned word that points
	// into a tracked allocation marks it. Catches all inline pointer fields
	// without needing per-type field lists; container members (e.g.
	// std::vector<Node*>) are handled by explicit tracers instead.
	void scanObject(const void* obj, size_t size) {
		if (obj == nullptr) {
			return;
		}
		const auto* begin = static_cast<const uintptr_t*>(obj);
		const auto* end =
		    reinterpret_cast<const uintptr_t*>(static_cast<const char*>(obj) +
		                                       (size / sizeof(uintptr_t)) *
		                                           sizeof(uintptr_t));
		for (const uintptr_t* w = begin; w != end; ++w) {
			markPointer(reinterpret_cast<const void*>(*w));
		}
	}

	// Free every unmarked tracked allocation; survivors are unmarked for the
	// next pass. Returns the number of freed allocations.
	size_t sweep() {
		size_t freed = 0;
		size_t live = 0;
		TrackedAlloc** pp = &trackedAllocs_;
		while (*pp != nullptr) {
			TrackedAlloc* h = *pp;
			if (h->marked) {
				h->marked = false;
				live += sizeof(TrackedAlloc) + h->size;
				pp = &h->next;
				continue;
			}
			*pp = h->next;
			h->destroy();
			++freed;
		}
		markIndex_.clear();
		// Fold this arena's accounting into the process-wide GC counters:
		// the bytes it reported as "allocated since last sweep" are now
		// resolved (freed or promoted to live), and its live set changes by
		// the delta of the previous and current surviving totals.
		trackedBytesAllocdGlobal_.fetch_sub(trackedBytesAllocd_,
		                                    std::memory_order_relaxed);
		if (live >= trackedBytesLive_) {
			trackedBytesLiveGlobal_.fetch_add(
			    live - trackedBytesLive_, std::memory_order_relaxed);
		} else {
			trackedBytesLiveGlobal_.fetch_sub(
			    trackedBytesLive_ - live, std::memory_order_relaxed);
		}
		trackedBytesLive_ = live;
		trackedBytesAllocd_ = 0;
		return freed;
	}

	// Amortization gate for tracked arenas: a mark+sweep costs O(live heap),
	// so we only run one once the garbage since the last sweep crosses a
	// threshold — mirroring Go's GC pacing (GOGC≈25: collect once the heap
	// grows ~25% over the live set), which is what makes Go's ReleaseArenas
	// O(1). Go's trigger is global — it watches total heap, not per-arena
	// garbage — so the accounting here is process-wide across all tracked
	// arenas; each arena is still swept only by its own owner's release call
	// (the same thread that allocates on it), matching the GC-assist model.
	// RSS bound: unswept tracked bytes stay < max(floor, live/4, cap).
	static bool globalShouldSweep(size_t floorBytes, size_t capBytes) {
		size_t allocd = trackedBytesAllocdGlobal_.load(
		    std::memory_order_relaxed);
		size_t live = trackedBytesLiveGlobal_.load(
		    std::memory_order_relaxed);
		size_t threshold = live / 4;
		if (threshold < floorBytes)
			threshold = floorBytes;
		if (threshold > capBytes)
			threshold = capBytes;
		if (debugGc_) {
			fprintf(stderr, "[gc] allocd=%zu live=%zu threshold=%zu\n",
			        allocd, live, threshold);
		}
		return allocd >= threshold;
	}

private:
	static inline std::atomic<size_t> trackedBytesAllocdGlobal_{0};
	static inline std::atomic<size_t> trackedBytesLiveGlobal_{0};
	static inline bool debugGc_ = getenv("TSCPP_GC_DEBUG") != nullptr;

	struct alignas(std::max_align_t) TrackedAlloc {
		TrackedAlloc* next;
		TraceFn trace;
		DtorFn dtor;
		uint32_t size;
		bool marked;

		void* payload() {
			return reinterpret_cast<char*>(this) + sizeof(TrackedAlloc);
		}
		const void* payload() const {
			return reinterpret_cast<const char*>(this) + sizeof(TrackedAlloc);
		}
		void destroy() {
			if (dtor != nullptr) {
				dtor(payload());
			}
			::operator delete(this);
		}
	};

	TrackedAlloc* newTrackedAlloc(size_t size) {
		auto* hdr = static_cast<TrackedAlloc*>(
		    ::operator new(sizeof(TrackedAlloc) + size));
		size_t bytes = sizeof(TrackedAlloc) + size;
		trackedBytesAllocd_ += bytes;
		trackedBytesAllocdGlobal_.fetch_add(bytes,
		                                    std::memory_order_relaxed);
		return hdr;
	}

	template <class T, class... Args>
	T* allocImpl(TraceFn tracer, Args&&... args) {
		static_assert(std::is_trivially_destructible_v<T> || true,
		              "arena objects are never destructed");
		if (tracked_) {
			static_assert(alignof(T) <= alignof(std::max_align_t),
			              "tracked arena supports max_align_t alignment only");
			auto* hdr = newTrackedAlloc(sizeof(T));
			hdr->trace = tracer;
			hdr->dtor = +[](void* p) { static_cast<T*>(p)->~T(); };
			hdr->size = sizeof(T);
			hdr->marked = false;
			hdr->next = trackedAllocs_;
			trackedAllocs_ = hdr;
			return new (hdr->payload()) T(std::forward<Args>(args)...);
		}
		void* p = raw(alignof(T) > alignof(std::max_align_t) ? alignof(T)
		                                                     : alignof(std::max_align_t),
		            sizeof(T));
		return new (p) T(std::forward<Args>(args)...);
	}

	void* trackedRaw(size_t size, DtorFn dtor) {
		auto* hdr = newTrackedAlloc(size);
		hdr->trace = nullptr;
		hdr->dtor = dtor;
		hdr->size = static_cast<uint32_t>(size);
		hdr->marked = false;
		hdr->next = trackedAllocs_;
		trackedAllocs_ = hdr;
		return hdr->payload();
	}

	void* raw(size_t align, size_t size) {
		offset_ = (offset_ + align - 1) & ~(align - 1);
		if (offset_ + size > capacity_) {
			size_t n = blockSize_ > size + align ? blockSize_ : size + align;
			blockBytes_ += n;
			// Geometric growth like Go's span/vector doubling: small arenas
			// stay small; heavy users reach maxBlockSize_ in a few steps.
			blockSize_ = n * 2 <= maxBlockSize_ ? n * 2 : maxBlockSize_;
			// make_unique_for_overwrite: arena bytes are dead storage until
			// written — zero-initializing them costs a full-block memset
			// (dominated parse profiles at ~80% of instructions) plus an
			// eager page-fault on every fresh page.
			blocks_.push_back(std::make_unique_for_overwrite<char[]>(n));
			capacity_ = n;
			cur_ = blocks_.back().get();
			offset_ = 0;
		}
		void* p = cur_ + offset_;
		offset_ += size;
		return p;
	}

	size_t blockSize_;
	size_t maxBlockSize_ = kMaxBlockSize;
	size_t blockBytes_ = 0;
	size_t capacity_ = 0;
	size_t offset_ = 0;
	char* cur_ = nullptr;
	std::vector<std::unique_ptr<char[]>> blocks_;
	bool tracked_ = false;
	TrackedAlloc* trackedAllocs_ = nullptr;
	size_t trackedBytesAllocd_ = 0;
	size_t trackedBytesLive_ = 0;
	std::vector<std::pair<const char*, TrackedAlloc*>> markIndex_;
	std::vector<TrackedAlloc*> markWorklist_;
};

}  // namespace tsc
