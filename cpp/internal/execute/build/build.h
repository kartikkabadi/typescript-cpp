#pragma once

// Port of tsc/internal/execute/build — the `tsc -b/--build` project-graph
// build orchestrator (task graph, up-to-date status, per-project compiler
// hosts, parse cache).
//
// Go context.Context params are dropped throughout (the port is
// single-threaded); Go `error` returns are gostd::Error or
// std::optional<std::string> per the callee API; iter.Seq values are
// yield-callback parameters. Goroutines/channels map to
// std::thread + mutex + condition_variable preserving observable semantics.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/collections/collections.h"
#include "internal/compiler/program.h"
#include "internal/contentmapper/contentmapper.h"
#include "internal/core/types.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/execute/incremental/incremental.h"
#include "internal/execute/tsc/tsc.h"
#include "internal/execute/watchmanager/watchmanager.h"
#include "internal/fswatch/fswatch.h"
#include "internal/gostd/gostd.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfs.h"

namespace tsc {

// ast.go — SourceFileParseOptions serves as the parseCache key (Go
// `comparable`); operator== lives on the structs in ast.h (this TU once
// owned them — moved when program.cpp started comparing ParseOptions
// for canReplaceFileInProgram, program.go:466).

} // namespace tsc

namespace tsc::execute::build {

namespace etsc = ::tsc::execute::tsc;

// build-domain times are filesystem mtimes (file_clock); Sys.Now() values
// are converted via std::chrono::file_clock::from_sys.
using fileTime = std::filesystem::file_time_type;

// fileTimeZero stands in for Go's time.Time{} zero value: the minimum
// representable time. `fileTime{}` would be the file_clock *epoch* (a real
// timestamp that sorts after converted sys times on libstdc++, whose epoch
// is 2174), so it cannot be used where Go relies on zero-time semantics.
inline constexpr fileTime fileTimeZero = fileTime::min();

// ===========================================================================
// parseCache.go — parseCache
// ===========================================================================

// parseCache.go:9 parseCacheEntry.
template <typename V>
struct parseCacheEntry {
	V value{};
	std::mutex mu;
};

// parseCache.go:14 parseCache.
template <typename K, typename V, typename Hash = std::hash<K>>
struct parseCache {
	collections::SyncMap<K, parseCacheEntry<V>*, Hash> entries;

	// loadOrStore — parse once per key under the entry's mutex; zero values
	// count as a hit only when allowZero.
	V loadOrStore(const K& key, const std::function<V(const K&)>& parse,
	              bool allowZero) {
		auto* newEntry = new parseCacheEntry<V>();
		newEntry->mu.lock();
		std::unique_lock<std::mutex> unlockNewEntry(newEntry->mu,
		                                            std::adopt_lock);
		if (auto [entry, loaded] = entries.LoadOrStore(key, newEntry);
		    loaded) {
			entry->mu.lock();
			std::unique_lock<std::mutex> unlockEntry(entry->mu,
			                                         std::adopt_lock);
			if (allowZero || entry->value != V{}) {
				return entry->value;
			}
			newEntry = entry;
		}
		newEntry->value = parse(key);
		return newEntry->value;
	}

	void store(const K& key, V value) {
		auto* e = new parseCacheEntry<V>();
		e->value = value;
		entries.Store(key, e);
	}

	void del(const K& key) { entries.Delete(key); }

	void reset() {
		entries = collections::SyncMap<K, parseCacheEntry<V>*, Hash>{};
	}
};

// parseCache key hash for ast.SourceFileParseOptions.
struct sourceFileParseOptionsHash {
	size_t operator()(const SourceFileParseOptions& o) const {
		size_t h = std::hash<std::string>{}(o.FileName);
		h ^= std::hash<std::string>{}(o.Path) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<int>{}(static_cast<int>(
		         o.ExternalModuleIndicatorOptions.JSX)) +
		     0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<bool>{}(o.ExternalModuleIndicatorOptions.Force) +
		     0x9e3779b9 + (h << 6) + (h >> 2);
		return h;
	}
};

// ===========================================================================
// uptodatestatus.go — upToDateStatus
// ===========================================================================

// uptodatestatus.go:5 upToDateStatusType.
enum class upToDateStatusType : uint16_t {
	// Errors:

	// config file was not found
	ConfigFileNotFound = 0,
	// found errors during build
	BuildErrors,
	// did not build because upstream project has errors - and we have option
	// to stop build on upstream errors
	UpstreamErrors,

