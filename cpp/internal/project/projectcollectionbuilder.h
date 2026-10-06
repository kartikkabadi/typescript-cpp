// projectcollectionbuilder.go — ProjectCollectionBuilder +
// searchNode/searchNodeKey/searchResult.
//
// projectcollectionbuilder.h
#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/collections/collections.h"
#include "internal/contentmapper/contentmapper.h"
#include "internal/core/context.h"
#include "internal/core/utilities.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/project/client.h"
#include "internal/project/configfileregistrybuilder.h"
#include "internal/project/dirty/dirty.h"
#include "internal/project/extendedconfigcache.h"
#include "internal/project/filechange.h"
#include "internal/project/overlayfs.h"
#include "internal/project/parsecache.h"
#include "internal/project/projectcollection.h"
#include "internal/project/sessiontypes.h"
#include "internal/project/snapshotfs.h"
#include "internal/tspath/tspath.h"

namespace tsc::project {

struct APISnapshotRequest;
struct ATAStateChange;
struct ProjectTreeRequest;

// searchNode — projectcollectionbuilder.go:1044.
struct searchNode {
	std::string configFileName;
	projectLoadKind loadKind;
	logging::LogTree* logger = nullptr;
};

// searchNodeKey — projectcollectionbuilder.go:1050.
struct searchNodeKey {
	std::string configFileName;
	projectLoadKind loadKind;

	bool operator==(const searchNodeKey&) const = default;
};

// searchResult — projectcollectionbuilder.go:1055.
struct searchResult {
	std::shared_ptr<dirty::SyncMapEntry<ConfiguredProjectID, Project*>>
	    project;
	collections::Set<tspath::Path> retain;
};

// ProjectCollectionBuilder — projectcollectionbuilder.go:33.
struct ProjectCollectionBuilder {
	SessionOptions* sessionOptions = nullptr;
	ParseCache* parseCache = nullptr;
	ContentMappedParseCache* contentMappedParseCache = nullptr;
	ExtendedConfigCache* extendedConfigCache = nullptr;
	contentmapper::Host* contentMapperHost = nullptr;
	std::function<tspath::Path(const std::string&)> toPath;

	gostd::Context ctx;
	snapshotFSBuilder* fs = nullptr;
	std::unordered_map<tspath::Path, Overlay*> overlays;
	ProjectCollection* base = nullptr;
	CompilerOptions* compilerOptionsForInferredProjects = nullptr;
	std::vector<contentmapper::Mapper*> inferredContentMappers;
	std::vector<std::string> inferredContentMapperExtensions;
	configFileRegistryBuilder* configFileRegistryBuilder_ = nullptr;

	Client* client = nullptr; // optional; used for project loading
	                          // notifications

	uint64_t newSnapshotID = 0;
	bool programStructureChanged = false;
	bool defaultProjectsInvalidated = false;
	bool openFilesChanged = false;

	std::unordered_map<tspath::Path, ID> fileDefaultProjects;
	dirty::SyncMap<ConfiguredProjectID, Project*>* configuredProjects =
	    nullptr;
	dirty::SyncMap<SyntheticProjectID, Project*>* syntheticProjects =
	    nullptr;
	dirty::Box<Project*>* inferredProject = nullptr;
	std::vector<Project*> createdPrograms;

	APIState apiState;

	// isOpenFile — projectcollectionbuilder.go:106.
	bool isOpenFile(const tspath::Path& path) const {
		return overlays.count(path) != 0;
	}

	// Finalize — projectcollectionbuilder.go:109.
	std::pair<ProjectCollection*, ConfigFileRegistry*>
	Finalize(logging::LogTree* logger);

	// forEachProject — projectcollectionbuilder.go:167.
	void forEachProject(
	    const std::function<bool(dirty::IValue<Project*>*)>& fn);

	// HandleAPIRequest — projectcollectionbuilder.go:183.
	gostd::Error HandleAPIRequest(APISnapshotRequest* apiRequest,
	                              logging::LogTree* logger);

	// nextSyntheticProjectID — projectcollectionbuilder.go:343.
	SyntheticProjectID nextSyntheticProjectID();

	// DidChangeFiles — projectcollectionbuilder.go:351.
	void DidChangeFiles(const FileChangeSummary& summary,
	                    logging::LogTree* logger);

	// refreshContentMapperProjectForChanges —
	// projectcollectionbuilder.go:429.
	void refreshContentMapperProjectForChanges(
	    dirty::IValue<Project*>* entry,
	    const std::vector<tspath::Path>& paths, bool refreshAll,
	    logging::LogTree* logger);

