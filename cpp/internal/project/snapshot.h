// snapshot.go — Snapshot + request/change types.
//
// snapshot.h
#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/collections/collections.h"
#include "internal/compiler/program.h"
#include "internal/contentmapper/contentmapper.h"
#include "internal/core/context.h"
#include "internal/core/utilities.h"
#include "internal/ls/autoimport/autoimport.h"
#include "internal/ls/lsconv/lsconv.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/ls/ls.h"
#include "internal/project/ata/ata.h"
#include "internal/project/client.h"
#include "internal/project/configfileregistry.h"
#include "internal/project/filechange.h"
#include "internal/project/files.h"
#include "internal/project/overlayfs.h"
#include "internal/project/projectcollection.h"
#include "internal/project/sessiontypes.h"
#include "internal/project/snapshotfs.h"
#include "internal/project/watch.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfsmatch/vfsmatch.h"

namespace tsc::project {

struct Project;
struct SnapshotHost;
struct Snapshot;

// APICreateProgramRequest — snapshot.go:322.
struct APICreateProgramRequest {
	std::vector<std::string> RootFileNames;
	CompilerOptions* CompilerOptions = nullptr;
	std::vector<ProjectReference*> ProjectReferences;
	std::vector<Diagnostic*> ConfigFileParsingDiagnostics;
	ModuleResolverFactory* ModuleResolverFactory = nullptr;
	uint64_t ModuleResolverID = 0;
};

// APIReconfigureProgramRequest — snapshot.go:335.
struct APIReconfigureProgramRequest : APICreateProgramRequest {
	SyntheticProjectID ProgramID;
};

// APISnapshotRequest — snapshot.go:340.
struct APISnapshotRequest {
	collections::Set<std::string>* OpenProjects = nullptr;
	collections::Set<tspath::Path>* CloseProjects = nullptr;
	std::unordered_map<tspath::Path, std::string>* OpenFiles =
	    nullptr;
	collections::Set<tspath::Path>* CloseFiles = nullptr;
	std::vector<APICreateProgramRequest*> CreatePrograms;
	std::vector<APIReconfigureProgramRequest*> ReconfigurePrograms;
	collections::Set<SyntheticProjectID>* RemovePrograms = nullptr;
	collections::Set<ID>* EnsurePrograms = nullptr;
	bool EnsureAllPrograms = false;
	std::unordered_map<tspath::Path, std::string> EnsureFiles;
	vfs::FS* FileSystem = nullptr;
	// ReplaceFileSystem indicates a total filesystem replacement.
	// Layers use per-path file changes instead of invalidating all
	// inherited state.
	bool ReplaceFileSystem = false;
};

// ProjectTreeRequest — snapshot.go:357.
struct ProjectTreeRequest {
	// If null, all project trees need to be loaded, otherwise only
	// those that are referenced.
	collections::Set<tspath::Path>* referencedProjects = nullptr;

	// IsAllProjects — snapshot.go:362.
	bool IsAllProjects() const { return referencedProjects == nullptr; }
	// IsProjectReferenced — snapshot.go:366.
	bool IsProjectReferenced(const tspath::Path& projectID) const {
		return referencedProjects->Has(projectID);
	}
	// Projects — snapshot.go:370.
	std::vector<tspath::Path> Projects() const {
		if (referencedProjects == nullptr) {
			return {};
		}
		return {referencedProjects->Keys().begin(),
		        referencedProjects->Keys().end()};
	}
};

// ResourceRequest — snapshot.go:377.
struct ResourceRequest {
	// Documents are URIs that were requested by the client. The new
	// snapshot should ensure projects for these URIs have loaded
	// programs.
	std::vector<lsp::lsproto::DocumentUri> Documents;
	// ConfiguredProjectDocuments are URIs for which configured
	// projects should be loaded (if
	// disableSolutionSearching/disableReferencedProjectLoad settings
	// allow), but no inferred project should be created if no
	// configured project is found. This is used by cross-project
	// operations like find-all-references.
	std::vector<lsp::lsproto::DocumentUri> ConfiguredProjectDocuments;
	// Update requested Projects — this is used when we want to get LS
	// and from all the Projects the file can be part of.
	std::vector<ID> Projects;
	// Update and ensure project trees that reference the projects.
	// This is used to compute the solution and project tree so that we
	// can find references across all the projects in the solution
	// irrespective of which project is open.
	ProjectTreeRequest* ProjectTree = nullptr;
	// AutoImports is the document URI for which auto imports should be
	// prepared.
	lsp::lsproto::DocumentUri AutoImports;
};

// ATAStateChange — snapshot.go:421. Represents a change to a project's
// ATA state.
struct ATAStateChange {
	// TypingsInfo is the new typings info for the project.
	ata::TypingsInfo* TypingsInfo = nullptr;
	// TypingsFiles is the new list of typing files for the project.
	std::vector<std::string> TypingsFiles;
	// TypingsFilesToWatch is the new list of typing files to watch for
	// changes.
	std::vector<std::string> TypingsFilesToWatch;
	logging::LogTree* Logs = nullptr;
};

// SnapshotChange — snapshot.go:394.
struct SnapshotChange : ResourceRequest {
	UpdateReason reason = UpdateReasonUnknown;
	// fs overrides the session filesystem for this snapshot. It is
	// used by API snapshots that supply their own memory or cache
	// filesystem.
	vfs::FS* fs = nullptr;
	bool fileSystemOverride = false;
	bool replaceFileSystem = false;
	// fileChanges are the changes that have occurred since the last
	// snapshot.
	FileChangeSummary fileChanges;
	// compilerOptionsForInferredProjects is the compiler options to
	// use for inferred projects. It should only be set the value in
	// the next snapshot should be changed. If nil, the value from the
	// previous snapshot will be copied to the new snapshot.
	CompilerOptions* compilerOptionsForInferredProjects = nullptr;
	ContentMapperContributions* contentMapperContributions = nullptr;
	ls::lsutil::UserPreferences* newConfig = nullptr;
	// ataChanges contains ATA-related changes to apply to projects in
	// the new snapshot.
	std::unordered_map<ID, ATAStateChange*> ataChanges;
	APISnapshotRequest* apiRequest = nullptr;
	// cleanFileCache triggers cleaning of cached files not referenced
	// by any open project.
	bool cleanFileCache = false;
};

// Snapshot — snapshot.go:30.
struct Snapshot {
	SnapshotHost* host = nullptr;
	uint64_t id = 0;
	uint64_t parentId = 0;
	std::atomic<int32_t> refCount{0};

