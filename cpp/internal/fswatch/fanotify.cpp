// fanotify_linux.go — port of tsc/internal/fswatch/fanotify_linux.go: the
// Linux fanotify(7) backend.
//
// Uses Linux's fanotify(7) API (kernel ≥ 5.13 without CAP_SYS_ADMIN) to
// watch directory trees. Unlike inotify, fanotify uses FID-based event
// reporting (FAN_REPORT_FID | FAN_REPORT_DFID_NAME): each event carries the
// parent directory's file handle and the child entry name, so watch
// dispatch is keyed by (fsid, handle_type, handle_bytes) instead of a wd
// integer. This avoids the inotify per-user watch limit (fs.inotify.
// max_user_watches) entirely.
//
//	┌──────────────────────────────────────────────────────────────┐
//	│                     fanotifyBackend                          │
//	│                                                              │
//	│  ┌───────────┐        poll(2)        ┌──────────────────┐    │
//	│  │ pipe[0]   ├──────────────────────►│                  │    │
//	│  │ (wakeup)  │                       │  start()         │    │
//	│  └───────────┘                       │  thread          │    │
//	│  ┌───────────┐                       │  (event loop)    │    │
//	│  │ fanotify  ├──────────────────────►│                  │    │
//	│  │ fd        │                       └────────┬─────────┘    │
//	│  └───────────┘                                │              │
//	│                                      handleEvents()          │
//	│                                               │              │
//	│                                  parseFanotifyDfidNames      │
//	│                                  (extract handleKey + name)  │
//	│                                               │              │
//	│                                               ▼              │
//	│                               ┌─────────────────────────┐    │
//	│                               │ subscriptions           │    │
//	│                               │ map[handleKey] → []sub  │    │
//	│                               │  sub.dirWatch.events    │    │
//	│                               └─────────────────────────┘    │
//	│                                                              │
//	│  handleKey = (fsid, handle_type, handle_bytes)               │
//	│  obtained via statfs(2) + name_to_handle_at(2) per dir       │
//	└──────────────────────────────────────────────────────────────┘
//
// Goroutines and threading:
//   - One long-lived thread (start), launched by watcherBase.run(). It
//     owns the poll(2) loop and runs for the process lifetime. All event
//     reading and dispatch (handleEvents, handleParsedEvent,
//     handleSubscription, handleRenameEvent) execute on this thread,
//     under b.mu.
//   - subscribe/closeWatch run on the caller's thread under
//     watcherBase.mu. The event loop acquires b.mu for watch map
//     access, providing safe interleaving.
//
// Callback delivery:
//   dirWatch.notify() posts to the shared process-wide debouncer. After a
//   coalescing window (50 ms min / 500 ms max), the debouncer invokes all
//   registered WatchCallbacks on its own dedicated thread; never on
//   the caller's thread or the event-loop thread.
//
// WatchDirectory flow (caller thread):
//  1. Walk the target directory.
//  2. On the first subscribe, probe FAN_RENAME support (Linux 5.17+) by
//     attempting a fanotify_mark with FAN_RENAME. If the kernel returns
//     EINVAL or EOPNOTSUPP, fall back to FAN_MOVED_FROM | FAN_MOVED_TO
//     (two separate events instead of one paired event for renames).
//  3. For every directory found:
//     a. fanotify_mark(FAN_MARK_ADD | FAN_MARK_ONLYDIR) to watch it.
//     b. name_to_handle_at(2) to obtain the directory's file handle.
//     c. statfs(2) to obtain the filesystem ID (fsid).
//     d. Map (fsid, handle_type, handle_bytes) → fanotifySubscription.
//
// Event format:
//   Each event has a FanotifyEventMetadata header followed by variable-length
//   info records. parseFanotifyDfidNames extracts DFID_NAME records
//   (FAN_EVENT_INFO_TYPE_DFID_NAME, OLD_DFID_NAME, NEW_DFID_NAME) containing
//   the parent directory's file handle and child entry name. The file handle
//   is matched against the watch map to find the watched directory.
//
// Event dispatch (on start thread):
//   - FAN_CREATE / FAN_MOVED_TO  → events.create (→ EventUpdate); if the new
//     entry is a directory (FAN_ONDIR), recursively walk and mark it.
//   - FAN_MODIFY               → events.update (→ EventUpdate).
//   - FAN_DELETE* / FAN_MOVE*    → events.remove (→ EventDelete); drop
//     subscriptions for the removed path and any descendants.
//   - FAN_RENAME (5.17+)         → single paired event with OLD_DFID_NAME +
//     NEW_DFID_NAME info records; handleRenameEvent deletes the old path and
//     creates the new path in one pass.
//   - FAN_Q_OVERFLOW             → set ErrOverflow on every active dirWatch.
//
//   Merged events: fanotify can merge consecutive events on the same object
//   into one event with multiple mask bits. When both create and delete bits
//   are set, handleSubscription stats the path to determine which happened
//   last (exists → delete-then-create = update; gone → create-then-delete =
//   events cancel out).
//
//   After processing all buffered events, call dirWatch.notify() on each
//   touched dirWatch to trigger the debouncer.
//
// Shutdown:
//   Write a byte to pipe[1] → poll sees POLLIN on pipe[0] → loop exits →
//   deferred closeFDs closes fanotify fd, pipe fds, and signals endedSignal.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE // pipe2, name_to_handle_at
#endif

