// Port of tsc/internal/testrunner/compiler_runner.go
#include "internal/testrunner/testrunner.h"

#include <algorithm>
#include <filesystem>

#include "internal/checker/checker.h"
#include "internal/repo/paths.h"
#include "internal/testutil/baseline/baseline.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/tsbaseline/tsbaseline.h"
#include "internal/vfs/osvfs/osvfs.h"

namespace tsc::testrunner {

namespace {

// compilerBaselineRegex — compiler_runner.go:28.
const gostd::regexp::Regexp& compilerBaselineRegex() {
	static const gostd::regexp::Regexp re{R"(\.tsx?$)"};
	return re;
}

// requireStr — compiler_runner.go:29.
constexpr std::string_view requireStr = "require(";

// referencesRegex — compiler_runner.go:30.
const gostd::regexp::Regexp& referencesRegex() {
	static const gostd::regexp::Regexp re{R"(reference\spath)"};
	return re;
}

// skippedTests — compiler_runner.go:78-124.
const std::vector<std::string_view>& skippedTests() {
	static const std::vector<std::string_view> v{
	    // Tests that depended on typescript.d.ts in built.
	    "APILibCheck.ts",
	    "APISample_Watch.ts",
	    "APISample_WatchWithDefaults.ts",
	    "APISample_WatchWithOwnWatchHost.ts",
	    "APISample_compile.ts",
	    "APISample_jsdoc.ts",
	    "APISample_linter.ts",
	    "APISample_parseConfig.ts",
	    "APISample_transform.ts",
	    "APISample_watcher.ts",

	    // These tests contain options that have been completely removed,
	    // so fail to parse.
	    "preserveUnusedImports.ts",
	    "noCrashWithVerbatimModuleSyntaxAndImportsNotUsedAsValues.ts",
	    "verbatimModuleSyntaxCompat.ts",
	    "verbatimModuleSyntaxCompat2.ts",
	    "verbatimModuleSyntaxCompat3.ts",
	    "verbatimModuleSyntaxCompat4.ts",
	    "preserveValueImports.ts",
	    "preserveValueImports_importsNotUsedAsValues.ts",
	    "preserveValueImports_errors.ts",
	    "preserveValueImports_mixedImports.ts",
	    "preserveValueImports_module.ts",
	    "importsNotUsedAsValues_error.ts",
	    "alwaysStrictNoImplicitUseStrict.ts",
	    "nonPrimitiveIndexingWithForInSupressError.ts",
	    "parameterInitializerBeforeDestructuringEmit.ts",
	    "mappedTypeUnionConstraintInferences.ts",
	    "lateBoundConstraintTypeChecksCorrectly.ts",
	    "keyofDoesntContainSymbols.ts",
	    "noStrictGenericChecks.ts",
	    "noImplicitUseStrict_umd.ts",
	    "noImplicitUseStrict_system.ts",
	    "noImplicitUseStrict_es6.ts",
	    "noImplicitUseStrict_commonjs.ts",
	    "noImplicitAnyIndexingSuppressed.ts",
	    "excessPropertyErrorsSuppressed.ts",
	    "moduleNoneDynamicImport.ts",
	    "moduleNoneErrors.ts",
	    "noErrorUsingImportExportModuleAugmentationInDeclarationFile1.ts",
	    "noErrorUsingImportExportModuleAugmentationInDeclarationFile2.ts",
	    "noErrorUsingImportExportModuleAugmentationInDeclarationFile3.ts",
	    "requireOfJsonFileWithModuleEmitNone.ts",
	    "requireOfJsonFileWithModuleNodeResolutionEmitNone.ts",
	};
	return v;
}

// skippedEmitTests — compiler_runner.go:429-438.
const std::unordered_map<std::string_view, std::string_view>&
skippedEmitTests() {
	static const std::unordered_map<std::string_view, std::string_view> m{
	    {"filesEmittingIntoSameOutput.ts",
	     "Output order nondeterministic due to collision on filename during "
	     "parallel emit."},
	    {"jsFileCompilationWithJsEmitPathSameAsInput.ts",
	     "Output order nondeterministic due to collision on filename during "
	     "parallel emit."},
	    {"grammarErrors.ts",
	     "Output order nondeterministic due to collision on filename during "
	     "parallel emit."},
	    {"jsFileCompilationEmitBlockedCorrectly.ts",
	     "Output order nondeterministic due to collision on filename during "
	     "parallel emit."},
	    {"jsDeclarationsReexportAliasesEsModuleInterop.ts",
	     "cls.d.ts is missing statements when run concurrently."},
	    {"jsFileCompilationWithoutJsExtensions.ts",
	     "No files are emitted."},
	    {"typeOnlyMerge2.ts",
	     "Nondeterministic contents when run concurrently."},
	    {"typeOnlyMerge3.ts",
	     "Nondeterministic contents when run concurrently."},
	};
	return m;
}

// --- small core/slices/strings helpers (not yet ported elsewhere) ---

// core.Map
template <typename T, typename F>
auto coreMap(const std::vector<T>& v, F&& f)
    -> std::vector<std::invoke_result_t<F&, const T&>> {
	std::vector<std::invoke_result_t<F&, const T&>> out;
	out.reserve(v.size());
	for (const auto& x : v) {
		out.push_back(f(x));
	}
	return out;
}

// core.Filter
template <typename T, typename F>
std::vector<T> coreFilter(const std::vector<T>& v, F&& f) {
	std::vector<T> out;
	for (const auto& x : v) {
		if (f(x)) out.push_back(x);
	}
	return out;
}

// core.Some
template <typename T, typename F>
bool coreSome(const std::vector<T>& v, F&& f) {
	for (const auto& x : v) {
		if (f(x)) return true;
	}
	return false;
}

// core.Concatenate
template <typename T>
std::vector<T> coreConcatenate(const std::vector<T>& a,
                               const std::vector<T>& b) {
	std::vector<T> out;
	out.reserve(a.size() + b.size());
	out.insert(out.end(), a.begin(), a.end());
	out.insert(out.end(), b.begin(), b.end());
	return out;
}

// slices.Contains
template <typename C, typename V>
bool slicesContains(const C& c, const V& v) {
	return std::ranges::find(c, v) != c.end();
}

// strings.Contains
inline bool stringsContains(std::string_view s, std::string_view sub) {
	return s.find(sub) != std::string_view::npos;
}

// getCompilerVaryByMap — compiler_runner.go:152-177.
// Set of compiler options for which we allow variations to be specified in
// the test file, for instance `// @strict: true, false`.
std::unordered_set<std::string> getCompilerVaryByMap() {
	std::vector<std::string> varyByOptions;
	for (auto* option : tsoptions::OptionsDeclarations()) {
		if (!option->IsCommandLineOnly &&
		    (option->Kind == tsoptions::CommandLineOptionTypeBoolean ||
		     option->Kind == tsoptions::CommandLineOptionTypeEnum) &&
		    (option->AffectsProgramStructure || option->AffectsEmit ||
		     option->AffectsModuleResolution ||
		     option->AffectsBindDiagnostics ||
		     option->AffectsSemanticDiagnostics ||
		     option->AffectsSourceFile ||
		     option->AffectsDeclarationPath || option->AffectsBuildInfo)) {
			varyByOptions.push_back(option->Name);
		}
	}
	// explicit variations that do not match above conditions
	varyByOptions.push_back("noEmit");
	varyByOptions.push_back("isolatedModules");

	std::unordered_set<std::string> varyByMap;
	for (auto& option : varyByOptions) {
		varyByMap.insert(detail::toLowerGo(option));
	}
	return varyByMap;
}

// compilerVaryBy — compiler_runner.go:150.
const std::unordered_set<std::string>& compilerVaryBy() {
	static const std::unordered_set<std::string> v = getCompilerVaryByMap();
	return v;
}

// PcgRand — math/rand/v2 NewPCG(seed1, seed2) + Rand, just the Uint64 and
// Shuffle surface used by verifyUnionOrdering.
struct PcgRand {
	uint64_t hi, lo;

