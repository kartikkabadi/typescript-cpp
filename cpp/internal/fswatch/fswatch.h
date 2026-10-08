// fswatch.h — port of tsc/internal/fswatch (Linux paths only: inotify,
// fanotify, POSIX dirent walking). Filesystem event watching with debounced,
// consolidated callback delivery.
//
// Ports: watcher.go, event.go, debounce.go, inotify_linux.go,
// fanotify_linux.go, walkdir.go, walkdir_unix.go, walkdir_dirent_linux.go,
// pathcompare.go, pathkey.go, canonicalize_other.go. Skipped per task scope:
// *_darwin*, fsevents*, kqueue.go, windows.go, walkdir_windows.go,
// walkdir_other.go, and all *_test.go files.
#pragma once

#include <any>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "internal/gostd/gostd.h"

namespace tsc::fswatch {

struct dirWatch;
class debounce;
struct watcher;
struct watcherImpl;
struct callback;

// --- sentinel errors — watcher.go:17-50 ------------------------------------

inline const gostd::Error errNilCallback =
    gostd::newError("fswatch: callback must not be nil");

// errRootPath is returned by WatchFile when the supplied path is a
// filesystem root with no parent directory to watch.
inline const gostd::Error errRootPath =
    gostd::newError("fswatch: cannot watch a root path");

// errNotAbsolute is returned by [Watcher.WatchDirectory] and
// [Watcher.WatchFile] when the supplied path is not absolute.
inline const gostd::Error errNotAbsolute =
    gostd::newError("fswatch: path must be absolute");

// ErrOverflow indicates that the kernel event queue overflowed and
// some filesystem changes were missed. The watch remains
// active; further events will continue to be delivered. Callers
// should treat this as a signal to rescan the watched directory.
inline const gostd::Error ErrOverflow =
    gostd::newError("fswatch: event overflow; some changes were missed");

// ErrWatchTerminated indicates that the watch was terminated due to
// an unrecoverable error (e.g. the watched directory was deleted or
// the watch descriptor was revoked). No further events will be
// delivered. Call Close to release remaining state.
inline const gostd::Error ErrWatchTerminated =
    gostd::newError("fswatch: watch terminated");

// ErrUnavailable indicates that a requested watcher is not
// available on the current platform.
inline const gostd::Error ErrUnavailable =
    gostd::newError("fswatch: watcher not available on this platform");

// ErrFilesystemUnsupported indicates that the active watcher backend cannot
// operate on the target filesystem, even though the backend is available on
// the current platform. This happens, for example, with the fanotify backend
// on filesystems that do not support FID-based watching: name_to_handle_at
// returning EOPNOTSUPP (some Docker bind mounts backed by virtiofs, gRPC FUSE,
// or overlayfs) or fanotify_mark returning ENODEV (e.g. NTFS mounted via
// fuseblk).
inline const gostd::Error ErrFilesystemUnsupported = gostd::newError(
    "fswatch: watcher backend unsupported on this filesystem");

// --- events — event.go ------------------------------------------------------

// EventKind classifies a filesystem change.
enum class EventKind {
	EventUpdate = 1,
	EventDelete = 2,
};

std::string_view eventKindString(EventKind k);

// Event describes a single filesystem change.
struct Event {
	EventKind kind{};
	std::string path;
	bool includedWatchRoot = false;
};

// eventEntry tracks coalescing state during a debounce batch.
struct eventEntry {
	uint64_t createdSeq = 0;
	uint64_t updatedSeq = 0;
	uint64_t deletedSeq = 0;
	bool includedWatchRoot = false;

	bool isDeleted() const;
	std::pair<EventKind, bool> kindSince(uint64_t startSeq) const;
};

// eventList coalesces filesystem events by path within a debounce window.
//   - create after delete → update (rapid delete+recreate)
//   - getEvents skips entries that were both created and deleted
struct eventList {
	std::mutex mu;
	std::unordered_map<std::string, eventEntry> entries;
	gostd::Error err;
	uint64_t seq = 0;

	// create records a new-file event for path. Both create and update
	// produce EventUpdate externally; sequence state tracks coalescing
	// (create+delete within a batch cancels out).
	void create(const std::string& path);
	void createAt(const std::string& path, uint64_t seq);
	void createLocked(const std::string& path, uint64_t seq);

	// update records an update event for path.
	void update(const std::string& path);
	void updateAt(const std::string& path, uint64_t seq);
	void updateWatchRootAt(const std::string& path, uint64_t seq);
	void updateLocked(const std::string& path, uint64_t seq);

	// remove records a delete event for path.
	void remove(const std::string& path);
	uint64_t removeAndGetSequence(const std::string& path);
	void removeAt(const std::string& path, uint64_t seq);
	void removeWatchRootAt(const std::string& path, uint64_t seq);
	void removeLocked(const std::string& path, uint64_t seq);

