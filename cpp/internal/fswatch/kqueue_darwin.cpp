// kqueue.go — the kqueue watcher backend used on macOS (and the BSDs).
// Uses the kernel's kqueue/kevent mechanism to watch individual files and
// directories via EVFILT_VNODE. Unlike inotify, kqueue requires an open
// file descriptor per watched path, not just per directory. On macOS,
// O_EVTONLY opens files for event notification without granting read
// access.
//
//	structure:
//	│  ┌───────────┐       kevent(2)        ┌──────────────────┐   │
//	│  │ watched   │ ────────────────────▶ │  kqueue fd       │   │
//	│  │ files/dirs│                       │  (event loop)    │   │
//	│  └───────────┘                       └──────────────────┘   │
//
//   - subscribe builds an entries map per dirWatch (path → dirEntry),
//     opens every path with O_EVTONLY, and registers each fd with the
//     kqueue for EVFILT_VNODE events (NOTE_DELETE, NOTE_WRITE,
//     NOTE_EXTEND, NOTE_ATTRIB, NOTE_RENAME, NOTE_REVOKE). Store the
//     fd↔dirEntry mapping.
//   - NOTE_WRITE on a directory → compareDir: re-read the directory from
//     disk, diff against the entries map, emit create/remove.
//   - NOTE_DELETE / NOTE_RENAME / NOTE_REVOKE → close the stale fd. For a
//     pure NOTE_DELETE on a file, tryRewatchLocked checks whether the
//     path was recreated with the same type and emits update instead.
//   - NOTE_WRITE / NOTE_ATTRIB / NOTE_EXTEND on a file → emit update.
//   - After processing all returned kevents, call dirWatch.notify() on
//     each touched watcher.
//   - Shutdown: write a byte to pipe[1] → kevent sees the pipe fd →
//     loop exits → deferred closeFDs/closeSubscriptions run.

#ifdef __APPLE__

#include "internal/fswatch/fswatch.h"

#include <fcntl.h>
#include <sys/event.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tsc::fswatch {

namespace {

// openForEvents — kqueue.go:88. On darwin, O_EVTONLY opens the file for
// event notification without granting read access.
int openForEvents(const std::string& path) {
	return ::open(path.c_str(), O_EVTONLY);
}

// dirEntry — kqueue.go:97. `fd == -1` is Go's `state == nil`.
struct dirEntry {
	std::string path;
	std::string watchPath;
	bool isDir = false;
	int fd = -1;
};

using entriesMap = std::unordered_map<std::string, dirEntry>;

// kqueueSubscription — kqueue.go:105.
struct kqueueSubscription {
	std::shared_ptr<dirWatch> dirWatchPtr;
	std::string path;
	std::shared_ptr<entriesMap> entries;
	int fd = -1;
};

// readEntries — kqueue.go:741.
std::pair<std::vector<osDirEntry>, gostd::Error>
readEntries(const std::string& path) {
	return osReadDir(path);
}

// removeEntryAndDescendants — kqueue.go:761.
void removeEntryAndDescendants(entriesMap& entries, const std::string& path) {
	entries.erase(path);
	for (auto it = entries.begin(); it != entries.end();) {
		const std::string& k = it->first;
		if (k.size() > path.size() && k[path.size()] == '/' &&
		    k.compare(0, path.size(), path) == 0) {
			it = entries.erase(it);
		} else {
			++it;
		}
	}
}

constexpr uint32_t vnodeFFlags = NOTE_DELETE | NOTE_WRITE | NOTE_EXTEND |
    NOTE_ATTRIB | NOTE_RENAME | NOTE_REVOKE;

// kqueueBackend — kqueue.go:114.
struct kqueueBackend : watcherBase {
	std::mutex kqMu; // local lock for kqueue-specific maps
	int kq = -1;
	int pipeFDs[2] = {-1, -1};
	std::atomic<int32_t> pipeWriteFD{-1};
	std::unordered_map<std::string,
	                   std::vector<std::shared_ptr<kqueueSubscription>>>
	    subsByPath;
	std::unordered_map<int, dirEntry*> fdToEntry;

