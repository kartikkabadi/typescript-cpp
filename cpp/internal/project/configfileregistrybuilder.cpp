// configfileregistrybuilder.go — configFileRegistryBuilder methods.
//
// === slice: project ===

#include "internal/project/configfileregistrybuilder.h"

#include <algorithm>
#include <utility>

#include "internal/core/types.h"
#include "internal/core/utilities.h"
#include "internal/core/utilities.h"
#include "internal/project/snapshotfs.h"
#include "internal/project/watch.h"

namespace tsc::project {

// newConfigFileRegistryBuilder — configfileregistrybuilder.go:44.
configFileRegistryBuilder* newConfigFileRegistryBuilder(
    bool hasRelativePatternCapability, snapshotFSBuilder* fs,
    std::function<bool(const tspath::Path&)> isOpenFile,
    ConfigFileRegistry* oldConfigFileRegistry,
    ExtendedConfigCache* extendedConfigCache, uint64_t snapshotID,
    SessionOptions* sessionOptions,
    const std::string& customConfigFileName,
    logging::LogTree* logger) {
	auto* b = new configFileRegistryBuilder{};
	b->hasRelativePatternCapability = hasRelativePatternCapability;
	b->fs = newSourceFS(false, fs, fs->toPath);
	b->isOpenFile = std::move(isOpenFile);
	b->base = oldConfigFileRegistry;
	b->sessionOptions = sessionOptions;
	b->extendedConfigCache = extendedConfigCache;
	b->snapshotID = snapshotID;
	b->customConfigFileName = customConfigFileName;
	b->customConfigFileNameChanged =
	    customConfigFileName != oldConfigFileRegistry->customConfigFileName;
	b->allConfiguredContentMappers =
	    oldConfigFileRegistry->contentMappers();
	b->configs = dirty::newSyncMap<tspath::Path, configFileEntry*>(
	    oldConfigFileRegistry->configs);
	b->configFileNames =
	    dirty::newMap<tspath::Path, configFileNames*>(
	        oldConfigFileRegistry->configFileNames);
	return b;
}

// findOrAcquireConfigForFile — configfileregistrybuilder.go:125.
tsoptions::ParsedCommandLine*
configFileRegistryBuilder::findOrAcquireConfigForFile(
    const std::string& configFileName,
    const tspath::Path& configFilePath,
    const tspath::Path& filePath, projectLoadKind loadKind,
    logging::LogTree* logger) {
	switch (loadKind) {
	case projectLoadKindFind: {
		auto res = configs->Load(configFilePath);
		if (res.second) {
			return res.first->Value()->commandLine;
		}
		return nullptr;
	}
	case projectLoadKindCreate:
		return acquireConfigForFile(configFileName, configFilePath,
		                            filePath, logger);
	default:
		TSC_UNREACHABLE("unknown project load kind");
	}
}

// reloadIfNeeded — configfileregistrybuilder.go:148. Should only be
// called from within the Change() method of a dirty map entry.
bool configFileRegistryBuilder::reloadIfNeeded(
    configFileEntry* entry, const std::string& fileName,
    const tspath::Path& path, logging::LogTree* logger) {
	auto* oldCommandLine = entry->commandLine;
	switch (entry->pendingReload) {
	case PendingReloadFileNames:
		logging::log(logger, "Reloading file names for config: " + fileName);
		entry->commandLine =
		    entry->commandLine->ReloadFileNamesOfParsedCommandLine(
		        this);
		break;
	case PendingReloadFull: {
		logging::log(logger, "Loading config file: " + fileName);
		// When the workspace is trusted, enable external content mappers
		// so a config's contentMappers pass the runExternalCode gate and
		// register, as they would with the CLI flag.
		CompilerOptions* existingOptions = nullptr;
		if (sessionOptions->RunExternalCode) {
			existingOptions = new CompilerOptions();
			existingOptions->RunExternalCode = Tristate::True;
		}
		entry->commandLine =
		    tsoptions::GetParsedCommandLineOfConfigFilePath(
		        fileName, path, existingOptions,
		        tsoptions::JsonObjectPtr{} /*optionsRaw*/, this, this)
		        .first;
		updateExtendingConfigs(path, entry->commandLine,
		                       oldCommandLine);
		updateRootFilesWatch(fileName, entry);
		logging::log(logger, "Finished loading config file");
		break;
	}
	default:
		return false;
	}
	entry->pendingReload = PendingReloadNone;
	return oldCommandLine != entry->commandLine;
}

// updateExtendingConfigs — configfileregistrybuilder.go:173.
void configFileRegistryBuilder::updateExtendingConfigs(
    const tspath::Path& extendingConfigPath,
    tsoptions::ParsedCommandLine* newCommandLine,
    tsoptions::ParsedCommandLine* oldCommandLine) {
	collections::Set<tspath::Path> newExtendedConfigPaths;
	if (newCommandLine != nullptr) {
		for (auto& extendedConfig :
		     newCommandLine->ExtendedSourceFiles()) {
			auto extendedConfigPath = fs->toPath(extendedConfig);
			newExtendedConfigPaths.Add(extendedConfigPath);
			auto res = configs->LoadOrStore(
			    extendedConfigPath,
			    newExtendedConfigFileEntry(extendedConfig,
			                               extendingConfigPath));
			auto& entry = res.first;
			auto loaded = res.second;
			if (loaded) {
				entry->ChangeIf(
				    [&](configFileEntry* config) {
					    return config->retainingConfigs.count(
					               extendingConfigPath) == 0;
				    },
				    [&](configFileEntry* config) {
					    config->retainingConfigs.emplace(
					        extendingConfigPath,
					        std::monostate{});
				    });
			}
		}
	}
	if (oldCommandLine != nullptr) {
		for (auto& extendedConfig :
		     oldCommandLine->ExtendedSourceFiles()) {
			auto extendedConfigPath = fs->toPath(extendedConfig);
			if (newExtendedConfigPaths.Has(extendedConfigPath)) {
				continue;
			}
			auto res = configs->Load(extendedConfigPath);
			if (res.second) {
				auto& entry = res.first;
				entry->ChangeIf(
				    [&](configFileEntry* config) {
					    return config->retainingConfigs.count(
					               extendingConfigPath) != 0;
				    },
				    [&](configFileEntry* config) {
					    config->retainingConfigs.erase(
					        extendingConfigPath);
				    });
			}
		}
	}
}

// updateRootFilesWatch — configfileregistrybuilder.go:217.
void configFileRegistryBuilder::updateRootFilesWatch(
    const std::string& fileName, configFileEntry* entry) {
	if (entry->rootFilesWatch == nullptr) {
		return;
	}

	std::unordered_set<std::string> ignored;
	std::vector<std::string> globs;
	std::vector<std::string> externalDirectories;
	bool includeWorkspace = false;
	bool includeTsconfigDir = false;
	auto tsconfigDir = tspath::getDirectoryPath(fileName);
	tspath::ComparePathsOptions comparePathsOptions{
	    .useCaseSensitiveFileNames =
	        fs->UseCaseSensitiveFileNames(),
	    .currentDirectory = sessionOptions->CurrentDirectory,
	};
	// entry->commandLine is nil when the config file couldn't be parsed
	// (e.g. it doesn't exist); Go's WildcardDirectories/LiteralFileNames
	// are nil-receiver-safe and iterate empty here.
	if (entry->commandLine != nullptr) {
		auto* wildcardDirectories =
		    entry->commandLine->WildcardDirectories();
		for (auto& [dir, _] : *wildcardDirectories) {
			if (tspath::containsPath(sessionOptions->CurrentDirectory,
			                         dir, comparePathsOptions)) {
				includeWorkspace = true;
			} else if (tspath::containsPath(tsconfigDir, dir,
			                                comparePathsOptions)) {
				includeTsconfigDir = true;
			} else {
				externalDirectories.push_back(dir);
			}
		}
		for (auto& rootFileName :
		     entry->commandLine->LiteralFileNames()) {
			if (tspath::containsPath(sessionOptions->CurrentDirectory,
			                         rootFileName,
			                         comparePathsOptions)) {
				includeWorkspace = true;
			} else if (tspath::containsPath(tsconfigDir, rootFileName,
			                                comparePathsOptions)) {
				includeTsconfigDir = true;
			} else {
				externalDirectories.push_back(
				    tspath::getDirectoryPath(rootFileName));
			}
		}
	}

	if (includeWorkspace) {
		globs.push_back(getRecursiveGlobPattern(
		    sessionOptions->CurrentDirectory));
	}
	if (includeTsconfigDir) {
		globs.push_back(getRecursiveGlobPattern(tsconfigDir));
	}
	for (auto& extendedFileName :
	     entry->commandLine != nullptr
	         ? entry->commandLine->ExtendedSourceFiles()
	         : std::vector<std::string>{}) {
		if (includeWorkspace &&
		    tspath::containsPath(sessionOptions->CurrentDirectory,
		                         extendedFileName,
		                         comparePathsOptions)) {
			continue;
		}
		globs.push_back(extendedFileName);
	}
	if (!externalDirectories.empty()) {
		auto parentsRes = tspath::getCommonParents(
		    externalDirectories, minWatchLocationDepth,
		    getPathComponentsForWatching, comparePathsOptions);
		auto& commonParents = parentsRes.first;
		auto& ignoredExternalDirs = parentsRes.second;
		for (auto& parent : commonParents) {
			globs.push_back(getRecursiveGlobPattern(parent));
		}
		for (auto& d : ignoredExternalDirs) {
			ignored.insert(d);
		}
	}

	std::sort(globs.begin(), globs.end());
	entry->rootFilesWatch = entry->rootFilesWatch->Clone(
	    PatternsAndIgnored{{}, globs, ignored});
}

// acquireConfigForProject — configfileregistrybuilder.go:283. Loads a
// config file entry from the cache, or parses it if not already
// cached, then adds the project (if provided) to `retainingProjects`
// to keep it alive in the cache. Each `acquireConfigForProject` call
// that passes a `project` should be accompanied by an eventual
// `releaseConfigForProject` call with the same project.
tsoptions::ParsedCommandLine*
configFileRegistryBuilder::acquireConfigForProject(
    const std::string& fileName, const tspath::Path& path,
    Project* project, logging::LogTree* logger) {
	auto res = configs->LoadOrStore(
	    path,
	    newConfigFileEntry(hasRelativePatternCapability, fileName));
	auto& entry = res.first;
	bool needsRetainProject = false;
	bool contentMappersChanged = false;
	entry->ChangeIf(
	    [&](configFileEntry* config) {
		    needsRetainProject = config->retainingProjects.count(
		                             project->ID()) == 0;
		    return needsRetainProject ||
		           config->pendingReload != PendingReloadNone;
	    },
	    [&](configFileEntry* config) {
		    if (needsRetainProject) {
			    config->retainingProjects.emplace(
			        project->ID(), std::monostate{});
		    }
		    contentMappersChanged =
		        reloadIfNeeded(config, fileName, path, logger);
	    });
	if (contentMappersChanged) {
		invalidateContentMappers();
	}
	return entry->Value()->commandLine;
}

// acquireConfigForFile — configfileregistrybuilder.go:313. Loads a
// config file entry from the cache, or parses it if not already
// cached, then adds the open file to `retainingOpenFiles` to keep it
// alive in the cache. Each `acquireConfigForFile` call that passes an
// `openFilePath` should be accompanied by an eventual
// `releaseConfigForOpenFile` call with the same open file.
tsoptions::ParsedCommandLine*
configFileRegistryBuilder::acquireConfigForFile(
    const std::string& configFileName,
    const tspath::Path& configFilePath,
    const tspath::Path& filePath, logging::LogTree* logger) {
	auto res = configs->LoadOrStore(
	    configFilePath,
	    newConfigFileEntry(hasRelativePatternCapability,
	                       configFileName));
	auto& entry = res.first;
	bool needsRetainOpenFile = false;
	bool contentMappersChanged = false;
	entry->ChangeIf(
	    [&](configFileEntry* config) {
		    if (isOpenFile(filePath)) {
			    needsRetainOpenFile =
			        config->retainingOpenFiles.count(filePath) == 0;
		    }
		    return needsRetainOpenFile ||
		           config->pendingReload != PendingReloadNone;
	    },
	    [&](configFileEntry* config) {
		    if (needsRetainOpenFile) {
			    config->retainingOpenFiles.emplace(
			        filePath, std::monostate{});
		    }
		    contentMappersChanged = reloadIfNeeded(
		        config, configFileName, configFilePath, logger);
	    });
	if (contentMappersChanged) {
		invalidateContentMappers();
	}
	return entry->Value()->commandLine;
}

// releaseConfigForProject — configfileregistrybuilder.go:343. Removes
// the project from the config entry. Once no projects or files are
// associated with the config entry, it will be removed on the next
// call to `cleanup`.
void configFileRegistryBuilder::releaseConfigForProject(
    const tspath::Path& configFilePath, const ID& projectID) {
	auto res = configs->Load(configFilePath);
	if (res.second) {
		auto& entry = res.first;
		entry->ChangeIf(
		    [&](configFileEntry* config) {
			    return config->retainingProjects.count(projectID) !=
			           0;
		    },
		    [&](configFileEntry* config) {
			    config->retainingProjects.erase(projectID);
		    });
	}
}

// retainConfigForProject — configfileregistrybuilder.go:357.
void configFileRegistryBuilder::retainConfigForProject(
    const tspath::Path& configFilePath, const ID& projectID) {
	auto res = configs->Load(configFilePath);
	if (res.second) {
		auto& entry = res.first;
		entry->ChangeIf(
		    [&](configFileEntry* config) {
			    return config->retainingProjects.count(projectID) ==
			           0;
		    },
		    [&](configFileEntry* config) {
			    config->retainingProjects.emplace(projectID,
			                                      std::monostate{});
		    });
	}
}

// didCloseFile — configfileregistrybuilder.go:376. Removes the open
// file from the config entry. Once no projects or files are
// associated with the config entry, it will be removed on the next
// call to `cleanup`.
void configFileRegistryBuilder::didCloseFile(const tspath::Path& path) {
	if (tspath::isDynamicFileName(path)) {
		return;
	}
	configFileNames->Delete(path);
	configs->Range(
	    [&](const std::shared_ptr<
	        dirty::SyncMapEntry<tspath::Path, configFileEntry*>>&
	            entry) {
		    entry->ChangeIf(
		        [&](configFileEntry* config) {
			        return config->retainingOpenFiles.count(path) !=
			               0;
		        },
		        [&](configFileEntry* config) {
			        config->retainingOpenFiles.erase(path);
		        });
		    return true;
	    });
}

// DidChangeCustomConfigFileName — configfileregistrybuilder.go:404.
bool configFileRegistryBuilder::DidChangeCustomConfigFileName(
    logging::LogTree* logger) {
	if (!customConfigFileNameChanged) {
		return false;
	}
	configFileNames->Clear();
	return true;
}

// invalidateCache — configfileregistrybuilder.go:413.
changeFileResult
configFileRegistryBuilder::invalidateCache(logging::LogTree* logger) {
	std::unordered_map<ID, std::monostate>* affectedProjects = nullptr;
	std::unordered_map<tspath::Path, std::monostate>* affectedFiles =
	    nullptr;

	logging::log(logger, "Too many files changed; marking all configs for "
	            "reload");
	configFileNames->Range([&, this](
	    dirty::MapEntry<tspath::Path, project::configFileNames*>* entry) {
		    if (affectedFiles == nullptr) {
			    affectedFiles =
			        new std::unordered_map<tspath::Path,
			                               std::monostate>();
		    }
		    affectedFiles->emplace(entry->Key(), std::monostate{});
		    return true;
	    });
	configFileNames->Clear();

	configs->Range(
	    [&](const std::shared_ptr<
	        dirty::SyncMapEntry<tspath::Path, configFileEntry*>>&
	            entry) {
		    entry->Change([&](configFileEntry* value) {
			    affectedProjects = tsc::CopyMapInto(
			        affectedProjects, value->retainingProjects);
			    if (value->pendingReload != PendingReloadFull) {
				    auto textRes = fs->ReadFile(value->fileName);
				    auto& text = textRes.first;
				    auto ok = textRes.second;
				    if (!ok || value->commandLine == nullptr ||
				        text != value->commandLine->ConfigFile
				                    ->SourceFile->Text()) {
					    value->pendingReload = PendingReloadFull;
				    } else {
					    value->pendingReload =
					        PendingReloadFileNames;
				    }
			    }
		    });
		    return true;
	    });

	changeFileResult result;
	result.affectedProjects = affectedProjects;
	result.affectedFiles = affectedFiles;
	return result;
}

// DidChangeFiles — configfileregistrybuilder.go:453.
changeFileResult configFileRegistryBuilder::DidChangeFiles(
    const FileChangeSummary& summary, logging::LogTree* logger) {
	if (summary.InvalidateAll) {
		return invalidateCache(logger);
	}
	std::unordered_map<ID, std::monostate>* affectedProjects = nullptr;
	std::unordered_map<tspath::Path, std::monostate>* affectedFiles =
	    nullptr;
	bool shouldInvalidateCache = false;

	logging::log(logger, "Summarizing file changes");
	bool hasExcessiveChanges =
	    summary.HasExcessiveWatchEvents() &&
	    summary.IncludesWatchChangeOutsideNodeModules;
	std::unordered_map<tspath::Path, std::string> createdFiles;
	std::unordered_map<tspath::Path, std::string> deletedFiles;
	std::unordered_set<tspath::Path> createdOrDeletedConfigFiles;
	std::unordered_set<tspath::Path> createdOrChangedOrDeletedFiles;
	for (auto& uri : summary.Changed.Keys()) {
		if (tspath::containsIgnoredPath(uri)) {
			continue;
		}
		auto fileName = lsp::lsproto::documentUriFileName(uri);
		auto path = fs->toPath(fileName);
		auto baseName = tspath::getBaseFileName(path);
		if (isConfigBaseName(std::string{baseName})) {
			createdOrDeletedConfigFiles.insert(path);
		}
		createdOrChangedOrDeletedFiles.insert(path);
	}
	for (auto& uri : summary.Deleted.Keys()) {
		if (tspath::containsIgnoredPath(uri)) {
			continue;
		}
		auto fileName = lsp::lsproto::documentUriFileName(uri);
		auto path = fs->toPath(fileName);
		deletedFiles.emplace(path, fileName);
		auto baseName = tspath::getBaseFileName(path);
		if (isConfigBaseName(std::string{baseName})) {
			createdOrDeletedConfigFiles.insert(path);
		}
		createdOrChangedOrDeletedFiles.insert(path);
	}
	for (auto& uri : summary.Created.Keys()) {
		if (tspath::containsIgnoredPath(uri)) {
			continue;
		}
		auto fileName = lsp::lsproto::documentUriFileName(uri);
		auto path = fs->toPath(fileName);
		createdFiles.emplace(path, fileName);
		auto baseName = tspath::getBaseFileName(path);
		if (isConfigBaseName(std::string{baseName})) {
			createdOrDeletedConfigFiles.insert(path);
		}
		createdOrChangedOrDeletedFiles.insert(path);
	}

	// Handle closed files - this ranges over config entries and could be
	// combined with the file change handling, but a separate loop is
	// simpler and a snapshot change with both closing and watch changes
	// seems rare.
	for (auto& uri : summary.Closed.Keys()) {
		auto fileName = lsp::lsproto::documentUriFileName(uri);
		auto path = fs->toPath(fileName);
		didCloseFile(path);
	}

	// Handle changes to stored config files and their content mapper
	// package manifests.
	logging::log(logger, "Checking if any changed files are configuration files");
	for (auto& path : createdOrChangedOrDeletedFiles) {
		auto res = configs->Load(path);
		if (res.second) {
			if (hasExcessiveChanges) {
				return invalidateCache(logger);
			}
			if (auto* res2 = handleConfigChange(res.first, logger)) {
				affectedProjects = tsc::CopyMapInto(
				    affectedProjects, *res2);
			}
			for (auto& [extendingConfigPath, _] :
			     res.first->Value()->retainingConfigs) {
				auto extRes = configs->Load(extendingConfigPath);
				if (extRes.second) {
					if (auto* res3 = handleConfigChange(
					        extRes.first, logger)) {
						affectedProjects = tsc::CopyMapInto(
						    affectedProjects, *res3);
					}
				}
			}
			// This was a config file, so assume it's not also a root file
			createdFiles.erase(path);
		} else if (tspath::getBaseFileName(path) ==
		           "package.json") {
			bool manifestChanged = false;
			configs->Range(
			    [&](const std::shared_ptr<dirty::SyncMapEntry<
			            tspath::Path, configFileEntry*>>& entry) {
				    if (contentMapperManifestPath(
				            entry->Value()->commandLine, fs->toPath,
				            path)) {
					    if (auto* res4 = handleConfigChange(
					            entry, logger)) {
						    affectedProjects = tsc::CopyMapInto(
						        affectedProjects, *res4);
					    }
					    manifestChanged = true;
				    }
				    return true;
			    });
			if (manifestChanged) {
				invalidateContentMappers();
			}
		}
	}

	// Handle created/deleted files named "tsconfig.json" or
	// "jsconfig.json"
	for (auto& path : createdOrDeletedConfigFiles) {
		if (hasExcessiveChanges) {
			return invalidateCache(logger);
		}
		auto directoryPath = tspath::getDirectoryPath(std::string{path});
		configFileNames->Range([&, this](
		    dirty::MapEntry<tspath::Path,
		                     project::configFileNames*>* entry) {
			    if (tspath::pathContainsPath(tspath::Path{directoryPath},
			                               entry->Key())) {
				    if (affectedFiles == nullptr) {
					    affectedFiles =
					        new std::unordered_map<tspath::Path,
					                               std::monostate>();
				    }
				    affectedFiles->emplace(entry->Key(),
				                           std::monostate{});
				    entry->Delete();
			    }
			    return true;
		    });
	}

	// Handle deletions of wildcard-included root files
	for (auto& deletedFile : deletedFiles) {
		auto path = deletedFile.first;
		auto fileName = deletedFile.second;
		configs->Range(
		    [&, this](const std::shared_ptr<dirty::SyncMapEntry<
		            tspath::Path, configFileEntry*>>& entry) {
			    entry->ChangeIf(
			        [&](configFileEntry* config) {
				        if (config->pendingReload !=
				                PendingReloadNone ||
				            config->commandLine == nullptr) {
					        return false;
				        }
				        if (config->commandLine->FileNamesByPath()
				                ->count(path) != 0) {
					        // If the file is included in FileNames()
					        // but not matched by literal "files", it
					        // must be included via wildcard, which
					        // means a reload of filenames will remove
					        // it from the list. (Files explicitly
					        // specified in "files" are always
					        // included in the ParsedCommandLine,
					        // triggering a missing root file error
					        // during program construction.)
					        return config->commandLine
					                   ->GetMatchedFileSpec(
					                       fileName)
					                   .empty();
				        }
				        return false;
			        },
			        [&](configFileEntry* config) {
				        config->pendingReload =
				            PendingReloadFileNames;
				        if (affectedProjects == nullptr) {
					        affectedProjects =
					            new std::unordered_map<
					                ID, std::monostate>();
				        }
				        affectedProjects->insert(
				            config->retainingProjects.begin(),
				            config->retainingProjects.end());
				        logging::logf(logger, "Root files for config %s changed",
				                     {entry->Key()});
				        shouldInvalidateCache = hasExcessiveChanges;
			        });
			    return !shouldInvalidateCache;
		    });
		if (shouldInvalidateCache) {
			return invalidateCache(logger);
		}
	}

	// Handle possible root file creation
	if (!createdFiles.empty()) {
		configs->Range(
		    [&](const std::shared_ptr<dirty::SyncMapEntry<
		            tspath::Path, configFileEntry*>>& entry) {
			    entry->ChangeIf(
			        [&](configFileEntry* config) {
				        if (config->commandLine == nullptr ||
				            config->rootFilesWatch == nullptr ||
				            config->pendingReload !=
				                PendingReloadNone) {
					        return false;
				        }
				        logging::logf(logger, "Checking if any of %d created files "
				                     "match root files for config %s",
				                     {int(createdFiles.size()),
				                      entry->Key()});
				        for (auto& [createdPath, createdFileName] :
				             createdFiles) {
					        if (config->commandLine
					                ->PossiblyMatchesFileName(
					                    createdFileName)) {
						        return true;
					        }
					        if (config->commandLine
					                ->PossiblyMatchesDirectoryName(
					                    createdPath) &&
					            fs->DirectoryExists(createdFileName)) {
						        // If we got a creation event for a
						        // directory, it's probably a symlink.
						        // We don't need to test realpath here;
						        // this is enough confidence to trigger
						        // a filename reload.
						        return true;
					        }
				        }
				        return false;
			        },
			        [&](configFileEntry* config) {
				        config->pendingReload =
				            PendingReloadFileNames;
				        if (affectedProjects == nullptr) {
					        affectedProjects =
					            new std::unordered_map<
					                ID, std::monostate>();
				        }
				        affectedProjects->insert(
				            config->retainingProjects.begin(),
				            config->retainingProjects.end());
				        logging::logf(logger, "Root files for config %s changed",
				                     {entry->Key()});
				        shouldInvalidateCache = hasExcessiveChanges;
			        });
			    return !shouldInvalidateCache;
		    });
		if (shouldInvalidateCache) {
			return invalidateCache(logger);
		}
	}

	changeFileResult result;
	result.affectedProjects = affectedProjects;
	result.affectedFiles = affectedFiles;
	return result;
}

// handleConfigChange — configfileregistrybuilder.go:642.
std::unordered_map<ID, std::monostate>*
configFileRegistryBuilder::handleConfigChange(
    const std::shared_ptr<
        dirty::SyncMapEntry<tspath::Path, configFileEntry*>>& entry,
    logging::LogTree* logger) {
	std::unordered_map<ID, std::monostate>* affectedProjects = nullptr;
	bool changed = entry->ChangeIf(
	    [](configFileEntry* config) {
		    return config->pendingReload != PendingReloadFull;
	    },
	    [](configFileEntry* config) {
		    config->pendingReload = PendingReloadFull;
	    });
	if (changed) {
		logging::logf(logger, "Config file %s changed", {entry->Key()});
		affectedProjects =
		    new std::unordered_map<ID, std::monostate>(
		        entry->Value()->retainingProjects);
	}
	return affectedProjects;
}

// contentMapperManifestPath — configfileregistrybuilder.go:656.
bool contentMapperManifestPath(
    tsoptions::ParsedCommandLine* commandLine,
    const std::function<tspath::Path(const std::string&)>& toPath,
    const tspath::Path& path) {
	if (commandLine == nullptr) {
		return false;
	}
	for (auto* mapper : commandLine->ContentMappers()) {
		if (!mapper->Definition.Package.empty() &&
		    mapper->ContributionID.empty() &&
		    !mapper->PackageDirectory.empty() &&
		    toPath(tspath::combinePaths(mapper->PackageDirectory,
		                                {"package.json"})) == path) {
			return true;
		}
	}
	return false;
}

// computeConfigFileName — configfileregistrybuilder.go:669.
std::string configFileRegistryBuilder::computeConfigFileName(
    const std::string& fileName, bool skipSearchInDirectoryOfFile,
    logging::LogTree* logger) {
	auto searchPath = tspath::getDirectoryPath(fileName);
	// Prefer custom config file if provided; search ancestors with
	// correct skip behavior.
	if (!customConfigFileName.empty()) {
		bool skip = skipSearchInDirectoryOfFile;
		auto res = tspath::forEachAncestorDirectory<std::string>(
		    searchPath, [&](std::string_view directory) {
			    if (!skip) {
				    auto customPath = tspath::combinePaths(
				        directory, {customConfigFileName});
				    if (fs->FileExists(customPath)) {
					    return std::pair{customPath, true};
				    }
			    }
			    if (directory.size() >= 13 &&
			        directory.compare(directory.size() - 13,
			                          std::string::npos,
			                          "/node_modules") == 0) {
				    return std::pair{std::string{}, true};
			    }
			    skip = false;
			    return std::pair{std::string{}, false};
		    });
		if (!res.first.empty()) {
			logging::logf(logger, 
			    "computeConfigFileName:: File: %s:: Result: %s",
			    {fileName, res.first});
			return res.first;
		}
	}

	// When searching for ancestor of a config file, determine which
	// config types to skip in the starting directory. This matches
	// TSServer's forEachConfigFileLocation behavior:
	// - For ancestor of tsconfig.json: skip tsconfig.json but still
	//   check jsconfig.json
	// - For ancestor of jsconfig.json: skip both tsconfig.json and
	//   jsconfig.json
	bool skipTsconfig = skipSearchInDirectoryOfFile;
	bool skipJsconfig =
	    skipSearchInDirectoryOfFile &&
	    !(fileName.size() >= 14 &&
	      fileName.compare(fileName.size() - 14, std::string::npos,
	                       "/tsconfig.json") == 0);
	auto res = tspath::forEachAncestorDirectory<std::string>(
	    searchPath, [&](std::string_view directory) {
		    if (!skipTsconfig) {
			    auto tsconfigPath =
			        tspath::combinePaths(directory, {"tsconfig.json"});
			    if (fs->FileExists(tsconfigPath)) {
				    return std::pair{tsconfigPath, true};
			    }
		    }
		    if (!skipJsconfig) {
			    auto jsconfigPath =
			        tspath::combinePaths(directory, {"jsconfig.json"});
			    if (fs->FileExists(jsconfigPath)) {
				    return std::pair{jsconfigPath, true};
			    }
		    }
		    if (directory.size() >= 13 &&
		        directory.compare(directory.size() - 13,
		                          std::string::npos,
		                          "/node_modules") == 0) {
			    return std::pair{std::string{}, true};
		    }
		    skipTsconfig = false;
		    skipJsconfig = false;
		    return std::pair{std::string{}, false};
	    });
	logging::logf(logger, "computeConfigFileName:: File: %s:: Result: %s",
	             {fileName, res.first});
	return res.first;
}

// getConfigFileNameForFile — configfileregistrybuilder.go:722.
std::string configFileRegistryBuilder::getConfigFileNameForFile(
    const std::string& fileName, const tspath::Path& path,
    logging::LogTree* logger) {
	if (tspath::isDynamicFileName(fileName)) {
		return "";
	}

	auto res = configFileNames->Get(path);
	if (res.second) {
		return res.first->Value()->nearestConfigFileName;
	}

	auto configName = computeConfigFileName(fileName, false, logger);
	if (isOpenFile(path)) {
		configFileNames->Add(path, new project::configFileNames{configName});
	}
	return configName;
}

// forEachConfigFileNameFor — configfileregistrybuilder.go:740.
void configFileRegistryBuilder::forEachConfigFileNameFor(
    const tspath::Path& path,
    const std::function<void(const std::string&)>& cb) {
	if (tspath::isDynamicFileName(std::string{path})) {
		return;
	}

	auto res = configFileNames->Get(path);
	if (res.second) {
		auto entry = res.first;
		auto configFileName = entry->Value()->nearestConfigFileName;
		while (!configFileName.empty()) {
			cb(configFileName);
			auto it =
			    entry->Value()->ancestors.find(configFileName);
			if (it != entry->Value()->ancestors.end()) {
				configFileName = it->second;
			} else {
				return;
			}
		}
	}
}

// getAncestorConfigFileName — configfileregistrybuilder.go:758.
std::string configFileRegistryBuilder::getAncestorConfigFileName(
    const std::string& fileName, const tspath::Path& path,
    const std::string& configFileName, logging::LogTree* logger) {
	if (tspath::isDynamicFileName(fileName)) {
		return "";
	}

	auto res = configFileNames->Get(path);
	if (!res.second) {
		return "";
	}
	auto entry = res.first;

	auto it = entry->Value()->ancestors.find(configFileName);
	if (it != entry->Value()->ancestors.end()) {
		return it->second;
	}

	// Look for config in parent folders of config file
	auto result = computeConfigFileName(configFileName, true, logger);

	if (isOpenFile(path)) {
		entry->Change([&](project::configFileNames* value) {
			value->ancestors[configFileName] = result;
		});
	}
	return result;
}

// GetExtendedConfig — configfileregistrybuilder.go:797. Implements
// tsoptions.ExtendedConfigCache.
tsoptions::ExtendedConfigCacheEntry*
configFileRegistryBuilder::GetExtendedConfig(
    std::string_view fileName, const tspath::Path& path,
    const std::vector<tspath::Path>& resolutionStack,
    tsoptions::ParseConfigHost* host) {
	std::string content;
	auto* fh = fs->GetFileByPath(std::string{fileName}, path);
	if (fh != nullptr) {
		content = fh->Content();
	}

	ExtendedConfigParseArgs args;
	args.FileName = std::string{fileName};
	args.Content = content;
	args.FS = fs->source;
	args.ResolutionStack = resolutionStack;
	args.Host = host;
	args.Cache = this;
	return extendedConfigCache
	    ->LoadAndAcquire(path, snapshotID, args)
	    ->ExtendedConfigCacheEntry;
}

// Cleanup — configfileregistrybuilder.go:814.
void configFileRegistryBuilder::Cleanup() {
	bool changed = false;
	configs->Range(
	    [&](const std::shared_ptr<
	        dirty::SyncMapEntry<tspath::Path, configFileEntry*>>&
	            entry) {
		    entry->DeleteIf([&](configFileEntry* value) {
			    bool shouldDelete =
			        value->retainingProjects.empty() &&
			        value->retainingOpenFiles.empty() &&
			        value->retainingConfigs.empty();
			    changed = changed || shouldDelete;
			    return shouldDelete;
		    });
		    return true;
	    });
	if (changed) {
		invalidateContentMappers();
	}
}

} // namespace tsc::project