	// size returns the number of tracked entries (including ones that may
	// cancel out in getEvents).
	size_t size();

	// snapshotLocked/snapshotSinceLocked — caller must hold el.mu.
	std::vector<Event> snapshotLocked();
	std::vector<Event> snapshotSinceLocked(uint64_t startSeq);

	// getEvents returns a snapshot of events, skipping entries that were both
	// created and deleted. Order is not guaranteed.
	std::vector<Event> getEvents();

	// drain atomically snapshots all pending events and the stored error,
	// then clears the list. This prevents events added between a separate
	// getEvents+clear from being silently dropped.
	std::pair<std::vector<Event>, gostd::Error> drain();
	std::pair<std::vector<std::vector<Event>>, gostd::Error>
	drainForSequences(const std::vector<uint64_t>& startSeqs);

	// setError stores the first error encountered (later errors are ignored).
	void setError(const gostd::Error& err);
	bool hasError();
	gostd::Error getError();

	eventEntry& getOrCreate(const std::string& path);
	uint64_t sequence();
	uint64_t nextSeqLocked();
	void advanceSeqLocked(uint64_t seq);
};

// --- debounce — debounce.go ------------------------------------------------

inline gostd::Duration minWaitTime = std::chrono::milliseconds(50);
inline gostd::Duration maxWaitTime = std::chrono::milliseconds(500);

// debounce batches filesystem events for one backend. Each *watcher
// owns one debounce instance, created lazily on first subscribe and
// living for the process lifetime. The background goroutine costs
// nothing when idle.
//
// Per-backend (rather than process-wide) isolation means a slow user
// callback on one backend cannot starve event delivery on the others.
//
// Internally uses a resettable latch: the loop blocks until trigger()
// is called, then coalesces for minWaitTime before firing callbacks.
class debounce {
	std::mutex mu;
	std::unordered_map<dirWatch*, std::function<void()>> callbacks;
	gostd::Time lastTime;

	// Latch state — replaces Go's waitCh/triggerCh channel pair. waitGen is
	// bumped when waitCh is closed (or replaced); triggerGen on each
	// triggerCh close. Generation numbers + the condvar give the same
	// semantics as close/replace on Go channels.
	std::mutex latchMu;
	std::condition_variable latchCv;
	uint64_t waitGen = 0;
	uint64_t triggerGen = 0;
	bool notified = false;

public:
	void add(dirWatch* key, std::function<void()> cb);
	void remove(dirWatch* key);

	// trigger wakes the debounce loop.
	void trigger();

	void loop();
	void notifyIfReady();
	void coalesceWait();

	// fireCallbacks snapshots and invokes all registered callbacks.
	void fireCallbacks();

	// latch helpers (replace signal_)
	void latchWait();
	void latchReset();
};

debounce* newDebounce();

// --- path comparers — pathcompare.go / pathkey.go --------------------------

// comparisonCache is shared only within a synchronous callback/termination
// pass, never published to a subscriber or stored on a watch.
using comparisonCache = std::unordered_map<std::string, std::string>;

// Watch roots are prepared before publication and are immutable thereafter.
// Event paths are local to one routing operation and folded only on demand.
struct comparisonPath {
	std::string path;
	std::string folded;
	bool ready = false;
	comparisonCache* cache = nullptr;

	std::string fold();
};

struct pathComparer {
	bool ignoreCase = false;

	bool operator==(const pathComparer&) const = default;

	comparisonPath prepare(const std::string& path) const;

	// suffix returns the part of path below root, respecting directory
	// boundaries.
	std::pair<std::string, bool> suffix(const std::string& root,
	                                    const std::string& path) const;
	std::pair<std::string, bool> suffixPrepared(comparisonPath root,
	                                            comparisonPath* path) const;
	std::pair<std::string, bool> suffixUnicode(comparisonPath root,
	                                           comparisonPath* path) const;

	bool contains(const std::string& root, const std::string& path) const;
	std::pair<std::string, bool> rebase(const std::string& path,
	                                    const std::string& from,
	                                    const std::string& to) const;
	std::pair<std::string, bool> rebasePrepared(comparisonPath* path,
	                                            comparisonPath from,
	                                            const std::string& to) const;
};

// canonicalize_other.go / fsevents_darwin_ffi.go:195 — native path
// folding is only available on Darwin.
#ifdef __APPLE__
inline constexpr bool nativePathFolding = true;
#else
inline constexpr bool nativePathFolding = false;
#endif
inline constexpr bool NativePathComparisonAvailable = nativePathFolding;

#ifdef __APPLE__
// canonicalize_darwin.go — UTF-8 NFC normalization used by watch keys
// and incoming FSEvents paths on darwin (ASCII fast path).
std::string normalizeNFC(std::string_view s);
#endif

[[noreturn]] void foldNativePathPanic();
std::string foldNativePath(std::string_view path);
std::string canonicalizePath(const std::string& p);

// PathComparer describes the filename equivalence of a watched volume. Its
// zero value compares bytes exactly. It is immutable and safe to share.
struct PathComparer {
	pathComparer comparer;

