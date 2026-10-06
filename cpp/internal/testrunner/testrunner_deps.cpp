// Dep-impls and dep-stubs for the testrunner slice — functions declared in
// the dep headers under cpp/internal/testutil/, cpp/internal/tsoptions/,
// cpp/internal/testutil/baseline/, cpp/internal/testutil/tsbaseline/.
// The dep-impls are real ports of the small helpers on the ported code's
// unconditional paths; the dep-stubs are TSC_UNREACHABLE and must be
// replaced when the owning slices land.
#include <filesystem>
#include <unordered_map>

#include "internal/gostd/gostd.h"
#include "internal/repo/paths.h"
#include "internal/stringutil/stringutil.h"
#include "internal/testutil/baseline/baseline.h"
#include "internal/testutil/harnessutil/harnessutil.h"
#include "internal/testutil/tsbaseline/tsbaseline.h"
#include "internal/testutil/testutil.h"
#include "internal/tsoptions/tsoptionstest/tsoptionstest.h"
#include "internal/vfs/vfstest/vfstest.h"

namespace tsc::testutil::harnessutil {

namespace {

// strings.ToLower — file-local copy (see test_case_parser.cpp's helper).
std::string toLowerGo(std::string_view s) {
	std::string out;
	out.reserve(s.size());
	int w = 0;
	for (std::string_view rest = s; !rest.empty(); rest.remove_prefix(w)) {
		char32_t r = decodeUtf8Rune(rest, &w);
		out += utf8String(stringutil::toLowerRune(r));
	}
	return out;
}

// listFilesWorker — harnessutil.go:1001.
std::vector<std::string> listFilesWorker(const gostd::regexp::Regexp* spec,
                                         bool recursive,
                                         const std::string& folderIn,
                                         gostd::Error* err) {
	std::string folder =
	    tspath::getNormalizedAbsolutePath(folderIn, repo::testDataPath());
	std::error_code ec;
	std::filesystem::directory_iterator it(folder, ec);
	if (ec) {
		// os.ReadDir → *fs.PathError "open <folder>: <err>".
		*err = gostd::errorf("open %s: %s", {folder, ec.message()});
		return {};
	}
	std::vector<std::string> paths;
	for (auto& entry : it) {
		auto path = tspath::normalizePath(
		    (std::filesystem::path(folder) / entry.path().filename())
		        .generic_string());
		std::error_code ec2;
		bool isDir = entry.is_directory(ec2);
		if (ec2) {
			isDir = false;
		}
		if (!isDir) {
			if (spec == nullptr || spec->MatchString(path)) {
				paths.push_back(path);
			}
		} else if (recursive) {
			auto subPaths = listFilesWorker(spec, recursive, path, err);
			if (*err != nullptr) {
				return {};
			}
			paths.insert(paths.end(),
			             std::make_move_iterator(subPaths.begin()),
			             std::make_move_iterator(subPaths.end()));
		}
	}
	return paths;
}

}  // namespace

// EnumerateFiles — harnessutil.go:990-997.
std::pair<std::vector<std::string>, gostd::Error>
EnumerateFiles(const std::string& folder, const gostd::regexp::Regexp* spec,
               bool recursive) {
	gostd::Error err;
	auto files = listFilesWorker(spec, recursive, folder, &err);
	if (err != nullptr) {
		return {{}, err};
	}
	std::vector<std::string> normalized;
	normalized.reserve(files.size());
	for (auto& f : files) {
		normalized.push_back(std::string(tspath::normalizeSlashes(f)));
	}
	return {std::move(normalized), nullptr};
}

// GetConfigNameFromFileName — harnessutil.go:1228-1234.
std::string GetConfigNameFromFileName(std::string_view filename) {
	auto basenameLower = toLowerGo(tspath::getBaseFileName(filename));
	if (basenameLower == "tsconfig.json" ||
	    basenameLower == "jsconfig.json") {
		return basenameLower;
	}
	return "";
}

// --- dep-stubs: owned by the testutil/harnessutil slice ---

// CompileFiles — harnessutil.go:81.
CompilationResult* CompileFiles(
    gostd::testing::T* t, const std::vector<TestFile*>& inputFiles,
    const std::vector<TestFile*>& otherFiles,
    const TestConfiguration& testConfig, tsoptions::ParsedCommandLine* tsconfig,
    const std::string& currentDirectory,
    const std::unordered_map<std::string, std::string>& symlinks) {
	(void)t;
	(void)inputFiles;
	(void)otherFiles;
	(void)testConfig;
	(void)tsconfig;
	(void)currentDirectory;
	(void)symlinks;
	TSC_UNREACHABLE("harnessutil::CompileFiles — owned by testutil slice");
}

// SetOptionsFromTestConfig — harnessutil.go:291.
void SetOptionsFromTestConfig(gostd::testing::T* t,
                              const TestConfiguration& testConfig,
                              CompilerOptions* compilerOptions,
                              HarnessOptions* harnessOptions,
                              const std::string& currentDirectory,
                              bool allowUnknownOptions) {
	(void)t;
	(void)testConfig;
	(void)compilerOptions;
	(void)harnessOptions;
	(void)currentDirectory;
	(void)allowUnknownOptions;
	TSC_UNREACHABLE(
	    "harnessutil::SetOptionsFromTestConfig — owned by testutil slice");
}

// GetFileBasedTestConfigurations — harnessutil.go:1038.
std::vector<NamedTestConfiguration*> GetFileBasedTestConfigurations(
    gostd::testing::T* t,
    const std::unordered_map<std::string, std::string>& settings,
    const std::unordered_set<std::string>& varyByOptions) {
	(void)t;
	(void)settings;
	(void)varyByOptions;
	TSC_UNREACHABLE(
	    "harnessutil::GetFileBasedTestConfigurations — owned by testutil "
	    "slice");
}

namespace {

// moduleKindToString — core/modulekind_stringer_generated.go.
std::string moduleKindToString(ModuleKind k) {
	switch (k) {
		case ModuleKind::None: return "None";
		case ModuleKind::CommonJS: return "CommonJS";
		case ModuleKind::AMD: return "AMD";
		case ModuleKind::UMD: return "UMD";
		case ModuleKind::System: return "System";
		case ModuleKind::ES2015: return "ES2015";
		case ModuleKind::ES2020: return "ES2020";
		case ModuleKind::ES2022: return "ES2022";
		case ModuleKind::ESNext: return "ESNext";
		case ModuleKind::Node16: return "Node16";
		case ModuleKind::Node18: return "Node18";
		case ModuleKind::Node20: return "Node20";
		case ModuleKind::NodeNext: return "NodeNext";
		case ModuleKind::Preserve: return "Preserve";
	}
	return "ModuleKind(" + std::to_string((int)k) + ")";
}

// scriptTargetToString — core/scripttarget_stringer_generated.go.
std::string scriptTargetToString(ScriptTarget t) {
	switch (t) {
		case ScriptTarget::None: return "None";
		case ScriptTarget::ES5: return "ES5";
		case ScriptTarget::ES2015: return "ES2015";
		case ScriptTarget::ES2016: return "ES2016";
		case ScriptTarget::ES2017: return "ES2017";
		case ScriptTarget::ES2018: return "ES2018";
		case ScriptTarget::ES2019: return "ES2019";
		case ScriptTarget::ES2020: return "ES2020";
		case ScriptTarget::ES2021: return "ES2021";
		case ScriptTarget::ES2022: return "ES2022";
		case ScriptTarget::ES2023: return "ES2023";
		case ScriptTarget::ES2024: return "ES2024";
		case ScriptTarget::ES2025: return "ES2025";
		case ScriptTarget::ES2026: return "ES2026";
		case ScriptTarget::ESNext: return "ESNext";
		case ScriptTarget::JSON: return "JSON";
	}
	return "ScriptTarget(" + std::to_string((int)t) + ")";
}

// failOnUnsupportedCompilerOptions — harnessutil.go:1265.
void failOnUnsupportedCompilerOptions(gostd::testing::T* t,
                                      const CompilerOptions* options) {
	t->Helper();
	if (options->Module == ModuleKind::AMD) {
		t->Fatalf("unsupported module kind %s",
		          {moduleKindToString(options->Module)});
	}
	if (!options->OutFile.empty()) {
		t->Fatalf("unsupported outFile %s", {options->OutFile});
	}
}

}  // namespace

// SkipUnsupportedCompilerOptions — harnessutil.go:1236-1263. Dep-impl: the
// whole body is option-field checks + Skipf/Fatalf, all already ported.
void SkipUnsupportedCompilerOptions(gostd::testing::T* t,
                                    const CompilerOptions* options) {
	t->Helper();
	failOnUnsupportedCompilerOptions(t, options);
	switch (options->Module) {
		case ModuleKind::UMD:
		case ModuleKind::System:
			t->Skipf("unsupported module kind %s",
			         {moduleKindToString(options->Module)});
	}
	switch (options->ModuleResolution) {
		case ModuleResolutionKind::Node10:
		case ModuleResolutionKind::Classic:
			t->Skipf("unsupported module resolution kind %d",
			         {(int)options->ModuleResolution});
	}
	if (tristateIsFalse(options->ESModuleInterop)) {
		t->Skipf("esModuleInterop=false is unsupported", {});
	}
	if (tristateIsFalse(options->AllowSyntheticDefaultImports)) {
		t->Skipf("allowSyntheticDefaultImports=false is unsupported", {});
	}
	if (!options->BaseUrl.empty()) {
		t->Skipf("unsupported baseUrl %s", {options->BaseUrl});
	}
	if (options->Target == ScriptTarget::ES5) {
		t->Skipf("unsupported target %s",
		         {scriptTargetToString(options->Target)});
	}
	if (tristateIsFalse(options->AlwaysStrict)) {
		t->Skipf("alwaysStrict=false is unsupported", {});
	}
}

}  // namespace tsc::testutil::harnessutil