	lsconv::Converters* converters = nullptr;

	// Immutable state, cloned between snapshots
	SnapshotFS* fs = nullptr;
	ProjectCollection* ProjectCollection = nullptr;
	project::ConfigFileRegistry* ConfigFileRegistry = nullptr;
	ls::autoimport::Registry* AutoImports = nullptr;
	WatchedFiles<
	    std::unordered_map<tspath::Path, std::string>>*
	    autoImportsWatch = nullptr;
	CompilerOptions* compilerOptionsForInferredProjects = nullptr;
	std::vector<contentmapper::Mapper*>
	    inferredProjectContentMappers;
	std::vector<std::string>
	    inferredProjectContentMapperExtensions;
	ls::lsutil::UserPreferences userPreferences;
	std::once_flag contentMapperWatchStateOnce;
	std::vector<std::string> contentMapperExtensions_;
	collections::Set<tspath::Path>* contentMapperWatchedFiles_ =
	    nullptr;

	logging::LogTree* builderLogs = nullptr;
	gostd::Error apiError;
	// fileSystemOverride indicates that this snapshot was built from a
	// filesystem supplied by an API update rather than the session
	// host filesystem.
	bool fileSystemOverride = false;

	std::vector<Project*> createdPrograms;

	// contentMapperWatchState — snapshot.go:57.
	std::pair<std::vector<std::string>,
	          collections::Set<tspath::Path>*>
	contentMapperWatchState() {
		std::call_once(contentMapperWatchStateOnce, [&] {
			auto* configured =
			    ConfigFileRegistry->contentMappers();
			if (configured != nullptr) {
				contentMapperExtensions_ =
				    configured->extensions;
			}
			contentMapperExtensions_.insert(
			    contentMapperExtensions_.end(),
			    inferredProjectContentMapperExtensions.begin(),
			    inferredProjectContentMapperExtensions.end());
			std::sort(contentMapperExtensions_.begin(),
			          contentMapperExtensions_.end());
			contentMapperExtensions_.erase(
			    std::unique(contentMapperExtensions_.begin(),
			                contentMapperExtensions_.end()),
			    contentMapperExtensions_.end());

			contentMapperWatchedFiles_ =
			    new collections::Set<tspath::Path>();
			for (auto* project : ProjectCollection->Projects()) {
				if (project->contentMapperWatchedFiles !=
				    nullptr) {
					for (auto& path : project
					         ->contentMapperWatchedFiles
					         ->Keys()) {
						contentMapperWatchedFiles_->Add(path);
					}
				}
			}
		});
		return {contentMapperExtensions_,
		        contentMapperWatchedFiles_};
	}

	// overlays — snapshot.go:102.
	std::unordered_map<tspath::Path, Overlay*> overlays() const {
		return fs->fs->Overlays();
	}

	// CreatedPrograms — snapshot.go:106.
	const std::vector<Project*>& CreatedPrograms() const {
		return createdPrograms;
	}