	bool operator==(const PathComparer&) const = default;

	// Key returns a watch-only comparison key, not a filesystem path or a
	// compiler identity. Native Darwin comparers use the same CoreFoundation
	// folding as the watcher. Other comparers preserve bytes, including
	// malformed UTF-8.
	std::string key(const std::string& path) const;

	// Rebase replaces a matching directory prefix while preserving the
	// spelling and byte boundaries of the remaining event path.
	std::pair<std::string, bool> rebase(const std::string& path,
	                                    const std::string& from,
	                                    const std::string& to) const;
};

// PathComparerForPath returns exact comparison on platforms without native
// Darwin watch aliases. It does not inspect the host filesystem.
std::pair<PathComparer, gostd::Error>
PathComparerForPath(const std::string& path);

// pathSuffixASCII — pathcompare.go:112. The third result requests Unicode
// comparison; an ASCII rejection must not reject an expanding alias just
// because the other spelling is ASCII.
struct pathSuffixASCIIResult {
	std::string suffix;
	bool ok = false;
	bool unicode = false;
};
pathSuffixASCIIResult pathSuffixASCII(std::string_view root,
                                      std::string_view path);

// pathSuffixFoldUnicode — pathcompare.go:141. Comparing the remaining
// components avoids assuming case-equivalent UTF-8 strings have the same
// byte length (for example, s and long s).
std::pair<std::string, bool> pathSuffixFoldUnicode(std::string_view root,
                                                   std::string_view path);

// --- shared path helpers (watcher.go + walkdir users) -----------------------

// isInDirectoryOrSelf — watcher.go:1086.
bool isInDirectoryOrSelf(std::string_view dir, std::string_view path);

// isDirectChild reports whether path is an immediate child of dir.
// Both paths must be absolute. Returns false for path == dir.
bool isDirectChild(std::string_view dir, std::string_view path);

// rebasePath replaces the from root in path with to, preserving any child
// suffix. Prefix matches must end at a path separator so sibling paths like
// "/foo2" are not rebased from "/foo".
std::string rebasePath(const std::string& path, const std::string& from,
                       const std::string& to);
std::string joinPathSuffix(const std::string& root, std::string_view suffix);

// physicalDirFor returns the physical path to watch for dir. If dir, or an
// ancestor of dir, is a symlink or reparse point, events are subscribed on
// its realpath while callbacks still use dir.
std::string physicalDirFor(const std::string& dir);

// --- Go stdlib analogs used by this package --------------------------------
// (os / io/fs / syscall / path/filepath subset; unix semantics)

inline bool isPathSeparator(char c) { return c == '/'; }

// syscall.Errno analogue: Is() implements fs.ErrPermission/ErrNotExist/
// ErrExist matching like Go's errno → sentinel mapping.
struct errnoErrorObj : gostd::ErrObj {
	int e;
	explicit errnoErrorObj(int e) : e(e) {}
	std::string Error() const override;
};

gostd::Error errnoError(int e);

// errors.Is(err, syscall.Errno(e)) — walks the unwrap chain for
// errnoErrorObj instances.
bool errnoIs(const gostd::Error& err, int e);

// io/fs sentinels (used by errors.Is checks in walkDirGeneric).
inline const gostd::Error fsErrPermission =
    gostd::newError("permission denied");
inline const gostd::Error fsErrNotExist =
    gostd::newError("file does not exist");
inline const gostd::Error fsErrExist = gostd::newError("file already exists");

// errors.Is(err, fs.ErrPermission) / errors.Is(err, fs.ErrNotExist).
bool errIsFsPermission(const gostd::Error& err);
bool errIsFsNotExist(const gostd::Error& err);

// os.PathError analogue.
gostd::Error osPathError(std::string_view op, const std::string& path, int e);

// os.Stat / os.Lstat (only IsDir is surfaced; enough for this package).
struct osFileInfo {
	bool isDir = false;
};
std::pair<osFileInfo, gostd::Error> osStat(const std::string& path);
std::pair<osFileInfo, gostd::Error> osLstat(const std::string& path);

// os.ReadDir — sorted by filename, as os.ReadDir guarantees. isDir comes
// from the dirent's d_type, matching Go's DirEntry.IsDir (DT_UNKNOWN →
// false; no lazy lstat).
struct osDirEntry {
	std::string name;
	bool isDir = false;
};
std::pair<std::vector<osDirEntry>, gostd::Error>
osReadDir(const std::string& path);

// utf8.ValidString — strict UTF-8 validity (Go semantics).
bool validUtf8(std::string_view s);

// strings.EqualFold — Go's per-rune case-fold comparison.
bool equalFold(std::string_view a, std::string_view b);

// --- walkdir — walkdir.go + walkdir_unix.go + walkdir_dirent_linux.go -------

using walkFn = std::function<gostd::Error(const std::string& path, bool isDir)>;

#ifndef _WIN32
#ifdef __APPLE__
// walkdir_dirent_darwin.go — getdirentries returns plain struct dirent
// records (d_fileno is the inode number).
#include <dirent.h>
using darwinDirent = struct dirent;
inline uint16_t reclenOf(const darwinDirent* d) { return d->d_reclen; }
inline uint64_t inoOf(const darwinDirent* d) { return d->d_fileno; }
#else
// linux_dirent64 — the getdents64 record layout (glibc has no userspace
// declaration). reclenOf/inoOf (walkdir_dirent_linux.go) access it.
struct linuxDirent64 {
	uint64_t d_ino;
	int64_t d_off;
	uint16_t d_reclen;
	uint8_t d_type;
	char d_name[];
};
inline uint16_t reclenOf(const linuxDirent64* d) { return d->d_reclen; }
inline uint64_t inoOf(const linuxDirent64* d) { return d->d_ino; }
#endif
#endif // !_WIN32

// walkDir walks dir, optionally recursively, invoking fn for each entry.
// On Linux it uses getdents64 directly so the d_type in each record drives
// the isDir flag without a stat; on Windows it is FindFirstFile-based
// (walkdir_windows.go).
gostd::Error walkDir(const std::string& dir, bool recursive, const walkFn& fn);

#ifndef _WIN32
// iterateDir reads fd's entries, invokes fn for the dir and each entry,
// and recurses into subdirectories via openat(fd, name). fd is owned by
// the caller; iterateDir does not close it. Sharing fd as the openat
// anchor for children avoids reopening the parent path once for the
// listing and again for each child.
struct walkState {
	std::vector<char> buf;
};
gostd::Error iterateDir(walkState* st, int fd, const std::string& dirname,
                        bool recursive, const walkFn& fn);

struct unixDirent {
	std::string name;
	uint8_t typ;
};

// readDirEntries reads every entry on fd via getdents64, extracting d_type
// so callers can skip per-entry lstat on filesystems that support it. The
// supplied buf is reused for every getdents64 syscall in the loop and may be
// reused across calls.
std::pair<std::vector<unixDirent>, gostd::Error>
readDirEntries(int fd, std::vector<char>& buf);
#endif // !_WIN32

// walkDirGeneric is the portable walkDir implementation. It is used as the
// primary implementation on platforms without a native version, and is
// tested on all platforms.
gostd::Error walkDirGeneric(const std::string& dir, bool recursive,
                            const walkFn& fn);
gostd::Error walkDirGenericVisit(const std::string& dir, bool recursive,
                                 const walkFn& fn);

// --- public API — watcher.go ------------------------------------------------

// WatchCallback receives batched filesystem events. Rapid changes
// are coalesced before delivery.
//
// For a given Watch, the callback is never invoked concurrently
// with itself. It runs on a library goroutine, not the caller's.
//
// When err is non-nil, use [errors.Is] to check for [ErrOverflow]
// (recoverable) or [ErrWatchTerminated] (terminal).
using WatchCallback = std::function<void(std::vector<Event>, gostd::Error)>;

struct watchOptions {
	std::function<bool(const std::string& path)> ignore;
	bool recursive = false;
	std::string file;
};

// WatchOption configures a watch.
struct WatchOption {
	virtual ~WatchOption() = default;
	virtual void applyWatchOption(watchOptions& opts) const = 0;
};

// fileOption defers the file filter until the parent directory's comparer is
// available, so WatchFile does not need a second filesystem query.
struct fileOption : WatchOption {
	std::string path;
	explicit fileOption(std::string p) : path(std::move(p)) {}
	void applyWatchOption(watchOptions& opts) const override {
		opts.file = path;
	}
};

struct ignoreOption : WatchOption {
	std::function<bool(const std::string& path)> fn;
	explicit ignoreOption(std::function<bool(const std::string&)> f)
	    : fn(std::move(f)) {}
	void applyWatchOption(watchOptions& opts) const override {
		opts.ignore = fn;
	}
};

struct recursiveOption : WatchOption {
	void applyWatchOption(watchOptions& opts) const override {
		opts.recursive = true;
	}
};

// WithIgnore returns a [WatchOption] that filters events before delivery.
// If the function returns true for a path, events for that path are
// silently dropped. The filtering is per-subscriber; multiple watches
// on the same directory may have different ignore functions.
std::shared_ptr<WatchOption>
WithIgnore(std::function<bool(const std::string& path)> fn);

// WithRecursive returns a [WatchOption] that enables recursive watching
// of the entire directory tree. Without this option,
// [Watcher.WatchDirectory] watches only direct children of dir.
std::shared_ptr<WatchOption> WithRecursive();

// Watch represents a live watch. Close stops watching
// and releases resources. It is idempotent.
struct Watch {
	virtual ~Watch() = default;
	virtual gostd::Error close() = 0;
	virtual void unexported() = 0;
};

// WatchDirectoryRequest describes one directory subscription in a
// [Watcher.WatchDirectories] batch.
struct WatchDirectoryRequest {
	std::string dir;
	WatchCallback callback;
	std::vector<std::shared_ptr<WatchOption>> options;
};

// Watcher represents a filesystem watching implementation.
// Use one of the constructor functions ([Inotify], [FSEvents], [Kqueue],
// [Windows]) to obtain a value, or [Default] for the platform default.
//
// All watchers exist on every platform. Subscribing with a watcher that
// is not supported on the current OS returns [ErrUnavailable].
struct Watcher {
	virtual ~Watcher() = default;
	// Name returns a stable identifier ("inotify", "fsevents", "kqueue",
	// "windows").
	virtual std::string name() = 0;
	// Available reports whether this watcher works on the current OS.
	virtual bool available() = 0;
	// HasFastRecursiveBackend reports whether this watcher supports efficient
	// recursive watching without requiring a full userspace tree walk.
	virtual bool hasFastRecursiveBackend() = 0;
	virtual std::pair<std::shared_ptr<Watch>, gostd::Error>
	watchDirectory(const std::string& dir, const WatchCallback& fn,
	               std::vector<std::shared_ptr<WatchOption>> opts = {}) = 0;
	virtual std::pair<std::vector<std::shared_ptr<Watch>>, gostd::Error>
	watchDirectories(std::vector<WatchDirectoryRequest> requests) = 0;
	virtual std::pair<std::shared_ptr<Watch>, gostd::Error>
	watchFile(const std::string& path, const WatchCallback& fn) = 0;
	virtual void unexported() = 0;
};

// --- callback / dirWatch — watcher.go:780-1164 ------------------------------

struct callback {
	uint64_t id = 0;
	std::string dir;
	std::string physicalDir;
	std::string watchDir;
	std::string watchPhysicalDir;
	bool recursive = false;
	WatchCallback fn;
	std::function<bool(const std::string& path)> ignore;
	uint64_t sinceSeq = 0;
	gostd::Error terminal;
	bool delivered = false;
	pathComparer comparer;
	comparisonPath dirComparison;
	comparisonPath physicalComparison;
	comparisonPath fileComparison;

