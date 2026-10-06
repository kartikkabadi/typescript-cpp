#pragma once

// watchmanager.go — port of
// tsc/internal/execute/watchmanager/watchmanager.go: fswatch directory-watch
// lifecycle, event accumulation, and the DoCycle wake-up channel shared by
// the CLI watcher and build mode.

#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/execute/watchmanager/watchbackend.h"
#include "internal/gostd/gostd.h"
#include "internal/tspath/tspath.h"

namespace tsc::execute::watchmanager {

// watchedDir — watchmanager.go:14.
struct watchedDir {
	std::unique_ptr<gostd::io::Closer> closer;
	bool recursive = false;
};

// dirWatchUpdate — watchmanager.go:19.
struct dirWatchUpdate {
	std::string dir;
	bool recursive = false;
};

// WatchManager — watchmanager.go:29. Manages fswatch directory watches, event
// accumulation, and DoCycle signaling. It is shared by the CLI watcher and
// the build mode orchestrator.
//
// Locking contract:
//   - Call Lock/Unlock around the entire DoCycle body.
//   - ReconcileWatches must be called under Lock.
//   - CloseAllWatches and handleWatchTerminated manage their own locking.
class WatchManager {
	std::mutex mu;
	WatchBackend* backend = nullptr;
	std::unordered_map<std::string, watchedDir*> watchedDirs;
	// Go `doCycleCh chan struct{}` (cap 1) — a pending flag under mu.
	bool doCyclePending = false;
	std::condition_variable doCycleCv;

	std::mutex changedMu;
	std::unordered_map<std::string, fswatch::EventKind> changedPaths;
	bool changedOverflow = false;

public:
	// DebugLog receives verbose watch diagnostics when non-nil
	std::ostream* DebugLog = nullptr;

	std::ostream* warnWriter;
	std::function<bool(const std::string&)> dirExists;

	// NewWatchManager — watchmanager.go:44.
	explicit WatchManager(std::ostream* warnWriter,
	                      std::function<bool(const std::string&)> dirExists)
	    : warnWriter(warnWriter), dirExists(std::move(dirExists)) {}

	// SetBackend — watchmanager.go:54.
	void SetBackend(WatchBackend* b) { backend = b; }
	// Backend — watchmanager.go:56.
	WatchBackend* Backend() { return backend; }
	// EnsureDefaultBackend — watchmanager.go:58.
	void EnsureDefaultBackend();
	// Lock/Unlock — watchmanager.go:68.
	void Lock() { mu.lock(); }
	void Unlock() { mu.unlock(); }
	// DrainEvents — watchmanager.go:72.
	std::pair<std::unordered_map<std::string, fswatch::EventKind>, bool>
	DrainEvents();
	// ForceOverflow — watchmanager.go:83.
	void ForceOverflow();
	// signalDoCycle — watchmanager.go:89. Non-blocking send on the cap-1
	// channel: no-op when a signal is already pending.
	void signalDoCycle();
	// onWatchEvents — watchmanager.go:98.
	void onWatchEvents(const std::vector<fswatch::Event>& events,
	                   gostd::Error err);
	// handleWatchTerminated — watchmanager.go:133.
	void handleWatchTerminated(const std::string& dir, watchedDir* identity);
	// CloseAllWatches — watchmanager.go:152.
	void CloseAllWatches();
	// createDirWatchRequest — watchmanager.go:165.
	WatchDirectoryRequest createDirWatchRequest(const std::string& dir,
	                                            watchedDir* entry);
	// ResolveDesiredDirs — watchmanager.go:179.
	std::unordered_map<std::string, bool> ResolveDesiredDirs(
	    const std::unordered_map<std::string, bool>& desiredDirs);
	// ReconcileWatches — watchmanager.go:205.
	gostd::Error ReconcileWatches(
	    const std::unordered_map<std::string, bool>& desiredDirs);
	// createDirWatches — watchmanager.go:247.
	gostd::Error createDirWatches(
	    const std::vector<dirWatchUpdate>& updates);
	// IsPathUnderWatch — watchmanager.go:309.
	bool IsPathUnderWatch(const std::string& path,
	                      const tspath::ComparePathsOptions& opts);
	// RunLoop — watchmanager.go:318.
	void RunLoop(gostd::Context ctx, const std::function<void()>& doCycle);
};

// DirWatchSet — watchmanager.go:272. Accumulates the set of directories that
// should be watched while answering coverage queries efficiently. A directory
// is "covered" when it is already present in the set, or when it is contained
// within a recursive watch directory already in the set.
struct DirWatchSet {
	tspath::ComparePathsOptions opts;
	std::unordered_map<std::string, bool> dirs;
	std::unordered_map<std::string, std::string> names;

	// NewDirWatchSet — watchmanager.go:279.
	explicit DirWatchSet(const tspath::ComparePathsOptions& opts)
	    : opts(opts) {}

	// canonical — watchmanager.go:287.
	std::string canonical(const std::string& dir) const {
		return tspath::getCanonicalFileName(dir, opts.useCaseSensitiveFileNames);
	}
	// Set — watchmanager.go:291.
	void Set(const std::string& dir, bool recursive);
	// Covered — watchmanager.go:299.
	bool Covered(std::string dir);
	// Dirs — watchmanager.go:312.
	std::unordered_map<std::string, bool> Dirs() const;
};

}  // namespace tsc::execute::watchmanager
