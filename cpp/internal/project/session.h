// session.go — Session.
//
// session.h
#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/collections/collections.h"
#include "internal/contentmapper/contentmapper.h"
#include "internal/core/types.h"
#include "internal/gostd/gostd.h"
#include "internal/ls/autoimport/autoimport.h"
#include "internal/ls/lsconv/lsconv.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/project/ata/ata.h"
#include "internal/project/background/background.h"
#include "internal/project/client.h"
#include "internal/project/filechange.h"
#include "internal/project/files.h"
#include "internal/project/autoimport.h"
#include "internal/project/logging/logging.h"
#include "internal/project/overlayfs.h"
#include "internal/project/snapshot.h"
#include "internal/project/snapshothost.h"
#include "internal/project/watch.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfs.h"

namespace tsc::project {
struct Snapshot;
}

namespace tsc::ls {
// LanguageService — owned by the ls slice. dep-stub.
class LanguageService;
// NewLanguageService — languageservice.go:25. dep-stub — owned by ls.
// (project::ID satisfies the Go autoimport.ProjectID interface; the C++
// side passes an interned adapter.)
LanguageService* NewLanguageService(
    autoimport::ProjectID* projectID, compiler::SimpleProgram* program,
    project::Snapshot* host, const std::string& activeFile);
} // namespace tsc::ls

namespace tsc::project {

struct Session;

// idleTimer — time.AfterFunc semantics: Stop() prevents a pending
// fire; once fired the timer is consumed.
struct idleTimer {
	std::atomic<bool> stopped{false};
	template <typename F> void start(std::chrono::nanoseconds d, F fn) {
		std::thread([this, d, fn = std::move(fn)] {
			auto deadline = std::chrono::steady_clock::now() + d;
			while (std::chrono::steady_clock::now() < deadline) {
				if (stopped.load()) {
					return;
				}
				std::this_thread::sleep_for(
				    std::chrono::milliseconds(10));
			}
			if (!stopped.exchange(true)) {
				fn();
			}
		}).detach();
	}
	void Stop() { stopped.store(true); }
};

// Session manages the state of an LSP session. It receives
// textDocument events and requests for LanguageService objects from
// the LSP server and processes them into immutable snapshots as the
// data source for LanguageServices. When Session transitions from one
// snapshot to the next, it diffs them and updates file watchers and
// Automatic Type Acquisition (ATA) state accordingly.
//
// Implements ata.TypingsInstallerHost (NpmExecutor +
// module.ResolutionHost).
struct Session : ata::TypingsInstallerHost {
	SnapshotHost* snapshotHost = nullptr;

	SessionOptions* options = nullptr;
	logging::Logger* logger = nullptr;
	gostd::Context backgroundCtx;
	std::function<tspath::Path(const std::string&)> toPath;
	Client* client = nullptr;
	std::chrono::steady_clock::time_point startTime;
	ata::NpmExecutor* npmExecutor = nullptr;
	overlayFS* fs = nullptr;
	// contentMapperTimings is the cumulative host snapshot at the most
	// recent session snapshot adoption.
	contentmapper::Timings contentMapperTimings;
	std::mutex contentMapperTimingsMu;

	// registeredContentMapperSnapshotID is the ID of the newest
	// snapshot whose registration has been applied. Registration runs
	// from background tasks that may finish out of order, so
	// contentMapperRegistrationMu serializes updates and the snapshot
	// ID keeps a stale task from overwriting a newer snapshot's
	// registration.
	std::vector<std::string> registeredContentMapperExtensions;
	uint64_t registeredContentMapperSnapshotID = 0;
	std::mutex contentMapperRegistrationMu;

	// read-only after initialization
	ls::lsutil::UserPreferences initialUserPreferences;
	// current preferences
	ls::lsutil::UserPreferences workspaceUserPreferences;
	CompilerOptions* compilerOptionsForInferredProjects = nullptr;
	ata::TypingsInstaller* typingsInstaller = nullptr;
	background::Queue* backgroundQueue = nullptr;

	// snapshot is the current immutable state of all projects.
	project::Snapshot* snapshot = nullptr;
	std::shared_mutex snapshotMu;
	std::mutex snapshotUpdateMu;

