#pragma once

// === dep stubs — removed when owner slice lands ===
// tsc.h — dep-stub decls for tsc/internal/execute/tsc, owned by the
// execute-tsc slice (tsc command-line driver: System, reporters,
// EmitAndReportStatistics, Statistics, CompileTimes, watch plumbing).
// Only what execute/build references today; real implementations land with
// the execute-tsc slice.
//
// Go context.Context params are dropped throughout (the port is
// single-threaded); Go `error` returns are gostd::Error (nil-able).

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/collections/collections.h"
#include "internal/compiler/program.h"
#include "internal/contentmapper/contentmapper.h"
#include "internal/core/types.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/execute/incremental/incremental.h"
#include "internal/gostd/gostd.h"
#include "internal/locale/locale.h"
#include "internal/tracing/tracing.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfs.h"

namespace tsc::execute::tsc {

// ===========================================================================
// compile.go — System / ExitStatus / Watcher / CommandLineResult
// ===========================================================================

// compile.go — System interface. Spawn comes from contentmapper::Spawner so a
// System can be passed to contentmapper::NewHostWithOptions unchanged.
struct System : contentmapper::Spawner {
	virtual ~System() = default;
	virtual gostd::io::Writer* Writer() = 0;
	virtual gostd::io::Writer* ErrorWriter() = 0;
	virtual vfs::FS* FS() = 0;
	virtual std::string DefaultLibraryPath() = 0;
	virtual std::string GetCurrentDirectory() = 0;
	virtual bool WriteOutputIsTTY() = 0;
	virtual int GetWidthOfTerminal() = 0;
	virtual std::pair<std::string, bool>
	GetEnvironmentVariable(const std::string& name) = 0;
	// Spawn — inherited from contentmapper::Spawner.
	virtual vfs::TimePoint Now() = 0;              // time.Time
	virtual gostd::Duration SinceStart() = 0;      // time.Duration
};

// compile.go — ExitStatus.
using ExitStatus = int;
inline constexpr ExitStatus ExitStatusSuccess = 0;
inline constexpr ExitStatus ExitStatusDiagnosticsPresent_OutputsSkipped = 1;
inline constexpr ExitStatus ExitStatusDiagnosticsPresent_OutputsGenerated = 2;
inline constexpr ExitStatus ExitStatusInvalidProject_OutputsSkipped = 3;
inline constexpr ExitStatus ExitStatusProjectReferenceCycle_OutputsSkipped = 4;
inline constexpr ExitStatus ExitStatusNotImplemented = 5;

// compile.go — Watcher interface.
struct Watcher {
	virtual ~Watcher() = default;
	virtual void DoCycle() = 0;
};

// compile.go — CommandLineResult.
struct CommandLineResult {
	ExitStatus Status = 0;
	Watcher* Watcher = nullptr;
};

// compile.go — CommandLineTesting interface.
struct CommandLineTesting {
	virtual ~CommandLineTesting() = default;
	// Ensure that all emitted files are timestamped in order to ensure they
	// are deterministic for test baseline.
	virtual void OnEmittedFiles(
	    compiler::EmitResult* result,
	    collections::SyncMap<tspath::Path,
	                         std::filesystem::file_time_type>* mTimesCache) = 0;
	virtual void OnListFilesStart(gostd::io::Writer* w) = 0;
	virtual void OnListFilesEnd(gostd::io::Writer* w) = 0;
	virtual void OnStatisticsStart(gostd::io::Writer* w) = 0;
	virtual void OnStatisticsEnd(gostd::io::Writer* w) = 0;
	virtual void OnBuildStatusReportStart(gostd::io::Writer* w) = 0;
	virtual void OnBuildStatusReportEnd(gostd::io::Writer* w) = 0;
	virtual void OnWatchStatusReportStart() = 0;
	virtual void OnWatchStatusReportEnd() = 0;
	virtual std::function<void(
	    const DiagnosticMessage*, const std::vector<std::string>&)>
	GetTrace(gostd::io::Writer* w, locale::Locale locale) = 0;
	virtual void OnProgram(incremental::Program* program) = 0;
};

// compile.go — CompileTimes.
struct CompileTimes {
	gostd::Duration ConfigTime{0};
	gostd::Duration ParseTime{0};
	contentmapper::Timings ContentMapperTimes;
	gostd::Duration bindTime{0};   // unexported in Go
	gostd::Duration checkTime{0};  // unexported in Go
	gostd::Duration totalTime{0};  // unexported in Go
	gostd::Duration emitTime{0};   // unexported in Go
	gostd::Duration BuildInfoReadTime{0};
	gostd::Duration ChangesComputeTime{0};
};

// compile.go — CompileAndEmitResult.
struct CompileAndEmitResult {
	std::vector<Diagnostic*> Diagnostics;
	compiler::EmitResult* EmitResult = nullptr;
	ExitStatus Status = 0;
	CompileTimes* times = nullptr;  // unexported in Go
};

// ===========================================================================
// statistics.go — Statistics
// ===========================================================================

struct Statistics {
	bool isAggregate = false;       // unexported in Go
	int Projects = 0;
	int ProjectsBuilt = 0;
	int TimestampUpdates = 0;
	int files = 0;                  // unexported in Go
	int lines = 0;                  // unexported in Go
	int identifiers = 0;            // unexported in Go
	int symbols = 0;                // unexported in Go
	int types = 0;                  // unexported in Go
	int instantiations = 0;         // unexported in Go
	uint64_t memoryUsed = 0;        // unexported in Go
	uint64_t memoryAllocs = 0;      // unexported in Go
	CompileTimes* compileTimes = nullptr;  // unexported in Go