	// Its all good, no work to do
	UpToDate,

	// Pseudo-builds - touch timestamps, no actual build:

	// The project appears out of date because its upstream inputs are newer
	// than its outputs, but all of its outputs are actually newer than the
	// previous identical outputs of its (.d.ts) inputs. This means we can
	// Pseudo-build (just touch timestamps), as if we had actually built this
	// project.
	UpToDateWithUpstreamTypes,
	// The project appears up to date and even though input file changed,
	// its text didnt so just need to update timestamps
	UpToDateWithInputFileText,

	// Needs build:

	// input file is missing
	InputFileMissing,
	// output file is missing
	OutputMissing,
	// input file is newer than output file
	InputFileNewer,
	// build info is out of date as we need to emit some files
	OutOfDateBuildInfoWithPendingEmit,
	// build info indicates that project has errors and they need to be
	// reported
	OutOfDateBuildInfoWithErrors,
	// build info options indicate there is work to do based on changes in
	// options
	OutOfDateOptions,
	// file was root when built but not any more
	OutOfDateRoots,
	// buildInfo.version mismatch with current ts version
	TsVersionOutputOfDate,
	// build because --force was specified
	ForceBuild,

	// solution file
	Solution,
};

// uptodatestatus.go:54 inputOutputName.
struct inputOutputName {
	std::string input;
	std::string output;
};

// uptodatestatus.go:59 fileAndTime.
struct fileAndTime {
	std::string file;
	fileTime time = fileTimeZero;
};

// uptodatestatus.go:64 inputOutputFileAndTime.
struct inputOutputFileAndTime {
	fileAndTime input;
	fileAndTime output;
	std::string buildInfo;
};

// uptodatestatus.go:70 upstreamErrors.
struct upstreamErrors {
	std::string ref;
	bool refHasUpstreamErrors = false;
};

// uptodatestatus.go:75 upToDateStatus — `data` is Go `any`, modelled as a
// variant of the payload types stored at each kind.
struct upToDateStatus {
	upToDateStatusType kind;
	std::variant<std::monostate, std::string, inputOutputName,
	             inputOutputFileAndTime, upstreamErrors>
	    data;

	bool isError() const;
	bool isPseudoBuild() const;
	inputOutputFileAndTime* inputOutputFileAndTimeData();
	inputOutputName* inputOutputNameData();
	std::string oldestOutputFileName();
	upstreamErrors* upstreamErrorsData();
};

// ===========================================================================
// buildtask.go — BuildTask / taskResult / buildKind
// ===========================================================================

// buildtask.go:25 buildKind.
using buildKind = unsigned;
inline constexpr buildKind buildKindNone = 0;
inline constexpr buildKind buildKindPseudo = 1;
inline constexpr buildKind buildKindProgram = 2;

struct BuildTask;

// buildtask.go:33 upstreamTask.
struct upstreamTask {
	BuildTask* task = nullptr;
	int refIndex = 0;
};

// buildtask.go:37 buildInfoEntry.
struct buildInfoEntry {
	incremental::BuildInfo* buildInfo = nullptr;
	tspath::Path path;
	fileTime mTime = fileTimeZero;
	std::optional<fileTime> dtsTime;
};

// strings.Builder stand-in — an std::ostream over a std::string buffer.
struct stringWriter : std::ostream {
	std::stringbuf b;
	stringWriter() : std::ostream(&b) {}
	std::string str() const { return b.str(); }
};

// buildtask.go:44 taskResult.
struct taskResult {
	stringWriter builder;
	etsc::DiagnosticReporter reportStatus;
	etsc::DiagnosticReporter diagnosticReporter;
	etsc::ExitStatus exitStatus = etsc::ExitStatusSuccess;
	etsc::Statistics* statistics = nullptr;
	incremental::Program* program = nullptr;
	buildKind kind_ = buildKindNone;
	std::vector<std::string> filesToDelete;
};

// chan struct{} closed-broadcast — Go `chan struct{}`/`close(ch)`/`<-ch`.
struct closeChan {
	std::mutex mu;
	std::condition_variable cv;
	bool closed = false;
	void close() {
		{
			std::lock_guard<std::mutex> lock(mu);
			closed = true;
		}
		cv.notify_all();
	}
	void wait() {
		std::unique_lock<std::mutex> lock(mu);
		cv.wait(lock, [&] { return closed; });
	}
};

struct Orchestrator;
struct OrchestratorResult;

// buildtask.go:55 BuildTask.
struct BuildTask {
	std::string config;
	tsoptions::ParsedCommandLine* resolved = nullptr;
	std::vector<upstreamTask*> upStream;
	std::vector<BuildTask*> downStream; // Only set and used in watch mode
	upToDateStatus* status = nullptr;
	std::shared_ptr<closeChan> done;

