#pragma once

// watch.go — watchRegistry + WatchedFiles[T] + glob helpers. Registry state
// and watcher lists are mutex-guarded exactly like the Go sync.Mutex /
// sync.Once usage.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "internal/ast/ast.h" // tscUnreachable
#include "internal/collections/collections.h"
#include "internal/core/types.h"
#include "internal/ls/lsconv/lsconv.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/project/client.h" // WatcherID
#include "internal/tspath/tspath.h"

namespace tsc::project {

inline constexpr int minWatchLocationDepth = 2;

using lsp::lsproto::WatchKind;
using lsp::lsproto::WatchKindChange;
using lsp::lsproto::WatchKindCreate;
using lsp::lsproto::WatchKindDelete;

// fileSystemWatcherKey — watch.go:22.
struct fileSystemWatcherKey {
	std::string pattern;
	lsp::lsproto::WatchKind kind = lsp::lsproto::WatchKind(0);

	bool operator==(const fileSystemWatcherKey&) const = default;
};

struct fileSystemWatcherKeyHash {
	size_t operator()(const fileSystemWatcherKey& k) const {
		return std::hash<std::string>()(k.pattern) * 131 +
		       static_cast<size_t>(k.kind);
	}
};

// fileSystemWatcherValue — watch.go:27.
struct fileSystemWatcherValue {
	int count = 0;
	WatcherID id;
};

// watchRegistry tracks the current watch globs and how many individual
// WatchedFiles reference each glob. It provides ref-count helpers so callers
// don't manipulate the map directly.
//
// All methods are safe for concurrent use; locking is handled internally.
struct watchRegistry {
	std::mutex mu;
	std::unordered_map<fileSystemWatcherKey,
	                   std::shared_ptr<fileSystemWatcherValue>,
	                   fileSystemWatcherKeyHash>
	    entries;
	std::unordered_set<WatcherID> pending;

	// Acquire increments the ref count for a watcher. If this is the first
	// reference (count goes from 0 to 1), it returns true so the caller
	// knows to register the watcher with the client.
	bool Acquire(lsp::lsproto::FileSystemWatcher* watcher, WatcherID id) {
		std::lock_guard<std::mutex> lk(mu);
		auto key = toFileSystemWatcherKey(watcher);
		auto it = entries.find(key);
		std::shared_ptr<fileSystemWatcherValue> value;
		if (it == entries.end()) {
			value = std::make_shared<fileSystemWatcherValue>();
			value->id = id;
			entries.emplace(key, value);
		} else {
			value = it->second;
		}
		value->count++;
		return value->count == 1;
	}

	// Release decrements the ref count for a watcher. If no references
	// remain, the entry is removed and the function returns the WatcherID
	// and true so the caller knows to unregister the watcher from the
	// client.
	std::pair<WatcherID, bool> Release(
		lsp::lsproto::FileSystemWatcher* watcher) {
		std::lock_guard<std::mutex> lk(mu);
		auto key = toFileSystemWatcherKey(watcher);
		auto it = entries.find(key);
		if (it == entries.end()) {
			return {"", false};
		}
		auto& value = it->second;
		if (value->count <= 1) {
			auto id = value->id;
			entries.erase(it);
			return {id, true};
		}
		value->count--;
		return {"", false};
	}

	// MarkPending records that a watcher's registration failed and needs
	// retry.
	void MarkPending(const WatcherID& id) {
		std::lock_guard<std::mutex> lk(mu);
		pending.insert(id);
	}

	// ClearPending removes a watcher from the pending set after successful
	// registration.
	void ClearPending(const WatcherID& id) {
		std::lock_guard<std::mutex> lk(mu);
		pending.erase(id);
	}