	// cleanupConfiguredProjects — projectcollectionbuilder.go:457.
	void cleanupConfiguredProjects(
	    collections::Set<tspath::Path>* retain,
	    logging::LogTree* logger);

	// cleanupAllConfiguredProjects — projectcollectionbuilder.go:569.
	void cleanupAllConfiguredProjects(logging::LogTree* logger);

	// collectInferredProjectRoots — projectcollectionbuilder.go:588.
	std::vector<std::string> collectInferredProjectRoots();

	// appendAPIOpenedInferredRoots — projectcollectionbuilder.go:599.
	std::vector<std::string> appendAPIOpenedInferredRoots(
	    std::vector<std::string> inferredProjectFiles);

	// cleanupInferredProject — projectcollectionbuilder.go:611.
	void cleanupInferredProject(logging::LogTree* logger);

	// DidChangeContentMapperContributions —
	// projectcollectionbuilder.go:615.
	void DidChangeContentMapperContributions(logging::LogTree* logger);

	// ensureInferredProjectIncludesClosedFile —
	// projectcollectionbuilder.go:622.
	void ensureInferredProjectIncludesClosedFile(
	    const std::string& fileName, logging::LogTree* logger);

	// DidRequestFile — projectcollectionbuilder.go:633.
	void DidRequestFile(const lsp::lsproto::DocumentUri& uri,
	                    bool configuredProjectsOnly,
	                    logging::LogTree* logger);

	// didRequestFile — projectcollectionbuilder.go:640.
	void didRequestFile(const std::string& fileName,
	                    const tspath::Path& path,
	                    bool configuredProjectsOnly,
	                    logging::LogTree* logger);

	// DidRequestProject — projectcollectionbuilder.go:704.
	void DidRequestProject(const ID& projectID,
	                       logging::LogTree* logger);

	// DidRequestProjectTrees — projectcollectionbuilder.go:730.
	void DidRequestProjectTrees(
	    ProjectTreeRequest* projectTreeRequest,
	    logging::LogTree* logger);

	// ensureProjectTree — projectcollectionbuilder.go:764.
	void ensureProjectTree(
	    workGroup* wg,
	    const std::shared_ptr<
	        dirty::SyncMapEntry<ConfiguredProjectID, Project*>>& entry,
	    ProjectTreeRequest* projectTreeRequest,
	    collections::SyncSet<ConfiguredProjectID>* seenProjects,
	    logging::LogTree* logger);

	// DidUpdateATAState — projectcollectionbuilder.go:821.
	void DidUpdateATAState(
	    const std::unordered_map<ID, ATAStateChange*>& ataChanges,
	    logging::LogTree* logger);

	// DidChangeCustomConfigFileName — projectcollectionbuilder.go:872.
	void DidChangeCustomConfigFileName(logging::LogTree* logger);

	// DidChangeUserPreferences — projectcollectionbuilder.go:883.
	void DidChangeUserPreferences(
	    const ls::lsutil::UserPreferences& oldPreferences,
	    const ls::lsutil::UserPreferences& newPreferences,
	    logging::LogTree* logger);

	// markProjectsAffectedByConfigChanges —
	// projectcollectionbuilder.go:896.
	bool markProjectsAffectedByConfigChanges(
	    const changeFileResult& configChangeResult,
	    logging::LogTree* logger);

	// findDefaultProject — projectcollectionbuilder.go:934.
	// Returns a shared_ptr so a freshly-Loaded SyncMapEntry stays
	// alive in the caller (base-entry Loads mint new handles).
	std::shared_ptr<dirty::IValue<Project*>> findDefaultProject(
	    const std::string& fileName, const tspath::Path& path);

	// findDefaultConfiguredProject — projectcollectionbuilder.go:952.
	std::shared_ptr<
	    dirty::SyncMapEntry<ConfiguredProjectID, Project*>>
	findDefaultConfiguredProject(const std::string& fileName,
	                             const tspath::Path& path);

	// ensureConfiguredProjectAndAncestorsForFile —
	// projectcollectionbuilder.go:982.
	searchResult ensureConfiguredProjectAndAncestorsForFile(
	    const std::string& fileName, const tspath::Path& path,
	    logging::LogTree* logger);

	// createAncestorTree — projectcollectionbuilder.go:990.
	void createAncestorTree(const std::string& fileName,
	                        const tspath::Path& path,
	                        searchResult* openResult,
	                        logging::LogTree* logger);

	// findOrCreateDefaultConfiguredProjectWorker —
	// projectcollectionbuilder.go:1060.
	searchResult findOrCreateDefaultConfiguredProjectWorker(
	    const std::string& fileName, const tspath::Path& path,
	    const std::string& configFileName, projectLoadKind loadKind,
	    collections::SyncSet<searchNodeKey>* visited,
	    searchResult* fallback, logging::LogTree* logger);

