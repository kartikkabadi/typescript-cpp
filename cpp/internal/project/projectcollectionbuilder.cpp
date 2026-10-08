// projectcollectionbuilder.go — ProjectCollectionBuilder methods.
//
// === slice: project ===

#include "internal/project/projectcollectionbuilder.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>

#include "internal/core/utilities.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/gostd/gostd.h"
#include "internal/project/checkerpool.h"
#include "internal/project/compilerhost.h"
#include "internal/project/snapshot.h"
#include "internal/project/watch.h"

namespace tsc::project {

using namespace std::chrono;

// newProjectCollectionBuilder — projectcollectionbuilder.go:60.
ProjectCollectionBuilder* newProjectCollectionBuilder(
    const gostd::Context& ctx, uint64_t newSnapshotID,
    snapshotFSBuilder* fs,
    std::unordered_map<tspath::Path, Overlay*> overlays,
    ProjectCollection* oldProjectCollection,
    ConfigFileRegistry* oldConfigFileRegistry,
    const APIState& oldAPIState,
    CompilerOptions* compilerOptionsForInferredProjects,
    const std::vector<contentmapper::Mapper*>& inferredContentMappers,
    const std::vector<std::string>& inferredContentMapperExtensions,
    SessionOptions* sessionOptions,
    const std::string& customConfigFileName, ParseCache* parseCache,
    ContentMappedParseCache* contentMappedParseCache,
    ExtendedConfigCache* extendedConfigCache,
    contentmapper::Host* contentMapperHost, Client* client) {
	auto openFiles = openFilePaths(overlays);
	auto* b = new ProjectCollectionBuilder{};
	b->ctx = ctx;
	b->fs = fs;
	b->overlays = std::move(overlays);
	b->toPath = fs->toPath;
	b->compilerOptionsForInferredProjects =
	    compilerOptionsForInferredProjects;
	b->inferredContentMappers = inferredContentMappers;
	b->inferredContentMapperExtensions =
	    inferredContentMapperExtensions;
	b->sessionOptions = sessionOptions;
	b->parseCache = parseCache;
	b->contentMappedParseCache = contentMappedParseCache;
	b->extendedConfigCache = extendedConfigCache;
	b->contentMapperHost = contentMapperHost;
	b->base = oldProjectCollection;
	bool hasRelativePatternCapability = false;
	if (auto caps = lsp::lsproto::getClientCapabilities(ctx);
	    caps != nullptr) {
		hasRelativePatternCapability =
		    caps->Workspace.DidChangeWatchedFiles
		        .RelativePatternSupport;
	}
	b->configFileRegistryBuilder_ = newConfigFileRegistryBuilder(
	    hasRelativePatternCapability, fs,
	    [b](const tspath::Path& path) {
		    return b->overlays.count(path) != 0;
	    },
	    oldConfigFileRegistry, extendedConfigCache, newSnapshotID,
	    sessionOptions, customConfigFileName, nullptr);
	b->newSnapshotID = newSnapshotID;
	b->openFilesChanged =
	    !openFiles.Equals(oldProjectCollection->openFiles);
	b->configuredProjects =
	    dirty::newSyncMap<ConfiguredProjectID, Project*>(
	        oldProjectCollection->configuredProjects);
	b->syntheticProjects =
	    dirty::newSyncMap<SyntheticProjectID, Project*>(
	        oldProjectCollection->syntheticProjects);
	b->inferredProject =
	    dirty::newBox(oldProjectCollection->inferredProject);
	b->apiState = oldAPIState.clone();
	b->client = client;
	return b;
}

// Finalize — projectcollectionbuilder.go:109.
std::pair<ProjectCollection*, ConfigFileRegistry*>
ProjectCollectionBuilder::Finalize(logging::LogTree* logger) {
	bool changed = false;
	auto* newProjectCollection = base;
	auto ensureCloned = [&] {
		if (!changed) {
			newProjectCollection = newProjectCollection->clone();
			changed = true;
		}
	};

	auto configuredRes = configuredProjects->Finalize();
	if (configuredRes.second) {
		ensureCloned();
		newProjectCollection->configuredProjects = configuredRes.first;
	}
	auto syntheticRes = syntheticProjects->Finalize();
	if (syntheticRes.second) {
		ensureCloned();
		newProjectCollection->syntheticProjects = syntheticRes.first;
	}

	if (openFilesChanged) {
		ensureCloned();
		newProjectCollection->openFiles = openFilePaths(overlays);
	}

	if (fileDefaultProjects != base->fileDefaultProjects) {
		ensureCloned();
		newProjectCollection->fileDefaultProjects =
		    fileDefaultProjects;
	}

	auto inferredRes = inferredProject->Finalize();
	if (inferredRes.second) {
		ensureCloned();
		newProjectCollection->inferredProject = inferredRes.first;
	}

	auto* configFileRegistry = configFileRegistryBuilder_->Finalize();
	if (configFileRegistry != base->configFileRegistry) {
		ensureCloned();
		newProjectCollection->configFileRegistry =
		    configFileRegistry;
	}

	if (!apiState.equals(base->apiState)) {
		ensureCloned();
		newProjectCollection->apiState = apiState;
	}

	return {newProjectCollection, configFileRegistry};
}

// forEachProject — projectcollectionbuilder.go:167.
void ProjectCollectionBuilder::forEachProject(
    const std::function<bool(dirty::IValue<Project*>*)>& fn) {
	bool keepGoing = true;
	configuredProjects->Range(
	    [&](const std::shared_ptr<dirty::SyncMapEntry<
	            ConfiguredProjectID, Project*>>& entry) {
		    keepGoing = fn(entry.get());
		    return keepGoing;
	    });
	if (keepGoing) {
		syntheticProjects->Range(
		    [&](const std::shared_ptr<dirty::SyncMapEntry<
		            SyntheticProjectID, Project*>>& entry) {
			    keepGoing = fn(entry.get());
			    return keepGoing;
		    });
	}
	if (!keepGoing) {
		return;
	}
	if (inferredProject->Value() != nullptr) {
		fn(inferredProject);
	}
}

// HandleAPIRequest — projectcollectionbuilder.go:183.
gostd::Error ProjectCollectionBuilder::HandleAPIRequest(
    APISnapshotRequest* apiRequest, logging::LogTree* logger) {
	std::unordered_set<tspath::Path> projectsToClose;
	if (apiRequest->CloseProjects != nullptr) {
		for (auto& projectPath : apiRequest->CloseProjects->Keys()) {
			auto it = apiState.openProjects.find(projectPath);
			if (it != apiState.openProjects.end() && it->second > 1) {
				it->second = it->second - 1;
			} else if (it != apiState.openProjects.end() &&
			           it->second == 1) {
				apiState.openProjects.erase(it);
				projectsToClose.insert(projectPath);
			}
		}
	}

	if (apiRequest->OpenProjects != nullptr) {
		for (auto& configFileName :
		     apiRequest->OpenProjects->Keys()) {
			auto configPath = toPath(configFileName);
			auto entry = findOrCreateProject(configFileName, configPath,
			                                 projectLoadKindCreate,
			                                 logger);
			if (entry != nullptr) {
				apiState.openProjects[configPath]++;
				// A project re-opened in the same request shouldn't be
				// closed.
				projectsToClose.erase(configPath);
				updateProgram(entry.get(), logger);
			} else {
				return gostd::newError(
				    "project not found for open: " +
				    configFileName);
			}
		}
	}

	if (apiRequest->CloseFiles != nullptr) {
		for (auto& path : apiRequest->CloseFiles->Keys()) {
			auto it = apiState.openFiles.find(path);
			if (it != apiState.openFiles.end()) {
				if (it->second.refCount > 1) {
					it->second.refCount--;
				} else {
					apiState.openFiles.erase(it);
				}
			}
		}
	}

	if (apiRequest->OpenFiles != nullptr) {
		for (auto& [path, fileName] : *apiRequest->OpenFiles) {
			auto& entry = apiState.openFiles[path];
			entry.fileName = fileName;
			entry.refCount++;
		}
	}

	for (auto& [opath, overlay] : overlays) {
		if (auto entry = findDefaultConfiguredProject(
		        overlay->FileName(), toPath(overlay->FileName()));
		    entry != nullptr) {
			projectsToClose.erase(
			    entry->Value()->configFilePath);
		}
	}

	for (auto& projectPath : projectsToClose) {
		auto res = configuredProjects->Load(
		    ConfiguredProjectID{projectPath});
		if (res.second) {
			deleteProject(res.first.get(), logger);
		}
	}

	// Place newly API-opened files like LSP's textDocument/didOpen,
	// ensuring only their target projects. Existing API-opened files are
	// retained by cleanup below without implicitly updating their
	// programs.
	if (apiRequest->OpenFiles != nullptr) {
		collections::Set<tspath::Path> retain;
		bool ensureInferredProject = false;
		for (auto& [path, fileName] : *apiRequest->OpenFiles) {
			if (isOpenFile(path)) {
				if (findDefaultConfiguredProject(fileName, path) ==
				        nullptr &&
				    !isSupportedInInferredProject(fileName)) {
					return gostd::newError(
					    "no project found for opened file: " +
					    fileName);
				}
				continue;
			}
			auto result =
			    ensureConfiguredProjectAndAncestorsForFile(
			        fileName, path, logger);
			retain.Union(result.retain);
			if (result.project == nullptr) {
				if (!isSupportedInInferredProject(fileName)) {
					return gostd::newError(
					    "no project found for opened file: " +
					    fileName);
				}
				ensureInferredProject = true;
			}
		}
		cleanupConfiguredProjects(&retain, logger);
		if (ensureInferredProject &&
		    inferredProject->Value() != nullptr) {
			updateProgram(inferredProject, logger);
		}
	} else if (apiRequest->CloseFiles != nullptr) {
		cleanupConfiguredProjects(nullptr, logger);
	}
	collections::Set<SyntheticProjectID> seenReconfiguredPrograms;
	for (auto* request : apiRequest->ReconfigurePrograms) {
		if (seenReconfiguredPrograms.Has(request->ProgramID)) {
			return gostd::newError(
			    "synthetic program reconfigured more than once: " +
			    std::string{request->ProgramID});
		}
		seenReconfiguredPrograms.Add(request->ProgramID);
		if (apiRequest->RemovePrograms == nullptr) {
			TSC_UNREACHABLE(
			    "APISnapshotRequest.RemovePrograms is required");
		}
		if (apiRequest->RemovePrograms->Has(request->ProgramID)) {
			return gostd::newError(
			    "synthetic program cannot be reconfigured and "
			    "removed: " +
			    std::string{request->ProgramID});
		}
		if (auto res =
		        syntheticProjects->Load(request->ProgramID);
		    !res.second) {
			return gostd::newError(
			    "synthetic program not found for reconfiguration: " +
			    std::string{request->ProgramID});
		}
	}
	if (apiRequest->RemovePrograms == nullptr) {
		TSC_UNREACHABLE(
		    "APISnapshotRequest.RemovePrograms is required");
	}
	for (auto& programID : apiRequest->RemovePrograms->Keys()) {
		auto res = syntheticProjects->Load(programID);
		if (!res.second) {
			return gostd::newError(
			    "synthetic program not found for removal: " +
			    std::string{programID});
		}
		deleteProject(res.first.get(), logger);
	}
	std::vector<Project*> createdProgramsVec(
	    apiRequest->CreatePrograms.size(), nullptr);
	std::vector<
	    std::shared_ptr<dirty::SyncMapEntry<SyntheticProjectID,
	                                        Project*>>>
	    createdEntries(apiRequest->CreatePrograms.size());
	for (size_t i = 0; i < apiRequest->CreatePrograms.size(); i++) {
		auto* request = apiRequest->CreatePrograms[i];
		auto entry = updateOrCreateSyntheticProject(
		    nextSyntheticProjectID(), request->RootFileNames,
		    request->CompilerOptions, request->ProjectReferences,
		    request->ConfigFileParsingDiagnostics,
		    request->ModuleResolverFactory,
		    request->ModuleResolverID, inferredContentMappers,
		    logger);
		createdEntries[i] = entry;
	}
	std::vector<
	    std::shared_ptr<dirty::SyncMapEntry<SyntheticProjectID,
	                                        Project*>>>
	    reconfiguredEntries(apiRequest->ReconfigurePrograms.size());
	for (size_t i = 0; i < apiRequest->ReconfigurePrograms.size();
	     i++) {
		auto* request = apiRequest->ReconfigurePrograms[i];
		reconfiguredEntries[i] = updateOrCreateSyntheticProject(
		    request->ProgramID, request->RootFileNames,
		    request->CompilerOptions, request->ProjectReferences,
		    request->ConfigFileParsingDiagnostics,
		    request->ModuleResolverFactory,
		    request->ModuleResolverID, inferredContentMappers,
		    logger);
	}
	{
		// Go's sync.WaitGroup wg.Go: one goroutine per entry.
		std::vector<std::thread> threads;
		for (size_t i = 0; i < createdEntries.size(); i++) {
			threads.emplace_back([&, i] {
				auto& entry = createdEntries[i];
				if (entry->Value()->dirty) {
					updateProgram(entry.get(), logger);
				}
				createdProgramsVec[i] = entry->Value();
			});
		}
		for (auto& entry : reconfiguredEntries) {
			threads.emplace_back([&, entry] {
				if (entry->Value()->dirty) {
					updateProgram(entry.get(), logger);
				}
			});
		}
		for (auto& t : threads) {
			t.join();
		}
	}
	this->createdPrograms = std::move(createdProgramsVec);
	for (auto& [path, fileName] : apiRequest->EnsureFiles) {
		didRequestFile(fileName, path,
		               false /*configuredProjectsOnly*/, logger);
		if (findDefaultProject(fileName, path) == nullptr) {
			return gostd::newError(
			    "no project found for opened file: " + fileName);
		}
	}
	if (apiRequest->EnsurePrograms == nullptr) {
		TSC_UNREACHABLE(
		    "APISnapshotRequest.EnsurePrograms is required");
	}
	for (auto& projectID : apiRequest->EnsurePrograms->Keys()) {
		DidRequestProject(projectID, logger);
	}
	if (apiRequest->EnsureAllPrograms) {
		forEachProject([&](dirty::IValue<Project*>* entry) {
			updateProgram(entry, logger);
			return true;
		});
	}
	gostd::Error moduleResolutionError;
	forEachProject([&](dirty::IValue<Project*>* entry) {
		auto* project = entry->Value();
		if (project->Program != nullptr) {
			moduleResolutionError =
			    project->Program->ModuleResolutionError();
		}
		return moduleResolutionError == nullptr;
	});
	return moduleResolutionError;
}

// nextSyntheticProjectID — projectcollectionbuilder.go:343.
SyntheticProjectID ProjectCollectionBuilder::nextSyntheticProjectID() {
	for (int id = 1;; id++) {
		auto projectID = NewSyntheticProjectID(id);
		if (auto res = syntheticProjects->Load(projectID);
		    !res.second) {
			return projectID;
		}
	}
}

// DidChangeFiles — projectcollectionbuilder.go:351.
void ProjectCollectionBuilder::DidChangeFiles(
    const FileChangeSummary& summary, logging::LogTree* logger) {
	openFilesChanged = openFilesChanged ||
	                   !summary.Opened.empty() ||
	                   summary.Closed.Len() > 0;

	auto toPaths = [&](const collections::Set<lsp::lsproto::DocumentUri>&
	                       uris) {
		std::vector<tspath::Path> paths;
		paths.reserve(uris.Len());
		for (auto& uri : uris.Keys()) {
			paths.push_back(
			    toPath(lsp::lsproto::documentUriFileName(uri)));
		}
		return paths;
	};
	auto changedFiles = toPaths(summary.Changed);
	auto deletedFiles = toPaths(summary.Deleted);
	auto createdFiles = toPaths(summary.Created);
	if (contentMapperHost != nullptr) {
		std::vector<tspath::Path> allWatchChanges;
		allWatchChanges.insert(allWatchChanges.end(),
		                       changedFiles.begin(),
		                       changedFiles.end());
		allWatchChanges.insert(allWatchChanges.end(),
		                       deletedFiles.begin(),
		                       deletedFiles.end());
		allWatchChanges.insert(allWatchChanges.end(),
		                       createdFiles.begin(),
		                       createdFiles.end());
		forEachProject([&](dirty::IValue<Project*>* entry) {
			refreshContentMapperProjectForChanges(
			    entry, allWatchChanges,
			    summary.HasExcessiveNonCreateWatchEvents(),
			    logger);
			return true;
		});
	}

	auto* configChangeLogger =
	    logging::fork(logger, "Checking for changes affecting config "
	                          "files");
	auto configChangeResult =
	    configFileRegistryBuilder_->DidChangeFiles(
	        summary, configChangeLogger);
	logChangeFileResult(configChangeResult, configChangeLogger);

	programStructureChanged = markProjectsAffectedByConfigChanges(
	    configChangeResult, logger);

	forEachProject([&](dirty::IValue<Project*>* entry) {
		// Only consider change/delete; creates are handled by the config
		// file registry
		if (summary.HasExcessiveNonCreateWatchEvents()) {
			entry->Change([&](Project*& p) {
				p->dirty = true;
				p->dirtyFilePath = tspath::Path{};
				logging::logf(logger,
				              "Marking project as dirty due to "
				              "excessive watch changes: %s",
				              std::string{p->ID()});
			});
			return true;
		}

		// Handle closed and changed files
		markFilesChanged(entry, changedFiles,
		                 lsp::lsproto::FileChangeTypeChanged, logger);
		if (entry->Value()->Kind == Kind::Inferred &&
		    summary.Closed.Len() > 0) {
			auto* rootFilesMap =
			    entry->Value()->CommandLine->FileNamesByPath();
			auto newRootFiles =
			    entry->Value()->CommandLine->FileNames();
			for (auto& uri : summary.Closed.Keys()) {
				auto fileName =
				    lsp::lsproto::documentUriFileName(uri);
				auto path = toPath(fileName);
				if (rootFilesMap->count(path) != 0) {
					auto it = std::find(newRootFiles.begin(),
					                    newRootFiles.end(),
					                    fileName);
					if (it != newRootFiles.end()) {
						newRootFiles.erase(it);
					}
				}
			}
			updateInferredProjectRoots(newRootFiles, logger);
		}

		// Handle deleted files
		if (summary.Deleted.Len() > 0) {
			markFilesChanged(entry, deletedFiles,
			                 lsp::lsproto::FileChangeTypeDeleted,
			                 logger);
		}

		// Handle created files
		if (summary.Created.Len() > 0) {
			markFilesChanged(entry, createdFiles,
			                 lsp::lsproto::FileChangeTypeCreated,
			                 logger);
		}

		return true;
	});

	// Handle opened file
	if (!summary.Opened.empty() || !summary.Reopened.empty()) {
		auto fileName = lsp::lsproto::documentUriFileName(
		    FirstNonZero(summary.Opened, summary.Reopened));
		auto path = toPath(fileName);
		auto openFileResult =
		    ensureConfiguredProjectAndAncestorsForFile(fileName,
		                                               path, logger);
		cleanupConfiguredProjects(&openFileResult.retain, logger);
	}
}

// refreshContentMapperProjectForChanges —
// projectcollectionbuilder.go:429.
void ProjectCollectionBuilder::
    refreshContentMapperProjectForChanges(
        dirty::IValue<Project*>* entry,
        const std::vector<tspath::Path>& paths, bool refreshAll,
        logging::LogTree* logger) {
	auto* project = entry->Value();
	if (project->Program == nullptr ||
	    project->contentMapperWatchedFiles == nullptr) {
		return;
	}
	bool affected = refreshAll;
	if (!affected) {
		for (auto& p : paths) {
			if (project->contentMapperWatchedFiles->Has(p)) {
				affected = true;
				break;
			}
		}
	}
	if (!affected) {
		return;
	}
	if (auto contentMapperProject =
	        project->Program->ContentMapperProject();
	    contentMapperProject != nullptr) {
		contentMapperProject->Refresh();
	}
	entry->Change([&](Project*& project) {
		project->dirty = true;
		project->dirtyFilePath = tspath::Path{};
		logging::logf(logger,
		              "Marking project as dirty due to content "
		              "mapper configuration changes: %s",
		              std::string{project->ID()});
	});
}

// cleanupConfiguredProjects — projectcollectionbuilder.go:457. Sweeps
// the loaded configured projects and unloads those that are no longer
// needed. Starting from the set of all configured projects, it retains
// any project that is the default project (along with its references
// and ancestor configs) of an open overlay file or an API-opened
// file, any project explicitly opened through the API, and any project
// in retain (e.g. the ancestor solution tree built for a freshly
// opened overlay file). Every other configured project is deleted,
// the inferred project roots are recomputed, and the config file
// registry is cleaned up. This is the shared mechanism that keeps the
// set of loaded projects minimal for both LSP file opens and API file
// opens/closes.
void ProjectCollectionBuilder::cleanupConfiguredProjects(
    collections::Set<tspath::Path>* retain,
    logging::LogTree* logger) {
	collections::Set<tspath::Path> toRemoveProjects;
	configuredProjects->Range(
	    [&](const std::shared_ptr<dirty::SyncMapEntry<
	            ConfiguredProjectID, Project*>>& entry) {
		    toRemoveProjects.Add(tspath::Path{entry->Key()});
		    return true;
	    });

	auto retainConfiguredProjectAndReferences =
	    [&](Project* project) {
		    // Retain project
		    toRemoveProjects.Delete(project->ConfigFilePath());
		    if (auto* program = project->GetProgram();
		        program != nullptr) {
			    program->RangeResolvedProjectReference(
			        [&](tspath::Path referencePath,
			            tsoptions::ParsedCommandLine*,
			            tsoptions::ParsedCommandLine*, int) {
				        if (auto res = configuredProjects->Load(
				                ConfiguredProjectID{referencePath});
				            res.second) {
					        toRemoveProjects.Delete(
					            referencePath);
				        }
				        return true;
			        });
		    }
	    };

	auto retainDefaultConfiguredProject =
	    [&](const tspath::Path& openFilePath, Project* project) {
		    // Retain project and its references
		    retainConfiguredProjectAndReferences(project);

		    // Retain all the ancestor projects
		    configFileRegistryBuilder_->forEachConfigFileNameFor(
		        openFilePath, [&](const std::string& configFileName) {
			        if (auto ancestor = findOrCreateProject(
			                configFileName, toPath(configFileName),
			                projectLoadKindFind, logger);
			            ancestor != nullptr) {
				        retainConfiguredProjectAndReferences(
				            ancestor->Value());
			        }
		        });
	    };

	std::vector<std::string> inferredProjectFiles;
	for (auto& [opath, overlay] : overlays) {
		auto openFile = overlay->FileName();
		auto openFilePath = toPath(openFile);
		if (auto p = findDefaultConfiguredProject(openFile,
		                                          openFilePath);
		    p != nullptr) {
			retainDefaultConfiguredProject(openFilePath,
			                               p->Value());
		} else {
			inferredProjectFiles.push_back(openFile);
		}
	}
	// Treat API-opened files like open files: retain their configured
	// project (so an LSP-driven open doesn't close it), or keep them
	// as inferred project roots.
	for (auto& [path, file] : apiState.openFiles) {
		if (isOpenFile(path)) {
			continue;
		}
		if (auto p = findDefaultConfiguredProject(file.fileName,
		                                          path);
		    p != nullptr) {
			retainDefaultConfiguredProject(path, p->Value());
		} else {
			inferredProjectFiles.push_back(file.fileName);
		}
	}

	for (auto& projectPath : toRemoveProjects.Keys()) {
		if (retain != nullptr && retain->Has(projectPath)) {
			continue;
		}
		if (apiState.openProjects.count(projectPath) != 0) {
			continue;
		}
		if (auto res = configuredProjects->Load(
		        ConfiguredProjectID{projectPath});
		    res.second) {
			deleteProject(res.first.get(), logger);
		}
	}
	updateInferredProjectRoots(inferredProjectFiles, logger);
	configFileRegistryBuilder_->Cleanup();
}

// cleanupAllConfiguredProjects — projectcollectionbuilder.go:569.
// Removes all configured projects unconditionally.
void ProjectCollectionBuilder::cleanupAllConfiguredProjects(
    logging::LogTree* logger) {
	configuredProjects->Range(
	    [&](const std::shared_ptr<dirty::SyncMapEntry<
	            ConfiguredProjectID, Project*>>& entry) {
		    if (auto res = configuredProjects->Load(entry->Key());
		        res.second) {
			    deleteProject(res.first.get(), logger);
		    }
		    return true;
	    });
	configFileRegistryBuilder_->Cleanup();
}

// logChangeFileResult — projectcollectionbuilder.go:579.
void logChangeFileResult(const changeFileResult& result,
                         logging::LogTree* logger) {
	if (result.affectedProjects != nullptr &&
	    !result.affectedProjects->empty()) {
		std::vector<ID> keys;
		for (auto& [k, _] : *result.affectedProjects) {
			keys.push_back(k);
		}
		std::vector<gostd::fmtArg> args;
		args.emplace_back("[" + [&] {
			std::string s;
			for (size_t i = 0; i < keys.size(); i++) {
				if (i)
					s += " ";
				s += std::string{keys[i]};
			}
			return s;
		}() + "]");
		logger->Logf("Config file change affected projects: %v",
	             args);
	}
	if (result.affectedFiles != nullptr &&
	    !result.affectedFiles->empty()) {
		logging::logf(logger,
		              "Config file change affected config file "
		              "lookups for %d files",
		              int(result.affectedFiles->size()));
	}
}

// collectInferredProjectRoots — projectcollectionbuilder.go:588.
std::vector<std::string>
ProjectCollectionBuilder::collectInferredProjectRoots() {
	std::vector<std::string> inferredProjectFiles;
	for (auto& [path, overlay] : overlays) {
		if (findDefaultConfiguredProject(overlay->FileName(),
		                                 path) == nullptr) {
			inferredProjectFiles.push_back(overlay->FileName());
		}
	}
	return appendAPIOpenedInferredRoots(
	    std::move(inferredProjectFiles));
}

// appendAPIOpenedInferredRoots — projectcollectionbuilder.go:599.
// Appends API-opened files that aren't open in an overlay and have no
// configured project, so they're kept as inferred project roots and
// persist across snapshots.
std::vector<std::string>
ProjectCollectionBuilder::appendAPIOpenedInferredRoots(
    std::vector<std::string> inferredProjectFiles) {
	for (auto& [path, file] : apiState.openFiles) {
		if (isOpenFile(path)) {
			continue;
		}
		if (findDefaultConfiguredProject(file.fileName, path) ==
		    nullptr) {
			inferredProjectFiles.push_back(file.fileName);
		}
	}
	return inferredProjectFiles;
}

// cleanupInferredProject — projectcollectionbuilder.go:611.
void ProjectCollectionBuilder::cleanupInferredProject(
    logging::LogTree* logger) {
	updateInferredProjectRoots(collectInferredProjectRoots(),
	                           logger);
}

// DidChangeContentMapperContributions —
// projectcollectionbuilder.go:615.
void ProjectCollectionBuilder::DidChangeContentMapperContributions(
    logging::LogTree* logger) {
	cleanupInferredProject(logger);
	if (inferredProject->Value() != nullptr) {
		updateProgram(inferredProject, logger);
	}
}

// ensureInferredProjectIncludesClosedFile —
// projectcollectionbuilder.go:622.
void ProjectCollectionBuilder::
    ensureInferredProjectIncludesClosedFile(
        const std::string& fileName, logging::LogTree* logger) {
	// Collect existing inferred project roots (open files not in
	// configured projects) plus this closed file.
	auto inferredProjectFiles = collectInferredProjectRoots();
	inferredProjectFiles.push_back(fileName);
	updateInferredProjectRoots(inferredProjectFiles, logger);
	if (inferredProject->Value() != nullptr) {
		updateProgram(inferredProject, logger);
	}
}

// DidRequestFile — projectcollectionbuilder.go:633. Ensures projects
// are loaded for the given URI. If configuredProjectsOnly is true,
// only configured projects are loaded; no inferred project is created
// and it is not guaranteed that there will be any project containing
// the file in the resulting snapshot.
void ProjectCollectionBuilder::DidRequestFile(
    const lsp::lsproto::DocumentUri& uri, bool configuredProjectsOnly,
    logging::LogTree* logger) {
	auto fileName = lsp::lsproto::documentUriFileName(uri);
	auto path = toPath(fileName);
	didRequestFile(fileName, path, configuredProjectsOnly, logger);
}

// didRequestFile — projectcollectionbuilder.go:640.
void ProjectCollectionBuilder::didRequestFile(
    const std::string& fileName, const tspath::Path& path,
    bool configuredProjectsOnly, logging::LogTree* logger) {
	auto startTime = steady_clock::now();
	if (defaultProjectsInvalidated) {
		ensureConfiguredProjectAndAncestorsForFile(fileName, path,
		                                           logger);
		if (!isOpenFile(path)) {
			return;
		}
	}
	if (isOpenFile(path)) {
		bool hasChanges = programStructureChanged;

		// See if we can find a default project without updating a bunch
		// of stuff.
		if (auto result = findDefaultProject(fileName, path);
		    result != nullptr) {
			hasChanges =
			    updateProgram(result.get(), logger) || hasChanges;
			if (result->Value() != nullptr &&
			    result->Value()->containsFile(path)) {
				if (hasChanges) {
					cleanupInferredProject(logger);
					if (inferredProject->Value() != nullptr) {
						updateProgram(inferredProject, logger);
					}
				}
				return;
			}
		}

		// Make sure all projects we know about are up to date...
		configuredProjects->Range(
		    [&](const std::shared_ptr<dirty::SyncMapEntry<
		            ConfiguredProjectID, Project*>>& entry) {
			    hasChanges =
			        updateProgram(entry.get(), logger) ||
			        hasChanges;
			    return true;
		    });
		if (hasChanges) {
			// If the structure of other projects changed, we might need
			// to move files in/out of the inferred project.
			cleanupInferredProject(logger);
		}

		if (inferredProject->Value() != nullptr) {
			updateProgram(inferredProject, logger);
		}

		// At this point we should be able to find the default project
		// for the file without creating anything else. Initially, I
		// verified that and panicked if nothing was found, but that
		// panic was getting triggered by fourslash infrastructure when
		// it told us to open a package.json file. This is something
		// the VS Code client would never do, but it seems possible
		// that another client would. There's no point in panicking; we
		// don't really even have an error condition until it tries to
		// ask us language questions about a non-TS-handleable file.
	} else {
		auto result = ensureConfiguredProjectAndAncestorsForFile(
		    fileName, path, logger);
		if (result.project == nullptr && !configuredProjectsOnly) {
			// No configured project found for this closed file.
			// Add it to the inferred project so language service
			// requests can be served.
			ensureInferredProjectIncludesClosedFile(fileName,
			                                      logger);
		}
	}

	auto elapsed =
	    duration_cast<nanoseconds>(steady_clock::now() - startTime);
	logging::log(logger,
	             "Completed file request for " + fileName + " in " +
	                 gostd::durationString(elapsed));
}

// DidRequestProject — projectcollectionbuilder.go:704.
void ProjectCollectionBuilder::DidRequestProject(
    const ID& projectID, logging::LogTree* logger) {
	auto startTime = steady_clock::now();
	if (idInferred(projectID).second) {
		// Update inferred project
		if (inferredProject->Value() != nullptr) {
			updateProgram(inferredProject, logger);
		}
	} else if (auto syntheticID = idSynthetic(projectID);
	           syntheticID.second) {
		if (auto res = syntheticProjects->Load(syntheticID.first);
		    res.second) {
			updateProgram(res.first.get(), logger);
		}
	} else if (auto configuredID = idConfigured(projectID);
	           configuredID.second) {
		if (auto res = configuredProjects->Load(configuredID.first);
		    res.second) {
			updateProgram(res.first.get(), logger);
		}
	}

	auto elapsed =
	    duration_cast<nanoseconds>(steady_clock::now() - startTime);
	logging::log(logger,
	             "Completed project update request for " +
	                 std::string{projectID} + " in " +
	                 gostd::durationString(elapsed));
}

// DidRequestProjectTrees — projectcollectionbuilder.go:730.
void ProjectCollectionBuilder::DidRequestProjectTrees(
    ProjectTreeRequest* projectTreeRequest,
    logging::LogTree* logger) {
	auto startTime = steady_clock::now();

	std::vector<ConfiguredProjectID> currentProjects;
	configuredProjects->Range(
	    [&](const std::shared_ptr<dirty::SyncMapEntry<
	            ConfiguredProjectID, Project*>>& sme) {
		    currentProjects.push_back(sme->Key());
		    return true;
	    });

	collections::SyncSet<ConfiguredProjectID> seenProjects;
	std::unique_ptr<workGroup> wg(
	    newWorkGroup(false));
	for (auto& projectId : currentProjects) {
		wg->Queue([&, projectId] {
			if (auto res = configuredProjects->Load(projectId);
			    res.second) {
				auto& entry = res.first;
				// If this project has potential project reference for
				// any of the project we are loading ancestor tree for
				// load this project first
				if (auto* project = entry->Value();
				    project != nullptr &&
				    (projectTreeRequest->IsAllProjects() ||
				     project->hasPotentialProjectReference(
				         projectTreeRequest))) {
					updateProgram(entry.get(), logger);
				}
				ensureProjectTree(wg.get(), entry,
				                  projectTreeRequest,
				                  &seenProjects, logger);
			}
		});
	}
	wg->RunAndWait();

	auto elapsed =
	    duration_cast<nanoseconds>(steady_clock::now() - startTime);
	std::string projectsStr;
	for (auto& p : projectTreeRequest->Projects()) {
		if (!projectsStr.empty()) projectsStr += " ";
		projectsStr += std::string{p};
	}
	logging::log(logger, "Completed project tree request for [" +
	                     projectsStr + "] in " +
	                     gostd::durationString(elapsed));
}

// ensureProjectTree — projectcollectionbuilder.go:764.
void ProjectCollectionBuilder::ensureProjectTree(
    workGroup* wg,
    const std::shared_ptr<
        dirty::SyncMapEntry<ConfiguredProjectID, Project*>>& entry,
    ProjectTreeRequest* projectTreeRequest,
    collections::SyncSet<ConfiguredProjectID>* seenProjects,
    logging::LogTree* logger) {
	if (!seenProjects->AddIfAbsent(entry->Key())) {
		return;
	}

	auto* project = entry->Value();
	if (project == nullptr) {
		return;
	}

	auto* program = project->GetProgram();
	if (program == nullptr) {
		return;
	}

	// If this project disables child load ignore it
	if (program->CommandLine()
	        ->CompilerOptions()
	        ->DisableReferencedProjectLoad == Tristate::True) {
		return;
	}

	auto children = program->GetResolvedProjectReferences();
	if (children.empty()) {
		return;
	}
	for (auto* childConfig : children) {
		if (childConfig == nullptr) {
			continue;
		}
		// Capture everything by value: this frame can return before the
		// queued task runs.
		wg->Queue([this, wg, projectTreeRequest, seenProjects, logger,
		           childConfig, program] {
			if (!projectTreeRequest->IsAllProjects() &&
			    program->RangeResolvedProjectReferenceInChildConfig(
			        childConfig,
			        [&](tspath::Path referencePath,
			            tsoptions::ParsedCommandLine*,
			            tsoptions::ParsedCommandLine*, int) {
				        return !projectTreeRequest
				            ->IsProjectReferenced(
				                referencePath);
			        })) {
				return;
			}

			// Load this child project since this is referenced
			auto childProjectEntry = findOrCreateProject(
			    childConfig->ConfigName(),
			    childConfig->ConfigFile->SourceFile->Path(),
			    projectLoadKindCreate, logger);
			if (childProjectEntry != nullptr) {
				updateProgram(childProjectEntry.get(), logger);
			}

			// Ensure children for this project
			ensureProjectTree(wg, childProjectEntry,
			                  projectTreeRequest, seenProjects,
			                  logger);
		});
	}
}

// DidUpdateATAState — projectcollectionbuilder.go:821.
void ProjectCollectionBuilder::DidUpdateATAState(
    const std::unordered_map<ID, ATAStateChange*>& ataChanges,
    logging::LogTree* logger) {
	auto updateProject = [&](dirty::IValue<Project*>* project,
	                         ATAStateChange* ataChange) {
		project->ChangeIf(
		    [&](Project* p) {
			    if (p == nullptr) {
				    return false;
			    }
			    // Consistency check: the ATA demands (project options,
			    // unresolved imports) of this project has not changed
			    // since the time the ATA request was dispatched; the
			    // change can still be applied to this project in its
			    // current state.
			    return ataChange->TypingsInfo->Equals(
			        p->ComputeTypingsInfo());
		    },
		    [&](Project*& p) {
			    // We checked before triggering this change (in
			    // Session.triggerATAForUpdatedProjects) that the set
			    // of typings files is actually different.
			    p->installedTypingsInfo = ataChange->TypingsInfo;
			    p->typingsFiles = ataChange->TypingsFiles;
			    auto typingsWatchGlobs = getTypingsLocationsGlobs(
			        ataChange->TypingsFilesToWatch,
			        sessionOptions->TypingsLocation,
			        sessionOptions->CurrentDirectory,
			        p->currentDirectory,
			        fs->fs->UseCaseSensitiveFileNames());
			    p->typingsWatch =
			        watchedFilesClone(p->typingsWatch, typingsWatchGlobs);
			    p->dirty = true;
			    p->dirtyFilePath = tspath::Path{};
		    });
	};

	for (auto& [projectID, ataChange] : ataChanges) {
		logging::embed(logger, ataChange->Logs);
		if (idInferred(projectID).second) {
			updateProject(inferredProject, ataChange);
		} else if (auto syntheticProjectID = idSynthetic(projectID);
		           syntheticProjectID.second) {
			if (auto res =
			        syntheticProjects->Load(syntheticProjectID.first);
			    res.second) {
				updateProject(res.first.get(), ataChange);
			}
		} else if (auto configuredID = idConfigured(projectID);
		           configuredID.second) {
			if (auto res =
			        configuredProjects->Load(configuredID.first);
			    res.second) {
				updateProject(res.first.get(), ataChange);
			}
		}

		logging::log(logger, "Updated ATA state for project " +
		                     std::string{projectID});
	}
}

// DidChangeCustomConfigFileName — projectcollectionbuilder.go:872.
// If customConfigFileName changes, invalidate default projects.
void ProjectCollectionBuilder::DidChangeCustomConfigFileName(
    logging::LogTree* logger) {
	if (!configFileRegistryBuilder_->DidChangeCustomConfigFileName(
	        logger)) {
		return;
	}

	fileDefaultProjects.clear();
	defaultProjectsInvalidated = true;
	programStructureChanged = true;
}

// DidChangeUserPreferences — projectcollectionbuilder.go:883.
void ProjectCollectionBuilder::DidChangeUserPreferences(
    const lsutil::UserPreferences& oldPreferences,
    const lsutil::UserPreferences& newPreferences,
    logging::LogTree* logger) {
	if (oldPreferences.Locale == newPreferences.Locale) {
		return;
	}
	forEachProject([&](dirty::IValue<Project*>* entry) {
		entry->Change([&](Project*& project) {
			project->dirty = true;
			project->dirtyFilePath = tspath::Path{};
			logging::logf(logger,
			              "Marking project as dirty due to locale "
			              "change: %s",
			              std::string{project->ID()});
		});
		return true;
	});
}

// markProjectsAffectedByConfigChanges —
// projectcollectionbuilder.go:896.
bool ProjectCollectionBuilder::markProjectsAffectedByConfigChanges(
    const changeFileResult& configChangeResult,
    logging::LogTree* logger) {
	if (configChangeResult.affectedProjects != nullptr) {
		for (auto& kv : *configChangeResult.affectedProjects) {
			auto& projectID = kv.first;
			dirty::IValue<Project*>* project = nullptr;
			std::shared_ptr<dirty::SyncMapEntry<ConfiguredProjectID,
			                                    Project*>>
			    configuredEntry;
			std::shared_ptr<dirty::SyncMapEntry<SyntheticProjectID,
			                                    Project*>>
			    syntheticEntry;
			if (idInferred(projectID).second) {
				project = inferredProject;
			} else {
				if (auto syntheticProjectID =
				        idSynthetic(projectID);
				    syntheticProjectID.second) {
					if (auto res = syntheticProjects->Load(
					        syntheticProjectID.first);
					    res.second) {
						syntheticEntry = res.first;
						project = syntheticEntry.get();
					}
				}
				if (project == nullptr) {
					if (auto configuredID =
					        idConfigured(projectID);
					    configuredID.second) {
						if (auto res =
						        configuredProjects->Load(
						            configuredID.first);
						    res.second) {
							configuredEntry = res.first;
							project = configuredEntry.get();
						}
					}
				}
			}
			if (project == nullptr ||
			    project->Value() == nullptr) {
				TSC_UNREACHABLE(
				    "project affected by config change not "
				    "found");
			}
			auto* projectEntry = project;
			projectEntry->ChangeIf(
			    [&](Project* p) {
				    return !p->dirty ||
				           !p->dirtyFilePath.empty();
			    },
			    [&](Project*& p) {
				    p->dirty = true;
				    p->dirtyFilePath = tspath::Path{};
				    logging::logf(
				        logger,
				        "Marking project %s as dirty due to "
				        "change affecting config",
				        std::string{projectID});
			    });
		}
	}

	// Recompute default projects for open files that now have
	// different config file presence.
	bool hasChanges = false;
	if (configChangeResult.affectedFiles != nullptr) {
		for (auto& [path, _] : *configChangeResult.affectedFiles) {
			auto it = overlays.find(path);
			if (it == overlays.end()) {
				continue;
			}
			auto fileName = it->second->FileName();
			ensureConfiguredProjectAndAncestorsForFile(fileName,
			                                           path, logger);
			hasChanges = true;
		}
	}

	return hasChanges;
}

// findDefaultProject — projectcollectionbuilder.go:934.
std::shared_ptr<dirty::IValue<Project*>>
ProjectCollectionBuilder::findDefaultProject(
    const std::string& fileName, const tspath::Path& path) {
	if (auto configuredProject =
	        findDefaultConfiguredProject(fileName, path);
	    configuredProject != nullptr) {
		return configuredProject;
	}
	auto it = fileDefaultProjects.find(path);
	if (it != fileDefaultProjects.end()) {
		if (idInferred(it->second).second) {
			return std::shared_ptr<dirty::IValue<Project*>>(
			    inferredProject,
			    [](dirty::IValue<Project*>*) {});
		}
	}
	if (auto* inferredProjectValue = inferredProject->Value();
	    inferredProjectValue != nullptr &&
	    inferredProjectValue->containsFile(path)) {
		fileDefaultProjects[path] = inferredProjectID.AsID();
		return std::shared_ptr<dirty::IValue<Project*>>(
		    inferredProject, [](dirty::IValue<Project*>*) {});
	}
	return nullptr;
}

// findDefaultConfiguredProject — projectcollectionbuilder.go:952.
std::shared_ptr<dirty::SyncMapEntry<ConfiguredProjectID, Project*>>
ProjectCollectionBuilder::findDefaultConfiguredProject(
    const std::string& fileName, const tspath::Path& path) {
	auto it = fileDefaultProjects.find(path);
	if (it != fileDefaultProjects.end()) {
		if (auto configuredID = idConfigured(it->second);
		    configuredID.second) {
			if (auto res =
			        configuredProjects->Load(configuredID.first);
			    res.second) {
				return res.first;
			}
		}
	}
	// Sort configured projects so we can use a deterministic "first" as
	// a last resort.
	std::vector<tspath::Path> configuredProjectPaths;
	std::unordered_map<
	    tspath::Path,
	    std::shared_ptr<
	        dirty::SyncMapEntry<ConfiguredProjectID, Project*>>>
	    configuredProjectsMap;
	configuredProjects->Range(
	    [&](const std::shared_ptr<dirty::SyncMapEntry<
	            ConfiguredProjectID, Project*>>& entry) {
		    auto configuredPath = tspath::Path{entry->Key()};
		    configuredProjectPaths.push_back(configuredPath);
		    configuredProjectsMap[configuredPath] = entry;
		    return true;
	    });
	std::sort(configuredProjectPaths.begin(),
	          configuredProjectPaths.end());

	auto res = findDefaultConfiguredProjectFromProgramInclusion(
	    fileName, path, configuredProjectPaths,
	    [&](const tspath::Path& projectPath) {
		    return configuredProjectsMap[projectPath]->Value();
	    });
	auto& project = res.first;
	auto& multipleCandidates = res.second;

	if (multipleCandidates) {
		if (auto p =
		        findOrCreateDefaultConfiguredProjectForFile(
		            fileName, path, projectLoadKindFind, nullptr)
		            .project;
		    p != nullptr) {
			return p;
		}
	}

	auto jt = configuredProjectsMap.find(project);
	return jt != configuredProjectsMap.end() ? jt->second : nullptr;
}

// ensureConfiguredProjectAndAncestorsForFile —
// projectcollectionbuilder.go:982.
searchResult
ProjectCollectionBuilder::ensureConfiguredProjectAndAncestorsForFile(
    const std::string& fileName, const tspath::Path& path,
    logging::LogTree* logger) {
	auto result = findOrCreateDefaultConfiguredProjectForFile(
	    fileName, path, projectLoadKindCreate, logger);
	if (result.project != nullptr && isOpenFile(path)) {
		createAncestorTree(fileName, path, &result, logger);
	}
	return result;
}

// createAncestorTree — projectcollectionbuilder.go:990.
void ProjectCollectionBuilder::createAncestorTree(
    const std::string& fileName, const tspath::Path& path,
    searchResult* openResult, logging::LogTree* logger) {
	auto* project = openResult->project->Value();
	for (;;) {
		// Skip if project is not composite and we are only looking for
		// solution
		if (project->CommandLine != nullptr &&
		    (project->CommandLine->CompilerOptions()
		             ->Composite != Tristate::True ||
		     project->CommandLine->CompilerOptions()
		             ->DisableSolutionSearching == Tristate::True)) {
			return;
		}

		// Get config file name
		auto ancestorConfigName =
		    configFileRegistryBuilder_->getAncestorConfigFileName(
		        fileName, path, project->ConfigFileName(), logger);
		if (ancestorConfigName.empty()) {
			return;
		}

		// find or delay load the project
		auto ancestorPath = toPath(ancestorConfigName);
		auto ancestor = findOrCreateProject(ancestorConfigName,
		                                    ancestorPath,
		                                    projectLoadKindCreate,
		                                    logger);
		if (ancestor == nullptr) {
			return;
		}

		openResult->retain.Add(ancestorPath);

		// If this ancestor is new and was not updated because we are
		// just creating it for future loading eg when invoking find
		// all references or rename that could span multiple projects
		// we would make the current project as its potential project
		// reference
		if (ancestor->Value()->CommandLine == nullptr &&
		    (project->CommandLine == nullptr ||
		     project->CommandLine->CompilerOptions()
		             ->Composite == Tristate::True)) {
			ancestor->Change([&](Project*& ancestorProject) {
				ancestorProject->setPotentialProjectReference(
				    project->configFilePath);
			});
		}

		project = ancestor->Value();
	}
}

// findOrCreateDefaultConfiguredProjectWorker —
// projectcollectionbuilder.go:1060.
searchResult
ProjectCollectionBuilder::findOrCreateDefaultConfiguredProjectWorker(
    const std::string& fileName, const tspath::Path& path,
    const std::string& configFileName, projectLoadKind loadKind,
    collections::SyncSet<searchNodeKey>* visited,
    searchResult* fallback, logging::LogTree* logger) {
	collections::SyncMap<tspath::Path,
	                     tsoptions::ParsedCommandLine*>
	    configs;
	collections::SyncSet<searchNodeKey> ownedVisited;
	if (visited == nullptr) {
		visited = &ownedVisited;
	}

	auto search =
	    tsc::BreadthFirstSearchParallelEx<searchNodeKey, searchNode>(
	        searchNode{configFileName, loadKind, logger},
	        [&](const searchNode& node) {
		        auto res = configs.Load(
		            toPath(node.configFileName));
		        if (res.second &&
		            !res.first->ProjectReferences().empty()) {
			        auto referenceLoadKind = node.loadKind;
			        if (res.first->CompilerOptions()
			                ->DisableReferencedProjectLoad
			                 == Tristate::True) {
				        referenceLoadKind = projectLoadKindFind;
			        }

			        logging::LogTree* refLogger = nullptr;
			        auto references =
			            res.first->ResolvedProjectReferencePaths();
			        if (!references.empty() &&
			            node.logger != nullptr) {
				        refLogger = node.logger->Fork(
				            "Searching " +
				            std::to_string(references.size()) +
				            " project references of " +
				            node.configFileName);
			        }
			        return tsc::Map<std::string, searchNode>(
			            references, [&](const std::string& refConfig) {
				            return searchNode{
				                refConfig, referenceLoadKind,
				                logging::fork(
				                    refLogger,
				                    "Searching project "
				                    "reference " +
				                        refConfig)};
			            });
		        }
		        return std::vector<searchNode>{};
	        },
	        [&](const searchNode& node) {
		        auto configFilePath =
		            toPath(node.configFileName);
		        auto* config =
		            configFileRegistryBuilder_
		                ->findOrAcquireConfigForFile(
		                    node.configFileName, configFilePath,
		                    path, node.loadKind,
		                    logging::fork(
		                        node.logger,
		                        "Acquiring config for open file"));
		        if (config == nullptr) {
			        logging::log(
			            node.logger,
			            "Config file for project does not "
			            "already exist");
			        return std::pair{false, false};
		        }
		        configs.Store(configFilePath, config);
		        if (config->FileNames().empty()) {
			        // Likely a solution tsconfig.json - the search
			        // will fan out to its references.
			        logging::log(
			            node.logger,
			            "Project does not contain file (no root "
			            "files)");
			        return std::pair{false, false};
		        }

		        if (config->CompilerOptions()->Composite ==
		            Tristate::True) {
			        // For composite projects, we can get an early
			        // negative result.
			        // !!! what about declaration files in
			        //     node_modules? wouldn't it be better to
			        //     check project inclusion if the project is
			        //     already loaded?
			        if (config->FileNamesByPath()->count(path) ==
			            0) {
				        logging::log(
				            node.logger,
				            "Project does not contain file (by "
				            "composite config inclusion)");
				        return std::pair{false, false};
			        }
		        }

		        auto project =
		            findOrCreateProject(node.configFileName,
		                                configFilePath,
		                                node.loadKind,
		                                node.logger);
		        if (project == nullptr) {
			        logging::log(
			            node.logger,
			            "Project does not already exist");
			        return std::pair{false, false};
		        }

		        if (node.loadKind == projectLoadKindCreate) {
			        // Ensure project is up to date before checking
			        // for file inclusion
			        updateProgram(project.get(), node.logger);
		        }

		        if (project->Value()->containsFile(path)) {
			        bool isDirectInclusion =
			            !project->Value()
			                 ->IsSourceFromProjectReference(path);
			        logging::logf(
			            node.logger, "Project contains file %s",
			            isDirectInclusion
			                ? "directly"
			                : "as a source of a referenced "
			                  "project");
			        return std::pair{true, isDirectInclusion};
		        }

		        logging::log(node.logger,
		                     "Project does not contain file");
		        return std::pair{false, false};
	        },
	        tsc::BreadthFirstSearchOptions<searchNodeKey,
	                                        searchNode>{
	            .Visited = visited,
	            .PreprocessLevel =
	                [&](tsc::BreadthFirstSearchLevel<
	                    searchNodeKey, searchNode>* level) {
		                level->Range([&](const searchNode& node) {
			                if (node.loadKind ==
			                        projectLoadKindFind &&
			                    level->Has(searchNodeKey{
			                        node.configFileName,
			                        projectLoadKindCreate})) {
				                // Remove find requests when a create
				                // request for the same project is
				                // already present.
				                level->Delete(searchNodeKey{
				                    node.configFileName,
				                    node.loadKind});
			                }
			                return true;
		                });
	                },
	        },
	        [](const searchNode& node) {
		        return searchNodeKey{node.configFileName,
		                             node.loadKind};
	        });

	collections::Set<tspath::Path> retain;
	std::shared_ptr<
	    dirty::SyncMapEntry<ConfiguredProjectID, Project*>>
	    project;
	if (!search.Path.empty()) {
		auto res = configuredProjects->Load(
		    ConfiguredProjectID{
		        toPath(search.Path[0].configFileName)});
		project = res.first;
		// If we found a project, we retain each project along the BFS
		// path. We don't want to retain everything we visited since
		// BFS can terminate early, and we don't want to retain
		// nondeterministically.
		for (auto& node : search.Path) {
			retain.Add(toPath(node.configFileName));
		}
	}

	if (search.Stopped) {
		// Found a project that directly contains the file.
		return searchResult{project, retain};
	}

	if (project != nullptr) {
		// If we found a project that contains the file, but it is a
		// source from a project reference, record it as a fallback.
		*fallback = searchResult{project, retain};
	}

	// Look for tsconfig.json files higher up the directory tree and do
	// the same. This handles the common case where a higher-level
	// "solution" tsconfig.json contains all projects in a workspace.
	if (auto res = configs.Load(toPath(configFileName));
	    res.second &&
	    res.first->CompilerOptions()
	            ->DisableSolutionSearching == Tristate::True) {
		if (fallback != nullptr) {
			return *fallback;
		}
	}
	if (auto ancestorConfigName =
	        configFileRegistryBuilder_->getAncestorConfigFileName(
	            fileName, path, configFileName, logger);
	    !ancestorConfigName.empty()) {
		return findOrCreateDefaultConfiguredProjectWorker(
		    fileName, path, ancestorConfigName, loadKind, visited,
		    fallback,
		    logging::fork(logger, "Searching ancestor config file "
		                          "at " +
		                          ancestorConfigName));
	}
	if (fallback != nullptr) {
		return *fallback;
	}
	// If we didn't find anything, we can retain everything we
	// visited, since the whole graph must have been traversed (i.e.,
	// the set of retained projects is guaranteed to be
	// deterministic).
	visited->Range([&](const searchNodeKey& node) {
		retain.Add(toPath(node.configFileName));
		return true;
	});
	return searchResult{nullptr, retain};
}

// findOrCreateDefaultConfiguredProjectForFile —
// projectcollectionbuilder.go:1218.
searchResult ProjectCollectionBuilder::
    findOrCreateDefaultConfiguredProjectForFile(
        const std::string& fileName, const tspath::Path& path,
        projectLoadKind loadKind, logging::LogTree* logger) {
	auto it = fileDefaultProjects.find(path);
	if (it != fileDefaultProjects.end()) {
		if (idInferred(it->second).second) {
			// The file belongs to the inferred project
			return searchResult{};
		}
		auto configuredID = idConfigured(it->second);
		auto res = configuredProjects->Load(configuredID.first);
		return searchResult{res.first, {}};
	}
	if (auto configFileName =
	        configFileRegistryBuilder_->getConfigFileNameForFile(
	            fileName, path, logger);
	    !configFileName.empty()) {
		auto startTime = steady_clock::now();
		auto result = findOrCreateDefaultConfiguredProjectWorker(
		    fileName, path, configFileName, loadKind, nullptr,
		    nullptr,
		    logging::fork(
		        logger, "Searching for default configured "
		                "project for " +
		                fileName));
		if (result.project != nullptr) {
			fileDefaultProjects[path] =
			    result.project->Value()->ID();
		}
		auto elapsed = duration_cast<nanoseconds>(
		    steady_clock::now() - startTime);
		if (result.project != nullptr) {
			logging::log(logger,
			             "Found default configured project for " +
			                 fileName + ": " +
			                 result.project->Value()
			                     ->ConfigFileName() +
			                 " (in " +
			                 gostd::durationString(elapsed) +
			                 ")");
		} else {
			logging::log(logger,
			             "No default configured project found "
			             "for " +
			                 fileName + " (searched in " +
			                 gostd::durationString(elapsed) +
			                 ")");
		}
		return result;
	}
	return searchResult{};
}

// findOrCreateProject — projectcollectionbuilder.go:1257.
std::shared_ptr<dirty::SyncMapEntry<ConfiguredProjectID, Project*>>
ProjectCollectionBuilder::findOrCreateProject(
    const std::string& configFileName,
    const tspath::Path& configFilePath, projectLoadKind loadKind,
    logging::LogTree* logger) {
	if (loadKind == projectLoadKindFind) {
		auto res = configuredProjects->Load(
		    ConfiguredProjectID{configFilePath});
		return res.first;
	}
	auto res = configuredProjects->LoadOrStore(
	    ConfiguredProjectID{configFilePath},
	    NewConfiguredProject(configFileName, configFilePath, this,
	                         logger));
	return res.first;
}

// updateInferredProjectRoots — projectcollectionbuilder.go:1269.
bool ProjectCollectionBuilder::updateInferredProjectRoots(
    const std::vector<std::string>& rootFileNames,
    logging::LogTree* logger) {
	auto filtered = tsc::Filter(
	    rootFileNames,
	    [&](const std::string& f) {
		    return isSupportedInInferredProject(f);
	    });
	std::vector<ProjectReference*> projectReferences;
	std::vector<Diagnostic*> configFileParsingDiagnostics;
	if (auto* project = inferredProject->Value();
	    project != nullptr) {
		projectReferences =
		    project->CommandLine->ProjectReferences();
		configFileParsingDiagnostics =
		    project->CommandLine->Errors;
	}
	return updateInferredProject(
	    filtered, compilerOptionsForInferredProjects,
	    projectReferences, configFileParsingDiagnostics,
	    inferredContentMappers, logger);
}

// updateOrCreateSyntheticProject —
// projectcollectionbuilder.go:1280.
std::shared_ptr<dirty::SyncMapEntry<SyntheticProjectID, Project*>>
ProjectCollectionBuilder::updateOrCreateSyntheticProject(
    const SyntheticProjectID& projectID,
    const std::vector<std::string>& rootFileNames,
    CompilerOptions* compilerOptions,
    const std::vector<ProjectReference*>& projectReferences,
    const std::vector<Diagnostic*>& configFileParsingDiagnostics,
    ModuleResolverFactory* moduleResolverFactory,
    uint64_t moduleResolverID,
    const std::vector<contentmapper::Mapper*>& contentMappers,
    logging::LogTree* logger) {
	auto res = syntheticProjects->Load(projectID);
	if (!res.second) {
		auto* syntheticProject = newSyntheticProject(
		    projectID, sessionOptions->CurrentDirectory,
		    compilerOptions, rootFileNames, projectReferences,
		    contentMappers, this, logger);
		syntheticProject->CommandLine->Errors =
		    configFileParsingDiagnostics;
		syntheticProject->moduleResolverFactory =
		    moduleResolverFactory;
		syntheticProject->moduleResolverID = moduleResolverID;
		auto res2 =
		    syntheticProjects->LoadOrStore(projectID,
		                                   syntheticProject);
		return res2.first;
	}

	auto project = res.first;
	auto* currentProject = project->Value();
	if (compilerOptions == nullptr) {
		compilerOptions =
		    currentProject->CommandLine->CompilerOptions();
	}
	auto* newCommandLine = newInferredProjectCommandLine(
	    compilerOptions, rootFileNames, projectReferences,
	    contentMappers,
	    tspath::ComparePathsOptions{
	        .useCaseSensitiveFileNames =
	            fs->fs->UseCaseSensitiveFileNames(),
	        .currentDirectory = currentProject->currentDirectory,
	    });
	newCommandLine->Errors = configFileParsingDiagnostics;
	project->ChangeIf(
	    [&](Project* p) {
		    return p->CommandLine->FileNames() !=
		               newCommandLine->FileNames() ||
		           !compilerOptionsDeepEqual(
		               p->CommandLine->CompilerOptions(),
		               compilerOptions) ||
		           !projectReferencesEqual(
		               p->CommandLine->ProjectReferences(),
		               projectReferences) ||
		           p->CommandLine->Errors !=
		               configFileParsingDiagnostics ||
		           p->CommandLine->ContentMappers() !=
		               newCommandLine->ContentMappers() ||
		           p->moduleResolverID != moduleResolverID;
	    },
	    [&](Project*& p) {
		    logging::log(
		        logger, "Updating synthetic project config with " +
		                std::to_string(rootFileNames.size()) +
		                " root files");
		    p->SetCommandLine(newCommandLine);
		    p->moduleResolverFactory = moduleResolverFactory;
		    p->moduleResolverID = moduleResolverID;
	    });
	return project;
}

// updateInferredProject — projectcollectionbuilder.go:1333. Preserves
// the current command line when roots/options are unchanged.
bool ProjectCollectionBuilder::updateInferredProject(
    std::vector<std::string> rootFileNames,
    CompilerOptions* compilerOptions,
    const std::vector<ProjectReference*>& projectReferences,
    const std::vector<Diagnostic*>& configFileParsingDiagnostics,
    const std::vector<contentmapper::Mapper*>& contentMappers,
    logging::LogTree* logger) {
	if (rootFileNames.empty()) {
		return deleteInferredProject(logger);
	}
	rootFileNames = std::vector<std::string>(rootFileNames);
	std::sort(rootFileNames.begin(), rootFileNames.end());
	return updateOrCreateInferredProject(
	    rootFileNames, compilerOptions, projectReferences,
	    configFileParsingDiagnostics, contentMappers, logger);
}

// deleteInferredProject — projectcollectionbuilder.go:1346.
bool ProjectCollectionBuilder::deleteInferredProject(
    logging::LogTree* logger) {
	auto* project = inferredProject->Value();
	if (project == nullptr) {
		return false;
	}
	logging::log(logger, "Deleting inferred project");
	if (project->Program != nullptr) {
		project->Program->RangeResolvedProjectReference(
		    [&](tspath::Path referencePath,
		        tsoptions::ParsedCommandLine*,
		        tsoptions::ParsedCommandLine*, int) {
			    configFileRegistryBuilder_
			        ->releaseConfigForProject(
			            referencePath, project->ID());
			    return true;
		    });
	}
	inferredProject->Delete();
	return true;
}

// updateOrCreateInferredProject — projectcollectionbuilder.go:1367.
// Always retains an inferred project, including when rootFileNames is
// empty. The caller transfers ownership of rootFileNames.
bool ProjectCollectionBuilder::updateOrCreateInferredProject(
    std::vector<std::string> rootFileNames,
    CompilerOptions* compilerOptions,
    const std::vector<ProjectReference*>& projectReferences,
    const std::vector<Diagnostic*>& configFileParsingDiagnostics,
    const std::vector<contentmapper::Mapper*>& contentMappers,
    logging::LogTree* logger) {
	auto* project = inferredProject->Value();
	if (project == nullptr) {
		project = NewInferredProject(
		    sessionOptions->CurrentDirectory, compilerOptions,
		    rootFileNames, projectReferences, contentMappers, this,
		    logger);
		project->CommandLine->Errors =
		    configFileParsingDiagnostics;
		inferredProject->Set(project);
		return true;
	}

	if (compilerOptions == nullptr) {
		compilerOptions = project->CommandLine->CompilerOptions();
	}
	auto* newCommandLine = newInferredProjectCommandLine(
	    compilerOptions, rootFileNames, projectReferences,
	    contentMappers,
	    tspath::ComparePathsOptions{
	        .useCaseSensitiveFileNames =
	            fs->fs->UseCaseSensitiveFileNames(),
	        .currentDirectory = project->currentDirectory,
	    });
	newCommandLine->Errors = configFileParsingDiagnostics;
	bool changed = inferredProject->ChangeIf(
	    [&](Project* p) {
		    return p->CommandLine->FileNames() !=
		               newCommandLine->FileNames() ||
		           !compilerOptionsDeepEqual(
		               p->CommandLine->CompilerOptions(),
		               compilerOptions) ||
		           !projectReferencesEqual(
		               p->CommandLine->ProjectReferences(),
		               projectReferences) ||
		           p->CommandLine->Errors !=
		               configFileParsingDiagnostics ||
		           p->CommandLine->ContentMappers() !=
		               newCommandLine->ContentMappers();
	    },
	    [&](Project*& p) {
		    logging::log(
		        logger, "Updating inferred project config with " +
		                std::to_string(rootFileNames.size()) +
		                " root files");
		    p->SetCommandLine(newCommandLine);
	    });
	if (!changed) {
		return false;
	}
	return true;
}

// projectReferencesEqual — projectcollectionbuilder.go:1410.
bool projectReferencesEqual(
    const std::vector<ProjectReference*>& a,
    const std::vector<ProjectReference*>& b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (size_t i = 0; i < a.size(); i++) {
		auto* pa = a[i];
		auto* pb = b[i];
		if (pa == nullptr || pb == nullptr) {
			if (pa != pb) {
				return false;
			}
			continue;
		}
		if (pa->Path != pb->Path || pa->Circular != pb->Circular) {
			return false;
		}
	}
	return true;
}

// isSupportedInInferredProject — projectcollectionbuilder.go:1420.
bool ProjectCollectionBuilder::isSupportedInInferredProject(
    const std::string& fileName) {
	if (tspath::isDynamicFileName(fileName) ||
	    tsc::getScriptKindFromFileName(fileName) !=
	        ScriptKind::Unknown) {
		return true;
	}
	if (auto* file = fs->GetFile(fileName);
	    file != nullptr && file->IsOverlay() &&
	    tspath::getAnyExtensionFromPath(fileName, nullptr, false)
	        .empty()) {
		return true;
	}
	static const std::vector<std::string_view> emptyExts;
	std::vector<std::string_view> exts;
	exts.reserve(inferredContentMapperExtensions.size());
	for (auto& e : inferredContentMapperExtensions) {
		exts.push_back(e);
	}
	return tspath::fileExtensionIsOneOf(fileName, exts);
}

// updateProgram — projectcollectionbuilder.go:1432. Updates the
// program for the given project entry if necessary. Returns whether
// the update could have caused any structure-affecting changes.
bool ProjectCollectionBuilder::updateProgram(
    dirty::IValue<Project*>* entry, logging::LogTree* logger) {
	bool doUpdateProgram = false;
	bool deleteProjectFlag = false;
	bool filesChanged = false;
	auto projectID = entry->Value()->ID();
	auto startTime = steady_clock::now();
	bool notifiedLoading = false;
	std::string displayName;
	entry->Locked([&](dirty::IValue<Project*>* locked) {
		if (locked->Value()->Kind == Kind::Configured) {
			auto* commandLine =
			    configFileRegistryBuilder_
			        ->acquireConfigForProject(
			            locked->Value()->ConfigFileName(),
			            locked->Value()->configFilePath,
			            locked->Value(),
			            logging::fork(
			                logger,
			                "Acquiring config for project"));
			if (commandLine == nullptr) {
				deleteProjectFlag = true;
				filesChanged = true;
				return;
			}
			if (locked->Value()->CommandLine != commandLine) {
				doUpdateProgram = true;
				locked->Change([&](Project*& p) {
					p->SetCommandLine(commandLine);
				});
			}
		}
		if (!doUpdateProgram) {
			doUpdateProgram = locked->Value()->dirty;
		}
		if (doUpdateProgram) {
			if (client != nullptr) {
				displayName = locked->Value()->DisplayName(
				    sessionOptions->CurrentDirectory);
				notifiedLoading = true;
			}
		}
	});
	if (notifiedLoading && client != nullptr) {
		client->ProgressStart(tsc::Project_0,
		                      {displayName});
	}
	if (deleteProjectFlag) {
		deleteProject(entry, logger);
	}
	if (doUpdateProgram) {
		entry->Locked([&](dirty::IValue<Project*>* locked) {
			locked->Change([&](Project*& project) {
				auto* oldHost = project->host;
				auto* oldProgram = project->Program;
				auto* oldCheckerPool = project->checkerPool;
				project->host = newCompilerHost(
				    project->currentDirectory, project, this,
				    logging::fork(logger, "CompilerHost"));
				auto result = project->CreateProgram();
				std::vector<std::string> watchedFiles;
				for (auto* mapper :
				     project->CommandLine->ContentMappers()) {
					if (!mapper->Definition.Package.empty() &&
					    mapper->ContributionID.empty() &&
					    !mapper->PackageDirectory.empty()) {
						watchedFiles.push_back(
						    tspath::combinePaths(
						        mapper->PackageDirectory,
						        {"package.json"}));
					}
				}
				auto sourceFiles =
				    result.Program->SourceFiles();
				bool hasContentMapped = std::any_of(
				    sourceFiles.begin(), sourceFiles.end(),
				    [](SourceFile* file) {
					    return !file->ContentMapper()
					            .empty();
				    });
				if (hasContentMapped) {
					if (auto contentMapperProject =
					        project->host
					            ->ContentMapperProject();
					    contentMapperProject != nullptr) {
						auto watched =
						    contentMapperProject
						        ->WatchedFiles();
						watchedFiles.insert(
						    watchedFiles.end(),
						    watched.first.begin(),
						    watched.first.end());
					}
				}
				std::sort(watchedFiles.begin(),
				          watchedFiles.end());
				watchedFiles.erase(
				    std::unique(watchedFiles.begin(),
				                watchedFiles.end()),
				    watchedFiles.end());
				project->contentMapperWatch =
				    project->contentMapperWatch->Clone(
				        watchedFiles);
				project->contentMapperWatchedFiles =
				    new collections::Set<tspath::Path>(
				        watchedFiles.size());
				for (auto& wf : watchedFiles) {
					project->contentMapperWatchedFiles->Add(
					    toPath(wf));
				}
				project->Program = result.Program;
				project->checkerPool =
				    static_cast<checkerPool*>(
				        result.Program->GetCheckerPool());
				project->ProgramUpdateKind = result.UpdateKind;
				project->ProgramLastUpdate = newSnapshotID;
				if (result.UpdateKind ==
				    ProgramUpdateKind::Cloned) {
					project->host->sourceFS->seenFiles =
					    oldHost->sourceFS->seenFiles;
				}
				if (result.UpdateKind ==
				    ProgramUpdateKind::NewFiles) {
					filesChanged = true;
					project->programFilesWatch =
					    project->CloneWatchers();
				}
				project->dirty = false;
				project->dirtyFilePath = tspath::Path{};
				releaseDroppedProjectReferences(
				    oldProgram, result.Program,
				    project->ID());
				if (oldCheckerPool != nullptr) {
					oldCheckerPool->Discard();
				}
			});
		});
	}
	if (notifiedLoading && client != nullptr) {
		client->ProgressFinish(tsc::Project_0,
		                       {displayName});
	}
	if (doUpdateProgram) {
		auto elapsed = duration_cast<nanoseconds>(
		    steady_clock::now() - startTime);
		logging::log(logger,
		             "Program update for " +
		                 std::string{projectID} +
		                 " completed in " +
		                 gostd::durationString(elapsed));
	}
	return filesChanged;
}

// markFilesChanged — projectcollectionbuilder.go:1537.
void ProjectCollectionBuilder::markFilesChanged(
    dirty::IValue<Project*>* entry,
    const std::vector<tspath::Path>& paths,
    lsp::lsproto::FileChangeType changeType,
    logging::LogTree* logger) {
	bool isDirty = false;
	tspath::Path dirtyFilePath;
	entry->ChangeIf(
	    [&](Project* p) {
		    if (p->Program == nullptr ||
		        (p->dirty && p->dirtyFilePath.empty())) {
			    return false;
		    }

		    dirtyFilePath = p->dirtyFilePath;
		    for (auto& path : paths) {
			    if (p->containsFile(path)) {
				    isDirty = true;
				    if (changeType ==
				        lsp::lsproto::FileChangeTypeDeleted) {
					    dirtyFilePath = tspath::Path{};
					    break;
				    }
				    // package.json changes can affect module
				    // resolution and package identity (e.g. dedup
				    // decisions), so they must always trigger a
				    // full rebuild rather than a single-file
				    // clone.
				    if (tspath::getBaseFileName(
				            std::string{path}) ==
				        "package.json") {
					    dirtyFilePath = tspath::Path{};
					    break;
				    }
				    if (dirtyFilePath.empty()) {
					    dirtyFilePath = path;
				    } else if (dirtyFilePath != path) {
					    dirtyFilePath = tspath::Path{};
					    break;
				    }
			    } else if (
			        p->host != nullptr &&
			        ((changeType ==
			              lsp::lsproto::FileChangeTypeCreated &&
			          p->host->sourceFS
			              ->SeenFileOrMissingParentDirectory(
			                  path)) ||
			         (changeType !=
			              lsp::lsproto::FileChangeTypeCreated &&
			          p->host->sourceFS->SeenFile(path)))) {
				    isDirty = true;
				    dirtyFilePath = tspath::Path{};
				    break;
			    }
		    }
		    return isDirty ||
		           p->dirtyFilePath != dirtyFilePath;
	    },
	    [&](Project*& p) {
		    p->dirty = true;
		    p->dirtyFilePath = dirtyFilePath;
		    if (!dirtyFilePath.empty()) {
			    logging::logf(logger,
			                  "Marking project %s as dirty due "
			                  "to changes in %s",
			                  std::string{p->ID()},
			                  std::string{dirtyFilePath});
		    } else {
			    logging::logf(logger,
			                  "Marking project %s as dirty",
			                  std::string{p->ID()});
		    }
	    });
}

// deleteProject — projectcollectionbuilder.go:1590.
void ProjectCollectionBuilder::deleteProject(
    dirty::IValue<Project*>* project, logging::LogTree* logger) {
	auto* value = project->Value();
	auto projectID = value->ID();
	logging::logf(logger, "Deleting %s project: %s",
	              kindString(value->Kind),
	              std::string{value->ID()});
	if (value->Program != nullptr) {
		value->Program->RangeResolvedProjectReference(
		    [&](tspath::Path referencePath,
		        tsoptions::ParsedCommandLine*,
		        tsoptions::ParsedCommandLine*, int) {
			    configFileRegistryBuilder_
			        ->releaseConfigForProject(
			            referencePath, projectID);
			    return true;
		    });
	}
	if (value->Kind == Kind::Configured) {
		configFileRegistryBuilder_->releaseConfigForProject(
		    value->ConfigFilePath(), projectID);
	}
	project->Delete();
}

// releaseDroppedProjectReferences — projectcollectionbuilder.go:1612.
// Releases the config entries for project references that were
// present in oldProgram but are no longer referenced by newProgram.
// Creating newProgram already re-acquires the config for every
// reference it still resolves, so only the dropped references need
// to be released here.
void ProjectCollectionBuilder::releaseDroppedProjectReferences(
    compiler::SimpleProgram* oldProgram,
    compiler::SimpleProgram* newProgram, const ID& projectID) {
	if (oldProgram == nullptr || oldProgram == newProgram) {
		return;
	}
	collections::Set<tspath::Path> newReferences;
	if (newProgram != nullptr) {
		newProgram->RangeResolvedProjectReference(
		    [&](tspath::Path referencePath,
		        tsoptions::ParsedCommandLine*,
		        tsoptions::ParsedCommandLine*, int) {
			    newReferences.Add(referencePath);
			    return true;
		    });
	}
	oldProgram->RangeResolvedProjectReference(
	    [&](tspath::Path referencePath,
	        tsoptions::ParsedCommandLine*,
	        tsoptions::ParsedCommandLine*, int) {
		    if (!newReferences.Has(referencePath)) {
			    configFileRegistryBuilder_
			        ->releaseConfigForProject(
			            referencePath, projectID);
		    }
		    return true;
	    });
}

} // namespace tsc::project