	// PCG.next — state = state * mul + inc (128-bit arithmetic).
	void next() {
		constexpr uint64_t mulHi = 2549297995355413924ULL;
		constexpr uint64_t mulLo = 4865540595714422341ULL;
		constexpr uint64_t incHi = 6364136223846793005ULL;
		constexpr uint64_t incLo = 1442695040888963407ULL;
		unsigned __int128 prod = (unsigned __int128)lo * mulLo;
		uint64_t newLo = (uint64_t)prod;
		uint64_t newHi =
		    (uint64_t)(prod >> 64) + hi * mulLo + lo * mulHi;
		uint64_t sumLo = newLo + incLo;
		newHi += incHi + (sumLo < newLo ? 1 : 0);
		lo = sumLo;
		hi = newHi;
	}

	// PCG.Uint64 — DXSM output permutation over the advanced state.
	uint64_t Uint64() {
		next();
		constexpr uint64_t cheapMul = 0xda942042e4dd58b5ULL;
		uint64_t h = hi;
		h ^= h >> 32;
		h *= cheapMul;
		h ^= h >> 48;
		h *= (lo | 1);
		return h;
	}

	// Rand.uint64n — Lemire's bounded reduction (64-bit path; Go's 32-bit
	// shortcut is guarded by is32bit which is false here).
	uint64_t uint64n(uint64_t n) {
		if ((n & (n - 1)) == 0) {  // n is power of two, can mask
			return Uint64() & (n - 1);
		}
		unsigned __int128 m = (unsigned __int128)Uint64() * n;
		uint64_t hi_ = (uint64_t)(m >> 64);
		uint64_t lo_ = (uint64_t)m;
		if (lo_ < n) {
			uint64_t thresh = (uint64_t)(0ULL - n) % n;
			while (lo_ < thresh) {
				m = (unsigned __int128)Uint64() * n;
				hi_ = (uint64_t)(m >> 64);
				lo_ = (uint64_t)m;
			}
		}
		return hi_;
	}