	void Report(gostd::io::Writer* w, CommandLineTesting* testing);
	void Aggregate(Statistics* stat);
	void SetTotalTime(gostd::Duration totalTime);
};

// ===========================================================================
// diagnostics.go — reporters
// ===========================================================================

// diagnostics.go:23 DiagnosticReporter.
using DiagnosticReporter = std::function<void(Diagnostic*)>;

// diagnostics.go:25 QuietDiagnosticReporter — intentionally a no-op.
void QuietDiagnosticReporter(Diagnostic* diagnostic);

// diagnostics.go:27 CreateDiagnosticReporter.
DiagnosticReporter CreateDiagnosticReporter(
    System* sys, gostd::io::Writer* w, locale::Locale locale,
    CompilerOptions* options);

// diagnostics.go:136 DiagnosticsReporter.
using DiagnosticsReporter =
    std::function<void(const std::vector<Diagnostic*>&)>;

// diagnostics.go:138 QuietDiagnosticsReporter — intentionally a no-op.
void QuietDiagnosticsReporter(const std::vector<Diagnostic*>& diagnostics);

// diagnostics.go:140 CreateReportErrorSummary.
DiagnosticsReporter CreateReportErrorSummary(
    System* sys, locale::Locale locale, CompilerOptions* options);

// diagnostics.go:150 CreateBuilderStatusReporter.
DiagnosticReporter CreateBuilderStatusReporter(
    System* sys, gostd::io::Writer* w, locale::Locale locale,
    CompilerOptions* options, CommandLineTesting* testing);

// diagnostics.go:168 CreateWatchStatusReporter.
DiagnosticReporter CreateWatchStatusReporter(
    System* sys, locale::Locale locale, CompilerOptions* options,
    CommandLineTesting* testing);

// ===========================================================================
// emit.go — tracing + emit entry point
// ===========================================================================

// emit.go:21 GetTraceWithWriterFromSys — returns the trace fn (or nil).
std::function<void(const DiagnosticMessage*,
                   const std::vector<std::string>&)>
GetTraceWithWriterFromSys(gostd::io::Writer* w, locale::Locale locale,
                          CommandLineTesting* testing);

// emit.go:31 EmitInput.
struct EmitInput {
	System* Sys = nullptr;
	compiler::ProgramLike* ProgramLike = nullptr;
	compiler::SimpleProgram* Program = nullptr;
	tsoptions::ParsedCommandLine* Config = nullptr;
	DiagnosticReporter ReportDiagnostic;
	DiagnosticsReporter ReportErrorSummary;
	gostd::io::Writer* Writer = nullptr;
	compiler::WriteFile WriteFile;
	CompileTimes* CompileTimes = nullptr;
	CommandLineTesting* Testing = nullptr;
	collections::SyncMap<tspath::Path, std::filesystem::file_time_type>*
	    TestingMTimesCache = nullptr;
	tracing::Tracing* Tracing = nullptr;
};

// emit.go:46 EmitAndReportStatistics.
std::pair<CompileAndEmitResult, Statistics*>
EmitAndReportStatistics(const EmitInput& input);

// ===========================================================================
// compile.go — NewContentMapperHost
// ===========================================================================

// compile.go:89 NewContentMapperHost — Go's ctx param dropped per port
// convention.
std::shared_ptr<contentmapper::Host>
NewContentMapperHost(System* sys, CompilerOptions* options);

// ===========================================================================
// extendedconfigcache.go — ExtendedConfigCache
// ===========================================================================

// extendedconfigcache.go — minimal implementation of
// tsoptions::ExtendedConfigCache; concurrency-safe, stores entries
// permanently.
class ExtendedConfigCache : public tsoptions::ExtendedConfigCache {
	struct entry {
		tsoptions::ExtendedConfigCacheEntry* value = nullptr;
		std::mutex mu;
	};

public:
	tsoptions::ExtendedConfigCacheEntry* GetExtendedConfig(
	    std::string_view fileName, const tspath::Path& path,
	    const std::vector<tspath::Path>& resolutionStack,
	    tsoptions::ParseConfigHost* host) override;

private:
	collections::SyncMap<tspath::Path, entry*> m;
};

} // namespace tsc::execute::tsc