	// IsPending returns true if the watcher needs retry due to a previous
	// failure.
	bool IsPending(const WatcherID& id) {
		std::lock_guard<std::mutex> lk(mu);
		return pending.count(id) != 0;
	}

private:
	// defined below (needs FileSystemWatcher complete-ish decls)
	static fileSystemWatcherKey toFileSystemWatcherKey(
		lsp::lsproto::FileSystemWatcher* w);
};

inline watchRegistry* newWatchRegistry() {
	return new watchRegistry();
}

// PatternsAndIgnored — watch.go:107.
struct PatternsAndIgnored {
	std::vector<std::string> directoriesOutsideWorkspace;
	std::vector<std::string> patternsInsideWorkspace;
	std::unordered_set<std::string> ignored;
};

// getRecursiveGlobPattern — watch.go:458.
inline std::string getRecursiveGlobPattern(const std::string& directory) {
	return gostd::sprintf("%s/%s", {tspath::removeTrailingDirectorySeparator(
	                                   directory),
	                                std::string_view("**/*")});
}

// newRecursiveDirectoryWatcher — watch.go:474. Creates a FileSystemWatcher
// for recursively watching a directory. When useRelativePattern is true, a
// RelativePattern with a file:// base URI is used; otherwise a plain glob
// Pattern is used.
inline lsp::lsproto::FileSystemWatcher* newRecursiveDirectoryWatcher(
	const std::string& directory, lsp::lsproto::WatchKind kind,
	bool useRelativePattern) {
	if (useRelativePattern) {
		auto baseUri = std::make_shared<lsp::lsproto::URI>(
		    lsconv::FileNameToDocumentURI(directory));
		auto rp = std::make_shared<lsp::lsproto::RelativePattern>();
		rp->BaseUri.URI = baseUri;
		rp->Pattern = "**/*";
		auto* watcher = new lsp::lsproto::FileSystemWatcher();
		watcher->GlobPattern.RelativePattern = rp;
		watcher->Kind = std::make_shared<lsp::lsproto::WatchKind>(kind);
		return watcher;
	}
	auto glob = std::make_shared<std::string>(getRecursiveGlobPattern(directory));
	auto* watcher = new lsp::lsproto::FileSystemWatcher();
	watcher->GlobPattern.Pattern = glob;
	watcher->Kind = std::make_shared<lsp::lsproto::WatchKind>(kind);
	return watcher;
}

// recursiveDirectoryGlobPattern — watch.go:464. Returns the string form of a
// recursive watcher for the given directory that would be produced by
// newRecursiveDirectoryWatcher.
inline std::string recursiveDirectoryGlobPattern(const std::string& directory,
                                                 bool useRelativePattern) {
	if (useRelativePattern) {
		return std::string(lsconv::FileNameToDocumentURI(directory)) +
		       "/**/*";
	}
	return getRecursiveGlobPattern(directory);
}

// fileSystemWatcherGlobString — watch.go:139.
inline std::string fileSystemWatcherGlobString(
	lsp::lsproto::FileSystemWatcher* w) {
	if (w->GlobPattern.Pattern != nullptr) {
		return *w->GlobPattern.Pattern;
	}
	if (w->GlobPattern.RelativePattern != nullptr) {
		std::string base;
		if (w->GlobPattern.RelativePattern->BaseUri.URI != nullptr) {
			base = *w->GlobPattern.RelativePattern->BaseUri.URI;
		} else if (w->GlobPattern.RelativePattern->BaseUri.WorkspaceFolder !=
		           nullptr) {
			TSC_UNREACHABLE(
			    "workspace folder-based relative patterns not implemented");
		}
		return base + "/" + w->GlobPattern.RelativePattern->Pattern;
	}
	return "";
}

// toFileSystemWatcherKey — watch.go:119. Produces a deduplication key for a
// file system watcher. Note: this key is a simple string concatenation of
// the base and pattern, so structurally different watchers (Pattern vs
// RelativePattern, URI vs WorkspaceFolder) could theoretically collide. In
// practice, workspace watchers use plain Pattern with filesystem paths while
// outside-workspace watchers use RelativePattern with file:// URIs, so
// collisions don't occur.
inline fileSystemWatcherKey toFileSystemWatcherKey(
	lsp::lsproto::FileSystemWatcher* w) {
	lsp::lsproto::WatchKind kind = lsp::lsproto::WatchKindCreate |
	                               lsp::lsproto::WatchKindChange |
	                               lsp::lsproto::WatchKindDelete;
	if (w->Kind != nullptr) {
		kind = *w->Kind;
	}
	std::string pattern;
	if (w->GlobPattern.Pattern != nullptr) {
		pattern = *w->GlobPattern.Pattern;
	} else if (w->GlobPattern.RelativePattern != nullptr) {
		std::string base;
		if (w->GlobPattern.RelativePattern->BaseUri.URI != nullptr) {
			base = *w->GlobPattern.RelativePattern->BaseUri.URI;
		} else if (w->GlobPattern.RelativePattern->BaseUri.WorkspaceFolder !=
		           nullptr) {
			TSC_UNREACHABLE(
			    "workspace folder-based relative patterns not implemented");
		}
		pattern = base + "/" + w->GlobPattern.RelativePattern->Pattern;
	}
	return fileSystemWatcherKey{pattern, kind};
}

inline fileSystemWatcherKey watchRegistry::toFileSystemWatcherKey(
	lsp::lsproto::FileSystemWatcher* w) {
	return project::toFileSystemWatcherKey(w);
}

// watcherID — watch.go:157.
inline std::atomic<uint64_t> watcherID{0};

// Watchers — watch.go:211.
struct Watchers {
	WatcherID WatcherID;
	std::vector<lsp::lsproto::FileSystemWatcher*> WorkspaceWatchers;
	std::vector<lsp::lsproto::FileSystemWatcher*> OutsideWorkspaceWatchers;
	std::unordered_set<std::string> IgnoredPaths;
};

// WatchedFiles — watch.go:159.
template <typename T>
struct WatchedFiles {
	std::string name;
	lsp::lsproto::WatchKind watchKind;
	bool hasRelativePatternCapability;
	std::function<PatternsAndIgnored(T)> computeGlobPatterns;