	// task reporting
	taskResult* result = nullptr;
	std::shared_ptr<closeChan> built; // closed when result is ready to be reported

	buildInfoEntry* buildInfoEntry_ = nullptr;
	std::mutex buildInfoEntryMu;
	std::vector<std::string> packageJsons;

	std::vector<Diagnostic*> errors;
	std::atomic<bool> pending{false};
	bool isInitialCycle = false;
	std::mutex downStreamUpdateMu;
	bool dirty = false;

	std::once_flag contentMapperProjectOnce;
	std::shared_ptr<contentmapper::Project> contentMapperProject;
	gostd::Error contentMapperProjectErr;

	std::pair<contentmapper::Project*, gostd::Error>
	getContentMapperProject(Orchestrator* orchestrator);
	void refreshContentMapperProject(Orchestrator* orchestrator);
	void waitOnUpstream();
	void unblockDownstream();
	void reportDiagnostic(Diagnostic* err);
	void report(Orchestrator* orchestrator, const tspath::Path& configPath,
	            OrchestratorResult* buildResult);
	void buildProject(Orchestrator* orchestrator, const tspath::Path& path);
	void updateDownstream(Orchestrator* orchestrator, const tspath::Path& path);
	void compileAndEmit(Orchestrator* orchestrator, const tspath::Path& path);
	bool handleStatusThatDoesntRequireBuild(Orchestrator* orchestrator);
	upToDateStatus* getUpToDateStatus(Orchestrator* orchestrator,
	                                  const tspath::Path& configPath);
	void reportUpToDateStatus(Orchestrator* orchestrator);
	bool canUpdateJsDtsOutputTimestamps();
	void updateTimeStamps(Orchestrator* orchestrator,
	                      const std::vector<std::string>& emittedFiles,
	                      const DiagnosticMessage* verboseMessage);
	void cleanProject(Orchestrator* orchestrator, const tspath::Path& path);
	void cleanProjectOutput(Orchestrator* orchestrator,
	                        const std::string& outputFile,
	                        collections::Set<tspath::Path>* inputs);
	void updateWatch(
	    Orchestrator* orchestrator,
	    collections::SyncMap<tspath::Path, fileTime>* oldCache);
	void resetStatus();
	void resetConfig(Orchestrator* orchestrator, const tspath::Path& path);
	std::pair<incremental::BuildInfo*, fileTime> loadOrStoreBuildInfo(
	    Orchestrator* orchestrator, const tspath::Path& configPath,
	    const std::string& buildInfoFileName);
	void onBuildInfoEmit(Orchestrator* orchestrator,
	                     const std::string& buildInfoFileName,
	                     incremental::BuildInfo* buildInfo,
	                     bool hasChangedDtsFile);
	bool hasConflictingBuildInfo(Orchestrator* orchestrator,
	                             BuildTask* upstream);
	fileTime getLatestChangedDtsMTime(Orchestrator* orchestrator);
	bool storeOutputTimeStamp(Orchestrator* orchestrator);
	std::optional<std::string> writeFile(Orchestrator* orchestrator,
	                                     const std::string& fileName,
	                                     const std::string& text,
	                                     compiler::WriteFileData* data);
};

// ===========================================================================
// host.go — host
// ===========================================================================

// host.go:18 host — wraps the per-session compiler host with build-cycle
// caches. Implements incremental::Host + incremental::BuildInfoReader.
struct host : incremental::Host, incremental::BuildInfoReader {
	Orchestrator* orchestrator = nullptr;
	compiler::CompilerHost* host_ = nullptr; // Go `host compiler.CompilerHost`

	// Caches that last only for build cycle and then cleared out
	std::unique_ptr<etsc::ExtendedConfigCache> extendedConfigCache =
	    std::make_unique<etsc::ExtendedConfigCache>();
	parseCache<SourceFileParseOptions, SourceFile*, sourceFileParseOptionsHash>
	    sourceFiles;
	collections::SyncMap<tspath::Path, gostd::Duration> configTimes;

	// caches that stay as long as they are needed
	parseCache<tspath::Path, tsoptions::ParsedCommandLine*> resolvedReferences;
	collections::SyncMap<tspath::Path, fileTime>* mTimes = nullptr;

