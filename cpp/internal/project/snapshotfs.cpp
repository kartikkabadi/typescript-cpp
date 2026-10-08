// === slice: project ===
// snapshotfs.go — bodies for SnapshotFS / snapshotFSBuilder / helpers.

#include "internal/project/snapshotfs.h"

#include <algorithm>
#include <cstring>

namespace tsc::project {

// SnapshotFS.GetFileByPath — snapshotfs.go:121.
FileHandle* SnapshotFS::GetFileByPath(const std::string& fileName,
                                      const tspath::Path& path) {
	if (auto it = cacheFiles.find(path); it != cacheFiles.end()) {
		return it->second;
	}
	auto newEntry = std::make_shared<memoizedCachedFile>();
	newEntry->fn = [this, fileName, path] {
		return fs->GetFileByPath(fileName, path);
	};
	auto [entry, _] = readFiles.LoadOrStore(path, newEntry);
	return entry->call();
}

// SnapshotFS.GetAccessibleEntries — snapshotfs.go:132.
vfs::Entries
SnapshotFS::GetAccessibleEntries(const std::string& directoryName) {
	auto lowerEntries = fs->GetAccessibleEntries(directoryName);
	auto it = cacheDirectories.find(toPath(directoryName));
	if (it == cacheDirectories.end()) {
		return lowerEntries;
	}
	return mergeCachedDirectoryEntries(
	    lowerEntries, it->second,
	    [&](const tspath::Path& path) -> bool {
		    bool cached = cacheFiles.count(path) != 0;
		    return cached || fs->FileExists(path);
	    },
	    fs->UseCaseSensitiveFileNames());
}

// mergeCachedDirectoryEntries — snapshotfs.go:144.
vfs::Entries mergeCachedDirectoryEntries(
	vfs::Entries directoryEntries,
	const dirty::CloneableMap<tspath::Path, std::string>& cachedEntries,
	const std::function<bool(const tspath::Path&)>& isCachedFile,
	bool useCaseSensitiveFileNames) {
	vfs::Entries entries;
	entries.symlinks = directoryEntries.symlinks; // maps.Clone
	auto equalName = [&](const std::string& left,
	                     const std::string& right) {
		return tspath::getCanonicalFileName(left,
		                                    useCaseSensitiveFileNames) ==
		       tspath::getCanonicalFileName(right,
		                                    useCaseSensitiveFileNames);
	};
	auto hasName = [&](const std::vector<std::string>& names,
	                   const std::string& name) {
		return std::any_of(names.begin(), names.end(),
		                   [&](const std::string& candidate) {
			                   return equalName(candidate, name);
		                   });
	};
	for (const auto& kv : cachedEntries) {
		const auto& childPath = kv.first;
		const auto& childName = kv.second;
		if (entries.symlinks) {
			for (auto it = entries.symlinks->begin();
			     it != entries.symlinks->end();) {
				if (equalName(*it, childName)) {
					it = entries.symlinks->erase(it);
				} else {
					++it;
				}
			}
		}
		if (isCachedFile(childPath)) {
			entries.files.push_back(childName);
		} else {
			entries.directories.push_back(childName);
		}
	}
	for (const auto& fileName : directoryEntries.files) {
		if (!hasName(entries.files, fileName) &&
		    !hasName(entries.directories, fileName)) {
			entries.files.push_back(fileName);
		}
	}
	for (const auto& directoryName : directoryEntries.directories) {
		if (!hasName(entries.files, directoryName) &&
		    !hasName(entries.directories, directoryName)) {
			entries.directories.push_back(directoryName);
		}
	}
	return entries;
}

// snapshotFSBuilder.Finalize — snapshotfs.go:208.
std::pair<SnapshotFS*, bool> snapshotFSBuilder::Finalize() {
	// Synchronize directory structure based on added and deleted cache
	// entries.
	std::function<void(const tspath::Path&)> onDeletedFileOrDirectory;
	std::unordered_map<tspath::Path, cachedFile*> deleted;

	auto onAddedFile = [&](const tspath::Path& path,
	                       const std::string& fileName) {
		auto childPath = path;
		auto child = fileName;
		for (;;) {
			auto parentPath = tspath::getDirectoryPath(childPath);
			auto parent = tspath::getDirectoryPath(child);
			if (childPath == parentPath) {
				break; // reached root
			}
			auto baseName = std::string(tspath::getBaseFileName(child));
			auto getResult = cacheDirectories->Get(parentPath);
			if (getResult.second) {
				auto dirEntry = getResult.first;
				dirEntry->Change(
				    [&](dirty::CloneableMap<tspath::Path,
				                            std::string>& dir) {
					    dir[childPath] = baseName;
				    });
				break;
			}
			dirty::CloneableMap<tspath::Path, std::string> dir;
			dir[childPath] = baseName;
			cacheDirectories->Add(parentPath, dir);
			childPath = parentPath;
			child = parent;
		}
	};

	onDeletedFileOrDirectory = [&](const tspath::Path& path) {
		auto getResult =
		    cacheDirectories->Get(tspath::getDirectoryPath(path));
		if (!getResult.second) {
			return;
		}
		auto dirEntry = getResult.first;
		dirEntry->Change([&](dirty::CloneableMap<tspath::Path,
		                                         std::string>& dir) {
			dir.erase(path);
			if (dir.empty()) {
				dirEntry->Delete();
				onDeletedFileOrDirectory(dirEntry->Key());
			}
		});
	};

	dirty::FinalizationHooks<tspath::Path, cachedFile*> hooks;
	hooks.OnDelete = [&](const tspath::Path& key, cachedFile* value) {
		if (sourceBackedReplacements.Has(key)) {
			return;
		}
		deleted[key] = value;
	};
	hooks.OnAdd = [&](const tspath::Path& key, cachedFile* value) {
		onAddedFile(key, value->FileName());
	};
	auto [cacheFilesResult, changed] = cacheFiles->FinalizeWith(hooks);

	for (const auto& kv : deleted) {
		onDeletedFileOrDirectory(kv.first);
	}

	// Prune deleted symlink paths from realpath alias sets before
	// finalizing, so that empty sets are dropped during finalization.
	for (const auto& kv : deleted) {
		const auto& deletedPath = kv.first;
		auto* deletedFile = kv.second;
		if (deletedFile->realpathPath.empty()) {
			continue;
		}
		auto loadResult =
		    nodeModulesRealpathAliases->Load(deletedFile->realpathPath);
		if (loadResult.second) {
			auto entry = loadResult.first;
			entry->Locked([&](dirty::IValue<realpathAliasSet*>* e) {
				e->Change([&](realpathAliasSet*& aliasSet) {
					aliasSet->paths.Delete(deletedPath);
				});
				if (e->Value()->paths.Len() == 0) {
					e->Delete();
				}
			});
		}
	}

	auto [aliasesResult, aliasesChanged] =
	    nodeModulesRealpathAliases->Finalize();

	auto* snapshotFS = new SnapshotFS();
	snapshotFS->fs = fs;
	snapshotFS->cacheFiles = std::move(cacheFilesResult);
	snapshotFS->cacheDirectories = cacheDirectories->Finalize().first;
	snapshotFS->nodeModulesRealpathAliases = std::move(aliasesResult);
	snapshotFS->toPath = toPath;
	return {snapshotFS, changed || aliasesChanged};
}

// snapshotFSBuilder.deleteCacheEntry — snapshotfs.go:305.
void snapshotFSBuilder::deleteCacheEntry(
	const std::shared_ptr<dirty::SyncMapEntry<tspath::Path, cachedFile*>>&
	    entry) {
	if (auto* file = entry->Value();
	    file != nullptr && fs->FileExists(file->FileName())) {
		sourceBackedReplacements.Add(entry->Key());
	}
	entry->Delete();
}

// snapshotFSBuilder.FileExists — snapshotfs.go:312.
bool snapshotFSBuilder::FileExists(const std::string& fileName,
                                   const tspath::Path& path) {
	if (auto [entry, ok] = cacheFiles->Load(path); ok) {
		auto val = entry->Value();
		if (val == nullptr) {
			return false;
		}
		// Entry may be dirty - reload to check current state in the
		// source filesystem.
		return reloadEntryIfNeeded(entry) != nullptr;
	}
	// Path never loaded into cacheFiles - use cached stat (no file
	// read).
	return fs->FileExists(fileName);
}

// snapshotFSBuilder.GetFileByPath — snapshotfs.go:325.
FileHandle* snapshotFSBuilder::GetFileByPath(const std::string& fileName,
                                             const tspath::Path& path) {
	if (auto [entry, ok] = cacheFiles->Load(path); ok) {
		return reloadEntryIfNeeded(entry);
	}
	auto* file = fs->GetFileByPath(fileName, path);
	if (file == nullptr || file->IsOverlay()) {
		return file;
	}
	return cacheSourceFile(fileName, path, file);
}

// snapshotFSBuilder.GetAccessibleEntries — snapshotfs.go:336.
vfs::Entries
snapshotFSBuilder::GetAccessibleEntries(const std::string& path) {
	auto lowerEntries = fs->GetAccessibleEntries(path);
	auto [directory, ok] = cacheDirectories->Get(toPath(path));
	if (!ok) {
		return lowerEntries;
	}
	return mergeCachedDirectoryEntries(
	    lowerEntries, directory->Value(),
	    [&](const tspath::Path& p) -> bool {
		    auto [entry, cached] = cacheFiles->Load(p);
		    return (cached && entry->Value() != nullptr) ||
		           fs->FileExists(p);
	    },
	    fs->UseCaseSensitiveFileNames());
}

// snapshotFSBuilder.cacheSourceFile — snapshotfs.go:348.
FileHandle* snapshotFSBuilder::cacheSourceFile(const std::string& fileName,
                                               const tspath::Path& path,
                                               FileHandle* source) {
	auto* file = new cachedFile(fileName, source->Content());
	file->hash = source->Hash();
	auto [entry, loaded] = cacheFiles->LoadOrStore(path, file);
	if (entry == nullptr) {
		return nullptr;
	}
	if (!loaded && path.find("/node_modules/") != std::string::npos) {
		recordRealpathAlias(entry, fileName, path);
	}
	return reloadEntryIfNeeded(entry);
}

// snapshotFSBuilder.getCachedFile — snapshotfs.go:361.
FileHandle* snapshotFSBuilder::getCachedFile(const std::string& fileName,
                                             const tspath::Path& path,
                                             bool forceReload) {
	// Go: &cachedFile{fileName: fileName, needsReload: true} — the zero
	// hash is significant (the ctor would otherwise hash the empty
	// content).
	auto* placeholder = new cachedFile(fileName, "");
	placeholder->hash = xxh3::Uint128{};
	placeholder->needsReload = true;
	auto [entry, loaded] = cacheFiles->LoadOrStore(path, placeholder);
	if (entry != nullptr) {
		if (!loaded &&
		    path.find("/node_modules/") != std::string::npos) {
			recordRealpathAlias(entry, fileName, path);
		}
		if (forceReload) {
			return reloadEntry(entry);
		}
		return reloadEntryIfNeeded(entry);
	}
	return nullptr;
}

// snapshotFSBuilder.recordRealpathAlias — snapshotfs.go:378.
void snapshotFSBuilder::recordRealpathAlias(
	const std::shared_ptr<dirty::SyncMapEntry<tspath::Path, cachedFile*>>&
	    cachedFileEntry,
	const std::string& symlinkFileName, const tspath::Path& symlinkPath) {
	auto realpath = fs->Realpath(symlinkFileName);
	auto realpathPath = toPath(realpath);
	if (realpathPath != symlinkPath) {
		cachedFileEntry->Change([&](cachedFile*& file) {
			file->realpathPath = realpathPath;
		});
		auto loadResult = nodeModulesRealpathAliases->LoadOrStore(
		    realpathPath, new realpathAliasSet());
		loadResult.first->Change([&](realpathAliasSet*& aliasSet) {
			aliasSet->Add(symlinkPath);
		});
	}
}

// snapshotFSBuilder.reloadEntry — snapshotfs.go:392.
FileHandle* snapshotFSBuilder::reloadEntry(
	const std::shared_ptr<dirty::SyncMapEntry<tspath::Path, cachedFile*>>&
	    entry) {
	std::string fileName;
	entry->Locked([&](dirty::IValue<cachedFile*>* e) {
		if (e->Value() != nullptr) {
			fileName = e->Value()->fileName;
		}
	});
	if (fileName.empty()) {
		return nullptr;
	}
	// Read file outside the lock to avoid blocking other goroutines.
	auto readResult = fs->ReadFile(fileName);
	auto content = std::move(readResult.first);
	auto ok = readResult.second;
	entry->Locked([&](dirty::IValue<cachedFile*>* e) {
		if (e->Value() == nullptr) {
			return;
		}
		if (ok) {
			e->Change([&](cachedFile*& file) {
				file->content = content;
				file->hash = xxh3::hash128(content);
				file->needsReload = false;
			});
		} else {
			e->Delete();
		}
	});
	if (entry->Value() == nullptr) {
		return nullptr;
	}
	return entry->Value();
}

// snapshotFSBuilder.reloadEntryIfNeeded — snapshotfs.go:424.
FileHandle* snapshotFSBuilder::reloadEntryIfNeeded(
	const std::shared_ptr<dirty::SyncMapEntry<tspath::Path, cachedFile*>>&
	    entry) {
	std::string fileName;
	entry->Locked([&](dirty::IValue<cachedFile*>* e) {
		if (e->Value() != nullptr && !e->Value()->MatchesDiskText()) {
			fileName = e->Value()->fileName;
		}
	});
	if (!fileName.empty()) {
		// Read file outside the lock to avoid blocking other
		// goroutines.
		auto readResult = fs->ReadFile(fileName);
		auto content = std::move(readResult.first);
		auto ok = readResult.second;
		entry->Locked([&](dirty::IValue<cachedFile*>* e) {
			if (e->Value() == nullptr || e->Value()->MatchesDiskText()) {
				return; // another goroutine already reloaded it
			}
			if (ok) {
				e->Change([&](cachedFile*& file) {
					file->content = content;
					file->hash = xxh3::hash128(content);
					file->needsReload = false;
				});
			} else {
				e->Delete();
			}
		});
	}
	if (entry->Value() == nullptr) {
		return nullptr;
	}
	return entry->Value();
}

// snapshotFSBuilder.watchChangesOverlapCache — snapshotfs.go:455.
bool snapshotFSBuilder::watchChangesOverlapCache(
	const FileChangeSummary& change,
	const std::unordered_map<tspath::Path, FileHandle*>&
	    previousOpenFiles,
	const std::unordered_map<tspath::Path, FileHandle*>& openFiles) {
	for (const auto& uri : change.Changed.Keys()) {
		auto path = toPath(lsp::lsproto::documentUriFileName(uri));
		if ((previousOpenFiles.count(path) &&
		     previousOpenFiles.at(path) != nullptr) ||
		    (openFiles.count(path) && openFiles.at(path) != nullptr)) {
			return true;
		}
		if (cacheFiles->Load(path).second) {
			return true;
		}
		if (nodeModulesRealpathAliases->Load(path).second) {
			return true;
		}
	}
	for (const auto& uri : change.Deleted.Keys()) {
		auto path = toPath(lsp::lsproto::documentUriFileName(uri));
		if ((previousOpenFiles.count(path) &&
		     previousOpenFiles.at(path) != nullptr) ||
		    (openFiles.count(path) && openFiles.at(path) != nullptr)) {
			return true;
		}
		if (cacheFiles->Load(path).second) {
			return true;
		}
		if (nodeModulesRealpathAliases->Load(path).second) {
			return true;
		}
	}
	return false;
}

// snapshotFSBuilder.invalidateCache — snapshotfs.go:483.
void snapshotFSBuilder::invalidateCache() {
	cacheFiles->Range(
	    [](const std::shared_ptr<
	        dirty::SyncMapEntry<tspath::Path, cachedFile*>>& entry) {
		    entry->Change(
		        [](cachedFile*& file) { file->needsReload = true; });
		    return true;
	    });
}

// snapshotFSBuilder.invalidateNodeModulesCache — snapshotfs.go:492.
void snapshotFSBuilder::invalidateNodeModulesCache() {
	cacheFiles->Range(
	    [](const std::shared_ptr<
	        dirty::SyncMapEntry<tspath::Path, cachedFile*>>& entry) {
		    if (entry->Key().find("/node_modules/") !=
		        std::string::npos) {
			    entry->Change([](cachedFile*& file) {
				    file->needsReload = true;
			    });
		    }
		    return true;
	    });
}

// snapshotFSBuilder.markDirtyFiles — snapshotfs.go:503.
FileChangeSummary
snapshotFSBuilder::markDirtyFiles(FileChangeSummary change) {
	if (change.Changed.Len() > 0) {
		collections::SyncSet<lsp::lsproto::DocumentUri> filteredChanged;
		std::unique_ptr<workGroup> wg(newWorkGroup(false));
		for (const auto& uri : change.Changed.Keys()) {
			auto path =
			    toPath(lsp::lsproto::documentUriFileName(uri));
			if (auto* file =
			        fs->GetFileByPath(
			            lsp::lsproto::documentUriFileName(uri), path);
			    file != nullptr && file->IsOverlay()) {
				filteredChanged.Add(uri);
				continue;
			}
			auto loadResult = cacheFiles->Load(path);
			if (!loadResult.second) {
				filteredChanged.Add(uri);
				continue;
			}
			auto entry = loadResult.first;
			wg->Queue([this, entry, uri, &filteredChanged] {
				if (reloadEntryIfContentChanged(entry)) {
					filteredChanged.Add(uri);
				}
			});
		}
		wg->RunAndWait();
		collections::Set<lsp::lsproto::DocumentUri> newChanged(
		    filteredChanged.Size());
		filteredChanged.Range(
		    [&](const lsp::lsproto::DocumentUri& uri) {
			    newChanged.Add(uri);
			    return true;
		    });
		change.Changed = std::move(newChanged);
	}
	for (const auto& uri : change.Deleted.Keys()) {
		auto path = toPath(lsp::lsproto::documentUriFileName(uri));
		auto loadResult = cacheFiles->Load(path);
		if (loadResult.second) {
			deleteCacheEntry(loadResult.first);
		}
	}
	return change;
}

// snapshotFSBuilder.reloadEntryIfContentChanged — snapshotfs.go:540.
bool snapshotFSBuilder::reloadEntryIfContentChanged(
	const std::shared_ptr<dirty::SyncMapEntry<tspath::Path, cachedFile*>>&
	    entry) {
	auto* file = entry->Value();
	if (file == nullptr) {
		return true;
	}
	auto readResult = fs->ReadFile(file->fileName);
	auto content = std::move(readResult.first);
	auto ok = readResult.second;
	bool changed = true;
	entry->Locked([&](dirty::IValue<cachedFile*>* e) {
		auto cur = e->Value();
		if (cur == nullptr) {
			return;
		}
		if (!ok) {
			e->Delete();
			return;
		}
		if (content == cur->content) {
			changed = false;
			if (!cur->MatchesDiskText()) {
				e->Change([](cachedFile*& file) {
					file->needsReload = false;
				});
			}
			return;
		}
		e->Change([&](cachedFile*& file) {
			file->content = content;
			file->hash = xxh3::hash128(content);
			file->needsReload = false;
		});
	});
	return changed;
}

// SnapshotFS.expandRealpathAliases — snapshotfs.go:578.
FileChangeSummary
SnapshotFS::expandRealpathAliases(FileChangeSummary change) {
	if (nodeModulesRealpathAliases.empty()) {
		return change;
	}

	collections::Set<lsp::lsproto::DocumentUri> additionalChanged;
	for (const auto& uri : change.Changed.Keys()) {
		auto path = toPath(lsp::lsproto::documentUriFileName(uri));
		if (auto it = nodeModulesRealpathAliases.find(path);
		    it != nodeModulesRealpathAliases.end()) {
			auto* aliases = it->second;
			for (const auto& aliasPath : aliases->paths.Keys()) {
				additionalChanged.Add(
				    lsp::lsproto::URI(
				        lsconv::FileNameToDocumentURI(aliasPath)));
			}
		}
	}
	for (const auto& uri : additionalChanged.Keys()) {
		change.Changed.Add(uri);
	}

	collections::Set<lsp::lsproto::DocumentUri> additionalDeleted;
	for (const auto& uri : change.Deleted.Keys()) {
		auto path = toPath(lsp::lsproto::documentUriFileName(uri));
		if (auto it = nodeModulesRealpathAliases.find(path);
		    it != nodeModulesRealpathAliases.end()) {
			auto* aliases = it->second;
			for (const auto& aliasPath : aliases->paths.Keys()) {
				additionalDeleted.Add(
				    lsp::lsproto::URI(
				        lsconv::FileNameToDocumentURI(aliasPath)));
			}
		}
	}
	for (const auto& uri : additionalDeleted.Keys()) {
		change.Deleted.Add(uri);
	}

	return change;
}

// snapshotFSBuilder.isRelevantFileName — snapshotfs.go:615.
bool snapshotFSBuilder::isRelevantFileName(
	const lsp::lsproto::DocumentUri& uri,
	const std::vector<std::string>& contentMapperExtensions,
	const collections::Set<tspath::Path>* contentMapperWatchedFiles,
	const std::unordered_map<tspath::Path, FileHandle*>& openFiles) {
	auto fileName = lsp::lsproto::documentUriFileName(uri);
	if (contentMapperWatchedFiles != nullptr &&
	    contentMapperWatchedFiles->Has(toPath(fileName))) {
		return true;
	}
	std::vector<std::string_view> extViews(
	    contentMapperExtensions.begin(), contentMapperExtensions.end());
	if (tspath::fileExtensionIsOneOf(fileName, extViews)) {
		return true;
	}
	if (tspath::isDynamicFileName(fileName)) {
		return true;
	}
	auto path = toPath(fileName);
	if (openFiles.count(path) != 0) {
		return true;
	}
	auto i = path.find_last_of('.');
	if (i == std::string::npos) {
		return false;
	}
	return isRelevantExtension(path.substr(i));
}

// isRelevantExtension — snapshotfs.go:639.
bool isRelevantExtension(const std::string& ext) {
	static const std::unordered_set<std::string> exts = {
	    ".js", ".jsx", ".mjs", ".cjs", ".ts", ".tsx", ".mts", ".cts",
	    ".json"};
	return exts.count(ext) != 0;
}

// snapshotFSBuilder.expandAndFilterWatchEvents — snapshotfs.go:651.
FileChangeSummary snapshotFSBuilder::expandAndFilterWatchEvents(
	FileChangeSummary change,
	const std::vector<std::string>& contentMapperExtensions,
	const collections::Set<tspath::Path>* contentMapperWatchedFiles,
	const std::unordered_map<tspath::Path, FileHandle*>&
	    previousOpenFiles,
	const std::unordered_map<tspath::Path, FileHandle*>& openFiles) {
	if (change.Deleted.Len() > 0) {
		collections::Set<lsp::lsproto::DocumentUri> filteredDeleted;
		for (const auto& uri : change.Deleted.Keys()) {
			auto path =
			    toPath(lsp::lsproto::documentUriFileName(uri));
			if (cacheDirectories->Get(path).second ||
			    hasOpenFileWithin(path, previousOpenFiles,
			                      openFiles)) {
				collectFilesRecursive(path, &filteredDeleted,
				                      previousOpenFiles, openFiles);
			} else if (isRelevantFileName(uri,
			                              contentMapperExtensions,
			                              contentMapperWatchedFiles,
			                              openFiles) ||
			           isNodeModulesPath(path)) {
				// node_modules deletions must always be preserved for
				// auto-import registry change handlers.
				// They won't be in cacheDirectories since the registry
				// doesn't use the snapshotFSBuilder for
				// its file system, since we don't want to retain
				// files read there.
				filteredDeleted.Add(uri);
			}
		}
		change.Deleted = std::move(filteredDeleted);
	}

	if (change.Changed.Len() > 0) {
		collections::Set<lsp::lsproto::DocumentUri> filteredChanged;
		for (const auto& uri : change.Changed.Keys()) {
			if (isRelevantFileName(uri, contentMapperExtensions,
			                       contentMapperWatchedFiles,
			                       openFiles)) {
				filteredChanged.Add(uri);
			}
		}
		change.Changed = std::move(filteredChanged);
	}

	// We can't filter created events because any created path could be a
	// directory symlink that includes relevant files.
	// configFileRegistryBuilder will do check if these paths
	// are directories if they fall within a config's wildcard
	// directories.

	return change;
}

// isNodeModulesPath — snapshotfs.go:688.
bool isNodeModulesPath(const tspath::Path& path) {
	const std::string& s = path;
	return s.ends_with("/node_modules") ||
	       s.find("/node_modules/") != std::string::npos;
}

// hasOpenFileWithin — snapshotfs.go:693.
bool hasOpenFileWithin(
	const tspath::Path& path,
	const std::unordered_map<tspath::Path, FileHandle*>& previousOpenFiles,
	const std::unordered_map<tspath::Path, FileHandle*>& openFiles) {
	for (const auto& kv : openFiles) {
		if (tspath::pathContainsPath(path, kv.first)) {
			return true;
		}
	}
	for (const auto& kv : previousOpenFiles) {
		if (tspath::pathContainsPath(path, kv.first)) {
			return true;
		}
	}
	return false;
}

// snapshotFSBuilder.collectFilesRecursive — snapshotfs.go:709.
void snapshotFSBuilder::collectFilesRecursive(
	const tspath::Path& dirPath,
	collections::Set<lsp::lsproto::DocumentUri>* files,
	const std::unordered_map<tspath::Path, FileHandle*>&
	    previousOpenFiles,
	const std::unordered_map<tspath::Path, FileHandle*>& openFiles) {
	for (const auto& kv : openFiles) {
		if (tspath::pathContainsPath(dirPath, kv.first)) {
			files->Add(lsp::lsproto::URI(
			    lsconv::FileNameToDocumentURI(
			        kv.second->FileName())));
		}
	}
	for (const auto& kv : previousOpenFiles) {
		if (tspath::pathContainsPath(dirPath, kv.first)) {
			files->Add(lsp::lsproto::URI(
			    lsconv::FileNameToDocumentURI(
			        kv.second->FileName())));
		}
	}
	auto getResult = cacheDirectories->Get(dirPath);
	if (!getResult.second) {
		return;
	}
	for (const auto& childPath : getResult.first->Value()) {
		auto loadResult = cacheFiles->Load(childPath.first);
		if (loadResult.second) {
			if (auto* file = loadResult.first->Value();
			    file != nullptr) {
				files->Add(lsp::lsproto::URI(
				    lsconv::FileNameToDocumentURI(
				        file->FileName())));
			}
		}
		collectFilesRecursive(childPath.first, files,
		                      previousOpenFiles, openFiles);
	}
}

// snapshotFSBuilder.convertOpenAndCloseToChanges — snapshotfs.go:734.
FileChangeSummary snapshotFSBuilder::convertOpenAndCloseToChanges(
	FileChangeSummary change,
	const std::unordered_map<tspath::Path, FileHandle*>&
	    previousOpenFiles,
	const std::unordered_map<tspath::Path, FileHandle*>& openFiles) {
	if (!change.Opened.empty() &&
	    !tspath::isDynamicFileName(
	        lsp::lsproto::documentUriFileName(change.Opened))) {
		auto path = toPath(
		    lsp::lsproto::documentUriFileName(change.Opened));
		auto loadResult = cacheFiles->Load(path);
		if (!loadResult.second ||
		    loadResult.first->Original() == nullptr) {
			change.Created.Add(change.Opened);
		} else {
			auto entry = loadResult.first;
			auto it = openFiles.find(path);
			if (it != openFiles.end()) {
				// The file already exists in the program, but the
				// open-file content from
				// didOpen may differ from what was originally read
				// from the source (e.g. the
				// editor normalizes line endings, or the source file
				// changed since the
				// project was loaded). Mark it as Changed so the
				// project rebuilds.
				auto* openFile = it->second;
				if (auto* cached = entry->Original();
				    cached != nullptr &&
				    openFile->Hash() != cached->Hash()) {
					change.Changed.Add(change.Opened);
				}
				deleteCacheEntry(entry);
			}
		}
	}
	for (const auto& uri : change.Closed.Keys()) {
		auto fileName = lsp::lsproto::documentUriFileName(uri);
		if (tspath::isDynamicFileName(fileName)) {
			continue;
		}
		auto path = toPath(fileName);
		// We may have ignored watcher events while the file was open,
		// so force a reload.
		if (auto* fh = getCachedFile(fileName, path, true);
		    fh != nullptr) {
			auto it = previousOpenFiles.find(path);
			if (it != previousOpenFiles.end() &&
			    it->second != nullptr &&
			    fh->Hash() != it->second->Hash()) {
				change.Changed.Add(uri);
			}
			continue;
		}
		change.Deleted.Add(uri);
	}
	return change;
}

// newSnapshotFSBuilderFromSource — snapshotfs.go:186.
snapshotFSBuilder* newSnapshotFSBuilderFromSource(
	LayeredFileSystem* fs,
	std::unordered_map<tspath::Path, cachedFile*> cacheFiles,
	std::unordered_map<tspath::Path,
	                   dirty::CloneableMap<tspath::Path, std::string>>
	    cacheDirectories,
	std::unordered_map<tspath::Path, realpathAliasSet*>
	    nodeModulesRealpathAliases,
	std::function<tspath::Path(const std::string&)> toPath) {
	fs = newCachedLayeredFileSystem(fs);

	auto* builder = new snapshotFSBuilder();
	builder->fs = fs;
	builder->cacheFiles =
	    dirty::newSyncMap<tspath::Path, cachedFile*>(
	        std::move(cacheFiles));
	builder->cacheDirectories = dirty::newMap<
	    tspath::Path,
	    dirty::CloneableMap<tspath::Path, std::string>>(
	    std::move(cacheDirectories));
	builder->nodeModulesRealpathAliases =
	    dirty::newSyncMap<tspath::Path, realpathAliasSet*>(
	        std::move(nodeModulesRealpathAliases));
	builder->toPath = std::move(toPath);
	return builder;
}

} // namespace tsc::project