	// scheduledSnapshotUpdateCancel is the cancelation function for a
	// scheduled snapshot update. Snapshot updates are scheduled and
	// debounced after file closes.
	gostd::CancelFunc scheduledSnapshotUpdateCancel;
	uint64_t scheduledSnapshotUpdateGeneration = 0;
	std::mutex scheduledSnapshotUpdateMu;

	bool pendingUserConfigChanges = false;
	std::mutex configureMu;
	std::mutex userConfigRWMu;

	// pendingFileChanges are accumulated from textDocument/* events
	// delivered by the LSP server through DidOpenFile(),
	// DidChangeFile(), etc. They are applied to the next snapshot
	// update.
	std::vector<FileChange> pendingFileChanges;
	std::mutex pendingFileChangesMu;

	// pendingATAChanges are produced by Automatic Type Acquisition
	// (ATA) installations and applied to the next snapshot update.
	std::unordered_map<ID, ATAStateChange*> pendingATAChanges;
	std::mutex pendingATAChangesMu;

	// diagnosticsRefreshCancel is the cancelation function for a
	// scheduled diagnostics refresh. Diagnostics refreshes are
	// scheduled and debounced after file watch changes and ATA
	// updates.
	gostd::CancelFunc diagnosticsRefreshCancel;
	uint64_t diagnosticsRefreshGeneration = 0;
	std::mutex diagnosticsRefreshMu;

	// warmAutoImportCancel is the cancelation function for a running
	// auto-import cache warming task. It is cancelled on file opens,
	// closes, changes, watched-file changes, new auto-import warming
	// requests, and when the session closes.
	gostd::CancelFunc warmAutoImportCancel;
	std::mutex warmAutoImportMu;

	// idleCacheCleanTimer is a resettable timer for scheduling idle
	// disk cache cleans. The timer resets on any file event (open,
	// close, change, save, watch) and fires after 30 seconds of
	// inactivity.
	idleTimer* idleCacheCleanTimer = nullptr;
	std::mutex idleCacheCleanMu;

	// performanceTelemetryCancel cancels the periodic performance
	// telemetry ticker.
	gostd::CancelFunc performanceTelemetryCancel;

	// seenProjects tracks projects that have already had telemetry
	// sent.
	collections::SyncSet<ID> seenProjects;

	// watches tracks the current watch globs and how many individual
	// WatchedFiles are using each glob.
	watchRegistry* watches = nullptr;

	// Interned project IDs come from ls::autoimport::InternProjectID
	// so the clone hosts built during snapshot clones produce the same
	// ProjectID* the session hands to LanguageServices.

	// globalDiagPublishPending is set to true when a global
	// diagnostics publish task should be enqueued. It is reset when
	// the task runs, coalescing multiple requests into a single
	// background task.
	std::atomic<bool> globalDiagPublishPending{false};

	// --- ResolutionHost / NpmExecutor (TypingsInstallerHost) ---

	// FS — session.go:247.
	vfs::FS* FS() { return fs; }

	// GetCurrentDirectory implements module.ResolutionHost —
	// session.go:252.
	std::string GetCurrentDirectory() override {
		return options->CurrentDirectory;
	}

	// DefaultLibraryPath — session.go:256.
	std::string DefaultLibraryPath() const {
		return options->DefaultLibraryPath;
	}

	bool FileExists(std::string_view fileName) override {
		return fs->FileExists(std::string{fileName});
	}
	bool DirectoryExists(std::string_view directory) override {
		return fs->DirectoryExists(std::string{directory});
	}
	std::optional<std::string> ReadFile(
	    std::string_view fileName) override {
		auto [content, ok] = fs->ReadFile(std::string{fileName});
		if (!ok) {
			return std::nullopt;
		}
		return content;
	}
	std::string Realpath(std::string_view path) override {
		return fs->Realpath(std::string{path});
	}
	bool UseCaseSensitiveFileNames() override {
		return fs->UseCaseSensitiveFileNames();
	}
	module::ResolutionHost::AccessibleEntries GetAccessibleEntries(
	    std::string_view path) override {
		auto entries = fs->GetAccessibleEntries(std::string{path});
		return {.files = std::move(entries.files),
		        .directories = std::move(entries.directories),
		        .symlinks = std::move(entries.symlinks)};
	}

