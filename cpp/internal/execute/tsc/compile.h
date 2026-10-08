#pragma once

// compile.go — port of tsc/internal/execute/tsc/compile.go: the tsc CLI
// System abstraction, exit-status codes, command results and testing hooks,
// content mapper host creation, and compile timing buckets.

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "internal/collections/collections.h"
#include "internal/compiler/program.h"
#include "internal/contentmapper/contentmapper.h"
#include "internal/core/types.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/execute/incremental/incremental.h"
#include "internal/gostd/gostd.h"
#include "internal/locale/locale.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfs.h"

namespace tsc::execute::tsc {

// TraceFn — Go `func(msg *diagnostics.Message, args ...any)`; args are
// pre-stringified (the Go StringifyArgs step happens at call sites).
using TraceFn =
    std::function<void(const DiagnosticMessage*, const std::vector<std::string>&)>;

// System — compile.go:23. The process environment the CLI runs against:
// output writers, the vfs filesystem, terminal info, env vars, child-process
// spawning, and the monotonic clock.
//
// System is a tsoptions::ParseConfigHost (Go passes `sys` to
// tsoptions.ParseCommandLine / GetParsedCommandLineOfConfigFile) and a
// contentmapper::Spawner (Go passes `sys` to
// contentmapper.NewHostWithOptions). ParseConfigHost folds vfs.FS into the
// host: `FS()` returns `this`, so the vfs itself is exposed as `fs()`.
class System : public tsoptions::ParseConfigHost, public contentmapper::Spawner {
public:
	virtual std::ostream* Writer() = 0;
	virtual std::ostream* ErrorWriter() = 0;
	// fs — Go `FS() vfs.FS`. (Renamed: ParseConfigHost::FS() already returns
	// `this`, which mirrors the Go `host.FS().X()` call shape inside
	// tsoptions.)
	virtual std::shared_ptr<vfs::FS> fs() = 0;
	virtual std::string DefaultLibraryPath() = 0;
	virtual bool WriteOutputIsTTY() = 0;
	virtual int GetWidthOfTerminal() = 0;
	virtual std::pair<std::string, bool>
	GetEnvironmentVariable(std::string_view name) = 0;
	// Spawn — contentmapper::Spawner: compile.go:36. Read is the child's
	// stdout, Write its stdin; Close tears the process down.
	// Now — Go time.Time; system_clock here (vfs::TimePoint).
	virtual vfs::TimePoint Now() = 0;
	virtual gostd::Duration SinceStart() = 0;