	std::mutex endedMu;
	std::condition_variable endedCv;
	bool ended = false;

	// Persistent buffer reused across event batches. Only accessed
	// from the start thread, so no synchronization needed.
	std::unordered_set<dirWatch*> watchersTouched;

	kqueueBackend() { init(); }

	// start — kqueue.go:151.
	gostd::Error start() override {
		kq = ::kqueue();
		if (kq < 0) {
			return gostd::errorf("unable to open kqueue: %w",
			                     {errnoError(errno)});
		}
		struct cleanup {
			kqueueBackend* b;
			~cleanup() {
				b->closeSubscriptions();
				b->closeFDs();
				{
					std::lock_guard<std::mutex> lk(b->endedMu);
					b->ended = true;
				}
				b->endedCv.notify_all();
			}
		} deferred{this};

		if (::pipe(pipeFDs) != 0) {
			return gostd::errorf("unable to open pipe: %w",
			                     {errnoError(errno)});
		}
		(void)::fcntl(pipeFDs[0], F_SETFD,
		              ::fcntl(pipeFDs[0], F_GETFD) | FD_CLOEXEC);
		(void)::fcntl(pipeFDs[1], F_SETFD,
		              ::fcntl(pipeFDs[1], F_GETFD) | FD_CLOEXEC);
		pipeWriteFD.store(pipeFDs[1]);

		// Register the pipe read end so shutdown can break the loop.
		struct kevent pipeEv;
		EV_SET(&pipeEv, pipeFDs[0], EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0,
		       nullptr);
		if (::kevent(kq, &pipeEv, 1, nullptr, 0, nullptr) < 0) {
			return gostd::errorf("unable to watch pipe: %w",
			                     {errnoError(errno)});
		}

		notifyStarted();

		struct kevent events[128];
		for (;;) {
			int n = ::kevent(kq, nullptr, 0, events, 128, nullptr);
			if (n < 0) {
				if (errno == EINTR) {
					continue;
				}
				return gostd::errorf("kevent error: %w",
				                     {errnoError(errno)});
			}

			auto& touched = watchersTouched;
			bool stop = false;
			for (int i = 0; i < n; i++) {
				uint32_t fflags = events[i].fflags;
				uint32_t flags = events[i].flags;
				int fd = static_cast<int>(events[i].ident);
				if (fd == pipeFDs[0]) {
					stop = true;
					break;
				}

				// EV_ERROR indicates kevent couldn't apply a changelist
				// entry or that the kernel rejected the registration.
				// Data carries the errno. Skip dispatching as a normal
				// event since fflags are not meaningful in this case.
				if ((flags & EV_ERROR) != 0) {
					continue;
				}

				dirEntry* entry;
				{
					std::lock_guard<std::mutex> lk(kqMu);
					auto it = fdToEntry.find(fd);
					if (it == fdToEntry.end()) {
						continue;
					}
					entry = it->second;
				}

				if ((fflags & NOTE_WRITE) != 0 && entry->isDir) {
					compareDir(fd, entry->path, touched);
					// NOTE_WRITE on a dir already ran compareDir above.
					// On DragonFlyBSD, rename-over coalesces NOTE_DELETE
					// with NOTE_WRITE on the parent directory (rather
					// than firing NOTE_DELETE on the replaced file's fd).
					// Skip handleFileEvent so we don't misinterpret the
					// coalesced NOTE_DELETE as the directory itself being
					// removed.
					fflags &= ~NOTE_DELETE;
				}
				if ((fflags & ~NOTE_WRITE) != 0 || !entry->isDir) {
					handleFileEvent(fflags, entry, touched);
				}
			}

			for (dirWatch* w : touched) {
				w->notify();
			}
			touched.clear();
			if (stop) {
				break;
			}
		}
		return nullptr;
	}

