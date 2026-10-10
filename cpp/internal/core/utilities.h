// Port of selected free helpers from tsc/internal/core/core.go.
#pragma once

#include "internal/gostd/gostd.h" // TSC_UNREACHABLE
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "internal/collections/collections.h"
#include "internal/core/types.h"
#include "internal/tspath/tspath.h"

namespace tsc {
// From ast/ast.h — re-declared so this header need not include ast.h.
[[noreturn]] void tscUnreachable(const char* message);
}

namespace tsc {

// ShouldRewriteModuleSpecifier — core.go:724
inline bool shouldRewriteModuleSpecifier(std::string_view specifier,
                                         const CompilerOptions* options) {
	return tristateIsTrue(options->RewriteRelativeImportExtensions) &&
	       tspath::pathIsRelative(specifier) &&
	       !tspath::isDeclarationFileName(specifier) &&
	       tspath::hasTSFileExtension(specifier);
}

// === slice: project ===

// workGroup — core/workgroup.go. Queue a fn (may run immediately or be
// deferred); RunAndWait runs all queued fns and blocks until they complete.
// Queue must not be called after RunAndWait returns.
struct workGroup {
	virtual ~workGroup() = default;
	virtual void Queue(std::function<void()> fn) = 0;
	virtual void RunAndWait() = 0;
};

struct parallelWorkGroup final : workGroup {
	// Go runs each queued fn on a new goroutine; the runtime multiplexes
	// them over GOMAXPROCS OS threads. A std::thread per task matches the
	// semantics but not the cost model (thousands of spawned threads thrash
	// the scheduler, malloc arenas and shared mutexes), so this port runs a
	// bounded pool of workers — the same effective parallelism as Go —
	// pulling queued fns. Task granularity, concurrency guarantees and
	// RunAndWait's wait-for-all contract are unchanged.
	std::atomic<bool> done{false};
	std::mutex mu;
	std::condition_variable cv;
	std::deque<std::function<void()>> pending;
	std::vector<std::thread> threads;
	int running = 0;
	int workers = 0;

	~parallelWorkGroup() override {
		// Workers are joinable so destruction waits for every worker's
		// last use of mu/cv: after RunAndWait returns a worker's loop
		// tail can still re-lock mu (checking pending once more), which
		// would otherwise touch a destroyed mutex.
		{
			std::lock_guard<std::mutex> lock(mu);
			done.store(true);
		}
		cv.notify_all();
		for (auto& t : threads) {
			if (t.joinable()) {
				t.join();
			}
		}
	}

	void Queue(std::function<void()> fn) override {
		if (done.load()) {
			TSC_UNREACHABLE("Queue called after RunAndWait returned");
		}
		bool runInline = false;
		{
			std::lock_guard<std::mutex> lock(mu);
			pending.push_back(std::move(fn));
			if (workers < maxWorkers()) {
				workers++;
				runInline = !spawnWorker();
			}
		}
		cv.notify_one();
		if (runInline) {
			workerLoop();
		}
	}

	void RunAndWait() override {
		{
			std::unique_lock<std::mutex> lock(mu);
			cv.wait(lock, [&] { return pending.empty() && running == 0; });
		}
		done.store(true);
	}

private:
	static int maxWorkers() {
		// Go's effective parallelism bound is GOMAXPROCS, which defaults
		// to the number of CPUs.
		int n = static_cast<int>(std::thread::hardware_concurrency());
		return n > 0 ? n : 1;
	}

	// spawnWorker — caller must hold mu; returns false when thread
	// creation fails (caller then drains the queue inline, unlocked).
	bool spawnWorker() {
		try {
			threads.emplace_back([this] { workerLoop(); });
			return true;
		} catch (const std::system_error&) {
			// Thread creation failed under resource pressure (e.g.
			// EAGAIN on memory-starved hosts). Go goroutines cannot
			// fail to spawn; the caller runs the work inline instead
			// of letting the exception terminate the process. The
			// workers slot stays counted — workerLoop's own exit
			// path decrements it.
			return false;
		}
	}

	void workerLoop() {
		for (;;) {
			std::function<void()> fn;
			{
				std::lock_guard<std::mutex> lock(mu);
				if (pending.empty()) {
					workers--;
					if (workers == 0) {
						cv.notify_all();
					}
					return;
				}
				fn = std::move(pending.front());
				pending.pop_front();
				running++;
			}
			fn();
			{
				// notify under mu so the destructor's lock
				// acquisition is a happens-after edge for the
				// worker's last use of cv/running.
				std::lock_guard<std::mutex> lock(mu);
				running--;
				if (pending.empty() && running == 0) {
					cv.notify_all();
				}
			}
		}
	}
};

struct singleThreadedWorkGroup final : workGroup {
	std::atomic<bool> done{false};
	std::mutex fnsMu;
	std::vector<std::function<void()>> fns;

