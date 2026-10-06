#pragma once

// snapshotfs.go — SnapshotFS + snapshotFSBuilder + sourceFS +
// cachedLayeredFileSystem + realpathAliasSet. Declarations here; bodies in
// snapshotfs.cpp.

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "internal/ast/ast.h" // tscUnreachable
#include "internal/collections/collections.h"
#include "internal/core/utilities.h" // workGroup
#include "internal/ls/lsconv/lsconv.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/project/dirty/dirty.h"
#include "internal/project/filechange.h"
#include "internal/project/files.h"
#include "internal/project/overlayfs.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/cachedvfs/cachedvfs.h"
#include "internal/vfs/vfs.h"
#include "internal/xxh3/xxh3.h"

namespace tsc::project {

// cachedLayeredFileSystem — snapshotfs.go:33. Wraps a LayeredFileSystem in a
// cachedvfs.FS (method calls hit the cache; GetFile/Overlays delegate to the
// layered source).
struct cachedLayeredFileSystem : LayeredFileSystem {
	std::shared_ptr<vfs::cachedvfs::FS> cached;
	LayeredFileSystem* layered;

	// vfs::FS forwards to the cached layer.
	bool UseCaseSensitiveFileNames() override {
		return cached->UseCaseSensitiveFileNames();
	}
	bool FileExists(const std::string& path) override {
		return cached->FileExists(path);
	}
	std::pair<std::string, bool>
	ReadFile(const std::string& path) override {
		return cached->ReadFile(path);
	}
	vfs::Error WriteFile(const std::string& path,
	                     const std::string& data) override {
		return cached->WriteFile(path, data);
	}
	vfs::Error AppendFile(const std::string& path,
	                      const std::string& data) override {
		return cached->AppendFile(path, data);
	}
	vfs::Error Remove(const std::string& path) override {
		return cached->Remove(path);
	}
	vfs::Error Chtimes(const std::string& path, vfs::TimePoint aTime,
	                   vfs::TimePoint mTime) override {
		return cached->Chtimes(path, aTime, mTime);
	}
	bool DirectoryExists(const std::string& path) override {
		return cached->DirectoryExists(path);
	}
	vfs::Entries
	GetAccessibleEntries(const std::string& path) override {
		return cached->GetAccessibleEntries(path);
	}
	std::shared_ptr<vfs::FileInfo>
	Stat(const std::string& path) override {
		return cached->Stat(path);
	}
	std::string Realpath(const std::string& path) override {
		return cached->Realpath(path);
	}

	// FileHandleSource delegates to the layered source (uncached).
	FileHandle* GetFile(const std::string& fileName) override {
		return layered->GetFile(fileName);
	}
	FileHandle* GetFileByPath(const std::string& fileName,
	                          const tspath::Path& path) override {
		return layered->GetFileByPath(fileName, path);
	}
	std::unordered_map<tspath::Path, Overlay*> Overlays() override {
		return layered->Overlays();
	}

	// ExpandFileChanges — snapshotfs.go:57.
	FileChangeSummary ExpandFileChanges(FileChangeSummary change) {
		if (auto* expander = dynamic_cast<FileChangeExpander*>(layered)) {
			return expander->ExpandFileChanges(change);
		}
		return change;
	}
};

// newCachedLayeredFileSystem — snapshotfs.go:38.
inline LayeredFileSystem*
newCachedLayeredFileSystem(LayeredFileSystem* fileSystem) {
	auto* fs = new cachedLayeredFileSystem();
	fs->cached = vfs::cachedvfs::From(fileSystem);
	fs->layered = fileSystem;
	return fs;
}

// realpathAliasSet — snapshotfs.go:71. A thread-safe set of symlink paths
// that alias a single realpath. Implements dirty.Cloneable so it can be
// used as a value in dirty.SyncMap.
struct realpathAliasSet {
	std::mutex mu;
	collections::Set<tspath::Path> paths;

	void Add(const tspath::Path& path) {
		std::lock_guard<std::mutex> lk(mu);
		paths.Add(path);
	}

	realpathAliasSet* Clone() {
		std::lock_guard<std::mutex> lk(mu);
		auto* clone = new realpathAliasSet();
		if (paths.Len() > 0) {
			clone->paths = paths.Clone();
		}
		return clone;
	}
};

// memoizedCachedFile — snapshotfs.go:104 (Go `memoizedCachedFile func()
// FileHandle` created by sync.OnceValue). Heap object so it can be shared
// through the SyncMap; call() computes the value exactly once.
struct memoizedCachedFile {
	std::once_flag once;
	FileHandle* result = nullptr;
	std::function<FileHandle*()> fn;