	// incremental::Host — Go's Host.FS() vfs.FS is adapted in C++ to return
	// the compiler host.
	compiler::CompilerHost* FS() override { return host_; }
	std::filesystem::file_time_type
	GetMTime(const std::string& fileName) override {
		return loadOrStoreMTime(fileName, nullptr, true);
	}
	std::optional<std::string>
	SetMTime(const std::string& fileName,
	         std::filesystem::file_time_type mTime) override;

	// incremental::BuildInfoReader
	incremental::BuildInfo*
	ReadBuildInfo(tsoptions::ParsedCommandLine* config) override;

	std::string DefaultLibraryPath() { return host_->DefaultLibraryPath(); }
	std::string GetCurrentDirectory() { return host_->GetCurrentDirectory(); }
	void Trace(const DiagnosticMessage* msg,
	           const std::vector<std::string>& args);
	SourceFile* GetSourceFile(const SourceFileParseOptions& opts);
	std::pair<contentmapper::SourceFiles, gostd::Error>
	GetContentMappedSourceFiles(const SourceFileParseOptions& parseOptions,
	                            contentmapper::Mapper* mapper);
	contentmapper::Project* ContentMapperProject();
	tsoptions::ParsedCommandLine*
	GetResolvedProjectReference(const std::string& fileName,
	                            const tspath::Path& path);
	fileTime
	loadOrStoreMTime(const std::string& file,
	                 collections::SyncMap<tspath::Path, fileTime>* oldCache,
	                 bool store);
	void storeMTime(const std::string& file, fileTime mTime);
	void storeMTimeFromOldCache(
	    const std::string& file,
	    collections::SyncMap<tspath::Path, fileTime>* oldCache);
};

// ===========================================================================
// compilerHost.go — compilerHost
// ===========================================================================

// compilerHost.go:13 compilerHost — per-project compiler host that forwards
// through the orchestrator's host caches.
struct compilerHost : compiler::CompilerHost {
	build::host* host_ = nullptr; // Go `host *host`
	std::function<void(const DiagnosticMessage*,
	                   const std::vector<std::string>&)>
	    trace;

	// compilerHost.go:33 Trace.
	void Trace(const DiagnosticMessage* msg,
	           const std::vector<std::string>& args) override {
		trace(msg, args);
	}

	// compilerHost.go:37 GetSourceFile.
	SourceFile* GetSourceFile(const SourceFileParseOptions& opts,
	                          SourceFileMetaData metaData) override;
	// compilerHost.go:41 GetContentMappedSourceFiles.
	std::pair<contentmapper::SourceFiles, gostd::Error>
	GetContentMappedSourceFiles(const SourceFileParseOptions& parseOptions,
	                            contentmapper::Mapper* mapper) override;
	// compilerHost.go:56 ContentMapperProject.
	contentmapper::Project* ContentMapperProject() const override;
	// compilerHost.go:60 GetResolvedProjectReference.
	tsoptions::ParsedCommandLine*
	GetResolvedProjectReference(const std::string& fileName,
	                            const tspath::Path& path) override;
};

// ===========================================================================
// orchestrator.go — Options / OrchestratorResult / Orchestrator
// ===========================================================================

// orchestrator.go:27 Options.
struct Options {
	etsc::System* Sys = nullptr;
	tsoptions::ParsedBuildCommandLine* Command = nullptr;
	etsc::CommandLineTesting* Testing = nullptr;
};

// orchestrator.go:32 OrchestratorResult.
struct OrchestratorResult {
	etsc::CommandLineResult Result;
	std::vector<Diagnostic*> Errors;
	etsc::Statistics Statistics;
	std::vector<std::string> FilesToDelete;

	void report(Orchestrator* o);
	void reportWithFilesToDelete(Orchestrator* o, bool reportFilesToDelete);
};

// orchestrator.go:66 Orchestrator.
struct Orchestrator : etsc::Watcher {
	Options opts;
	tspath::ComparePathsOptions comparePathsOptions;
	build::host* host_ = nullptr;

	// contentMapperHost transforms content-mapped files; it is created once
	// per build session (when enabled) and shared across all projects so
	// mapper processes are consolidated. It closes itself when the session
	// context is cancelled (see contentmapper.New).
	std::shared_ptr<contentmapper::Host> contentMapperHost;