	// Trace implements module.ResolutionHost — session.go:280.
	void Trace(std::string_view msg) {
		TSC_UNREACHABLE(
		    "ATA module resolution should not use tracing");
	}

	// NpmInstall implements ata.NpmExecutor — session.go:1849.
	std::pair<std::string, gostd::Error> NpmInstall(
	    const std::string& cwd,
	    const std::vector<std::string>& npmInstallArgs) override;

	// Config — session.go:261. Gets a copy of the current
	// configuration.
	ls::lsutil::UserPreferences Config() {
		std::lock_guard<std::mutex> lk(userConfigRWMu);
		return workspaceUserPreferences;
	}

	// backgroundContext — session.go:266.
	gostd::Context backgroundContext() {
		return WithCurrentLocale(backgroundCtx);
	}

	// WithCurrentLocale — session.go:270.
	gostd::Context WithCurrentLocale(const gostd::Context& ctx) {
		if (client == nullptr) {
			return ctx;
		}
		return locale::withLocale(ctx, client->GetLocale());
	}

	// Configure — session.go:284.
	void Configure(const ls::lsutil::UserPreferences& config);

	// InitializeWithUserConfig — session.go:312.
	void InitializeWithUserConfig(
	    const ls::lsutil::UserPreferences& config) {
		initialUserPreferences = config;
		Configure(config);
	}

	// DidOpenFile — session.go:317.
	void DidOpenFile(const gostd::Context& ctx,
	                 const lsp::lsproto::DocumentUri& uri,
	                 int32_t version, const std::string& content,
	                 const lsp::lsproto::LanguageKind& languageKind);

	// SetContentMapperContributions — session.go:344. Atomically
	// replaces extension-provided inferred-project mappers and
	// discovers configured projects for matching open documents.
	// Configured projects never consume these mappers.
	void SetContentMapperContributions(
	    const gostd::Context& ctx,
	    const ContentMapperContributions& contributions,
	    const std::vector<lsp::lsproto::DocumentUri>& documentURIs);

	// DidCloseFile — session.go:364.
	void DidCloseFile(const gostd::Context& ctx,
	                  const lsp::lsproto::DocumentUri& uri);

	// DidChangeFile — session.go:376.
	void DidChangeFile(
	    const gostd::Context& ctx,
	    const lsp::lsproto::DocumentUri& uri, int32_t version,
	    const std::vector<lsp::lsproto::
	                          TextDocumentContentChangePartialOrWholeDocument>&
	        changes);

	// isContentMapperFile — session.go:407. Reports whether uri is a
	// content-mapped file handled by a configured content mapper,
	// based on the extensions currently registered with the client
	// for text document synchronization.
	bool isContentMapperFile(const lsp::lsproto::DocumentUri& uri);

	// DidSaveFile — session.go:412.
	void DidSaveFile(const gostd::Context& ctx,
	                 const lsp::lsproto::DocumentUri& uri);

	// DidChangeWatchedFiles — session.go:421.
	void DidChangeWatchedFiles(
	    const gostd::Context& ctx,
	    const std::vector<lsp::lsproto::FileEvent*>& changes);

	// DidChangeCompilerOptionsForInferredProjects — session.go:495.
	void DidChangeCompilerOptionsForInferredProjects(
	    const gostd::Context& ctx, CompilerOptions* options);

	// ScheduleDiagnosticsRefresh — session.go:503.
	void ScheduleDiagnosticsRefresh() {
		scheduleDiagnosticsRefresh(options->DebounceDelay);
	}

	// scheduleDiagnosticsRefresh — session.go:508. Schedules a
	// coalesced workspace diagnostics refresh after delay. A delay of
	// 0 refreshes as soon as the background queue runs the task; it is
	// used for interactive edits (e.g. a content-mapped file) where
	// the debounce would make dependent-file diagnostics feel
	// sluggish.
	void scheduleDiagnosticsRefresh(
	    std::chrono::nanoseconds delay);

	// cancelDiagnosticsRefresh — session.go:549.
	void cancelDiagnosticsRefresh();