	Event mapEvent(Event e) const;
	Event mapEventCached(Event e, comparisonCache* cache) const;
	std::string eventPhysicalPath(const std::string& path) const;
};

// dirWatchError associates an error with a specific directory watch.
struct dirWatchErrorObj : gostd::ErrObj {
	gostd::Error err;
	std::shared_ptr<fswatch::dirWatch> dirWatch;

	dirWatchErrorObj(gostd::Error e, std::shared_ptr<fswatch::dirWatch> dw)
	    : err(std::move(e)), dirWatch(std::move(dw)) {}
	std::string Error() const override { return err->Error(); }
	std::vector<gostd::Error> unwrap() const override { return {err}; }
};

// dirWatch holds per-directory state: pending events, registered callbacks,
// and a reference to the shared debouncer. Each watched directory has one.
struct dirWatch {
	// dir is the caller-visible watch root used in delivered event paths.
	std::string dir;
	// physicalDir is the path passed to OS watcher APIs. It differs from dir
	// when dir or an ancestor is a symlink or reparse point to a directory.
	std::string physicalDir;
	bool recursive = false;
	eventList events;
	pathComparer comparer;
	std::string dirFold;
	std::string physicalDirFold;

	// state stores per-directory platform-specific bookkeeping
	// (fsevents, windows); unused on Linux.
	std::any state;
	// sequence returns a backend event sequence cutoff for new logical
	// callbacks.
	std::function<uint64_t()> sequence;

