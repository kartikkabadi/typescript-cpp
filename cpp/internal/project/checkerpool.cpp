// checkerpool.go — faithful port.
//
// Semaphores: Go's buffered `chan struct{}` — send claims a slot
// (blocks when all are in use), receive frees one. semaphore::acquire
// waits for a token; release hands one back.
//
// sync.OnceFunc: every returned release funnels through a shared
// atomic flag so only the first call releases.

#include "internal/project/checkerpool.h"

#include <algorithm>

#include "internal/debug/debug.h"

namespace tsc::project {

void checkerPool::semaphore::acquire() {
	std::unique_lock<std::mutex> lock(mu);
	cv.wait(lock, [&] { return tokens > 0; });
	tokens--;
}

void checkerPool::semaphore::release() {
	{
		std::lock_guard<std::mutex> lock(mu);
		tokens++;
	}
	cv.notify_one();
}

// newCheckerPool — checkerpool.go:84.
checkerPool* newCheckerPool(const CheckerPoolOptions& optsIn,
                            compiler::SimpleProgram* program,
                            std::function<void(std::string)> log) {
	CheckerPoolOptions opts = optsIn;
	if (opts.MaxCheckers <= 0) {
		opts.MaxCheckers = 4;
	} else if (opts.MaxCheckers < 2) {
		opts.MaxCheckers =
		    2; // at least 1 diagnostics + 1 query checker
	}
	if (opts.IdleTimeout <= std::chrono::nanoseconds{0}) {
		opts.IdleTimeout = std::chrono::seconds(30);
	}
	int querySlots = opts.MaxCheckers - 1;
	auto* pool = new checkerPool();
	pool->program = program;
	pool->opts = opts;
	pool->checkers.resize(opts.MaxCheckers);
	pool->heldBy.resize(opts.MaxCheckers);
	pool->lastReleased.resize(opts.MaxCheckers);
	pool->diagSem.tokens = 1;
	pool->querySem.tokens = querySlots;
	pool->persistentSem.tokens = 1;
	pool->log = std::move(log);
	pool->globalDiagCheckerCount.resize(opts.MaxCheckers);
	if (pool->log == nullptr) {
		pool->log = [](const std::string&) {};
	}
	return pool;
}

// holdTag — checkerpool.go:115. The value stored in heldBy for the
// given request ID.
static const std::string& holdTag(const std::string& requestID) {
	if (requestID.empty()) {
		return checkerHeldAnonymous;
	}
	return requestID;
}

// GetChecker — checkerpool.go:123.
std::pair<checker::Checker*, std::function<void()>>
checkerPool::GetChecker(const gostd::Context& ctx,
                        SourceFile* file) {
	core::CheckerLifetime lifetime = core::GetCheckerLifetime(ctx);
	std::string requestID = core::GetRequestID(ctx);

	// Request affinity is cleaned up via context.AfterFunc when the
	// request context is done. If the context can never be canceled
	// (ctx.Done() == nil, e.g. context.Background()), that cleanup
	// would never run and requestAssociations would grow unboundedly,
	// so disable affinity entirely.
	if (!gostd::ctxCancelable(ctx)) {
		requestID = "";
	}
	switch (lifetime) {
	case core::CheckerLifetime::Diagnostics:
		return getDiagnosticsChecker(ctx, requestID);
	case core::CheckerLifetime::API:
		return getPersistentChecker();
	default:
		return getQueryChecker(ctx, requestID, file);
	}
}

// tryReacquireForRequest — checkerpool.go:145. Claims a semaphore
// slot, then checks whether the request has an idle associated
// checker. If the association is in the wrong category (diagnostics
// index for a query request), it is deleted and normal acquisition
// proceeds.
//
// Request affinity is only a preference for an idle checker, not
// permission to reuse a held checker: concurrent acquisitions can
// share the same request ID. The caller must proceed with normal
// acquisition when this returns ok=false — in that case a slot has
// already been claimed. Must NOT be called with p.mu held.
std::tuple<checker::Checker*, std::function<void()>, bool>
checkerPool::tryReacquireForRequest(const std::string& requestID,
                                    semaphore& sem, bool isDiag) {
	sem.acquire();
	if (requestID.empty()) {
		return {nullptr, nullptr, false};
	}

	std::lock_guard<std::mutex> lock(mu);
	auto it = requestAssociations.find(requestID);
	if (it == requestAssociations.end()) {
		return {nullptr, nullptr, false};
	}
	int index = it->second;

	// Validate that the associated index matches the expected
	// category. Index 0 is diagnostics; indices 1+ are queries.
	if ((isDiag && index != 0) || (!isDiag && index == 0)) {
		requestAssociations.erase(it);
		return {nullptr, nullptr, false};
	}

	checker::Checker* c = checkers[index].get();
	if (c == nullptr) {
		requestAssociations.erase(it);
		return {nullptr, nullptr, false};
	}

	if (heldBy[index].empty()) {
		heldBy[index] = requestID;
		return {c, createRelease(requestID, index, c), true};
	}

	return {nullptr, nullptr, false};
}

// getDiagnosticsChecker — checkerpool.go:220. The dedicated
// diagnostics checker (index 0), created on first use; blocks on
// diagSem while in use.
std::pair<checker::Checker*, std::function<void()>>
checkerPool::getDiagnosticsChecker(const gostd::Context& ctx,
                                   const std::string& requestID) {
	constexpr int diagIndex = 0;

	auto reacquire = tryReacquireForRequest(requestID, diagSem, true);
	if (std::get<2>(reacquire)) {
		return {std::get<0>(reacquire), std::get<1>(reacquire)};
	}

	// Token consumed — proceed with normal acquisition.
	std::lock_guard<std::mutex> lock(mu);

	if (checkers[diagIndex] == nullptr) {
		log("checkerpool: Creating diagnostics checker");
		auto c = std::make_unique<checker::Checker>();
		c->init(program);
		checkers[diagIndex] = std::move(c);
	}

	checker::Checker* c = checkers[diagIndex].get();
	heldBy[diagIndex] = holdTag(requestID);
	log("checkerpool: Acquired diagnostics checker for request " +
	    holdTag(requestID));
	if (!requestID.empty()) {
		if (requestAssociations.find(requestID) ==
		    requestAssociations.end()) {
			requestAssociations[requestID] = diagIndex;
			registerRequestCleanup(ctx, requestID);
		}
	}
	return {c, createRelease(requestID, diagIndex, c)};
}

// getQueryChecker — checkerpool.go:252. Ephemeral query checker from
// indices 1+: request affinity, then file affinity, then
// find/create; blocks on querySem when all slots are in use.
std::pair<checker::Checker*, std::function<void()>>
checkerPool::getQueryChecker(const gostd::Context& ctx,
                             const std::string& requestID,
                             SourceFile* file) {
	auto reacquire = tryReacquireForRequest(requestID, querySem, false);
	if (std::get<2>(reacquire)) {
		return {std::get<0>(reacquire), std::get<1>(reacquire)};
	}

	// Token consumed — proceed with normal acquisition.
	std::lock_guard<std::mutex> lock(mu);

	// Try file affinity.
	if (file != nullptr) {
		auto it = fileAssociations.find(file);
		if (it != fileAssociations.end() && it->second > 0) {
			int index = it->second;
			checker::Checker* c = checkers[index].get();
			if (c != nullptr && heldBy[index].empty()) {
				heldBy[index] = holdTag(requestID);
				if (!requestID.empty()) {
					if (requestAssociations.find(requestID) ==
					    requestAssociations.end()) {
						requestAssociations[requestID] =
						    index;
						registerRequestCleanup(ctx,
						                       requestID);
					}
				}
				return {c, createRelease(requestID, index, c)};
			}
		}
	}

	// Find any available query checker or create one.
	auto created = findOrCreateQueryCheckerLocked();
	checker::Checker* c = created.first;
	int index = created.second;
	heldBy[index] = holdTag(requestID);
	log("checkerpool: Acquired query checker " +
	    std::to_string(index) + " for request " +
	    holdTag(requestID));
	if (!requestID.empty()) {
		if (requestAssociations.find(requestID) ==
		    requestAssociations.end()) {
			requestAssociations[requestID] = index;
			registerRequestCleanup(ctx, requestID);
		}
	}
	if (file != nullptr) {
		fileAssociations[file] = index;
	}
	return {c, createRelease(requestID, index, c)};
}

// findOrCreateQueryCheckerLocked — checkerpool.go:296. An idle query
// checker or one created in the first empty slot; the semaphore
// guarantees at least one slot is free. p.mu held.
std::pair<checker::Checker*, int>
checkerPool::findOrCreateQueryCheckerLocked() {
	// Prefer an existing idle checker.
	for (size_t i = 1; i < checkers.size(); i++) {
		checker::Checker* c = checkers[i].get();
		if (c != nullptr && heldBy[i].empty()) {
			return {c, static_cast<int>(i)};
		}
	}
	// Create in the first empty slot.
	for (size_t i = 1; i < checkers.size(); i++) {
		if (checkers[i] == nullptr) {
			log("checkerpool: Creating query checker " +
			    std::to_string(i));
			auto c = std::make_unique<checker::Checker>();
			c->init(program);
			checkers[i] = std::move(c);
			return {checkers[i].get(), static_cast<int>(i)};
		}
	}
	TSC_UNREACHABLE(
	    "checkerpool: no available query slot despite holding "
	    "semaphore token");
}

// getPersistentChecker — checkerpool.go:315. The API checker; never
// idle-cleaned.
std::pair<checker::Checker*, std::function<void()>>
checkerPool::getPersistentChecker() {
	persistentSem.acquire();
	mu.lock();

	if (persistentChecker == nullptr) {
		log("checkerpool: Creating persistent checker");
		auto c = std::make_unique<checker::Checker>();
		c->init(program);
		persistentChecker = std::move(c);
	}

	checker::Checker* c = persistentChecker.get();
	persistentHeld = true;
	mu.unlock();

	// sync.OnceFunc — a shared flag makes only the first call real.
	auto once = std::make_shared<std::atomic<bool>>(false);
	return {c, [this, c, once] {
		        if (once->exchange(true)) {
			        return;
		        }
		        mu.lock();
		        persistentHeld = false;
		        if (c->WasCanceled()) {
			        // A canceled checker panics on reuse, so drop
			        // it; the next API acquisition creates a
			        // fresh persistent checker.
			        log("checkerpool: Persistent checker was "
			            "canceled, disposing");
			        if (persistentChecker.get() == c) {
				        persistentChecker.reset();
			        }
		        }
		        mu.unlock();
		        persistentSem.release();
	        }};
}

// createRelease — checkerpool.go:345. sync.OnceFunc release: disposes
// a canceled checker, otherwise merges global diagnostics (index 0),
// clears the hold, stamps lastReleased, and reschedules idle cleanup.
std::function<void()> checkerPool::createRelease(
    const std::string& requestID, int index, checker::Checker* c) {
	auto once = std::make_shared<std::atomic<bool>>(false);
	return [this, once, requestID, index, c] {
		if (once->exchange(true)) {
			return;
		}
		mu.lock();

		if (c->WasCanceled()) {
			// Canceled checkers must be disposed.
			log("checkerpool: Checker " + std::to_string(index) +
			    " for request " + holdTag(requestID) +
			    " was canceled, disposing");
			disposeCheckerLocked(index, c);
		} else {
			// Query checkers can produce incidental errors while
			// serializing types.
			if (index == 0) {
				mergeGlobalDiagnosticsFromCheckerLocked(index, c);
			}
			heldBy[index] = "";
			lastReleased[index] =
			    std::chrono::steady_clock::now();
			if (!discarded) {
				scheduleCleanupLocked();
			}
			// If discarded, skip scheduling cleanup — checkers
			// stay alive until the pool is garbage collected
			// so API clients can keep resolving handles.
		}

		// Unlock before releasing the semaphore slot. If we
		// received from the channel while holding p.mu, a woken
		// goroutine could immediately try to acquire p.mu,
		// risking priority inversion or unnecessary contention.
		mu.unlock();

		// Release the semaphore slot.
		if (index == 0) {
			diagSem.release();
		} else {
			querySem.release();
		}
	};
}

// registerRequestCleanup — checkerpool.go:387. context.AfterFunc
// deletes the request association when the request context is done,
// so the map does not grow unboundedly. p.mu held; the cleanup runs
// asynchronously.
void checkerPool::registerRequestCleanup(const gostd::Context& ctx,
                                         const std::string& requestID) {
	gostd::contextAfterFunc(ctx, [this, requestID] {
		std::lock_guard<std::mutex> lock(mu);
		requestAssociations.erase(requestID);
	});
}

// scheduleCleanupLocked — checkerpool.go:399. Reset (or start) the
// cleanup timer so it fires at the earliest pending checker-
// expiration deadline among all currently idle, unheld checkers.
// p.mu held; never on discarded pools.
void checkerPool::scheduleCleanupLocked() {
	std::chrono::steady_clock::time_point earliestDeadline;
	bool haveDeadline = false;
	for (size_t i = 0; i < checkers.size(); i++) {
		if (checkers[i] == nullptr || !heldBy[i].empty() ||
		    lastReleased[i] ==
		        std::chrono::steady_clock::time_point{}) {
			continue;
		}
		auto deadline = lastReleased[i] + opts.IdleTimeout;
		if (!haveDeadline || deadline < earliestDeadline) {
			earliestDeadline = deadline;
			haveDeadline = true;
		}
	}
	if (!haveDeadline) {
		// No idle checkers remain — stop the timer if it exists.
		if (cleanupTimer != nullptr) {
			cleanupTimer->Stop();
			cleanupTimer.reset();
		}
		return;
	}
	auto delay = earliestDeadline - std::chrono::steady_clock::now();
	if (delay <= std::chrono::nanoseconds{0}) {
		delay = std::chrono::milliseconds(1);
	}
	if (cleanupTimer != nullptr) {
		cleanupTimer->Reset(
		    std::chrono::duration_cast<std::chrono::nanoseconds>(
		        delay));
	} else {
		cleanupTimer.reset(gostd::afterFunc(
		    std::chrono::duration_cast<std::chrono::nanoseconds>(
		        delay),
		    [this] { cleanupIdleCheckers(); }));
	}
}

// cleanupIdleCheckers — checkerpool.go:431. Disposes checkers idle
// longer than IdleTimeout; the API checker is separate and never
// idle-cleaned.
void checkerPool::cleanupIdleCheckers() {
	std::lock_guard<std::mutex> lock(mu);
	// The timer callback may already have been in flight when
	// Discard() called Stop() (which does not guarantee the callback
	// won't run). Bail out without rescheduling so a discarded pool
	// doesn't keep itself alive via a new timer.
	if (discarded) {
		return;
	}
	auto now = std::chrono::steady_clock::now();
	for (size_t i = 0; i < checkers.size(); i++) {
		checker::Checker* c = checkers[i].get();
		if (c == nullptr || !heldBy[i].empty()) {
			continue;
		}
		if (lastReleased[i] ==
		    std::chrono::steady_clock::time_point{}) {
			continue;
		}
		auto idle = now - lastReleased[i];
		if (idle >= opts.IdleTimeout) {
			log("checkerpool: Disposing idle checker " +
			    std::to_string(i) + " (idle " +
			    gostd::durationString(
			        std::chrono::duration_cast<
			            std::chrono::nanoseconds>(idle)) +
			    ")");
			disposeCheckerLocked(static_cast<int>(i), c);
		}
	}
	// Reschedule for any remaining idle-but-not-yet-expired
	// checkers. scheduleCleanupLocked Resets the existing timer
	// rather than creating a new one, avoiding goroutine leaks.
	scheduleCleanupLocked();
}

// disposeCheckerLocked — checkerpool.go:463. Removes a checker and
// clears all associations referencing it. p.mu held.
void checkerPool::disposeCheckerLocked(int index, checker::Checker* c) {
	debug::assert(checkers[index].get() == c);
	checkers[index].reset();
	heldBy[index] = "";
	globalDiagCheckerCount[index] = 0;
	lastReleased[index] = std::chrono::steady_clock::time_point{};
	for (auto it = fileAssociations.begin();
	     it != fileAssociations.end();) {
		if (it->second == index) {
			it = fileAssociations.erase(it);
		} else {
			++it;
		}
	}
	for (auto it = requestAssociations.begin();
	     it != requestAssociations.end();) {
		if (it->second == index) {
			it = requestAssociations.erase(it);
		} else {
			++it;
		}
	}
}

// mergeGlobalDiagnosticsFromCheckerLocked — checkerpool.go:484. If
// the checker produced new global diagnostics since last looked,
// merges them into the accumulated set. p.mu held.
void checkerPool::mergeGlobalDiagnosticsFromCheckerLocked(
    int index, checker::Checker* c) {
	std::vector<Diagnostic*> globals = c->GetGlobalDiagnostics();
	if (static_cast<int>(globals.size()) ==
	    globalDiagCheckerCount[index]) {
		return;
	}
	globalDiagCheckerCount[index] =
	    static_cast<int>(globals.size());
	size_t before = globalDiagAccumulated.size();
	globalDiagAccumulated.insert(globalDiagAccumulated.end(),
	                             globals.begin(), globals.end());
	globalDiagAccumulated =
	    compiler::sortAndDeduplicateDiagnostics(
	        std::move(globalDiagAccumulated));
	if (globalDiagAccumulated.size() != before) {
		globalDiagChanged = true;
	}
}

// GetGlobalDiagnostics — checkerpool.go:499.
std::vector<Diagnostic*> checkerPool::GetGlobalDiagnostics() {
	std::lock_guard<std::mutex> lock(mu);
	return globalDiagAccumulated;
}

// TakeNewGlobalDiagnostics — checkerpool.go:507.
bool checkerPool::TakeNewGlobalDiagnostics() {
	std::lock_guard<std::mutex> lock(mu);
	bool changed = globalDiagChanged;
	globalDiagChanged = false;
	return changed;
}

// Discard — checkerpool.go:519. The pool's program was replaced; the
// pool stays functional but stops its idle-cleanup timer so checkers
// are not disposed until the pool is GC'd.
void checkerPool::Discard() {
	std::lock_guard<std::mutex> lock(mu);
	if (discarded) {
		return; // already discarded
	}
	log("checkerpool: Discarding pool, stopping idle cleanup");
	discarded = true;
	if (cleanupTimer != nullptr) {
		cleanupTimer->Stop();
		cleanupTimer.reset();
	}
}

} // namespace tsc::project
