// inotify_linux.go — port of tsc/internal/fswatch/inotify_linux.go: the
// Linux inotify(7) backend.
//
// Uses the kernel's inotify(7) subsystem to watch directory trees. A single
// inotify instance serves all subscriptions for the process lifetime.
//
//	┌───────────────────────────────────────────────────────────┐
//	│                    inotifyBackend                         │
//	│                                                           │
//	│  ┌───────────┐        poll(2)        ┌─────────────────┐  │
//	│  │ pipe[0]   ├──────────────────────►│                 │  │
//	│  │ (wakeup)  │                       │  start()        │  │
//	│  └───────────┘                       │  thread         │  │
//	│  ┌───────────┐                       │  (event loop)   │  │
//	│  │ inotify   ├──────────────────────►│                 │  │
//	│  │ fd        │                       └────────┬────────┘  │
//	│  └───────────┘                                │           │
//	│                                      handleEvents()       │
//	│                                               │           │
//	│                                               ▼           │
//	│                               ┌─────────────────────────┐ │
//	│                               │ subscriptions           │ │
//	│                               │ map[wd] → []sub         │ │
//	│                               │  sub.dirWatch.events    │ │
//	│                               └─────────────────────────┘ │
//	└───────────────────────────────────────────────────────────┘
//
// Goroutines and threading:
//   - One long-lived thread (start), launched by watcherBase.run(). It
//     owns the poll(2) loop and runs for the process lifetime. All event
//     reading and dispatch (handleEvents, handleEvent, handleSubscription)
//     execute on this thread, under b.mu.
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
// WatchDirectory flow:
//  1. Walk the target directory (caller thread).
//  2. For every directory found, call inotify_add_watch to obtain a
//     watch descriptor (wd). Map wd → inotifySubscription.
//
// Event dispatch (handleEvents → handleSubscription, on start thread):
//   - IN_CREATE / IN_MOVED_TO  → events.create (→ EventUpdate); if the new
//     entry is a directory (IN_ISDIR), recursively walk and watch it.
//   - IN_MODIFY                → events.update.
//   - IN_DELETE* / IN_MOVE*    → events.remove; drop inotify subscriptions
//     for the removed path and any descendants.
//   - IN_Q_OVERFLOW            → set ErrOverflow on every active dirWatch.
//   After processing all buffered events, call dirWatch.notify() on each
//   touched dirWatch to trigger the debouncer.
//
// Shutdown:
//   Write a byte to pipe[1] → poll sees POLLIN on pipe[0] → loop exits →
//   deferred closeFDs closes inotify fd, pipe fds, and signals endedSignal.

#ifdef __linux__
#ifndef _GNU_SOURCE
#define _GNU_SOURCE // pipe2
#endif

#include "internal/fswatch/fswatch.h"

#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <atomic>

namespace tsc::fswatch {

namespace {

constexpr uint32_t inotifyMask = IN_CREATE | IN_DELETE | IN_DELETE_SELF |
    IN_MODIFY | IN_MOVE_SELF | IN_MOVED_FROM | IN_MOVED_TO |
    IN_DONT_FOLLOW | IN_ONLYDIR | IN_EXCL_UNLINK;
constexpr size_t inotifyBufferSize = 8192;

// inotifySubscription.
struct inotifySubscription {
	std::string path;
	std::string watchPath;
	std::shared_ptr<fswatch::dirWatch> dirWatch;
	int wd;
};

// inotifyBackend.
struct inotifyBackend : watcherBase {
	int pipeFDs[2];
	// pipeWriteFD shadows pipeFDs[1] as an atomic so shutdown (any thread)
	// can safely race against the start thread's deferred closeFDs.
	// Sentinel -1 once closed.
	std::atomic<int32_t> pipeWriteFD{-1};
	int inotify;
	std::unordered_map<int, std::vector<inotifySubscription*>>
	    subscriptions; // multimap<wd, sub>

	// endedSignal chan struct{} — a flag + condvar.
	std::mutex endedMu;
	std::condition_variable endedCv;
	bool ended = false;

	// Persistent buffers reused across handleEvents calls. Only accessed
	// from the start thread, so no synchronization needed.
	std::vector<char> readBuf;
	std::unordered_set<std::shared_ptr<fswatch::dirWatch>> watchersTouched;

