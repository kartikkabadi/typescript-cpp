// Declarations for tsc/internal/testutil/harnessutil — the Go test-harness
// utility package. Only the surface consumed by the testrunner slice is
// declared. Bodies live in testrunner_deps.cpp: a few are real dep-impls
// (small helpers on the ported functions' unconditional paths); the rest are
// TSC_UNREACHABLE dep-stubs to be replaced when the testutil slice lands.
#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "internal/collections/collections.h"
#include "internal/core/types.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/regexp.h"
#include "internal/gostd/testing.h"

namespace tsc::compiler {
class CompilerHost;
struct EmitResult;
class ProgramLike;
}  // namespace tsc::compiler

namespace tsc {
struct CompilerOptions;
struct Diagnostic;
}  // namespace tsc

namespace tsc::tsoptions {
struct ParsedCommandLine;
}

namespace tsc::testutil::harnessutil {

// harnessutil.go:41 — Posix-style path to additional test libraries.
inline constexpr std::string_view testLibFolder = "/.lib";
inline constexpr std::string_view FakeTSVersion = "FakeTSVersion";

// TestFile — harnessutil.go:45.
struct TestFile {
	std::string UnitName;
	std::string Content;
};

// TestConfiguration — harnessutil.go:57 (map[string]string alias in Go).
using TestConfiguration = std::unordered_map<std::string, std::string>;

// NamedTestConfiguration — harnessutil.go:59.
struct NamedTestConfiguration {
	std::string Name;
	TestConfiguration Config;
};

// HarnessOptions — harnessutil.go:64.
struct HarnessOptions {
	bool UseCaseSensitiveFileNames = false;
	std::string BaselineFile;
	std::string IncludeBuiltFile;
	std::string FileName;
	std::vector<std::string> LibFiles;
	bool NoImplicitReferences = false;
	std::string CurrentDirectory;
	std::string Symlink;
	std::string Link;
	bool NoTypesAndSymbols = false;
	bool FullEmitPaths = false;
	bool ReportDiagnostics = false;
	bool CaptureSuggestions = false;
	std::string TypescriptVersion;
};

struct CompilationOutput;
struct CompilationResult;

// CompilationOutput — harnessutil.go:748.
struct CompilationOutput {
	std::vector<TestFile*> Inputs;
	TestFile* JS = nullptr;
	TestFile* DTS = nullptr;
	TestFile* Map = nullptr;
};

// CompilationResult — harnessutil.go:721.
struct CompilationResult {
	std::vector<Diagnostic*> Diagnostics;
	compiler::EmitResult* Result = nullptr;
	compiler::ProgramLike* Program = nullptr;
	CompilerOptions* Options = nullptr;
	struct HarnessOptions* HarnessOptions = nullptr;
	collections::OrderedMap<std::string, TestFile*> JS;
	collections::OrderedMap<std::string, TestFile*> DTS;
	collections::OrderedMap<std::string, TestFile*> Maps;
	std::unordered_map<std::string, std::string> Symlinks;
	std::function<CompilationResult*(TestConfiguration)> Repeat;
	std::vector<TestFile*> outputs;
	std::vector<TestFile*> inputs;
	collections::OrderedMap<std::string, CompilationOutput*>
	    inputsAndOutputs;
	std::string Trace;
	compiler::CompilerHost* Host = nullptr;
};

// CompileFiles — harnessutil.go:81.
CompilationResult* CompileFiles(
    gostd::testing::T* t, const std::vector<TestFile*>& inputFiles,
    const std::vector<TestFile*>& otherFiles,
    const TestConfiguration& testConfig,
    tsoptions::ParsedCommandLine* tsconfig,
    const std::string& currentDirectory,
    const std::unordered_map<std::string, std::string>& symlinks);

// SetOptionsFromTestConfig — harnessutil.go:291.
void SetOptionsFromTestConfig(gostd::testing::T* t,
                              const TestConfiguration& testConfig,
                              CompilerOptions* compilerOptions,
                              HarnessOptions* harnessOptions,
                              const std::string& currentDirectory,
                              bool allowUnknownOptions);

// EnumerateFiles — harnessutil.go:990.
std::pair<std::vector<std::string>, gostd::Error>
EnumerateFiles(const std::string& folder,
               const gostd::regexp::Regexp* testRegex, bool recursive);

// GetFileBasedTestConfigurations — harnessutil.go:1038.
// Ownership follows Go (GC-managed); the eventual testutil port decides
// whether these are arena-allocated or shared.
std::vector<NamedTestConfiguration*> GetFileBasedTestConfigurations(
    gostd::testing::T* t,
    const std::unordered_map<std::string, std::string>& settings,
    const std::unordered_set<std::string>& varyByOptions);

// GetConfigNameFromFileName — harnessutil.go:1228.
std::string GetConfigNameFromFileName(std::string_view filename);

// SkipUnsupportedCompilerOptions — harnessutil.go:1236.
void SkipUnsupportedCompilerOptions(gostd::testing::T* t,
                                    const CompilerOptions* options);

}  // namespace tsc::testutil::harnessutil