	// resourceRequestForDocument — snapshot.go:110.
	ResourceRequest resourceRequestForDocument(
	    const lsp::lsproto::DocumentUri& uri);

	// processFileChanges — snapshot.go:124.
	FileChangeSummary processFileChanges(
	    snapshotFSBuilder* fs, FileChangeSummary fileChanges,
	    logging::LogTree* logger,
	    ContentMapperContributions* contentMapperContributions,
	    const std::unordered_map<tspath::Path, Overlay*>&
	        previousOverlays,
	    const std::unordered_map<tspath::Path, Overlay*>& overlays);

	// GetDefaultProject — snapshot.go:199.
	Project* GetDefaultProject(const lsp::lsproto::DocumentUri& uri);

	// GetLanguageServiceProjectsContainingFile — snapshot.go:207.
	// Does not consider synthetic projects (ones created by API via
	// createProgram).
	std::vector<ls::Project*>
	GetLanguageServiceProjectsContainingFile(
	    const lsp::lsproto::DocumentUri& uri);

	// GetFile — snapshot.go:216.
	FileHandle* GetFile(const std::string& fileName) {
		return fs->GetFile(fileName);
	}

	// LSPLineMap — snapshot.go:220.
	lsconv::LSPLineMap* LSPLineMap(const std::string& fileName) {
		if (auto* file = fs->GetFile(fileName); file != nullptr) {
			return file->LSPLineMap();
		}
		return nullptr;
	}

	// GetECMALineInfo — snapshot.go:227.
	sourcemap::ECMALineInfo* GetECMALineInfo(
	    const std::string& fileName) {
		if (auto* file = fs->GetFile(fileName); file != nullptr) {
			return file->ECMALineInfo();
		}
		return nullptr;
	}

	// GetPreferences — snapshot.go:234.
	ls::lsutil::UserPreferences GetPreferences(
	    const std::string& activeFile) {
		return userPreferences;
	}

	// UserPreferences — snapshot.go:238.
	ls::lsutil::UserPreferences UserPreferences() const {
		return userPreferences;
	}

	// Converters — snapshot.go:242.
	lsconv::Converters* Converters() const { return converters; }

	// AutoImportRegistry — snapshot.go:246.
	ls::autoimport::Registry* AutoImportRegistry() const {
		return AutoImports;
	}

	// ID — snapshot.go:250.
	uint64_t ID() const { return id; }

	// toPath — snapshot.go:254.
	tspath::Path toPath(const std::string& fileName) const;

	// isOpenFile — snapshot.go:258.
	bool isOpenFile(const std::string& fileName) {
		return overlays().count(toPath(fileName)) != 0;
	}

	// hasOverlayWithin — snapshot.go:263.
	bool hasOverlayWithin(const tspath::Path& path) {
		for (auto& [overlayPath, o] : overlays()) {
			if (tspath::pathContainsPath(path, overlayPath)) {
				return true;
			}
		}
		return false;
	}

	// UseCaseSensitiveFileNames — snapshot.go:271.
	bool UseCaseSensitiveFileNames() const {
		return fs->fs->UseCaseSensitiveFileNames();
	}

	// FileSystem — snapshot.go:276. Returns the filesystem backing
	// this snapshot.
	vfs::FS* FileSystem() const { return fs->fs; }

	// HasFileSystemOverride — snapshot.go:281. Reports whether this
	// snapshot uses an API-supplied filesystem instead of the session
	// host filesystem.
	bool HasFileSystemOverride() const { return fileSystemOverride; }

	// ReadFile — snapshot.go:286.
	std::pair<std::string, bool> ReadFile(const std::string& fileName) {
		auto* handle = GetFile(fileName);
		if (handle == nullptr) {
			return {"", false};
		}
		return {handle->Content(), true};
	}

	// DirectoryExists — snapshot.go:295.
	bool DirectoryExists(const std::string& path) {
		return fs->fs->DirectoryExists(path);
	}

	// FileExists — snapshot.go:299.
	bool FileExists(const std::string& path) {
		return fs->fs->FileExists(path);
	}

	// GetDirectories — snapshot.go:300.
	std::vector<std::string> GetDirectories(
	    const std::string& path) {
		return fs->fs->GetAccessibleEntries(path).directories;
	}

	// ReadDirectory — snapshot.go:305.
	std::vector<std::string> ReadDirectory(
	    const std::string& currentDir, const std::string& path,
	    const std::vector<std::string>& extensions,
	    const std::vector<std::string>& excludes,
	    const std::vector<std::string>& includes, int depth) {
		std::vector<std::string_view> extViews;
		extViews.reserve(extensions.size());
		for (auto& e : extensions) {
			extViews.push_back(e);
		}
		return vfs::vfsmatch::ReadDirectory(
		    fs->fs, currentDir, path, extViews, excludes,
		    includes, depth);
	}

