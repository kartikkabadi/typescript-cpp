// snapshot.go — Snapshot::Clone + helpers.
//
// === slice: project ===

#include "internal/project/snapshot.h"

#include <algorithm>

#include "internal/ast/ast.h"
#include "internal/core/utilities.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/gostd/gostd.h"
#include "internal/project/autoimport.h"
#include "internal/project/logging/logging.h"
#include "internal/project/compilerhost.h"
#include "internal/project/projectcollectionbuilder.h"
#include "internal/project/snapshothost.h"

namespace tsc::project {

namespace {

// formatSlice — fmt "%v" on a slice: "[a b c]".
template <typename T>
std::string formatSlice(const std::vector<T>& v) {
	std::string out = "[";
	for (size_t i = 0; i < v.size(); i++) {
		if (i) {
			out += " ";
		}
		if constexpr (std::is_same_v<T, tspath::Path>) {
			out += static_cast<const std::string&>(v[i]);
		} else if constexpr (std::is_same_v<T, ID>) {
			out += static_cast<const std::string&>(v[i]);
		} else {
			out += v[i];
		}
	}
	out += "]";
	return out;
}

// formatSet — fmt "%v" on a Set.
template <typename T>
std::string formatSet(const collections::Set<T>& s) {
	return formatSlice(
	    std::vector<T>(s.Keys().begin(), s.Keys().end()));
}

} // namespace


// snapshotOverlays — snapshot.go:98.
std::unordered_map<tspath::Path, Overlay*>
snapshotOverlays(SnapshotFS* fs) {
	return fs->fs->Overlays();
}

// resourceRequestForDocument — snapshot.go:110.
ResourceRequest Snapshot::resourceRequestForDocument(
    const lsp::lsproto::DocumentUri& uri) {
	auto path = lsp::lsproto::documentUriPath(uri,
	                                     UseCaseSensitiveFileNames());
	ResourceRequest request;
	request.Documents = {uri};
	for (auto* project : ProjectCollection->SyntheticProjects()) {
		if (project->containsFile(path) ||
		    (project->host != nullptr &&
		     project->host->sourceFS
		         ->SeenFileOrMissingParentDirectory(path))) {
			request.Projects.push_back(project->ID());
		}
	}
	return request;
}

// processFileChanges — snapshot.go:124.
FileChangeSummary Snapshot::processFileChanges(
    snapshotFSBuilder* fs, FileChangeSummary fileChanges,
    logging::LogTree* logger,
    ContentMapperContributions* contentMapperContributions,
    const std::unordered_map<tspath::Path, Overlay*>&
        previousOverlays,
    const std::unordered_map<tspath::Path, Overlay*>& overlays) {
	if (auto* expander =
	        dynamic_cast<FileChangeExpander*>(fs->fs);
	    expander != nullptr) {
		fileChanges = expander->ExpandFileChanges(fileChanges);
	}
	auto previousOpenFiles = overlayFileHandles(previousOverlays);
	auto openFiles = overlayFileHandles(overlays);
	if (fileChanges.HasExcessiveWatchEvents()) {
		auto invalidateStart = std::chrono::steady_clock::now();
		if (fileChanges.InvalidateAll) {
			fs->invalidateCache();
			logging::logf(logger,
			              "InvalidateAll: invalidated file cache "
			              "in %v",
			              gostd::durationString(
			                  std::chrono::steady_clock::now() -
			                  invalidateStart));
		} else if (!fs->watchChangesOverlapCache(
		               fileChanges, previousOpenFiles,
		               openFiles)) {
			// All watch changes/deletes are files we haven't seen;
			// should be irrelevant to us (probably an external
			// tool's build or something)
			fileChanges.Changed =
			    collections::Set<lsp::lsproto::DocumentUri>();
			fileChanges.Deleted =
			    collections::Set<lsp::lsproto::DocumentUri>();
		} else if (fileChanges
		               .IncludesWatchChangeOutsideNodeModules) {
			fs->invalidateCache();
			logging::logf(
			    logger,
			    "Excessive watch changes detected, "
			    "invalidated file cache in %v",
			    gostd::durationString(
			        std::chrono::steady_clock::now() -
			        invalidateStart));
		} else {
			fs->invalidateNodeModulesCache();
			logging::logf(
			    logger,
			    "npm install detected, invalidated "
			    "node_modules cache in %v",
			    gostd::durationString(
			        std::chrono::steady_clock::now() -
			        invalidateStart));
		}
	} else {
		std::vector<std::string> contentMapperExtensions;
		if (contentMapperContributions == nullptr) {
			auto [extensions, _files] =
			    contentMapperWatchState();
			contentMapperExtensions = std::move(extensions);
		} else {
			if (auto* configuredContentMappers =
			        ConfigFileRegistry->contentMappers();
			    configuredContentMappers != nullptr) {
				contentMapperExtensions =
				    configuredContentMappers->extensions;
			}
			contentMapperExtensions.insert(
			    contentMapperExtensions.end(),
			    contentMapperContributions->Extensions.begin(),
			    contentMapperContributions->Extensions.end());
		}
		auto [_exts, contentMapperWatchedFiles] =
		    contentMapperWatchState();
		fileChanges = fs->expandAndFilterWatchEvents(
		    fileChanges, contentMapperExtensions,
		    contentMapperWatchedFiles, previousOpenFiles,
		    openFiles);
		fileChanges = this->fs->expandRealpathAliases(fileChanges);
		fileChanges = fs->markDirtyFiles(fileChanges);
		fileChanges = fs->convertOpenAndCloseToChanges(
		    fileChanges, previousOpenFiles, openFiles);
	}
	for (auto& [path, fh] : openFiles) {
		auto [entry, ok] = fs->cacheFiles->Load(path);
		if (ok) {
			fs->deleteCacheEntry(entry);
		}
	}
	return fileChanges;
}

// GetDefaultProject — snapshot.go:199.
Project* Snapshot::GetDefaultProject(const lsp::lsproto::DocumentUri& uri) {
	return ProjectCollection->GetDefaultProject(
	    lsp::lsproto::documentUriPath(uri,
	                             UseCaseSensitiveFileNames()));
}

// GetLanguageServiceProjectsContainingFile — snapshot.go:207.
std::vector<ls::Project*>
Snapshot::GetLanguageServiceProjectsContainingFile(
    const lsp::lsproto::DocumentUri& uri) {
	auto fileName = lsp::lsproto::documentUriFileName(uri);
	auto path = host->toPath(fileName);
	// TODO!! sheetal may be change this to handle symlinks!!
	return ProjectCollection->GetLanguageServiceProjectsContainingFile(
	    path);
}

// toPath — snapshot.go:254.
tspath::Path Snapshot::toPath(const std::string& fileName) const {
	return host->toPath(fileName);
}

// FS — snapshot.go:310.
vfs::FS* Snapshot::FS() {
	return newSourceFS(false, fs, host->toPath);
}

// GetCurrentDirectory — snapshot.go:314.
std::string Snapshot::GetCurrentDirectory() const {
	return host->GetCurrentDirectory();
}

// Clone — snapshot.go:435.
Snapshot* Snapshot::Clone(
    const gostd::Context& ctx, const SnapshotChange& change,
    std::unordered_map<tspath::Path, Overlay*> overlays,
    logging::Logger* sessionLogger, Client* client) {
	if (apiError != nullptr) {
		TSC_UNREACHABLE("cannot clone snapshot with API error");
	}
	auto* store = host;
	logging::LogTree* logger = nullptr;

	// Print in-progress logs immediately if cloning fails. Go:
	// `defer func(){ if r := recover(); r != nil {
	// sessionLogger.Log(logger.String()); panic(r) } }`.
	struct panicLogGuard {
		SnapshotHost* store;
		logging::Logger* sessionLogger;
		logging::LogTree** logger;
		bool enabled = true;
		~panicLogGuard() noexcept(false) {}
	};
	// RAII with an explicit disarm flag — fired by a helper lambda so
	// the catch site is controlled.
	{
		// handled below via try/catch
	}
	try {
		if (store->options->LoggingEnabled &&
		    sessionLogger != nullptr) {
			logger = logging::newLogTree(
			    "Cloning snapshot " + std::to_string(id));
			auto getDetails = [&]() -> std::string {
				std::string details;
				if (!change.Documents.empty()) {
					details += " Documents: " +
					    formatSlice(change.Documents);
				}
				if (!change.ConfiguredProjectDocuments
				        .empty()) {
					details +=
					    " ConfiguredProjectDocuments: " +
					    formatSlice(change.ConfiguredProjectDocuments);
				}
				if (!change.Projects.empty()) {
					details += " Projects: " +
					    formatSlice(change.Projects);
				}
				if (change.ProjectTree != nullptr) {
					details += " ProjectTree: " +
					    formatSlice(change.ProjectTree->Projects());
				}
				return details;
			};
			switch (change.reason) {
			case UpdateReason::DidOpenFile:
				logger->Logf(
				    "Reason: DidOpenFile - %s",
				    {gostd::fmtArg(
				        change.fileChanges.Opened)});
				break;
			case UpdateReason::DidCloseFile:
				logger->Logf(
				    "Reason: DidCloseFile - %v",
				    {gostd::fmtArg(formatSet(change.fileChanges.Closed))});
				break;
			case UpdateReason::
			    DidChangeCompilerOptionsForInferredProjects:
				logger->Log(
				    "Reason: "
				    "DidChangeCompilerOptionsForInferredProjects");
				break;
			case UpdateReason::
			    RequestedLanguageServicePendingChanges:
				logger->Logf(
				    "Reason: RequestedLanguageService "
				    "(pending file changes) - %v",
				    {gostd::fmtArg(getDetails())});
				break;
			case UpdateReason::
			    RequestedLanguageServiceProjectNotLoaded:
				logger->Logf(
				    "Reason: RequestedLanguageService "
				    "(project not loaded) - %v",
				    {gostd::fmtArg(getDetails())});
				break;
			case UpdateReason::
			    RequestedLanguageServiceForFileNotOpen:
				logger->Logf(
				    "Reason: RequestedLanguageService "
				    "(file not open) - %v",
				    {gostd::fmtArg(getDetails())});
				break;
			case UpdateReason::
			    RequestedLanguageServiceProjectDirty:
				logger->Logf(
				    "Reason: RequestedLanguageService "
				    "(project dirty) - %v",
				    {gostd::fmtArg(getDetails())});
				break;
			case UpdateReason::RequestedLoadProjectTree:
				logger->Logf(
				    "Reason: RequestedLoadProjectTree "
				    "- %v",
				    {gostd::fmtArg(getDetails())});
				break;
			case UpdateReason::IdleCleanDiskCache:
				logger->Log(
				    "Reason: IdleCleanDiskCache");
				break;
			case UpdateReason::DidChangeConfigFile:
				logger->Logf(
				    "Reason: DidChangeConfigFile - %v",
				    {gostd::fmtArg(getDetails())});
				break;
			case UpdateReason::
			    DidChangeContentMapperContributions:
				logger->Logf(
				    "Reason: "
				    "DidChangeContentMapperContributions - %v",
				    {gostd::fmtArg(getDetails())});
				break;
			default:
				break;
			}
		}

		auto start = std::chrono::steady_clock::now();
		auto inferredContentMappers =
		    inferredProjectContentMappers;
		auto inferredContentMapperExtensions =
		    inferredProjectContentMapperExtensions;
		if (change.contentMapperContributions != nullptr) {
			inferredContentMappers =
			    change.contentMapperContributions->Mappers;
			inferredContentMapperExtensions =
			    change.contentMapperContributions->Extensions;
		}
		auto* baseFS = store->fs;
		if (change.fs != nullptr) {
			baseFS = change.fs;
		}
		// Total replacements and returning to the session host
		// must not retain files from the previous filesystem.
		// Layers invalidate only their per-path changes,
		// including the first layer over a host-backed
		// snapshot.
		auto localChange = change;
		if (localChange.replaceFileSystem ||
		    (fileSystemOverride &&
		     !localChange.fileSystemOverride)) {
			localChange.fileChanges.InvalidateAll = true;
		}
		auto* layeredFS = layerOverlayFileSystem(
		    baseFS, overlays, store->options->PositionEncoding,
		    store->toPath);
		overlays = layeredFS->Overlays();
		auto* fsb = newSnapshotFSBuilderFromSource(
		    layeredFS, fs->cacheFiles, fs->cacheDirectories,
		    fs->nodeModulesRealpathAliases, store->toPath);
		localChange.fileChanges = processFileChanges(
		    fsb, localChange.fileChanges, logger,
		    localChange.contentMapperContributions,
		    this->overlays(), overlays);

		auto* compilerOptionsForInferredProjects =
		    this->compilerOptionsForInferredProjects;
		if (localChange.compilerOptionsForInferredProjects !=
		    nullptr) {
			compilerOptionsForInferredProjects =
			    localChange
			        .compilerOptionsForInferredProjects;
		}

		// Compute effective customConfigFileName from user
		// preferences
		std::string customConfigFileName =
		    ConfigFileRegistry->customConfigFileName;
		if (localChange.newConfig != nullptr) {
			customConfigFileName =
			    localChange.newConfig->CustomConfigFileName;
		}

		auto newSnapshotID = store->nextSnapshotID();
		auto* projectCollectionBuilder =
		    newProjectCollectionBuilder(
		        ctx, newSnapshotID, fsb, overlays,
		        ProjectCollection, ConfigFileRegistry,
		        ProjectCollection->apiState,
		        compilerOptionsForInferredProjects,
		        inferredContentMappers,
		        inferredContentMapperExtensions,
		        store->options, customConfigFileName,
		        store->parseCache,
		        store->contentMappedParseCache,
		        store->extendedConfigCache,
		        store->contentMapperHost.get(), client);

		if (!localChange.ataChanges.empty()) {
			projectCollectionBuilder->DidUpdateATAState(
			    localChange.ataChanges,
			    logging::fork(logger, "DidUpdateATAState"));
		}

		projectCollectionBuilder
		    ->DidChangeCustomConfigFileName(
		        logging::fork(logger,
		                      "DidChangeCustomConfigFileName"));
		if (localChange
		            .compilerOptionsForInferredProjects !=
		            nullptr &&
		    projectCollectionBuilder->inferredProject
		            ->Value() != nullptr) {
			auto* inferredValue =
			    projectCollectionBuilder->inferredProject
			        ->Value();
			projectCollectionBuilder->updateInferredProject(
			    inferredValue->CommandLine->FileNames(),
			    localChange
			        .compilerOptionsForInferredProjects,
			    inferredValue->CommandLine
			        ->ProjectReferences(),
			    inferredValue->CommandLine->Errors,
			    inferredValue->CommandLine
			        ->ContentMappers(),
			    logging::fork(
			        logger,
			        "DidChangeCompilerOptionsForInferredProjects"));
		}
		if (localChange.contentMapperContributions != nullptr) {
			projectCollectionBuilder
			    ->DidChangeContentMapperContributions(
			        logging::fork(
			            logger,
			            "DidChangeContentMapperContributions"));
		}
		if (localChange.newConfig != nullptr) {
			projectCollectionBuilder
			    ->DidChangeUserPreferences(
			        userPreferences, *localChange.newConfig,
			        logging::fork(logger,
			                      "DidChangeUserPreferences"));
		}

		if (!localChange.fileChanges.IsEmpty()) {
			projectCollectionBuilder->DidChangeFiles(
			    localChange.fileChanges,
			    logging::fork(logger, "DidChangeFiles"));
		}

		gostd::Error apiError;
		if (localChange.apiRequest != nullptr) {
			apiError = projectCollectionBuilder
			               ->HandleAPIRequest(
			                   localChange.apiRequest,
			                   logging::fork(logger,
			                                 "HandleAPIRequest"));
		}

		for (auto& uri : localChange.Documents) {
			projectCollectionBuilder->DidRequestFile(
			    uri, false /*configuredProjectsOnly*/,
			    logging::fork(logger, "DidRequestFile"));
		}

		for (auto& uri :
		     localChange.ConfiguredProjectDocuments) {
			projectCollectionBuilder->DidRequestFile(
			    uri, true /*configuredProjectsOnly*/,
			    logging::fork(logger,
			                  "DidRequestFile (optional)"));
		}

		for (auto& projectId : localChange.Projects) {
			projectCollectionBuilder->DidRequestProject(
			    projectId,
			    logging::fork(logger, "DidRequestProject"));
		}

		if (localChange.ProjectTree != nullptr) {
			projectCollectionBuilder
			    ->DidRequestProjectTrees(
			        localChange.ProjectTree,
			        logging::fork(logger,
			                      "DidRequestProjectTrees"));
		}

		auto finalized = projectCollectionBuilder->Finalize(logger);
		auto* projectCollection = finalized.first;
		auto* configFileRegistry = finalized.second;

		// The clone host is created here (earlier than in Go) so its
		// interned ProjectID adapters can key
		// projectsWithNewProgramStructure by pointer identity.
		auto* autoImportHost = newAutoImportRegistryCloneHost(
		    projectCollection, store->parseCache, fsb,
		    store->options->CurrentDirectory, store->toPath);
		std::unordered_map<ls::autoimport::ProjectID*, bool>
		    projectsWithNewProgramStructure;
		for (auto* project : projectCollection->Projects()) {
			if (project->ProgramLastUpdate ==
			        newSnapshotID &&
			    project->ProgramUpdateKind !=
			        ProgramUpdateKindCloned) {
				projectsWithNewProgramStructure
				    [autoImportHost->internID(
				        project->ID())] =
				        project->ProgramUpdateKind ==
				        ProgramUpdateKindNewFiles;
			}
		}

		// Clean cached files not touched by any open project on
		// file open, close, delete, or when explicitly
		// requested (e.g. by an idle timer).
		bool shouldCleanFileCache =
		    localChange.cleanFileCache ||
		    !localChange.fileChanges.Opened.empty() ||
		    !localChange.fileChanges.Reopened.empty() ||
		    localChange.fileChanges.Closed.Len() > 0 ||
		    localChange.fileChanges.Deleted.Len() > 0;
		if (shouldCleanFileCache) {
			// The set of seen files can change only if a
			// program was constructed (not cloned) during
			// this snapshot. When cleanFileCache is
			// explicitly set, always attempt cleaning.
			if (!projectsWithNewProgramStructure.empty() ||
			    localChange.cleanFileCache) {
				auto cleanFilesStart =
				    std::chrono::steady_clock::now();
				int removedFiles = 0;
				fsb->cacheFiles->Range(
				    [&](const std::shared_ptr<
				        dirty::SyncMapEntry<
				            tspath::Path, cachedFile*>>&
				            entry) {
					    for (auto* project :
					         projectCollection->Projects()) {
						    if (project->host !=
						            nullptr &&
						        project->host->sourceFS
						            ->SeenFile(
						                entry->Key())) {
							    return true;
						    }
					    }
					    entry->Delete();
					    removedFiles++;
					    return true;
				    });
				logging::logf(
				    logger,
				    "Removed %d cached file(s) in %v",
				    removedFiles,
				    gostd::durationString(
				        std::chrono::steady_clock::now() -
				        cleanFilesStart));
			}
		}

		auto config = userPreferences;
		if (localChange.newConfig != nullptr) {
			config = *localChange.newConfig;
		}

		std::unordered_map<tspath::Path, std::string>
		    openFiles;
		openFiles.reserve(overlays.size());
		for (auto& [path, overlay] : overlays) {
			openFiles[path] = overlay->FileName();
		}
		auto prepareAutoImports = tspath::Path("");
		if (!localChange.AutoImports.empty()) {
			prepareAutoImports =
			    lsp::lsproto::documentUriPath(
			        localChange.AutoImports,
			        UseCaseSensitiveFileNames());
		}
		auto* oldAutoImports = AutoImports;
		std::unique_ptr<ls::autoimport::Registry>
		    oldAutoImportsOwned;
		if (oldAutoImports == nullptr) {
			oldAutoImportsOwned = ls::autoimport::NewRegistry(
			    store->toPath, userPreferences);
			oldAutoImports = oldAutoImportsOwned.get();
		}
		WatchedFiles<std::unordered_map<tspath::Path,
		                              std::string>>*
		    autoImportsWatch = nullptr;
		ls::autoimport::RegistryChange registryChange;
		registryChange.RequestedFile = prepareAutoImports;
		registryChange.OpenFiles = openFiles;
		registryChange.Changed =
		    localChange.fileChanges.Changed;
		registryChange.Created =
		    localChange.fileChanges.Created;
		registryChange.Deleted =
		    localChange.fileChanges.Deleted;
		registryChange.RebuiltPrograms =
		    projectsWithNewProgramStructure;
		registryChange.UserPreferences = localChange.newConfig;
		auto [autoImports, err] = oldAutoImports->Clone(
		    ctx, registryChange, autoImportHost,
		    logging::fork(logger, "UpdateAutoImports"));
		if (err == nullptr) {
			autoImportsWatch = watchedFilesClone(
			    this->autoImportsWatch,
			    autoImports->NodeModulesDirectories());
		}

		auto [snapshotFS, _fsOk] = fsb->Finalize();
		auto* newSnapshot = store->newSnapshot(
		    newSnapshotID, snapshotFS, nullptr,
		    compilerOptionsForInferredProjects, config,
		    autoImports.release(), autoImportsWatch);
		newSnapshot->parentId = id;
		newSnapshot->ProjectCollection = projectCollection;
		newSnapshot->ConfigFileRegistry = configFileRegistry;
		newSnapshot->inferredProjectContentMappers =
		    inferredContentMappers;
		newSnapshot->inferredProjectContentMapperExtensions =
		    inferredContentMapperExtensions;
		newSnapshot->builderLogs = logger;
		newSnapshot->apiError = apiError;
		newSnapshot->fileSystemOverride =
		    localChange.fileSystemOverride;
		newSnapshot->createdPrograms =
		    projectCollectionBuilder->createdPrograms;

		for (auto* project :
		     newSnapshot->ProjectCollection->Projects()) {
			if (project->Program != nullptr) {
				store->programCounter->Ref(project->Program);
				if (project->ProgramLastUpdate ==
				    newSnapshotID) {
					// If the program was updated during
					// this clone, the project and its host
					// are new and still retain references
					// to the builder. Freezing clears the
					// builder reference so it's GC'd and
					// to ensure the project can't access
					// any data not already in the
					// snapshot during use. This is pretty
					// kludgy, but it's an artifact of
					// Program design: Program has a
					// single host, which is expected to
					// implement a full vfs.FS, among
					// other things. That host is *mostly*
					// only used during program
					// *construction*, but a few methods
					// may get exercised during program
					// *use*. So, our compiler host is
					// allowed to access caches and
					// perform mutating effects (like
					// acquire referenced project config
					// files) during snapshot building,
					// and then we call `freeze` to ensure
					// those mutations don't happen
					// afterwards. In the future, we might
					// improve things by separating what
					// it takes to build a program from
					// what it takes to use a program, and
					// only pass the former into
					// NewProgram instead of retaining it
					// indefinitely.
					project->host->freeze(
					    snapshotFS,
					    newSnapshot->ConfigFileRegistry);
				}
			}
		}
		for (auto& [configPath, config] :
		     newSnapshot->ConfigFileRegistry->configs) {
			if (config->commandLine != nullptr &&
			    config->commandLine->ConfigFile !=
			        nullptr) {
				for (auto& file : config->commandLine
				         ->ConfigFile->ExtendedSourceFiles) {
					store->extendedConfigCache->AddOwner(
					    store->toPath(file), newSnapshot->id);
				}
			}
		}

		autoImportHost->Dispose();

		logging::logf(logger,
		              "Finished cloning snapshot %d into "
		              "snapshot %d in %v",
		              static_cast<int64_t>(id),
		              static_cast<int64_t>(newSnapshot->id),
		              gostd::durationString(
		                  std::chrono::steady_clock::now() -
		                  start));
		return newSnapshot;
	} catch (...) {
		if (store->options->LoggingEnabled &&
		    sessionLogger != nullptr && logger != nullptr) {
			sessionLogger->Log(logger->String());
		}
		throw;
	}
}

// dispose — snapshot.go:787.
void Snapshot::dispose() {
	auto* store = host;
	for (auto* project : ProjectCollection->Projects()) {
		if (project->Program != nullptr &&
		    store->programCounter->Deref(project->Program)) {
			if (auto* contentMapperProject =
			        project->Program->ContentMapperProject();
			    contentMapperProject != nullptr) {
				contentMapperProject->Close();
			}
			// This program is no longer referenced by any
			// snapshot. Mark its checker pool as discarded so
			// its idle-cleanup timer stops keeping the pool
			// alive, allowing the pool and any idle checkers
			// it still references to be reclaimed when the
			// pool is garbage-collected.
			if (project->checkerPool != nullptr) {
				project->checkerPool->Discard();
			}
			for (auto* file :
			     project->Program->SourceFiles()) {
				if (!file->IsContentMapperFailureStub() &&
				    !file->IsContentMapperSupplemental()) {
					if (!file->ContentMapper().empty()) {
						store->contentMappedParseCache
						    ->Deref(
						        contentMappedParseCacheKeyForFile(
						            file));
					} else {
						store->parseCache->Deref(
						    parseCacheKeyForFile(file));
					}
				}
			}
			for (auto* file : project->Program
			         ->DuplicateSourceFiles()) {
				if (!file->IsContentMapperFailureStub) {
					if (!file->ContentMapper.empty()) {
						store->contentMappedParseCache
						    ->Deref(
						        contentMappedParseCacheKeyForDuplicate(
						            file));
					} else {
						store->parseCache->Deref(
						    parseCacheKeyForDuplicate(
						        file));
					}
				}
			}
		}
	}
	for (auto& [configPath, config] :
	     ConfigFileRegistry->configs) {
		if (config->commandLine != nullptr) {
			for (auto& file : config->commandLine
			         ->ExtendedSourceFiles()) {
				store->extendedConfigCache->Release(
				    store->toPath(file), id);
			}
		}
	}
}

// newSnapshot — snapshot.go:81.
Snapshot* SnapshotHost::newSnapshot(
    uint64_t id_, SnapshotFS* fs_, ConfigFileRegistry* registry,
    CompilerOptions* compilerOptionsForInferredProjects_,
    const ls::lsutil::UserPreferences& userPreferences_,
    ls::autoimport::Registry* autoImports_,
    WatchedFiles<std::unordered_map<tspath::Path, std::string>>*
        autoImportsWatch_) {
	auto overlays = snapshotOverlays(fs_);
	auto* s = new Snapshot();
	s->host = this;
	s->id = id_;
	s->fs = fs_;
	s->ConfigFileRegistry = registry;
	s->ProjectCollection = new ProjectCollection();
	s->ProjectCollection->toPath = toPath;
	s->ProjectCollection->openFiles = openFilePaths(overlays);
	s->compilerOptionsForInferredProjects =
	    compilerOptionsForInferredProjects_;
	s->userPreferences = userPreferences_;
	s->AutoImports = autoImports_;
	s->autoImportsWatch = autoImportsWatch_;
	s->refCount.store(1);
	s->converters = lsconv::NewConverters(
	    options->PositionEncoding,
	    [s](const std::string& fileName) {
		    return s->LSPLineMap(fileName);
	    });
	return s;
}

} // namespace tsc::project
