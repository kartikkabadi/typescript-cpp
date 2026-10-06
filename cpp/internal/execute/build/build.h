// === dep decls — owned by execute/build ===
// Orchestrator surface needed by the api slice (orchestrator.go, buildtask.go).
// Stateful machinery is stubbed with TSC_UNREACHABLE in build.cpp; the
// execute/build slice should replace this file when it lands.
#pragma once

#include <string>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/execute/tsc/compile.h"
#include "internal/execute/tsc/statistics.h"
#include "internal/gostd/gostd.h"
#include "internal/tsoptions/tsoptions.h"

namespace tsc::build {

// Options — orchestrator.go:27.
struct Options {
	execute::tsc::System* Sys = nullptr;
	tsoptions::ParsedBuildCommandLine* Command = nullptr;
	execute::tsc::CommandLineTesting* Testing = nullptr;
};

// OrchestratorResult — orchestrator.go:32.
struct OrchestratorResult {
	execute::tsc::CommandLineResult Result;
	std::vector<Diagnostic*> Errors;
	execute::tsc::Statistics Statistics;
	std::vector<std::string> FilesToDelete;
};

// Orchestrator — orchestrator.go:66.
struct Orchestrator {
	virtual ~Orchestrator() = default;
	virtual OrchestratorResult* Build(gostd::Context ctx,
	                                  const std::string& project) = 0;
	virtual OrchestratorResult* BuildReferences(gostd::Context ctx,
	                                            const std::string& project) = 0;
	virtual OrchestratorResult* Clean(const std::string& project) = 0;
	virtual OrchestratorResult* CleanReferences(
	    const std::string& project) = 0;
};

// NewOrchestrator — orchestrator.go:991. Stubbed (execute/build slice).
Orchestrator* NewOrchestrator(Options opts);

} // namespace tsc::build
