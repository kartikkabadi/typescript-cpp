// watcher.go — port of tsc/internal/fswatch/watcher.go: the Watcher
// interface, package-level watcher registry, and the shared dirWatch
// bookkeeping machinery.

#include "internal/fswatch/fswatch.h"

#include "internal/nativepath/nativepath.h"
#include "internal/vfs/vfs.h"

#include <algorithm>
#include <stdexcept>
#include <thread>

namespace tsc::fswatch {

// --- WatchOption constructors — watcher.go:143-168 ---------------------------

// WithIgnore returns a [WatchOption] that filters events before delivery.
// If the function returns true for a path, events for that path are
// silently dropped. The filtering is per-subscriber; multiple watches
// on the same directory may have different ignore functions.
std::shared_ptr<WatchOption>
WithIgnore(std::function<bool(const std::string& path)> fn) {
	return std::make_shared<ignoreOption>(std::move(fn));
}

// WithRecursive returns a [WatchOption] that enables recursive watching
// of the entire directory tree. Without this option,
// [Watcher.WatchDirectory] watches only direct children of dir.
//
// In recursive mode, events for all descendants at any depth are
// delivered. On inotify/fanotify, a watch descriptor is added for
// every subdirectory. On kqueue, an fd is opened for every entry.
// On Windows, bWatchSubtree=TRUE is passed to ReadDirectoryChangesW.
// On FSEvents, the kernel is inherently recursive.
std::shared_ptr<WatchOption> WithRecursive() {
	return std::make_shared<recursiveOption>();
}

// --- package watchers — watcher.go:187-251 -----------------------------------

// Package-level watcher instances. Platform init() functions set the factory.
// (inotifyWatcher / fanotifyWatcher are defined in inotify.cpp/fanotify.cpp;
// these three have no factory on Linux → never Available.)
watcher& fseventsWatcher() {
	static watcher w{"fsevents"};
	return w;
}
watcher& kqueueWatcher() {
	static watcher w{"kqueue"};
	return w;
}
watcher& windowsWatcher() {
	static watcher w{"windows"};
	return w;
}
fallbackWatcher& fanotifyFallbackWatcher() {
	static fallbackWatcher w{&fanotifyWatcher(), &inotifyWatcher()};
	return w;
}

// AllWatchers returns a fresh slice listing every watcher backend the package
// knows about. Use [Watcher.Available] to check which ones work on the
// current OS.
std::vector<Watcher*> AllWatchers() {
	return {
	    &inotifyWatcher(),
	    &fseventsWatcher(),
	    &kqueueWatcher(),
	    &windowsWatcher(),
	    &fanotifyFallbackWatcher(),
	};
}

Watcher* Inotify() { return &inotifyWatcher(); }   // Linux and Android
Watcher* FSEvents() { return &fseventsWatcher(); } // macOS
Watcher* Kqueue() { return &kqueueWatcher(); }     // macOS, FreeBSD, other BSDs
Watcher* Windows() { return &windowsWatcher(); }   // ReadDirectoryChangesW

// Fanotify returns the fanotify watcher (Linux, kernel ≥ 5.13). Directories
// on filesystems that don't support fanotify watches automatically use
// inotify instead.
Watcher* Fanotify() { return &fanotifyFallbackWatcher(); }

// Default returns the recommended watcher for the current OS.
Watcher* Default() {
	// runtime.GOOS — this build is Linux-only; the other cases are kept for
	// parity with watcher.go.
	constexpr std::string_view goos = "linux";
	if (goos == "linux") {
		if (Fanotify()->available()) {
			return Fanotify();
		}
		return Inotify();
	}
	if (goos == "android") {
		return Inotify();
	}
	if (goos == "darwin") {
		if (FSEvents()->available()) {
			return FSEvents();
		}
		return Kqueue();
	}
	if (goos == "windows") {
		return Windows();
	}
	if (goos == "freebsd" || goos == "openbsd" || goos == "netbsd" ||
	    goos == "dragonfly") {
		return Kqueue();
	}
	return new watcher{"unsupported"};
}

// --- fallbackWatcher — watcher.go:255-310 ------------------------------------

std::pair<std::shared_ptr<Watch>, gostd::Error> fallbackWatcher::watchDirectory(
    const std::string& dir, const WatchCallback& fn,
    std::vector<std::shared_ptr<WatchOption>> opts) {
	auto [watches, err] =
	    watchDirectories({WatchDirectoryRequest{dir, fn, std::move(opts)}});
	if (err != nullptr) {
		return {nullptr, err};
	}
	return {watches[0], nullptr};
}

std::pair<std::vector<std::shared_ptr<Watch>>, gostd::Error>
fallbackWatcher::watchDirectories(
    std::vector<WatchDirectoryRequest> requests) {
	auto result = primary->watchDirectories(requests);
	auto& watches = result.first;
	auto& err = result.second;
	if (err == nullptr ||
	    !gostd::errorIs(err, ErrFilesystemUnsupported)) {
		return {std::move(watches), err};
	}

	watches.clear();
	watches.reserve(requests.size());
	auto rollback = [&] {
		for (auto it = watches.rbegin(); it != watches.rend(); ++it) {
			(void)(*it)->close();
		}
	};
	for (auto& request : requests) {
		auto [w, err] = primary->watchDirectory(request.dir,
		                                      request.callback,
		                                      request.options);
		if (gostd::errorIs(err, ErrFilesystemUnsupported)) {
			auto p = secondary->watchDirectory(request.dir,
			                                   request.callback,
			                                   request.options);
			w = std::move(p.first);
			err = std::move(p.second);
		}
		if (err != nullptr) {
			rollback();
			return {{},
			        gostd::errorf("fswatch: failed to watch directory %q: %w",
			                      {request.dir, err})};
		}
		watches.push_back(std::move(w));
	}
	return {watches, nullptr};
}

std::pair<std::shared_ptr<Watch>, gostd::Error>
fallbackWatcher::watchFile(const std::string& path, const WatchCallback& fn) {
	auto [w, err] = primary->watchFile(path, fn);
	if (gostd::errorIs(err, ErrFilesystemUnsupported)) {
		return secondary->watchFile(path, fn);
	}
	return {w, err};
}

// --- watcher — watcher.go:315-470 ---------------------------------------------

// HasFastRecursiveBackend implements [Watcher.HasFastRecursiveBackend].
bool watcher::hasFastRecursiveBackend() {
	return watcherName == "windows" || watcherName == "fsevents";
}

bool watcher::canShareRecursiveDirWatches() const {
	// TODO: Re-enable this for Windows once coalesced recursive watches have
	// more real-world bake time.
	return watcherName == "fsevents";
}

std::pair<watcherImpl*, gostd::Error> watcher::getImpl() {
	mu.lock();
	if (impl != nullptr) {
		watcherImpl* i = impl;
		mu.unlock();
		return {i, nullptr};
	}
	auto f = factory;
	mu.unlock();

	if (f == nullptr) {
		return {nullptr, ErrUnavailable};
	}

	watcherImpl* i = f();
	if (auto err = i->run(); err != nullptr) {
		return {nullptr, err};
	}

	mu.lock();
	if (impl != nullptr) {
		watcherImpl* winner = impl;
		mu.unlock();
		i->shutdown();
		return {winner, nullptr};
	}
	impl = i;
	mu.unlock();
	return {i, nullptr};
}

std::string watcher::keyForDirWatch(const std::string& dir,
                                    bool recursive) const {
	if (recursive) {
		return dir + '\0' + "recursive";
	}
	return dir;
}

std::shared_ptr<dirWatch> watcher::findCoveringRecursiveWatchLocked(
    const std::string& dir, const std::string& physicalDir,
    const fswatch::pathComparer& comparer) {
	std::shared_ptr<dirWatch> best;
	for (auto& kv : dirWatches) {
		auto& dw = kv.second;
		if (!dw->recursive || dw->comparer != comparer ||
		    !isInDirectoryOrSelf(dw->dir, dir) ||
		    !isInDirectoryOrSelf(dw->physicalDir, physicalDir)) {
			continue;
		}
		if (best == nullptr || dw->dir.size() > best->dir.size()) {
			best = dw;
		}
	}
	return best;
}

std::string watcher::findConsolidationDirLocked(const std::string& dirArg,
                                                const std::string& physicalDir) {
	if (!canShareRecursiveDirWatches()) {
		return "";
	}
	std::string dir = dirArg;
	std::string parent = vfs::pathDir(dir); // filepath.Dir
	while (parent != dir && parent != ".") {
		if (vfs::pathDir(parent) == parent) {
			break;
		}
		std::string physicalParent = physicalDirFor(parent);
		if (!isInDirectoryOrSelf(physicalParent, physicalDir)) {
			return "";
		}
		int count = 1;
		for (auto& kv : dirWatches) {
			auto& dw = kv.second;
			if (isInDirectoryOrSelf(parent, dw->dir) &&
			    isInDirectoryOrSelf(physicalParent, dw->physicalDir)) {
				count++;
				if (count >= recursiveConsolidateThreshold) {
					return parent;
				}
			}
		}
		std::string next = vfs::pathDir(parent);
		if (next == parent) {
			break;
		}
		dir = parent;
		parent = std::move(next);
	}
	return "";
}

std::pair<std::shared_ptr<dirWatch>, gostd::Error>
watcher::getOrCreateDirWatch(const std::string& dirArg,
                             const std::string& physicalDirArg,
                             bool recursiveArg,
                             const fswatch::pathComparer& comparer) {
	std::lock_guard<std::mutex> lk(mu);
	std::string dir = dirArg;
	std::string physicalDir = physicalDirArg;
	bool recursive = recursiveArg;
	// Go lazily makes w.dirWatches; unordered_map needs no init.
	if (debounce == nullptr) {
		debounce = newDebounce();
	}

	if (canShareRecursiveDirWatches()) {
		if (auto dw =
		        findCoveringRecursiveWatchLocked(dir, physicalDir, comparer);
		    dw != nullptr) {
			return {dw, nullptr};
		}
		std::string consolidationDir =
		    findConsolidationDirLocked(dir, physicalDir);
		if (!consolidationDir.empty()) {
			auto [parentComparer, err] = pathComparer(consolidationDir);
			if (err != nullptr) {
				return {nullptr, err};
			}
			if (parentComparer == comparer) {
				dir = consolidationDir;
				physicalDir = physicalDirFor(dir);
				recursive = true;
				if (auto dw = findCoveringRecursiveWatchLocked(
				        dir, physicalDir, comparer);
				    dw != nullptr) {
					return {dw, nullptr};
				}
			}
		}
	}

	std::string key = keyForDirWatch(dir, recursive);
	if (auto it = dirWatches.find(key); it != dirWatches.end()) {
		return {it->second, nullptr};
	}
	auto dw = newDirWatch(dir, physicalDir, debounce);
	dw->setComparer(comparer);
	dw->sequence = sequence;
	dw->recursive = recursive;
	dirWatches[key] = dw;
	return {dw, nullptr};
}

void watcher::removeDirWatch(dirWatch* dw) {
	std::lock_guard<std::mutex> lk(mu);
	std::string key = keyForDirWatch(dw->dir, dw->recursive);
	auto it = dirWatches.find(key);
	if (it != dirWatches.end() && it->second.get() == dw) {
		dirWatches.erase(it);
		dw->destroyDebounce();
	}
}

// --- WatchDirectory/WatchDirectories/WatchFile — watcher.go:482-609 ----------

std::pair<std::shared_ptr<Watch>, gostd::Error> watcher::watchDirectory(
    const std::string& dir, const WatchCallback& fn,
    std::vector<std::shared_ptr<WatchOption>> opts) {
	auto [watches, err] =
	    watchDirectories({WatchDirectoryRequest{dir, fn, std::move(opts)}});
	if (err != nullptr) {
		return {nullptr, err};
	}
	return {watches[0], nullptr};
}

std::pair<std::vector<std::shared_ptr<Watch>>, gostd::Error>
watcher::watchDirectories(std::vector<WatchDirectoryRequest> requests) {
	if (!available()) {
		return {{}, ErrUnavailable};
	}
	if (requests.empty()) {
		return {{}, nullptr};
	}

	struct preparedWatch {
		std::shared_ptr<dirWatch> dw;
		uint64_t id;
		bool recursive;
		std::string dir;
	};
	std::vector<preparedWatch> prepared;
	prepared.reserve(requests.size());
	std::vector<std::shared_ptr<dirWatch>> uniqueDirWatches;
	uniqueDirWatches.reserve(requests.size());
	std::unordered_set<dirWatch*> seenDirWatches;
	auto rollback = [&] {
		for (auto it = prepared.rbegin(); it != prepared.rend(); ++it) {
			it->dw->unwatch(it->id);
			it->dw->unref(this);
		}
	};

	for (auto& request : requests) {
		std::string dir = request.dir;
		WatchCallback fn = request.callback;
		if (fn == nullptr) {
			rollback();
			return {{}, errNilCallback};
		}
		dir = vfs::pathClean(dir); // filepath.Clean
		if (!vfs::pathIsAbs(dir)) { // filepath.IsAbs
			rollback();
			return {{}, errNotAbsolute};
		}
		dir = canonicalizePath(dir);
		if (canShareRecursiveDirWatches()) {
			if (auto err = validateWatchDirectory(dir); err != nullptr) {
				rollback();
				return {{}, err};
			}
		}
		std::string physicalDir = physicalDirFor(dir);

		watchOptions sopts;
		for (auto& o : request.options) {
			o->applyWatchOption(sopts);
		}

		auto [comparer, err] = pathComparer(dir);
		if (err != nullptr) {
			rollback();
			return {{}, err};
		}
		auto [dw, dwErr] =
		    getOrCreateDirWatch(dir, physicalDir, sopts.recursive, comparer);
		if (dwErr != nullptr) {
			rollback();
			return {{}, dwErr};
		}
		auto [id, _ok] =
		    dw->addCallback(dir, physicalDir, sopts.recursive, fn, sopts.ignore,
		                    sopts.file);
		(void)_ok;
		prepared.push_back(preparedWatch{dw, id, sopts.recursive, dir});
		if (seenDirWatches.insert(dw.get()).second) {
			uniqueDirWatches.push_back(dw);
		}
	}

	auto [impl, implErr] = getImpl();
	if (implErr != nullptr) {
		rollback();
		return {{}, implErr};
	}
	if (auto err = impl->watchAddMany(uniqueDirWatches); err != nullptr) {
		rollback();
		return {{}, err};
	}

	std::vector<std::shared_ptr<Watch>> watches(prepared.size());
	for (size_t i = 0; i < prepared.size(); i++) {
		watches[i] = std::make_shared<watch>(this, prepared[i].dw, impl,
		                                     prepared[i].id);
	}
	return {watches, nullptr};
}

// validateWatchDirectory — watcher.go:580-589.
gostd::Error validateWatchDirectory(const std::string& dir) {
	auto [info, err] = osStat(dir);
	if (err != nullptr) {
		return err;
	}
	if (!info.isDir) {
		return errnoError(ENOTDIR);
	}
	return nullptr;
}

std::pair<std::shared_ptr<Watch>, gostd::Error>
watcher::watchFile(const std::string& path, const WatchCallback& fn) {
	if (fn == nullptr) {
		return {nullptr, errNilCallback};
	}
	if (!available()) {
		return {nullptr, ErrUnavailable};
	}
	std::string p = vfs::pathClean(path); // filepath.Clean
	if (!vfs::pathIsAbs(p)) {             // filepath.IsAbs
		return {nullptr, errNotAbsolute};
	}
	p = canonicalizePath(p);
	std::string dir = vfs::pathDir(p); // filepath.Dir
	if (dir == p) {
		return {nullptr, errRootPath};
	}

	std::vector<std::shared_ptr<WatchOption>> opts{
	    std::make_shared<fileOption>(p)};
	return watchDirectory(dir, fn, std::move(opts));
}

// --- watch — watcher.go:620-635 ----------------------------------------------

gostd::Error watch::close() {
	std::lock_guard<std::mutex> lk(mu);
	if (cancelled) {
		return nullptr;
	}
	cancelled = true;
	bool last = dw->unwatch(id);
	if (last) {
		impl->watchRemove(dw);
		dw->unref(w);
	}
	return nullptr;
}

// --- watcherImpl/watcherBase — watcher.go:637-778 -----------------------------

void watcherBase::init() {
	// b.self back-reference is unnecessary under real virtual dispatch.
	subscriptions.clear();
	startedFlag = false;
	startErr = nullptr;
}

void watcherBase::notifyStarted() {
	std::lock_guard<std::mutex> lk(startedMu);
	if (!startedFlag) {
		startedFlag = true;
		startedCv.notify_all();
	}
}

gostd::Error watcherBase::run() {
	std::thread([this] {
		// Go: defer func() { if r := recover(); r != nil { ... } }()
		try {
			if (auto err = start(); err != nullptr) {
				handleStartError(err);
			}
		} catch (const std::exception& e) {
			handleStartError(gostd::newError(e.what()));
		} catch (...) {
			handleStartError(gostd::newError("panic in watcher start"));
		}
	}).detach();
	std::unique_lock<std::mutex> lk(startedMu);
	startedCv.wait(lk, [&] { return startedFlag; });
	lk.unlock();
	std::lock_guard<std::mutex> lk2(mu);
	return startErr;
}

void watcherBase::handleStartError(const gostd::Error& err) {
	std::vector<std::shared_ptr<dirWatch>> subs;
	{
		std::lock_guard<std::mutex> lk(mu);
		startErr = err;
		subs.reserve(subscriptions.size());
		for (auto& w : subscriptions) {
			subs.push_back(w);
		}
	}
	for (auto& w : subs) {
		w->notifyError(err);
	}
	notifyStarted();
}

gostd::Error watcherBase::watchAdd(std::shared_ptr<dirWatch> w) {
	return watchAddMany({std::move(w)});
}

gostd::Error
watcherBase::watchAddMany(const std::vector<std::shared_ptr<dirWatch>>& watches) {
	std::unique_lock<std::mutex> lk(mu);
	std::vector<std::shared_ptr<dirWatch>> toAdd;
	toAdd.reserve(watches.size());
	for (auto& w : watches) {
		if (subscriptions.count(w) != 0) {
			continue;
		}
		toAdd.push_back(w);
	}
	if (toAdd.empty()) {
		lk.unlock();
		return nullptr;
	}

	if (supportsSubscribeMany()) {
		if (auto err = subscribeMany(toAdd); err != nullptr) {
			lk.unlock();
			return err;
		}
		for (auto& w : toAdd) {
			subscriptions.insert(w);
		}
		lk.unlock();
		return nullptr;
	}

	std::vector<std::shared_ptr<dirWatch>> added;
	added.reserve(toAdd.size());
	for (auto& w : toAdd) {
		if (auto err = subscribe(w); err != nullptr) {
			for (auto& addedWatch : added) {
				subscriptions.erase(addedWatch);
				(void)closeWatch(addedWatch);
			}
			lk.unlock();
			return err;
		}
		subscriptions.insert(w);
		added.push_back(w);
	}
	lk.unlock();
	return nullptr;
}

void watcherBase::watchRemove(std::shared_ptr<dirWatch> w) {
	std::lock_guard<std::mutex> lk(mu);
	auto it = subscriptions.find(w);
	if (it == subscriptions.end()) {
		return;
	}
	subscriptions.erase(it);
	(void)closeWatch(std::move(w));
}

void watcherBase::handleWatcherError(
    const std::shared_ptr<dirWatchErrorObj>& werr) {
	watchRemove(werr->dirWatch);
	gostd::Error werrE = werr; // fmtArg takes Error, not a derived shared_ptr
	werr->dirWatch->notifyError(
	    gostd::errorf("%w: %w", {ErrWatchTerminated, werrE}));
}

// --- callback / dirWatch — watcher.go:782-1164 ---------------------------------

std::shared_ptr<dirWatch>
newDirWatch(const std::string& dir, const std::string& physicalDir,
            fswatch::debounce* db) {
	auto dw = std::make_shared<dirWatch>();
	dw->dir = dir;
	dw->physicalDir = physicalDir;
	dw->debounce = db;
	std::shared_ptr<dirWatch> dwKeep = dw;
	dw->debounce->add(dw.get(), [dwKeep]() { dwKeep->triggerCallbacks(); });
	return dw;
}

void dirWatch::setComparer(const pathComparer& c) {
	comparer = c;
	dirFold = c.prepare(dir).folded;
	if (physicalDir == dir) {
		physicalDirFold = dirFold;
	} else {
		physicalDirFold = c.prepare(physicalDir).folded;
	}
}

// physicalDirFor — watcher.go:851-863.
std::string physicalDirFor(const std::string& dir) {
	// nativepath::realpath returns (string, std::error_code), not
	// gostd::Error.
	auto [realpath, ec] = nativepath::realpath(dir);
	if (ec) {
		return dir;
	}
	if (realpath == dir) {
		return dir;
	}
	return canonicalizePath(vfs::pathClean(realpath)); // filepath.Clean
}

std::string dirWatch::displayPath(const std::string& watchPath) const {
	return rebasePath(watchPath, physicalDir, dir);
}

std::string dirWatch::physicalPath(const std::string& displayPath) const {
	return rebasePath(displayPath, dir, physicalDir);
}

// rebasePath — watcher.go:876-897.
std::string rebasePath(const std::string& path, const std::string& from,
                       const std::string& to) {
	if (from == to) {
		return path;
	}
	if (path == from) {
		return to;
	}
	if (path.compare(0, from.size(), from) != 0) {
		return path;
	}
	std::string_view suffix = std::string_view(path).substr(from.size());
	if (!from.empty() && isPathSeparator(from.back())) {
		return joinPathSuffix(to, suffix);
	}
	if (suffix.empty() || !isPathSeparator(suffix[0])) {
		return path;
	}
	return joinPathSuffix(to, suffix);
}

// joinPathSuffix — watcher.go:899-913.
std::string joinPathSuffix(const std::string& root, std::string_view suffix) {
	if (suffix.empty()) {
		return root;
	}
	if (isPathSeparator(suffix[0])) {
		if (!root.empty() && isPathSeparator(root.back())) {
			return root + std::string(suffix.substr(1));
		}
		return root + std::string(suffix);
	}
	if (!root.empty() && isPathSeparator(root.back())) {
		return root + std::string(suffix);
	}
	return root + "/" + std::string(suffix);
}

void dirWatch::destroyDebounce() {
	fswatch::debounce* db;
	{
		std::lock_guard<std::mutex> lk(mu);
		db = debounce;
		debounce = nullptr;
	}
	if (db != nullptr) {
		db->remove(this);
	}
}

void dirWatch::notify() {
	fswatch::debounce* db;
	bool hasPendingCBs, hasTerminal, hasEvents, hasError;
	{
		std::lock_guard<std::mutex> lk(mu);
		hasPendingCBs =
		    std::any_of(callbacks.begin(), callbacks.end(),
		                [](const callback& cb) { return !cb.delivered; });
		hasTerminal =
		    std::any_of(callbacks.begin(), callbacks.end(),
		                [](const callback& cb) {
			                return cb.terminal != nullptr && !cb.delivered;
		                });
		hasEvents = events.size() > 0;
		hasError = events.hasError();
		db = debounce;
	}
	if (hasPendingCBs && (hasEvents || hasError || hasTerminal) &&
	    db != nullptr) {
		db->trigger();
	}
}

void dirWatch::notifyError(const gostd::Error& err) {
	std::vector<callback> cbs;
	{
		std::lock_guard<std::mutex> lk(mu);
		cbs = callbacks;
		callbacks.clear();
	}
	for (auto& cb : cbs) {
		cb.fn({}, err);
	}
}

void dirWatch::triggerCallbacks() {
	std::vector<callback> cbs;
	std::vector<std::vector<Event>> eventsByCallback;
	gostd::Error err;
	{
		std::lock_guard<std::mutex> lk(mu);
		bool hasError = events.hasError();
		bool hasEvents = events.size() > 0;
		cbs.reserve(callbacks.size());
		bool hasTerminal = false;
		for (auto& cb : callbacks) {
			if (cb.delivered) {
				continue;
			}
			if (cb.terminal != nullptr) {
				hasTerminal = true;
			}
			cbs.push_back(cb);
		}
		if (cbs.empty()) {
			if (hasEvents || hasError) {
				(void)events.drain();
			}
			return;
		}
		if (!hasEvents && !hasError && !hasTerminal) {
			return;
		}
		std::vector<uint64_t> startSeqs(cbs.size());
		for (size_t i = 0; i < cbs.size(); i++) {
			startSeqs[i] = cbs[i].sinceSeq;
		}
		auto [byCallback, derr] = events.drainForSequences(startSeqs);
		eventsByCallback = std::move(byCallback);
		err = derr;
		for (auto& cb : cbs) {
			if (cb.terminal == nullptr) {
				continue;
			}
			for (auto& live : callbacks) {
				if (live.id == cb.id) {
					live.delivered = true;
					break;
				}
			}
		}
	}

	comparisonCache comparisons;
	for (size_t i = 0; i < cbs.size(); i++) {
		auto& cb = cbs[i];
		std::vector<Event> cbEvents = std::move(eventsByCallback[i]);
		if (cb.ignore != nullptr || !cb.recursive || cb.dir != dir ||
		    !cb.fileComparison.path.empty()) {
			std::vector<Event> filtered;
			filtered.reserve(cbEvents.size());
			for (auto& e : cbEvents) {
				e = cb.mapEventCached(e, &comparisons);
				if (!cb.fileComparison.path.empty()) {
					comparisonPath path;
					path.path = e.path;
					path.cache = &comparisons;
					auto [suffix, ok] =
					    cb.comparer.suffixPrepared(cb.fileComparison, &path);
					if (!ok || !suffix.empty()) {
						continue;
					}
					e.path = cb.fileComparison.path;
				}
				if (cb.ignore != nullptr && cb.ignore(e.path)) {
					continue;
				}
				if (cb.dir != dir && !e.includedWatchRoot &&
				    e.path == cb.dir && e.kind == EventKind::EventUpdate) {
					continue;
				}
				if (cb.recursive) {
					if (cb.dir != dir &&
					    !isInDirectoryOrSelf(cb.dir, e.path)) {
						continue;
					}
				} else if (!isDirectChild(cb.dir, e.path) &&
				           !(cb.dir != dir && e.path == cb.dir)) {
					continue;
				}
				filtered.push_back(std::move(e));
			}
			cbEvents = std::move(filtered);
		}
		gostd::Error cbErr = err;
		if (cb.terminal != nullptr) {
			cbErr = cb.terminal;
		}
		if (!cbEvents.empty() || cbErr != nullptr) {
			cb.fn(std::move(cbEvents), cbErr);
		}
	}
}

Event callback::mapEvent(Event e) const {
	return mapEventCached(std::move(e), nullptr);
}

Event callback::mapEventCached(Event e, comparisonCache* cache) const {
	if (!physicalDir.empty() &&
	    (physicalDir != dir || comparer.ignoreCase)) {
		comparisonPath physicalPath;
		physicalPath.path = eventPhysicalPath(e.path);
		physicalPath.cache = cache;
		comparisonPath root = physicalComparison;
		if (root.path.empty()) {
			root.path = physicalDir;
		}
		if (auto [path, ok] =
		        comparer.rebasePrepared(&physicalPath, root, dir);
		    ok) {
			e.path = std::move(path);
		}
	}
	return e;
}

std::string callback::eventPhysicalPath(const std::string& path) const {
	if (!watchPhysicalDir.empty() && !watchDir.empty() &&
	    watchPhysicalDir != watchDir &&
	    isInDirectoryOrSelf(watchDir, path)) {
		return rebasePath(path, watchDir, watchPhysicalDir);
	}
	return path;
}

bool dirWatch::terminateCallbacksForDeletedRoot(const std::string& path,
                                              uint64_t seq,
                                              const gostd::Error& err) {
	std::lock_guard<std::mutex> lk(mu);
	bool changed = false;
	comparisonCache comparisons;
	comparisonPath deleted;
	deleted.path = path;
	deleted.cache = &comparisons;
	for (auto& cb : callbacks) {
		if (cb.delivered || cb.terminal != nullptr || cb.sinceSeq >= seq) {
			continue;
		}
		comparisonPath physicalPath;
		physicalPath.path = cb.eventPhysicalPath(path);
		physicalPath.cache = &comparisons;
		comparisonPath dirC = cb.dirComparison;
		comparisonPath physicalC = cb.physicalComparison;
		auto [_, logicalMatch] = cb.comparer.suffixPrepared(deleted, &dirC);
		auto [_2, physicalMatch] =
		    cb.comparer.suffixPrepared(physicalPath, &physicalC);
		if (logicalMatch || physicalMatch) {
			cb.terminal = err;
			changed = true;
		}
	}
	return changed;
}

// isInDirectoryOrSelf — watcher.go:1086-1104.
bool isInDirectoryOrSelf(std::string_view dir, std::string_view path) {
	if (dir.empty()) {
		return false;
	}
	if (path == dir) {
		return true;
	}
	if (path.compare(0, dir.size(), dir) != 0) {
		return false;
	}
	std::string_view rest = path.substr(dir.size());
	if (rest.empty()) {
		return false;
	}
	if (isPathSeparator(dir.back())) {
		return true;
	}
	return isPathSeparator(rest[0]);
}

// isDirectChild reports whether path is an immediate child of dir.
// Both paths must be absolute. Returns false for path == dir.
bool isDirectChild(std::string_view dir, std::string_view path) {
	if (path.compare(0, dir.size(), dir) != 0) {
		return false;
	}
	std::string_view rest = path.substr(dir.size());
	if (rest.empty()) {
		return false;
	}
	if (rest[0] != '/' && rest[0] != '/') { // '/' == filepath.Separator
		return false;
	}
	rest = rest.substr(1);
	return !rest.empty() && rest.find('/') == std::string_view::npos &&
	    rest.find('/') == std::string_view::npos;
}

std::pair<uint64_t, bool>
dirWatch::watch(const std::string& dir, const std::string& physicalDir,
                bool recursive, const WatchCallback& fn,
                const std::function<bool(const std::string&)>& ignore) {
	return addCallback(dir, physicalDir, recursive, fn, ignore, "");
}

std::pair<uint64_t, bool>
dirWatch::addCallback(const std::string& dir, const std::string& physicalDir,
                    bool recursive, const WatchCallback& fn,
                    const std::function<bool(const std::string&)>& ignore,
                    const std::string& file) {
	std::lock_guard<std::mutex> lk(mu);
	nextCBID++;
	uint64_t id = nextCBID;
	uint64_t sinceSeq = events.sequence();
	if (sequence) {
		sinceSeq = sequence();
	}
	callback cb;
	cb.id = id;
	cb.dir = dir;
	cb.physicalDir = physicalDir;
	cb.watchDir = this->dir;
	cb.watchPhysicalDir = this->physicalDir;
	cb.recursive = recursive;
	cb.fn = fn;
	cb.ignore = ignore;
	cb.sinceSeq = sinceSeq;
	cb.comparer = comparer;
	cb.dirComparison = comparer.prepare(dir);
	cb.physicalComparison = comparer.prepare(physicalDir);
	cb.fileComparison = comparer.prepare(file);
	callbacks.push_back(std::move(cb));
	return {id, true};
}

bool dirWatch::unwatch(uint64_t id) {
	std::lock_guard<std::mutex> lk(mu);
	for (size_t i = 0; i < callbacks.size(); i++) {
		if (callbacks[i].id == id) {
			callbacks.erase(callbacks.begin() + i);
			return callbacks.empty();
		}
	}
	return false;
}

void dirWatch::unref(watcher* w) {
	bool empty;
	{
		std::lock_guard<std::mutex> lk(mu);
		empty = callbacks.empty();
	}
	if (empty) {
		w->removeDirWatch(this);
	}
}

} // namespace tsc::fswatch