	void Queue(std::function<void()> fn) override {
		if (done.load()) {
			TSC_UNREACHABLE("Queue called after RunAndWait returned");
		}
		std::lock_guard<std::mutex> lock(fnsMu);
		fns.push_back(std::move(fn));
	}

	void RunAndWait() override {
		for (;;) {
			std::function<void()> fn;
			{
				std::lock_guard<std::mutex> lock(fnsMu);
				if (fns.empty()) {
					break;
				}
				fn = std::move(fns.front());
				fns.erase(fns.begin());
			}
			fn();
		}
		done.store(true);
	}
};

// newWorkGroup — core.NewWorkGroup.
inline workGroup* newWorkGroup(bool singleThreaded) {
	if (singleThreaded) {
		return new singleThreadedWorkGroup();
	}
	return new parallelWorkGroup();
}

// === slice: project ===

// Map — core.go:80. Nil-safe: an empty input returns an empty vector.
template <typename T, typename U, typename F>
inline std::vector<U> Map(const std::vector<T>& slice, F f);
// Map (deduced U) — for call sites that omit the explicit <U>.
template <typename T, typename F>
inline auto Map(const std::vector<T>& slice, F f)
    -> std::vector<std::invoke_result_t<F, const T&>> {
	using U = std::invoke_result_t<F, const T&>;
	std::vector<U> result;
	result.reserve(slice.size());
	for (const T& v : slice) {
		result.push_back(f(v));
	}
	return result;
}
template <typename T, typename U, typename F>
inline std::vector<U> Map(const std::vector<T>& slice, F f) {
	std::vector<U> result;
	result.reserve(slice.size());
	for (const T& v : slice) {
		result.push_back(f(v));
	}
	return result;
}

// IfElse — core.go:400. Both branches are evaluated before the call.
template <typename T>
inline T IfElse(bool b, T whenTrue, T whenFalse) {
	if (b) {
		return whenTrue;
	}
	return whenFalse;
}

// Identity — core.go:702.
template <typename T>
inline T Identity(T t) {
	return t;
}

// CopyMapInto — core.go:803. dst nil → clone src; else copy entries
// in. Works on unordered_map<K,V> (V any) and unordered_set<K>
// (Go map[K]struct{}).
template <typename K, typename V>
inline std::unordered_map<K, V>*
CopyMapInto(std::unordered_map<K, V>* dst,
            const std::unordered_map<K, V>& src) {
	if (dst == nullptr) {
		return new std::unordered_map<K, V>(src);
	}
	dst->insert(src.begin(), src.end());
	return dst;
}
template <typename K>
inline std::unordered_set<K>*
CopyMapInto(std::unordered_set<K>* dst, const std::unordered_set<K>& src) {
	if (dst == nullptr) {
		return new std::unordered_set<K>(src);
	}
	dst->insert(src.begin(), src.end());
	return dst;
}

// FirstNonZero — core.go:295. First arg that isn't the zero value.
template <typename T, typename... Rest>
inline T FirstNonZero(const T& first, const Rest&... rest) {
	if (first != T{}) {
		return first;
	}
	if constexpr (sizeof...(rest) > 0) {
		return FirstNonZero(rest...);
	} else {
		return first;
	}
}


// MapNonNil — core.go:117. Maps slice, dropping results equal to the
// zero value of U (Go `mapped != *new(U)`).
template <typename T, typename U, typename F>
inline std::vector<U> MapNonNil(const std::vector<T>& slice, F f) {
	std::vector<U> result;
	for (const T& v : slice) {
		U mapped = f(v);
		if (mapped != U{}) {
			result.push_back(std::move(mapped));
		}
	}
	return result;
}

// DiffMapsFunc — core.go:779. onAdded fires for keys only in m2;
// onRemoved for keys only in m1; onChanged when equalValues fails.
// onRemoved alone is never skipped (Go calls it unconditionally).
template <typename K, typename V1, typename V2, typename E, typename A,
          typename R, typename C>
inline void DiffMapsFunc(const std::unordered_map<K, V1>& m1,
                         const std::unordered_map<K, V2>& m2,
                         E equalValues, A onAdded, R onRemoved,
                         C onChanged) {
	for (const auto& [k, v2] : m2) {
		if (m1.find(k) == m1.end()) {
			onAdded(k, v2);
		}
	}
	for (const auto& [k, v1] : m1) {
		auto it = m2.find(k);
		if (it != m2.end()) {
			if (!equalValues(v1, it->second)) {
				onChanged(k, v1, it->second);
			}
		} else {
			onRemoved(k, v1);
		}
	}
}

// DiffMaps — core.go:771 (comparable equality).
template <typename K, typename V, typename A, typename R, typename C>
inline void DiffMaps(const std::unordered_map<K, V>& m1,
                     const std::unordered_map<K, V>& m2, A onAdded,
                     R onRemoved, C onChanged) {
	DiffMapsFunc(m1, m2, [](const V& a, const V& b) { return a == b; },
	             onAdded, onRemoved, onChanged);
}

// DiffOrderedMapsFunc — ordered_map.go:301. Iterates m2's keys in
// insertion order for adds, then m1's for removals/modifications.
template <typename K, typename V, typename E, typename A, typename R,
          typename C>
inline void DiffOrderedMapsFunc(const collections::OrderedMap<K, V>& m1,
                                const collections::OrderedMap<K, V>& m2,
                                E equalValues, A onAdded, R onRemoved,
                                C onModified) {
	for (const K& k : m2.keys) {
		if (!m1.Has(k)) {
			onAdded(k, m2.mp.at(k));
		}
	}
	for (const K& k : m1.keys) {
		const V* v2 = m2.mp.count(k) ? &m2.mp.at(k) : nullptr;
		if (v2 != nullptr) {
			if (!equalValues(m1.mp.at(k), *v2)) {
				onModified(k, m1.mp.at(k), *v2);
			}
		} else {
			onRemoved(k, m1.mp.at(k));
		}
	}
}

// DiffOrderedMaps — ordered_map.go:295 (comparable equality).
template <typename K, typename V, typename A, typename R, typename C>
inline void DiffOrderedMaps(const collections::OrderedMap<K, V>& m1,
                            const collections::OrderedMap<K, V>& m2,
                            A onAdded, R onRemoved, C onModified) {
	DiffOrderedMapsFunc(m1, m2,
	                    [](const V& a, const V& b) { return a == b; },
	                    onAdded, onRemoved, onModified);
}

// BreadthFirstSearchResult — bfs.go:11.
template <typename N>
struct BreadthFirstSearchResult {
	bool Stopped = false;
	std::vector<N> Path;
};

namespace detail {
template <typename N>
struct breadthFirstSearchJob {
	N node;
	breadthFirstSearchJob<N>* parent = nullptr;
};
} // namespace detail

// BreadthFirstSearchLevel — bfs.go:21. Read/delete view over the
// level's job map handed to PreprocessLevel.
template <typename K, typename N>
struct BreadthFirstSearchLevel {
	collections::OrderedMap<K, detail::breadthFirstSearchJob<N>*>& jobs;

