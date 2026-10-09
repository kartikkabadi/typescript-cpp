// tests_emit.cpp — port of
// tsc/internal/execute/tsc/emit_test.go.
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "internal/compiler/program.h"
#include "internal/core/utilities.h"
#include "internal/execute/incremental/incremental.h"
#include "internal/execute/tsc/diagnostics.h"
#include "internal/execute/tsc/emit.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace tsc {
namespace {
using namespace ::tsc::execute::tsc;
namespace etsc = ::tsc::execute::tsc;
namespace incremental = ::tsc::execute::incremental;
namespace vfstest = ::tsc::vfs::vfstest;


using gostd::testing::T;

// discardStream — io.Discard.
struct discardStream : std::ostream {
	struct nullbuf : std::streambuf {
		int overflow(int c) override { return c; }
	} buf;
	discardStream() : std::ostream(&buf) {}
};

struct controlledClock {
	std::mutex mu;
	vfs::TimePoint now;
	bool nestedEmitInProgress = false;
	int nestedEmitCalls = 0;

	vfs::TimePoint Now() {
		std::lock_guard<std::mutex> lock(mu);
		return now;
	}
	vfs::TimePoint NestedEmitNow() {
		std::lock_guard<std::mutex> lock(mu);
		nestedEmitCalls++;
		if (nestedEmitInProgress) {
			now += std::chrono::duration_cast<
			    std::chrono::system_clock::duration>(gostd::second());
		}
		nestedEmitInProgress = !nestedEmitInProgress;
		return now;
	}
	int NestedEmitCalls() {
		std::lock_guard<std::mutex> lock(mu);
		return nestedEmitCalls;
	}
	gostd::Duration SinceStart() { return gostd::Duration(0); }
};

struct fileClock : vfstest::Clock {
	std::mutex mu;
	vfs::TimePoint now;

	vfs::TimePoint Now() override {
		std::lock_guard<std::mutex> lock(mu);
		now += std::chrono::duration_cast<
		    std::chrono::system_clock::duration>(gostd::second());
		return now;
	}
	vfs::Duration SinceStart() override { return vfs::Duration(0); }
};

// timingTestSystem — emit_test.go.
struct timingTestSystem : etsc::System {
	std::shared_ptr<vfs::FS> fs_;
	controlledClock* clock = nullptr;

	discardStream discard;
	std::ostream* Writer() override { return &discard; }
	std::ostream* ErrorWriter() override { return &discard; }
	std::shared_ptr<vfs::FS> fs() override { return fs_; }
	std::string DefaultLibraryPath() override { return "/lib"; }
	std::string GetCurrentDirectory() override { return "/project"; }
	bool WriteOutputIsTTY() override { return false; }
	int GetWidthOfTerminal() override { return 0; }
	std::pair<std::string, bool>
	GetEnvironmentVariable(std::string_view name) override {
		return {"", false};
	}
	vfs::TimePoint Now() override { return clock->Now(); }
	gostd::Duration SinceStart() override {
		return clock->SinceStart();
	}
	std::pair<std::shared_ptr<gostd::io::ReadWriteCloser>, gostd::Error>
	Spawn(const std::vector<std::string>& command, const std::string& dir,
	      gostd::io::Writer* stderr) override {
		return {nullptr,
		        gostd::newError(
		            "spawn not implemented in timingTestSystem")};
	}
};

struct contentMapperLoggingTestSystem : timingTestSystem {
	bool enabled = false;
	std::ostringstream stderr_;