	std::mutex mu;
	std::vector<callback> callbacks;
	fswatch::debounce* debounce = nullptr;
	uint64_t nextCBID = 0;

	void setComparer(const pathComparer& comparer);

	// displayPath maps a physical event path back under the caller-visible
	// watch root.
	std::string displayPath(const std::string& watchPath) const;

	// physicalPath maps a caller-visible path to the physical watched root.
	std::string physicalPath(const std::string& displayPath) const;

	void destroyDebounce();
	void notify();
	void notifyError(const gostd::Error& err);
	void triggerCallbacks();
	bool terminateCallbacksForDeletedRoot(const std::string& path,
	                                      uint64_t seq,
	                                      const gostd::Error& err);

	std::pair<uint64_t, bool>
	watch(const std::string& dir, const std::string& physicalDir,
	      bool recursive, const WatchCallback& fn,
	      const std::function<bool(const std::string&)>& ignore);
	std::pair<uint64_t, bool>
	addCallback(const std::string& dir, const std::string& physicalDir,
	            bool recursive, const WatchCallback& fn,
	            const std::function<bool(const std::string&)>& ignore,
	            const std::string& file);
	bool unwatch(uint64_t id);
	void unref(watcher* w);
};

std::shared_ptr<dirWatch>
newDirWatch(const std::string& dir, const std::string& physicalDir,
            fswatch::debounce* db);

// --- watcherImpl / watcherBase — watcher.go:637-778 --------------------------

// watcherImpl is the internal interface implemented by each platform watcher.
struct watcherImpl {
	virtual ~watcherImpl() = default;
	virtual gostd::Error start() = 0;
	virtual gostd::Error run() = 0;
	virtual void shutdown() = 0;