	// ScheduleSnapshotUpdate — session.go:559.
	void ScheduleSnapshotUpdate(UpdateReason reason);

	// cancelScheduledSnapshotUpdate — session.go:637.
	void cancelScheduledSnapshotUpdate();

	// cancelWarmAutoImportCache — session.go:652.
	void cancelWarmAutoImportCache();

	// scheduleIdleCacheClean — session.go:661.
	void scheduleIdleCacheClean();

	// cancelIdleCacheClean — session.go:688.
	void cancelIdleCacheClean();

	// StartPerformanceTelemetry — session.go:733. Begins periodic
	// collection and sending of performance telemetry. It should be
	// called once after the session is initialized.
	void StartPerformanceTelemetry();

	// stopPerformanceTelemetry — session.go:751.
	void stopPerformanceTelemetry();

	// sendPerformanceTelemetry — session.go:758.
	void sendPerformanceTelemetry(const gostd::Context& ctx);

	// sendProjectInfoTelemetryForNewProjects — session.go:858.
	void sendProjectInfoTelemetryForNewProjects(
	    project::Snapshot* oldSnapshot,
	    project::Snapshot* newSnapshot);

	// sendProjectInfoTelemetry — session.go:874.
	void sendProjectInfoTelemetry(const gostd::Context& ctx,
	                              Project* project);

	// collectProjectInfoTelemetry — session.go:893.
	lsp::lsproto::TelemetryEvent collectProjectInfoTelemetry(
	    Project* project);

	// internProjectID — C++ adapter: the Go side passes project.ID()
	// (which satisfies autoimport.ProjectID) directly.
	ls::autoimport::ProjectID* internProjectID(const ID& id);
	// internATAProjectID — ata.TypingsInstallRequest.ProjectID wants
	// ata::ProjectID (String()); Go's project.ID() is a fmt.Stringer.
	ata::ProjectID* internATAProjectID(const ID& id);

	// Snapshot — session.go:985.
	project::Snapshot* Snapshot() {
		std::shared_lock<std::shared_mutex> lk(snapshotMu);
		return snapshot;
	}

	// getSnapshot — session.go:993. Flushes pending changes and
	// updates the session's snapshot if needed for the given request.
	// When callerRef is true, the returned snapshot has an extra
	// reference for the caller (taken atomically under snapshotMu),
	// guaranteeing it stays alive until the caller calls Deref.
	project::Snapshot* getSnapshot(const gostd::Context& ctx,
	                               const ResourceRequest& request,
	                               bool callerRef);

	// getSnapshotAndDefaultProject — session.go:1076.
	std::tuple<project::Snapshot*, Project*, ls::LanguageService*,
	           gostd::Error>
	getSnapshotAndDefaultProject(const gostd::Context& ctx,
	                             const lsp::lsproto::DocumentUri& uri,
	                             bool callerRef);

	// GetLanguageService — session.go:1095.
	std::pair<ls::LanguageService*, gostd::Error>
	GetLanguageService(const gostd::Context& ctx,
	                   const lsp::lsproto::DocumentUri& uri);

	// GetLanguageServiceAndProjectsForFile — session.go:1103.
	std::tuple<Project*, ls::LanguageService*,
	           std::vector<ls::Project*>, gostd::Error>
	GetLanguageServiceAndProjectsForFile(
	    const gostd::Context& ctx,
	    const lsp::lsproto::DocumentUri& uri);

	// GetProjectsForFile — session.go:1113.
	std::pair<std::vector<ls::Project*>, gostd::Error>
	GetProjectsForFile(const gostd::Context& ctx,
	                   const lsp::lsproto::DocumentUri& uri);

	// GetLanguageServicesForDocumentsLoadingProjectTree —
	// session.go:1128. Returns language services for every project in
	// the snapshot, loading all project trees first so that projects
	// that were never opened but reference the given documents are
	// included. Loading the trees is expensive, so this should only be
	// used by operations that need to touch every project in a
	// solution, like file rename.
	std::vector<ls::LanguageService*>
	GetLanguageServicesForDocumentsLoadingProjectTree(
	    const gostd::Context& ctx,
	    const std::vector<lsp::lsproto::DocumentUri>& uris);