	// Rand.Shuffle — Fisher-Yates.
	void Shuffle(int n, const std::function<void(int, int)>& swap) {
		if (n < 0) {
			TSC_UNREACHABLE("invalid argument to Shuffle");
		}
		for (int i = n - 1; i > 0; i--) {
			int j = (int)uint64n((uint64_t)(i + 1));
			swap(i, j);
		}
	}
};

// strings.CutPrefix — returns the remainder without prefix, or {sv,false}.
std::pair<std::string_view, bool> cutPrefix(std::string_view s,
                                            std::string_view prefix) {
	if (s.starts_with(prefix)) {
		return {s.substr(prefix.size()), true};
	}
	return {s, false};
}

}  // namespace

// localBasePath — compiler_runner.go:138.
const std::string& localBasePath() {
	static const std::string v =
	    tspath::combinePaths(repo::testDataPath(), {"baselines", "local"});
	return v;
}

// CompilerTestType.String — compiler_runner.go:43-48.
std::string testTypeString(CompilerTestType testType) {
	if (testType == CompilerTestType::Regression) {
		return "compiler";
	}
	return "conformance";
}

// CompilerBaselineRunner — compiler_runner.go:50.
CompilerBaselineRunner::CompilerBaselineRunner(std::string testSuitName,
                                               CompilerTestType)
    : basePath_("tests/cases/" + testSuitName),
      testSuitName_(std::move(testSuitName)) {}

// NewCompilerBaselineRunner — compiler_runner.go:58-64.
CompilerBaselineRunner* NewCompilerBaselineRunner(CompilerTestType testType) {
	auto testSuitName = testTypeString(testType);
	return new CompilerBaselineRunner(testSuitName, testType);
}

// EnumerateTestFiles — compiler_runner.go:66-76.
std::vector<std::string> CompilerBaselineRunner::EnumerateTestFiles() {
	if (!testFiles_.empty()) {
		return testFiles_;
	}
	auto [files, err] = testutil::harnessutil::EnumerateFiles(
	    basePath_, &compilerBaselineRegex(), true /*recursive*/);
	if (err != nullptr) {
		TSC_UNREACHABLE(("Could not read compiler test files: " +
		                 err->Error())
		                    .c_str());
	}
	testFiles_ = files;
	return files;
}

// RunTests — compiler_runner.go:126-136.
void CompilerBaselineRunner::RunTests(gostd::testing::T* t) {
	cleanUpLocal(t);
	auto files = EnumerateTestFiles();

	for (auto& filename : files) {
		if (slicesContains(
		        skippedTests(),
		        std::string_view(tspath::getBaseFileName(filename)))) {
			continue;
		}
		runTest(t, filename);
	}
}

// cleanUpLocal — compiler_runner.go:140-146.
void CompilerBaselineRunner::cleanUpLocal(gostd::testing::T*) {
	auto localPath = std::filesystem::path(localBasePath()) / testSuitName_;
	std::error_code ec;
	std::filesystem::remove_all(localPath, ec);
	if (ec) {
		TSC_UNREACHABLE(
		    ("Could not clean up local compiler tests: " + ec.message())
		        .c_str());
	}
}

// runTest — compiler_runner.go:179-193.
void CompilerBaselineRunner::runTest(gostd::testing::T* t,
                                     const std::string& filename) {
	auto* test = getCompilerFileBasedTest(t, filename);
	auto basename = tspath::getBaseFileName(filename);
	if (!test->configurations.empty()) {
		for (auto* config : test->configurations) {
			std::string testName{basename};
			if (!config->Name.empty()) {
				testName += " " + config->Name;
			}
			t->Run(testName, [this, &testName, test, config](
			                     gostd::testing::T* t) {
				runSingleConfigTest(t, testName, test, config);
			});
		}
	} else {
		t->Run(std::string{basename}, [this, &basename, test](
		                                  gostd::testing::T* t) {
			runSingleConfigTest(t, std::string{basename}, test, nullptr);
		});
	}
}

// runSingleConfigTest — compiler_runner.go:195-213.
void CompilerBaselineRunner::runSingleConfigTest(
    gostd::testing::T* t, const std::string& testName,
    compilerFileBasedTest* test,
    testutil::harnessutil::NamedTestConfiguration* config) {
	t->Parallel();
	testutil::withRecoverAndFail(
	    t, "Panic on compiler test " + test->filename, [&] {
		    auto payload =
		        makeUnitsFromTest(test->content, test->filename);
		    auto* c = newCompilerTest(t, testName, test->filename,
		                              &payload, config);

		    testutil::harnessutil::SkipUnsupportedCompilerOptions(
		        t, c->options);

		    c->verifyDiagnostics(t, testSuitName_);
		    c->verifyContentMapper(t, testSuitName_);
		    c->verifyJavaScriptOutput(t, testSuitName_);
		    c->verifySourceMapOutput(t, testSuitName_);
		    c->verifySourceMapRecord(t, testSuitName_);
		    c->verifyTypesAndSymbols(t, testSuitName_);
		    c->verifyModuleResolution(t, testSuitName_);
		    c->verifyUnionOrdering(t);
		    c->verifyParentPointers(t);
	    });
}

// getCompilerFileBasedTest — compiler_runner.go:221-233.
compilerFileBasedTest* getCompilerFileBasedTest(
    gostd::testing::T* t, const std::string& filename) {
	auto [content, ok] = vfs::osvfs::FS()->ReadFile(filename);
	if (!ok) {
		TSC_UNREACHABLE(
		    ("Could not read test file: " + filename).c_str());
	}
	auto settings = extractCompilerSettings(content);
	auto configurations = testutil::harnessutil::
	    GetFileBasedTestConfigurations(t, settings, compilerVaryBy());
	return new compilerFileBasedTest{filename, content,
	                                 std::move(configurations)};
}

// newCompilerTest — compiler_runner.go:255-362.
compilerTest* newCompilerTest(
    gostd::testing::T* t, const std::string& testName,
    const std::string& filename, testCaseContent* testContent,
    testutil::harnessutil::NamedTestConfiguration* namedConfiguration) {
	auto basename = tspath::getBaseFileName(filename);
	std::string configuredName{basename};
	if (namedConfiguration != nullptr && !namedConfiguration->Name.empty()) {
		auto extname =
		    tspath::getAnyExtensionFromPath(basename, nullptr, false);
		auto extensionlessBasename =
		    basename.substr(0, basename.size() - extname.size());
		configuredName =
		    gostd::sprintf("%s(%s)%s",
		                   {extensionlessBasename, namedConfiguration->Name,
		                    extname});
	}

	testutil::harnessutil::TestConfiguration configuration;
	if (namedConfiguration != nullptr) {
		configuration = namedConfiguration->Config;
	}
	testCaseContentWithConfig testContentWithConfig{
	    testCaseContent{*testContent}, configuration};

	auto& harnessConfig = testContentWithConfig.configuration;
	auto currentDirIt = harnessConfig.find("currentdirectory");
	std::string currentDirectory = tspath::getNormalizedAbsolutePath(
	    currentDirIt != harnessConfig.end() ? currentDirIt->second : "",
	    srcFolder);

	auto& units = testContentWithConfig.testUnitData;
	std::vector<testutil::harnessutil::TestFile*> toBeCompiled;
	std::vector<testutil::harnessutil::TestFile*> otherFiles;
	tsoptions::ParsedCommandLine* tsConfig = nullptr;
	bool hasNonDtsFiles = coreSome(units, [](testUnit* unit) {
		return !tspath::fileExtensionIs(unit->name, tspath::extensionDts);
	});
	std::vector<testutil::harnessutil::TestFile*> tsConfigFiles;
	if (testContentWithConfig.tsConfig != nullptr) {
		tsConfig = testContentWithConfig.tsConfig;
		tsConfigFiles = {createHarnessTestFile(
		    testContentWithConfig.tsConfigFileUnitData, currentDirectory)};
		for (auto* unit : units) {
			if (slicesContains(
			        tsConfig->ParsedConfig->FileNames,
			        tspath::getNormalizedAbsolutePath(unit->name,
			                                          currentDirectory))) {
				toBeCompiled.push_back(
				    createHarnessTestFile(unit, currentDirectory));
			} else {
				otherFiles.push_back(
				    createHarnessTestFile(unit, currentDirectory));
			}
		}
	} else {
		if (auto baseUrl = harnessConfig.find("baseurl");
		    baseUrl != harnessConfig.end() &&
		    !tspath::isRootedDiskPath(baseUrl->second)) {
			baseUrl->second = tspath::getNormalizedAbsolutePath(
			    baseUrl->second, currentDirectory);
		}

		auto* lastUnit = units[units.size() - 1];
		// We need to assemble the list of input files for the compiler and
		// other related files on the 'filesystem' (ie in a multi-file
		// test). If the last file in a test uses require or a triple slash
		// reference we'll assume all other files will be brought in via
		// references, otherwise, assume all files are just meant to be in
		// the same compilation session without explicit references to one
		// another.

		auto noImplicitReferences =
		    harnessConfig.find("noimplicitreferences");
		if ((noImplicitReferences != harnessConfig.end() &&
		     !noImplicitReferences->second.empty()) ||
		    stringsContains(lastUnit->content, requireStr) ||
		    referencesRegex().MatchString(lastUnit->content)) {
			toBeCompiled.push_back(
			    createHarnessTestFile(lastUnit, currentDirectory));
			for (size_t i = 0; i + 1 < units.size(); i++) {
				otherFiles.push_back(
				    createHarnessTestFile(units[i], currentDirectory));
			}
		} else {
			toBeCompiled = coreMap(
			    units, [&](testUnit* unit) {
				    return createHarnessTestFile(unit,
				                                 currentDirectory);
			    });
		}
	}

	auto* result = testutil::harnessutil::CompileFiles(
	    t, toBeCompiled, otherFiles, harnessConfig, tsConfig,
	    currentDirectory, testContentWithConfig.symlinks);

	// Content-mapped files are transformed during program construction;
	// the transformed text is what the compiler actually parses and
	// reports positions against. Baseline that text (rather than the
	// original foreign source) so the type, symbol, and error baselines
	// line up with the compiler's positions.
	for (auto* file : coreConcatenate(toBeCompiled, otherFiles)) {
		if (auto* sf =
		        result->Program->GetSourceFile(file->UnitName);
		    sf != nullptr && !sf->ContentMapper().empty()) {
			file->Content = sf->Text();
		}
	}

	return new compilerTest{
	    /*testName*/ testName,
	    /*filename*/ filename,
	    /*basename*/ std::string(basename),
	    /*configuredName*/ configuredName,
	    /*currentDirectory*/ currentDirectory,
	    /*options*/ result->Options,
	    /*harnessOptions*/ result->HarnessOptions,
	    /*result*/ result,
	    /*tsConfigFiles*/ std::move(tsConfigFiles),
	    /*toBeCompiled*/ std::move(toBeCompiled),
	    /*otherFiles*/ std::move(otherFiles),
	    /*hasNonDtsFiles*/ hasNonDtsFiles,
	};
}

// verifyDiagnostics — compiler_runner.go:364-403.
void compilerTest::verifyDiagnostics(gostd::testing::T* t,
                                     std::string_view suiteName) {
	t->Run("error", [this, suiteName](gostd::testing::T* t) {
		testutil::withRecoverAndFail(
		    t,
		    "Panic on creating error baseline for test " + filename,
		    [&] {
			    auto files = coreConcatenate(
			        tsConfigFiles,
			        coreConcatenate(toBeCompiled, otherFiles));
			    auto diagnostics = result->Diagnostics;
			    // Content-mapped files' diagnostics are baselined
			    // separately (see verifyContentMapper), where they can be
			    // rendered against the correct text; the squiggle
			    // renderer here assumes a single coordinate space.
			    if (auto contentMapped = contentMappedFileNames();
			        !contentMapped.empty()) {
				    files = coreFilter(
				        files, [&](testutil::harnessutil::TestFile* f) {
					        return !contentMapped.contains(
					            tspath::getNormalizedAbsolutePath(
					                f->UnitName, currentDirectory));
				        });
				    diagnostics =
				        coreFilter(diagnostics, [&](Diagnostic* d) {
					        return d->File() == nullptr ||
					               !contentMapped.contains(
					                   d->File()->FileName());
				        });
			    }
			    testutil::tsbaseline::DoErrorBaseline(
			        t, configuredName, files, diagnostics,
			        tristateIsTrue(result->Options->Pretty),
			        testutil::baseline::Options{
			            .Subfolder = std::string(suiteName),

			            .DiffFixupOld = [](std::string old) {
				            std::string sb;
				            sb.reserve(old.size());

				            // strings.SplitSeq(old, "\n")
				            size_t start = 0;
				            for (;;) {
					            size_t nl = old.find('\n', start);
					            std::string_view line =
					                std::string_view(old).substr(
					                    start,
					                    nl == std::string_view::npos
					                        ? nl
					                        : nl - start);
					            constexpr std::string_view
					                relativePrefixNew = "==== ";
					            constexpr std::string_view
					                relativePrefixOld = "==== ./";
					            if (auto [rest, ok2] =
					                        cutPrefix(line,
					                                  relativePrefixOld);
					                ok2) {
						            sb += relativePrefixNew;
						            sb += rest;
					            } else {
						            sb += line;
					            }

					            sb += '\n';
					            if (nl == std::string_view::npos) break;
					            start = nl + 1;
				            }

				            return sb.substr(0, sb.size() - 1);
			            },
			        });
		    });
	});
}

// verifyContentMapper — compiler_runner.go:405-412.
void compilerTest::verifyContentMapper(gostd::testing::T* t,
                                       std::string_view suiteName) {
	t->Run("content mapper", [this, suiteName](gostd::testing::T* t) {
		testutil::withRecoverAndFail(
		    t,
		    "Panic on creating content mapper baseline for test " +
		        filename,
		    [&] {
			    testutil::tsbaseline::DoContentMapperBaseline(
			        t, configuredName, result->Program,
			        result->Diagnostics,
			        testutil::baseline::Options{
			            .Subfolder = std::string(suiteName)});
		    });
	});
}

// contentMappedFileNames — compiler_runner.go:415-427. Returns the set of
// absolute file names that were produced by a content mapper.
std::unordered_map<std::string, bool> compilerTest::contentMappedFileNames() {
	auto* program = result->Program->GetProgram();
	std::unordered_map<std::string, bool> mapped;
	for (auto* file : result->Program->GetSourceFiles()) {
		if (program->GetContentMapper(file) != nullptr) {
			mapped[file->FileName()] = true;
		}
	}
	return mapped;
}

// verifyJavaScriptOutput — compiler_runner.go:440-466.
void compilerTest::verifyJavaScriptOutput(gostd::testing::T* t,
                                          std::string_view suiteName) {
	if (!hasNonDtsFiles) {
		return;
	}

	t->Run("output", [this, suiteName](gostd::testing::T* t) {
		if (auto it = skippedEmitTests().find(basename);
		    it != skippedEmitTests().end()) {
			t->Skip({std::string(it->second)});
		}

		testutil::withRecoverAndFail(
		    t, "Panic on creating js output for test " + filename, [&] {
			    auto headerComponents =
			        tspath::getPathComponentsRelativeTo(
			            repo::testDataPath(), filename,
			            tspath::ComparePathsOptions{});
			    std::vector<std::string_view> headerComponentViews(
			        headerComponents.begin(), headerComponents.end());
			    auto header = tspath::getPathFromPathComponents(
			        headerComponentViews);
			    testutil::tsbaseline::DoJSEmitBaseline(
			        t, configuredName, header, options, result,
			        tsConfigFiles, toBeCompiled, otherFiles,
			        harnessOptions,
			        testutil::baseline::Options{
			            .Subfolder = std::string(suiteName)});
		    });
	});
}

// verifySourceMapOutput — compiler_runner.go:468-483.
void compilerTest::verifySourceMapOutput(gostd::testing::T* t,
                                         std::string_view suiteName) {
	t->Run("sourcemap", [this, suiteName](gostd::testing::T* t) {
		testutil::withRecoverAndFail(
		    t,
		    "Panic on creating source map output for test " + filename,
		    [&] {
			    auto headerComponents =
			        tspath::getPathComponentsRelativeTo(
			            repo::testDataPath(), filename,
			            tspath::ComparePathsOptions{});
			    std::vector<std::string_view> headerComponentViews(
			        headerComponents.begin(), headerComponents.end());
			    auto header = tspath::getPathFromPathComponents(
			        headerComponentViews);
			    testutil::tsbaseline::DoSourcemapBaseline(
			        t, configuredName, header, options, result,
			        harnessOptions,
			        testutil::baseline::Options{
			            .Subfolder = std::string(suiteName)});
		    });
	});
}

// verifySourceMapRecord — compiler_runner.go:485-500.
void compilerTest::verifySourceMapRecord(gostd::testing::T* t,
                                         std::string_view suiteName) {
	t->Run("sourcemap record", [this, suiteName](gostd::testing::T* t) {
		testutil::withRecoverAndFail(
		    t,
		    "Panic on creating source map record for test " + filename,
		    [&] {
			    auto headerComponents =
			        tspath::getPathComponentsRelativeTo(
			            repo::testDataPath(), filename,
			            tspath::ComparePathsOptions{});
			    std::vector<std::string_view> headerComponentViews(
			        headerComponents.begin(), headerComponents.end());
			    auto header = tspath::getPathFromPathComponents(
			        headerComponentViews);
			    testutil::tsbaseline::DoSourcemapRecordBaseline(
			        t, configuredName, header, options, result,
			        harnessOptions,
			        testutil::baseline::Options{
			            .Subfolder = std::string(suiteName)});
		    });
	});
}

// verifyTypesAndSymbols — compiler_runner.go:502-528.
void compilerTest::verifyTypesAndSymbols(gostd::testing::T* t,
                                         std::string_view suiteName) {
	bool noTypesAndSymbols = harnessOptions->NoTypesAndSymbols;
	if (noTypesAndSymbols) {
		return;
	}
	auto* program = result->Program;
	auto allFiles = coreFilter(
	    coreConcatenate(toBeCompiled, otherFiles),
	    [&](testutil::harnessutil::TestFile* f) {
		    return program->GetSourceFile(f->UnitName) != nullptr;
	    });

	auto headerComponents = tspath::getPathComponentsRelativeTo(
	    repo::testDataPath(), filename, tspath::ComparePathsOptions{});
	std::vector<std::string_view> headerComponentViews(
	    headerComponents.begin(), headerComponents.end());
	auto header = tspath::getPathFromPathComponents(headerComponentViews);
	testutil::tsbaseline::DoTypeAndSymbolBaseline(
	    t, configuredName, header, program, allFiles,
	    testutil::baseline::Options{.Subfolder = std::string(suiteName)},
	    false, false, !result->Diagnostics.empty());
}

// verifyModuleResolution — compiler_runner.go:530-543.
void compilerTest::verifyModuleResolution(gostd::testing::T* t,
                                          std::string_view suiteName) {
	if (!tristateIsTrue(options->TraceResolution)) {
		return;
	}

	t->Run("module resolution", [this, suiteName](gostd::testing::T* t) {
		testutil::withRecoverAndFail(
		    t,
		    "Panic on creating module resolution baseline for test " +
		        filename,
		    [&] {
			    testutil::tsbaseline::DoModuleResolutionBaseline(
			        t, configuredName, result->Trace,
			        testutil::baseline::Options{
			            .Subfolder = std::string(suiteName),

			            .SkipDiffWithOld = true,
			        });
		    });
	});
}

// createHarnessTestFile — compiler_runner.go:545-550.
testutil::harnessutil::TestFile* createHarnessTestFile(
    const testUnit* unit, const std::string& currentDirectory) {
	return new testutil::harnessutil::TestFile{
	    tspath::getNormalizedAbsolutePath(unit->name, currentDirectory),
	    unit->content};
}

// verifyUnionOrdering — compiler_runner.go:552-575.
void compilerTest::verifyUnionOrdering(gostd::testing::T* t) {
	t->Run("union ordering", [this](gostd::testing::T* t) {
		auto* p = result->Program->GetProgram();
		p->ForEachCheckerParallel([t](int, checker::Checker* c) {
			for (auto* unionType : c->UnionTypes()) {
				auto& types = unionType->types();

				std::vector<checker::Type*> reversed(types.begin(), types.end());
				std::ranges::reverse(reversed);
				std::sort(reversed.begin(), reversed.end(),
				          [](checker::Type* a, checker::Type* b) {
					          return checker::CompareTypes(a, b) < 0;
				          });
				gotest::assert::Assert(
				    t, reversed == types,
				    "compareTypes does not sort union types "
				    "consistently");

				std::vector<checker::Type*> shuffled(types.begin(), types.end());
				PcgRand rng{1234, 5678};

				for (int round = 0; round < 10; round++) {
					rng.Shuffle((int)shuffled.size(),
					            [&](int i, int j) {
						            std::swap(shuffled[i], shuffled[j]);
					            });
					std::sort(shuffled.begin(), shuffled.end(),
					          [](checker::Type* a, checker::Type* b) {
						          return checker::CompareTypes(a, b) < 0;
					          });
					gotest::assert::Assert(
					    t, shuffled == types,
					    "compareTypes does not sort union types "
					    "consistently");
				}
			}
		});
	});
}

// verifyParentPointers — compiler_runner.go:577-607.
void compilerTest::verifyParentPointers(gostd::testing::T* t) {
	t->Run("source file parent pointers", [this](gostd::testing::T* t) {
		Node* parent = nullptr;
		std::function<bool(Node*)> verifier;
		verifier = [&](Node* n) -> bool {
			if (n == nullptr) {
				return false;
			}
			gotest::assert::Assert(t, n->parent != nullptr,
			                       "parent node does not exist");
			std::string elab;
			if (!nodeIsSynthesized(n)) {
				auto& text = getSourceFileOfNode(n)->Text();
				elab += text.substr(n->loc.pos(), n->loc.len());
			} else {
				elab += "!synthetic! no text available";
			}
			gotest::assert::Assert(
			    t, n->parent == parent,
			    "parent node does not match traversed parent: " +
			        std::string(kindToString(n->kind)) + ": " + elab);
			Node* oldParent = parent;
			parent = n;
			n->forEachChild(verifier);
			parent = oldParent;
			return false;
		};
		for (auto* f : result->Program->GetSourceFiles()) {
			if (result->Program->IsSourceFileDefaultLibrary(f->Path())) {
				continue;
			}
			parent = f->asNode();
			f->asNode()->forEachChild(verifier);
		}
	});
}

}  // namespace tsc::testrunner
