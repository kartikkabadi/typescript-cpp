// lspwatcher — port of tsc/internal/lsp/lspwatcher (lspwatcher.go).
// In-process file watcher used as a drop-in replacement for LSP-based file
// watching when the client does not support dynamic registration of file
// watchers.
#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "internal/fswatch/fswatch.h"
#include "internal/gostd/gostd.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/project/logging/logging.h"
#include "internal/vfs/vfs.h"

namespace tsc::lsconv {

// === dep decls — owned by ls slice (tsc/internal/ls/lsconv) ===
// converters.go:332 — FileNameToDocumentURI. Real port in lspwatcher.cpp (it
// is a pure leaf function the watcher needs to actually function); the ls
// slice owns the real package and replaces this when it lands.
lsp::lsproto::DocumentUri FileNameToDocumentURI(const std::string& fileName);

// === end dep decls ===

} // namespace tsc::lsconv

namespace tsc::lsp::lspwatcher {

// throttleWindow mirrors VS Code's parcel watcher integration: give the
// first batch a short grace window so adjacent filesystem bursts coalesce.
inline constexpr auto throttleWindow = std::chrono::milliseconds{75};

// watcherBackend — lspwatcher.go:27.
struct watcherBackend {
	virtual ~watcherBackend() = default;
	virtual std::pair<std::shared_ptr<fswatch::Watch>, gostd::Error>
	watchDirectory(const std::string& dir, const fswatch::WatchCallback& fn,
	               std::vector<std::shared_ptr<fswatch::WatchOption>> opts) = 0;
};

// defaultWatcherBackend — lspwatcher.go:31.
struct defaultWatcherBackend : watcherBackend {
	fswatch::Watcher* watcher;

	std::pair<std::shared_ptr<fswatch::Watch>, gostd::Error>
	watchDirectory(const std::string& dir, const fswatch::WatchCallback& fn,
	               std::vector<std::shared_ptr<fswatch::WatchOption>> opts) override {
		return watcher->watchDirectory(dir, fn, std::move(opts));
	}
};

struct watch;

// goTimer — time.AfterFunc: fires f after d unless stopped. The timer thread
// holds a shared_ptr to itself and to whatever f captures, mirroring Go's GC
// semantics (a pending AfterFunc keeps its closure alive).
struct goTimer {
	std::mutex mu;
	std::condition_variable cv;
	bool stopped = false;

	static std::shared_ptr<goTimer>
	afterFunc(std::chrono::milliseconds d, std::function<void()> f) {
		auto self = std::make_shared<goTimer>();
		std::thread([self, d, f = std::move(f)] {
			std::unique_lock<std::mutex> lk(self->mu);
			bool fired =
			    !self->cv.wait_for(lk, d, [&] { return self->stopped; });
			lk.unlock();
			if (fired) {
				f();
			}
		}).detach();
		return self;
	}

	// time.Timer.Stop — returns whether the timer was still pending.
	bool stop() {
		std::lock_guard<std::mutex> lk(mu);
		bool wasPending = !stopped;
		stopped = true;
		cv.notify_all();
		return wasPending;
	}
};

// Watcher manages a set of file system subscriptions identified by
// WatcherID strings (matching the LSP server's project.WatcherID type).
// Events are delivered to onChanges in batches as `*lsproto.FileEvent`,
// shaped exactly like a `workspace/didChangeWatchedFiles` notification.
struct Watcher : std::enable_shared_from_this<Watcher> {
	std::shared_ptr<vfs::FS> fs;
	std::shared_ptr<watcherBackend> backend;
	std::function<void(std::vector<std::shared_ptr<lsproto::FileEvent>>)> onChanges;
	std::shared_ptr<tsc::logging::Logger> logger;