	// GetLanguageServiceForProjectWithFile — session.go:1153.
	ls::LanguageService* GetLanguageServiceForProjectWithFile(
	    const gostd::Context& ctx, Project* project,
	    const lsp::lsproto::DocumentUri& uri);

	// WithSnapshotLoadingProjectTree — session.go:1172. Acquires a
	// ref'd snapshot with the requested project trees loaded, then
	// calls fn. The snapshot stays alive for the duration of fn.
	void WithSnapshotLoadingProjectTree(
	    const gostd::Context& ctx,
	    collections::Set<tspath::Path>* requestedProjectTrees,
	    const std::function<void(project::Snapshot*)>& fn);

	// WithSnapshotForDocument — session.go:1185.
	void WithSnapshotForDocument(
	    const gostd::Context& ctx,
	    const lsp::lsproto::DocumentUri& uri,
	    const std::function<void(project::Snapshot*)>& fn);

	// GetCurrentLanguageServiceWithAutoImports — session.go:1197.
	// Flushes pending file changes, clones the current snapshot with
	// auto-import preparation for the given URI, then returns a
	// LanguageService for the default project. Use this only outside
	// of request handling (e.g. cache warming). For request handlers,
	// use GetLanguageServiceWithAutoImports with the request-level
	// snapshot instead.
	std::pair<ls::LanguageService*, gostd::Error>
	GetCurrentLanguageServiceWithAutoImports(
	    const gostd::Context& ctx,
	    const lsp::lsproto::DocumentUri& uri);

	// WithLanguageServiceAndSnapshot — session.go:1220. Synchronously
	// acquires a ref'd snapshot and creates a language service for the
	// given URI. fn receives both the language service and the backing
	// snapshot so it can clone the snapshot (e.g. to enable
	// auto-imports). The snapshot is kept alive until the async work
	// completes.
	//
	// Only use this method when the callback needs direct access to
	// the snapshot. For handlers that only need a LanguageService, use
	// GetLanguageService directly—language services continue to work
	// even after their backing snapshot has been disposed.
	std::pair<std::function<gostd::Error()>, gostd::Error>
	WithLanguageServiceAndSnapshot(
	    const gostd::Context& ctx,
	    const lsp::lsproto::DocumentUri& uri,
	    const std::function<
	        std::pair<std::function<gostd::Error()>, gostd::Error>(
	            ls::LanguageService*, project::Snapshot*)>& fn);

	// GetLanguageServiceWithAutoImports — session.go:1244. Clones the
	// given snapshot with auto-import preparation for the given URI,
	// without flushing pending file changes. The cloned snapshot will
	// be adopted as the session's current snapshot in the background
	// if other changes haven't been adopted in the meantime.
	std::pair<ls::LanguageService*, gostd::Error>
	GetLanguageServiceWithAutoImports(
	    const gostd::Context& ctx, project::Snapshot* baseSnapshot,
	    const lsp::lsproto::DocumentUri& uri);

	// tryAdoptSnapshotChangeInBackground — session.go:1260.
	void tryAdoptSnapshotChangeInBackground(
	    project::Snapshot* baseSnapshot,
	    project::Snapshot* newSnapshot);

	// adoptSnapshotChange — session.go:1273. Promotes a cloned
	// snapshot as the session's current snapshot so future requests
	// benefit from the work already done. If the session has moved on,
	// the snapshot is discarded; the next request needing auto-imports
	// will redo the work on the latest snapshot.
	void adoptSnapshotChange(project::Snapshot* baseSnapshot,
	                         project::Snapshot* newSnapshot);

	// UpdateSnapshot — session.go:1309.
	void UpdateSnapshot(
	    const gostd::Context& ctx,
	    std::unordered_map<tspath::Path, Overlay*> overlays,
	    const SnapshotChange& change) {
		updateSnapshot(ctx, std::move(overlays), change, false);
	}