	virtual gostd::Error watchAdd(std::shared_ptr<dirWatch> w) = 0;
	virtual gostd::Error
	watchAddMany(const std::vector<std::shared_ptr<dirWatch>>& watches) = 0;
	virtual void watchRemove(std::shared_ptr<dirWatch> w) = 0;
	virtual void handleWatcherError(
	    const std::shared_ptr<dirWatchErrorObj>& werr) = 0;

	virtual gostd::Error subscribe(std::shared_ptr<dirWatch> w) = 0;
	virtual gostd::Error closeWatch(std::shared_ptr<dirWatch> w) = 0;

	// Go's `b.self.(interface{ subscribeMany })` type assertion: backends
	// that batch subscriptions override supportsSubscribeMany/subscribeMany.
	virtual bool supportsSubscribeMany() const { return false; }
	virtual gostd::Error
	subscribeMany(const std::vector<std::shared_ptr<dirWatch>>& watches) {
		return nullptr;
	}
};

// watcherBase provides shared watch-tracking and lifecycle logic.
// Concrete backends embed it and override subscribe/closeWatch/start.
struct watcherBase : watcherImpl {
	std::mutex mu;
	std::unordered_set<std::shared_ptr<dirWatch>> subscriptions;

	// 'started' channel: a flag + condvar under startedMu replaces
	// chan struct{} / close().
	std::mutex startedMu;
	std::condition_variable startedCv;
	bool startedFlag = false;
	gostd::Error startErr;

	// init(self) — the back-reference is unnecessary in C++ (real virtual
	// dispatch); kept as a no-arg initializer for structural parity.
	void init();
	void notifyStarted();
	void shutdown() override {}
	gostd::Error run() override;
	void handleStartError(const gostd::Error& err);

	gostd::Error watchAdd(std::shared_ptr<dirWatch> w) override;
	gostd::Error
	watchAddMany(const std::vector<std::shared_ptr<dirWatch>>& watches)
	    override;
	void watchRemove(std::shared_ptr<dirWatch> w) override;
	void handleWatcherError(const std::shared_ptr<dirWatchErrorObj>& werr)
	    override;
};

// --- watcher — watcher.go:312-578 -------------------------------------------

inline constexpr int recursiveConsolidateThreshold = 10;

// watcher is the concrete implementation of [Watcher]. Each platform
// watcher is a package-level *watcher whose factory is set by the
// platform's init() function.
struct watcher : Watcher {
	std::string watcherName;
	std::mutex mu;
	watcherImpl* impl = nullptr;
	std::function<watcherImpl*()> factory; // nil if not available
	std::unordered_map<std::string, std::shared_ptr<dirWatch>> dirWatches;
	fswatch::debounce* debounce = nullptr; // lazily created in
	                                     // getOrCreateDirWatch
	std::function<uint64_t()> sequence;

	explicit watcher(std::string name) : watcherName(std::move(name)) {}
	~watcher() override = default;

	std::string name() override { return watcherName; }
	std::string string() const { return watcherName; }
	bool available() override { return factory != nullptr; }
	void unexported() override {}
	bool hasFastRecursiveBackend() override;
	bool canShareRecursiveDirWatches() const;