	// module::ResolutionHost — forwarded to fs() so ParseConfigHost-style
	// calls (`host->FS()->FileExists(...)`) resolve through the vfs.
	bool FileExists(std::string_view path) override {
		return fs()->FileExists(std::string(path));
	}
	bool DirectoryExists(std::string_view path) override {
		return fs()->DirectoryExists(std::string(path));
	}
	std::optional<std::string> ReadFile(std::string_view path) override {
		auto [text, ok] = fs()->ReadFile(std::string(path));
		if (!ok) {
			return std::nullopt;
		}
		return text;
	}
	std::string Realpath(std::string_view path) override {
		return fs()->Realpath(std::string(path));
	}
	bool UseCaseSensitiveFileNames() override {
		return fs()->UseCaseSensitiveFileNames();
	}
	AccessibleEntries GetAccessibleEntries(std::string_view path) override {
		auto entries = fs()->GetAccessibleEntries(std::string(path));
		return {std::move(entries.files), std::move(entries.directories),
		        std::move(entries.symlinks)};
	}
};

// newContentMapperLogger — compile.go:40. Returns nil unless
// TS_CONTENT_MAPPER_DEBUG is set; logs lines to ErrorWriter under a mutex.
contentmapper::Logger newContentMapperLogger(System* sys);

// ExitStatus — compile.go:50.
enum class ExitStatus : int32_t {
	Success = 0,
	DiagnosticsPresent_OutputsSkipped = 1,
	DiagnosticsPresent_OutputsGenerated = 2,
	InvalidProject_OutputsSkipped = 3,
	ProjectReferenceCycle_OutputsSkipped = 4,
	NotImplemented = 5,
};
inline constexpr ExitStatus ExitStatusSuccess = ExitStatus::Success;
inline constexpr ExitStatus ExitStatusDiagnosticsPresent_OutputsSkipped =
    ExitStatus::DiagnosticsPresent_OutputsSkipped;
inline constexpr ExitStatus ExitStatusDiagnosticsPresent_OutputsGenerated =
    ExitStatus::DiagnosticsPresent_OutputsGenerated;
inline constexpr ExitStatus ExitStatusInvalidProject_OutputsSkipped =
    ExitStatus::InvalidProject_OutputsSkipped;
inline constexpr ExitStatus ExitStatusProjectReferenceCycle_OutputsSkipped =
    ExitStatus::ProjectReferenceCycle_OutputsSkipped;
inline constexpr ExitStatus ExitStatusNotImplemented =
    ExitStatus::NotImplemented;

// Watcher — compile.go:61.
struct Watcher {
	virtual ~Watcher() = default;
	virtual void DoCycle() = 0;
};

// CommandLineResult — compile.go:65.
struct CommandLineResult {
	ExitStatus Status = ExitStatusSuccess;
	Watcher* Watcher = nullptr;
};

// CommandLineTesting — compile.go:70. Optional hooks the CLI calls at
// boundaries; nil in production.
struct CommandLineTesting {
	virtual ~CommandLineTesting() = default;
	// Ensure that all emitted files are timestamped in order to ensure they
	// are deterministic for test baseline.
	virtual void OnEmittedFiles(
	    compiler::EmitResult* result,
	    collections::SyncMap<tspath::Path,
	                         std::filesystem::file_time_type>* mTimesCache) = 0;
	virtual void OnListFilesStart(std::ostream* w) = 0;
	virtual void OnListFilesEnd(std::ostream* w) = 0;
	virtual void OnStatisticsStart(std::ostream* w) = 0;
	virtual void OnStatisticsEnd(std::ostream* w) = 0;
	virtual void OnBuildStatusReportStart(std::ostream* w) = 0;
	virtual void OnBuildStatusReportEnd(std::ostream* w) = 0;
	virtual void OnWatchStatusReportStart() = 0;
	virtual void OnWatchStatusReportEnd() = 0;
	virtual TraceFn GetTrace(std::ostream* w, locale::Locale locale) = 0;
	virtual void OnProgram(incremental::Program* program) = 0;
};

// NewContentMapperHost — compile.go:90. Creates a content mapper host when
// content mappers are enabled via the --runExternalCode flag, spawning
// mapper processes through the system's Spawn. Returns null otherwise, in
// which case no content-mapped files can be loaded. The caller owns the host
// and must Close it when the compilation session ends.
std::shared_ptr<contentmapper::Host>
NewContentMapperHost(gostd::Context ctx, System* sys,
                     const CompilerOptions* options);

// CompileTimes — compile.go:102. Go package-privacy on bindTime/checkTime/
// totalTime/emitTime doesn't map; public here with the same semantics.
struct CompileTimes {
	gostd::Duration ConfigTime{0};
	gostd::Duration ParseTime{0};
	contentmapper::Timings ContentMapperTimes;
	gostd::Duration bindTime{0};
	gostd::Duration checkTime{0};
	gostd::Duration totalTime{0};
	gostd::Duration emitTime{0};
	gostd::Duration BuildInfoReadTime{0};
	gostd::Duration ChangesComputeTime{0};
};

// CompileAndEmitResult — compile.go:112.
struct CompileAndEmitResult {
	std::vector<Diagnostic*> Diagnostics;
	compiler::EmitResult* EmitResult = nullptr;
	ExitStatus Status = ExitStatusSuccess;
	CompileTimes* times = nullptr;
};

}  // namespace tsc::execute::tsc
