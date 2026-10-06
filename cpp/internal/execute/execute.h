#pragma once

// tsc.go — port of tsc/internal/execute/tsc.go: the top-level tsc CLI entry
// (CommandLine dispatch, config resolution, plain/incremental/watch
// compilation selection, tracing lifecycle).

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "internal/collections/collections.h"
#include "internal/compiler/program.h"
#include "internal/contentmapper/contentmapper.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/execute/incremental/incremental.h"
#include "internal/execute/tsc/compile.h"
#include "internal/execute/tsc/diagnostics.h"
#include "internal/execute/tsc/statistics.h"
#include "internal/gostd/gostd.h"
#include "internal/locale/locale.h"
#include "internal/tracing/tracing.h"
#include "internal/tsoptions/tsoptions.h"

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------


namespace tsc::execute {

// startTracingIfNeeded — tsc.go:29.
tracing::Tracing* startTracingIfNeeded(
    tsc::System* sys, tsoptions::ParsedCommandLine* config,
    tsc::CommandLineTesting* testing);

// stopTracing — tsc.go:45.
void stopTracing(tsc::System* sys, tracing::Tracing* tr);

// CommandLine — tsc.go:55.
tsc::CommandLineResult CommandLine(
    gostd::Context ctx, tsc::System* sys,
    const std::vector<std::string>& commandLineArgs,
    tsc::CommandLineTesting* testing);

// fmtMain — tsc.go:69 (dead code in Go — the `-f` case is commented out;
// ported faithfully anyway).
tsc::ExitStatus fmtMain(tsc::System* sys, std::string input,
                        std::string output);

// tscBuildCompilation — tsc.go:95.
tsc::CommandLineResult tscBuildCompilation(
    gostd::Context ctx, tsc::System* sys,
    tsoptions::ParsedBuildCommandLine* buildCommand,
    tsc::CommandLineTesting* testing);

// tscCompilation — tsc.go:122.
tsc::CommandLineResult tscCompilation(
    gostd::Context ctx, tsc::System* sys,
    tsoptions::ParsedCommandLine* commandLine,
    tsc::CommandLineTesting* testing);

// findConfigFile — tsc.go:249.
std::string findConfigFile(
    const std::string& searchPath,
    const std::function<bool(const std::string&)>& fileExists,
    const std::string& configName);

// getTraceFromSys — tsc.go:262.
tsc::TraceFn getTraceFromSys(tsc::System* sys, locale::Locale locale,
                             tsc::CommandLineTesting* testing);

// performIncrementalCompilation — tsc.go:266.
tsc::CommandLineResult performIncrementalCompilation(
    gostd::Context ctx, tsc::System* sys,
    tsoptions::ParsedCommandLine* config,
    const tsc::DiagnosticReporter& reportDiagnostic,
    const tsc::DiagnosticsReporter& reportErrorSummary,
    tsoptions::ExtendedConfigCache* extendedConfigCache,
    tsc::CompileTimes* compileTimes, tsc::CommandLineTesting* testing);

// performCompilation — tsc.go:330.
tsc::CommandLineResult performCompilation(
    gostd::Context ctx, tsc::System* sys,
    tsoptions::ParsedCommandLine* config,
    const tsc::DiagnosticReporter& reportDiagnostic,
    const tsc::DiagnosticsReporter& reportErrorSummary,
    tsoptions::ExtendedConfigCache* extendedConfigCache,
    tsc::CompileTimes* compileTimes, tsc::CommandLineTesting* testing);

// getContentMapperProject — tsc.go:389.
std::shared_ptr<contentmapper::Project> getContentMapperProject(
    const std::shared_ptr<contentmapper::Host>& host,
    tsoptions::ParsedCommandLine* config);

// showConfig — tsc.go:399.
void showConfig(tsc::System* sys, tsoptions::ParsedCommandLine* config,
                const std::string& configFileName);

// createWatcher — watcher.go:99. Defined in watcher.cpp.
class Watcher;
Watcher* createWatcher(
    tsc::System* sys, tsoptions::ParsedCommandLine* config,
    const CompilerOptions* optionsFromCommandLine,
    const tsoptions::JsonObjectPtr& commandLineRaw,
    const tsc::DiagnosticReporter& reportDiagnostic,
    const tsc::DiagnosticsReporter& reportErrorSummary,
    tsc::CommandLineTesting* testing);

}  // namespace tsc::execute