	// order generation result
	collections::SyncMap<tspath::Path, BuildTask*>* tasks = nullptr;
	std::vector<std::string> order;
	std::vector<Diagnostic*> errors;
	bool graphGenerated = false;

	etsc::DiagnosticsReporter errorSummaryReporter;
	etsc::DiagnosticReporter watchStatusReporter;

	// fswatch event-based watching
	watchmanager::WatchManager* wm = nullptr;
	// order sorted by dependency depth, to reduce how often builders block on
	// upstream projects
	std::vector<std::string> scheduleOrder;

	std::string relativeFileName(const std::string& fileName);
	tspath::Path toPath(const std::string& fileName);
	std::string resolveBuildInfoFileName(const std::string& fileName,
	                                     const std::string& buildInfoDir);
	std::vector<std::string> Order() { return order; }
	// ScheduleOrder is the order in which builders pick up projects:
	// Order() stably sorted by dependency depth.
	std::vector<std::string> ScheduleOrder() { return scheduleOrder; }
	std::vector<std::string> computeScheduleOrder();
	std::vector<std::string> Upstream(const std::string& configName);
	std::vector<std::string> Downstream(const std::string& configName);
	BuildTask* getTask(const tspath::Path& path);
	void createBuildTasks(
	    collections::SyncMap<tspath::Path, BuildTask*>* oldTasks,
	    const std::vector<std::string>& configs, struct workGroup* wg);
	BuildTask* setupBuildTask(const std::string& configName,
	                          BuildTask* downStream, bool inCircularContext,
	                          collections::Set<tspath::Path>* completed,
	                          collections::Set<tspath::Path>* analyzing,
	                          std::vector<std::string> circularityStack);
	void GenerateGraphReusingOldTasks();
	void GenerateGraph(collections::SyncMap<tspath::Path, BuildTask*>* oldTasks);
	// tsc -b entrypoint
	etsc::CommandLineResult Start();
	// orchestrator.Build() entrypoint for api
	OrchestratorResult* Build(const std::string& project);
	// orchestrator.BuildReferences() entrypoint for api
	OrchestratorResult* BuildReferences(const std::string& project);
	OrchestratorResult* start(const std::string& project, bool onlyReferences);
	void recheckAllProjects(const std::string& project);
	// orchestrator.Clean() entrypoint for api
	OrchestratorResult* Clean(const std::string& project);
	// orchestrator.CleanReferences() entrypoint for api
	OrchestratorResult* CleanReferences(const std::string& project);
	OrchestratorResult* clean(const std::string& project, bool onlyReferences);
	std::pair<std::vector<std::string>, bool>
	getBuildOrderFor(const std::string& project);
	bool cleanProjectOutput(const std::string& outputFile,
	                        collections::Set<tspath::Path>* inputs, bool dry,
	                        std::vector<std::string>* filesToDelete,
	                        etsc::DiagnosticReporter reportDiagnostic);
	void Watch();
	void updateWatch();
	void resetCaches();
	void checkTasksForEventChanges(
	    const std::unordered_map<std::string, fswatch::EventKind>& changedPaths,
	    std::atomic<bool>* needsConfigUpdate, std::atomic<bool>* needsUpdate);
	bool packageJsonLookupChanged(
	    const std::string& packageJson,
	    const std::unordered_map<tspath::Path, fswatch::EventKind>&
	        changedPaths);
	std::unordered_map<std::string, bool> computeDesiredWatches();
	void addWatchDir(watchmanager::DirWatchSet* desiredDirs,
	                 const std::string& dir);
	void addPackageJsonWatchDirs(watchmanager::DirWatchSet* desiredDirs,
	                             const std::string& packageJson);
	void DoCycle() override;
	etsc::CommandLineResult buildOrClean();
	OrchestratorResult* buildOrCleanOrder(const std::vector<std::string>& order);
	void rangeTask(
	    const std::function<void(const tspath::Path&, BuildTask*)>& f);
	void rangeTasks(
	    const std::vector<std::string>& order,
	    const std::function<void(const tspath::Path&, BuildTask*)>& f);
	void buildOrCleanProject(BuildTask* task, const tspath::Path& path);
	std::ostream* getWriter(BuildTask* task);
	etsc::DiagnosticReporter createBuilderStatusReporter(BuildTask* task);
	etsc::DiagnosticReporter createDiagnosticReporter(BuildTask* task);
};

// orchestrator.go:991 NewOrchestrator.
Orchestrator* NewOrchestrator(Options opts);

} // namespace tsc::execute::build