	std::shared_mutex mu;
	// Go zero-value semantics: a freshly-created WatchedFiles has
	// input == T{} (nil for pointer types) until Clone replaces it.
	T input{};
	std::once_flag computeWatchersOnce;
	std::vector<lsp::lsproto::FileSystemWatcher*> workspaceWatchers;
	std::vector<lsp::lsproto::FileSystemWatcher*> outsideWorkspaceWatchers;
	std::unordered_set<std::string> ignored;
	uint64_t id = 0;

	WatchedFiles(const std::string& name, lsp::lsproto::WatchKind watchKind,
	             bool hasRelativePatternCapability,
	             std::function<PatternsAndIgnored(const T&)>
	                 computeGlobPatterns)
	    : name(name),
	      watchKind(watchKind),
	      hasRelativePatternCapability(
	          hasRelativePatternCapability),
	      computeGlobPatterns(std::move(computeGlobPatterns)) {
		id = watcherID.fetch_add(1) + 1;
	}

	// Watchers — watch.go:218.
	project::Watchers Watchers() {
		std::call_once(computeWatchersOnce, [&] {
			std::unique_lock<std::shared_mutex> lk(mu);
			PatternsAndIgnored result = computeGlobPatterns(input);

			std::vector<std::string> globs =
			    result.patternsInsideWorkspace;
			std::sort(globs.begin(), globs.end());
			globs.erase(std::unique(globs.begin(), globs.end()),
			            globs.end());

			auto ignoredSet = result.ignored;
			// ignored is only used for logging and doesn't affect watcher
			// identity
			ignored = ignoredSet;
			bool changed = false;
			bool globsEqual = workspaceWatchers.size() == globs.size();
			if (globsEqual) {
				for (size_t i = 0; i < globs.size(); i++) {
					if (*workspaceWatchers[i]->GlobPattern.Pattern !=
					    globs[i]) {
						globsEqual = false;
						break;
					}
				}
			}
			if (!globsEqual) {
				workspaceWatchers.clear();
				workspaceWatchers.reserve(globs.size());
				for (const auto& glob : globs) {
					auto* watcher =
					    new lsp::lsproto::FileSystemWatcher();
					watcher->GlobPattern.Pattern =
					    std::make_shared<std::string>(glob);
					watcher->Kind =
					    std::make_shared<lsp::lsproto::WatchKind>(watchKind);
					workspaceWatchers.push_back(watcher);
				}
				changed = true;
			}
			std::vector<std::string> dirsOutside =
			    result.directoriesOutsideWorkspace;
			std::sort(dirsOutside.begin(), dirsOutside.end());
			dirsOutside.erase(
			    std::unique(dirsOutside.begin(), dirsOutside.end()),
			    dirsOutside.end());
			bool dirsEqual =
			    outsideWorkspaceWatchers.size() == dirsOutside.size();
			if (dirsEqual) {
				for (size_t i = 0; i < dirsOutside.size(); i++) {
					if (fileSystemWatcherGlobString(
					        outsideWorkspaceWatchers[i]) !=
					    recursiveDirectoryGlobPattern(
					        dirsOutside[i],
					        hasRelativePatternCapability)) {
						dirsEqual = false;
						break;
					}
				}
			}
			if (!dirsEqual) {
				outsideWorkspaceWatchers.clear();
				outsideWorkspaceWatchers.reserve(dirsOutside.size());
				for (const auto& dir : dirsOutside) {
					outsideWorkspaceWatchers.push_back(
					    newRecursiveDirectoryWatcher(
					        dir, watchKind,
					        hasRelativePatternCapability));
				}
				changed = true;
			}
			if (changed) {
				id = watcherID.fetch_add(1) + 1;
			}
		});

		std::shared_lock<std::shared_mutex> lk(mu);
		return project::Watchers{
		    gostd::sprintf("%s watcher %d", {name, id}),
		    workspaceWatchers,
		    outsideWorkspaceWatchers,
		    ignored,
		};
	}