	FileHandle* call() {
		std::call_once(once, [&] { result = fn(); });
		return result;
	}
};

// SnapshotFS — snapshotfs.go:92.
struct SnapshotFS : FileSource {
	std::function<tspath::Path(const std::string&)> toPath;
	LayeredFileSystem* fs;
	std::unordered_map<tspath::Path, cachedFile*> cacheFiles;
	std::unordered_map<tspath::Path,
	                   dirty::CloneableMap<tspath::Path, std::string>>
	    cacheDirectories;
	collections::SyncMap<tspath::Path,
	                     std::shared_ptr<memoizedCachedFile>>
	    readFiles;
	// nodeModulesRealpathAliases maps realpath-based keys to sets of
	// symlink-based keys, for files inside node_modules that are accessed
	// through directory symlinks. This allows watch events (which use
	// realpaths) to invalidate files cached under symlink paths.
	std::unordered_map<tspath::Path, realpathAliasSet*>
	    nodeModulesRealpathAliases;

	vfs::FS* FS() override { return fs; }

	FileHandle* GetFile(const std::string& fileName) override {
		return GetFileByPath(fileName, toPath(fileName));
	}

	bool FileExists(const std::string& fileName,
	                const tspath::Path& path) override {
		if (cacheFiles.count(path) != 0) {
			return true;
		}
		return fs->FileExists(fileName);
	}

	FileHandle* GetFileByPath(const std::string& fileName,
	                          const tspath::Path& path) override;

	vfs::Entries
	GetAccessibleEntries(const std::string& directoryName) override;

	// expandRealpathAliases — snapshotfs.go:578. Adds synthetic URIs to the
	// Changed and Deleted sets for files that were accessed through
	// node_modules symlinks. When a watch event arrives using a realpath,
	// this expands it to include the symlink-based path so that downstream
	// consumers (markDirtyFiles, markFilesChanged) can find cached
	// entries.
	FileChangeSummary expandRealpathAliases(FileChangeSummary change);
};

// mergeCachedDirectoryEntries — snapshotfs.go:144.
vfs::Entries mergeCachedDirectoryEntries(
	vfs::Entries directoryEntries,
	const dirty::CloneableMap<tspath::Path, std::string>& cachedEntries,
	const std::function<bool(const tspath::Path&)>& isCachedFile,
	bool useCaseSensitiveFileNames);

// snapshotFSBuilder — snapshotfs.go:177.
struct snapshotFSBuilder : FileSource {
	LayeredFileSystem* fs;
	dirty::SyncMap<tspath::Path, cachedFile*>* cacheFiles;
	dirty::Map<tspath::Path, dirty::CloneableMap<tspath::Path, std::string>>*
	    cacheDirectories;
	collections::Set<tspath::Path> sourceBackedReplacements;
	dirty::SyncMap<tspath::Path, realpathAliasSet*>*
	    nodeModulesRealpathAliases;
	std::function<tspath::Path(const std::string&)> toPath;

	vfs::FS* FS() override { return fs; }

	// Finalize — snapshotfs.go:208.
	std::pair<SnapshotFS*, bool> Finalize();

	FileHandle* GetFile(const std::string& fileName) override {
		return GetFileByPath(fileName, toPath(fileName));
	}

	// deleteCacheEntry — snapshotfs.go:305.
	void deleteCacheEntry(
		const std::shared_ptr<dirty::SyncMapEntry<tspath::Path,
		                                        cachedFile*>>& entry);

	bool FileExists(const std::string& fileName,
	                const tspath::Path& path) override;

	FileHandle* GetFileByPath(const std::string& fileName,
	                          const tspath::Path& path) override;

	vfs::Entries
	GetAccessibleEntries(const std::string& path) override;

	// cacheSourceFile — snapshotfs.go:348.
	FileHandle* cacheSourceFile(const std::string& fileName,
	                            const tspath::Path& path,
	                            FileHandle* source);

	// getCachedFile — snapshotfs.go:361.
	FileHandle* getCachedFile(const std::string& fileName,
	                          const tspath::Path& path, bool forceReload);