	void closeFDs() {
		if (pipeFDs[0] >= 0) {
			(void)::close(pipeFDs[0]);
			pipeFDs[0] = -1;
		}
		if (int fd = pipeWriteFD.exchange(-1); fd >= 0) {
			(void)::close(fd);
		}
		pipeFDs[1] = -1;
		if (kq >= 0) {
			(void)::close(kq);
			kq = -1;
		}
	}

	void closeSubscriptions() {
		std::lock_guard<std::mutex> lk(kqMu);
		std::unordered_set<int> seenFDs;
		for (auto& [path, list] : subsByPath) {
			for (auto& sub : list) {
				if (sub->fd < 0) {
					continue;
				}
				if (!seenFDs.insert(sub->fd).second) {
					continue;
				}
				(void)::close(sub->fd);
			}
		}
		subsByPath.clear();
		fdToEntry.clear();
	}

	// shutdown — kqueue.go:278.
	void shutdown() override {
		int fd = pipeWriteFD.load();
		if (fd < 0) {
			return;
		}
		(void)::write(fd, "X", 1);
		std::unique_lock<std::mutex> lk(endedMu);
		endedCv.wait(lk, [&] { return ended; });
	}

	// handleFileEvent — kqueue.go:287.
	void handleFileEvent(uint32_t fflags, dirEntry* entry,
	                     std::unordered_set<dirWatch*>& touched) {
		std::lock_guard<std::mutex> lk(kqMu);
		auto subs = findSubscriptionsLocked(entry->path);

		if ((fflags & (NOTE_DELETE | NOTE_RENAME | NOTE_REVOKE)) != 0) {
			// Close the stale fd; the watched inode is gone.
			if (entry->fd >= 0) {
				(void)::close(entry->fd);
				fdToEntry.erase(entry->fd);
				entry->fd = -1;
			}

			bool recreated = false;
			if ((fflags & NOTE_DELETE) != 0 &&
			    (fflags & (NOTE_RENAME | NOTE_REVOKE)) == 0 &&
			    !entry->isDir) {
				recreated = tryRewatchLocked(entry);
			}

			for (auto& sub : subs) {
				dirWatch* w = sub->dirWatchPtr.get();
				touched.insert(w);
				if (recreated) {
					w->events.update(sub->path);
				} else {
					w->events.remove(sub->path);
					// If we lost a directory, walk the entries map and
					// close every fd we had open for descendants. Some
					// kernels deliver only the parent's
					// NOTE_DELETE/NOTE_RENAME and never fire NOTE_DELETE
					// on the children; without this cleanup, modifying a
					// file inside the moved tree later surfaces an event
					// against the descendant's stale (pre-rename) path.
					// We also emit a delete for each descendant we close.
					if (entry->isDir) {
						closeDescendantFDsLocked(w, *sub->entries,
						                         sub->path);
					}
					removeEntryAndDescendants(*sub->entries, sub->path);
					// Root-of-watch deletion: no more events can fire
					// for this dirWatch. Tell the caller.
					if (sub->path == w->dir) {
						w->events.setError(gostd::errorf(
						    "%w: watched directory removed",
						    {ErrWatchTerminated}));
					}
				}
			}
			if (!recreated) {
				subsByPath.erase(entry->path);
			}
			return;
		}

		for (auto& sub : subs) {
			dirWatch* w = sub->dirWatchPtr.get();
			touched.insert(w);
			if ((fflags & (NOTE_WRITE | NOTE_ATTRIB | NOTE_EXTEND)) !=
			    0) {
				w->events.update(sub->path);
			}
		}
	}

	// closeDescendantFDsLocked — kqueue.go:356.
	void closeDescendantFDsLocked(dirWatch* w, entriesMap& entries,
	                              const std::string& root) {
		std::string prefix = root + "/";
		for (auto& [path, e] : entries) {
			if (path.compare(0, prefix.size(), prefix) != 0) {
				continue;
			}
			if (e.fd >= 0) {
				(void)::close(e.fd);
				fdToEntry.erase(e.fd);
				e.fd = -1;
			}
			subsByPath.erase(path);
			w->events.remove(path);
		}
	}

