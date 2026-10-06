#pragma once

// === dep stubs — removed when owner slice lands ===
// watchmanager.h — dep-stub decls for tsc/internal/execute/watchmanager,
// owned by the execute-tsc slice. Only what execute/build references today;
// real implementations land with the owner slice.
//
// Go context.Context params are dropped throughout (the port is
// single-threaded); Go `error` returns are gostd::Error (nil-able).

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "internal/fswatch/fswatch.h"
#include "internal/gostd/gostd.h"
#include "internal/tspath/tspath.h"

namespace tsc::execute::watchmanager {

// ===========================================================================
// watchbackend.go — WatchBackend / WatchDirectoryRequest
// ===========================================================================

// watchbackend.go:13 WatchDirectoryRequest.
struct WatchDirectoryRequest {
	std::string Dir;
	fswatch::WatchCallback Callback;
	bool Recursive = false;
	std::function<bool(const std::string&)> Ignore;
};

// watchbackend.go — WatchBackend abstracts fswatch.Watcher for testing.
struct WatchBackend {
	virtual ~WatchBackend() = default;
	virtual std::pair<std::shared_ptr<gostd::io::Closer>, gostd::Error>
	WatchDirectory(const std::string& dir, const fswatch::WatchCallback& fn,
	               bool recursive,
	               std::function<bool(const std::string&)> ignore) = 0;
	virtual std::pair<std::vector<std::shared_ptr<gostd::io::Closer>>,
	                  gostd::Error>
	WatchDirectories(const std::vector<WatchDirectoryRequest>& requests) = 0;
};

// watchbackend.go — CommandLineTestingWithWatchBackend is an optional
// extension of CommandLineTesting that supplies a WatchBackend for test mode.
struct CommandLineTestingWithWatchBackend {
	virtual ~CommandLineTestingWithWatchBackend() = default;
	virtual watchmanager::WatchBackend* WatchBackend() = 0;
};

// watchbackend.go — ShouldIgnoreWatchPath.
bool ShouldIgnoreWatchPath(const std::string& path);
// watchbackend.go — CanWatchDirectory.
bool CanWatchDirectory(const std::string& dir);
// watchbackend.go — PerceivedOsRootLengthForWatching.
int PerceivedOsRootLengthForWatching(const std::vector<std::string>& components);

// ===========================================================================
// watchmanager.go — DirWatchSet / WatchManager
// ===========================================================================

// watchmanager.go — DirWatchSet tracks desired watch dirs keyed by canonical
// path; names map canonical dir -> recursive flag name.
struct DirWatchSet {
	tspath::ComparePathsOptions opts;
	std::map<std::string, bool> dirs;
	std::map<std::string, std::string> names;

	// watchmanager.go — Set records dir (canonicalized) with recursive flag.
	void Set(const std::string& dir, bool recursive);
	// watchmanager.go — Covered reports whether dir is already watched or
	// nested inside a watched dir.
	bool Covered(const std::string& dir);
	// watchmanager.go — Dirs returns canonical dir -> recursive flag.
	const std::map<std::string, bool>& Dirs() const { return dirs; }
};

// watchmanager.go — NewDirWatchSet.
DirWatchSet* NewDirWatchSet(const tspath::ComparePathsOptions& opts);

// watchmanager.go — WatchManager owns fswatch backends and event draining.
struct WatchManager {
	std::mutex mu;
	WatchBackend* backend = nullptr;
	std::map<std::string, struct watchedDir*> watchedDirs;
	// doCycleCh chan struct{} (capacity 1) — signalling channel; stub
	// placeholder keeps the Go field shape.
	struct doCycleChan* doCycleCh = nullptr;
	gostd::io::Writer* DebugLog = nullptr;
	gostd::io::Writer* warnWriter = nullptr;
	std::function<bool(const std::string&)> dirExists;
	std::mutex changedMu;
	std::map<std::string, fswatch::EventKind> changedPaths;
	bool changedOverflow = false;

	void Lock();
	void Unlock();
	void SetBackend(WatchBackend* b);
	WatchBackend* Backend();
	void EnsureDefaultBackend();
	// doCycleCh <- struct{}{} non-blocking wake signal.
	void DoCycleChan();
	// DrainEvents — returns (changed paths, overflow flag).
	std::pair<std::map<std::string, fswatch::EventKind>, bool> DrainEvents();
	void ForceOverflow();
	// ResolveDesiredDirs — canonical dir set -> absolute dir set to watch.
	std::map<std::string, bool>
	ResolveDesiredDirs(const std::map<std::string, bool>& dirs);
	// ReconcileWatches — align actual watches with desired dirs.
	gostd::Error ReconcileWatches(const std::map<std::string, bool>& desired);
	// IsPathUnderWatch — whether dir is within a currently watched dir.
	bool IsPathUnderWatch(const std::string& dir,
	                      const tspath::ComparePathsOptions& opts);
	// RunLoop — Go takes (ctx, doCycle); ctx dropped per port convention.
	void RunLoop(const std::function<void()>& doCycle);
	void CloseAllWatches();
};

// watchmanager.go — NewWatchManager.
WatchManager* NewWatchManager(
    gostd::io::Writer* warnWriter,
    std::function<bool(const std::string&)> dirExists);

} // namespace tsc::execute::watchmanager