	std::pair<watcherImpl*, gostd::Error> getImpl();
	std::string keyForDirWatch(const std::string& dir, bool recursive) const;
	std::shared_ptr<dirWatch> findCoveringRecursiveWatchLocked(
	    const std::string& dir, const std::string& physicalDir,
	    const pathComparer& comparer);
	std::string findConsolidationDirLocked(const std::string& dir,
	                                       const std::string& physicalDir);
	std::pair<std::shared_ptr<dirWatch>, gostd::Error>
	getOrCreateDirWatch(const std::string& dir, const std::string& physicalDir,
	                    bool recursive, const pathComparer& comparer);
	void removeDirWatch(dirWatch* dw);

	std::pair<std::shared_ptr<Watch>, gostd::Error>
	watchDirectory(const std::string& dir, const WatchCallback& fn,
	               std::vector<std::shared_ptr<WatchOption>> opts = {})
	    override;
	std::pair<std::vector<std::shared_ptr<Watch>>, gostd::Error>
	watchDirectories(std::vector<WatchDirectoryRequest> requests) override;
	std::pair<std::shared_ptr<Watch>, gostd::Error>
	watchFile(const std::string& path, const WatchCallback& fn) override;

	// watcher.pathComparer — canonicalize_other.go:16.
	std::pair<fswatch::pathComparer, gostd::Error>
	pathComparer(const std::string& dir) const;
};

// validateWatchDirectory — watcher.go:580.
gostd::Error validateWatchDirectory(const std::string& dir);

// watch — watcher.go:611-635. Live subscription handle.
struct watch : Watch {
	std::mutex mu;
	watcher* w;
	std::shared_ptr<dirWatch> dw;
	watcherImpl* impl;
	uint64_t id;
	bool cancelled = false;

	watch(fswatch::watcher* w, std::shared_ptr<fswatch::dirWatch> dw,
	      watcherImpl* impl, uint64_t id)
	    : w(w), dw(dw), impl(impl), id(id) {}

	gostd::Error close() override;
	void unexported() override {}
};

// --- fallbackWatcher — watcher.go:253-310 ------------------------------------

// fallbackWatcher keeps the primary backend for supported filesystems while
// routing individual unsupported watches to the secondary backend.
struct fallbackWatcher : Watcher {
	Watcher* primary;
	Watcher* secondary;

	fallbackWatcher(Watcher* p, Watcher* s) : primary(p), secondary(s) {}

	std::string name() override { return primary->name(); }
	bool available() override { return primary->available(); }
	bool hasFastRecursiveBackend() override {
		return primary->hasFastRecursiveBackend();
	}
	void unexported() override {}

	std::pair<std::shared_ptr<Watch>, gostd::Error>
	watchDirectory(const std::string& dir, const WatchCallback& fn,
	               std::vector<std::shared_ptr<WatchOption>> opts = {})
	    override;
	std::pair<std::vector<std::shared_ptr<Watch>>, gostd::Error>
	watchDirectories(std::vector<WatchDirectoryRequest> requests) override;
	std::pair<std::shared_ptr<Watch>, gostd::Error>
	watchFile(const std::string& path, const WatchCallback& fn) override;
};

// --- package watchers — watcher.go:187-251 -----------------------------------

// Package-level watcher instances (Go: `var inotifyWatcher = &watcher{...}` +
// platform init() setting factory). Function-local statics replace the
// globals so backend registration (a Go init()) is immune to cross-TU
// static-init ordering. The Linux backends define inotifyWatcher()/
// fanotifyWatcher() in their .cpps; the rest live in watcher.cpp with no
// factory, matching "not available on this platform".
watcher& inotifyWatcher();
watcher& fseventsWatcher();
watcher& kqueueWatcher();
watcher& windowsWatcher();
watcher& fanotifyWatcher();
fallbackWatcher& fanotifyFallbackWatcher();

// AllWatchers returns a fresh slice listing every watcher backend the package
// knows about. Use [Watcher.Available] to check which ones work on the
// current OS.
std::vector<Watcher*> AllWatchers();

Watcher* Inotify();  // inotify watcher (Linux and Android)
Watcher* FSEvents(); // FSEvents watcher (macOS)
Watcher* Kqueue();   // kqueue watcher (macOS, FreeBSD, and other BSDs)
Watcher* Windows();  // ReadDirectoryChangesW watcher (Windows)

// Fanotify returns the fanotify watcher (Linux, kernel ≥ 5.13). Directories
// on filesystems that don't support fanotify watches automatically use
// inotify instead.
Watcher* Fanotify();

// Default returns the recommended watcher for the current OS.
Watcher* Default();

// --- fanotify internals — fanotify_linux.go --------------------------------
// Unexported package internals. In Go the package tests access these because
// test files share package fswatch; in C++ they are declared here so
// internal/fswatch/tests can reach them. Not public API.

// fanotifyHandleKey uniquely identifies a filesystem object by its fsid and
// file handle. Used as a map key for watch dispatch.
struct fanotifyHandleKey {
	std::array<int32_t, 2> fsid;
	int32_t handleType = 0;
	std::string handle; // raw handle bytes as string for map comparability

