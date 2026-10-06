// Declarations for tsc/internal/testutil/tsbaseline — the Go TypeScript
// baseline package. Declared for the testrunner slice; bodies are
// TSC_UNREACHABLE dep-stubs (testrunner_deps.cpp) until the testutil slice
// lands.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "internal/compiler/program.h"
#include "internal/core/types.h"
#include "internal/diagnosticwriter/diagnosticwriter.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/baseline/baseline.h"
#include "internal/testutil/harnessutil/harnessutil.h"

namespace tsc::testutil::tsbaseline {

// DoErrorBaseline — error_baseline.go:35.
void DoErrorBaseline(gostd::testing::T* t, const std::string& baselinePath,
                     const std::vector<harnessutil::TestFile*>& inputFiles,
                     const std::vector<Diagnostic*>& errors, bool pretty,
                     const baseline::Options& opts);

// GetErrorBaseline — error_baseline.go:61. Go is generic over
// diagnosticwriter.Diagnostic; the port declares the ASTDiagnostic
// instantiation used by testrunner.
std::string GetErrorBaseline(
    gostd::testing::T* t,
    const std::vector<harnessutil::TestFile*>& inputFiles,
    const std::vector<
        std::unique_ptr<diagnosticwriter::ASTDiagnostic>>& diagnostics,
    int (*compareDiagnostics)(diagnosticwriter::ASTDiagnostic*,
                              diagnosticwriter::ASTDiagnostic*),
    bool pretty);

// DoContentMapperBaseline — contentmapper_baseline.go:26.
void DoContentMapperBaseline(gostd::testing::T* t,
                             const std::string& baselinePath,
                             compiler::ProgramLike* program,
                             const std::vector<Diagnostic*>& diagnostics,
                             const baseline::Options& opts);

// DoJSEmitBaseline — js_emit_baseline.go:20.
void DoJSEmitBaseline(gostd::testing::T* t, const std::string& baselinePath,
                      const std::string& header,
                      CompilerOptions* options,
                      harnessutil::CompilationResult* result,
                      const std::vector<harnessutil::TestFile*>& tsConfigFiles,
                      const std::vector<harnessutil::TestFile*>& toBeCompiled,
                      const std::vector<harnessutil::TestFile*>& otherFiles,
                      harnessutil::HarnessOptions* harnessSettings,
                      const baseline::Options& opts);

// DoSourcemapBaseline — sourcemap_baseline.go:18.
void DoSourcemapBaseline(gostd::testing::T* t, const std::string& baselinePath,
                         const std::string& header, CompilerOptions* options,
                         harnessutil::CompilationResult* result,
                         harnessutil::HarnessOptions* harnessSettings,
                         const baseline::Options& opts);

// DoSourcemapRecordBaseline — sourcemap_record_baseline.go:12.
void DoSourcemapRecordBaseline(gostd::testing::T* t,
                               const std::string& baselinePath,
                               const std::string& header,
                               CompilerOptions* options,
                               harnessutil::CompilationResult* result,
                               harnessutil::HarnessOptions* harnessSettings,
                               const baseline::Options& opts);

// DoTypeAndSymbolBaseline — type_symbol_baseline.go:30.
void DoTypeAndSymbolBaseline(gostd::testing::T* t,
                             const std::string& baselinePath,
                             const std::string& header,
                             compiler::ProgramLike* program,
                             const std::vector<harnessutil::TestFile*>&
                                 allFiles,
                             const baseline::Options& opts,
                             bool skipTypeBaselines, bool skipSymbolBaselines,
                             bool hasErrorBaseline);

// DoModuleResolutionBaseline — module_resolution_baseline.go:9.
void DoModuleResolutionBaseline(gostd::testing::T* t,
                                const std::string& baselinePath,
                                const std::string& trace,
                                const baseline::Options& opts);

}  // namespace tsc::testutil::tsbaseline