	// updateSnapshotRef — session.go:1316. Like UpdateSnapshot but
	// returns the created snapshot with an extra reference for the
	// caller. The ref is taken atomically with the snapshot assignment
	// under snapshotMu, so the snapshot is guaranteed to be alive when
	// returned. The caller must call snapshot.Deref() when done.
	project::Snapshot* updateSnapshotRef(
	    const gostd::Context& ctx,
	    std::unordered_map<tspath::Path, Overlay*> overlays,
	    const SnapshotChange& change) {
		return updateSnapshot(ctx, std::move(overlays), change, true);
	}

	// updateSnapshot — session.go:1320.
	project::Snapshot* updateSnapshot(
	    const gostd::Context& ctx,
	    std::unordered_map<tspath::Path, Overlay*> overlays,
	    const SnapshotChange& change, bool callerRef);

	// takeContentMapperTimingDelta — session.go:1378.
	contentmapper::Timings takeContentMapperTimingDelta();

	// logContentMapperTimings — session.go:1390.
	void logContentMapperTimings(
	    const contentmapper::Timings& timings);

	// WaitForBackgroundTasks — session.go:1425. Waits for all
	// background tasks to complete. Intended for testing only.
	void WaitForBackgroundTasks() {
		cancelIdleCacheClean();
		backgroundQueue->Wait();
	}

	// updateWatch — session.go:1430.
	template <typename T>
	std::vector<gostd::Error> updateWatch(
	    const gostd::Context& ctx, WatchedFiles<T>* oldWatcher,
	    WatchedFiles<T>* newWatcher);
	// updateWatchNew / updateWatchOld — the one-sided call sites; the
	// typed helpers let T deduce from the non-null argument (Go's
	// untyped nil doesn't carry a type).
	template <typename T>
	std::vector<gostd::Error> updateWatchNew(
	    const gostd::Context& ctx, WatchedFiles<T>* newWatcher) {
		return updateWatch<T>(ctx, nullptr, newWatcher);
	}
	template <typename T>
	std::vector<gostd::Error> updateWatchOld(
	    const gostd::Context& ctx, WatchedFiles<T>* oldWatcher) {
		return updateWatch<T>(ctx, oldWatcher, nullptr);
	}

	// updateContentMapperRegistrations — session.go:1545. Computes the
	// union of content mapper extensions across all loaded configs in
	// the new snapshot and, when the set changes, asks the client to
	// synchronize text documents with those extensions. This is how an
	// otherwise unsupported file (e.g. a `.vue`) begins flowing to the
	// server once a config that maps it is discovered.
	gostd::Error updateContentMapperRegistrations(
	    const gostd::Context& ctx, project::Snapshot* snapshot);

	// updateWatches — session.go:1579.
	gostd::Error updateWatches(project::Snapshot* oldSnapshot,
	                           project::Snapshot* newSnapshot);

	// Close — session.go:1654.
	void Close();

	// flushChanges — session.go:1671.
	std::tuple<FileChangeSummary,
	           std::unordered_map<tspath::Path, Overlay*>,
	           std::unordered_map<ID, ATAStateChange*>,
	           ls::lsutil::UserPreferences*>
	flushChanges(const gostd::Context& ctx);

	// flushChangesLocked — session.go:1689. Should only be called
	// with pendingFileChangesMu held.
	std::pair<FileChangeSummary,
	          std::unordered_map<tspath::Path, Overlay*>>
	flushChangesLocked(const gostd::Context& ctx);

	// logProjectChanges — session.go:1703. Logs information about
	// projects that have changed between snapshots.
	void logProjectChanges(project::Snapshot* oldSnapshot,
	                       project::Snapshot* newSnapshot);

	// logCacheStats — session.go:1731.
	void logCacheStats(project::Snapshot* snapshot);

	// refreshInlayHintsIfNeeded — session.go:1853.
	void refreshInlayHintsIfNeeded(
	    const ls::lsutil::UserPreferences& oldPrefs,
	    const ls::lsutil::UserPreferences& newPrefs);

	// refreshCodeLensIfNeeded — session.go:1861.
	void refreshCodeLensIfNeeded(
	    const ls::lsutil::UserPreferences& oldPrefs,
	    const ls::lsutil::UserPreferences& newPrefs);

	// refreshDiagnosticsIfNeeded — session.go:1869.
	void refreshDiagnosticsIfNeeded(
	    const ls::lsutil::UserPreferences& oldPrefs,
	    const ls::lsutil::UserPreferences& newPrefs);