	// recordRealpathAlias — snapshotfs.go:375. Checks if fileName is
	// accessed through a symlink and, if so, records a mapping from the
	// realpath-based key to the symlink-based key. This is only called for
	// files inside node_modules where symlinks are common.
	void recordRealpathAlias(
		const std::shared_ptr<
		    dirty::SyncMapEntry<tspath::Path, cachedFile*>>&
		    cachedFileEntry,
		const std::string& symlinkFileName,
		const tspath::Path& symlinkPath);

	// reloadEntry — snapshotfs.go:392.
	FileHandle* reloadEntry(
		const std::shared_ptr<
		    dirty::SyncMapEntry<tspath::Path, cachedFile*>>& entry);

	// reloadEntryIfNeeded — snapshotfs.go:424.
	FileHandle* reloadEntryIfNeeded(
		const std::shared_ptr<
		    dirty::SyncMapEntry<tspath::Path, cachedFile*>>& entry);

	// watchChangesOverlapCache — snapshotfs.go:455.
	bool watchChangesOverlapCache(
		const FileChangeSummary& change,
		const std::unordered_map<tspath::Path, FileHandle*>&
		    previousOpenFiles,
		const std::unordered_map<tspath::Path, FileHandle*>& openFiles);

	// invalidateCache — snapshotfs.go:483.
	void invalidateCache();

	// invalidateNodeModulesCache — snapshotfs.go:492.
	void invalidateNodeModulesCache();

	// markDirtyFiles — snapshotfs.go:503.
	FileChangeSummary markDirtyFiles(FileChangeSummary change);

	// reloadEntryIfContentChanged — snapshotfs.go:540.
	bool reloadEntryIfContentChanged(
		const std::shared_ptr<
		    dirty::SyncMapEntry<tspath::Path, cachedFile*>>& entry);

	// isRelevantFileName — snapshotfs.go:615.
	bool isRelevantFileName(
		const lsp::lsproto::DocumentUri& uri,
		const std::vector<std::string>& contentMapperExtensions,
		const collections::Set<tspath::Path>* contentMapperWatchedFiles,
		const std::unordered_map<tspath::Path, FileHandle*>& openFiles);

	// expandAndFilterWatchEvents — snapshotfs.go:651. Expands directory
	// deletion URIs into individual file deletion URIs using the cached
	// directory structure, and filters out watch events for paths that are
	// neither known directories nor have relevant file extensions.
	FileChangeSummary expandAndFilterWatchEvents(
		FileChangeSummary change,
		const std::vector<std::string>& contentMapperExtensions,
		const collections::Set<tspath::Path>* contentMapperWatchedFiles,
		const std::unordered_map<tspath::Path, FileHandle*>&
		    previousOpenFiles,
		const std::unordered_map<tspath::Path, FileHandle*>& openFiles);

	// collectFilesRecursive — snapshotfs.go:709.
	void collectFilesRecursive(
		const tspath::Path& dirPath,
		collections::Set<lsp::lsproto::DocumentUri>* files,
		const std::unordered_map<tspath::Path, FileHandle*>&
		    previousOpenFiles,
		const std::unordered_map<tspath::Path, FileHandle*>& openFiles);

	// convertOpenAndCloseToChanges — snapshotfs.go:734.
	FileChangeSummary convertOpenAndCloseToChanges(
		FileChangeSummary change,
		const std::unordered_map<tspath::Path, FileHandle*>&
		    previousOpenFiles,
		const std::unordered_map<tspath::Path, FileHandle*>& openFiles);
};

// newSnapshotFSBuilderFromSource — snapshotfs.go:186.
snapshotFSBuilder* newSnapshotFSBuilderFromSource(
	LayeredFileSystem* fs,
	std::unordered_map<tspath::Path, cachedFile*> cacheFiles,
	std::unordered_map<tspath::Path,
	                   dirty::CloneableMap<tspath::Path, std::string>>
	    cacheDirectories,
	std::unordered_map<tspath::Path, realpathAliasSet*>
	    nodeModulesRealpathAliases,
	std::function<tspath::Path(const std::string&)> toPath);

// isRelevantExtension — snapshotfs.go:639.
bool isRelevantExtension(const std::string& ext);

// isNodeModulesPath — snapshotfs.go:688. Reports whether path is a
// node_modules directory itself or lives inside one. Used to preserve
// node_modules watch deletions, whose package files are read transiently
// and therefore never tracked in cacheDirectories.
bool isNodeModulesPath(const tspath::Path& path);

// hasOpenFileWithin — snapshotfs.go:693.
bool hasOpenFileWithin(
	const tspath::Path& path,
	const std::unordered_map<tspath::Path, FileHandle*>& previousOpenFiles,
	const std::unordered_map<tspath::Path, FileHandle*>& openFiles);

// sourceFS — snapshotfs.go:769. A vfs.FS that sources files from a
// FileSource and tracks seen files.
struct sourceFS : vfs::FS {
	std::atomic<bool> tracking{false};
	std::function<tspath::Path(const std::string&)> toPath;
	collections::SyncSet<tspath::Path>* missingDirectories = nullptr;
	collections::SyncSet<tspath::Path>* seenFiles = nullptr;
	FileSource* source = nullptr;

