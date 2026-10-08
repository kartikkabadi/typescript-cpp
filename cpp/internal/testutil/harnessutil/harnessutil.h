// Declarations for tsc/internal/testutil/harnessutil — the Go test-harness
// utility package.
#pragma once

#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "internal/collections/collections.h"
#include "internal/core/types.h"
#include "internal/gostd/gostd.h"
#include "internal/gostd/regexp.h"
#include "internal/gostd/testing.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/vfs.h"

namespace tsc::compiler {
class CompilerHost;
struct EmitResult;
class ProgramLike;
}  // namespace tsc::compiler

namespace tsc {
struct CompilerOptions;
struct Diagnostic;
struct DiagnosticMessage;
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

	// getOutputPath — harnessutil.go:836.
	std::string getOutputPath(const std::string& path,
	                          const std::string& ext);
	// FS — harnessutil.go:861.
	std::shared_ptr<vfs::FS> FS();
	// GetNumberOfJSFiles — harnessutil.go:865.
	int GetNumberOfJSFiles(bool includeJson);
	// Inputs — harnessutil.go:877.
	const std::vector<TestFile*>& Inputs() { return inputs; }
	// Outputs — harnessutil.go:881.
	const std::vector<TestFile*>& Outputs() { return outputs; }
	// GetInputsAndOutputsForFile — harnessutil.go:885.
	CompilationOutput* GetInputsAndOutputsForFile(const std::string& path);
	// GetInputsForFile — harnessutil.go:889.
	std::vector<TestFile*> GetInputsForFile(const std::string& path);
	// GetOutput — harnessutil.go:897 (kind: "js" | "dts" | "map").
	TestFile* GetOutput(const std::string& path,
	                    const std::string& kind);
	// GetSourceMapRecord — harnessutil.go:914.
	std::string GetSourceMapRecord();
};

// CompileFiles — harnessutil.go:81.
CompilationResult* CompileFiles(
    gostd::testing::T* t, const std::vector<TestFile*>& inputFiles,
    const std::vector<TestFile*>& otherFiles,
    const TestConfiguration& testConfig,
    tsoptions::ParsedCommandLine* tsconfig,
    const std::string& currentDirectory,
    const std::unordered_map<std::string, std::string>& symlinks);

// CompileFilesEx — harnessutil.go:119.
CompilationResult* CompileFilesEx(
    gostd::testing::T* t, const std::vector<TestFile*>& inputFiles,
    const std::vector<TestFile*>& otherFiles, HarnessOptions* harnessOptions,
    CompilerOptions* compilerOptions, const std::string& currentDirectory,
    const std::unordered_map<std::string, std::string>& symlinks,
    tsoptions::ParsedCommandLine* tsconfig);

// NewOutputRecorderFS — recorderfs.go:18.
std::shared_ptr<vfs::FS>
NewOutputRecorderFS(const std::shared_ptr<vfs::FS>& fs);

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

// === slice: tsctests ===
// TracerForBaselining — harnessutil.go:489. Sanitizing trace sink used by
// test systems so module-resolution/package.json lookups produce stable
// baseline output (lifts the previously TU-local type for tsctests).
struct TracerForBaselining {
	tspath::ComparePathsOptions opts;
	std::unordered_map<tspath::Path, bool> packageJsonCache;
	// strings.Builder — the output buffer, owned by the caller.
	std::string* builder = nullptr;

	// Trace — harnessutil.go:502.
	void Trace(const DiagnosticMessage* msg,
	           const std::vector<std::string>& args);
	// TraceWithWriter — harnessutil.go:507 (io.Writer → std::ostream*).
	void TraceWithWriter(std::ostream* w, const std::string& msg,
	                     bool usePackageJsonCache);
	// sanitizeTrace — harnessutil.go:519.
	std::string sanitizeTrace(const std::string& msg,
	                          bool usePackageJsonCache);
	// String/Reset — harnessutil.go:607/611.
	const std::string& String() const;
	void Reset();
};

// NewTracerForBaselining — harnessutil.go:495.
TracerForBaselining* NewTracerForBaselining(
    tspath::ComparePathsOptions opts, std::string* builder);
// === end slice: tsctests ===

// GetConfigNameFromFileName — harnessutil.go:1228.
std::string GetConfigNameFromFileName(std::string_view filename);

// SkipUnsupportedCompilerOptions — harnessutil.go:1236.
void SkipUnsupportedCompilerOptions(gostd::testing::T* t,
                                    const CompilerOptions* options);

}  // namespace tsc::testutil::harnessutil