	// refreshATAIfNeeded — session.go:1879.
	void refreshATAIfNeeded(const ls::lsutil::UserPreferences& oldPrefs,
	                        const ls::lsutil::UserPreferences& newPrefs);

	// publishProgramDiagnostics — session.go:1887.
	void publishProgramDiagnostics(project::Snapshot* oldSnapshot,
	                               project::Snapshot* newSnapshot);

	// publishProjectDiagnostics — session.go:1946.
	void publishProjectDiagnostics(
	    const gostd::Context& ctx, const std::string& configFilePath,
	    const std::vector<Diagnostic*>& diagnostics,
	    lsconv::Converters* converters);

	// EnqueuePublishGlobalDiagnostics — session.go:1965. Schedules a
	// background check for new accumulated global diagnostics from
	// checker pools, re-publishing tsconfig diagnostics if changed.
	// Multiple calls are coalesced into a single background task.
	void EnqueuePublishGlobalDiagnostics();

	// publishGlobalDiagnostics — session.go:1974.
	void publishGlobalDiagnostics(const gostd::Context& ctx);

	// triggerATAForUpdatedProjects — session.go:1990.
	void triggerATAForUpdatedProjects(project::Snapshot* newSnapshot);

	// warmAutoImportCache — session.go:2057.
	void warmAutoImportCache(const gostd::Context& ctx,
	                         const SnapshotChange& change,
	                         project::Snapshot* oldSnapshot,
	                         project::Snapshot* newSnapshot);

	// APIUpdate — api.go:19. Creates a new snapshot incorporating the
	// given file changes and the supplied API open/close request. The
	// apiRequest may open or close projects and files; opens are
	// tracked in the snapshot (ref-counted) so they persist across
	// future updates, and closes release a previously taken ref.
	// Programs are updated only when explicitly requested by an open
	// or ensure operation. On success, returns a ref'd snapshot which
	// the caller must Deref when done. On failure, releases the
	// rejected snapshot and returns nil and the error. A snapshot with
	// an API error is never adopted as canonical session state; host
	// changes flushed alongside it are adopted separately.
	std::pair<project::Snapshot*, gostd::Error> APIUpdate(
	    const gostd::Context& ctx,
	    const FileChangeSummary& apiFileChanges,
	    APISnapshotRequest* apiRequest);

	// TryAdoptSnapshotInBackground — api.go:57. Retains a derived
	// snapshot and attempts to adopt it as the session's current
	// snapshot without blocking the caller.
	void TryAdoptSnapshotInBackground(project::Snapshot* baseSnapshot,
	                                  project::Snapshot* newSnapshot) {
		snapshotHost->RetainSnapshot(newSnapshot);
		tryAdoptSnapshotChangeInBackground(baseSnapshot,
		                                   newSnapshot);
	}
};

// NewSession — session.go:210.
Session* NewSession(SessionInit* init);

// shouldPublishProgramDiagnostics — session.go:1941.
inline bool shouldPublishProgramDiagnostics(Project* p,
                                            uint64_t snapshotID) {
	if (p->Kind != KindConfigured || p->Program == nullptr ||
	    p->ProgramLastUpdate != snapshotID) {
		return false;
	}
	return p->ProgramUpdateKind > ProgramUpdateKindCloned;
}

// setTristate — session.go:956.
void setTristate(tsoptions::JsonObject* m, const std::string& key,
                 Tristate v);

// boolTelemetry — session.go:964.
inline std::string boolTelemetry(bool v) {
	return v ? "true" : "false";
}

// countFileStats — session.go:971.
lsp::lsproto::ProjectInfoTelemetryMeasurements* countFileStats(
    const std::vector<SourceFile*>& sourceFiles);

// hasContentMapperOperationTimings — session.go:1417.
bool hasContentMapperOperationTimings(
    const std::unordered_map<std::string, contentmapper::MapperTimings>& timings);

// hasContentMapperOperationTiming — session.go:1425.
bool hasContentMapperOperationTiming(
    const contentmapper::MapperTimings& timing);

} // namespace tsc::project