// ---------------------------------------------------------------------------

namespace tsc::testutil::baseline {

// Run — baseline.go:23. Dep-stub — owned by testutil slice.
void Run(gostd::testing::T* t, const std::string& fileName,
         const std::string& actual, const Options& opts) {
	(void)t;
	(void)fileName;
	(void)actual;
	(void)opts;
	TSC_UNREACHABLE("baseline::Run — owned by testutil slice");
}

}  // namespace tsc::testutil::baseline

// ---------------------------------------------------------------------------

namespace tsc::testutil::tsbaseline {

// Dep-stubs — all owned by the testutil/tsbaseline slice.

void DoErrorBaseline(gostd::testing::T* t, const std::string& baselinePath,
                     const std::vector<harnessutil::TestFile*>& inputFiles,
                     const std::vector<Diagnostic*>& errors, bool pretty,
                     const baseline::Options& opts) {
	(void)t;
	(void)baselinePath;
	(void)inputFiles;
	(void)errors;
	(void)pretty;
	(void)opts;
	TSC_UNREACHABLE("tsbaseline::DoErrorBaseline — owned by testutil slice");
}

std::string GetErrorBaseline(
    gostd::testing::T* t,
    const std::vector<harnessutil::TestFile*>& inputFiles,
    const std::vector<
        std::unique_ptr<diagnosticwriter::ASTDiagnostic>>& diagnostics,
    int (*compareDiagnostics)(diagnosticwriter::ASTDiagnostic*,
                              diagnosticwriter::ASTDiagnostic*),
    bool pretty) {
	(void)t;
	(void)inputFiles;
	(void)diagnostics;
	(void)compareDiagnostics;
	(void)pretty;
	TSC_UNREACHABLE("tsbaseline::GetErrorBaseline — owned by testutil slice");
}

void DoContentMapperBaseline(gostd::testing::T* t,
                             const std::string& baselinePath,
                             compiler::ProgramLike* program,
                             const std::vector<Diagnostic*>& diagnostics,
                             const baseline::Options& opts) {
	(void)t;
	(void)baselinePath;
	(void)program;
	(void)diagnostics;
	(void)opts;
	TSC_UNREACHABLE(
	    "tsbaseline::DoContentMapperBaseline — owned by testutil slice");
}

void DoJSEmitBaseline(gostd::testing::T* t, const std::string& baselinePath,
                      const std::string& header, CompilerOptions* options,
                      harnessutil::CompilationResult* result,
                      const std::vector<harnessutil::TestFile*>& tsConfigFiles,
                      const std::vector<harnessutil::TestFile*>& toBeCompiled,
                      const std::vector<harnessutil::TestFile*>& otherFiles,
                      harnessutil::HarnessOptions* harnessSettings,
                      const baseline::Options& opts) {
	(void)t;
	(void)baselinePath;
	(void)header;
	(void)options;
	(void)result;
	(void)tsConfigFiles;
	(void)toBeCompiled;
	(void)otherFiles;
	(void)harnessSettings;
	(void)opts;
	TSC_UNREACHABLE("tsbaseline::DoJSEmitBaseline — owned by testutil slice");
}

void DoSourcemapBaseline(gostd::testing::T* t, const std::string& baselinePath,
                         const std::string& header, CompilerOptions* options,
                         harnessutil::CompilationResult* result,
                         harnessutil::HarnessOptions* harnessSettings,
                         const baseline::Options& opts) {
	(void)t;
	(void)baselinePath;
	(void)header;
	(void)options;
	(void)result;
	(void)harnessSettings;
	(void)opts;
	TSC_UNREACHABLE(
	    "tsbaseline::DoSourcemapBaseline — owned by testutil slice");
}

void DoSourcemapRecordBaseline(gostd::testing::T* t,
                               const std::string& baselinePath,
                               const std::string& header,
                               CompilerOptions* options,
                               harnessutil::CompilationResult* result,
                               harnessutil::HarnessOptions* harnessSettings,
                               const baseline::Options& opts) {
	(void)t;
	(void)baselinePath;
	(void)header;
	(void)options;
	(void)result;
	(void)harnessSettings;
	(void)opts;
	TSC_UNREACHABLE(
	    "tsbaseline::DoSourcemapRecordBaseline — owned by testutil slice");
}

void DoTypeAndSymbolBaseline(
    gostd::testing::T* t, const std::string& baselinePath,
    const std::string& header, compiler::ProgramLike* program,
    const std::vector<harnessutil::TestFile*>& allFiles,
    const baseline::Options& opts, bool skipTypeBaselines,
    bool skipSymbolBaselines, bool hasErrorBaseline) {
	(void)t;
	(void)baselinePath;
	(void)header;
	(void)program;
	(void)allFiles;
	(void)opts;
	(void)skipTypeBaselines;
	(void)skipSymbolBaselines;
	(void)hasErrorBaseline;
	TSC_UNREACHABLE(
	    "tsbaseline::DoTypeAndSymbolBaseline — owned by testutil slice");
}

void DoModuleResolutionBaseline(gostd::testing::T* t,
                                const std::string& baselinePath,
                                const std::string& trace,
                                const baseline::Options& opts) {
	(void)t;
	(void)baselinePath;
	(void)trace;
	(void)opts;
	TSC_UNREACHABLE(
	    "tsbaseline::DoModuleResolutionBaseline — owned by testutil slice");
}

}  // namespace tsc::testutil::tsbaseline