#include "internal/fswatch/fswatch.h"

#include <fcntl.h>
#include <linux/fanotify.h>
#include <poll.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cstring>
#include <optional>

namespace tsc::fswatch {

namespace {

constexpr uint32_t fanotifyInitFlags = FAN_CLASS_NOTIF | FAN_CLOEXEC |
    FAN_NONBLOCK | FAN_REPORT_FID | FAN_REPORT_DFID_NAME;

constexpr uint64_t fanotifyMarkMaskBase = FAN_CREATE | FAN_DELETE |
    FAN_MODIFY | FAN_DELETE_SELF | FAN_MOVE_SELF | FAN_ONDIR |
    FAN_EVENT_ON_CHILD;

// Used when FAN_RENAME is available (Linux 5.17+).
constexpr uint64_t fanotifyMarkMaskRename = fanotifyMarkMaskBase | FAN_RENAME;

// Fallback when FAN_RENAME is not available.
constexpr uint64_t fanotifyMarkMaskMovedFromTo =
    fanotifyMarkMaskBase | FAN_MOVED_FROM | FAN_MOVED_TO;

constexpr uint32_t fanotifyMarkAddFlags =
    FAN_MARK_ADD | FAN_MARK_ONLYDIR | FAN_MARK_DONT_FOLLOW;

constexpr size_t fanotifyBufferSize = 8192;

// Raw syscall wrappers — glibc has no fanotify_init/fanotify_mark.
int fanotifyInit(uint32_t flags, uint32_t eventFlags) {
	return static_cast<int>(
	    ::syscall(SYS_fanotify_init, flags, eventFlags));
}
int fanotifyMark(int fd, uint32_t flags, uint64_t mask, int dirfd,
                 const char* pathname) {
	return static_cast<int>(
	    ::syscall(SYS_fanotify_mark, fd, flags, mask, dirfd, pathname));
}

// fanotifyHandleKey uniquely identifies a filesystem object by its fsid and
// file handle. Used as a map key for watch dispatch.
struct fanotifyHandleKey {
	std::array<int32_t, 2> fsid;
	int32_t handleType;
	std::string handle; // raw handle bytes as string for map comparability

