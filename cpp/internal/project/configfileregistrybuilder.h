// configfileregistrybuilder.go — configFileRegistryBuilder.
//
// configfileregistrybuilder.h
#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/project/configfileregistry.h"
#include "internal/project/dirty/dirty.h"
#include "internal/project/extendedconfigcache.h"
#include "internal/project/filechange.h"
#include "internal/project/sessiontypes.h"
#include "internal/project/snapshotfs.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tspath/tspath.h"

namespace tsc::project {

// configFileRegistryBuilder — configfileregistrybuilder.go:27. Tracks
// changes made on top of a previous configFileRegistry, producing a
// new clone with `Finalize()` after all changes have been made.
struct configFileRegistryBuilder : tsoptions::ParseConfigHost,
                                   tsoptions::ExtendedConfigCache {
	bool hasRelativePatternCapability = false;
	sourceFS* fs = nullptr;
	std::function<bool(const tspath::Path&)> isOpenFile;
	project::ExtendedConfigCache* extendedConfigCache = nullptr;
	uint64_t snapshotID = 0;
	SessionOptions* sessionOptions = nullptr;
	std::string customConfigFileName;

	ConfigFileRegistry* base = nullptr;
	dirty::SyncMap<tspath::Path, configFileEntry*>* configs = nullptr;
	dirty::Map<tspath::Path, configFileNames*>* configFileNames =
	    nullptr;
	bool customConfigFileNameChanged = false;
	std::mutex contentMappersMu;
	configuredContentMappers* allConfiguredContentMappers = nullptr;

	// Finalize — configfileregistrybuilder.go:74. Creates a new
	// configFileRegistry based on the changes made in the builder. If
	// no changes were made, returns the original base registry.
	ConfigFileRegistry* Finalize() {
		bool changed = false;
		ConfigFileRegistry* newRegistry = base;
		auto ensureCloned = [&] {
			if (!changed) {
				newRegistry = newRegistry->clone();
				changed = true;
			}
		};

		auto configsRes = configs->Finalize();
		if (configsRes.second) {
			ensureCloned();
			newRegistry->configs = configsRes.first;
			newRegistry->allConfiguredContentMappers =
			    contentMappers();
		}

		auto namesRes = configFileNames->Finalize();
		if (namesRes.second) {
			ensureCloned();
			newRegistry->configFileNames = namesRes.first;
		}

		if (customConfigFileNameChanged) {
			ensureCloned();
			newRegistry->customConfigFileName = customConfigFileName;
		}

		return newRegistry;
	}

	// contentMappers — configfileregistrybuilder.go:103.
	configuredContentMappers* contentMappers() {
		std::lock_guard<std::mutex> lk(contentMappersMu);
		if (allConfiguredContentMappers == nullptr) {
			std::vector<tsoptions::ParsedCommandLine*> commandLines;
			configs->Range(
			    [&](const std::shared_ptr<dirty::SyncMapEntry<
			            tspath::Path, configFileEntry*>>& entry) {
				    if (auto* commandLine =
				            entry->Value()->commandLine) {
					    commandLines.push_back(commandLine);
				    }
				    return true;
			    });
			allConfiguredContentMappers =
			    collectConfiguredContentMappers(commandLines);
		}
		return allConfiguredContentMappers;
	}

	// invalidateContentMappers — configfileregistrybuilder.go:119.
	void invalidateContentMappers() {
		std::lock_guard<std::mutex> lk(contentMappersMu);
		allConfiguredContentMappers = nullptr;
	}

	// findOrAcquireConfigForFile — configfileregistrybuilder.go:125.
	tsoptions::ParsedCommandLine* findOrAcquireConfigForFile(
	    const std::string& configFileName,
	    const tspath::Path& configFilePath,
	    const tspath::Path& filePath, projectLoadKind loadKind,
	    logging::LogTree* logger);

	// reloadIfNeeded — configfileregistrybuilder.go:148. Should only
	// be called from within the Change() method of a dirty map entry.
	bool reloadIfNeeded(configFileEntry* entry,
	                    const std::string& fileName,
	                    const tspath::Path& path,
	                    logging::LogTree* logger);

	// updateExtendingConfigs — configfileregistrybuilder.go:173.
	void updateExtendingConfigs(
	    const tspath::Path& extendingConfigPath,
	    tsoptions::ParsedCommandLine* newCommandLine,
	    tsoptions::ParsedCommandLine* oldCommandLine);

	// updateRootFilesWatch — configfileregistrybuilder.go:217.
	void updateRootFilesWatch(const std::string& fileName,
	                          configFileEntry* entry);

	// acquireConfigForProject — configfileregistrybuilder.go:283.
	tsoptions::ParsedCommandLine* acquireConfigForProject(
	    const std::string& fileName, const tspath::Path& path,
	    Project* project, logging::LogTree* logger);

	// acquireConfigForFile — configfileregistrybuilder.go:313.
	tsoptions::ParsedCommandLine* acquireConfigForFile(
	    const std::string& configFileName,
	    const tspath::Path& configFilePath,
	    const tspath::Path& filePath, logging::LogTree* logger);

	// releaseConfigForProject — configfileregistrybuilder.go:343.
	void releaseConfigForProject(const tspath::Path& configFilePath,
	                             const ID& projectID);

	// retainConfigForProject — configfileregistrybuilder.go:357.
	void retainConfigForProject(const tspath::Path& configFilePath,
	                            const ID& projectID);

	// didCloseFile — configfileregistrybuilder.go:376.
	void didCloseFile(const tspath::Path& path);

	// DidChangeCustomConfigFileName —
	// configfileregistrybuilder.go:404.
	bool DidChangeCustomConfigFileName(logging::LogTree* logger);

	// invalidateCache — configfileregistrybuilder.go:413.
	changeFileResult invalidateCache(logging::LogTree* logger);

	// isConfigBaseName — configfileregistrybuilder.go:448.
	bool isConfigBaseName(const std::string& baseName) const {
		return baseName == "tsconfig.json" ||
		       baseName == "jsconfig.json" ||
		       (!customConfigFileName.empty() &&
		        baseName == customConfigFileName);
	}

	// DidChangeFiles — configfileregistrybuilder.go:453.
	changeFileResult DidChangeFiles(const FileChangeSummary& summary,
	                                logging::LogTree* logger);

	// handleConfigChange — configfileregistrybuilder.go:642.
	std::unordered_map<ID, std::monostate>* handleConfigChange(
	    const std::shared_ptr<
	        dirty::SyncMapEntry<tspath::Path, configFileEntry*>>&
	        entry,
	    logging::LogTree* logger);

	// computeConfigFileName — configfileregistrybuilder.go:669.
	std::string computeConfigFileName(
	    const std::string& fileName, bool skipSearchInDirectoryOfFile,
	    logging::LogTree* logger);

	// getConfigFileNameForFile — configfileregistrybuilder.go:722.
	std::string getConfigFileNameForFile(
	    const std::string& fileName, const tspath::Path& path,
	    logging::LogTree* logger);

	// forEachConfigFileNameFor — configfileregistrybuilder.go:740.
	void forEachConfigFileNameFor(
	    const tspath::Path& path,
	    const std::function<void(const std::string&)>& cb);

	// getAncestorConfigFileName — configfileregistrybuilder.go:758.
	std::string getAncestorConfigFileName(
	    const std::string& fileName, const tspath::Path& path,
	    const std::string& configFileName,
	    logging::LogTree* logger);

	// module::ResolutionHost — the builder's ParseConfigHost
	// implementation forwards all filesystem access to the sourceFS
	// overlay (configfileregistrybuilder.go:787 `c.FS()`).
	bool FileExists(std::string_view path) override {
		return fs->FileExists(std::string{path});
	}
	bool DirectoryExists(std::string_view path) override {
		return fs->DirectoryExists(std::string{path});
	}
	std::optional<std::string>
	ReadFile(std::string_view path) override {
		auto res = fs->ReadFile(std::string{path});
		if (!res.second) {
			return std::nullopt;
		}
		return res.first;
	}
	std::string Realpath(std::string_view path) override {
		return fs->Realpath(std::string{path});
	}
	bool UseCaseSensitiveFileNames() override {
		return fs->UseCaseSensitiveFileNames();
	}
	AccessibleEntries
	GetAccessibleEntries(std::string_view path) override {
		auto e = fs->GetAccessibleEntries(std::string{path});
		return {e.files, e.directories, e.symlinks};
	}

	// GetCurrentDirectory — configfileregistrybuilder.go:792.
	std::string GetCurrentDirectory() override {
		return sessionOptions->CurrentDirectory;
	}

	// GetExtendedConfig — configfileregistrybuilder.go:797.
	tsoptions::ExtendedConfigCacheEntry* GetExtendedConfig(
	    std::string_view fileName, const tspath::Path& path,
	    const std::vector<tspath::Path>& resolutionStack,
	    tsoptions::ParseConfigHost* host) override;

	// Cleanup — configfileregistrybuilder.go:814.
	void Cleanup();
};

// newConfigFileRegistryBuilder — configfileregistrybuilder.go:44.
configFileRegistryBuilder* newConfigFileRegistryBuilder(
    bool hasRelativePatternCapability, snapshotFSBuilder* fs,
    std::function<bool(const tspath::Path&)> isOpenFile,
    ConfigFileRegistry* oldConfigFileRegistry,
    ExtendedConfigCache* extendedConfigCache, uint64_t snapshotID,
    SessionOptions* sessionOptions,
    const std::string& customConfigFileName,
    logging::LogTree* logger);

// contentMapperManifestPath — configfileregistrybuilder.go:656.
bool contentMapperManifestPath(
    tsoptions::ParsedCommandLine* commandLine,
    const std::function<tspath::Path(const std::string&)>& toPath,
    const tspath::Path& path);

} // namespace tsc::project