	bool operator==(const fanotifyHandleKey&) const = default;
};

fanotifyHandleKey makeFanotifyHandleKey(const std::array<int32_t, 2>& fsid,
                                        int32_t handleType,
                                        std::string_view handleBytes);

struct fanotifyHandleKeyHash {
	size_t operator()(const fanotifyHandleKey& k) const;
};

// fanotifySubscription mirrors inotifySubscription for the fanotify backend.
struct fanotifySubscription {
	std::string path;
	std::string watchPath;
	std::shared_ptr<fswatch::dirWatch> dirWatch;
	fanotifyHandleKey key;
};

// fanotifyDfidName holds parsed directory FID + name from an info record.
struct fanotifyDfidName {
	fanotifyHandleKey key;
	std::string name; // child entry name, or "" for self-events on
	                  // directories
};

// maybeWrapUnsupportedFilesystem tags errnos that mean the filesystem cannot
// support fanotify FID-based watching with ErrFilesystemUnsupported so the
// fallbackWatcher can route the watch to inotify (Go issue #63646/#63678).
gostd::Error maybeWrapUnsupportedFilesystem(const gostd::Error& err);

// fanotifyBackend is the fanotify-based watcher backend for Linux.
struct fanotifyBackend : watcherBase {
	int pipeFDs[2];
	std::atomic<int32_t> pipeWriteFD{-1};
	int fanotifyFD;
	uint64_t markMask; // fanotifyMarkMaskRename or
	                   // fanotifyMarkMaskMovedFromTo; 0 until first
	                   // subscribe
	bool noRename;     // when true, skip FAN_RENAME probe (for testing
	                   // fallback path)

	std::unordered_map<fanotifyHandleKey,
	                   std::vector<fanotifySubscription*>,
	                   fanotifyHandleKeyHash>
	    subscriptions;

	std::mutex endedMu;
	std::condition_variable endedCv;
	bool ended = false;

	// Persistent buffers reused across handleEvents calls. Only accessed
	// from the start thread, so no synchronization needed.
	std::vector<char> readBuf;
	std::unordered_set<std::shared_ptr<fswatch::dirWatch>> watchersTouched;

	fanotifyBackend(bool noRename);
	~fanotifyBackend() override;
	gostd::Error start() override;
	void closeFDs();
	void signalEnded();
	void shutdown() override;
	gostd::Error subscribe(std::shared_ptr<fswatch::dirWatch> w) override;
	gostd::Error markDir(std::shared_ptr<fswatch::dirWatch> w,
	                     const std::string& path, const std::string& markPath);
	gostd::Error handleEvents();
	void handleOverflow(
	    std::unordered_set<std::shared_ptr<fswatch::dirWatch>>& touched);
	void handleRenameEvent(
	    uint64_t mask, const fanotifyDfidName* dfidOld,
	    const fanotifyDfidName* dfidNew,
	    std::unordered_set<std::shared_ptr<fswatch::dirWatch>>& touched);
	void handleParsedEvent(
	    uint64_t mask, const fanotifyDfidName* dfid,
	    std::unordered_set<std::shared_ptr<fswatch::dirWatch>>& touched);
	bool handleSubscription(uint64_t mask, const fanotifyDfidName* dfid,
	                        fanotifySubscription* sub);
	void dropSubsForPathLocked(const std::string& path);
	void dropSubsForPathAndDescendantsLocked(const std::string& path);
	gostd::Error closeWatch(std::shared_ptr<fswatch::dirWatch> w) override;
};

// fanotifyAvailable probes whether fanotify_init succeeds with the flags
// this backend needs.
#ifndef __linux__
// fanotify is a Linux-only API (fanotify.cpp compiles only there, like
// Go's //go:build linux). Tests reference the probe on every platform.
inline bool fanotifyAvailable() { return false; }
inline fanotifyBackend* newFanotifyBackend(bool) { return nullptr; }
#else
bool fanotifyAvailable();
#endif

// newFanotifyBackend creates a fanotify backend. If noRename is true, the
// backend skips the FAN_RENAME probe and forces the FAN_MOVED_FROM/
// FAN_MOVED_TO fallback path; this is only used by the fanotify-no-rename
// test watcher to exercise the fallback path on kernels that natively
// support FAN_RENAME.
#ifdef __linux__
fanotifyBackend* newFanotifyBackend(bool noRename);
#endif

} // namespace tsc::fswatch
