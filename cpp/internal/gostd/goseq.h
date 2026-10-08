// goseq — Go's iter.Seq[T] = func(yield func(T) bool) sequence helpers.
#pragma once

#include <algorithm>
#include <functional>
#include <vector>

namespace tsc::goseq {

template <typename T>
using Seq = std::function<void(const std::function<bool(T)>&)>;

// Collect iterates a Seq into a vector (slices.Collect).
template <typename T>
std::vector<T> collect(const Seq<T>& seq) {
	std::vector<T> out;
	if (seq) {
		seq([&](T v) {
			out.push_back(std::move(v));
			return true;
		});
	}
	return out;
}

// Sorted collects and sorts (slices.Sorted).
template <typename T>
std::vector<T> sorted(const Seq<T>& seq) {
	auto out = collect(seq);
	std::sort(out.begin(), out.end());
	return out;
}

// keysSeq adapts an unordered_map's key set to a Seq (maps.Keys).
template <typename Map>
Seq<typename Map::key_type> keysSeq(const Map& m) {
	return [&m](const std::function<bool(typename Map::key_type)>& yield) {
		for (const auto& kv : m) {
			if (!yield(kv.first)) return;
		}
	};
}

} // namespace tsc::goseq