	// tryRewatchLocked — kqueue.go:375.
	bool tryRewatchLocked(dirEntry* entry) {
		struct stat st;
		if (::lstat(entry->watchPath.c_str(), &st) != 0) {
			return false;
		}

		// Only fast-path when the recreated path has the same type; a
		// file→dir change needs a full tree rebuild via compareDir.
		bool newIsDir = (st.st_mode & S_IFMT) == S_IFDIR;
		if (newIsDir != entry->isDir) {
			return false;
		}

		int fd = openForEvents(entry->watchPath);
		if (fd < 0) {
			return false;
		}

		struct kevent ev;
		EV_SET(&ev, fd, EVFILT_VNODE,
		       EV_ADD | EV_CLEAR | EV_ENABLE, vnodeFFlags, 0, nullptr);
		if (::kevent(kq, &ev, 1, nullptr, 0, nullptr) < 0) {
			(void)::close(fd);
			return false;
		}

		entry->fd = fd;
		fdToEntry[fd] = entry;
		return true;
	}

	void closeEntryLocked(dirEntry* entry) {
		if (entry->fd >= 0) {
			(void)::close(entry->fd);
			fdToEntry.erase(entry->fd);
			entry->fd = -1;
		}
	}

	// removeSubsForEntriesLocked — kqueue.go:416. Removes only the sub
	// whose `entries` field literally is entriesPtr (Go compares
	// `&sub.entries == entriesPtr` — field-identity, not map-identity).
	void removeSubsForEntriesLocked(
	    const std::string& path,
	    const std::shared_ptr<entriesMap>* entriesPtr) {
		auto it = subsByPath.find(path);
		if (it == subsByPath.end()) {
			return;
		}
		auto& list = it->second;
		list.erase(std::remove_if(list.begin(), list.end(),
		                          [&](const std::shared_ptr<
		                              kqueueSubscription>& sub) {
			                          return &sub->entries == entriesPtr;
		                          }),
		           list.end());
		if (list.empty()) {
			subsByPath.erase(it);
		}
	}

	// removeEntryAndDescendantsLocked — kqueue.go:432.
	void removeEntryAndDescendantsLocked(
	    std::shared_ptr<entriesMap>* entriesPtr, const std::string& path,
	    bool includeRoot) {
		entriesMap& entries = **entriesPtr;
		for (auto it = entries.begin(); it != entries.end();) {
			const std::string& descendant = it->first;
			if (descendant == path) {
				if (!includeRoot) {
					++it;
					continue;
				}
			} else if (!(descendant.size() > path.size() &&
			             descendant[path.size()] == '/' &&
			             descendant.compare(0, path.size(), path) == 0)) {
				++it;
				continue;
			}
			closeEntryLocked(&it->second);
			removeSubsForEntriesLocked(descendant, entriesPtr);
			it = entries.erase(it);
		}
	}

	std::vector<std::shared_ptr<kqueueSubscription>>
	findSubscriptionsLocked(const std::string& path) {
		auto it = subsByPath.find(path);
		if (it == subsByPath.end()) {
			return {};
		}
		return it->second;
	}