	// FS — snapshot.go:310.
	vfs::FS* FS();

	// GetCurrentDirectory — snapshot.go:314.
	std::string GetCurrentDirectory() const;

	// ContentMapperExtensions — snapshot.go:318.
	std::vector<std::string> ContentMapperExtensions() {
		return contentMapperWatchState().first;
	}

	// Clone — snapshot.go:435.
	Snapshot* Clone(const gostd::Context& ctx,
	                const SnapshotChange& change,
	                std::unordered_map<tspath::Path, Overlay*> overlays,
	                logging::Logger* sessionLogger, Client* client);

	// ref — snapshot.go:754. Increments the snapshot's reference
	// count, preventing it from being disposed until a corresponding
	// Deref is called. The snapshot must still be alive
	// (refCount > 0) when ref is called.
	void ref() {
		if (refCount.fetch_add(1) + 1 <= 1) {
			TSC_UNREACHABLE("snapshot ref on disposed snapshot");
		}
	}

	// tryRef — snapshot.go:761. Attempts to increment the snapshot's
	// reference count. If the snapshot is already disposed
	// (refCount == 0), returns false without modifying the count. On
	// success the caller must eventually call Deref.
	bool tryRef() {
		for (;;) {
			auto rc = refCount.load();
			if (rc <= 0) {
				return false;
			}
			if (refCount.compare_exchange_weak(rc, rc + 1)) {
				return true;
			}
		}
	}

	// Deref — snapshot.go:775. Decrements the snapshot's reference
	// count. When the count reaches zero, the snapshot is disposed and
	// its store-owned resources are released.
	void Deref() {
		auto rc = refCount.fetch_sub(1) - 1;
		if (rc < 0) {
			TSC_UNREACHABLE("snapshot ref count below zero");
		}
		if (rc == 0) {
			dispose();
		}
	}

	// dispose — snapshot.go:787.
	void dispose();
};

// SnapshotLSHost — Go's project.Snapshot satisfies ls.Host structurally;
// C++ needs an explicit adapter for the interface boundary. In Go the
// LanguageService's snapshot field keeps the snapshot reachable for the
// service's lifetime (GC); here the adapter holds a ref so the snapshot
// outlives every LanguageService built on it (the host is leak-tolerant
// like the rest of the snapshot graph, so Deref fires only if it is ever
// destroyed).
struct SnapshotLSHost : ls::Host {
	Snapshot* snapshot = nullptr;
	explicit SnapshotLSHost(Snapshot* s) : snapshot(s) { s->ref(); }
	~SnapshotLSHost() override { snapshot->Deref(); }

	bool UseCaseSensitiveFileNames() override {
		return snapshot->UseCaseSensitiveFileNames();
	}
	std::pair<std::string, bool> ReadFile(
	    const std::string& fileName) override {
		return snapshot->ReadFile(fileName);
	}
	lsconv::Converters* Converters() override {
		return snapshot->Converters();
	}
	ls::lsutil::UserPreferences GetPreferences(
	    const std::string& activeFile) override {
		return snapshot->GetPreferences(activeFile);
	}
	sourcemap::ECMALineInfo* GetECMALineInfo(
	    const std::string& fileName) override {
		return snapshot->GetECMALineInfo(fileName);
	}
	ls::autoimport::Registry* AutoImportRegistry() override {
		return snapshot->AutoImportRegistry();
	}
	std::vector<std::string> ReadDirectory(
	    const std::string& currentDir, const std::string& path,
	    const std::vector<std::string>& extensions,
	    const std::vector<std::string>* excludes,
	    const std::vector<std::string>& includes, int depth) override {
		static const std::vector<std::string> empty;
		return snapshot->ReadDirectory(
		    currentDir, path, extensions,
		    excludes != nullptr ? *excludes : empty,
		    includes, depth);
	}
	std::vector<std::string> GetDirectories(
	    const std::string& path) override {
		return snapshot->GetDirectories(path);
	}
	bool DirectoryExists(const std::string& path) override {
		return snapshot->DirectoryExists(path);
	}
	bool FileExists(const std::string& path) override {
		return snapshot->FileExists(path);
	}
};

// overlayFileHandles — snapshot.go:192.
inline std::unordered_map<tspath::Path, FileHandle*>
overlayFileHandles(
    const std::unordered_map<tspath::Path, Overlay*>& overlays) {
	std::unordered_map<tspath::Path, FileHandle*> files;
	files.reserve(overlays.size());
	for (auto& [path, overlay] : overlays) {
		files[path] = overlay;
	}
	return files;
}

} // namespace tsc::project
