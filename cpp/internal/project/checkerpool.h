// checkerpool.go — per-project pools of type checkers with request/file
// affinity, idle cleanup, and global-diagnostic accumulation.
//
// checkerpool.h
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/checker/checker.h"
#include "internal/compiler/program.h"
#include "internal/core/context.h"
#include "internal/gostd/gostd.h"

namespace tsc::project {

// checkerHeldAnonymous — checkerpool.go:20. Sentinel stored in heldBy
// when a checker is held by a caller with no request ID; distinguishes
// "held without ID" from "not held" ("").
inline const std::string checkerHeldAnonymous{"<anonymous>"};

// CheckerPoolOptions — checkerpool.go:22.
struct CheckerPoolOptions {
	// MaxCheckers — total checker slots per project (1 dedicated
	// diagnostics checker + N-1 query checkers). Minimum 2; 0 = 4.
	int MaxCheckers = 0;
	// IdleTimeout — how long an idle checker is kept before disposal.
	// 0 = 30s.
	std::chrono::nanoseconds IdleTimeout{0};
};

// checkerPool — checkerpool.go:41. Three checker categories:
//   - Diagnostics (index 0): a single checker for LSP diagnostics,
//     providing consistent walk order. Idle-cleaned.
//   - Temporary (indices 1+): ephemeral query checkers for LSP
//     operations. Idle-cleaned after IdleTimeout.
//   - API: a single checker for API operations with stable instance
//     identity for reference equality on handles. Never idle-cleaned.
class checkerPool : public compiler::CheckerPool {
public:
	CheckerPoolOptions opts;
	compiler::SimpleProgram* program = nullptr;

	std::mutex mu;

	// discarded — set when the pool's program has been replaced. The
	// pool remains fully functional but stops its idle-cleanup timer.
	bool discarded = false;

	// checkers[0] is the diagnostics checker; checkers[1:] ephemeral
	// query checkers. All idle-cleaned.
	std::vector<std::unique_ptr<checker::Checker>> checkers;
	// heldBy[i] is the requestID holding checker i,
	// checkerHeldAnonymous, or "" if not held.
	std::vector<std::string> heldBy;
	// file → query checker index (1+).
	std::unordered_map<SourceFile*, int> fileAssociations;
	// requestID → checker index.
	std::unordered_map<std::string, int> requestAssociations;
	// lastReleased[i] — when checker i was last released.
	std::vector<std::chrono::steady_clock::time_point> lastReleased;
	// cleanupTimer — reset each time a checker is released; when it
	// fires, idle checkers are disposed.
	std::unique_ptr<gostd::Timer> cleanupTimer;

	// persistentChecker — the API checker; never idle-cleaned.
	std::unique_ptr<checker::Checker> persistentChecker;
	bool persistentHeld = false;

	// Go's `chan struct{}` counting semaphores, one slot / N slots /
	// one slot. Implemented in checkerpool.cpp.
	struct semaphore {
		std::mutex mu;
		std::condition_variable cv;
		int tokens = 0;
		void acquire();   // sem <- struct{}{} in Go (send blocks when full)
		void release();   // <-sem in Go (receive frees a slot)
	};
	semaphore diagSem;
	semaphore querySem;
	semaphore persistentSem;

	std::function<void(std::string)> log;
	std::vector<Diagnostic*> globalDiagAccumulated;
	bool globalDiagChanged = false;
	// Per-checker count of globals last seen.
	std::vector<int> globalDiagCheckerCount;

	checkerPool() = default;
	checkerPool(const checkerPool&) = delete;
	checkerPool& operator=(const checkerPool&) = delete;

	// GetChecker — checkerpool.go:123 (compiler.CheckerPool iface).
	std::pair<checker::Checker*, std::function<void()>> GetChecker(
	    const gostd::Context& ctx, SourceFile* file) override;

	// GetGlobalDiagnostics — checkerpool.go:499.
	std::vector<Diagnostic*> GetGlobalDiagnostics();
	// TakeNewGlobalDiagnostics — checkerpool.go:507.
	bool TakeNewGlobalDiagnostics();
	// Discard — checkerpool.go:519.
	void Discard();

private:
	// tryReacquireForRequest — checkerpool.go:157. `sem` is acquired
	// inside the call exactly as Go sends to the channel.
	std::tuple<checker::Checker*, std::function<void()>, bool>
	tryReacquireForRequest(const std::string& requestID,
	                       semaphore& sem, bool isDiag);
	std::pair<checker::Checker*, std::function<void()>>
	getDiagnosticsChecker(const gostd::Context& ctx,
	                      const std::string& requestID);
	std::pair<checker::Checker*, std::function<void()>> getQueryChecker(
	    const gostd::Context& ctx, const std::string& requestID,
	    SourceFile* file);
	// findOrCreateQueryCheckerLocked — checkerpool.go:296. p.mu held.
	std::pair<checker::Checker*, int> findOrCreateQueryCheckerLocked();
	std::pair<checker::Checker*, std::function<void()>>
	getPersistentChecker();
	std::function<void()> createRelease(const std::string& requestID,
	                                    int index, checker::Checker* c);
	// registerRequestCleanup — checkerpool.go:387. p.mu held.
	void registerRequestCleanup(const gostd::Context& ctx,
	                            const std::string& requestID);
	// scheduleCleanupLocked — checkerpool.go:399. p.mu held; never on
	// discarded pools.
	void scheduleCleanupLocked();
	void cleanupIdleCheckers();
	// disposeCheckerLocked — checkerpool.go:463. p.mu held.
	void disposeCheckerLocked(int index, checker::Checker* c);
	// mergeGlobalDiagnosticsFromCheckerLocked — checkerpool.go:484.
	void mergeGlobalDiagnosticsFromCheckerLocked(int index,
	                                             checker::Checker* c);
};

// newCheckerPool — checkerpool.go:84.
checkerPool* newCheckerPool(const CheckerPoolOptions& opts,
                            compiler::SimpleProgram* program,
                            std::function<void(std::string)> log);

// noop — checkerpool.go:533.
inline void noop() {}

} // namespace tsc::project