	bool operator==(const fanotifyHandleKey&) const = default;
};

fanotifyHandleKey makeFanotifyHandleKey(const std::array<int32_t, 2>& fsid,
                                        int32_t handleType,
                                        std::string_view handleBytes) {
	return fanotifyHandleKey{fsid, handleType, std::string(handleBytes)};
}

struct fanotifyHandleKeyHash {
	size_t operator()(const fanotifyHandleKey& k) const {
		size_t h = std::hash<std::string>{}(k.handle);
		h = h * 31 + std::hash<int32_t>{}(k.fsid[0]);
		h = h * 31 + std::hash<int32_t>{}(k.fsid[1]);
		h = h * 31 + std::hash<int32_t>{}(k.handleType);
		return h;
	}
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

// Forward decls — definitions follow the backend.
gostd::Error maybeWrapUnsupportedFilesystem(const gostd::Error& err);
std::pair<std::shared_ptr<fanotifyDfidName>,
          std::shared_ptr<fanotifyDfidName>>
parseFanotifyDfidNames(std::string_view data);
std::shared_ptr<fanotifyDfidName>
parseFanotifyFidRecord(std::string_view data, bool hasName);

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
bool fanotifyAvailable() {
	// runtime.GOOS == "android" — false on this Linux-only build.
	constexpr std::string_view goos = "linux";
	if (goos == "android") {
		return false;
	}
	int fd = fanotifyInit(fanotifyInitFlags, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		return false;
	}
	(void)::close(fd);
	return true;
}

// newFanotifyBackend creates a fanotify backend. If noRename is true, the
// backend skips the FAN_RENAME probe and forces the FAN_MOVED_FROM/
// FAN_MOVED_TO fallback path; this is only used by the fanotify-no-rename
// test watcher to exercise the fallback path on kernels that natively
// support FAN_RENAME.
fanotifyBackend* newFanotifyBackend(bool noRename) {
	return new fanotifyBackend(noRename);
}

fanotifyBackend::fanotifyBackend(bool noRename_) {
	pipeFDs[0] = -1;
	pipeFDs[1] = -1;
	fanotifyFD = -1;
	markMask = 0;
	noRename = noRename_;
	readBuf.resize(fanotifyBufferSize);
	pipeWriteFD.store(-1);
	watcherBase::init();
}

fanotifyBackend::~fanotifyBackend() {
	for (auto& kv : subscriptions) {
		for (auto* s : kv.second) {
			delete s;
		}
	}
}

// defer func() { b.closeFDs(); close(b.endedSignal) }()
struct deferFanotifyCloseFDs {
	fanotifyBackend* b;
	~deferFanotifyCloseFDs() {
		b->closeFDs();
		b->signalEnded();
	}
};

gostd::Error fanotifyBackend::start() {
	if (::pipe2(pipeFDs, O_CLOEXEC | O_NONBLOCK) != 0) {
		return gostd::errorf("unable to open pipe: %w", {errnoError(errno)});
	}
	pipeWriteFD.store(pipeFDs[1]);
	deferFanotifyCloseFDs deferred{this};

	int fd = fanotifyInit(fanotifyInitFlags, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		return gostd::errorf("unable to initialize fanotify: %w",
		                     {errnoError(errno)});
	}
	fanotifyFD = fd;

	pollfd pollfds[2] = {
	    {pipeFDs[0], POLLIN, 0},
	    {fanotifyFD, POLLIN, 0},
	};

	notifyStarted();

	for (;;) {
		int r = ::poll(pollfds, 2, 500);
		if (r < 0) {
			if (errno == EINTR) {
				continue;
			}
			return gostd::errorf("unable to poll: %w", {errnoError(errno)});
		}
		if (pollfds[0].revents != 0) {
			break;
		}
		if (pollfds[1].revents != 0) {
			if (auto err = handleEvents(); err != nullptr) {
				return err;
			}
		}
	}

	return nullptr;
}

void fanotifyBackend::closeFDs() {
	std::lock_guard<std::mutex> lk(mu);
	if (pipeFDs[0] >= 0) {
		(void)::close(pipeFDs[0]);
		pipeFDs[0] = -1;
	}
	if (int fd = pipeWriteFD.exchange(-1); fd >= 0) {
		(void)::close(fd);
	}
	pipeFDs[1] = -1;
	if (fanotifyFD >= 0) {
		(void)::close(fanotifyFD);
		fanotifyFD = -1;
	}
}

void fanotifyBackend::signalEnded() {
	{
		std::lock_guard<std::mutex> lk(endedMu);
		ended = true;
	}
	endedCv.notify_all();
}

void fanotifyBackend::shutdown() {
	int fd = pipeWriteFD.load();
	if (fd < 0) {
		return;
	}
	(void)::write(fd, "X", 1);
	std::unique_lock<std::mutex> lk(endedMu);
	endedCv.wait(lk, [&] { return ended; });
}

gostd::Error
fanotifyBackend::subscribe(std::shared_ptr<fswatch::dirWatch> w) {
	// Probe FAN_RENAME on the first subscribe using the actual watch
	// directory. FAN_RENAME (Linux 5.17+) yields a single paired event
	// for renames; when unavailable we fall back to FAN_MOVED_FROM/
	// FAN_MOVED_TO which produces two separate events but is otherwise
	// equivalent. The kernel rejects unknown mask bits with EINVAL.
	if (markMask == 0) {
		if (noRename) {
			markMask = fanotifyMarkMaskMovedFromTo;
		} else {
			markMask = fanotifyMarkMaskRename;
			int r = fanotifyMark(fanotifyFD, fanotifyMarkAddFlags,
			                     fanotifyMarkMaskRename, AT_FDCWD,
			                     w->physicalDir.c_str());
			if (r == 0) {
				// B5: pair the probe Add with a matching Remove. If
				// Remove fails (rare; only EINTR or kernel resource
				// pressure realistically) we leave the probe mark
				// attached for the life of the process, but since
				// markDir below will Add the real mask with the same
				// flags the kernel just merges them. The probe is the
				// only failure path we explicitly retry.
				for (;;) {
					int rmErr = fanotifyMark(
					    fanotifyFD,
					    FAN_MARK_REMOVE | FAN_MARK_ONLYDIR,
					    fanotifyMarkMaskRename, AT_FDCWD,
					    w->physicalDir.c_str());
					if (rmErr == 0 || errno != EINTR) {
						break;
					}
				}
			} else if (errno == EINVAL || errno == EOPNOTSUPP) {
				markMask = fanotifyMarkMaskMovedFromTo;
			}
		}
	}
	if (!w->recursive) {
		if (auto err = markDir(w, w->dir, w->physicalDir); err != nullptr) {
			return std::make_shared<dirWatchErrorObj>(
			    gostd::errorf("fanotify_mark on '%s' failed: %w",
			                  {w->dir, err}),
			    w);
		}
		return nullptr;
	}
	if (auto err = walkDir(
	        w->physicalDir, true,
	        [&](const std::string& watchPath, bool isDir) -> gostd::Error {
		        if (!isDir) {
			        return nullptr;
		        }
		        std::string path = w->displayPath(watchPath);
		        if (auto merr = markDir(w, path, watchPath);
		            merr != nullptr) {
			        return std::make_shared<dirWatchErrorObj>(
			            gostd::errorf("fanotify_mark on '%s' failed: %w",
			                          {path, merr}),
			            w);
		        }
		        return nullptr;
	        });
	    err != nullptr) {
		(void)closeWatch(w);
		return err;
	}
	return nullptr;
}

gostd::Error fanotifyBackend::markDir(std::shared_ptr<fswatch::dirWatch> w,
                                      const std::string& path,
                                      const std::string& markPath) {
	if (fanotifyMark(fanotifyFD, fanotifyMarkAddFlags, markMask, AT_FDCWD,
	                 markPath.c_str()) != 0) {
		return maybeWrapUnsupportedFilesystem(errnoError(errno));
	}
	// unix.NameToHandleAt(AT_FDCWD, markPath, 0) — unix.NewFileHandle(128)
	// is MAX_HANDLE_SZ bytes of capacity.
	auto* handle = static_cast<struct file_handle*>(
	    std::malloc(sizeof(struct file_handle) + MAX_HANDLE_SZ));
	handle->handle_bytes = MAX_HANDLE_SZ;
	int mountId = 0;
	if (::name_to_handle_at(AT_FDCWD, markPath.c_str(), handle, &mountId,
	                        0) != 0) {
		int e = errno;
		std::free(handle);
		// Unmark since we can't track this directory without a handle.
		(void)fanotifyMark(fanotifyFD, FAN_MARK_REMOVE | FAN_MARK_ONLYDIR,
		                   markMask, AT_FDCWD, markPath.c_str());
		return maybeWrapUnsupportedFilesystem(
		    gostd::errorf("name_to_handle_at: %w", {errnoError(e)}));
	}
	struct statfs st;
	if (::statfs(markPath.c_str(), &st) != 0) {
		int e = errno;
		std::free(handle);
		(void)fanotifyMark(fanotifyFD, FAN_MARK_REMOVE | FAN_MARK_ONLYDIR,
		                   markMask, AT_FDCWD, markPath.c_str());
		return gostd::errorf("statfs: %w", {errnoError(e)});
	}
	fanotifyHandleKey key = makeFanotifyHandleKey(
	    {st.f_fsid.__val[0], st.f_fsid.__val[1]}, handle->handle_type,
	    std::string_view(
	        reinterpret_cast<const char*>(handle->f_handle),
	        handle->handle_bytes));
	std::free(handle);
	subscriptions[key].push_back(
	    new fanotifySubscription{path, markPath, w, key});
	return nullptr;
}

gostd::Error maybeWrapUnsupportedFilesystem(const gostd::Error& err) {
	if (errnoIs(err, EOPNOTSUPP) || errnoIs(err, ENOTSUP) ||
	    errnoIs(err, ENODEV)) {
		return gostd::errorf("%w: %w", {err, ErrFilesystemUnsupported});
	}
	return err;
}

// handleEvents reads and dispatches fanotify events from the fd.
gostd::Error fanotifyBackend::handleEvents() {
	auto& buf = readBuf;
	auto& watchersTouched = this->watchersTouched;

	for (;;) {
		ssize_t n = ::read(fanotifyFD, buf.data(), buf.size());
		if (n < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				break;
			}
			return gostd::errorf("Error reading from fanotify: %w",
			                     {errnoError(errno)});
		}
		if (n == 0) {
			break;
		}

		constexpr size_t metaSize = sizeof(struct fanotify_event_metadata);
		std::string_view data(buf.data(), static_cast<size_t>(n));
		while (data.size() >= metaSize) {
			auto* meta = reinterpret_cast<const struct fanotify_event_metadata*>(
			    data.data());
			if (meta->vers != FANOTIFY_METADATA_VERSION) {
				return gostd::errorf(
				    "unsupported fanotify metadata version: %d",
				    {gostd::fmtArg(int(meta->vers))});
			}
			size_t eventLen = meta->event_len;
			if (eventLen < meta->metadata_len || eventLen > data.size()) {
				break;
			}

			// FID mode: fd should be FAN_NOFD, but close if somehow set.
			if (meta->fd >= 0) {
				(void)::close(meta->fd);
			}

			if ((meta->mask & FAN_Q_OVERFLOW) != 0) {
				handleOverflow(watchersTouched);
				data = data.substr(eventLen);
				continue;
			}

			std::string_view infoData =
			    data.substr(meta->metadata_len,
			                eventLen - meta->metadata_len);
			auto [primary, renameTo] = parseFanotifyDfidNames(infoData);
			if ((meta->mask & FAN_RENAME) != 0) {
				if (primary || renameTo) {
					handleRenameEvent(meta->mask, primary.get(),
					                  renameTo.get(), watchersTouched);
				}
			} else if (primary) {
				handleParsedEvent(meta->mask, primary.get(),
				                  watchersTouched);
			}
			data = data.substr(eventLen);
		}
	}