	// ID — watch.go:266.
	project::WatcherID ID() { return Watchers().WatcherID; }

	const std::string& Name() const { return name; }
	lsp::lsproto::WatchKind WatchKind() const { return watchKind; }

	// Clone — watch.go:281.
	WatchedFiles* Clone(const T& input) {
		std::shared_lock<std::shared_mutex> lk(mu);
		auto* w = new WatchedFiles(name, watchKind,
		                           hasRelativePatternCapability,
		                           computeGlobPatterns);
		w->workspaceWatchers = workspaceWatchers;
		w->outsideWorkspaceWatchers = outsideWorkspaceWatchers;
		w->input = input;
		return w;
	}
};

// Nil-safe ID — Go `(w *WatchedFiles).ID()` returns "" on nil.
template <typename T>
inline WatcherID watchedFilesID(WatchedFiles<T>* w) {
	if (w == nullptr) {
		return "";
	}
	return w->ID();
}

// Nil-safe Clone — Go `(w *WatchedFiles).Clone(input)` returns nil on nil.
template <typename T>
inline WatchedFiles<T>* watchedFilesClone(WatchedFiles<T>* w, const T& input) {
	if (w == nullptr) {
		return nullptr;
	}
	return w->Clone(input);
}

// NewWatchedFiles — watch.go:174.
template <typename T>
inline WatchedFiles<T>* newWatchedFiles(
	const std::string& name, lsp::lsproto::WatchKind watchKind,
	bool hasRelativePatternCapability,
	std::function<PatternsAndIgnored(T)> computeGlobPatterns) {
	return new WatchedFiles<T>(name, watchKind,
	                           hasRelativePatternCapability,
	                           std::move(computeGlobPatterns));
}

// NewWatchedFilesForPaths — watch.go:186. Creates a watcher for exact file
// paths, routing files outside the workspace through directory-based
// external watchers so clients can use URI-based RelativePatterns when
// supported.
inline WatchedFiles<std::vector<std::string>>* newWatchedFilesForPaths(
	const std::string& name, lsp::lsproto::WatchKind watchKind,
	bool hasRelativePatternCapability, const std::string& workspaceDirectory,
	const std::string& currentDirectory, bool useCaseSensitiveFileNames) {
	tspath::ComparePathsOptions comparePathsOptions{
	    useCaseSensitiveFileNames, currentDirectory};
	return newWatchedFiles<std::vector<std::string>>(
	    name, watchKind, hasRelativePatternCapability,
	    [workspaceDirectory,
	     comparePathsOptions](const std::vector<std::string>& files)
	        -> PatternsAndIgnored {
		    PatternsAndIgnored result;
		    for (const auto& file : files) {
			    if (tspath::containsPath(workspaceDirectory, file,
			                             comparePathsOptions)) {
				    result.patternsInsideWorkspace.push_back(file);
			    } else {
				    result.directoriesOutsideWorkspace.push_back(
				        std::string(tspath::getDirectoryPath(file)));
			    }
		    }
		    return result;
	    });
}

// createResolutionLookupGlobMapper — watch.go:298.
std::function<PatternsAndIgnored(collections::SyncMap<tspath::Path, std::string>*)>
createResolutionLookupGlobMapper(const std::string& workspaceDirectory,
                                 const std::string& libDirectory,
                                 const std::string& currentDirectory,
                                 bool useCaseSensitiveFileNames);

// getTypingsLocationsGlobs — watch.go:380.
PatternsAndIgnored getTypingsLocationsGlobs(
	const std::vector<std::string>& typingsFiles,
	const std::string& typingsLocation, const std::string& workspaceDirectory,
	const std::string& currentDirectory, bool useCaseSensitiveFileNames);

// getPathComponentsForWatching — watch.go:424.
std::vector<std::string> getPathComponentsForWatching(
	std::string_view path, std::string_view currentDirectory);

// perceivedOsRootLengthForWatching — watch.go:434.
int perceivedOsRootLengthForWatching(
	const std::vector<std::string>& pathComponents);

} // namespace tsc::project
