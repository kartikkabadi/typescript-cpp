// emit.go — port of tsc/internal/execute/tsc/emit.go.

#include "internal/execute/tsc/emit.h"

#include "internal/execute/tsc/statistics.h"
#include "internal/pprof/pprof.h"
#include "internal/tspath/tspath.h"

#if defined(__GLIBC__)
#include <malloc.h>
#elif defined(__APPLE__)
#include <malloc/malloc.h>
#endif

namespace tsc::execute::tsc {

// GetTraceWithWriterFromSys — emit.go:21.
TraceFn GetTraceWithWriterFromSys(std::ostream* w, locale::Locale locale,
                                  CommandLineTesting* testing) {
	if (testing == nullptr) {
		return [w, locale](const DiagnosticMessage* msg,
		                   const std::vector<std::string>& args) {
			*w << ::tsc::localize(locale, msg, "", args) << '\n';
		};
	}
	return testing->GetTrace(w, locale);
}

// readMemStatsApprox — runtime.MemStats: Go reports `Alloc` (bytes of heap
// currently allocated) and `Mallocs` (cumulative allocation count). The C++
// runtime has no GC; mallinfo2 gives the closest in-use figure.
struct memStats {
	uint64_t Alloc = 0;
	uint64_t Mallocs = 0;
};

static memStats readMemStats() {
	memStats s;
#if defined(__GLIBC__)
	struct mallinfo2 mi = mallinfo2();
	s.Alloc = static_cast<uint64_t>(mi.uordblks);
#elif defined(__APPLE__)
	// malloc_statistics(3): size_in_use is the closest analog of
	// mallinfo2's uordblks (bytes currently in use by the default zone).
	malloc_statistics_t ms;
	malloc_zone_statistics(malloc_default_zone(), &ms);
	s.Alloc = static_cast<uint64_t>(ms.size_in_use);
#endif
	return s;
}

// EmitAndReportStatistics — emit.go:47.
std::pair<CompileAndEmitResult, Statistics*>
EmitAndReportStatistics(const EmitInput& input) {
	Statistics* statistics = nullptr;
	auto result = EmitFilesAndReportErrors(input);
	if (result.Status != ExitStatusSuccess) {
		// compile exited early
		return {result, nullptr};
	}
	result.times->totalTime = input.Sys->SinceStart();

	if (tristateIsTrue(input.Config->CompilerOptions()->Diagnostics) ||
	    tristateIsTrue(input.Config->CompilerOptions()->ExtendedDiagnostics)) {
		// GC must be called twice to allow things to settle.
		pprof::runGC();
		pprof::runGC();
		auto memStats = readMemStats();

		statistics = statisticsFromProgram(input, memStats.Alloc,
		                                   memStats.Mallocs);
		statistics->Report(input.Writer, input.Testing);
	}

	if (result.EmitResult->EmitSkipped && !result.Diagnostics.empty()) {
		result.Status = ExitStatusDiagnosticsPresent_OutputsSkipped;
	} else if (!result.Diagnostics.empty()) {
		result.Status = ExitStatusDiagnosticsPresent_OutputsGenerated;
	}
	return {result, statistics};
}

// EmitFilesAndReportErrors — emit.go:77.
CompileAndEmitResult EmitFilesAndReportErrors(const EmitInput& input) {
	CompileAndEmitResult result;
	result.times = input.CompileTimes;
	// ctx — Go context.Context is dropped (single-threaded port).

	using DiagFn = std::function<std::vector<Diagnostic*>(SourceFile*)>;
	std::vector<Diagnostic*> allDiagnostics =
	    compiler::getDiagnosticsOfAnyProgram(
	        input.ProgramLike, {}, false,
	        DiagFn{[&input, &result](SourceFile* file) {
		        // Options diagnostics include global diagnostics (even though
		        // we collect them separately), and global diagnostics create
		        // checkers, which then bind all of the files. Do this binding
		        // early so we can track the time.
		        std::function<void()> tracePop;
		        if (auto* tr = input.Tracing; tr != nullptr) {
			        tracePop = tr->Push(tracing::PhaseBind,
			                            "bindSourceFiles", tracing::TraceArgs{}, true);
		        }
		        auto bindStart = input.Sys->Now();
		        auto diags = input.ProgramLike->GetBindDiagnostics(file);
		        result.times->bindTime =
		            std::chrono::duration_cast<gostd::Duration>(
		                input.Sys->Now() - bindStart);
		        if (tracePop) {
			        tracePop();
		        }
		        return diags;
	        }},
	        DiagFn{[&input, &result](SourceFile* file) {
		        std::function<void()> tracePop;
		        if (auto* tr = input.Tracing; tr != nullptr) {
			        tracePop = tr->Push(tracing::PhaseCheck,
			                            "checkSourceFiles", tracing::TraceArgs{}, true);
		        }
		        auto checkStart = input.Sys->Now();
		        auto diags = input.ProgramLike->GetSemanticDiagnostics(file);
		        result.times->checkTime =
		            std::chrono::duration_cast<gostd::Duration>(
		                input.Sys->Now() - checkStart);
		        if (auto* program =
		                dynamic_cast<incremental::Program*>(input.ProgramLike);
		            program != nullptr) {
			        auto nestedEmitTime = program->TakeNestedEmitTime();
			        if (nestedEmitTime > result.times->checkTime) {
				        result.times->checkTime = gostd::Duration{0};
			        } else {
				        result.times->checkTime -= nestedEmitTime;
			        }
			        result.times->emitTime += nestedEmitTime;
		        }
		        if (tracePop) {
			        tracePop();
		        }
		        return diags;
	        }});

	auto* emitResult = new compiler::EmitResult();
	emitResult->EmitSkipped = true;
	if (!tristateIsTrue(input.ProgramLike->Options()->ListFilesOnly)) {
		auto emitStart = input.Sys->Now();
		compiler::EmitOptions emitOptions;
		emitOptions.WriteFile = input.WriteFile;
		emitResult = input.ProgramLike->Emit(&emitOptions);
		result.times->emitTime += std::chrono::duration_cast<gostd::Duration>(
		    input.Sys->Now() - emitStart);
	}
	if (emitResult != nullptr) {
		allDiagnostics.insert(allDiagnostics.end(),
		                      emitResult->Diagnostics.begin(),
		                      emitResult->Diagnostics.end());
	}
	if (input.Testing != nullptr) {
		input.Testing->OnEmittedFiles(emitResult, input.TestingMTimesCache);
	}

	allDiagnostics =
	    compiler::sortAndDeduplicateDiagnostics(std::move(allDiagnostics));
	for (auto* diagnostic : allDiagnostics) {
		input.ReportDiagnostic(diagnostic);
	}

	listFiles(input, emitResult);

	input.ReportErrorSummary(allDiagnostics);
	result.Diagnostics = allDiagnostics;
	result.EmitResult = emitResult;
	result.Status = ExitStatusSuccess;
	return result;
}

// listFiles — emit.go:143.
void listFiles(const EmitInput& input, compiler::EmitResult* emitResult) {
	if (input.Testing != nullptr) {
		input.Testing->OnListFilesStart(input.Writer);
	}
	auto* options = input.Program->Options();
	if (tristateIsTrue(options->ListEmittedFiles)) {
		for (auto& file : emitResult->EmittedFiles) {
			*input.Writer << "TSFILE: "
			              << tspath::getNormalizedAbsolutePath(
			                     file, input.Program->GetCurrentDirectory())
			              << '\n';
		}
	}
	if (tristateIsTrue(options->ExplainFiles)) {
		input.Program->ExplainFiles(*input.Writer, input.Config->Locale());
	} else if (tristateIsTrue(options->ListFiles) || tristateIsTrue(options->ListFilesOnly)) {
		for (auto* file : input.Program->GetSourceFiles()) {
			*input.Writer << file->FileName() << '\n';
		}
	}
	if (input.Testing != nullptr) {
		input.Testing->OnListFilesEnd(input.Writer);
	}
}

}  // namespace tsc::execute::tsc