	std::mutex mu;
	// watches holds the watches associated with each LSP WatcherID. A single
	// id may map to more than one watch because each FileSystemWatcher in the
	// registration becomes its own watch (different roots and kinds).
	std::unordered_map<std::string, std::vector<std::shared_ptr<watch>>> watches;
	bool closed = false;

	// Pending batch state, protected by mu.
	std::unordered_map<std::string, std::shared_ptr<lsproto::FileEvent>> pending;
	std::shared_ptr<goTimer> flushTimer;

	gostd::Error
	WatchFiles(const std::string& id,
	           const std::vector<std::shared_ptr<lsproto::FileSystemWatcher>>& fileSystemWatchers);
	gostd::Error UnwatchFiles(const std::string& id);
	void Close();

	// forwardEvents translates fswatch events into LSP file events and
	// enqueues them for the next debounced flush.
	void forwardEvents(lsproto::WatchKind kind, std::vector<fswatch::Event> events);

	// emitSyntheticCreates enqueues synthetic create events after a target
	// watch is (re)installed following a missing→present transition.
	void emitSyntheticCreates(const std::string& directory,
	                          lsproto::WatchKind kind, bool recursive);

	void enqueueSyntheticCreates(const std::vector<std::string>& paths);
	void scheduleFlushLocked();
	void flush();
};

// watch represents one FileSystemWatcher from the LSP registration. See
// lspwatcher.go:81-92 for the target/ancestor subscription model.
struct watch : std::enable_shared_from_this<watch> {
	std::shared_ptr<Watcher> watcher;
	std::string requestedDirectory;
	lsproto::WatchKind kind{};
	bool recursive = false;

	std::mutex mu;
	std::shared_ptr<fswatch::Watch> subscription; // target or ancestor; nil if none
	std::string watchedDirectory;
	bool watchingTarget = false;
	bool closed = false;

	void close();
	gostd::Error reconcile(bool emitSyntheticCreates);
	fswatch::WatchCallback targetCallback(const std::string& watchedDirectory);
	void handleTerminated();
	fswatch::WatchCallback ancestorCallback();
};

// New constructs a Watcher backed by internal/fswatch's platform-default
// watcher implementation.
std::shared_ptr<Watcher>
New(const std::shared_ptr<vfs::FS>& fs,
    std::function<void(std::vector<std::shared_ptr<lsproto::FileEvent>>)> onChanges,
    const std::shared_ptr<tsc::logging::Logger>& logger);

// newWithBackend — lspwatcher.go:99: shared construction path.
std::shared_ptr<Watcher>
newWithBackend(const std::shared_ptr<vfs::FS>& fs,
               const std::shared_ptr<watcherBackend>& backend,
               std::function<void(std::vector<std::shared_ptr<lsproto::FileEvent>>)> onChanges,
               const std::shared_ptr<tsc::logging::Logger>& logger);

// NewWithFSWatcher constructs a Watcher backed by the provided
// fswatch.Watcher.
std::shared_ptr<Watcher>
NewWithFSWatcher(const std::shared_ptr<vfs::FS>& fs, fswatch::Watcher* watcher,
                 std::function<void(std::vector<std::shared_ptr<lsproto::FileEvent>>)> onChanges,
                 const std::shared_ptr<tsc::logging::Logger>& logger);

// rootFromGlob — lspwatcher.go:549 (internal; tested by lspwatcher_test.go).
std::string rootFromGlob(std::string pattern);

// watchRoot extracts the directory the fswatch subscription should be rooted
// at from a FileSystemWatcher (lspwatcher.go:532).
std::pair<std::string, bool> watchRoot(const lsproto::FileSystemWatcher* fileSystemWatcher);
std::string watchPatternString(const lsproto::FileSystemWatcher* fileSystemWatcher);
bool isRecursiveGlob(const lsproto::FileSystemWatcher* fileSystemWatcher);
lsproto::WatchKind effectiveKind(const lsproto::FileSystemWatcher* fileSystemWatcher);

} // namespace tsc::lsp::lspwatcher