	// subscribe — kqueue.go:457. Called under watcherBase.mu via watchAdd.
	gostd::Error subscribe(std::shared_ptr<dirWatch> w) override {
		// Build the entries map without registering any watches or
		// subscriptions. This avoids a data race: registering a
		// subscription publishes the entries map to the event loop (via
		// subsByPath), which could read it via compareDir while we're
		// still populating it.
		auto entries = std::make_shared<entriesMap>();
		if (auto err =
		        walkDir(w->physicalDir, w->recursive,
		                [&](const std::string& watchPath,
		                    bool isDir) -> gostd::Error {
			                std::string path = w->displayPath(watchPath);
			                (*entries)[path] =
			                    dirEntry{path, watchPath, isDir, -1};
			                return nullptr;
		                });
		    err != nullptr) {
			return err;
		}

		// Open fds, register kevents, and publish subscriptions under
		// b.mu. Holding the lock for the entire block ensures that the
		// event loop cannot see a partially-built entries map, and that
		// fds are always tracked in fdToEntry (no leak on early return).
		std::lock_guard<std::mutex> lk(kqMu);

		std::vector<std::string> toErase;
		for (auto& [path, entry] : *entries) {
			int fd = openForEvents(entry.watchPath);
			if (fd < 0) {
				int e = errno;
				if (path == w->dir) {
					cleanupEntriesLocked(*entries);
					return std::make_shared<dirWatchErrorObj>(
					    gostd::errorf("error watching %s: %w",
					                  {w->dir, errnoError(e)}),
					    w);
				}
				toErase.push_back(path);
				continue;
			}
			struct kevent ev;
			EV_SET(&ev, fd, EVFILT_VNODE,
			       EV_ADD | EV_CLEAR | EV_ENABLE, vnodeFFlags, 0,
			       nullptr);
			if (::kevent(kq, &ev, 1, nullptr, 0, nullptr) < 0) {
				int e = errno;
				(void)::close(fd);
				if (path == w->dir) {
					cleanupEntriesLocked(*entries);
					return std::make_shared<dirWatchErrorObj>(
					    gostd::errorf("error watching %s: %w",
					                  {w->dir, errnoError(e)}),
					    w);
				}
				toErase.push_back(path);
				continue;
			}
			entry.fd = fd;
			fdToEntry[fd] = &entry;
		}
		for (auto& p : toErase) {
			entries->erase(p);
		}

		for (auto& [path, entry] : *entries) {
			auto sub = std::make_shared<kqueueSubscription>(
			    kqueueSubscription{w, path, entries, entry.fd});
			subsByPath[path].push_back(std::move(sub));
		}
		return nullptr;
	}

	// cleanupEntriesLocked — kqueue.go:521. Called on subscribe failure;
	// must hold kqMu.
	void cleanupEntriesLocked(entriesMap& entries) {
		for (auto& [path, e] : entries) {
			if (e.fd >= 0) {
				(void)::close(e.fd);
				fdToEntry.erase(e.fd);
				e.fd = -1;
			}
		}
	}

	// watchPath — kqueue.go:532 (`kqueueBackend::watchDir`).
	bool watchPath(const std::shared_ptr<dirWatch>& w,
	               const std::string& path,
	               const std::shared_ptr<entriesMap>& entries) {
		auto it = entries->find(path);
		if (it == entries->end()) {
			return false;
		}
		dirEntry* entry = &it->second;
		std::lock_guard<std::mutex> lk(kqMu);

		auto sub = std::make_shared<kqueueSubscription>(
		    kqueueSubscription{w, path, entries, -1});
		if (entry->fd < 0) {
			int fd = openForEvents(entry->watchPath);
			if (fd < 0) {
				return false;
			}
			struct kevent ev;
			EV_SET(&ev, fd, EVFILT_VNODE,
			       EV_ADD | EV_CLEAR | EV_ENABLE, vnodeFFlags, 0,
			       nullptr);
			if (::kevent(kq, &ev, 1, nullptr, 0, nullptr) < 0) {
				(void)::close(fd);
				return false;
			}
			entry->fd = fd;
			fdToEntry[fd] = entry;
		}
		sub->fd = entry->fd;
		subsByPath[path].push_back(std::move(sub));
		return true;
	}