	for (auto& w : watchersTouched) {
		w->notify();
	}
	watchersTouched.clear();
	return nullptr;
}

void fanotifyBackend::handleOverflow(
    std::unordered_set<std::shared_ptr<fswatch::dirWatch>>& touched) {
	std::lock_guard<std::mutex> lk(mu);
	std::unordered_set<fswatch::dirWatch*> seen;
	for (auto& kv : subscriptions) {
		for (auto* s : kv.second) {
			if (!seen.insert(s->dirWatch.get()).second) {
				continue;
			}
			s->dirWatch->events.setError(ErrOverflow);
			touched.insert(s->dirWatch);
		}
	}
}

void fanotifyBackend::handleRenameEvent(
    uint64_t mask, const fanotifyDfidName* dfidOld,
    const fanotifyDfidName* dfidNew,
    std::unordered_set<std::shared_ptr<fswatch::dirWatch>>& touched) {
	std::lock_guard<std::mutex> lk(mu);

	bool isDir = (mask & FAN_ONDIR) != 0;

	// Remove from old location.
	if (dfidOld != nullptr && !dfidOld->name.empty() &&
	    dfidOld->name != ".") {
		auto it = subscriptions.find(dfidOld->key);
		auto subs = it != subscriptions.end() ? it->second
		                                      : std::vector<fanotifySubscription*>{};
		for (auto* s : subs) {
			std::string oldPath = s->path + "/" + dfidOld->name;
			// If the renamed item is a dir, drop its subscriptions and
			// all descendant subscriptions. The kernel marks themselves
			// leak when the destination is outside our watched tree:
			// fanotify has no path-independent unmark and we don't
			// keep fds open for marked directories.
			if (isDir) {
				dropSubsForPathAndDescendantsLocked(oldPath);
			}
			s->dirWatch->events.remove(oldPath);
			touched.insert(s->dirWatch);
		}
	}

	// Create at new location.
	if (dfidNew != nullptr && !dfidNew->name.empty() &&
	    dfidNew->name != ".") {
		auto it = subscriptions.find(dfidNew->key);
		auto subs = it != subscriptions.end() ? it->second
		                                      : std::vector<fanotifySubscription*>{};
		for (auto* s : subs) {
			std::string newPath = s->path + "/" + dfidNew->name;
			s->dirWatch->events.create(newPath);
			if (isDir && s->dirWatch->recursive) {
				auto dw = s->dirWatch;
				(void)walkDir(
				    dw->physicalPath(newPath), true,
				    [&](const std::string& p, bool pIsDir)
				        -> gostd::Error {
					    if (!pIsDir) {
						    return nullptr;
					    }
					    (void)markDir(dw, dw->displayPath(p), p);
					    return nullptr;
				    });
			}
			touched.insert(s->dirWatch);
		}
	}
}

void fanotifyBackend::handleParsedEvent(
    uint64_t mask, const fanotifyDfidName* dfid,
    std::unordered_set<std::shared_ptr<fswatch::dirWatch>>& touched) {
	std::lock_guard<std::mutex> lk(mu);

	// b.subscriptions[key] holds at most one entry per
	// fanotifySubscription pointer (markDir always appends a fresh
	// struct), so no dedup is necessary. Snapshot: handleSubscription's
	// drop paths may delete the key's list (and sub objects) being
	// iterated — Go's GC keeps them alive for the range.
	auto it = subscriptions.find(dfid->key);
	if (it == subscriptions.end()) {
		return;
	}
	auto subs = it->second;
	for (auto* s : subs) {
		auto dw = s->dirWatch;
		if (handleSubscription(mask, dfid, s)) {
			touched.insert(std::move(dw));
		}
	}
}

bool fanotifyBackend::handleSubscription(uint64_t mask,
                                         const fanotifyDfidName* dfid,
                                         fanotifySubscription* sub) {
	// Hold the dirWatch: the drop paths below may delete sub itself.
	auto w = sub->dirWatch;

	// Compute full path. Self-events (name empty or ".") use the
	// watch path directly.
	bool isSelfEvent = dfid->name.empty() || dfid->name == ".";
	std::string path = sub->path;
	if (!isSelfEvent) {
		path = sub->path + "/" + dfid->name;
	}

	bool isDir = (mask & FAN_ONDIR) != 0;
	bool touched = false;

	bool hasDelete = (mask & (FAN_DELETE | FAN_MOVED_FROM)) != 0;
	bool hasCreate = (mask & (FAN_CREATE | FAN_MOVED_TO)) != 0;

	// Fanotify can merge consecutive events on the same object into a
	// single event with multiple mask bits. When both create and delete
	// bits are set, we can't tell the temporal order from the mask alone.
	// Stat the path: if it exists, the last op was create (delete→create
	// = "update"); if gone, the last op was delete (create→delete =
	// cancel out).
	if (hasCreate && hasDelete && !isSelfEvent) {
		struct stat st;
		if (::lstat(path.c_str(), &st) != 0) {
			// File was created then deleted: record both so they
			// cancel.
			w->events.create(path);
			w->events.remove(path);
			return true;
		}
		// File exists: was deleted then recreated. Fall through to the
		// normal delete-first processing which produces "update".
	}

	// Process delete/move-from FIRST so that a merged DELETE+CREATE
	// coalesces to "update" via the eventList's rapid-recreate logic.
	if ((mask &
	     (FAN_DELETE | FAN_DELETE_SELF | FAN_MOVED_FROM | FAN_MOVE_SELF)) !=
	    0) {
		bool isSelfMask = (mask & (FAN_DELETE_SELF | FAN_MOVE_SELF)) != 0;
		// Ignore delete/move self events unless this is the watch root.
		if (!(isSelfMask && path != w->dir)) {
			// If the deleted/moved item is a dir, drop subscriptions
			// for both the path itself and every descendant; otherwise
			// later events for the (now-moved) inodes would be reported
			// against stale paths. For FAN_MOVED_FROM that takes the
			// inode out of our watched tree the kernel mark on the
			// inode itself unfortunately leaks: fanotify has no
			// path-independent way to unmark and the destination is
			// outside everything we can resolve.
			// Self events may not have FAN_ONDIR set (like inotify).
			if (isSelfMask || isDir) {
				dropSubsForPathAndDescendantsLocked(path);
			} else {
				dropSubsForPathLocked(path);
			}
			w->events.remove(path);
			touched = true;
			// Root-of-watch deletion: the kernel has dropped the mark.
			// Surface ErrWatchTerminated alongside the delete so callers
			// know to clean up; no more events will arrive for w.
			if (isSelfMask && path == w->dir) {
				w->events.setError(gostd::errorf(
				    "%w: watched directory removed",
				    {ErrWatchTerminated}));
			}
		}
	}

	if (hasCreate) {
		w->events.create(path);
		if (isDir && w->recursive) {
			(void)walkDir(w->physicalPath(path), true,
			              [&](const std::string& p, bool pIsDir)
			                  -> gostd::Error {
				              if (!pIsDir) {
					              return nullptr;
				              }
				              (void)markDir(w, w->displayPath(p), p);
				              return nullptr;
			              });
		}
		touched = true;
	}

	if ((mask & FAN_MODIFY) != 0) {
		w->events.update(path);
		touched = true;
	}

	return touched;
}

// Native-endian reads of the fanotify wire format.
uint16_t u16Native(const char* p) {
	uint16_t v;
	std::memcpy(&v, p, sizeof(v));
	return v;
}
uint32_t u32Native(const char* p) {
	uint32_t v;
	std::memcpy(&v, p, sizeof(v));
	return v;
}
int32_t i32Native(const char* p) {
	int32_t v;
	std::memcpy(&v, p, sizeof(v));
	return v;
}

// parseFanotifyDfidNames extracts DFID_NAME info records from the event's
// info record area. Returns a primary record (DFID_NAME or OLD_DFID_NAME)
// and an optional second record (NEW_DFID_NAME, for FAN_RENAME events).
std::pair<std::shared_ptr<fanotifyDfidName>,
          std::shared_ptr<fanotifyDfidName>>
parseFanotifyDfidNames(std::string_view data) {
	constexpr size_t infoHdrSize = 4; // fanotify_event_info_header
	std::shared_ptr<fanotifyDfidName> primary, rename;
	size_t offset = 0;
	while (offset + infoHdrSize <= data.size()) {
		uint8_t infoType = static_cast<uint8_t>(data[offset]);
		size_t infoLen = u16Native(data.data() + offset + 2);
		if (infoLen < infoHdrSize || offset + infoLen > data.size()) {
			break;
		}

		switch (infoType) {
		case FAN_EVENT_INFO_TYPE_DFID_NAME:
		case FAN_EVENT_INFO_TYPE_OLD_DFID_NAME:
			if (auto parsed = parseFanotifyFidRecord(
			        data.substr(offset, infoLen), true);
			    parsed != nullptr) {
				primary = std::move(parsed);
			}
			break;
		case FAN_EVENT_INFO_TYPE_NEW_DFID_NAME:
			if (auto parsed = parseFanotifyFidRecord(
			        data.substr(offset, infoLen), true);
			    parsed != nullptr) {
				rename = std::move(parsed);
			}
			break;
		case FAN_EVENT_INFO_TYPE_DFID:
			// DFID without name: the handle identifies the directory
			// itself. Use as fallback if we haven't found a DFID_NAME
			// record.
			if (primary == nullptr) {
				if (auto parsed = parseFanotifyFidRecord(
				        data.substr(offset, infoLen), false);
				    parsed != nullptr) {
					primary = std::move(parsed);
				}
			}
			break;
		}

		if (primary != nullptr && rename != nullptr) {
			return {primary, rename};
		}
		offset += infoLen;
	}
	return {primary, rename};
}

// parseFanotifyFidRecord parses a single fanotify_event_info_fid record.
std::shared_ptr<fanotifyDfidName>
parseFanotifyFidRecord(std::string_view data, bool hasName) {
	constexpr size_t infoHdrSize = 4;
	constexpr size_t fsidSize = 8;
	constexpr size_t fhHdrSize = 8;
	constexpr size_t minSize = infoHdrSize + fsidSize + fhHdrSize;
	if (data.size() < minSize) {
		return nullptr;
	}
	std::string_view body = data.substr(infoHdrSize);

	std::array<int32_t, 2> fsid;
	fsid[0] = i32Native(body.data() + 0);
	fsid[1] = i32Native(body.data() + 4);

	size_t handleBytes = u32Native(body.data() + 8);
	int32_t handleType = i32Native(body.data() + 12);

	size_t handleStart = fsidSize + fhHdrSize;
	if (handleStart + handleBytes > body.size()) {
		return nullptr;
	}
	std::string_view handleData =
	    body.substr(handleStart, handleBytes);
	fanotifyHandleKey key =
	    makeFanotifyHandleKey(fsid, handleType, handleData);

	std::string name;
	if (hasName) {
		size_t nameStart = handleStart + handleBytes;
		if (nameStart < body.size()) {
			std::string_view nameData = body.substr(nameStart);
			size_t nul = nameData.find('\0');
			if (nul != std::string_view::npos) {
				nameData = nameData.substr(0, nul);
			}
			name = std::string(nameData);
		}
	}

	return std::make_shared<fanotifyDfidName>(
	    fanotifyDfidName{key, std::move(name)});
}

// dropSubsForPathLocked removes every subscription whose s.path equals
// path, regardless of which fanotify handle key it lives under. Must be
// called with b.mu held.
void fanotifyBackend::dropSubsForPathLocked(const std::string& path) {
	for (auto it = subscriptions.begin(); it != subscriptions.end();) {
		auto& list = it->second;
		size_t kept = 0;
		for (size_t i = 0; i < list.size(); i++) {
			fanotifySubscription* s = list[i];
			if (s->path == path) {
				delete s;
				continue;
			}
			list[kept++] = s;
		}
		if (kept == 0) {
			it = subscriptions.erase(it);
		} else {
			list.resize(kept);
			++it;
		}
	}
}

// dropSubsForPathAndDescendantsLocked removes every subscription whose
// s.path equals path or lives strictly under path. The kernel mark on
// the moved-out inode itself remains active (fanotify provides no
// path-independent unmark) but dropping the bookkeeping prevents later
// events from being reported against the no-longer-valid path.
// Must be called with b.mu held.
void fanotifyBackend::dropSubsForPathAndDescendantsLocked(
    const std::string& path) {
	for (auto it = subscriptions.begin(); it != subscriptions.end();) {
		auto& list = it->second;
		size_t kept = 0;
		for (size_t i = 0; i < list.size(); i++) {
			fanotifySubscription* s = list[i];
			if (s->path == path ||
			    (s->path.size() > path.size() &&
			     s->path[path.size()] == '/' &&
			     s->path.compare(0, path.size(), path) == 0)) {
				delete s;
				continue;
			}
			list[kept++] = s;
		}
		if (kept == 0) {
			it = subscriptions.erase(it);
		} else {
			list.resize(kept);
			++it;
		}
	}
}

gostd::Error
fanotifyBackend::closeWatch(std::shared_ptr<fswatch::dirWatch> w) {
	for (auto it = subscriptions.begin(); it != subscriptions.end();) {
		auto& list = it->second;
		size_t kept = 0;
		bool removedAny = false;
		std::string removedPath;
		for (size_t i = 0; i < list.size(); i++) {
			fanotifySubscription* s = list[i];
			if (s->dirWatch == w) {
				removedAny = true;
				removedPath = s->watchPath;
				delete s;
				continue;
			}
			list[kept++] = s;
		}
		if (!removedAny) {
			++it;
			continue;
		}
		if (kept == 0) {
			// Try to unmark. Skip the call entirely when markMask is
			// still 0 (closeWatch racing with a shutdown that happened
			// before subscribe ever set markMask); fanotify_mark with
			// mask=0 is undocumented. Ignore ENOENT (directory may have
			// been deleted) and EBADF (fanotify fd may already be
			// closed during shutdown).
			if (markMask != 0) {
				(void)fanotifyMark(fanotifyFD, FAN_MARK_REMOVE, markMask,
				                   AT_FDCWD, removedPath.c_str());
			}
			it = subscriptions.erase(it);
		} else {
			list.resize(kept);
			++it;
		}
	}
	return nullptr;
}

} // namespace

// Go var fanotifyWatcher + init() (watcher.go:182-186, watcher.go:193).
watcher& fanotifyWatcher() {
	static watcher w{"fanotify"};
	static bool registered = [] {
		if (fanotifyAvailable()) {
			w.factory =
			    []() -> watcherImpl* { return newFanotifyBackend(false); };
		}
		return true;
	}();
	(void)registered;
	return w;
}

} // namespace tsc::fswatch
