// ============================================================================
// lspwatcher.cpp — port of tsc/internal/lsp/lspwatcher/lspwatcher.go.
// ============================================================================

#include "internal/lsp/lspwatcher/lspwatcher.h"

#include "internal/bundled/bundled.h"
#include "internal/tspath/tspath.h"

namespace {

// --- strings helpers (Go stdlib strings) ----------------------------------

std::string stringsTrimRight(std::string_view s, std::string_view cutset) {
	while (!s.empty() && cutset.find(s.back()) != std::string_view::npos) {
		s.remove_suffix(1);
	}
	return std::string(s);
}

bool stringsContains(std::string_view s, std::string_view substr) {
	return s.find(substr) != std::string_view::npos;
}

} // namespace

namespace tsc::lsp::lspwatcher {

// Forward decls — defined below in Go source order (lspwatcher.go:382,549).
static std::pair<std::string, bool>
nearestExistingAncestor(const std::shared_ptr<vfs::FS>& fs, std::string dir);
std::string rootFromGlob(std::string pattern);

// ---------------------------------------------------------------------------
// New / NewWithFSWatcher / newWithBackend — lspwatcher.go:96-115.
// ---------------------------------------------------------------------------

std::shared_ptr<Watcher>
newWithBackend(const std::shared_ptr<vfs::FS>& fs,
               const std::shared_ptr<watcherBackend>& backend,
               std::function<void(std::vector<std::shared_ptr<lsproto::FileEvent>>)> onChanges,
               const std::shared_ptr<tsc::logging::Logger>& logger) {
	auto w = std::make_shared<Watcher>();
	w->fs = fs;
	w->backend = backend;
	w->onChanges = std::move(onChanges);
	w->logger = logger;
	return w;
}

std::shared_ptr<Watcher>
NewWithFSWatcher(const std::shared_ptr<vfs::FS>& fs, fswatch::Watcher* watcher,
                 std::function<void(std::vector<std::shared_ptr<lsproto::FileEvent>>)> onChanges,
                 const std::shared_ptr<tsc::logging::Logger>& logger) {
	auto backend = std::make_shared<defaultWatcherBackend>();
	backend->watcher = watcher;
	return newWithBackend(fs, backend, std::move(onChanges), logger);
}

std::shared_ptr<Watcher>
New(const std::shared_ptr<vfs::FS>& fs,
    std::function<void(std::vector<std::shared_ptr<lsproto::FileEvent>>)> onChanges,
    const std::shared_ptr<tsc::logging::Logger>& logger) {
	return NewWithFSWatcher(fs, fswatch::Default(), std::move(onChanges), logger);
}

// ---------------------------------------------------------------------------
// WatchFiles — lspwatcher.go:126.
// ---------------------------------------------------------------------------

gostd::Error
Watcher::WatchFiles(const std::string& id,
                    const std::vector<std::shared_ptr<lsproto::FileSystemWatcher>>& fileSystemWatchers) {
	mu.lock();
	if (closed) {
		mu.unlock();
		return gostd::newError("lspwatcher: closed");
	}
	if (watches.find(id) != watches.end()) {
		mu.unlock();
		return gostd::errorf("lspwatcher: watcher %q already exists", {id});
	}
	// Mark the id as existing before installing any watches so a concurrent
	// WatchFiles for the same id is rejected above.
	watches[id];
	mu.unlock();

	bool failed = false;
	for (auto& fileSystemWatcher : fileSystemWatchers) {
		auto [directory, ok] = watchRoot(fileSystemWatcher.get());
		if (!ok || directory.empty()) {
			logger->Logf("lspwatcher: skipping watcher %q: unrecognized pattern %q",
			             {id, watchPatternString(fileSystemWatcher.get())});
			continue;
		}
		auto newWatch = std::make_shared<watch>();
		newWatch->watcher = shared_from_this();
		newWatch->requestedDirectory = directory;
		newWatch->kind = effectiveKind(fileSystemWatcher.get());
		newWatch->recursive = isRecursiveGlob(fileSystemWatcher.get());
		if (auto err = newWatch->reconcile(false /*emitSynthetic*/); err != nullptr) {
			logger->Logf("lspwatcher: failed to register watcher %q for %q: %v",
			             {id, directory, err});
			newWatch->close();
			failed = true;
			break;
		}
		mu.lock();
		if (closed) {
			mu.unlock();
			newWatch->close();
			return gostd::newError("lspwatcher: closed");
		}
		watches[id].push_back(newWatch);
		mu.unlock();
	}

	if (failed) {
		// Roll back the whole id so the session's retry (MarkPending) can
		// cleanly re-register it. The session treats an id as a single unit.
		UnwatchFiles(id);
		return gostd::errorf("lspwatcher: failed to register one or more watchers for %q",
		                     {id});
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// UnwatchFiles — lspwatcher.go:180.
// ---------------------------------------------------------------------------

gostd::Error Watcher::UnwatchFiles(const std::string& id) {
	mu.lock();
	auto it = watches.find(id);
	if (it == watches.end()) {
		mu.unlock();
		return gostd::errorf("lspwatcher: no watcher with id %q", {id});
	}
	auto ws = std::move(it->second);
	watches.erase(it);
	mu.unlock();
	for (auto& w : ws) {
		w->close();
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// Close — lspwatcher.go:196. Removes every subscription. Safe to call
// multiple times.
// ---------------------------------------------------------------------------

void Watcher::Close() {
	mu.lock();
	if (closed) {
		mu.unlock();
		return;
	}
	closed = true;
	auto watchesByID = std::move(watches);
	watches.clear();
	if (flushTimer != nullptr) {
		flushTimer->stop();
		flushTimer = nullptr;
	}
	pending.clear();
	mu.unlock();
	for (auto& [_, ws] : watchesByID) {
		for (auto& w : ws) {
			w->close();
		}
	}
}

// ---------------------------------------------------------------------------
// watch::close — lspwatcher.go:220. Tears down the current subscription and
// prevents any in-flight reconcile from reinstalling one.
// ---------------------------------------------------------------------------

void watch::close() {
	mu.lock();
	closed = true;
	auto subscription = std::move(this->subscription);
	this->subscription = nullptr;
	watchedDirectory.clear();
	mu.unlock();
	if (subscription != nullptr) {
		subscription->close();
	}
}

// ---------------------------------------------------------------------------
// watch::reconcile — lspwatcher.go:245. Installs or advances this watch
// toward the target directory based on the current filesystem state.
// ---------------------------------------------------------------------------

gostd::Error watch::reconcile(bool emitSyntheticCreates) {
	std::lock_guard<std::mutex> lk(mu);
	auto watcher = this->watcher;
	for (;;) {
		if (closed) {
			return nullptr;
		}
		if (watcher->fs->DirectoryExists(requestedDirectory)) {
			if (watchingTarget && subscription != nullptr) {
				return nullptr; // already watching the target
			}
			std::string targetDirectory = requestedDirectory;
			std::vector<std::shared_ptr<fswatch::WatchOption>> options;
			if (recursive) {
				options.push_back(fswatch::WithRecursive());
			}
			auto [subscription_, err] = watcher->backend->watchDirectory(
			    targetDirectory, targetCallback(targetDirectory), std::move(options));
			if (err != nullptr) {
				return err;
			}
			auto previous = std::move(subscription);
			subscription = subscription_;
			watchedDirectory = targetDirectory;
			watchingTarget = true;
			if (previous != nullptr) {
				previous->close();
			}
			if (emitSyntheticCreates) {
				watcher->emitSyntheticCreates(targetDirectory, kind, recursive);
			}
			return nullptr;
		}

		auto [ancestor, ok] = nearestExistingAncestor(watcher->fs, requestedDirectory);
		if (!ok) {
			// Nothing exists to watch (even the root is gone); drop any
			// subscription.
			if (subscription != nullptr) {
				auto previous = std::move(subscription);
				subscription = nullptr;
				watchedDirectory.clear();
				watchingTarget = false;
				previous->close();
			}
			return nullptr;
		}
		std::string ancestorDirectory = ancestor;
		if (!watchingTarget && subscription != nullptr && watchedDirectory == ancestorDirectory) {
			return nullptr; // already watching the correct ancestor
		}
		auto [subscription_, err] = watcher->backend->watchDirectory(
		    ancestorDirectory, ancestorCallback(), {});
		if (err != nullptr) {
			return err;
		}
		auto previous = std::move(subscription);
		subscription = subscription_;
		watchedDirectory = ancestorDirectory;
		watchingTarget = false;
		if (previous != nullptr) {
			previous->close();
		}
		// The target may have appeared between the DirectoryExists check
		// above and installing this ancestor subscription (e.g. an atomic
		// tree creation), so loop to descend further or promote immediately.
		// Any promotion from here on is a missing→present transition, so
		// synthesize creates.
		emitSyntheticCreates = true;
	}
}

// ---------------------------------------------------------------------------
// watch::targetCallback — lspwatcher.go:319.
// ---------------------------------------------------------------------------

fswatch::WatchCallback watch::targetCallback(const std::string& watchedDirectory) {
	auto watcher = this->watcher;
	auto self = shared_from_this();
	return [watcher, self, watchedDirectory](std::vector<fswatch::Event> events,
	                                       gostd::Error err) {
		bool terminated = false;
		if (err != nullptr) {
			if (gostd::errorIs(err, fswatch::ErrOverflow)) {
				watcher->logger->Logf(
				    "lspwatcher: watch overflow in %q (some events may have been dropped): %v",
				    {watchedDirectory, err});
			} else if (gostd::errorIs(err, fswatch::ErrWatchTerminated)) {
				terminated = true;
				watcher->logger->Logf(
				    "lspwatcher: watch terminated in %q (directory removed): %v",
				    {watchedDirectory, err});
			} else {
				watcher->logger->Logf("lspwatcher: watch error in %q: %v",
				                      {watchedDirectory, err});
			}
		}
		if (!events.empty()) {
			watcher->forwardEvents(self->kind, events);
		}
		if (terminated) {
			// The delete event for the directory was forwarded above; now
			// re-attach to the nearest existing ancestor.
			self->handleTerminated();
		}
	};
}

// ---------------------------------------------------------------------------
// watch::handleTerminated — lspwatcher.go:352.
// ---------------------------------------------------------------------------

void watch::handleTerminated() {
	mu.lock();
	if (closed) {
		mu.unlock();
		return;
	}
	auto previous = std::move(subscription);
	subscription = nullptr;
	watchedDirectory.clear();
	watchingTarget = false;
	mu.unlock();
	if (previous != nullptr) {
		previous->close();
	}
	reconcile(true /*emitSyntheticCreates*/);
}

// ---------------------------------------------------------------------------
// watch::ancestorCallback — lspwatcher.go:373.
// ---------------------------------------------------------------------------

fswatch::WatchCallback watch::ancestorCallback() {
	auto self = shared_from_this();
	return [self](std::vector<fswatch::Event> events, gostd::Error err) {
		self->reconcile(true /*emitSyntheticCreates*/);
	};
}

// ---------------------------------------------------------------------------
// nearestExistingAncestor — lspwatcher.go:382.
// ---------------------------------------------------------------------------

static std::pair<std::string, bool>
nearestExistingAncestor(const std::shared_ptr<vfs::FS>& fs, std::string dir) {
	for (;;) {
		if (fs->DirectoryExists(dir)) {
			return {dir, true};
		}
		auto parent = tspath::getDirectoryPath(dir);
		if (parent == dir) {
			return {"", false};
		}
		dir = std::move(parent);
	}
}

// ---------------------------------------------------------------------------
// forwardEvents — lspwatcher.go:397.
// ---------------------------------------------------------------------------

void Watcher::forwardEvents(lsproto::WatchKind kind, std::vector<fswatch::Event> events) {
	mu.lock();
	if (closed) {
		mu.unlock();
		return;
	}
	for (auto& event : events) {
		lsproto::FileChangeType changeType;
		switch (event.kind) {
		case fswatch::EventKind::EventUpdate:
			// fswatch intentionally doesn't distinguish create vs update.
			// For LSP consumers this is fine: callers infer create/update
			// from their own cache and both should invalidate stale state.
			if ((kind & (lsproto::WatchKindCreate | lsproto::WatchKindChange)) == lsproto::WatchKind{0}) {
				continue;
			}
			changeType = lsproto::FileChangeTypeChanged;
			break;
		case fswatch::EventKind::EventDelete:
			if ((kind & lsproto::WatchKindDelete) == lsproto::WatchKind{0}) {
				continue;
			}
			changeType = lsproto::FileChangeTypeDeleted;
			break;
		default:
			continue;
		}

		auto path = tspath::normalizeSlashes(event.path);
		auto uri = lsconv::FileNameToDocumentURI(path);
		pending[std::string(uri)] = std::make_shared<lsproto::FileEvent>(
		    lsproto::FileEvent{uri, changeType});
	}
	scheduleFlushLocked();
	mu.unlock();
}

// ---------------------------------------------------------------------------
// emitSyntheticCreates — lspwatcher.go:443.
// ---------------------------------------------------------------------------

void Watcher::emitSyntheticCreates(const std::string& directory,
                                   lsproto::WatchKind kind, bool recursive) {
	if ((kind & lsproto::WatchKindCreate) == lsproto::WatchKind{0}) {
		return;
	}
	std::vector<std::string> paths{directory};
	if (recursive) {
		vfs::WalkDir(*fs, directory,
		             [&](const std::string& path,
		                 const std::shared_ptr<vfs::DirEntry>& entry,
		                 const vfs::Error& err) -> vfs::Error {
			             if (!err && path != directory) {
				             paths.push_back(path);
			             }
			             return vfs::Error{};
		             });
	} else {
		auto entries = fs->GetAccessibleEntries(directory);
		for (auto& name : entries.files) {
			paths.push_back(tspath::combinePaths(directory, {name}));
		}
		for (auto& name : entries.directories) {
			paths.push_back(tspath::combinePaths(directory, {name}));
		}
	}
	enqueueSyntheticCreates(paths);
}

// ---------------------------------------------------------------------------
// enqueueSyntheticCreates — lspwatcher.go:470.
// ---------------------------------------------------------------------------

void Watcher::enqueueSyntheticCreates(const std::vector<std::string>& paths) {
	mu.lock();
	if (closed) {
		mu.unlock();
		return;
	}
	for (auto& path : paths) {
		auto uri = lsconv::FileNameToDocumentURI(path);
		if (pending.find(std::string(uri)) != pending.end()) {
			continue;
		}
		pending[std::string(uri)] = std::make_shared<lsproto::FileEvent>(
		    lsproto::FileEvent{uri, lsproto::FileChangeTypeCreated});
	}
	scheduleFlushLocked();
	mu.unlock();
}

// ---------------------------------------------------------------------------
// scheduleFlushLocked — lspwatcher.go:495. Callers must hold w.mu.
// ---------------------------------------------------------------------------

void Watcher::scheduleFlushLocked() {
	if (flushTimer == nullptr) {
		auto self = shared_from_this();
		flushTimer = goTimer::afterFunc(throttleWindow, [self] { self->flush(); });
	}
}

// ---------------------------------------------------------------------------
// flush — lspwatcher.go:501.
// ---------------------------------------------------------------------------

void Watcher::flush() {
	mu.lock();
	if (closed) {
		mu.unlock();
		return;
	}
	auto pend = std::move(pending);
	pending.clear();
	flushTimer = nullptr;
	mu.unlock();

	if (pend.empty()) {
		return;
	}
	std::vector<std::shared_ptr<lsproto::FileEvent>> changes;
	changes.reserve(pend.size());
	for (auto& [_, event] : pend) {
		changes.push_back(event);
	}
	onChanges(changes);
}

// ---------------------------------------------------------------------------
// watchRoot — lspwatcher.go:532.
// ---------------------------------------------------------------------------

std::pair<std::string, bool> watchRoot(const lsproto::FileSystemWatcher* fileSystemWatcher) {
	if (fileSystemWatcher->GlobPattern.Pattern != nullptr) {
		return {rootFromGlob(*fileSystemWatcher->GlobPattern.Pattern), true};
	}
	if (auto* relativePattern = fileSystemWatcher->GlobPattern.RelativePattern.get();
	    relativePattern != nullptr) {
		std::string base;
		if (relativePattern->BaseUri.URI != nullptr) {
			base = lsproto::documentUriFileName(*relativePattern->BaseUri.URI);
		} else {
			return {"", false};
		}
		auto pattern = tspath::combinePaths(base, {relativePattern->Pattern});
		return {rootFromGlob(pattern), true};
	}
	return {"", false};
}

// ---------------------------------------------------------------------------
// rootFromGlob — lspwatcher.go:549.
// ---------------------------------------------------------------------------

std::string rootFromGlob(std::string pattern) {
	pattern = tspath::normalizeSlashes(pattern);
	int metaIndex = -1;
	for (int i = 0; i < (int)pattern.size(); i++) {
		switch (pattern[i]) {
		case '*': case '?': case '[': case '{':
			metaIndex = i;
			break;
		}
		if (metaIndex != -1) {
			break;
		}
	}
	if (metaIndex == -1) {
		return tspath::normalizePath(stringsTrimRight(pattern, "/"));
	}
	auto directory = stringsTrimRight(std::string_view(pattern).substr(0, metaIndex), "/");
	if (directory.empty()) {
		return "";
	}
	return tspath::normalizePath(directory);
}

// ---------------------------------------------------------------------------
// watchPatternString — lspwatcher.go:571.
// ---------------------------------------------------------------------------

std::string watchPatternString(const lsproto::FileSystemWatcher* fileSystemWatcher) {
	if (fileSystemWatcher->GlobPattern.Pattern != nullptr) {
		return *fileSystemWatcher->GlobPattern.Pattern;
	}
	if (auto* relativePattern = fileSystemWatcher->GlobPattern.RelativePattern.get();
	    relativePattern != nullptr) {
		std::string base;
		if (relativePattern->BaseUri.URI != nullptr) {
			base = *relativePattern->BaseUri.URI;
		}
		return base + "/" + relativePattern->Pattern;
	}
	return "";
}

// ---------------------------------------------------------------------------
// isRecursiveGlob — lspwatcher.go:588.
// ---------------------------------------------------------------------------

bool isRecursiveGlob(const lsproto::FileSystemWatcher* fileSystemWatcher) {
	return stringsContains(watchPatternString(fileSystemWatcher), "**");
}

// ---------------------------------------------------------------------------
// effectiveKind — lspwatcher.go:592.
// ---------------------------------------------------------------------------

lsproto::WatchKind effectiveKind(const lsproto::FileSystemWatcher* fileSystemWatcher) {
	if (fileSystemWatcher->Kind != nullptr) {
		return *fileSystemWatcher->Kind;
	}
	return lsproto::WatchKindCreate | lsproto::WatchKindChange |
	       lsproto::WatchKindDelete;
}

} // namespace tsc::lsp::lspwatcher
