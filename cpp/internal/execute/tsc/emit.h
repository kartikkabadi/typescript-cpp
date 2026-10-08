#pragma once

// emit.go — port of tsc/internal/execute/tsc/emit.go: the emit-and-report
// pipeline shared by the plain, incremental, and watch compile paths.

#include <filesystem>
#include <functional>
#include <ostream>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/collections/collections.h"
#include "internal/compiler/program.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/execute/incremental/incremental.h"
#include "internal/execute/tsc/compile.h"
#include "internal/execute/tsc/diagnostics.h"
#include "internal/gostd/gostd.h"
#include "internal/locale/locale.h"
#include "internal/tracing/tracing.h"
#include "internal/tsoptions/tsoptions.h"

namespace tsc::execute::tsc {

// GetTraceWithWriterFromSys — emit.go:21.
TraceFn GetTraceWithWriterFromSys(std::ostream* w, locale::Locale locale,
                                  CommandLineTesting* testing);

// EmitInput — emit.go:30.
struct EmitInput {
	System* Sys = nullptr;
	compiler::ProgramLike* ProgramLike = nullptr;
	compiler::SimpleProgram* Program = nullptr;
	tsoptions::ParsedCommandLine* Config = nullptr;
	DiagnosticReporter ReportDiagnostic;
	DiagnosticsReporter ReportErrorSummary;
	std::ostream* Writer = nullptr;
	compiler::WriteFile WriteFile;
	CompileTimes* CompileTimes = nullptr;
	CommandLineTesting* Testing = nullptr;
	collections::SyncMap<tspath::Path, std::filesystem::file_time_type>*
	    TestingMTimesCache = nullptr;
	tracing::Tracing* Tracing = nullptr;
};

struct Statistics;

// EmitAndReportStatistics — emit.go:47.
std::pair<CompileAndEmitResult, Statistics*>
EmitAndReportStatistics(const EmitInput& input);

// EmitFilesAndReportErrors — emit.go:77.
CompileAndEmitResult EmitFilesAndReportErrors(const EmitInput& input);

// listFiles — emit.go:143.
void listFiles(const EmitInput& input, compiler::EmitResult* emitResult);

}  // namespace tsc::execute::tsc