	void DisableTracking() { tracking = false; }

	void Track(const std::string& fileName) {
		if (!tracking.load()) {
			return;
		}
		seenFiles->Add(toPath(fileName));
	}

	bool SeenFile(const tspath::Path& path) {
		if (seenFiles == nullptr) {
			return false;
		}
		return seenFiles->Has(path);
	}

	bool SeenFileOrMissingParentDirectory(tspath::Path path) {
		if (seenFiles != nullptr && seenFiles->Has(path)) {
			return true;
		}
		if (missingDirectories != nullptr && !missingDirectories->IsEmpty()) {
			for (;;) {
				if (missingDirectories->Has(path)) {
					return true;
				}
				auto parent = tspath::getDirectoryPath(path);
				if (parent == path) {
					break;
				}
				path = parent;
			}
		}
		return false;
	}

	FileHandle* GetFile(const std::string& fileName) {
		Track(fileName);
		return source->GetFile(fileName);
	}

	FileHandle* GetFileByPath(const std::string& fileName,
	                          const tspath::Path& path) {
		Track(fileName);
		return source->GetFileByPath(fileName, path);
	}

	// DirectoryExists implements vfs.FS.
	bool DirectoryExists(const std::string& path) override {
		bool exists = source->FS()->DirectoryExists(path);
		if (!exists && tracking.load()) {
			missingDirectories->Add(toPath(path));
		}
		return exists;
	}

	// FileExists implements vfs.FS.
	bool FileExists(const std::string& path) override {
		Track(path);
		return source->FileExists(path, toPath(path));
	}

	// GetAccessibleEntries implements vfs.FS.
	vfs::Entries
	GetAccessibleEntries(const std::string& path) override {
		return source->GetAccessibleEntries(path);
	}

	// ReadFile implements vfs.FS.
	std::pair<std::string, bool>
	ReadFile(const std::string& path) override {
		if (auto* fh = GetFile(path); fh != nullptr) {
			return {fh->Content(), true};
		}
		return {"", false};
	}

	// Realpath implements vfs.FS.
	std::string Realpath(const std::string& path) override {
		return source->FS()->Realpath(path);
	}

	// Stat implements vfs.FS.
	std::shared_ptr<vfs::FileInfo>
	Stat(const std::string& path) override {
		return source->FS()->Stat(path);
	}

	// UseCaseSensitiveFileNames implements vfs.FS.
	bool UseCaseSensitiveFileNames() override {
		return source->FS()->UseCaseSensitiveFileNames();
	}

	// WriteFile implements vfs.FS.
	vfs::Error WriteFile(const std::string& path,
	                     const std::string& data) override {
		TSC_UNREACHABLE("sourceFS.WriteFile — unimplemented");
	}
	vfs::Error AppendFile(const std::string& path,
	                      const std::string& data) override {
		TSC_UNREACHABLE("sourceFS.AppendFile — unimplemented");
	}
	vfs::Error Remove(const std::string& path) override {
		TSC_UNREACHABLE("sourceFS.Remove — unimplemented");
	}
	vfs::Error Chtimes(const std::string& path, vfs::TimePoint atime,
	                   vfs::TimePoint mtime) override {
		TSC_UNREACHABLE("sourceFS.Chtimes — unimplemented");
	}
};

// newSourceFS — snapshotfs.go:777.
inline sourceFS* newSourceFS(
	bool tracking, FileSource* source,
	std::function<tspath::Path(const std::string&)> toPath) {
	auto* fs = new sourceFS();
	fs->tracking = tracking;
	fs->toPath = std::move(toPath);
	fs->source = source;
	if (tracking) {
		fs->seenFiles = new collections::SyncSet<tspath::Path>();
		fs->missingDirectories = new collections::SyncSet<tspath::Path>();
	}
	return fs;
}

} // namespace tsc::project