// ---------------------------------------------------------------------------

namespace tsc::tsoptions::tsoptionstest {

// fixRoot — vfsparseconfighost.go:9. Unused by the ported functions but
// ported for parity with the Go file.
[[maybe_unused]] static std::string fixRoot(std::string_view path) {
	int rootLength = tspath::getRootLength(path);
	if (rootLength == 0) {
		return std::string(path);
	}
	if ((int)path.size() == rootLength) {
		return ".";
	}
	return std::string(path.substr(rootLength));
}

// NewVFSParseConfigHost — vfsparseconfighost.go:36.
std::unique_ptr<VfsParseConfigHost> NewVFSParseConfigHost(
    const std::unordered_map<std::string, std::string>& files,
    const std::string& currentDirectory, bool useCaseSensitiveFileNames) {
	std::unordered_map<std::string, vfs::vfstest::MapFileInput> entries;
	entries.reserve(files.size());
	for (auto& [name, content] : files) {
		entries.emplace(name, content);
	}
	auto* host = new VfsParseConfigHost{};
	host->Vfs = vfs::vfstest::FromMap(entries, useCaseSensitiveFileNames);
	host->CurrentDirectory = currentDirectory;
	return std::unique_ptr<VfsParseConfigHost>(host);
}

// NewVFSParseConfigHostWithSymlinks — vfsparseconfighost.go:46. Builds a
// parse-config host whose vfs also contains the given symlinks
// (link path -> target path), so config parsing resolves packages through
// symlinks as it would on disk.
std::unique_ptr<VfsParseConfigHost> NewVFSParseConfigHostWithSymlinks(
    const std::unordered_map<std::string, std::string>& files,
    const std::unordered_map<std::string, std::string>& symlinks,
    const std::string& currentDirectory, bool useCaseSensitiveFileNames) {
	if (symlinks.empty()) {
		return NewVFSParseConfigHost(files, currentDirectory,
		                             useCaseSensitiveFileNames);
	}
	std::unordered_map<std::string, vfs::vfstest::MapFileInput> entries;
	entries.reserve(files.size() + symlinks.size());
	for (auto& [name, content] : files) {
		entries.emplace(name, content);
	}
	for (auto& [link, target] : symlinks) {
		entries[tspath::getNormalizedAbsolutePath(link,
		                                         currentDirectory)] =
		    vfs::vfstest::Symlink(tspath::getNormalizedAbsolutePath(
		        target, currentDirectory));
	}
	auto* host = new VfsParseConfigHost{};
	host->Vfs = vfs::vfstest::FromMap(entries, useCaseSensitiveFileNames);
	host->CurrentDirectory = currentDirectory;
	return std::unique_ptr<VfsParseConfigHost>(host);
}

}  // namespace tsc::tsoptions::tsoptionstest
