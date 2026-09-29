// Bump-pointer arena. Mirrors the role of tsc/internal/core/arena.go
// (per-factory per-type arenas), collapsed into one byte arena shared by all
// node types: allocations remain O(1) and adjacent in allocation order.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string_view>
#include <type_traits>
#include <vector>

namespace tsc {

class Arena {
public:
	explicit Arena(size_t blockSize = 1 << 20) : blockSize_(blockSize) {}
	Arena(const Arena&) = delete;
	Arena& operator=(const Arena&) = delete;

	template <class T, class... Args>
	T* alloc(Args&&... args) {
		static_assert(std::is_trivially_destructible_v<T> || true,
		              "arena objects are never destructed");
		void* p = raw(alignof(T) > alignof(std::max_align_t) ? alignof(T)
		                                                     : alignof(std::max_align_t),
		            sizeof(T));
		return new (p) T(std::forward<Args>(args)...);
	}

	// Allocate `n` contiguous elements (no construction beyond trivial default).
	template <class T>
	T* allocSlice(size_t n) {
		if (n == 0)
			return nullptr;
		return static_cast<T*>(raw(alignof(T), sizeof(T) * n));
	}

	char* dupBytes(std::string_view s) {
		char* p = static_cast<char*>(raw(1, s.size() + 1));
		std::memcpy(p, s.data(), s.size());
		p[s.size()] = '\0';
		return p;
	}

	std::string_view dupString(std::string_view s) {
		char* p = static_cast<char*>(raw(1, s.size()));
		std::memcpy(p, s.data(), s.size());
		return {p, s.size()};
	}

	void clear() {
		blocks_.clear();
		offset_ = 0;
	}

private:
	void* raw(size_t align, size_t size) {
		offset_ = (offset_ + align - 1) & ~(align - 1);
		if (offset_ + size > capacity_) {
			size_t n = blockSize_ > size + align ? blockSize_ : size + align;
			blocks_.push_back(std::make_unique<char[]>(n));
			capacity_ = n;
			cur_ = blocks_.back().get();
			offset_ = 0;
		}
		void* p = cur_ + offset_;
		offset_ += size;
		return p;
	}

	size_t blockSize_;
	size_t capacity_ = 0;
	size_t offset_ = 0;
	char* cur_ = nullptr;
	std::vector<std::unique_ptr<char[]>> blocks_;
};

}  // namespace tsc
