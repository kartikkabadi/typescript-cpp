// Port of tsc/internal/testrunner/transpile_runner.go
#include "internal/testrunner/testrunner.h"

#include <filesystem>

#include "internal/diagnosticwriter/diagnosticwriter.h"
#include "internal/outputpaths/outputpaths.h"
#include "internal/testutil/baseline/baseline.h"
#include "internal/testutil/tsbaseline/tsbaseline.h"
#include "internal/transpile/transpile.h"
#include "internal/vfs/osvfs/osvfs.h"

namespace tsc::testrunner {

namespace {

// transpileBaselineRegex — transpile_runner.go:22.
const gostd::regexp::Regexp& transpileBaselineRegex() {
	static const gostd::regexp::Regexp re{R"(\.[cm]?[tj]sx?$)"};
	return re;
}

// transpileVaryBy — transpile_runner.go:24.
const std::unordered_set<std::string>& transpileVaryBy() {
	static const std::unordered_set<std::string> v{
	    "declarationmap", "sourcemap", "inlinesourcemap",
	};
	return v;
}

// strings.ReplaceAll
std::string replaceAll(std::string_view s, std::string_view old,
                       std::string_view replacement) {
	if (old.empty()) {
		// Go inserts replacement between every rune boundary; test inputs
		// never hit this path.
		TSC_UNREACHABLE("strings.ReplaceAll with empty old");
	}
	std::string out;
	size_t pos = 0;
	for (;;) {
		size_t at = s.find(old, pos);
		if (at == std::string_view::npos) break;
		out.append(s, pos, at - pos);
		out += replacement;
		pos = at + old.size();
	}
	out.append(s, pos, s.size() - pos);
	return out;
}

// strings.TrimSuffix
std::string_view trimSuffix(std::string_view s, std::string_view suffix) {
	if (!suffix.empty() && s.ends_with(suffix)) {
		s.remove_suffix(suffix.size());
	}
	return s;
}

// strings.HasSuffix — plain (case-sensitive) wrapper.
inline bool hasSuffix(std::string_view s, std::string_view suffix) {
	return s.ends_with(suffix);
}

}  // namespace

// TranspileBaselineRunner — transpile_runner.go:30.
TranspileBaselineRunner::TranspileBaselineRunner()
    : basePath_("../testdata/tests/cases/transpile") {}

// EnumerateTestFiles — transpile_runner.go:43-53.
std::vector<std::string> TranspileBaselineRunner::EnumerateTestFiles() {
	if (!testFiles_.empty()) {
		return testFiles_;
	}
	auto [files, err] = testutil::harnessutil::EnumerateFiles(
	    basePath_, &transpileBaselineRegex(), true);
	if (err != nullptr) {
		TSC_UNREACHABLE(("Could not read transpile test files: " +
		                 err->Error())
		                    .c_str());
	}
	testFiles_ = files;
	return files;
}

// RunTests — transpile_runner.go:55-59.
void TranspileBaselineRunner::RunTests(gostd::testing::T* t) {
	for (auto& fileName : EnumerateTestFiles()) {
		runTest(t, fileName);
	}
}

// runTest — transpile_runner.go:61-95.
void TranspileBaselineRunner::runTest(gostd::testing::T* t,
                                      const std::string& fileName) {
	auto [content, ok] = vfs::osvfs::FS()->ReadFile(fileName);
	if (!ok) {
		TSC_UNREACHABLE(
		    ("Could not read transpile test file: " + fileName).c_str());
	}
	auto settings = extractCompilerSettings(content);
	auto configurations = testutil::harnessutil::
	    GetFileBasedTestConfigurations(t, settings, transpileVaryBy());
	if (configurations.empty()) {
		configurations = {new testutil::harnessutil::NamedTestConfiguration{
		    {}, settings}};
	}

	auto extension = tspath::getAnyExtensionFromPath(fileName, nullptr,
	                                                 false);
	auto baseName = tspath::getBaseFileName(fileName);
	auto justName = std::string(trimSuffix(baseName, extension));
	auto units = makeUnitsFromTest(content, std::string(baseName))
	                 .testUnitData;

	for (auto* configuration : configurations) {
		std::string configuredName = justName;
		if (!configuration->Name.empty()) {
			configuredName += "(" + formatTranspileConfigurationName(
			                            configuration->Name) +
			                  ")";
		}
		t->Run(configuredName, [this, &configuration, &configuredName,
		                        &extension, &units](gostd::testing::T* t) {
			auto* options = new CompilerOptions();
			auto* harnessOptions =
			    new testutil::harnessutil::HarnessOptions();
			testutil::harnessutil::SetOptionsFromTestConfig(
			    t, configuration->Config, options, harnessOptions,
			    srcFolder, false);

			if (!tristateIsTrue(options->EmitDeclarationOnly)) {
				runKind(t, configuredName, extension, units, options,
				        harnessOptions, false);
			}
			if (tristateIsTrue(options->Declaration)) {
				runKind(t, configuredName, extension, units, options,
				        harnessOptions, true);
			}
		});
	}
}

// formatTranspileConfigurationName — transpile_runner.go:97-101.
std::string formatTranspileConfigurationName(const std::string& name) {
	std::string out =
	    replaceAll(name, "declarationmap=", "declarationMap=");
	out = replaceAll(out, "inlinesourcemap=", "inlineSourceMap=");
	return replaceAll(out, "sourcemap=", "sourceMap=");
}

// runKind — transpile_runner.go:103-168.
void TranspileBaselineRunner::runKind(
    gostd::testing::T* t, const std::string& configuredName,
    std::string_view extension, const std::vector<testUnit*>& units,
    CompilerOptions* options,
    testutil::harnessutil::HarnessOptions* harnessOptions,
    bool declaration) {
	std::string result;  // strings.Builder
	for (auto* unit : units) {
		appendTranspileSection(result, unit->name, unit->content);
	}

	for (auto* unit : units) {
		transpile::Options transpileOptions{
		    /*CompilerOptions*/ options,
		    /*FileName*/ unit->name,
		    /*ReportDiagnostics*/ harnessOptions->ReportDiagnostics,
		};
		transpile::Output* output;
		if (declaration) {
			output =
			    transpile::TranspileDeclaration(unit->content,
			                                    transpileOptions);
		} else {
			output = transpile::TranspileModule(unit->content,
			                                    transpileOptions);
		}
		if (output == nullptr) {
			t->Fatal({"transpilation was canceled"});
		}

		std::string_view outputExtension =
		    outputpaths::GetOutputExtension(unit->name, options->Jsx);
		if (declaration) {
			outputExtension = tspath::getDeclarationEmitExtensionForPath(
			    unit->name);
		}
		auto outputFileName =
		    tspath::changeExtension(unit->name, outputExtension);
		appendTranspileSection(result, outputFileName, output->OutputText);
		if (!output->SourceMapText.empty()) {
			appendTranspileSection(result, outputFileName + ".map",
			                       output->SourceMapText);
		}
		if (!output->Diagnostics.empty()) {
			result += "\r\n\r\n//// [Diagnostics reported]\r\n";
			std::string diagnosticFileName = unit->name;
			if (auto* file = output->Diagnostics[0]->File();
			    file != nullptr) {
				diagnosticFileName = file->FileName();
			}
			std::vector<testutil::harnessutil::TestFile*> testFiles{
			    new testutil::harnessutil::TestFile{diagnosticFileName,
			                                        unit->content}};
			auto errorBaseline = testutil::tsbaseline::GetErrorBaseline(
			    t, testFiles,
			    diagnosticwriter::wrapASTDiagnostics(
			        output->Diagnostics),
			    diagnosticwriter::compareASTDiagnostics,
			    tristateIsTrue(options->Pretty));
			result += replaceAll(errorBaseline, diagnosticFileName,
			                     unit->name);
			if (!hasSuffix(result, "\n")) {
				result += "\r\n";
			}
		}
	}

	// Materialize the argument: GetOutputExtension returns a view into it
	// (Go's GC keeps the intermediate string alive).
	std::string baselineArg = configuredName + std::string(extension);
	std::string_view baselineExtension = outputpaths::GetOutputExtension(
	    baselineArg, options->Jsx);
	if (declaration) {
		baselineExtension = tspath::getDeclarationEmitExtensionForPath(
		    baselineArg);
	}
	std::string baselineName =
	    configuredName + std::string(baselineExtension);
	testutil::baseline::Run(t, "transpile/" + baselineName, result,
	                        testutil::baseline::Options{});
}

// appendTranspileSection — transpile_runner.go:170-176.
void appendTranspileSection(std::string& result, std::string_view fileName,
                            std::string_view content) {
	result += "//// [" + std::string(fileName) + "] ////\r\n";
	result += content;
	if (!hasSuffix(content, "\n")) {
		result += "\r\n";
	}
}

// cleanTranspileBaselines — transpile_runner.go:178-182.
void cleanTranspileBaselines() {
	std::error_code ec;
	std::filesystem::remove_all(
	    std::filesystem::path(localBasePath()) / "transpile", ec);
	if (ec) {
		TSC_UNREACHABLE(("Could not clean up transpile baselines: " +
		                 ec.message())
		                    .c_str());
	}
}

// RunTranspileTests — transpile_runner.go:184-187.
void RunTranspileTests(gostd::testing::T* t) {
	cleanTranspileBaselines();
	TranspileBaselineRunner runner;
	runner.RunTests(t);
}

}  // namespace tsc::testrunner