	inotifyBackend();
	gostd::Error start() override;
	void closeFDs();
	void signalEnded();
	void shutdown() override;
	gostd::Error subscribe(std::shared_ptr<fswatch::dirWatch> w) override;
	std::pair<int, gostd::Error>
	watchDir(std::shared_ptr<fswatch::dirWatch> w, const std::string& path,
	         const std::string& watchPath);
	gostd::Error handleEvents();
	void handleEvent(const inotify_event* ev, const std::string& name,
	                 std::unordered_set<std::shared_ptr<fswatch::dirWatch>>&
	                     touched);
	bool handleSubscription(const inotify_event* ev, const std::string& name,
	                        inotifySubscription* sub);
	gostd::Error closeWatch(std::shared_ptr<fswatch::dirWatch> w) override;
};

inotifyBackend* newInotifyBackend() {
	return new inotifyBackend();
}

inotifyBackend::inotifyBackend() {
	pipeFDs[0] = -1;
	pipeFDs[1] = -1;
	inotify = -1;
	readBuf.resize(inotifyBufferSize);
	pipeWriteFD.store(-1);
	watcherBase::init();
}

// defer func() { b.closeFDs(); close(b.endedSignal) }()
struct deferCloseFDs {
	inotifyBackend* b;
	~deferCloseFDs() {
		b->closeFDs();
		b->signalEnded();
	}
};

// start mirrors `inotifyBackend::start`.
gostd::Error inotifyBackend::start() {
	// Create a pipe so we can wake the poll(2) loop on shutdown.
	if (::pipe2(pipeFDs, O_CLOEXEC | O_NONBLOCK) != 0) {
		return gostd::errorf("unable to open pipe: %w", {errnoError(errno)});
	}
	pipeWriteFD.store(pipeFDs[1]);
	deferCloseFDs deferred{this};
	int fd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (fd < 0) {
		return gostd::errorf("unable to initialize inotify: %w",
		                     {errnoError(errno)});
	}
	inotify = fd;

	pollfd pollfds[2] = {
	    {pipeFDs[0], POLLIN, 0},
	    {inotify, POLLIN, 0},
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

// closeFDs runs in the start thread after the poll loop exits. Takes
// b.mu so the writes to b.inotify / b.pipeFDs synchronize-against the
// reads in closeWatch / subscribe (both of which run under b.mu).
void inotifyBackend::closeFDs() {
	std::lock_guard<std::mutex> lk(mu);
	if (pipeFDs[0] >= 0) {
		(void)::close(pipeFDs[0]);
		pipeFDs[0] = -1;
	}
	if (int fd = pipeWriteFD.exchange(-1); fd >= 0) {
		(void)::close(fd);
	}
	pipeFDs[1] = -1;
	if (inotify >= 0) {
		(void)::close(inotify);
		inotify = -1;
	}
}

void inotifyBackend::signalEnded() {
	{
		std::lock_guard<std::mutex> lk(endedMu);
		ended = true;
	}
	endedCv.notify_all();
}

// shutdown is the equivalent of the destructor's pipe-write+wait.
// Called by removeSharedBackend when the last watch drops. Reads
// the pipe write fd via atomic so it's safe to race against the start
// thread's deferred closeFDs.
void inotifyBackend::shutdown() {
	int fd = pipeWriteFD.load();
	if (fd < 0) {
		return;
	}
	(void)::write(fd, "X", 1);
	std::unique_lock<std::mutex> lk(endedMu);
	endedCv.wait(lk, [&] { return ended; });
}

// subscribe mirrors `inotifyBackend::subscribe`. Called via the watcherBase
// virtual dispatch under b.mu (so it's serialized against handleEvent).
gostd::Error
inotifyBackend::subscribe(std::shared_ptr<fswatch::dirWatch> w) {
	if (!w->recursive) {
		if (auto [wd, err] = watchDir(w, w->dir, w->physicalDir);
		    err != nullptr) {
			return std::make_shared<dirWatchErrorObj>(
			    gostd::errorf("inotify_add_watch on '%s' failed: %w",
			                  {w->dir, err}),
			    w);
		}
		return nullptr;
	}
	if (auto err =
	        walkDir(w->physicalDir, true,
	                [&](const std::string& watchPath, bool isDir)
	                    -> gostd::Error {
		                if (!isDir) {
			                return nullptr;
		                }
		                std::string path = w->displayPath(watchPath);
		                if (auto [wd, werr] = watchDir(w, path, watchPath);
		                    werr != nullptr) {
			                return std::make_shared<dirWatchErrorObj>(
			                    gostd::errorf(
			                        "inotify_add_watch on '%s' failed: %w",
			                        {path, werr}),
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

// watchDir registers an inotify watch on path and records the resulting
// subscription. Returns the kernel watch descriptor on success.
std::pair<int, gostd::Error>
inotifyBackend::watchDir(std::shared_ptr<fswatch::dirWatch> w,
                         const std::string& path,
                         const std::string& watchPath) {
	int wd = ::inotify_add_watch(inotify, watchPath.c_str(), inotifyMask);
	if (wd < 0) {
		return {0, errnoError(errno)};
	}
	subscriptions[wd].push_back(
	    new inotifySubscription{path, watchPath, w, wd});
	return {wd, nullptr};
}

// handleEvents mirrors `inotifyBackend::handleEvents`.
gostd::Error inotifyBackend::handleEvents() {
	auto& buf = readBuf;
	auto& watchersTouched = this->watchersTouched;

	for (;;) {
		ssize_t n = ::read(inotify, buf.data(), buf.size());
		if (n < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				break;
			}
			return gostd::errorf("Error reading from inotify: %w",
			                     {errnoError(errno)});
		}
		if (n == 0) {
			break;
		}
		// Walk the buffer.
		for (size_t offset = 0; offset < static_cast<size_t>(n);) {
			auto* ev = reinterpret_cast<const inotify_event*>(buf.data() +
			                                                  offset);
			size_t recordSize = sizeof(inotify_event) + ev->len;
			std::string name;
			if (ev->len > 0) {
				// Name is NUL-terminated; trim trailing zeros.
				std::string_view nameBytes(
				    buf.data() + offset + sizeof(inotify_event),
				    recordSize - sizeof(inotify_event));
				size_t nul = nameBytes.find('\0');
				if (nul != std::string_view::npos) {
					nameBytes = nameBytes.substr(0, nul);
				}
				name = std::string(nameBytes);
			}

			if ((ev->mask & IN_Q_OVERFLOW) != 0) {
				std::lock_guard<std::mutex> lk(mu);
				for (auto& kv : this->subscriptions) {
					for (auto* sub : kv.second) {
						sub->dirWatch->events.setError(ErrOverflow);
						watchersTouched.insert(sub->dirWatch);
					}
				}
				offset += recordSize;
				continue;
			}

			handleEvent(ev, name, watchersTouched);
			offset += recordSize;
		}
	}
	for (auto& w : watchersTouched) {
		w->notify();
	}
	watchersTouched.clear();
	return nullptr;
}

// handleEvent mirrors `inotifyBackend::handleEvent`.
void inotifyBackend::handleEvent(
    const inotify_event* ev, const std::string& name,
    std::unordered_set<std::shared_ptr<fswatch::dirWatch>>& touched) {
	std::lock_guard<std::mutex> lk(mu);

	// b.subscriptions[wd] holds at most one entry per inotifySubscription
	// pointer (watchDir always appends a fresh struct), so no dedup is
	// necessary; the upstream C++ used an unordered_set keyed by
	// shared_ptr identity but the equivalent Go invariant is structural.
	//
	// Snapshot the sub list and hold each dirWatch: handleSubscription's
	// drop path can delete the wd entry (and the sub objects) it is
	// iterating — Go's GC keeps them alive for the range.
	auto it = this->subscriptions.find(ev->wd);
	if (it == this->subscriptions.end()) {
		return;
	}
	auto subs = it->second;
	for (auto* s : subs) {
		auto dw = s->dirWatch;
		if (handleSubscription(ev, name, s)) {
			touched.insert(std::move(dw));
		}
	}
}

// handleSubscription mirrors `inotifyBackend::handleSubscription`.
bool inotifyBackend::handleSubscription(const inotify_event* ev,
                                        const std::string& name,
                                        inotifySubscription* sub) {
	// Hold the dirWatch: the drop path below may delete sub itself.
	auto w = sub->dirWatch;
	std::string path = sub->path;
	std::string watchPath = sub->watchPath;
	bool isDir = (ev->mask & IN_ISDIR) != 0;
	if (!name.empty()) {
		path = path + "/" + name;
		watchPath = watchPath + "/" + name;
	}

	if ((ev->mask & (IN_CREATE | IN_MOVED_TO)) != 0) {
		w->events.create(path);
		if (isDir && w->recursive) {
			(void)walkDir(watchPath, true,
			              [&](const std::string& p, bool pIsDir)
			                  -> gostd::Error {
				              if (!pIsDir) {
					              return nullptr;
				              }
				              (void)watchDir(w, w->displayPath(p), p);
				              return nullptr;
			              });
		}
	} else if ((ev->mask & IN_MODIFY) != 0) {
		w->events.update(path);
	} else if ((ev->mask &
	            (IN_DELETE | IN_DELETE_SELF | IN_MOVED_FROM | IN_MOVE_SELF)) !=
	           0) {
		bool isSelfEvent = (ev->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) != 0;
		// Ignore delete/move self events unless this is the watch root.
		if (isSelfEvent && path != w->dir) {
			return false;
		}
		// If deleted item is a dir, drop matching subscriptions.
		// XXX: self events don't have IN_ISDIR set.
		if (isSelfEvent || isDir) {
			for (auto it = this->subscriptions.begin();
			     it != this->subscriptions.end();) {
				auto& list = it->second;
				size_t kept = 0;
				for (size_t i = 0; i < list.size(); i++) {
					inotifySubscription* s = list[i];
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
					(void)::inotify_rm_watch(inotify, it->first);
					it = this->subscriptions.erase(it);
				} else {
					list.resize(kept);
					++it;
				}
			}
		}
		w->events.remove(path);
		// If the watched root itself is gone the kernel has already
		// auto-removed every wd associated with this dirWatch and no
		// further events will fire. Surface ErrWatchTerminated so the
		// caller knows to clean up; the delete event above still
		// flows through the same callback.
		if (isSelfEvent && path == w->dir) {
			w->events.setError(gostd::errorf(
			    "%w: watched directory removed", {ErrWatchTerminated}));
		}
	}
	return true;
}

// closeWatch mirrors `inotifyBackend::closeWatch`. Iterates every wd that
// referenced w and removes the matching subscriptions. If a kernel
// InotifyRmWatch fails we keep processing remaining wds and return the
// first error encountered; bailing early would leave the internal state
// half-cleaned and the caller's dirWatch hanging off other wds.
gostd::Error
inotifyBackend::closeWatch(std::shared_ptr<fswatch::dirWatch> w) {
	gostd::Error firstErr;
	for (auto it = subscriptions.begin(); it != subscriptions.end();) {
		auto& list = it->second;
		size_t kept = 0;
		bool removedAny = false;
		for (size_t i = 0; i < list.size(); i++) {
			inotifySubscription* s = list[i];
			if (s->dirWatch == w) {
				removedAny = true;
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
			if (::inotify_rm_watch(inotify, it->first) != 0 &&
			    firstErr == nullptr) {
				firstErr = std::make_shared<dirWatchErrorObj>(
				    gostd::errorf("unable to remove dirWatch: %w",
				                  {errnoError(errno)}),
				    w);
			}
			it = subscriptions.erase(it);
		} else {
			list.resize(kept);
			++it;
		}
	}
	return firstErr;
}

} // namespace

// Go var inotifyWatcher + init(): the package-level watcher whose factory is
// set by the platform's init() (watcher.go:117-119, watcher.go:189).
watcher& inotifyWatcher() {
	static watcher w{"inotify"};
	static bool registered = [] {
		w.factory = []() -> watcherImpl* { return newInotifyBackend(); };
		return true;
	}();
	(void)registered;
	return w;
}

} // namespace tsc::fswatch

#endif // __linux__