	// compareDir — kqueue.go:565. Triggered when a watched directory has
	// NOTE_WRITE: list the dir, diff against the tree, emit
	// create/remove events.
	bool compareDir(int /*fd*/, const std::string& path,
	                std::unordered_set<dirWatch*>& touched) {
		std::vector<std::shared_ptr<kqueueSubscription>> subs;
		{
			std::lock_guard<std::mutex> lk(kqMu);
			subs = findSubscriptionsLocked(path);
		}

		// For non-recursive subscriptions, only compareDir on the root
		// dir. NOTE_WRITE on a child dir means something changed inside
		// it, but non-recursive mode shouldn't report those changes.
		// Emit an update for the child dir itself (its metadata changed)
		// and return.
		std::vector<std::shared_ptr<kqueueSubscription>> filteredSubs;
		for (auto& s : subs) {
			if (!s->dirWatchPtr->recursive && path != s->dirWatchPtr->dir) {
				s->dirWatchPtr->events.update(path);
				touched.insert(s->dirWatchPtr.get());
			} else {
				filteredSubs.push_back(s);
			}
		}
		if (filteredSubs.empty()) {
			return true;
		}
		subs = std::move(filteredSubs);

		std::string dirStart = path + "/";
		struct diskSnapshot {
			std::vector<osDirEntry> entries;
			std::unordered_set<std::string> currentDisplayPaths;
		};
		std::unordered_map<std::string, diskSnapshot> snapshots;

		// Each subscription has its own entries map (built in
		// subscribe). Multiple subs at the same path arise from multiple
		// dirWatches covering overlapping subtrees; their maps are
		// always distinct, so we iterate subs directly rather than
		// trying to dedup by map identity.
		for (auto& sub : subs) {
			auto& entries = *sub->entries;
			auto baseIt = entries.find(path);
			if (baseIt == entries.end()) {
				continue;
			}
			std::string wpath = baseIt->second.watchPath;
			std::string watchDirStart = wpath + "/";

			auto snapIt = snapshots.find(wpath);
			if (snapIt == snapshots.end()) {
				auto [diskEntries, rerr] = readEntries(wpath);
				if (rerr != nullptr) {
					continue;
				}
				diskSnapshot snap;
				snap.entries = std::move(diskEntries);
				snap.currentDisplayPaths.reserve(snap.entries.size());
				for (auto& ent : snap.entries) {
					snap.currentDisplayPaths.insert(dirStart +
					                                ent.name);
				}
				snapIt = snapshots.emplace(wpath, std::move(snap))
				             .first;
			}
			diskSnapshot& snapshot = snapIt->second;

			for (auto& ent : snapshot.entries) {
				std::string fullPath = dirStart + ent.name;
				std::string fullWatchPath = watchDirStart + ent.name;

				auto existIt = entries.find(fullPath);
				if (existIt != entries.end()) {
					dirEntry& existing = existIt->second;
					if (existing.fd >= 0) {
						// Check if the fd still refers to the same
						// inode as the path on disk. On DragonFlyBSD,
						// rename-over doesn't fire NOTE_DELETE on the
						// replaced file's fd, leaving a stale entry
						// whose fd points to the old (now unlinked)
						// inode.
						struct stat fdSt, pathSt;
						if (::fstat(existing.fd, &fdSt) == 0 &&
						    ::lstat(fullWatchPath.c_str(), &pathSt) ==
						        0) {
							if (fdSt.st_dev != pathSt.st_dev ||
							    fdSt.st_ino != pathSt.st_ino) {
								// Inode changed: path was replaced.
								std::lock_guard<std::mutex> lk(kqMu);
								closeEntryLocked(&existing);
								removeSubsForEntriesLocked(
								    fullPath, &sub->entries);
								if (existing.isDir) {
									removeEntryAndDescendantsLocked(
									    &sub->entries, fullPath,
									    false);
								}
								existing.isDir = ent.isDir;
							}
						}
					}
					if (existing.fd >= 0) {
						continue;
					}
					// Entry exists but fd is stale: the file was
					// replaced. Re-watch it and emit an update.
					if (!watchPath(sub->dirWatchPtr, fullPath,
					               sub->entries)) {
						continue;
					}
					sub->dirWatchPtr->events.update(fullPath);
					touched.insert(sub->dirWatchPtr.get());
					if (ent.isDir && sub->dirWatchPtr->recursive) {
						(void)walkDir(
						    fullWatchPath, true,
						    [&](const std::string& p,
						        bool pIsDir) -> gostd::Error {
							    if (p == fullWatchPath) {
								    return nullptr;
							    }
							    std::string displayPath =
							        sub->dirWatchPtr->displayPath(p);
							    (*sub->entries)[displayPath] = dirEntry{
							        displayPath, p, pIsDir, -1};
							    sub->dirWatchPtr->events.create(
							        displayPath);
							    watchPath(sub->dirWatchPtr,
							              displayPath,
							              sub->entries);
							    return nullptr;
						    });
					}
					continue;
				}
				entries.emplace(fullPath, dirEntry{fullPath,
				                                   fullWatchPath,
				                                   ent.isDir, -1});
				if (!watchPath(sub->dirWatchPtr, fullPath,
				               sub->entries)) {
					entries.erase(fullPath);
					continue;
				}
				sub->dirWatchPtr->events.create(fullPath);
				touched.insert(sub->dirWatchPtr.get());

				// For recursive subscriptions, walk into the new
				// directory to catch pre-populated subdirectories (e.g.
				// a directory tree moved into the watched area).
				if (ent.isDir && sub->dirWatchPtr->recursive) {
					(void)walkDir(
					    fullWatchPath, true,
					    [&](const std::string& p,
					        bool pIsDir) -> gostd::Error {
						    if (p == fullWatchPath) {
							    return nullptr; // handled above
						    }
						    std::string displayPath =
						        sub->dirWatchPtr->displayPath(p);
						    (*sub->entries)[displayPath] = dirEntry{
						        displayPath, p, pIsDir, -1};
						    sub->dirWatchPtr->events.create(
						        displayPath);
						    watchPath(sub->dirWatchPtr, displayPath,
						              sub->entries);
						    return nullptr;
					    });
				}
			}

			// Detect removals: entries directly under dirStart that no
			// longer exist on disk.
			std::vector<std::string> toRemove;
			for (auto& [p, e] : entries) {
				if (p.compare(0, dirStart.size(), dirStart) != 0) {
					continue;
				}
				std::string rest = p.substr(dirStart.size());
				if (rest.find('/') != std::string::npos) {
					continue;
				}
				if (snapshot.currentDisplayPaths.count(p) != 0) {
					continue;
				}
				toRemove.push_back(p);
			}
			for (auto& p : toRemove) {
				sub->dirWatchPtr->events.remove(p);
				touched.insert(sub->dirWatchPtr.get());
				{
					std::lock_guard<std::mutex> lk(kqMu);
					for (auto& [descendant, e] : entries) {
						if (descendant != p &&
						    !(descendant.size() > p.size() &&
						      descendant[p.size()] == '/' &&
						      descendant.compare(0, p.size(), p) ==
						          0)) {
							continue;
						}
						if (e.fd >= 0) {
							(void)::close(e.fd);
							fdToEntry.erase(e.fd);
						}
						subsByPath.erase(descendant);
					}
				}
				removeEntryAndDescendants(entries, p);
			}
		}
		return true;
	}

