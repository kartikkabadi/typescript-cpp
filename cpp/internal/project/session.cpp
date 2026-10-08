// session.go — Session methods.
//
// === slice: project ===

#include "internal/project/session.h"

#include <algorithm>
#include <thread>
#include <tuple>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include "internal/core/utilities.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/locale/locale.h"
#include "internal/packagejson/packagejson.h"
#include "internal/pprof/pprof.h"
#include "internal/project/project.h"
#include "internal/project/snapshotfs.h"
#include "internal/tsoptions/tsoptions.h"

#if defined(__linux__)
#include <sys/sysinfo.h>
#endif

namespace tsc::project {

namespace {

// ctxSelectTimeout — Go `select { case <-time.After(d):
// proceed; case <-ctx.Done(): return }`. Returns true if the delay
// elapsed, false if the context was cancelled first.
inline bool ctxWaitDelay(const gostd::Context& ctx,
                         std::chrono::nanoseconds delay) {
	auto deadline = std::chrono::steady_clock::now() + delay;
	while (std::chrono::steady_clock::now() < deadline) {
		if (gostd::ctxDone(ctx)) {
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return true;
}

} // namespace

// NewSession — session.go:210.
Session* NewSession(SessionInit* init) {
	auto* snapshotHost = NewSnapshotHost(init);
	auto* sessionLogger = init->Logger;
	if (sessionLogger == nullptr) {
		sessionLogger = logging::newNopLogger();
	}
	auto* session = new Session();
	session->snapshotHost = snapshotHost;
	session->options = init->Options;
	session->logger = sessionLogger;
	session->backgroundCtx = init->BackgroundCtx;
	session->toPath = snapshotHost->toPath;
	session->client = init->Client;
	session->npmExecutor = init->NpmExecutor;
	session->clientRef = init->ClientRef;
	session->npmExecutorRef = init->NpmExecutorRef;
	session->fs = newOverlayFS(
	    snapshotHost->fs,
	    std::unordered_map<tspath::Path, Overlay*>{},
	    init->Options->PositionEncoding, snapshotHost->toPath);
	session->backgroundQueue = background::NewQueue();
	session->startTime = std::chrono::steady_clock::now();
	bool relPatternSupport = lsp::lsproto::getClientCapabilities(
	    init->BackgroundCtx)
	    ->Workspace.DidChangeWatchedFiles.RelativePatternSupport;
	session->snapshot =
	    snapshotHost->newRootSnapshot(0, relPatternSupport);
	session->initialUserPreferences =
	    lsutil::NewDefaultUserPreferences();
	session->workspaceUserPreferences =
	    lsutil::NewDefaultUserPreferences();
	session->pendingATAChanges =
	    std::unordered_map<ID, ATAStateChange*>();
	session->watches = new watchRegistry();

	if (!init->Options->TypingsLocation.empty() &&
	    init->NpmExecutor != nullptr) {
		ata::TypingsInstallerOptions ataOpts;
		ataOpts.TypingsLocation = init->Options->TypingsLocation;
		ataOpts.ThrottleLimit = 5;
		session->typingsInstaller =
		    new ata::TypingsInstaller(&ataOpts, session);
	}
	if (snapshotHost->contentMapperHost != nullptr) {
		session->contentMapperTimings =
		    snapshotHost->contentMapperHost->Timings();
	}

	return session;
}

// internProjectID — session.go (C++ adapter for
// autoimport.ProjectID-keyed interfaces). Delegates to the
// SnapshotHost cache so every interned adapter in the session is the
// same pointer the clone hosts keyed their registry buckets with.
ls::autoimport::ProjectID* Session::internProjectID(const ID& id) {
	return snapshotHost->internProjectID(id);
}

// ataProjectIDAdapter — session.go ATA call: Go's project.ID() is a
// fmt.Stringer; ata::ProjectID wants String().
struct ataProjectIDAdapter : ata::ProjectID {
	explicit ataProjectIDAdapter(const ID& i) : id(i) {}
	ID id;
	std::string String() const override { return idString(id); }
};

// internATAProjectID — session.go:2429.
ata::ProjectID* Session::internATAProjectID(const ID& id) {
	return new ataProjectIDAdapter{id};
}

// Configure — session.go:284.
void Session::Configure(const lsutil::UserPreferences& config) {
	std::lock_guard<std::mutex> lk(configureMu);
	userConfigRWMu.lock();
	pendingUserConfigChanges = true;
	auto oldConfig = workspaceUserPreferences;
	workspaceUserPreferences = config;
	userConfigRWMu.unlock();

	if (!config.Locale.empty()) {
		auto oldLocale = client->GetLocale();
		client->SetLocale(config.Locale);
		auto newLocale = client->GetLocale();
		if (oldLocale.String() != newLocale.String()) {
			if (snapshotHost->contentMapperHost != nullptr) {
				snapshotHost->contentMapperHost->SetLocale(newLocale);
			}
		}
	}

	// Tell the client to re-request certain commands depending on user
	// preference changes.
	refreshInlayHintsIfNeeded(oldConfig, config);
	refreshCodeLensIfNeeded(oldConfig, config);
	refreshDiagnosticsIfNeeded(oldConfig, config);
	refreshATAIfNeeded(oldConfig, config);
}

// DidOpenFile — session.go:317.
void Session::DidOpenFile(
    const gostd::Context& ctx,
    const lsp::lsproto::DocumentUri& uri, int32_t version,
    const std::string& content,
    const lsp::lsproto::LanguageKind& languageKind) {
	cancelWarmAutoImportCache();
	scheduleIdleCacheClean();
	cancelScheduledSnapshotUpdate();
	std::lock_guard<std::mutex> lk(snapshotUpdateMu);
	pendingFileChangesMu.lock();
	pendingFileChanges.push_back(FileChange{
	    .Kind = FileChangeKindOpen,
	    .URI = uri,
	    .Version = version,
	    .Content = content,
	    .LanguageKind = languageKind,
	});
	auto [changes, overlays] = flushChangesLocked(ctx);
	pendingFileChangesMu.unlock();
	SnapshotChange change;
	change.reason = UpdateReasonDidOpenFile;
	change.fileChanges = changes;
	change.Documents = {uri};
	UpdateSnapshot(ctx, std::move(overlays), change);
}

// SetContentMapperContributions — session.go:344.
void Session::SetContentMapperContributions(
    const gostd::Context& ctx,
    const ContentMapperContributions& contributions,
    const std::vector<lsp::lsproto::DocumentUri>& documentURIs) {
	if (!options->RunExternalCode) {
		return;
	}
	cancelScheduledSnapshotUpdate();
	std::lock_guard<std::mutex> lk(snapshotUpdateMu);
	pendingFileChangesMu.lock();
	auto [changes, overlays] = flushChangesLocked(ctx);
	pendingFileChangesMu.unlock();
	SnapshotChange change;
	change.reason =
	    UpdateReasonDidChangeContentMapperContributions;
	change.fileChanges = changes;
	change.contentMapperContributions =
	    const_cast<ContentMapperContributions*>(&contributions);
	change.ConfiguredProjectDocuments = documentURIs;
	UpdateSnapshot(ctx, std::move(overlays), change);
	updateContentMapperRegistrations(ctx, Snapshot());
}

// DidCloseFile — session.go:364.
void Session::DidCloseFile(const gostd::Context& ctx,
                           const lsp::lsproto::DocumentUri& uri) {
	cancelWarmAutoImportCache();
	scheduleIdleCacheClean();
	pendingFileChangesMu.lock();
	pendingFileChanges.push_back(FileChange{
	    .Kind = FileChangeKindClose,
	    .URI = uri,
	});
	pendingFileChangesMu.unlock();
	ScheduleSnapshotUpdate(UpdateReasonDidCloseFile);
}

// DidChangeFile — session.go:376.
void Session::DidChangeFile(
    const gostd::Context& ctx,
    const lsp::lsproto::DocumentUri& uri, int32_t version,
    const std::vector<
        lsp::lsproto::
            TextDocumentContentChangePartialOrWholeDocument>& changes) {
	cancelWarmAutoImportCache();
	scheduleIdleCacheClean();
	pendingFileChangesMu.lock();
	pendingFileChanges.push_back(FileChange{
	    .Kind = FileChangeKindChange,
	    .URI = uri,
	    .Version = version,
	    .Changes = changes,
	});
	pendingFileChangesMu.unlock();

	// Editing a content-mapped file changes the program like any
	// source edit, but the client's pull-diagnostics machinery won't
	// re-request diagnostics for dependent files: the content-mapped
	// file is not in the diagnostic provider's document selector, so a
	// change to it never triggers the client's inter-file re-pull.
	// Prompt a workspace refresh so dependents update. We skip the
	// debounce here so the edit doesn't feel sluggish (normal source
	// edits are pulled per-keystroke client-side); the refresh is
	// still coalesced. Ordinary source files are handled entirely
	// client-side, so we cancel any pending refresh for them as
	// before.
	if (isContentMapperFile(uri)) {
		scheduleDiagnosticsRefresh(std::chrono::nanoseconds(0));
	} else {
		cancelDiagnosticsRefresh();
	}
}

// isContentMapperFile — session.go:407.
bool Session::isContentMapperFile(
    const lsp::lsproto::DocumentUri& uri) {
	auto* snapshot = Snapshot();
	auto* configured = snapshot->ConfigFileRegistry->contentMappers();
	auto extensions = configured->extensions;
	extensions.insert(
	    extensions.end(),
	    snapshot->inferredProjectContentMapperExtensions.begin(),
	    snapshot->inferredProjectContentMapperExtensions.end());
	std::vector<std::string_view> extViews;
	extViews.reserve(extensions.size());
	for (auto& e : extensions) {
		extViews.push_back(e);
	}
	return tspath::fileExtensionIsOneOf(
	    lsp::lsproto::documentUriFileName(uri), extViews);
}

// DidSaveFile — session.go:412.
void Session::DidSaveFile(const gostd::Context& ctx,
                          const lsp::lsproto::DocumentUri& uri) {
	scheduleIdleCacheClean();
	std::lock_guard<std::mutex> lk(pendingFileChangesMu);
	pendingFileChanges.push_back(FileChange{
	    .Kind = FileChangeKindSave,
	    .URI = uri,
	});
}

// DidChangeWatchedFiles — session.go:421.
void Session::DidChangeWatchedFiles(
    const gostd::Context& ctx,
    const std::vector<lsp::lsproto::FileEvent*>& changes) {
	std::vector<FileChange> fileChanges;
	fileChanges.reserve(changes.size());
	bool hasRelevantChange = false;
	bool hasConfigChange = false;
	auto* snapshot = Snapshot();
	auto* configFileRegistry = snapshot->ConfigFileRegistry;
	auto [contentMapperExtensions, contentMapperWatchedFiles] =
	    
	    snapshot->contentMapperWatchState();
	std::vector<std::string_view> contentMapperExtensionsViews;
	contentMapperExtensionsViews.reserve(
	    contentMapperExtensions.size());
	for (auto& e : contentMapperExtensions) {
		contentMapperExtensionsViews.push_back(e);
	}
	for (auto* change : changes) {
		FileChangeKind kind;
		switch (static_cast<lsp::lsproto::FileChangeType>(
		    change->Type)) {
		case lsp::lsproto::FileChangeType::Created:
			kind = FileChangeKindWatchCreate;
			break;
		case lsp::lsproto::FileChangeType::Changed:
			kind = FileChangeKindWatchChange;
			break;
		case lsp::lsproto::FileChangeType::Deleted:
			kind = FileChangeKindWatchDelete;
			break;
		default:
			continue; // Ignore unknown change types.
		}
		fileChanges.push_back(FileChange{
		    .Kind = kind,
		    .URI = change->Uri,
		});

		if (!hasConfigChange &&
		    configFileRegistry->isTracked(
		        toPath(lsp::lsproto::documentUriFileName(
		            change->Uri)))) {
			hasConfigChange = true;
		}

		if (!hasRelevantChange) {
			auto fileName =
			    lsp::lsproto::documentUriFileName(change->Uri);
			// removeTrailingDirectorySeparator returns a string_view:
			// bind the toPath() result to an owning Path so the view
			// (and pathStr below) does not dangle.
			auto path = tspath::Path{tspath::removeTrailingDirectorySeparator(
			    toPath(fileName))};
			std::string pathStr{path};
			if (contentMapperWatchedFiles->Has(
			        tspath::Path{pathStr})) {
				hasRelevantChange = true;
				continue;
			}
			auto i = pathStr.rfind('.');
			if (i == std::string::npos ||
			    pathStr.rfind('/') > i) {
				// Extensionless paths might be directories.
				// For creations/changes, we can check the
				// file system. For deletions, consult the
				// current snapshot cache to avoid treating
				// extensionless file deletions as relevant.
				if (kind != FileChangeKindWatchDelete) {
					hasRelevantChange =
					    fs->DirectoryExists(fileName);
				} else {
					snapshotMu.lock_shared();
					auto* snapshot = this->snapshot;
					snapshot->ref();
					snapshotMu.unlock_shared();
					struct derefGuard {
						project::Snapshot* s;
						~derefGuard() { s->Deref(); }
					} dg{snapshot};
					if (snapshot->fs->cacheDirectories.count(
					        pathStr) ||
					    snapshot->hasOverlayWithin(
					        tspath::Path{pathStr}) ||
					    isNodeModulesPath(
					        tspath::Path{pathStr})) {
						hasRelevantChange = true;
					}
				}
			} else {
				if (isRelevantExtension(pathStr.substr(i)) ||
				    tspath::fileExtensionIsOneOf(
				        pathStr,
				        contentMapperExtensionsViews)) {
					hasRelevantChange = true;
				}
			}
		}
	}

	pendingFileChangesMu.lock();
	pendingFileChanges.insert(pendingFileChanges.end(),
	                          fileChanges.begin(),
	                          fileChanges.end());
	pendingFileChangesMu.unlock();

	if (hasRelevantChange) {
		// Schedule a debounced diagnostics refresh only for paths
		// that can affect the TypeScript program (relevant
		// extensions or directories).
		ScheduleDiagnosticsRefresh();
	}
	if (hasConfigChange) {
		// Config file diagnostics are pushed on snapshot updates
		// rather than pulled, so they must not depend on the client
		// re-pulling diagnostics in response to the refresh request
		// above.
		ScheduleSnapshotUpdate(UpdateReasonDidChangeConfigFile);
	}
	cancelWarmAutoImportCache();
	scheduleIdleCacheClean();
}

// DidChangeCompilerOptionsForInferredProjects — session.go:495.
void Session::DidChangeCompilerOptionsForInferredProjects(
    const gostd::Context& ctx, CompilerOptions* options_) {
	compilerOptionsForInferredProjects = options_;
	SnapshotChange change;
	change.reason =
	    UpdateReasonDidChangeCompilerOptionsForInferredProjects;
	change.compilerOptionsForInferredProjects = options_;
	UpdateSnapshot(ctx, fs->Overlays(), change);
}

// scheduleDiagnosticsRefresh — session.go:508.
void Session::scheduleDiagnosticsRefresh(
    std::chrono::nanoseconds delay) {
	std::lock_guard<std::mutex> lk(diagnosticsRefreshMu);

	// Cancel any existing scheduled diagnostics refresh
	if (diagnosticsRefreshCancel != nullptr) {
		diagnosticsRefreshCancel();
		logging::log(logger, "Delaying scheduled diagnostics refresh...");
	} else {
		logging::log(logger, "Scheduling new diagnostics refresh...");
	}

	// Create a new cancellable context for the debounce task
	auto debouncePair =
	    gostd::contextWithCancel(backgroundContext());
	auto& debounceCtx = debouncePair.first;
	gostd::CancelFunc cancel = std::move(debouncePair.second);
	diagnosticsRefreshGeneration++;
	uint64_t generation = diagnosticsRefreshGeneration;
	diagnosticsRefreshCancel = cancel;

	// Enqueue the (optionally debounced) diagnostics refresh
	auto* self = this;
	backgroundQueue->Enqueue(
	    debounceCtx, [self, cancel, delay,
	                  generation](const gostd::Context& ctx) {
		    struct cancelGuard {
			    gostd::CancelFunc cancel;
			    ~cancelGuard() { cancel(); }
		    } guard{cancel};
		    if (delay.count() > 0) {
			    // Wait out the debounce window; a newer event
			    // cancels this one.
			    if (!ctxWaitDelay(ctx, delay)) {
				    return;
			    }
		    } else if (gostd::ctxDone(ctx)) {
			    return;
		    }

		    // Clear the cancel function since we're about to
		    // execute the refresh
		    self->diagnosticsRefreshMu.lock();
		    if (self->diagnosticsRefreshGeneration !=
		        generation) {
			    self->diagnosticsRefreshMu.unlock();
			    return;
		    }
		    self->diagnosticsRefreshCancel = nullptr;
		    self->diagnosticsRefreshMu.unlock();

		    if (self->options->LoggingEnabled) {
			    logging::log(self->logger, 
			        "Running scheduled diagnostics refresh");
		    }
		    if (self->client->RefreshDiagnostics(
		            self->backgroundContext()) != nullptr &&
		        self->options->LoggingEnabled) {
			    logging::log(self->logger, 
			        "Error refreshing diagnostics");
		    }
	    });
}

// cancelDiagnosticsRefresh — session.go:549.
void Session::cancelDiagnosticsRefresh() {
	std::lock_guard<std::mutex> lk(diagnosticsRefreshMu);
	if (diagnosticsRefreshCancel != nullptr) {
		diagnosticsRefreshCancel();
		logging::log(logger, "Canceled scheduled diagnostics refresh");
		diagnosticsRefreshCancel = nullptr;
		diagnosticsRefreshGeneration++;
	}
}

// ScheduleSnapshotUpdate — session.go:559.
void Session::ScheduleSnapshotUpdate(UpdateReason reason) {
	std::lock_guard<std::mutex> lk(scheduledSnapshotUpdateMu);

	// Cancel any existing scheduled snapshot update
	if (scheduledSnapshotUpdateCancel != nullptr) {
		scheduledSnapshotUpdateCancel();
		if (options->LoggingEnabled) {
			logging::log(logger, "Delaying scheduled snapshot update...");
		}
	} else if (options->LoggingEnabled) {
		logging::log(logger, "Scheduling new snapshot update...");
	}

	// Create a new cancellable context for the debounce task
	auto debouncePair =
	    gostd::contextWithCancel(backgroundContext());
	auto& debounceCtx = debouncePair.first;
	gostd::CancelFunc cancel = std::move(debouncePair.second);
	scheduledSnapshotUpdateGeneration++;
	uint64_t generation = scheduledSnapshotUpdateGeneration;
	scheduledSnapshotUpdateCancel = cancel;

	// Enqueue the debounced snapshot update
	auto* self = this;
	backgroundQueue->Enqueue(
	    debounceCtx,
	    [self, cancel, generation,
	     reason](const gostd::Context& ctx) {
		    struct cancelGuard {
			    gostd::CancelFunc cancel;
			    ~cancelGuard() { cancel(); }
		    } guard{cancel};
		    // Sleep for the debounce delay
		    if (!ctxWaitDelay(ctx, self->options->DebounceDelay)) {
			    return;
		    }

		    // Clear the cancel function since we're about to
		    // execute the update
		    self->scheduledSnapshotUpdateMu.lock();
		    if (self->scheduledSnapshotUpdateGeneration !=
		        generation) {
			    self->scheduledSnapshotUpdateMu.unlock();
			    return;
		    }
		    self->scheduledSnapshotUpdateCancel = nullptr;
		    self->scheduledSnapshotUpdateMu.unlock();

		    if (self->options->LoggingEnabled) {
			    logging::log(self->logger, 
			        "Running scheduled snapshot update");
		    }

		    self->snapshotUpdateMu.lock();
		    struct unlockGuard {
			    std::mutex& m;
			    ~unlockGuard() { m.unlock(); }
		    } ug{self->snapshotUpdateMu};

		    auto [fileChanges, overlays, ataChanges, newConfig] =
		        self->flushChanges(ctx);
		    if (fileChanges.IsEmpty() && ataChanges.empty() &&
		        newConfig == nullptr) {
			    return;
		    }

		    SnapshotChange change;
		    change.reason = reason;
		    change.fileChanges = fileChanges;
		    change.ataChanges = ataChanges;
		    change.newConfig = newConfig;
		    self->UpdateSnapshot(ctx, std::move(overlays),
		                         change);
	    });
}

// cancelScheduledSnapshotUpdate — session.go:637.
void Session::cancelScheduledSnapshotUpdate() {
	std::lock_guard<std::mutex> lk(scheduledSnapshotUpdateMu);
	if (scheduledSnapshotUpdateCancel != nullptr) {
		scheduledSnapshotUpdateCancel();
		if (options->LoggingEnabled) {
			logging::log(logger, "Canceled scheduled snapshot update");
		}
		scheduledSnapshotUpdateCancel = nullptr;
		scheduledSnapshotUpdateGeneration++;
	}
}

// cancelWarmAutoImportCache — session.go:652.
void Session::cancelWarmAutoImportCache() {
	std::lock_guard<std::mutex> lk(warmAutoImportMu);
	if (warmAutoImportCancel != nullptr) {
		warmAutoImportCancel();
		warmAutoImportCancel = nullptr;
	}
}

// scheduleIdleCacheClean — session.go:661.
void Session::scheduleIdleCacheClean() {
	std::lock_guard<std::mutex> lk(idleCacheCleanMu);

	if (idleCacheCleanTimer != nullptr) {
		idleCacheCleanTimer->Stop();
	}

	auto* timer = new idleTimer();
	idleCacheCleanTimer = timer;
	auto* self = this;
	timer->start(idleCacheCleanDelay, [self, timer] {
		self->idleCacheCleanMu.lock();
		self->idleCacheCleanTimer = nullptr;
		self->idleCacheCleanMu.unlock();
		delete timer;

		std::lock_guard<std::mutex> lk(self->snapshotUpdateMu);
		self->cancelScheduledSnapshotUpdate();

		auto ctx = self->backgroundContext();
		auto [fileChanges, overlays, ataChanges, newConfig] =
		    self->flushChanges(ctx);
		SnapshotChange change;
		change.reason = UpdateReasonIdleCleanDiskCache;
		change.fileChanges = fileChanges;
		change.ataChanges = ataChanges;
		change.newConfig = newConfig;
		change.cleanFileCache = true;
		self->UpdateSnapshot(ctx, std::move(overlays), change);

		std::thread([] { pprof::runGC(); }).detach();
	});
}

// cancelIdleCacheClean — session.go:688.
void Session::cancelIdleCacheClean() {
	std::lock_guard<std::mutex> lk(idleCacheCleanMu);
	if (idleCacheCleanTimer != nullptr) {
		idleCacheCleanTimer->Stop();
		idleCacheCleanTimer = nullptr;
	}
}

// StartPerformanceTelemetry — session.go:733.
void Session::StartPerformanceTelemetry() {
	if (!options->TelemetryEnabled) {
		return;
	}
	auto [ctx, cancel] =
	    gostd::contextWithCancel(backgroundContext());
	performanceTelemetryCancel = cancel;
	auto* self = this;
	backgroundQueue->Enqueue(
	    ctx, [self](const gostd::Context& ctx) {
		    for (;;) {
			    // ticker tick or cancel
			    if (!ctxWaitDelay(
			            ctx, performanceTelemetryInterval)) {
				    return;
			    }
			    if (self->client == nullptr ||
			        !self->client->IsActive()) {
				    continue;
			    }
			    self->sendPerformanceTelemetry(ctx);
		    }
	    });
}

// stopPerformanceTelemetry — session.go:751.
void Session::stopPerformanceTelemetry() {
	if (performanceTelemetryCancel != nullptr) {
		performanceTelemetryCancel();
		performanceTelemetryCancel = nullptr;
	}
}

// sendPerformanceTelemetry — session.go:758.
void Session::sendPerformanceTelemetry(const gostd::Context& ctx) {
	if (client == nullptr || !options->TelemetryEnabled) {
		return;
	}
	snapshotMu.lock_shared();
	auto* snapshot = this->snapshot;
	snapshot->ref();
	snapshotMu.unlock_shared();
	struct derefGuard {
		project::Snapshot* s;
		~derefGuard() { s->Deref(); }
	} dg{snapshot};

	auto* measurements =
	    new lsp::lsproto::PerformanceStatsTelemetryMeasurements();
	measurements->OpenFileCount =
	    static_cast<double>(snapshot->overlays().size());
	measurements->UptimeSeconds =
	    std::chrono::duration<double>(
	        std::chrono::steady_clock::now() - startTime)
	        .count();
	measurements->ProjectCount = static_cast<double>(
	    snapshot->ProjectCollection->Projects().size());
	measurements->ConfigCount = static_cast<double>(
	    snapshot->ConfigFileRegistry->configs.size());
	measurements->CachedDiskFileCount = static_cast<double>(
	    snapshot->fs->cacheFiles.size());

	// Go runtime metrics (runtime/metrics + go-osstat) have no C++
	// runtime equivalents. Heap stats use mallinfo2 where available;
	// the rest report 0.
#if defined(__GLIBC__)
	{
		struct mallinfo2 mi = mallinfo2();
		measurements->MemoryUsedBytes =
		    static_cast<double>(mi.uordblks);
		measurements->HeapLiveBytes =
		    static_cast<double>(mi.uordblks);
		measurements->HeapObjectCount =
		    static_cast<double>(mi.hblks);
	}
#endif
	measurements->GoMaxProcs =
	    static_cast<double>(std::thread::hardware_concurrency());
#if defined(__linux__)
	{
		struct sysinfo si;
		if (::sysinfo(&si) == 0) {
			measurements->SystemMemTotal =
			    static_cast<double>(si.totalram) *
			    si.mem_unit;
			measurements->SystemMemUsed =
			    (static_cast<double>(si.totalram) -
			     static_cast<double>(si.freeram)) *
			    si.mem_unit;
		}
	}
#endif

	// Read auto-import registry stats
	if (auto* registry = snapshot->AutoImportRegistry();
	    registry != nullptr) {
		auto* autoImportStats = registry->GetCacheStats();
		measurements->AutoImportProjectBucketCount =
		    static_cast<double>(
		        autoImportStats->ProjectBuckets.size());
		measurements->AutoImportNodeModulesBucketCount =
		    static_cast<double>(
		        autoImportStats->NodeModulesBuckets.size());
		measurements->AutoImportUniquePackageCount =
		    static_cast<double>(
		        autoImportStats->UniquePackageCount);
		for (auto& b : autoImportStats->ProjectBuckets) {
			measurements->AutoImportProjectExportCount +=
			    static_cast<double>(b.ExportCount);
			measurements->AutoImportProjectFileCount +=
			    static_cast<double>(b.FileCount);
		}
		for (auto& b : autoImportStats->NodeModulesBuckets) {
			measurements->AutoImportNodeModulesExportCount +=
			    static_cast<double>(b.ExportCount);
			measurements->AutoImportNodeModulesFileCount +=
			    static_cast<double>(b.FileCount);
			if (b.DependencyNames == nullptr) {
				measurements
				    ->AutoImportNodeModulesUnfilteredBucketCount++;
			}
		}
	}

	lsp::lsproto::TelemetryEvent telemetry;
	auto* event =
	    new lsp::lsproto::PerformanceStatsTelemetryEvent();
	event->Measurements =
	    std::shared_ptr<lsp::lsproto::PerformanceStatsTelemetryMeasurements>(
	        measurements);
	telemetry.PerformanceStatsTelemetryEvent =
	    std::shared_ptr<lsp::lsproto::PerformanceStatsTelemetryEvent>(
	        event);
	if (client->SendTelemetry(ctx, telemetry) != nullptr &&
	    options->LoggingEnabled) {
		logging::log(logger, "Error sending performance telemetry");
	}
}

// sendProjectInfoTelemetryForNewProjects — session.go:858.
void Session::sendProjectInfoTelemetryForNewProjects(
    project::Snapshot* oldSnapshot, project::Snapshot* newSnapshot) {
	if (!options->TelemetryEnabled) {
		return;
	}
	auto ctx = backgroundContext();
	DiffOrderedMaps(
	    *oldSnapshot->ProjectCollection->ProjectsByID(),
	    *newSnapshot->ProjectCollection->ProjectsByID(),
	    [&](const ID&, Project* addedProject) {
		    sendProjectInfoTelemetry(ctx, addedProject);
	    },
	    [](const ID&, Project*) {},
	    [](const ID&, Project*, Project*) {});
}

// sendProjectInfoTelemetry — session.go:874.
void Session::sendProjectInfoTelemetry(const gostd::Context& ctx,
                                       Project* project) {
	if (client == nullptr || !options->TelemetryEnabled) {
		return;
	}
	if (seenProjects.Has(project->ID())) {
		return;
	}

	if (project->Program == nullptr ||
	    project->CommandLine == nullptr) {
		return;
	}

	auto info = collectProjectInfoTelemetry(project);
	if (client->SendTelemetry(ctx, info) != nullptr) {
		if (options->LoggingEnabled) {
			logging::log(logger, "Error sending project info telemetry");
		}
		return;
	}

	seenProjects.Add(project->ID());
}

// collectProjectInfoTelemetry — session.go:893.
lsp::lsproto::TelemetryEvent Session::collectProjectInfoTelemetry(
    Project* project) {
	auto* opts = project->CommandLine->CompilerOptions();
	if (opts == nullptr) {
		opts = new CompilerOptions();
	}

	std::string configFileName = "other";
	if (project->Kind == KindConfigured) {
		auto baseName =
		    tspath::getBaseFileName(project->ConfigFileName());
		if (baseName == "tsconfig.json" ||
		    baseName == "jsconfig.json") {
			configFileName = baseName;
		}
	}

	std::string projectType = "inferred";
	if (project->Kind == KindConfigured) {
		projectType = "configured";
	}

	lsp::lsproto::Map<std::string, std::string> props{
	    {"configFileName", configFileName},
	    {"projectType", projectType},
	    {"version", Version()},
	};

	// Compiler options — same approach as Strada's
	// convertCompilerOptionsForTelemetry: booleans and enum string
	// names, no paths.
	tsoptions::JsonObject compilerOptions;
	setTristate(&compilerOptions, "strict", opts->Strict);
	setTristate(&compilerOptions, "noImplicitAny",
	            opts->NoImplicitAny);
	setTristate(&compilerOptions, "noImplicitThis",
	            opts->NoImplicitThis);
	setTristate(&compilerOptions, "strictNullChecks",
	            opts->StrictNullChecks);
	setTristate(&compilerOptions, "strictFunctionTypes",
	            opts->StrictFunctionTypes);
	setTristate(&compilerOptions, "strictBindCallApply",
	            opts->StrictBindCallApply);
	setTristate(&compilerOptions, "strictPropertyInitialization",
	            opts->StrictPropertyInitialization);
	setTristate(&compilerOptions, "strictBuiltinIteratorReturn",
	            opts->StrictBuiltinIteratorReturn);
	setTristate(&compilerOptions, "useUnknownInCatchVariables",
	            opts->UseUnknownInCatchVariables);
	setTristate(&compilerOptions, "exactOptionalPropertyTypes",
	            opts->ExactOptionalPropertyTypes);
	setTristate(&compilerOptions, "allowJs", opts->AllowJs);
	setTristate(&compilerOptions, "checkJs", opts->CheckJs);
	setTristate(&compilerOptions, "noEmit", opts->NoEmit);
	setTristate(&compilerOptions, "declaration",
	            opts->Declaration);
	setTristate(&compilerOptions, "composite", opts->Composite);
	setTristate(&compilerOptions, "isolatedModules",
	            opts->IsolatedModules);
	setTristate(&compilerOptions, "skipLibCheck",
	            opts->SkipLibCheck);
	setTristate(&compilerOptions, "incremental",
	            opts->Incremental);
	if (opts->Target != ScriptTarget::None) {
		compilerOptions.Set(
		    "target", std::string(String(opts->Target)));
	}
	if (opts->Module != ModuleKind::None) {
		compilerOptions.Set("module",
		                    std::string(String(opts->Module)));
	}
	if (opts->ModuleResolution !=
	    ModuleResolutionKind::Unknown) {
		compilerOptions.Set(
		    "moduleResolution",
		    std::string(String(opts->ModuleResolution)));
	}
	if (opts->Jsx != JsxEmit::None) {
		compilerOptions.Set("jsx", std::string(String(opts->Jsx)));
	}
	props["compilerOptions"] = tsoptions::jsonMarshal(
	    tsoptions::CompilerOptionsValue{
	        tsoptions::JsonObjectPtr(
	            std::make_shared<tsoptions::JsonObject>(
	                std::move(compilerOptions)))});

	// Config file shape
	if (auto* raw = std::get_if<tsoptions::JsonObjectPtr>(
	        &project->CommandLine->Raw.v);
	    raw != nullptr && *raw != nullptr) {
		props["extends"] = boolTelemetry((*raw)->Has("extends"));
		props["files"] = boolTelemetry((*raw)->Has("files"));
		props["include"] = boolTelemetry((*raw)->Has("include"));
		props["exclude"] = boolTelemetry((*raw)->Has("exclude"));
	}

	lsp::lsproto::TelemetryEvent telemetry;
	auto* event = new lsp::lsproto::ProjectInfoTelemetryEvent();
	event->Properties = std::move(props);
	event->Measurements =
	    std::shared_ptr<lsp::lsproto::ProjectInfoTelemetryMeasurements>(
	        countFileStats(project->Program->GetSourceFiles()));
	telemetry.ProjectInfoTelemetryEvent =
	    std::shared_ptr<lsp::lsproto::ProjectInfoTelemetryEvent>(event);
	return telemetry;
}

// setTristate — session.go:956.
void setTristate(tsoptions::JsonObject* m, const std::string& key,
                 Tristate v) {
	if (v == Tristate::True) {
		m->Set(key, true);
	} else if (v == Tristate::False) {
		m->Set(key, false);
	}
}

// countFileStats — session.go:971.
lsp::lsproto::ProjectInfoTelemetryMeasurements* countFileStats(
    const std::vector<SourceFile*>& sourceFiles) {
	auto* stats =
	    new lsp::lsproto::ProjectInfoTelemetryMeasurements();
	for (auto* sf : sourceFiles) {
		auto size = static_cast<double>(sf->end());
		switch (sf->ScriptKind) {
		case ScriptKind::JS:
			stats->JsFileCount++;
			stats->JsFileSize += size;
			break;
		case ScriptKind::JSX:
			stats->JsxFileCount++;
			stats->JsxFileSize += size;
			break;
		case ScriptKind::TS:
			if (tspath::isDeclarationFileName(sf->FileName())) {
				stats->DtsFileCount++;
				stats->DtsFileSize += size;
			} else {
				stats->TsFileCount++;
				stats->TsFileSize += size;
			}
			break;
		case ScriptKind::TSX:
			stats->TsxFileCount++;
			stats->TsxFileSize += size;
			break;
		default:
			break;
		}
	}
	return stats;
}

// getSnapshot — session.go:993.
project::Snapshot* Session::getSnapshot(const gostd::Context& ctx,
                                        const ResourceRequest& request,
                                        bool callerRef) {
	snapshotUpdateMu.lock();
	struct unlockGuard {
		std::mutex& m;
		~unlockGuard() { m.unlock(); }
	} ug{snapshotUpdateMu};
	cancelScheduledSnapshotUpdate();

	auto [fileChanges, overlays, ataChanges, newConfig] =
	    flushChanges(ctx);
	bool updateSnapshot =
	    !fileChanges.IsEmpty() || !ataChanges.empty() ||
	    newConfig != nullptr;
	if (updateSnapshot) {
		// If there are pending file changes, we need to update the
		// snapshot. Sending the requested URI ensures that the
		// project for this URI is loaded.
		SnapshotChange change;
		change.reason =
		    UpdateReasonRequestedLanguageServicePendingChanges;
		change.fileChanges = fileChanges;
		change.ataChanges = ataChanges;
		change.newConfig = newConfig;
		static_cast<ResourceRequest&>(change) = request;
		return this->updateSnapshot(ctx, std::move(overlays),
		                            change, callerRef);
	}
	// If there are no pending file changes, we can try to use the
	// current snapshot.
	snapshotMu.lock_shared();
	auto* snapshot = this->snapshot;
	UpdateReason updateReason = UpdateReasonUnknown;
	if (!request.Projects.empty()) {
		updateReason =
		    UpdateReasonRequestedLanguageServiceProjectDirty;
	} else if (request.ProjectTree != nullptr) {
		updateReason = UpdateReasonRequestedLoadProjectTree;
	} else if (!request.AutoImports.empty()) {
		updateReason =
		    UpdateReasonRequestedLanguageServiceWithAutoImports;
	} else {
		for (auto& document : request.Documents) {
			auto* project = snapshot->GetDefaultProject(document);
			if (project == nullptr) {
				updateReason =
				    UpdateReasonRequestedLanguageServiceProjectNotLoaded;
			} else if (project->dirty) {
				updateReason =
				    UpdateReasonRequestedLanguageServiceProjectDirty;
			}
		}
		if (updateReason == UpdateReasonUnknown) {
			for (auto& document :
			     request.ConfiguredProjectDocuments) {
				if (snapshot->isOpenFile(
				        lsp::lsproto::documentUriFileName(
				            document))) {
					auto* project =
					    snapshot->GetDefaultProject(
					        document);
					if (project == nullptr) {
						updateReason =
						    UpdateReasonRequestedLanguageServiceProjectNotLoaded;
					} else if (project->dirty) {
						updateReason =
						    UpdateReasonRequestedLanguageServiceProjectDirty;
					}
				} else {
					updateReason =
					    UpdateReasonRequestedLanguageServiceForFileNotOpen;
				}
			}
		}
	}
	if (updateReason == UpdateReasonUnknown) {
		if (callerRef) {
			snapshot->ref();
		}
		snapshotMu.unlock_shared();
		return snapshot;
	}

	snapshotMu.unlock_shared();
	SnapshotChange change;
	change.reason = updateReason;
	static_cast<ResourceRequest&>(change) = request;
	return this->updateSnapshot(ctx, std::move(overlays), change,
	                            callerRef);
}

// getSnapshotAndDefaultProject — session.go:1076.
std::tuple<project::Snapshot*, Project*, ls::LanguageService*,
           gostd::Error>
Session::getSnapshotAndDefaultProject(
    const gostd::Context& ctx,
    const lsp::lsproto::DocumentUri& uri, bool callerRef) {
	ResourceRequest request;
	request.Documents = {uri};
	auto* snapshot = getSnapshot(ctx, request, callerRef);
	auto* project = snapshot->GetDefaultProject(uri);
	if (project == nullptr) {
		if (callerRef) {
			snapshot->Deref();
		}
		if (auto* file = snapshot->GetFile(
		        lsp::lsproto::documentUriFileName(uri));
		    file != nullptr &&
		    file->Kind() == ScriptKind::Unknown) {
			return {nullptr, nullptr, nullptr,
			        gostd::errorf(
			            "no project found for URI %s: %w",
			            {std::string(uri),
			             ErrNoProjectForUnknownScriptKind})};
		}
		return {nullptr, nullptr, nullptr,
		        gostd::newError("no project found for URI " +
		                        std::string(uri))};
	}
	return {snapshot, project,
	        ls::NewLanguageService(internProjectID(project->ID()),
	                               project->GetProgram(),
	                               new SnapshotLSHost(snapshot),
	                               lsp::lsproto::
	                                   documentUriFileName(uri)),
	        gostd::Error{}};
}

// GetLanguageService — session.go:1095.
std::pair<ls::LanguageService*, gostd::Error>
Session::GetLanguageService(const gostd::Context& ctx,
                            const lsp::lsproto::DocumentUri& uri) {
	auto [snapshot, project, languageService, err] =
	    getSnapshotAndDefaultProject(ctx, uri, false /*callerRef*/);
	if (err != nullptr) {
		return {nullptr, err};
	}
	return {languageService, gostd::Error{}};
}

// GetLanguageServiceAndProjectsForFile — session.go:1103.
std::tuple<Project*, ls::LanguageService*,
           std::vector<ls::Project*>, gostd::Error>
Session::GetLanguageServiceAndProjectsForFile(
    const gostd::Context& ctx,
    const lsp::lsproto::DocumentUri& uri) {
	auto [snapshot, project, defaultLs, err] =
	    getSnapshotAndDefaultProject(ctx, uri, false /*callerRef*/);
	if (err != nullptr) {
		return {nullptr, nullptr, {}, err};
	}
	// !!! TODO: sheetal: Get other projects that contain the file
	// with symlink
	auto allProjects =
	    snapshot->GetLanguageServiceProjectsContainingFile(uri);
	return {project, defaultLs, allProjects, gostd::Error{}};
}

// GetProjectsForFile — session.go:1113.
std::pair<std::vector<ls::Project*>, gostd::Error>
Session::GetProjectsForFile(const gostd::Context& ctx,
                            const lsp::lsproto::DocumentUri& uri) {
	ResourceRequest request;
	request.ConfiguredProjectDocuments = {uri};
	auto* snapshot = getSnapshot(ctx, request, false /*callerRef*/);

	// !!! TODO: sheetal: Get other projects that contain the file
	// with symlink
	auto allProjects =
	    snapshot->GetLanguageServiceProjectsContainingFile(uri);
	return {allProjects, gostd::Error{}};
}

// GetLanguageServicesForDocumentsLoadingProjectTree —
// session.go:1128.
std::vector<ls::LanguageService*>
Session::GetLanguageServicesForDocumentsLoadingProjectTree(
    const gostd::Context& ctx,
    const std::vector<lsp::lsproto::DocumentUri>& uris) {
	ResourceRequest request;
	request.Documents = uris;
	request.ProjectTree = new ProjectTreeRequest();
	auto* snapshot = getSnapshot(ctx, request, false /*callerRef*/);

	std::string activeFile;
	if (!uris.empty()) {
		activeFile = lsp::lsproto::documentUriFileName(uris[0]);
	}

	auto projects =
	    snapshot->ProjectCollection->LanguageServiceProjects();
	std::vector<ls::LanguageService*> services;
	services.reserve(projects.size());
	for (auto* project : projects) {
		auto* program = project->GetProgram();
		if (program == nullptr) {
			continue;
		}

		services.push_back(ls::NewLanguageService(
		    internProjectID(project->ID()), program,
		    new SnapshotLSHost(snapshot), activeFile));
	}
	return services;
}

// GetLanguageServiceForProjectWithFile — session.go:1153.
ls::LanguageService* Session::GetLanguageServiceForProjectWithFile(
    const gostd::Context& ctx, Project* project,
    const lsp::lsproto::DocumentUri& uri) {
	ResourceRequest request;
	request.Projects = {project->ID()};
	auto* snapshot = getSnapshot(ctx, request, false /*callerRef*/);
	// Ensure we have updated project
	project = snapshot->ProjectCollection->GetProject(project->ID());
	if (project == nullptr) {
		return nullptr;
	}
	// if program doesnt contain this file any more ignore it
	if (!project->HasFile(
	        lsp::lsproto::documentUriFileName(uri))) {
		return nullptr;
	}
	return ls::NewLanguageService(
	    internProjectID(project->ID()), project->GetProgram(),
	    new SnapshotLSHost(snapshot),
	    lsp::lsproto::documentUriFileName(uri));
}

// WithSnapshotLoadingProjectTree — session.go:1172.
void Session::WithSnapshotLoadingProjectTree(
    const gostd::Context& ctx,
    collections::Set<tspath::Path>* requestedProjectTrees,
    const std::function<void(project::Snapshot*)>& fn) {
	ProjectTreeRequest treeRequest;
	treeRequest.referencedProjects = requestedProjectTrees;
	ResourceRequest request;
	request.ProjectTree = &treeRequest;
	auto* snapshot = getSnapshot(ctx, request, true /*callerRef*/);
	struct derefGuard {
		project::Snapshot* s;
		~derefGuard() { s->Deref(); }
	} guard{snapshot};
	fn(snapshot);
}

// WithSnapshotForDocument — session.go:1185.
void Session::WithSnapshotForDocument(
    const gostd::Context& ctx,
    const lsp::lsproto::DocumentUri& uri,
    const std::function<void(project::Snapshot*)>& fn) {
	ResourceRequest request;
	request.Documents = {uri};
	auto* snapshot = getSnapshot(ctx, request, true /*callerRef*/);
	struct derefGuard {
		project::Snapshot* s;
		~derefGuard() { s->Deref(); }
	} guard{snapshot};
	fn(snapshot);
}

// GetCurrentLanguageServiceWithAutoImports — session.go:1197.
std::pair<ls::LanguageService*, gostd::Error>
Session::GetCurrentLanguageServiceWithAutoImports(
    const gostd::Context& ctx,
    const lsp::lsproto::DocumentUri& uri) {
	ResourceRequest request;
	request.Documents = {uri};
	request.AutoImports = uri;
	auto* snapshot = getSnapshot(ctx, request, false /*callerRef*/);
	auto* project = snapshot->GetDefaultProject(uri);
	if (project == nullptr) {
		return {nullptr,
		        gostd::newError("no project found for URI " +
		                        std::string(uri))};
	}
	return {ls::NewLanguageService(
	            internProjectID(project->ID()),
	            project->GetProgram(), new SnapshotLSHost(snapshot),
	            lsp::lsproto::documentUriFileName(uri)),
	        gostd::Error{}};
}

// WithLanguageServiceAndSnapshot — session.go:1220.
std::pair<std::function<gostd::Error()>, gostd::Error>
Session::WithLanguageServiceAndSnapshot(
    const gostd::Context& ctx,
    const lsp::lsproto::DocumentUri& uri,
    const std::function<
        std::pair<std::function<gostd::Error()>, gostd::Error>(
            ls::LanguageService*, project::Snapshot*)>& fn) {
	auto [snapshot, project, languageService, err] =
	    getSnapshotAndDefaultProject(ctx, uri, true /*callerRef*/);
	if (err != nullptr) {
		return {nullptr, err};
	}
	auto fnResult = fn(languageService, snapshot);
	auto& asyncWork = fnResult.first;
	auto& fnErr = fnResult.second;
	if (fnErr != nullptr || asyncWork == nullptr) {
		snapshot->Deref();
		return {nullptr, fnErr};
	}
	auto snapForGuard = snapshot;
	return {[snapForGuard, asyncWork]() -> gostd::Error {
			    struct derefGuard {
				    project::Snapshot* s;
				    ~derefGuard() { s->Deref(); }
			    } guard{snapForGuard};
			    return asyncWork();
		    },
	        gostd::Error{}};
}

// GetLanguageServiceWithAutoImports — session.go:1244.
std::pair<ls::LanguageService*, gostd::Error>
Session::GetLanguageServiceWithAutoImports(
    const gostd::Context& ctx, project::Snapshot* baseSnapshot,
    const lsp::lsproto::DocumentUri& uri) {
	auto* newSnapshot =
	    snapshotHost->CloneSnapshotWithAutoImports(ctx, baseSnapshot, uri, logger);
	auto* project = newSnapshot->GetDefaultProject(uri);
	if (project == nullptr) {
		// Clone's initial ref (1) is released since we won't use
		// this snapshot.
		newSnapshot->Deref();
		return {nullptr,
		        gostd::newError("no project found for URI " +
		                        std::string(uri))};
	}

	tryAdoptSnapshotChangeInBackground(baseSnapshot, newSnapshot);

	return {ls::NewLanguageService(
	            internProjectID(project->ID()),
	            project->GetProgram(), new SnapshotLSHost(newSnapshot),
	            lsp::lsproto::documentUriFileName(uri)),
	        gostd::Error{}};
}

// tryAdoptSnapshotChangeInBackground — session.go:1260.
void Session::tryAdoptSnapshotChangeInBackground(
    project::Snapshot* baseSnapshot,
    project::Snapshot* newSnapshot) {
	// The clone's initial ref (1) is transferred to
	// adoptSnapshotChange, which will either promote it as the
	// session's current snapshot or release it if the session has
	// moved on.
	auto* self = this;
	backgroundQueue->Enqueue(
	    backgroundContext(),
	    [self, baseSnapshot, newSnapshot](const gostd::Context& ctx) {
		    self->adoptSnapshotChange(baseSnapshot, newSnapshot);
	    });
}

// adoptSnapshotChange — session.go:1273.
void Session::adoptSnapshotChange(project::Snapshot* baseSnapshot,
                                  project::Snapshot* newSnapshot) {
	snapshotMu.lock();
	auto* oldSnapshot = snapshot;
	if (oldSnapshot == baseSnapshot) {
		// Session hasn't moved on; adopt the new snapshot. The
		// clone's initial ref is transferred to become the session's
		// ref for its current snapshot.
		snapshot = newSnapshot;
		oldSnapshot->Deref();
		auto contentMapperTimings =
		    takeContentMapperTimingDelta();
		snapshotMu.unlock();
		if (options->LoggingEnabled) {
			logging::logf(logger, 
			    "Adopted snapshot %d (parent %d) as current "
			    "session snapshot (replacing %d)",
			    {gostd::fmtArg(int64_t(newSnapshot->id)),
			     gostd::fmtArg(int64_t(newSnapshot->parentId)),
			     gostd::fmtArg(int64_t(oldSnapshot->id))});
			if (newSnapshot->builderLogs != nullptr) {
				logging::log(logger, newSnapshot->builderLogs->String());
			}
			logContentMapperTimings(contentMapperTimings);
		}
	} else {
		// Session has moved on to a newer snapshot; discard this
		// one. Release the clone's initial ref. If a handler is
		// still using the snapshot, its own ref keeps it alive.
		snapshotMu.unlock();
		if (options->LoggingEnabled) {
			logging::logf(logger, 
			    "Discarded snapshot %d (parent %d); session has "
			    "moved on to snapshot %d",
			    {gostd::fmtArg(int64_t(newSnapshot->id)),
			     gostd::fmtArg(int64_t(newSnapshot->parentId)),
			     gostd::fmtArg(int64_t(oldSnapshot->id))});
			if (newSnapshot->builderLogs != nullptr) {
				auto logs = newSnapshot->builderLogs->String();
				if (!logs.empty()) {
					logging::logf(logger, 
					    "--- Discarded snapshot %d builder "
					    "logs (NOT adopted) ---",
					    {gostd::fmtArg(
					        int64_t(newSnapshot->id))});
					logging::log(logger, logs);
					logging::logf(logger, 
					    "--- End discarded snapshot %d "
					    "builder logs ---",
					    {gostd::fmtArg(
					        int64_t(newSnapshot->id))});
				}
			}
		}
		newSnapshot->Deref();
	}
}

// updateSnapshot — session.go:1320.
project::Snapshot* Session::updateSnapshot(
    const gostd::Context& ctx_,
    std::unordered_map<tspath::Path, Overlay*> overlays,
    const SnapshotChange& change, bool callerRef) {
	auto ctx = ctx_;
	snapshotMu.lock();
	auto* oldSnapshot = snapshot;
	if (!locale::hasLocale(ctx)) {
		ctx = WithCurrentLocale(ctx);
	}
	auto* newSnapshot =
	    oldSnapshot->Clone(ctx, change, std::move(overlays), logger,
	                       client);
	// A failed API request may have mutated only a prefix of its
	// clone. Such a snapshot is returned to the caller for inspection
	// and cleanup, but must never become canonical session state or
	// trigger adoption side effects.
	if (newSnapshot->apiError != nullptr) {
		snapshotMu.unlock();
		if (callerRef) {
			return newSnapshot;
		}
		newSnapshot->Deref();
		return nullptr;
	}
	snapshot = newSnapshot;
	if (callerRef) {
		newSnapshot->ref();
	}
	// The post-update background task enqueued below dereferences
	// both snapshots (updateWatches, publishProgramDiagnostics,
	// warmAutoImportCache, ...). Hand it a ref on each BEFORE the
	// session drops its own ref — Go's GC keeps them alive for the
	// goroutine's duration, but here a later updateSnapshot could
	// otherwise free either snapshot while the task still walks its
	// projects/programs.
	oldSnapshot->ref();
	newSnapshot->ref();
	contentmapper::Timings contentMapperTimings_;
	if (newSnapshot != oldSnapshot) {
		// Release the session's reference to the old snapshot. The
		// new snapshot's clone ref (1) is transferred to become the
		// session's ref for its current snapshot. Other holders
		// (e.g. active handlers) keep the old snapshot alive via
		// their own refs until they complete.
		oldSnapshot->Deref();
		contentMapperTimings_ = takeContentMapperTimingDelta();
	}
	snapshotMu.unlock();

	// Enqueue ATA updates if needed
	if (typingsInstaller != nullptr &&
	    !Config().IsATADisabled()) {
		triggerATAForUpdatedProjects(newSnapshot);
	}

	// Enqueue logging, watch updates, and diagnostic refresh tasks
	// !!! userPreferences/configuration updates
	auto* self = this;
	auto capturedChange = change;
	auto timings = contentMapperTimings_;
	if (!backgroundQueue->Enqueue(
	        backgroundContext(),
	        [self, oldSnapshot, newSnapshot, timings,
	         capturedChange](const gostd::Context& ctx) {
		    struct derefGuard {
			    project::Snapshot* a;
			    project::Snapshot* b;
			    ~derefGuard() {
				    a->Deref();
				    b->Deref();
			    }
		    } dg{oldSnapshot, newSnapshot};
		    if (self->options->LoggingEnabled) {
			    logging::logf(self->logger, 
			        "Adopted snapshot %d (parent %d) as "
			        "current session snapshot (replacing %d)",
			        {gostd::fmtArg(int64_t(newSnapshot->id)),
			         gostd::fmtArg(
			             int64_t(newSnapshot->parentId)),
			         gostd::fmtArg(int64_t(oldSnapshot->id))});
			    if (newSnapshot->builderLogs != nullptr) {
				    logging::log(self->logger, 
				        newSnapshot->builderLogs->String());
			    }
			    self->logProjectChanges(oldSnapshot,
			                            newSnapshot);
			    self->logContentMapperTimings(timings);
			    logging::log(self->logger, "");
		    }
		    if (self->options->WatchEnabled) {
			    if (self->updateWatches(oldSnapshot,
			                            newSnapshot) !=
			            nullptr &&
			        self->options->LoggingEnabled) {
				    logging::log(self->logger, 
				        "errors updating watches");
			    }
		    }
		    self->updateContentMapperRegistrations(ctx,
		                                           newSnapshot);
		    self->publishProgramDiagnostics(oldSnapshot,
		                                    newSnapshot);
		    self->sendProjectInfoTelemetryForNewProjects(
		        oldSnapshot, newSnapshot);
		    self->warmAutoImportCache(ctx, capturedChange,
		                              oldSnapshot, newSnapshot);
	    })) {
		// Dropped (queue closed / context cancelled): unwind the refs.
		oldSnapshot->Deref();
		newSnapshot->Deref();
	}

	return newSnapshot;
}

// takeContentMapperTimingDelta — session.go:1378.
contentmapper::Timings Session::takeContentMapperTimingDelta() {
	if (snapshotHost->contentMapperHost == nullptr) {
		return contentmapper::Timings{};
	}
	auto current = snapshotHost->contentMapperHost->Timings();
	std::lock_guard<std::mutex> lk(contentMapperTimingsMu);
	auto delta = current.Since(contentMapperTimings);
	contentMapperTimings = current;
	return delta;
}

// logContentMapperTimings — session.go:1390.
void Session::logContentMapperTimings(
    const contentmapper::Timings& timings) {
	if (timings.RequestWait.count() == 0 &&
	    !hasContentMapperOperationTimings(timings.Mappers)) {
		return;
	}
	logging::log(logger, 
	    "Content mapper timings since previous snapshot adoption:");
	if (timings.RequestWait.count() != 0) {
		logging::logf(logger, "  Request wait time: %v",
		             {gostd::fmtArg(
		                 gostd::durationString(
		                     timings.RequestWait))});
	}
	std::vector<std::string> identities;
	for (auto& [id, m] : timings.Mappers) {
		identities.push_back(id);
	}
	std::sort(identities.begin(), identities.end());
	for (auto& identity : identities) {
		auto& mapper = timings.Mappers.at(identity);
		if (!hasContentMapperOperationTiming(mapper)) {
			continue;
		}
		logging::logf(logger, "  %s:", {gostd::fmtArg(identity)});
		if (mapper.Spawn.Count != 0) {
			logging::logf(logger, 
			    "    Initializations: %d (%v)",
			    {gostd::fmtArg(int64_t(mapper.Spawn.Count)),
			     gostd::fmtArg(gostd::durationString(
			         mapper.Spawn.Duration +
			         mapper.Initialize.Duration))});
		}
		if (mapper.OpenProject.Count != 0) {
			logging::logf(logger, 
			    "    openProject requests: %d (%v)",
			    {gostd::fmtArg(int64_t(mapper.OpenProject.Count)),
			     gostd::fmtArg(gostd::durationString(
			         mapper.OpenProject.Duration))});
		}
		if (mapper.CloseProject.Count != 0) {
			logging::logf(logger, 
			    "    closeProject requests: %d (%v)",
			    {gostd::fmtArg(
			         int64_t(mapper.CloseProject.Count)),
			     gostd::fmtArg(gostd::durationString(
			         mapper.CloseProject.Duration))});
		}
		if (mapper.Transform.Count != 0) {
			logging::logf(logger, 
			    "    Transforms: %d (%v)",
			    {gostd::fmtArg(int64_t(mapper.Transform.Count)),
			     gostd::fmtArg(gostd::durationString(
			         mapper.Transform.Duration))});
		}
	}
}

// hasContentMapperOperationTimings — session.go:1417.
bool hasContentMapperOperationTimings(
    const std::unordered_map<std::string, contentmapper::MapperTimings>&
        timings) {
	for (auto& [id, timing] : timings) {
		if (hasContentMapperOperationTiming(timing)) {
			return true;
		}
	}
	return false;
}

// hasContentMapperOperationTiming — session.go:1425.
bool hasContentMapperOperationTiming(
    const contentmapper::MapperTimings& timing) {
	return timing.Spawn.Count != 0 || timing.OpenProject.Count != 0 ||
	       timing.CloseProject.Count != 0 ||
	       timing.Transform.Count != 0;
}

// updateWatch — session.go:1430.
template <typename T>
std::vector<gostd::Error> Session::updateWatch(
    const gostd::Context& ctx, WatchedFiles<T>* oldWatcher,
    WatchedFiles<T>* newWatcher) {
	std::vector<gostd::Error> errors;
	if (newWatcher != nullptr) {
		auto w = newWatcher->Watchers();
		std::vector<lsp::lsproto::FileSystemWatcher*> watchers(
		    w.WorkspaceWatchers);
		watchers.insert(watchers.end(),
		                w.OutsideWorkspaceWatchers.begin(),
		                w.OutsideWorkspaceWatchers.end());
		if (!watchers.empty()) {
			collections::OrderedMap<
			    WatcherID, lsp::lsproto::FileSystemWatcher*>
			    newWatchers;
			for (size_t i = 0; i < watchers.size(); i++) {
				auto globId =
				    WatcherID(w.WatcherID + "." +
				              std::to_string(i));
				if (watches->Acquire(watchers[i], globId)) {
					newWatchers.Set(globId, watchers[i]);
				}
			}
			std::vector<gostd::Error> watchErrors;
			for (auto& entry : newWatchers.Entries()) {
				// Create a fresh timeout per client call so
				// earlier calls don't consume the deadline
				// for later ones.
				auto [callCtx, callCancel] =
				    gostd::contextWithTimeout(
				        ctx, watchRequestTimeout);
				auto err = client->WatchFiles(
				    callCtx, entry.first,
				    {entry.second});
				callCancel();
				if (err != nullptr) {
					watchErrors.push_back(err);
				} else if (logger != nullptr) {
					if (oldWatcher == nullptr) {
						logging::log(logger, "Added new watch: " +
						            entry.first);
					} else {
						logging::log(logger, "Updated watch: " +
						            entry.first);
					}
					logging::log(logger, 
					    "\t" +
					    fileSystemWatcherGlobString(
					        entry.second));
					logging::log(logger, "");
				}
			}
			if (!watchErrors.empty()) {
				// Roll back ALL newly-acquired watchers on
				// any failure to keep refcounts clean. On
				// retry, Acquire will see them as new
				// again. Re-registering an
				// already-registered watcher with the
				// client is harmless (registerCapability
				// with the same ID replaces it).
				for (auto& kv : newWatchers.Entries()) {
					watches->Release(kv.second);
				}
				watches->MarkPending(w.WatcherID);
				errors.insert(errors.end(),
				              watchErrors.begin(),
				              watchErrors.end());
			} else {
				watches->ClearPending(w.WatcherID);
			}
			if (!w.IgnoredPaths.empty()) {
				logging::logf(logger, "%d paths ineligible for "
				             "watching",
				             {gostd::fmtArg(int64_t(
				                 w.IgnoredPaths.size()))});
				if (logging::loggerIsVerbose(logger)) {
					for (auto& path : w.IgnoredPaths) {
						logging::log(logger, "\t" + path);
					}
				}
			}
		}
	}
	if (oldWatcher != nullptr) {
		auto w = oldWatcher->Watchers();
		std::vector<lsp::lsproto::FileSystemWatcher*> watchers(
		    w.WorkspaceWatchers);
		watchers.insert(watchers.end(),
		                w.OutsideWorkspaceWatchers.begin(),
		                w.OutsideWorkspaceWatchers.end());
		if (!watchers.empty()) {
			std::vector<WatcherID> removedIDs;
			for (auto* watcher : watchers) {
				auto [id, removed] =
				    watches->Release(watcher);
				if (removed) {
					removedIDs.push_back(id);
				}
			}
			for (auto& id : removedIDs) {
				auto [callCtx, callCancel] =
				    gostd::contextWithTimeout(
				        ctx, watchRequestTimeout);
				auto err =
				    client->UnwatchFiles(callCtx, id);
				callCancel();
				if (err != nullptr) {
					errors.push_back(err);
				} else if (logger != nullptr &&
				           newWatcher == nullptr) {
					logging::log(logger, "Removed watch: " + id);
				}
			}
		}
	}
	return errors;
}

// updateContentMapperRegistrations — session.go:1545.
gostd::Error Session::updateContentMapperRegistrations(
    const gostd::Context& ctx, project::Snapshot* snapshot) {
	if (client == nullptr) {
		return gostd::Error{};
	}
	auto* contentMappers =
	    snapshot->ConfigFileRegistry->contentMappers();
	auto extensions = contentMappers->extensions;
	extensions.insert(
	    extensions.end(),
	    snapshot->inferredProjectContentMapperExtensions.begin(),
	    snapshot->inferredProjectContentMapperExtensions.end());
	std::sort(extensions.begin(), extensions.end());
	extensions.erase(
	    std::unique(extensions.begin(), extensions.end()),
	    extensions.end());

	std::lock_guard<std::mutex> lk(contentMapperRegistrationMu);
	// Background tasks may finish out of order; never let an older
	// snapshot's task overwrite the registration derived from a newer
	// one.
	if (snapshot->ID() <= registeredContentMapperSnapshotID) {
		return gostd::Error{};
	}
	if (extensions == registeredContentMapperExtensions) {
		registeredContentMapperSnapshotID = snapshot->ID();
		return gostd::Error{};
	}
	// RegisterContentMapperExtensions replaces the prior registration
	// wholesale (unregistering extensions that are no longer mapped
	// and registering the current set), so an empty set removes the
	// registration once the last mapping config unloads. On failure
	// we leave the state unadvanced so the next snapshot update
	// retries.
	if (auto err = client->RegisterContentMapperExtensions(
	        ctx, extensions);
	    err != nullptr) {
		if (options->LoggingEnabled) {
			logging::log(logger, err->Error());
		}
		return err;
	}
	registeredContentMapperExtensions = extensions;
	registeredContentMapperSnapshotID = snapshot->ID();
	return gostd::Error{};
}

// updateWatches — session.go:1579.
gostd::Error Session::updateWatches(project::Snapshot* oldSnapshot,
                                    project::Snapshot* newSnapshot) {
	std::vector<gostd::Error> errors;
	auto start = std::chrono::steady_clock::now();
	auto ctx = backgroundContext();
	DiffMapsFunc(
	    oldSnapshot->ConfigFileRegistry->configs,
	    newSnapshot->ConfigFileRegistry->configs,
	    [](configFileEntry* a, configFileEntry* b) {
		    return watchedFilesID(a->rootFilesWatch) ==
		           watchedFilesID(b->rootFilesWatch);
	    },
	    [&](const tspath::Path&, configFileEntry* addedEntry) {
		    auto errs = updateWatch<PatternsAndIgnored>(
		        ctx, nullptr, addedEntry->rootFilesWatch);
		    errors.insert(errors.end(), errs.begin(),
		                  errs.end());
	    },
	    [&](const tspath::Path&,
	        configFileEntry* removedEntry) {
		    auto errs = updateWatch<PatternsAndIgnored>(
		        ctx, removedEntry->rootFilesWatch, nullptr);
		    errors.insert(errors.end(), errs.begin(),
		                  errs.end());
	    },
	    [&](const tspath::Path&, configFileEntry* oldEntry,
	        configFileEntry* newEntry) {
		    auto errs = updateWatch<PatternsAndIgnored>(
		        ctx, oldEntry->rootFilesWatch,
		        newEntry->rootFilesWatch);
		    errors.insert(errors.end(), errs.begin(),
		                  errs.end());
	    });
	// Retry config watchers whose IDs didn't change but whose previous
	// registration failed.
	for (auto& [path, newEntry] :
	     newSnapshot->ConfigFileRegistry->configs) {
		auto it =
		    oldSnapshot->ConfigFileRegistry->configs.find(path);
		if (it != oldSnapshot->ConfigFileRegistry->configs.end()) {
			auto* oldEntry = it->second;
			if (watchedFilesID(oldEntry->rootFilesWatch) ==
			    watchedFilesID(newEntry->rootFilesWatch)) {
				if (watches->IsPending(
				        watchedFilesID(newEntry->rootFilesWatch))) {
					auto errs =
					    updateWatch<PatternsAndIgnored>(
					        ctx, nullptr,
					        newEntry->rootFilesWatch);
					errors.insert(errors.end(),
					              errs.begin(),
					              errs.end());
				}
			}
		}
	}

	DiffOrderedMaps(
	    *oldSnapshot->ProjectCollection->ProjectsByID(),
	    *newSnapshot->ProjectCollection->ProjectsByID(),
	    [&](const ID&, Project* addedProject) {
		    if (addedProject->programFilesWatch != nullptr) {
			    auto errs =
			        updateWatchNew(ctx, addedProject->programFilesWatch);
			    errors.insert(errors.end(), errs.begin(),
			                  errs.end());
		    }
		    if (addedProject->typingsWatch != nullptr) {
			    auto errs = updateWatchNew(ctx, addedProject->typingsWatch);
			    errors.insert(errors.end(), errs.begin(),
			                  errs.end());
		    }
		    if (addedProject->contentMapperWatch != nullptr) {
			    auto errs = updateWatchNew(ctx, addedProject->contentMapperWatch);
			    errors.insert(errors.end(), errs.begin(),
			                  errs.end());
		    }
	    },
	    [&](const ID&, Project* removedProject) {
		    if (removedProject->programFilesWatch != nullptr) {
			    auto errs =
			        updateWatchOld(ctx, removedProject->programFilesWatch);
			    errors.insert(errors.end(), errs.begin(),
			                  errs.end());
		    }
		    if (removedProject->typingsWatch != nullptr) {
			    auto errs = updateWatchOld(ctx, removedProject->typingsWatch);
			    errors.insert(errors.end(), errs.begin(),
			                  errs.end());
		    }
		    if (removedProject->contentMapperWatch != nullptr) {
			    auto errs =
			        updateWatchOld(ctx, removedProject->contentMapperWatch);
			    errors.insert(errors.end(), errs.begin(),
			                  errs.end());
		    }
	    },
	    [&](const ID&, Project* oldProject,
	        Project* newProject) {
		    if (oldProject->programFilesWatch->ID() !=
		        newProject->programFilesWatch->ID()) {
			    auto errs = updateWatch(
			        ctx, oldProject->programFilesWatch,
			        newProject->programFilesWatch);
			    errors.insert(errors.end(), errs.begin(),
			                  errs.end());
		    } else {
			    if (watches->IsPending(
			        newProject->programFilesWatch->ID())) {
				    auto errs = updateWatchNew(
				        ctx, newProject->programFilesWatch);
				    errors.insert(errors.end(),
				                  errs.begin(), errs.end());
			    }
		    }
		    if (watchedFilesID(oldProject->typingsWatch) !=
		        watchedFilesID(newProject->typingsWatch)) {
			    auto errs = updateWatch(
			        ctx, oldProject->typingsWatch,
			        newProject->typingsWatch);
			    errors.insert(errors.end(), errs.begin(),
			                  errs.end());
		    } else {
			    if (watches->IsPending(
			        watchedFilesID(newProject->typingsWatch))) {
				    auto errs = updateWatchNew(
				        ctx, newProject->typingsWatch);
				    errors.insert(errors.end(),
				                  errs.begin(), errs.end());
			    }
		    }
		    if (oldProject->contentMapperWatch->ID() !=
		        newProject->contentMapperWatch->ID()) {
			    auto errs = updateWatch(
			        ctx, oldProject->contentMapperWatch,
			        newProject->contentMapperWatch);
			    errors.insert(errors.end(), errs.begin(),
			                  errs.end());
		    } else if (watches->IsPending(
		                   newProject->contentMapperWatch
		                       ->ID())) {
			    auto errs = updateWatchNew(
			        ctx, newProject->contentMapperWatch);
			    errors.insert(errors.end(), errs.begin(),
			                  errs.end());
		    }
	    });

	if (watchedFilesID(oldSnapshot->autoImportsWatch) !=
	    watchedFilesID(newSnapshot->autoImportsWatch)) {
		auto errs = updateWatch(ctx, oldSnapshot->autoImportsWatch,
		                        newSnapshot->autoImportsWatch);
		errors.insert(errors.end(), errs.begin(), errs.end());
	} else {
		if (watches->IsPending(
		        watchedFilesID(newSnapshot->autoImportsWatch))) {
			auto errs =
			    updateWatchNew(ctx, newSnapshot->autoImportsWatch);
			errors.insert(errors.end(), errs.begin(),
			              errs.end());
		}
	}

	if (!errors.empty()) {
		return gostd::newError("errors updating watches");
	} else if (options->LoggingEnabled) {
		logging::log(logger, "Updated watches in " +
		            gostd::durationString(
		                std::chrono::steady_clock::now() -
		                start));
	}
	return gostd::Error{};
}

// Close — session.go:1654.
void Session::Close() {
	// Cancel any pending scheduled snapshot update
	cancelScheduledSnapshotUpdate();
	// Cancel any pending diagnostics refresh
	cancelDiagnosticsRefresh();
	// Cancel any pending auto-import cache warming
	cancelWarmAutoImportCache();
	// Cancel any pending idle cache clean
	cancelIdleCacheClean();
	// Cancel periodic performance telemetry
	stopPerformanceTelemetry();
	backgroundQueue->Close();
	snapshotHost->Close();
}

// flushChanges — session.go:1671.
std::tuple<FileChangeSummary,
           std::unordered_map<tspath::Path, Overlay*>,
           std::unordered_map<ID, ATAStateChange*>,
           lsutil::UserPreferences*>
Session::flushChanges(const gostd::Context& ctx) {
	pendingFileChangesMu.lock();
	struct fGuard {
		std::mutex& m;
		~fGuard() { m.unlock(); }
	} fg{pendingFileChangesMu};
	pendingATAChangesMu.lock();
	struct aGuard {
		std::mutex& m;
		~aGuard() { m.unlock(); }
	} ag{pendingATAChangesMu};
	auto pendingATAChanges_ = pendingATAChanges;
	pendingATAChanges =
	    std::unordered_map<ID, ATAStateChange*>();
	auto [fileChanges, overlays] = flushChangesLocked(ctx);
	userConfigRWMu.lock();
	struct cGuard {
		std::mutex& m;
		~cGuard() { m.unlock(); }
	} cg{userConfigRWMu};
	lsutil::UserPreferences* newPrefs = nullptr;
	if (pendingUserConfigChanges) {
		auto* p = new lsutil::UserPreferences(
		    workspaceUserPreferences);
		newPrefs = p;
	}
	pendingUserConfigChanges = false;
	return {fileChanges, overlays, pendingATAChanges_, newPrefs};
}

// flushChangesLocked — session.go:1689.
std::pair<FileChangeSummary,
          std::unordered_map<tspath::Path, Overlay*>>
Session::flushChangesLocked(const gostd::Context& ctx) {
	if (pendingFileChanges.empty()) {
		return {FileChangeSummary{}, fs->Overlays()};
	}

	auto start = std::chrono::steady_clock::now();
	auto [changes, overlays] =
	    fs->processChanges(pendingFileChanges);
	if (options->LoggingEnabled) {
		logging::log(logger, 
		    "Processed " +
		    std::to_string(pendingFileChanges.size()) +
		    " file changes in " +
		    gostd::durationString(
		        std::chrono::steady_clock::now() - start));
	}
	pendingFileChanges.clear();
	return {changes, overlays};
}

// logProjectChanges — session.go:1703.
void Session::logProjectChanges(project::Snapshot* oldSnapshot,
                                project::Snapshot* newSnapshot) {
	bool loggedProjectChanges = false;
	auto logProject = [&](Project* project) {
		auto text = project->print(
		    logging::loggerIsVerbose(logger) /*writeFileNames*/,
		    logging::loggerIsVerbose(logger) /*writeFileExplanation*/);
		logging::log(logger, text);
		loggedProjectChanges = true;
	};
	DiffOrderedMaps(
	    *oldSnapshot->ProjectCollection->ProjectsByID(),
	    *newSnapshot->ProjectCollection->ProjectsByID(),
	    [&](const ID&, Project* addedProject) {
		    // New project added
		    logProject(addedProject);
	    },
	    [&](const ID&, Project* removedProject) {
		    // Project removed
		    logging::log(logger, "\nProject '" +
		                std::string(removedProject->ID()) +
		                "' removed\n" + hr);
	    },
	    [&](const ID&, Project* oldProject,
	        Project* newProject) {
		    // Project updated
		    if (newProject->ProgramUpdateKind ==
		        ProgramUpdateKindNewFiles) {
			    logProject(newProject);
		    }
	    });

	if (loggedProjectChanges || logging::loggerIsVerbose(logger)) {
		logCacheStats(newSnapshot);
	}
}

// logCacheStats — session.go:1731.
void Session::logCacheStats(project::Snapshot* snapshot) {
	int parseCacheSize = 0;
	int extendedConfigCount = 0;
	if (logging::loggerIsVerbose(logger)) {
		snapshotHost->parseCache->entries.Range([&](
		    const ParseCacheKey&,
		    const std::shared_ptr<refCountCacheEntry<SourceFile*>>&
		        entry) {
			    parseCacheSize++;
			    return true;
		    });
		snapshotHost->extendedConfigCache->entries.Range([&](
		    const tspath::Path&,
		    const std::shared_ptr<
		        ownerCacheEntry<ExtendedConfigCacheEntry*>>&
		        entry) {
			    extendedConfigCount++;
			    return true;
		    });
	}
	logging::log(logger, "\n======== Cache Statistics ========");
	logging::logf(logger, 
	    "Open file count:   %6d",
	    {gostd::fmtArg(
	        int64_t(snapshot->overlays().size()))});
	logging::logf(logger, 
	    "Cached disk files: %6d",
	    {gostd::fmtArg(
	        int64_t(snapshot->fs->cacheFiles.size()))});
	logging::logf(logger, 
	    "Realpath aliases:  %6d",
	    {gostd::fmtArg(int64_t(
	        snapshot->fs->nodeModulesRealpathAliases.size()))});
	logging::logf(logger, 
	    "Project count:     %6d",
	    {gostd::fmtArg(int64_t(
	        snapshot->ProjectCollection->Projects().size()))});
	logging::logf(logger, 
	    "Config count:      %6d",
	    {gostd::fmtArg(int64_t(
	        snapshot->ConfigFileRegistry->configs.size()))});
	if (logging::loggerIsVerbose(logger)) {
		logging::logf(logger, "Parse cache size:           %6d",
		             {gostd::fmtArg(int64_t(parseCacheSize))});
		logging::logf(logger, 
		    "Program count:              %6d",
		    {gostd::fmtArg(
		        int64_t(snapshotHost->programCounter->Len()))});
		logging::logf(logger, 
		    "Extended config cache size: %6d",
		    {gostd::fmtArg(int64_t(extendedConfigCount))});

		logging::log(logger, "Auto Imports:");
		auto* autoImportStats =
		    snapshot->AutoImportRegistry()->GetCacheStats();
		logging::logf(logger, 
		    "\tUnique packages (by realpath): %d",
		    {gostd::fmtArg(int64_t(
		        autoImportStats->UniquePackageCount))});
		if (!autoImportStats->ProjectBuckets.empty()) {
			logging::log(logger, "\tProject buckets:");
			for (auto& bucket :
			     autoImportStats->ProjectBuckets) {
				logging::logf(logger, 
				    "\t\t%s%s:",
				    {gostd::fmtArg(bucket.Name),
				     gostd::fmtArg(bucket.State.Dirty()
				                       ? " (dirty)"
				                       : "")});
				logging::logf(logger, 
				    "\t\t\tFiles: %d",
				    {gostd::fmtArg(
				        int64_t(bucket.FileCount))});
				logging::logf(logger, 
				    "\t\t\tExports: %d",
				    {gostd::fmtArg(
				        int64_t(bucket.ExportCount))});
			}
		}
		if (!autoImportStats->NodeModulesBuckets.empty()) {
			logging::log(logger, "\tnode_modules buckets:");
			for (auto& bucket :
			     autoImportStats->NodeModulesBuckets) {
				logging::logf(logger, 
				    "\t\t%s%s:",
				    {gostd::fmtArg(bucket.Name),
				     gostd::fmtArg(bucket.State.Dirty()
				                       ? " (dirty)"
				                       : "")});
				for (auto& packageName : bucket.State
				         .DirtyPackages()
				         ->Keys()) {
					logging::logf(logger, 
					    "\t\t\tNeeds granular update: %s",
					    {gostd::fmtArg(packageName)});
				}
				if (bucket.DependencyNames != nullptr) {
					logging::logf(logger, 
					    "\t\t\tCollected packages: %d",
					    {gostd::fmtArg(int64_t(
					        bucket.DependencyNames
					            ->Len()))});
				} else {
					logging::log(logger, 
					    "\t\t\tCollected packages: all, "
					    "due to no package.json!");
				}
				logging::logf(logger, 
				    "\t\t\tTotal packages: %d",
				    {gostd::fmtArg(int64_t(
				        bucket.PackageNames->Len()))});
				logging::logf(logger, 
				    "\t\t\tFiles: %d",
				    {gostd::fmtArg(
				        int64_t(bucket.FileCount))});
				logging::logf(logger, 
				    "\t\t\tExports: %d",
				    {gostd::fmtArg(
				        int64_t(bucket.ExportCount))});
				if (bucket.State
				        .RecursiveSearchPackages() ==
				    nullptr) {
					logging::log(logger, 
					    "\t\t\tRecursive search: all");
				} else if (bucket.State
				               .RecursiveSearchPackages()
				               ->Len() > 0) {
					logging::logf(logger, 
					    "\t\t\tRecursive search: %d "
					    "packages",
					    {gostd::fmtArg(int64_t(
					        bucket.State
					            .RecursiveSearchPackages()
					            ->Len()))});
				} else {
					logging::log(logger, 
					    "\t\t\tRecursive search: none");
				}
			}
		}
	}
}

// NpmInstall — session.go:1849.
std::pair<std::string, gostd::Error> Session::NpmInstall(
    const std::string& cwd,
    const std::vector<std::string>& npmInstallArgs) {
	return npmExecutor->NpmInstall(cwd, npmInstallArgs);
}

// refreshInlayHintsIfNeeded — session.go:1853.
void Session::refreshInlayHintsIfNeeded(
    const lsutil::UserPreferences& oldPrefs,
    const lsutil::UserPreferences& newPrefs) {
	if (!(oldPrefs.InlayHints == newPrefs.InlayHints)) {
		if (client->RefreshInlayHints(backgroundContext()) !=
		        nullptr &&
		    options->LoggingEnabled) {
			logging::log(logger, "Error refreshing inlay hints");
		}
	}
}

// refreshCodeLensIfNeeded — session.go:1861.
void Session::refreshCodeLensIfNeeded(
    const lsutil::UserPreferences& oldPrefs,
    const lsutil::UserPreferences& newPrefs) {
	if (!(oldPrefs.CodeLens == newPrefs.CodeLens)) {
		if (client->RefreshCodeLens(backgroundContext()) !=
		        nullptr &&
		    options->LoggingEnabled) {
			logging::log(logger, "Error refreshing code lens");
		}
	}
}

// refreshDiagnosticsIfNeeded — session.go:1869.
void Session::refreshDiagnosticsIfNeeded(
    const lsutil::UserPreferences& oldPrefs,
    const lsutil::UserPreferences& newPrefs) {
	if (oldPrefs.CustomConfigFileName !=
	        newPrefs.CustomConfigFileName ||
	    oldPrefs.ReportStyleChecksAsWarnings !=
	        newPrefs.ReportStyleChecksAsWarnings ||
	    !(oldPrefs.EnableValidation == newPrefs.EnableValidation)) {
		ScheduleDiagnosticsRefresh();
	}
}

// refreshATAIfNeeded — session.go:1879.
void Session::refreshATAIfNeeded(
    const lsutil::UserPreferences& oldPrefs,
    const lsutil::UserPreferences& newPrefs) {
	if (oldPrefs.IsATADisabled() && !newPrefs.IsATADisabled()) {
		// ATA was re-enabled; schedule a diagnostics refresh so the
		// next snapshot update re-triggers ATA for existing projects
		// with the new setting.
		ScheduleDiagnosticsRefresh();
	}
}

// publishProgramDiagnostics — session.go:1887.
void Session::publishProgramDiagnostics(
    project::Snapshot* oldSnapshot, project::Snapshot* newSnapshot) {
	if (!options->PushDiagnosticsEnabled) {
		return;
	}
	if (newSnapshot->UserPreferences().EnableValidation == Tristate::False) {
		if (oldSnapshot->UserPreferences()
		        .EnableValidation == Tristate::False) {
			return;
		}
		for (auto& kv :
		     oldSnapshot->ProjectCollection->ProjectsByID()
		         ->Entries()) {
			auto& pid = kv.first;
			auto* oldProject = kv.second;
			auto [configuredID, configured] =
			    idConfigured(oldProject->ID());
			if (configured &&
			    oldSnapshot->ProjectCollection
			        ->GetOpenConfiguredProjects()
			        ->Has(configuredID)) {
				auto configFilePath =
				    oldProject->ConfigFilePath();
				publishProjectDiagnostics(
				    backgroundContext(),
				    std::string(configFilePath), {},
				    oldSnapshot->converters);
			}
		}
		return;
	}

	auto ctx = backgroundContext();
	auto oldProjects =
	    oldSnapshot->ProjectCollection->ProjectsByID();
	auto newProjects =
	    newSnapshot->ProjectCollection->ProjectsByID();
	auto* oldOpenProjects = oldSnapshot->ProjectCollection
	                            ->GetOpenConfiguredProjects();
	auto* newOpenProjects = newSnapshot->ProjectCollection
	                            ->GetOpenConfiguredProjects();
	DiffOrderedMaps(
	    *oldProjects, *newProjects,
	    [&](const ID&, Project* addedProject) {
		    auto [configuredID, configured] =
		        idConfigured(addedProject->ID());
		    if (!shouldPublishProgramDiagnostics(
		            addedProject, newSnapshot->ID()) ||
		        !configured ||
		        !newOpenProjects->Has(configuredID)) {
			    return;
		    }
		    auto configFilePath =
		        addedProject->ConfigFilePath();
		    publishProjectDiagnostics(
		        ctx, std::string(configFilePath),
		        addedProject->GetProjectDiagnostics(),
		        newSnapshot->converters);
	    },
	    [&](const ID&, Project* removedProject) {
		    if (removedProject->Kind != KindConfigured) {
			    return;
		    }
		    auto configFilePath =
		        removedProject->ConfigFilePath();
		    publishProjectDiagnostics(
		        ctx, std::string(configFilePath), {},
		        oldSnapshot->converters);
	    },
	    [&](const ID&, Project* oldProject,
	        Project* newProject) {
		    auto [configuredID, configured] =
		        idConfigured(newProject->ID());
		    if (!shouldPublishProgramDiagnostics(
		            newProject, newSnapshot->ID()) ||
		        !configured ||
		        !newOpenProjects->Has(configuredID)) {
			    return;
		    }
		    auto configFilePath =
		        newProject->ConfigFilePath();
		    publishProjectDiagnostics(
		        ctx, std::string(configFilePath),
		        newProject->GetProjectDiagnostics(),
		        newSnapshot->converters);
	    });
	// Sync diagnostics for projects whose open-file state changed
	// without a program update.
	for (auto& kv : newProjects->Entries()) {
		auto& projectID = kv.first;
		auto* newProject = kv.second;
		if (newProject->Kind != KindConfigured) {
			continue;
		}
		if (!oldProjects->Has(projectID)) {
			continue; // Handled by added project case above
		}
		auto [configuredID, _c] =
		    idConfigured(newProject->ID());
		auto configFilePath = newProject->ConfigFilePath();
		auto* oldProject = *oldProjects->Get(projectID).first;
		bool newHasOpenFiles =
		    newOpenProjects->Has(configuredID);
		bool oldHasOpenFiles =
		    oldOpenProjects->Has(configuredID);
		if (newHasOpenFiles && !oldHasOpenFiles &&
		    (newProject == oldProject ||
		     !shouldPublishProgramDiagnostics(
		         newProject, newSnapshot->ID()))) {
			// Project reopened without a program update
			publishProjectDiagnostics(
			    ctx, std::string(configFilePath),
			    newProject->GetProjectDiagnostics(),
			    newSnapshot->converters);
		} else if (!newHasOpenFiles && oldHasOpenFiles) {
			// Project closed
			publishProjectDiagnostics(
			    ctx, std::string(configFilePath), {},
			    newSnapshot->converters);
		}
	}
}

// publishProjectDiagnostics — session.go:1946.
void Session::publishProjectDiagnostics(
    const gostd::Context& ctx_, const std::string& configFilePath,
    const std::vector<Diagnostic*>& diagnostics_,
    lsconv::Converters* converters) {
	auto diagnostics = diagnostics_;
	auto ctx = ctx_;
	if (Config().EnableValidation == Tristate::False) {
		diagnostics.clear();
	}
	ctx = WithCurrentLocale(ctx);
	std::vector<std::shared_ptr<lsp::lsproto::Diagnostic>>
	    lspDiagnostics;
	lspDiagnostics.reserve(diagnostics.size());
	for (auto* diag : diagnostics) {
		lspDiagnostics.push_back(
		    std::shared_ptr<lsp::lsproto::Diagnostic>(
		        lsconv::DiagnosticToLSPPush(ctx, converters, diag)));
	}

	lsp::lsproto::PublishDiagnosticsParams params;
	params.Uri =
	    lsconv::FileNameToDocumentURI(configFilePath);
	params.Diagnostics = lspDiagnostics;
	if (client->PublishDiagnostics(ctx, &params) != nullptr &&
	    options->LoggingEnabled) {
		logging::log(logger, "Error publishing diagnostics");
	}
}

// EnqueuePublishGlobalDiagnostics — session.go:1965.
void Session::EnqueuePublishGlobalDiagnostics() {
	if (!options->PushDiagnosticsEnabled ||
	    Config().EnableValidation == Tristate::False) {
		return;
	}
	bool expected = false;
	if (globalDiagPublishPending.compare_exchange_strong(
	        expected, true)) {
		auto* self = this;
		backgroundQueue->Enqueue(
		    backgroundContext(),
		    [self](const gostd::Context& ctx) {
			    self->publishGlobalDiagnostics(ctx);
		    });
	}
}

// publishGlobalDiagnostics — session.go:1974.
void Session::publishGlobalDiagnostics(const gostd::Context& ctx) {
	struct guard {
		std::atomic<bool>& flag;
		~guard() { flag.store(false); }
	} g{globalDiagPublishPending};

	snapshotMu.lock_shared();
	auto* snapshot = this->snapshot;
	snapshot->ref();
	snapshotMu.unlock_shared();
	struct derefGuard {
		project::Snapshot* s;
		~derefGuard() { s->Deref(); }
	} dg{snapshot};

	for (auto* project :
	     snapshot->ProjectCollection->Projects()) {
		if (project->Kind != KindConfigured ||
		    project->checkerPool == nullptr) {
			continue;
		}
		if (project->checkerPool
		        ->TakeNewGlobalDiagnostics()) {
			publishProjectDiagnostics(
			    ctx, std::string(project->configFilePath),
			    project->GetProjectDiagnostics(),
			    snapshot->converters);
		}
	}
}

// triggerATAForUpdatedProjects — session.go:1990.
void Session::triggerATAForUpdatedProjects(
    project::Snapshot* newSnapshot) {
	for (auto* project :
	     newSnapshot->ProjectCollection->Projects()) {
		if (project->ShouldTriggerATA(newSnapshot->ID())) {
			auto* self = this;
			auto* project_ = project;
			// Projects are owned by their snapshot — hold a ref so
			// the project can't be freed out from under the worker
			// (Go's GC does the same for the goroutine's captures).
			newSnapshot->ref();
			if (!backgroundQueue->Enqueue(
			        backgroundContext(),
			        [self, project_,
			         newSnapshot](const gostd::Context& ctx) {
				    struct derefGuard {
					    project::Snapshot* s;
					    ~derefGuard() { s->Deref(); }
				    } dg{newSnapshot};
				    logging::LogTree* logTree = nullptr;
				    if (self->options->LoggingEnabled) {
					    logTree = logging::newLogTree(
					        "Triggering ATA for project " +
					        std::string(project_->ID()));
				    }

				    auto typingsInfo =
				        project_->ComputeTypingsInfo();
				    ata::TypingsInstallRequest request;
				    request.ProjectID =
				        self->internATAProjectID(
				            project_->ID());
				    request.TypingsInfo = &typingsInfo;
				    for (auto* file :
				         project_->Program
				             ->GetSourceFiles()) {
					    request.FileNames.push_back(
					        file->FileName());
				    }
				    request.ProjectRootPath =
				        project_->currentDirectory;
				    request.CompilerOptions =
				        project_->CommandLine
				            ->CompilerOptions();
				    request.CurrentDirectory =
				        self->options->CurrentDirectory;
				    request.GetScriptKind =
				        tsc::getScriptKindFromFileName;
				    request.FS = self->fs;
				    request.Logger = logTree;

				    auto projectDisplayName =
				        project_->DisplayName(
				            self->options
				                ->CurrentDirectory);
				    if (self->client != nullptr) {
					    self->client->ProgressStart(
					        tsc::Installing_types_for_0,
					        {projectDisplayName});
				    }
				    auto [result, err] =
				        self->typingsInstaller
				            ->InstallTypings(&request);
				    if (self->client != nullptr) {
					    self->client->ProgressFinish(
					        tsc::Installing_types_for_0,
					        {projectDisplayName});
				    }
				    if (err != nullptr) {
					    if (logTree != nullptr) {
						    logging::log(self->logger, 
						        "ATA installation failed "
						        "for project " +
						        std::string(project_->ID()) +
						        ": " + err->Error());
						    logging::log(self->logger, 
						        logTree->String());
					    }
				    } else {
					    if (result->TypingsFiles !=
					        project_->typingsFiles) {
						    self->pendingATAChangesMu
						        .lock();
						    auto* change =
						        new ATAStateChange();
						    change->TypingsInfo =
						        new ata::TypingsInfo(
						            typingsInfo);
						    change->TypingsFiles =
						        result->TypingsFiles;
						    change->TypingsFilesToWatch =
						        result->FilesToWatch;
						    change->Logs = logTree;
						    self->pendingATAChanges
						        [project_->ID()] = change;
						    self->pendingATAChangesMu
						        .unlock();
						    self->ScheduleDiagnosticsRefresh();
					    }
				    }
			    })) {
				// Dropped — unwind the snapshot ref.
				newSnapshot->Deref();
			}
		}
	}
}

// warmAutoImportCache — session.go:2057.
void Session::warmAutoImportCache(const gostd::Context& ctx,
                                  const SnapshotChange& change,
                                  project::Snapshot* oldSnapshot,
                                  project::Snapshot* newSnapshot) {
	if (change.fileChanges.Changed.Len() == 1) {
		lsp::lsproto::DocumentUri changedFile;
		for (auto& uri :
		     change.fileChanges.Changed.Keys()) {
			changedFile = uri;
		}
		if (!newSnapshot->isOpenFile(
		        lsp::lsproto::documentUriFileName(
		            changedFile))) {
			return;
		}
		auto prefs = newSnapshot->UserPreferences();
		if (prefs.IncludeCompletionsForModuleExports == Tristate::False) {
			return;
		}
		auto* project =
		    newSnapshot->GetDefaultProject(changedFile);
		if (project == nullptr) {
			return;
		}
		if (newSnapshot->AutoImports
		        ->IsPreparedForImportingFile(
		            lsp::lsproto::documentUriFileName(
		                changedFile),
		            internProjectID(project->ID()), prefs)) {
			return;
		}

		// Cancel any previous auto-import warming and create a new
		// cancellable context. Only publish the new cancel func if
		// the derived context is still active, and make the stored
		// cancel func a no-op once that warming task is done.
		warmAutoImportMu.lock();
		if (warmAutoImportCancel != nullptr) {
			warmAutoImportCancel();
		}
		auto [warmCtx, cancel] = gostd::contextWithCancel(ctx);
		if (!gostd::ctxDone(warmCtx)) {
			auto warmCtxCopy = warmCtx;
			auto cancelCopy = cancel;
			warmAutoImportCancel =
			    [self = this, warmCtxCopy, cancelCopy,
			     changedFile]() {
				    if (gostd::ctxDone(warmCtxCopy)) {
					    return;
				    }
				    logging::log(self->logger, 
				        "Cancelling auto-import warming "
				        "for file " +
				        lsp::lsproto::
				            documentUriFileName(
				                changedFile));
				    cancelCopy();
			    };
		}
		warmAutoImportMu.unlock();

		if (gostd::ctxDone(warmCtx)) {
			cancel();
			return;
		}
		struct cancelGuard {
			gostd::CancelFunc cancel;
			~cancelGuard() { cancel(); }
		} guard{cancel};

		// Clone the snapshot with auto-imports using warmCtx so the
		// expensive extraction work is cancelled if a file change
		// arrives.
		if (!newSnapshot->tryRef()) {
			return;
		}
		struct derefGuard {
			project::Snapshot* s;
			~derefGuard() { s->Deref(); }
		} dg{newSnapshot};

		SnapshotChange warmChange;
		warmChange.reason =
		    UpdateReasonRequestedLanguageServiceWithAutoImports;
		warmChange.Documents = {changedFile};
		warmChange.AutoImports = changedFile;
		auto* clonedSnapshot =
		    newSnapshot->Clone(warmCtx, warmChange,
		                       newSnapshot->overlays(), logger,
		                       client);

		// If cancelled during clone, discard the incomplete
		// result.
		if (gostd::ctxDone(warmCtx)) {
			clonedSnapshot->Deref();
			return;
		}

		// Conditionally adopt: if the session hasn't moved past
		// newSnapshot, promote the clone so future requests
		// benefit from the warmed cache.
		adoptSnapshotChange(newSnapshot, clonedSnapshot);
	}
}

// APIUpdate — api.go:19.
std::pair<project::Snapshot*, gostd::Error> Session::APIUpdate(
    const gostd::Context& ctx,
    const FileChangeSummary& apiFileChanges,
    APISnapshotRequest* apiRequest) {
	std::lock_guard<std::mutex> lk(snapshotUpdateMu);
	cancelScheduledSnapshotUpdate();

	auto [hostFileChanges, overlays, ataChanges, _newConfig] =
	    flushChanges(ctx);
	auto fileChanges = hostFileChanges.Clone();
	mergeFileChangeSummary(&fileChanges, apiFileChanges);
	vfs::FS* fs = nullptr;
	bool replaceFileSystem = false;
	if (apiRequest != nullptr) {
		fs = apiRequest->FileSystem;
		replaceFileSystem = apiRequest->ReplaceFileSystem;
	}

	SnapshotChange change;
	change.apiRequest = apiRequest;
	change.fs = fs;
	change.fileSystemOverride = fs != nullptr;
	change.replaceFileSystem = replaceFileSystem;
	change.fileChanges = fileChanges;
	change.ataChanges = ataChanges;
	auto* newSnapshot =
	    updateSnapshotRef(ctx, std::move(overlays), change);
	if (newSnapshot->apiError != nullptr) {
		auto apiError = newSnapshot->apiError;
		newSnapshot->Deref();
		if (!hostFileChanges.IsEmpty() || !ataChanges.empty()) {
			// The API request is rejected as a unit, but host
			// changes were already flushed and must still
			// advance the canonical session snapshot.
			SnapshotChange hostChange;
			hostChange.fileChanges = hostFileChanges;
			hostChange.ataChanges = ataChanges;
			UpdateSnapshot(ctx, overlays, hostChange);
		}
		return {nullptr, apiError};
	}
	return {newSnapshot, gostd::Error{}};
}

} // namespace tsc::project