	std::pair<std::string, bool>
	GetEnvironmentVariable(std::string_view name) override {
		if (name == "TS_CONTENT_MAPPER_DEBUG" && enabled) {
			return {"1", true};
		}
		return {"", false};
	}
	std::ostream* ErrorWriter() override { return &stderr_; }
};

void TestContentMapperLoggerEnvironmentVariable(T* t) {
	t->Parallel();
	auto* sys = new contentMapperLoggingTestSystem();
	if (newContentMapperLogger(sys) != nullptr)
		t->Error({"expected nil logger"});
	sys->enabled = true;
	auto logger = newContentMapperLogger(sys);
	if (logger == nullptr)
		t->Fatal({"expected non-nil logger"});
	{
		std::vector<std::thread> threads;
		for (int i = 0; i < 10; i++) {
			threads.emplace_back(
			    [&logger] { logger("mapper log"); });
		}
		for (auto& th : threads)
			th.join();
	}
	std::string expected;
	for (int i = 0; i < 10; i++)
		expected += "mapper log\n";
	if (sys->stderr_.str() != expected)
		t->Error({gostd::sprintf("stderr = %q, want %q",
		                         {sys->stderr_.str(), expected})});
}
REGISTER_UNIT_TEST("tsc.TestContentMapperLoggerEnvironmentVariable",
                   TestContentMapperLoggerEnvironmentVariable);

void TestIncrementalDeclarationEmitTimeIsExcludedFromCheckTime(T* t) {
	t->Parallel();

	std::unordered_map<std::string, vfstest::MapFileInput> files{
	    {"/lib/lib.d.ts", std::string(R"(
interface Array<T> {}
interface Boolean {}
interface CallableFunction {}
interface Function {}
interface IArguments {}
interface NewableFunction {}
interface Number {}
interface Object {}
interface RegExp {}
interface String {}
)")},
	    {"/project/hub.ts", std::string(R"(
export interface Box {
    value: string;
}
export const make = (): Box => ({ value: "ok" });
)")},
	    {"/project/spoke.ts",
	     std::string(
	         "import { make, type Box } from \"./hub\"; export const "
	         "value: Box = make();")},
	};
	auto* clock =
	    new controlledClock{.now = std::chrono::system_clock::from_time_t(0)};
	auto* sys = new timingTestSystem();
	sys->fs_ = vfstest::FromMapWithClock(
	    files, true,
	    std::shared_ptr<vfstest::Clock>(new fileClock));
	sys->clock = clock;
	auto* options = new CompilerOptions{
	    .Declaration = Tristate::True,
	    .Incremental = Tristate::True,
	    .Module = ModuleKind::ESNext,
	    .NoEmit = Tristate::True,
	    .TsBuildInfoFile = "/project/tsconfig.tsbuildinfo",
	};
	auto* config = tsoptions::NewParsedCommandLine(
	    options,
	    {"/lib/lib.d.ts", "/project/hub.ts", "/project/spoke.ts"},
	    {},
	    tspath::ComparePathsOptions{.useCaseSensitiveFileNames = true,
	                                .currentDirectory = "/project"});

	auto compile = [&](incremental::Program* oldProgram)
	    -> std::pair<incremental::Program*, CompileTimes*> {
		auto* host = compiler::NewCachedFSCompilerHost(
		    sys->GetCurrentDirectory(), sys->fs(),
		    sys->DefaultLibraryPath(), nullptr, nullptr, nullptr);
		auto* program = compiler::NewProgram(
		    compiler::ProgramOptions{.Host = host, .Config = config});
		if (program->GetSourceFile("/lib/lib.d.ts") == nullptr) {
			t->Fatal({"default library was not loaded"});
		}
		auto* incrementalProgram = incremental::NewProgram(
		    program, oldProgram, incremental::CreateHost(host),
		    [clock] { return clock->NestedEmitNow(); }, false);
		auto* times = new CompileTimes();
		discardStream discard;
		EmitFilesAndReportErrors(etsc::EmitInput{
		    .Sys = sys,
		    .ProgramLike = incrementalProgram,
		    .Program = program,
		    .Config = config,
		    .ReportDiagnostic = QuietDiagnosticReporter,
		    .ReportErrorSummary = QuietDiagnosticsReporter,
		    .Writer = &discard,
		    .WriteFile =
		        [sys](const std::string& fileName, const std::string& text,
		              compiler::WriteFileData* data)
		        -> std::optional<std::string> {
			    auto err = sys->fs_->WriteFile(fileName, text);
			    if (err)
				    return err.str();
			    return std::nullopt;
		    },
		    .CompileTimes = times,
		});
		return {incrementalProgram, times};
	};
	auto [oldProgram, _times1] = compile(nullptr);
	if (auto err = sys->fs_->WriteFile(
	        "/project/hub.ts",
	        std::get<std::string>(files["/project/hub.ts"]) +
	            "\n// comment only change\n")) {
		t->Fatal({err.str()});
	}
	auto [_program, times] = compile(oldProgram);

	if (times->checkTime != gostd::Duration(0)) {
		t->Fatalf("check time = %v, want 0", {(int64_t)times->checkTime.count()});
	}
	if (times->emitTime != 2 * gostd::second()) {
		t->Fatalf("emit time = %v, want %v",
		          {(int64_t)times->emitTime.count(),
		           (int64_t)(2 * gostd::second()).count()});
	}
	if (int calls = clock->NestedEmitCalls(); calls != 4) {
		t->Fatalf("nested clock calls = %d, want 4", {calls});
	}
}
REGISTER_UNIT_TEST(
    "tsc.TestIncrementalDeclarationEmitTimeIsExcludedFromCheckTime",
    TestIncrementalDeclarationEmitTimeIsExcludedFromCheckTime);

} // namespace
} // namespace tsc