	// closeWatch — kqueue.go:746.
	gostd::Error closeWatch(std::shared_ptr<dirWatch> w) override {
		std::lock_guard<std::mutex> lk(kqMu);
		for (auto it = subsByPath.begin(); it != subsByPath.end();) {
			auto& list = it->second;
			bool removedAny = false;
			std::vector<std::shared_ptr<kqueueSubscription>> kept;
			for (auto& s : list) {
				if (s->dirWatchPtr == w) {
					removedAny = true;
					continue;
				}
				kept.push_back(s);
			}
			if (!removedAny) {
				++it;
				continue;
			}
			if (kept.empty()) {
				// Closing the file descriptor automatically unwatches
				// it in kqueue.
				int fd = list[0]->fd;
				(void)::close(fd);
				fdToEntry.erase(fd);
				it = subsByPath.erase(it);
			} else {
				it->second = std::move(kept);
				++it;
			}
		}
		return nullptr;
	}
};

kqueueBackend* newKqueueBackend() { return new kqueueBackend(); }

} // namespace

// Go's init() (kqueue.go:133): register the factory on the package watcher.
watcher& kqueueWatcher() {
	static watcher w{"kqueue"};
	static bool registered = [] {
		w.factory = []() -> watcherImpl* { return newKqueueBackend(); };
		return true;
	}();
	(void)registered;
	return w;
}

} // namespace tsc::fswatch

#endif // __APPLE__