	// findOrCreateDefaultConfiguredProjectForFile —
	// projectcollectionbuilder.go:1218.
	searchResult findOrCreateDefaultConfiguredProjectForFile(
	    const std::string& fileName, const tspath::Path& path,
	    projectLoadKind loadKind, logging::LogTree* logger);

	// findOrCreateProject — projectcollectionbuilder.go:1257.
	std::shared_ptr<
	    dirty::SyncMapEntry<ConfiguredProjectID, Project*>>
	findOrCreateProject(const std::string& configFileName,
	                    const tspath::Path& configFilePath,
	                    projectLoadKind loadKind,
	                    logging::LogTree* logger);

	// updateInferredProjectRoots — projectcollectionbuilder.go:1269.
	bool updateInferredProjectRoots(
	    const std::vector<std::string>& rootFileNames,
	    logging::LogTree* logger);

	// updateOrCreateSyntheticProject —
	// projectcollectionbuilder.go:1280.
	std::shared_ptr<
	    dirty::SyncMapEntry<SyntheticProjectID, Project*>>
	updateOrCreateSyntheticProject(
	    const SyntheticProjectID& projectID,
	    const std::vector<std::string>& rootFileNames,
	    CompilerOptions* compilerOptions,
	    const std::vector<ProjectReference*>& projectReferences,
	    const std::vector<Diagnostic*>& configFileParsingDiagnostics,
	    ModuleResolverFactory* moduleResolverFactory,
	    uint64_t moduleResolverID,
	    const std::vector<contentmapper::Mapper*>& contentMappers,
	    logging::LogTree* logger);

	// updateInferredProject — projectcollectionbuilder.go:1333.
	bool updateInferredProject(
	    std::vector<std::string> rootFileNames,
	    CompilerOptions* compilerOptions,
	    const std::vector<ProjectReference*>& projectReferences,
	    const std::vector<Diagnostic*>& configFileParsingDiagnostics,
	    const std::vector<contentmapper::Mapper*>& contentMappers,
	    logging::LogTree* logger);

	// deleteInferredProject — projectcollectionbuilder.go:1346.
	bool deleteInferredProject(logging::LogTree* logger);

	// updateOrCreateInferredProject — projectcollectionbuilder.go:1367.
	bool updateOrCreateInferredProject(
	    std::vector<std::string> rootFileNames,
	    CompilerOptions* compilerOptions,
	    const std::vector<ProjectReference*>& projectReferences,
	    const std::vector<Diagnostic*>& configFileParsingDiagnostics,
	    const std::vector<contentmapper::Mapper*>& contentMappers,
	    logging::LogTree* logger);

	// isSupportedInInferredProject — projectcollectionbuilder.go:1420.
	bool isSupportedInInferredProject(const std::string& fileName);

	// updateProgram — projectcollectionbuilder.go:1432.
	bool updateProgram(dirty::IValue<Project*>* entry,
	                   logging::LogTree* logger);

	// markFilesChanged — projectcollectionbuilder.go:1537.
	void markFilesChanged(dirty::IValue<Project*>* entry,
	                      const std::vector<tspath::Path>& paths,
	                      lsp::lsproto::FileChangeType changeType,
	                      logging::LogTree* logger);

	// deleteProject — projectcollectionbuilder.go:1590.
	void deleteProject(dirty::IValue<Project*>* project,
	                   logging::LogTree* logger);

	// releaseDroppedProjectReferences — projectcollectionbuilder.go:1612.
	void releaseDroppedProjectReferences(
	    compiler::SimpleProgram* oldProgram,
	    compiler::SimpleProgram* newProgram, const ID& projectID);
};

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
    contentmapper::Host* contentMapperHost, Client* client);

// logChangeFileResult — projectcollectionbuilder.go:579.
void logChangeFileResult(const changeFileResult& result,
                         logging::LogTree* logger);

// projectReferencesEqual — projectcollectionbuilder.go:1410.
bool projectReferencesEqual(
    const std::vector<ProjectReference*>& a,
    const std::vector<ProjectReference*>& b);

} // namespace tsc::project

// std::hash<searchNodeKey> — enables unordered containers keyed on the
// composite search key (Go map keys need no explicit hash).
template <>
struct std::hash<tsc::project::searchNodeKey> {
	size_t operator()(
	    const tsc::project::searchNodeKey& k) const noexcept {
		return std::hash<std::string>{}(k.configFileName) ^
		       (std::hash<int>{}(static_cast<int>(k.loadKind)) << 1);
	}
};