	bool Has(const K& key) { return jobs.Has(key); }
	void Delete(const K& key) { jobs.Delete(key); }
	// Range — ordered_map.go Values() order; fn returning false stops.
	// Go iterates the LIVE keys slice by index: mid-iteration Delete
	// shifts later elements into already-visited slots (skipped) and
	// appended elements get visited. Iterate by index — a stale element
	// reference across erase() is UB.
	void Range(const std::function<bool(const N&)>& f) {
		for (size_t i = 0; i < jobs.keys.size(); i++) {
			if (!f(jobs.mp.at(jobs.keys[i])->node)) {
				return;
			}
		}
	}
};

// BreadthFirstSearchOptions — bfs.go:36.
template <typename K, typename N>
struct BreadthFirstSearchOptions {
	// Visited — pre-seeded keys; nullptr builds a fresh set.
	collections::SyncSet<K>* Visited = nullptr;
	// PreprocessLevel — called before each level's parallel pass.
	std::function<void(BreadthFirstSearchLevel<K, N>*)> PreprocessLevel;
};

namespace detail {
// updateMin — bfs.go:223. CAS low-water mark.
inline void updateMin(std::atomic<int64_t>& a, int64_t candidate) {
	for (;;) {
		int64_t current = a.load();
		if (current < candidate) {
			return;
		}
		if (a.compare_exchange_weak(current, candidate)) {
			return;
		}
	}
}
} // namespace detail

// BreadthFirstSearchParallelEx — bfs.go:61. Level-synchronous BFS:
// each level's jobs run on their own threads (Go spawns a goroutine
// per job); the first satisfying visit's path back to start is the
// result, or the lowest-index fallback path when nothing stopped.
template <typename K, typename N, typename NF, typename VF, typename GK>
inline BreadthFirstSearchResult<N> BreadthFirstSearchParallelEx(
    N start, NF neighbors, VF visit,
    BreadthFirstSearchOptions<K, N> options, GK getKey) {
	collections::SyncSet<K> ownedVisited;
	collections::SyncSet<K>* visited = options.Visited;
	if (visited == nullptr) {
		visited = &ownedVisited;
	}
	const int64_t kMaxInt64 = std::numeric_limits<int64_t>::max();

	using Job = detail::breadthFirstSearchJob<N>;
	// Jobs are heap-allocated (Go: GC heap; parent links survive
	// across levels). The level workers run newJob in parallel, so
	// a shared container here would need locking — bare new matches
	// Go semantics exactly (objects are never deleted anyway).
	auto newJob = [&](const N& node, Job* parent) -> Job* {
		Job* j = new Job();
		j->node = node;
		j->parent = parent;
		return j;
	};

	struct LevelResult {
		bool stop = false;
		Job* job = nullptr;
		collections::OrderedMap<K, Job*> next;
	};

	std::atomic<Job*> fallback{nullptr};

	auto processLevel = [&](int64_t index,
	                        collections::OrderedMap<K, Job*>& jobs)
	    -> LevelResult {
		std::atomic<int64_t> lowestFallback{kMaxInt64};
		std::atomic<int64_t> lowestGoal{kMaxInt64};
		std::atomic<int64_t> nextJobCount{0};
		if (options.PreprocessLevel) {
			BreadthFirstSearchLevel<K, N> lvl{jobs};
			options.PreprocessLevel(&lvl);
		}
		size_t n = jobs.Size();
		std::vector<std::vector<Job*>> next(n);
		std::vector<std::thread> threads;
		threads.reserve(n);
		for (size_t i = 0; i < n; i++) {
			const K& k = jobs.keys[i];
			Job* j = jobs.mp.at(k);
			threads.emplace_back([&, i, j] {
				if (static_cast<int64_t>(i) >= lowestGoal.load()) {
					return; // A lower result already won this level.
				}
				// Already visited at a previous level (visit returned
				// false there, so no result indices need updating).
				if (!visited->AddIfAbsent(getKey(j->node))) {
					return;
				}
				auto visitResult = visit(j->node);
				bool isResult = visitResult.first;
				bool stop = visitResult.second;
				if (isResult) {
					if (stop) {
						detail::updateMin(lowestGoal,
						                  static_cast<int64_t>(i));
						return;
					}
					if (fallback.load() == nullptr) {
						detail::updateMin(lowestFallback,
						                  static_cast<int64_t>(i));
					}
				}
				if (static_cast<int64_t>(i) >= lowestGoal.load()) {
					return;
				}
				std::vector<N> neighborNodes = neighbors(j->node);
				if (!neighborNodes.empty()) {
					nextJobCount.fetch_add(
					    static_cast<int64_t>(neighborNodes.size()));
					next[i] = Map(neighborNodes, [&](const N& child) {
						return newJob(child, j);
					});
				}
			});
		}
		for (auto& t : threads) {
			t.join();
		}
		LevelResult result;
		if (lowestGoal.load() != kMaxInt64) {
			result.stop = true;
			result.job = jobs.mp.at(jobs.keys[lowestGoal.load()]);
			return result;
		}
		if (fallback.load() == nullptr &&
		    lowestFallback.load() != kMaxInt64) {
			fallback.store(
			    jobs.mp.at(jobs.keys[lowestFallback.load()]));
		}
		result.next = collections::OrderedMap<K, Job*>(
		    static_cast<size_t>(nextJobCount.load()));
		for (auto& jobsVec : next) {
			for (Job* j : jobsVec) {
				if (!result.next.Has(getKey(j->node))) {
					result.next.Set(getKey(j->node), j);
				}
			}
		}
		return result;
	};

	auto createPath = [&](Job* job) {
		std::vector<N> path;
		while (job != nullptr) {
			path.push_back(job->node);
			job = job->parent;
		}
		return path;
	};

	collections::OrderedMap<K, Job*> level;
	level.Set(getKey(start), newJob(start, nullptr));
	int64_t levelIndex = 0;
	while (level.Size() > 0) {
		LevelResult result = processLevel(levelIndex, level);
		if (result.stop) {
			return BreadthFirstSearchResult<N>{true,
			                                   createPath(result.job)};
		} else if (result.job != nullptr &&
		           fallback.load() == nullptr) {
			fallback.store(result.job);
		}
		level = std::move(result.next);
		levelIndex++;
	}
	return BreadthFirstSearchResult<N>{false,
	                                   createPath(fallback.load())};
}

// BreadthFirstSearchParallel — bfs.go:47.
template <typename N, typename NF, typename VF>
inline BreadthFirstSearchResult<N> BreadthFirstSearchParallel(
    N start, NF neighbors, VF visit) {
	return BreadthFirstSearchParallelEx<N, N>(
	    std::move(start), neighbors, visit,
	    BreadthFirstSearchOptions<N, N>{},
	    [](const N& n) { return n; });
}

// Version — version.go:10. Overridable var in Go; fixed string here.
inline const std::string& Version() {
	static const std::string v = "7.1.0-dev";
	return v;
}

// === end slice: project ===

} // namespace tsc
