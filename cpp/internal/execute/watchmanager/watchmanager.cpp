// watchmanager.go — port of
// tsc/internal/execute/watchmanager/watchmanager.go.

#include "internal/execute/watchmanager/watchmanager.h"

namespace tsc::execute::watchmanager {
namespace {

// diffMapsFunc — core.DiffMapsFunc (core.go:779); the helper is not yet
// ported into internal/core.
template <typename K, typename V1, typename V2>
void diffMapsFunc(
    const std::unordered_map<K, V1>& m1,
    const std::unordered_map<K, V2>& m2,
    const std::function<bool(const V1&, const V2&)>& equalValues,
    const std::function<void(const K&, const V2&)>& onAdded,
    const std::function<void(const K&, const V1&)>& onRemoved,
    const std::function<void(const K&, const V1&, const V2&)>& onChanged) {
	if (onAdded) {
		for (auto& [k, v2] : m2) {
			if (m1.find(k) == m1.end()) {
				onAdded(k, v2);
			}
		}
	}
	if (onChanged == nullptr && onRemoved == nullptr) {
		return;
	}
	for (auto& [k, v1] : m1) {
		auto it = m2.find(k);
		if (it != m2.end()) {
			if (onChanged && !equalValues(v1, it->second)) {
				onChanged(k, v1, it->second);
			}
		} else {
			onRemoved(k, v1);
		}
	}
}

}  // namespace

// EnsureDefaultBackend — watchmanager.go:58.
void WatchManager::EnsureDefaultBackend() {
	if (backend == nullptr) {
		auto* fsw = fswatch::Default();
		// Go: &FSWatchBackend{Inner: fsw}
		auto* fsb = new FSWatchBackend{};
		fsb->Inner = fsw;
		backend = fsb;
		if (DebugLog != nullptr) {
			*DebugLog << "[watch] using " << fsw->Name() << " backend\n";
		}
	}
}

// DrainEvents — watchmanager.go:72.
std::pair<std::unordered_map<std::string, fswatch::EventKind>, bool>
WatchManager::DrainEvents() {
	std::lock_guard<std::mutex> lock(changedMu);
	auto changed = std::move(changedPaths);
	auto overflow = changedOverflow;
	changedPaths.clear();
	changedOverflow = false;
	return {std::move(changed), overflow};
}

// ForceOverflow — watchmanager.go:83.
void WatchManager::ForceOverflow() {
	std::lock_guard<std::mutex> lock(changedMu);
	changedOverflow = true;
}

// signalDoCycle — watchmanager.go:89.
void WatchManager::signalDoCycle() {
	{
		std::lock_guard<std::mutex> lock(mu);
		if (doCyclePending) {
			// A signal is already pending; coalesced.
			return;
		}
		doCyclePending = true;
	}
	doCycleCv.notify_one();
}

// onWatchEvents — watchmanager.go:98.
void WatchManager::onWatchEvents(const std::vector<fswatch::Event>& events,
                                 gostd::Error err) {
	if (err) {
		if (gostd::errorIs(err, fswatch::ErrOverflow)) {
			if (DebugLog != nullptr) {
				*DebugLog << "[watch] event overflow, triggering rebuild\n";
			}
			{
				std::lock_guard<std::mutex> lock(changedMu);
				changedOverflow = true;
			}
			signalDoCycle();
			return;
		}
		*warnWriter << "Warning: File watch error: " << err->Error()
		            << '\n';
		return;
	}

	if (!events.empty()) {
		if (DebugLog != nullptr) {
			*DebugLog << "[watch] " << events.size() << " event(s): ";
			for (size_t i = 0; i < events.size(); i++) {
				if (i > 0) {
					*DebugLog << ", ";
				}
				if (i >= 5) {
					*DebugLog << "... and " << events.size() - i
					          << " more";
					break;
				}
				*DebugLog << fswatch::eventKindString(events[i].Kind)
				          << " " << events[i].Path;
			}
			*DebugLog << '\n';
		}
		{
			std::lock_guard<std::mutex> lock(changedMu);
			for (auto& e : events) {
				changedPaths[e.Path] = e.Kind;
			}
		}
		signalDoCycle();
	}
}

// handleWatchTerminated — watchmanager.go:133.
void WatchManager::handleWatchTerminated(const std::string& dir,
                                         watchedDir* identity) {
	if (DebugLog != nullptr) {
		*DebugLog << "[watch] watch terminated: " << dir << '\n';
	}
	std::unique_ptr<gostd::io::Closer> staleCloser;
	{
		std::lock_guard<std::mutex> lock(mu);
		auto it = watchedDirs.find(dir);
		if (it != watchedDirs.end() && it->second == identity) {
			staleCloser = std::move(it->second->closer);
			delete it->second;
			watchedDirs.erase(it);
		}
	}
	if (staleCloser != nullptr) {
		staleCloser->close();
	}
	{
		std::lock_guard<std::mutex> lock(changedMu);
		changedOverflow = true;
	}
	signalDoCycle();
}

// CloseAllWatches — watchmanager.go:152.
void WatchManager::CloseAllWatches() {
	std::vector<watchedDir*> closers;
	{
		std::lock_guard<std::mutex> lock(mu);
		closers.reserve(watchedDirs.size());
		for (auto& [dir, wd] : watchedDirs) {
			closers.push_back(wd);
		}
		watchedDirs.clear();
	}
	for (auto* wd : closers) {
		if (wd->closer != nullptr) {
			wd->closer->close();
		}
		delete wd;
	}
}

// createDirWatchRequest — watchmanager.go:165.
WatchDirectoryRequest WatchManager::createDirWatchRequest(
    const std::string& dir, watchedDir* entry) {
	return WatchDirectoryRequest{
	    .Dir = dir,
	    .Callback = [this, dir, entry](const std::vector<fswatch::Event>& events,
	                                 gostd::Error err) {
		    if (err && gostd::errorIs(err, fswatch::ErrWatchTerminated)) {
			    handleWatchTerminated(dir, entry);
			    return;
		    }
		    onWatchEvents(events, err);
	    },
	    .Recursive = entry->recursive,
	    .Ignore = ShouldIgnoreWatchPath,
	};
}

// ResolveDesiredDirs — watchmanager.go:179.
std::unordered_map<std::string, bool> WatchManager::ResolveDesiredDirs(
    const std::unordered_map<std::string, bool>& desiredDirs) {
	std::unordered_map<std::string, bool> resolved;
	resolved.reserve(desiredDirs.size());
	for (auto& [dir, recursive] : desiredDirs) {
		std::string watchDir = dir;
		bool watchRecursive = recursive;
		for (; !dirExists(watchDir);) {
			auto parent = tspath::getDirectoryPath(watchDir);
			if (parent == watchDir) {
				break;
			}
			watchDir = parent;
			watchRecursive = false;  // ancestor fallbacks are always non-recursive
		}
		if (!dirExists(watchDir) || !CanWatchDirectory(watchDir)) {
			if (DebugLog != nullptr) {
				*DebugLog << "[watch] no watchable ancestor for " << dir
				          << '\n';
			}
			continue;
		}
		if (watchDir != dir && DebugLog != nullptr) {
			*DebugLog << "[watch] resolved " << dir << " to ancestor "
			          << watchDir << '\n';
		}
		if (auto it = resolved.find(watchDir); it != resolved.end()) {
			it->second = it->second || watchRecursive;
		} else {
			resolved[watchDir] = watchRecursive;
		}
	}
	return resolved;
}

// ReconcileWatches — watchmanager.go:205.
gostd::Error WatchManager::ReconcileWatches(
    const std::unordered_map<std::string, bool>& desiredDirs) {
	if (backend == nullptr) {
		return nullptr;
	}

	std::vector<dirWatchUpdate> additions;
	std::vector<dirWatchUpdate> changes;

	diffMapsFunc<std::string, watchedDir*, bool>(
	    watchedDirs, desiredDirs,
	    [](watchedDir* const& wd, const bool& recursive) {
		    return wd->recursive == recursive;
	    },
	    [this, &additions](const std::string& dir, const bool& recursive) {
		    if (DebugLog != nullptr) {
			    *DebugLog << "[watch] watching directory " << dir
			              << " (recursive=" << (recursive ? "true" : "false")
			              << ")\n";
		    }
		    additions.push_back({dir, recursive});
	    },
	    [this](const std::string& dir, watchedDir* const& wd) {
		    if (DebugLog != nullptr) {
			    *DebugLog << "[watch] closing stale dir watch: " << dir
			              << '\n';
		    }
		    if (wd->closer != nullptr) {
			    wd->closer->close();
		    }
		    delete wd;
		    watchedDirs.erase(dir);
	    },
	    [this, &changes](const std::string& dir, watchedDir* const& wd,
	                     const bool& recursive) {
		    if (DebugLog != nullptr) {
			    *DebugLog << "[watch] recreating dir watch " << dir
			              << " (recursive "
			              << (wd->recursive ? "true" : "false") << "→"
			              << (recursive ? "true" : "false") << ")\n";
		    }
		    if (wd->closer != nullptr) {
			    wd->closer->close();
		    }
		    delete wd;
		    watchedDirs.erase(dir);
		    changes.push_back({dir, recursive});
	    });
	additions.insert(additions.end(), changes.begin(), changes.end());
	return createDirWatches(additions);
}

// createDirWatches — watchmanager.go:247.
gostd::Error WatchManager::createDirWatches(
    const std::vector<dirWatchUpdate>& updates) {
	if (updates.empty()) {
		return nullptr;
	}
	std::vector<WatchDirectoryRequest> requests(updates.size());
	std::vector<watchedDir*> entries(updates.size());
	for (size_t i = 0; i < updates.size(); i++) {
		auto& update = updates[i];
		auto* entry = new watchedDir();
		entry->recursive = update.recursive;
		entries[i] = entry;
		requests[i] = createDirWatchRequest(update.dir, entry);
	}
	auto [closers, err] = backend->WatchDirectories(requests);
	if (!err) {
		for (size_t i = 0; i < updates.size(); i++) {
			entries[i]->closer = std::move(closers[i]);
			watchedDirs[updates[i].dir] = entries[i];
		}
		return nullptr;
	}
	for (auto* entry : entries) {
		delete entry;
	}
	if (DebugLog != nullptr) {
		for (auto& update : updates) {
			*DebugLog << "[watch] failed to watch directory " << update.dir
			          << ": " << err->Error() << '\n';
		}
	}
	return err;
}

// DirWatchSet.Set — watchmanager.go:291.
void DirWatchSet::Set(const std::string& dir, bool recursive) {
	auto original = dir;
	auto canon = canonical(dir);
	if (names.find(canon) == names.end()) {
		names[canon] = original;
	}
	dirs[canon] = dirs[canon] || recursive;
}

// DirWatchSet.Covered — watchmanager.go:299.
bool DirWatchSet::Covered(std::string dir) {
	dir = canonical(dir);
	if (dirs.find(dir) != dirs.end()) {
		return true;
	}
	auto rootLength = tspath::getRootLength(dir);
	for (; dir.size() > rootLength;) {
		dir = tspath::getDirectoryPath(dir);
		if (auto it = dirs.find(dir); it != dirs.end() && it->second) {
			return true;
		}
	}
	return false;
}

// DirWatchSet.Dirs — watchmanager.go:312.
std::unordered_map<std::string, bool> DirWatchSet::Dirs() const {
	std::unordered_map<std::string, bool> out;
	out.reserve(dirs.size());
	for (auto& [key, recursive] : dirs) {
		out[names.at(key)] = recursive;
	}
	return out;
}

// IsPathUnderWatch — watchmanager.go:318.
bool WatchManager::IsPathUnderWatch(const std::string& path,
                                    const tspath::ComparePathsOptions& opts) {
	for (auto& [dir, wd] : watchedDirs) {
		if (tspath::containsPath(dir, path, opts)) {
			return true;
		}
	}
	return false;
}

// RunLoop — watchmanager.go:327.
void WatchManager::RunLoop(gostd::Context ctx,
                           const std::function<void()>& doCycle) {
	// select on ctx.Done() and the cap-1 doCycle channel. ctxDone is woken
	// via a context.AfterFunc-style registration.
	bool ctxDone = false;
	auto stop = gostd::contextAfterFunc(ctx, [&] {
		{
			std::lock_guard<std::mutex> lock(mu);
			ctxDone = true;
		}
		doCycleCv.notify_one();
	});
	for (;;) {
		{
			std::lock_guard<std::mutex> lock(mu);
			if (ctxDone) {
				CloseAllWatches();
				return;
			}
			if (doCyclePending) {
				doCyclePending = false;
			} else {
				std::unique_lock<std::mutex> ul(mu);
				doCycleCv.wait(ul, [&] { return doCyclePending || ctxDone; });
				continue;
			}
		}
		doCycle();
	}
}

}  // namespace tsc::execute::watchmanager
