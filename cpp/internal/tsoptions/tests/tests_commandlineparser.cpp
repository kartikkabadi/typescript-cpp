// Port of tsc/internal/tsoptions/commandlineparser_test.go
// (package tsoptions_test). The Go file's export_test.go helpers
// (ParseCommandLineTestWorker/TestCommandLineParser) are folded in here.
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/core/types.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/diagnosticwriter/diagnosticwriter.h"
#include "internal/gostd/testing.h"
#include "internal/json/json.h"
#include "internal/repo/paths.h"
#include "internal/testutil/baseline/baseline.h"
#include "internal/testutil/filefixture/filefixture.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tsoptions/tsoptionstest/tsoptionstest.h"
#include "internal/tspath/tspath.h"
#include "internal/vfs/osvfs/osvfs.h"
#include "internal/vfs/vfs.h"

namespace {

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
namespace baseline = tsc::testutil::baseline;
namespace diagnosticwriter = tsc::diagnosticwriter;
namespace filefixture = tsc::testutil::filefixture;
namespace json = tsc::json;
namespace osvfs = tsc::vfs::osvfs;
namespace vfs = tsc::vfs;
namespace repo = tsc::repo;
namespace tsoptions = tsc::tsoptions;
namespace tsoptionstest = tsc::tsoptions::tsoptionstest;
namespace tspath = tsc::tspath;

// --- export_test.go --------------------------------------------------------

std::string toLowerAscii(std::string_view s) {
	std::string r(s);
	for (auto& c : r) {
		if (c >= 'A' && c <= 'Z') c += 32;
	}
	return r;
}

// TestCommandLineParser — export_test.go:39.
struct TestCommandLineParser {
	std::unique_ptr<tsoptions::ParseCommandLineWorkerDiagnostics>
	    WorkerDiagnostics;
	std::vector<std::string> FileNames;
	tsoptions::JsonObjectPtr Options;
	std::vector<tsc::Diagnostic*> Errors;
};

// ParseCommandLineTestWorker — export_test.go:16. `fs` is a plain vfs.FS in
// Go; the C++ parser needs module::ResolutionHost — wrap in a
// VfsParseConfigHost (delegates every FS op to the given vfs::FS).
TestCommandLineParser* ParseCommandLineTestWorker(
    const std::vector<const tsoptions::CommandLineOption*>& decls,
    const std::vector<std::string>& commandLine,
    const std::shared_ptr<vfs::FS>& fs, const std::string& currentDirectory) {
	tsoptionstest::VfsParseConfigHost host;
	host.Vfs = fs;
	host.CurrentDirectory = currentDirectory;
	auto* parser = new tsoptions::commandLineParser{
	    .workerDiagnostics = &tsoptions::CompilerOptionsDidYouMeanDiagnostics(),
	    .fs = host.FS(),
	    .currentDirectory = currentDirectory,
	    .options = std::make_shared<tsoptions::JsonObject>(),
	};
	std::unique_ptr<tsoptions::ParseCommandLineWorkerDiagnostics>
	    workerDiagnostics;
	if (!decls.empty()) {
		workerDiagnostics =
		    tsoptions::getParseCommandLineWorkerDiagnostics(decls);
		parser->workerDiagnostics = workerDiagnostics.get();
	}
	parser->optionsMap =
	    tsoptions::GetNameMapFromList(*parser->OptionsDeclarations());
	parser->parseStrings(commandLine);
	auto* result = new TestCommandLineParser{
	    std::move(workerDiagnostics),
	    std::move(parser->fileNames),
	    std::move(parser->options),
	    std::move(parser->errors),
	};
	delete parser;
	return result;
}

// --- helpers ----------------------------------------------------------------

std::string join(const std::vector<std::string>& v,
                 const std::string& sep) {
	std::string r;
	for (size_t i = 0; i < v.size(); i++) {
		if (i) r += sep;
		r += v[i];
	}
	return r;
}

// strings.Cut — returns (before, after, found).
std::tuple<std::string, std::string, bool> strCut(const std::string& s,
                                                  const std::string& sep) {
	auto pos = s.find(sep);
	if (pos == std::string::npos) {
		return {s, "", false};
	}
	return {s.substr(0, pos), s.substr(pos + sep.size()), true};
}

void nilErrorStr(T* t, const std::string& e) {
	if (!e.empty()) {
		t->Fatalf("assert.NilError failed: %s", {e});
	}
}

std::string formatDiagnostics(
    const std::vector<tsc::Diagnostic*>& errors) {
	auto wrapped = diagnosticwriter::fromASTDiagnostics(errors);
	std::vector<diagnosticwriter::Diagnostic*> diags;
	diags.reserve(wrapped.size());
	for (auto& w : wrapped) diags.push_back(w.get());
	std::ostringstream out;
	diagnosticwriter::FormattingOptions opts{.newLine = "\n"};
	diagnosticwriter::writeFormatDiagnostics(out, diags, &opts);
	return out.str();
}

// CompilerOptions deep equality via the field table (the C++ stand-in for
// Go's reflect over exported fields + cmpopts.IgnoreUnexported).
bool covEqual(const tsoptions::CompilerOptionsValue& a,
              const tsoptions::CompilerOptionsValue& b) {
	if (a.v.index() != b.v.index()) return false;
	return std::visit(
	    [&](const auto& av) -> bool {
		    using U = std::decay_t<decltype(av)>;
		    const auto& bv = std::get<U>(b.v);
		    if constexpr (std::is_same_v<U, tsoptions::JsonObjectPtr> ||
		                  std::is_same_v<U, tsoptions::JsonGoMapPtr>) {
			    if (!av || !bv) return av == bv;
			    if constexpr (std::is_same_v<U,
			                               tsoptions::JsonObjectPtr>) {
				    if (av->keys != bv->keys) return false;
				    for (const auto& k : av->keys) {
					    if (!covEqual(av->mp.at(k),
					                  bv->mp.at(k))) return false;
				    }
			    } else {
				    if (av->size() != bv->size()) return false;
				    for (const auto& [k, avv] : *av) {
					    auto it = bv->find(k);
					    if (it == bv->end() || !covEqual(avv, it->second))
						    return false;
				    }
			    }
			    return true;
		    } else if constexpr (std::is_same_v<U, tsoptions::JsonArray> ||
		                         std::is_same_v<U, tsoptions::JsonStrList>) {
			    if constexpr (std::is_same_v<U, tsoptions::JsonArray>) {
				    if (av.size() != bv.size()) return false;
				    for (size_t i = 0; i < av.size(); i++) {
					    if (!covEqual(av[i], bv[i])) return false;
				    }
				    return true;
			    } else {
				    return av == bv;
			    }
		    } else {
			    return av == bv;
		    }
	    },
	    a.v);
}

bool compilerOptionsEqual(const tsc::CompilerOptions& a,
                          const tsc::CompilerOptions& b) {
	for (const auto& info : tsoptions::compilerOptionFieldInfos()) {
		if (!covEqual(info.get(&a), info.get(&b))) return false;
	}
	return true;
}

// TestCommandLineParser — parsed baseline shape (commandlineparser_test.go:318).
struct TestCommandLineParserBaseline {
	tsc::CompilerOptions options;
	tsc::WatchOptions watchoptions;
	std::string fileNames, errors;
};

// parseExistingCompilerBaseline — commandlineparser_test.go:310.
TestCommandLineParserBaseline parseExistingCompilerBaseline(
    T* t, const std::string& baselineText) {
	auto [_, rest0, _f0] = strCut(baselineText, "CompilerOptions::\n");
	auto [compilerOptions, rest1, watchFound] =
	    strCut(rest0, "\nWatchOptions::\n");
	auto [watchOptions, rest2, _f2] =
	    strCut(rest1, "\nFileNames::\n");
	auto [fileNames, errors, _f3] = strCut(rest2, "\nErrors::\n");

	TestCommandLineParserBaseline out;
	nilErrorStr(t, json::unmarshal(compilerOptions, &out.options));
	if (watchFound && !watchOptions.empty()) {
		nilErrorStr(t, json::unmarshal(watchOptions, &out.watchoptions));
	}
	out.fileNames = fileNames;
	out.errors = errors;
	return out;
}

// formatNewBaseline — commandlineparser_test.go:334.
std::string formatNewBaseline(
    const std::vector<std::string>& commandLine, const std::string& opts,
    const std::string& fileNames, const std::string& errors) {
	std::ostringstream formatted;
	formatted << "Args::\n[";
	for (size_t i = 0; i < commandLine.size(); i++) {
		if (i) formatted << ", ";
		formatted << '"' << commandLine[i] << '"';
	}
	formatted << "]\n\nCompilerOptions::\n" << opts
	          << "\n\nFileNames::\n"
	          << fileNames << "\n\nErrors::\n" << errors;
	return formatted.str();
}

struct TestCommandLineParserBuildBaseline {
	tsc::BuildOptions options;
	tsc::CompilerOptions compilerOptions;
	tsc::WatchOptions watchoptions;
	std::string projects, errors;
};

// parseExistingCompilerBaselineBuild — commandlineparser_test.go:427.
TestCommandLineParserBuildBaseline parseExistingCompilerBaselineBuild(
    T* t, const std::string& baselineText) {
	auto [_, rest0, _f0] = strCut(baselineText, "buildOptions::\n");
	auto [buildOptions, rest1, watchFound] =
	    strCut(rest0, "\nWatchOptions::\n");
	auto [watchOptions, rest2, _f2] = strCut(rest1, "\nProjects::\n");
	auto [projects, errors, _f3] = strCut(rest2, "\nErrors::\n");

	TestCommandLineParserBuildBaseline out;
	nilErrorStr(t, json::unmarshal(buildOptions, &out.options));
	nilErrorStr(t, json::unmarshal(buildOptions, &out.compilerOptions));
	if (watchFound && !watchOptions.empty()) {
		nilErrorStr(t, json::unmarshal(watchOptions, &out.watchoptions));
	}
	out.projects = projects;
	out.errors = errors;
	return out;
}

// formatNewBaselineBuild — commandlineparser_test.go:456.
std::string formatNewBaselineBuild(
    const std::vector<std::string>& commandLine, const std::string& opts,
    const std::string& compilerOpts, const std::string& projects,
    const std::string& errors) {
	std::ostringstream formatted;
	formatted << "Args::\n[";
	for (size_t i = 0; i < commandLine.size(); i++) {
		if (i) formatted << ", ";
		formatted << '"' << commandLine[i] << '"';
	}
	formatted << "]\n\nbuildOptions::\n" << opts
	          << "\n\ncompilerOptions::\n"
	          << compilerOpts << "\n\nProjects::\n"
	          << projects << "\n\nErrors::\n" << errors;
	return formatted.str();
}

// --- commandLineSubScenario --------------------------------------------------

struct commandLineSubScenario {
	std::shared_ptr<filefixture::Fixture> baselineFixture;
	std::string testName;
	std::vector<std::string> commandLine;
	std::vector<const tsoptions::CommandLineOption*> optDecls;

	void assertParseResult(T* t) const {
		t->Helper();
		t->Run(testName, [&](T* t) {
			std::string originalBaseline = baselineFixture->ReadFile(t);
			auto tsBaseline =
			    parseExistingCompilerBaseline(t, originalBaseline);

			std::shared_ptr<vfs::FS> osfs(vfs::osvfs::FS(),
			                              [](vfs::FS*) {});
			std::unique_ptr<TestCommandLineParser> parsed(
			    ParseCommandLineTestWorker(optDecls, commandLine, osfs,
			                               t->TempDir()));

			std::string newBaselineFileNames =
			    join(parsed->FileNames, ",");
			assert::Equal(t, tsBaseline.fileNames, newBaselineFileNames);

			auto o = tsoptions::jsonMarshal(
			    tsoptions::CompilerOptionsValue{parsed->Options});
			tsc::CompilerOptions newParsedCompilerOptions{};
			nilErrorStr(t,
			            json::unmarshal(o, &newParsedCompilerOptions));
			assert::Assert(
			    t,
			    compilerOptionsEqual(tsBaseline.options,
			                         newParsedCompilerOptions),
			    "assert.DeepEqual failed: CompilerOptions");

			tsc::WatchOptions newParsedWatchOptions{};
			nilErrorStr(t,
			            json::unmarshal(o, &newParsedWatchOptions));

			std::string newBaselineErrors = formatDiagnostics(parsed->Errors);

			baseline::Run(
			    t, testName + ".js",
			    formatNewBaseline(commandLine, o,
			                      newBaselineFileNames, newBaselineErrors),
			    baseline::Options{.Subfolder = "tsoptions/commandLineParsing"});
		});
	}

	void assertBuildParseResultWithTsBaseline(
	    T* t,
	    const std::function<TestCommandLineParserBuildBaseline()>&
	        getTsBaseline) const {
		t->Helper();
		t->Run(testName, [&](T* t) {
			TestCommandLineParserBuildBaseline tsBaseline;
			bool haveBaseline = false;
			if (getTsBaseline) {
				std::string originalBaseline = baselineFixture->ReadFile(t);
				tsBaseline = parseExistingCompilerBaselineBuild(
				    t, originalBaseline);
				haveBaseline = true;
			}

			tsoptionstest::VfsParseConfigHost host;
			host.Vfs = std::shared_ptr<vfs::FS>(vfs::osvfs::FS(),
			                                   [](vfs::FS*) {});
			host.CurrentDirectory =
			    tspath::normalizeSlashes(repo::testDataPath());
			auto* parsed =
			    tsoptions::ParseBuildCommandLine(commandLine, &host);

			std::string newBaselineProjects = join(parsed->Projects, ",");
			if (haveBaseline) {
				assert::Equal(t, tsBaseline.projects, newBaselineProjects);
			}

			auto o = json::marshal(*parsed->BuildOptions);
			tsc::BuildOptions newParsedBuildOptions{};
			nilErrorStr(t, json::unmarshal(o.first, &newParsedBuildOptions));
			if (haveBaseline) {
				assert::Assert(
				    t,
				    marshalEq(tsBaseline.options, newParsedBuildOptions),
				    "assert.DeepEqual failed: BuildOptions");
			}

			auto compilerOpts = json::marshal(*parsed->CompilerOptions);
			tsc::CompilerOptions newParsedCompilerOptions{};
			nilErrorStr(t, json::unmarshal(compilerOpts.first,
			                               &newParsedCompilerOptions));
			if (haveBaseline) {
				assert::Assert(t,
				               compilerOptionsEqual(tsBaseline.compilerOptions,
				                                    newParsedCompilerOptions),
				               "assert.DeepEqual failed: CompilerOptions");
			}

			tsc::WatchOptions newParsedWatchOptions{};
			nilErrorStr(t,
			            json::unmarshal(o.first, &newParsedWatchOptions));

			std::string newBaselineErrors = formatDiagnostics(parsed->Errors);

			baseline::Run(
			    t, testName + ".js",
			    formatNewBaselineBuild(commandLine, o.first,
			                           compilerOpts.first, newBaselineProjects,
			                           newBaselineErrors),
			    baseline::Options{.Subfolder = "tsoptions/commandLineParsing"});
		});
	}

	// marshalEq — BuildOptions structural compare via its JSON encoding
	// (Go DeepEqual over exported fields; the marshal emits every
	// omitzero-tagged field deterministically).
	template <class U>
	static bool marshalEq(const U& a, const U& b) {
		return json::marshal(a).first == json::marshal(b).first;
	}

	void assertBuildParseResult(T* t) const {
		t->Helper();
		assertBuildParseResultWithTsBaseline(
		    t,
		    [&] {
			    return parseExistingCompilerBaselineBuild(
			        t, baselineFixture->ReadFile(t));
		    });
	}
};

commandLineSubScenario createSubScenario(
    const std::string& scenarioKind, const std::string& subScenarioName,
    const std::vector<std::string>& commandline,
    const std::vector<const tsoptions::CommandLineOption*>& opts = {}) {
	std::string testName = scenarioKind + "/" + subScenarioName;
	std::string baselineFileName = "tests/baselines/reference/config/"
	                               "commandLineParsing/" +
	                               testName + ".js";
	return commandLineSubScenario{
	    filefixture::FromFile(testName,
	                          tspath::combinePaths(
	                              repo::testDataPath(),
	                              {"fixtures", "typescript",
	                               baselineFileName})),
	    testName, commandline, opts};
}

struct subScenarioInput {
	std::string name;
	std::vector<std::string> commandLineArgs;

	commandLineSubScenario createSubScenario(
	    const std::string& scenarioKind) const {
		return ::createSubScenario(scenarioKind, name, commandLineArgs);
	}
};

struct verifyNull {
	std::string subScenario;
	std::string optionName;
	std::string nonNullValue;
	std::vector<const tsoptions::CommandLineOption*> optDecls;
};

// createVerifyNullForNonNullIncluded — commandlineparser_test.go:256.
verifyNull createVerifyNullForNonNullIncluded(
    const std::string& subScenario,
    tsoptions::CommandLineOptionKind kind,
    const std::string& nonNullValue) {
	auto* optionDecl = new tsoptions::CommandLineOption{};
	optionDecl->Name = "optionName";
	optionDecl->Kind = kind;
	optionDecl->IsTSConfigOnly = true;
	optionDecl->Category = tsc::Backwards_Compatibility;
	optionDecl->Description = tsc::Enable_project_compilation;
	auto decls = tsoptions::OptionsDeclarations();
	decls.push_back(optionDecl);
	return {subScenario, "optionName", nonNullValue, decls};
}

// --- tests -----------------------------------------------------------------

// TestCommandLineParseResult — commandlineparser_test.go:25.
void TestCommandLineParseResult(T* t) {
	std::vector<subScenarioInput> parseCommandLineSubScenarios = {
	    {"Parse single option of library flag", {"--lib", "es6", "0.ts"}},
	    {"Handles may only be used with --build flags",
	     {"--build", "--clean", "--dry", "--force", "--verbose"}},
	    {"Handles did you mean for misspelt flags",
	     {"--declarations", "--allowTS"}},
	    {"Parse multiple options of library flags",
	     {"--lib", "es5,es2015.symbol.wellknown", "0.ts"}},
	    {"Parse invalid option of library flags",
	     {"--lib", "es5,invalidOption", "0.ts"}},
	    {"Parse empty options of --jsx", {"0.ts", "--jsx"}},
	    {"Parse empty options of --module", {"0.ts", "--module"}},
	    {"Parse empty options of --newLine", {"0.ts", "--newLine"}},
	    {"Parse empty options of --target", {"0.ts", "--target"}},
	    {"Parse empty options of --moduleResolution",
	     {"0.ts", "--moduleResolution"}},
	    {"Parse empty options of --lib", {"0.ts", "--lib"}},
	    {"Parse empty string of --lib", {"0.ts", "--lib", ""}},
	    {"Parse immediately following command line argument of --lib",
	     {"0.ts", "--lib", "--sourcemap"}},
	    {"Parse --lib option with extra comma",
	     {"--lib", "es5,", "es7", "0.ts"}},
	    {"Parse --lib option with trailing white-space",
	     {"--lib", "es5, ", "es7", "0.ts"}},
	    {"Parse multiple compiler flags with input files at the end",
	     {"--lib", "es5,es2015.symbol.wellknown", "--target", "es5", "0.ts"}},
	    {"Parse multiple compiler flags with input files in the middle",
	     {"--module", "commonjs", "--target", "es5", "0.ts", "--lib",
	      "es5,es2015.symbol.wellknown"}},
	    {"Parse multiple library compiler flags ",
	     {"--module", "commonjs", "--target", "es5", "--lib", "es5", "0.ts",
	      "--lib", "es2015.core, es2015.symbol.wellknown "}},
	    {"Parse explicit boolean flag value",
	     {"--strictNullChecks", "false", "0.ts"}},
	    {"Parse non boolean argument after boolean flag",
	     {"--noImplicitAny", "t", "0.ts"}},
	    {"Parse implicit boolean flag value", {"--strictNullChecks"}},
	    {"parse --incremental", {"--incremental", "0.ts"}},
	    {"parse --tsBuildInfoFile",
	     {"--tsBuildInfoFile", "build.tsbuildinfo", "0.ts"}},
	    {"allows tsconfig only option to be set to null",
	     {"--composite", "null", "-tsBuildInfoFile", "null", "0.ts"}},
	    // ****** Watch Options ******
	    {"parse --watchFile", {"--watchFile", "UseFsEvents", "0.ts"}},
	    {"parse --watchDirectory",
	     {"--watchDirectory", "FixedPollingInterval", "0.ts"}},
	    {"parse --fallbackPolling",
	     {"--fallbackPolling", "PriorityInterval", "0.ts"}},
	    {"parse --synchronousWatchDirectory",
	     {"--synchronousWatchDirectory", "0.ts"}},
	    {"errors on missing argument to --fallbackPolling",
	     {"0.ts", "--fallbackPolling"}},
	    {"parse --excludeDirectories", {"--excludeDirectories", "**/temp", "0.ts"}},
	    {"errors on invalid excludeDirectories",
	     {"--excludeDirectories", "**/../*", "0.ts"}},
	    {"parse --excludeFiles",
	     {"--excludeFiles", "**/temp/*.ts", "0.ts"}},
	    {"errors on invalid excludeFiles",
	     {"--excludeFiles", "**/../*", "0.ts"}},
	};

	for (const auto& testCase : parseCommandLineSubScenarios) {
		testCase.createSubScenario("parseCommandLine").assertParseResult(t);
	}
}

// TestResponseFileDoesNotPanic — commandlineparser_test.go:89.
void TestResponseFileDoesNotPanic(T* t) {
	// Passing `@` with an empty or relative filename should not panic.
	// It should produce a diagnostic error instead.
	std::string cwd = t->TempDir();
	t->Run("empty response file", [&](T* t) {
		std::shared_ptr<vfs::FS> osfs(vfs::osvfs::FS(), [](vfs::FS*) {});
		std::unique_ptr<TestCommandLineParser> parsed(
		    ParseCommandLineTestWorker({}, {"@"}, osfs, cwd));
		assert::Assert(t, !parsed->Errors.empty(),
		               "expected an error for empty response file name");
	});

	t->Run("relative response file", [&](T* t) {
		std::shared_ptr<vfs::FS> osfs(vfs::osvfs::FS(), [](vfs::FS*) {});
		std::unique_ptr<TestCommandLineParser> parsed(
		    ParseCommandLineTestWorker({}, {"@blah"}, osfs, cwd));
		assert::Assert(t, !parsed->Errors.empty(),
		               "expected an error for non-existent response file");
	});
}

// TestResponseFileParsing — commandlineparser_test.go:108.
void TestResponseFileParsing(T* t) {
	t->Run("final token without trailing whitespace", [&](T* t) {
		auto host = tsoptionstest::NewVFSParseConfigHost(
		    {{"/project/args.txt", "--strict --outDir dist"}},
		    "/project", true);
		auto* parsed =
		    tsoptions::ParseCommandLine({"@args.txt"}, host.get());
		assert::Equal(t, parsed->Errors.size(), size_t(0));
		assert::Assert(t, parsed->CompilerOptions()->Strict ==
		                    tsc::Tristate::True);
		assert::Equal(t, parsed->CompilerOptions()->OutDir,
		              std::string("/project/dist"));
	});

	t->Run("cyclic response files", [&](T* t) {
		auto host = tsoptionstest::NewVFSParseConfigHost(
		    {
		        {"/project/a.txt", "@/project/b.txt --strict"},
		        {"/project/b.txt", "@/project/a.txt --outDir dist"},
		    },
		    "/project", true);
		auto* parsed = tsoptions::ParseCommandLine({"@a.txt"}, host.get());
		assert::Equal(t, parsed->Errors.size(), size_t(0));
		assert::Assert(t, parsed->CompilerOptions()->Strict ==
		                    tsc::Tristate::True);
		assert::Equal(t, parsed->CompilerOptions()->OutDir,
		              std::string("/project/dist"));
	});
}

// TestParseCommandLineTypeRootsRelativePath — commandlineparser_test.go:135.
void TestParseCommandLineTypeRootsRelativePath(T* t) {
	auto host = tsoptionstest::NewVFSParseConfigHost(
	    {
	        {"/home/project/bug.ts", "let x = 1;"},
	    },
	    "/home/project", true);

	auto* cmdLine = tsoptions::ParseCommandLine(
	    {"--typeRoots", "t", "bug.ts"}, host.get());

	const auto& typeRoots = cmdLine->CompilerOptions()->TypeRoots;
	assert::Assert(t, !typeRoots.empty(), "typeRoots should not be nil");
	assert::Equal(t, typeRoots.size(), size_t(1));
	assert::Assert(t, tspath::isRootedDiskPath(typeRoots[0]),
	               "typeRoots entry should be an absolute path, got: " +
	                   typeRoots[0]);
	assert::Assert(t, typeRoots[0].size() >= 2 &&
	                    typeRoots[0].substr(typeRoots[0].size() - 2) == "/t",
	               "typeRoots entry should end with '/t', got: " +
	                   typeRoots[0]);
}

// TestCustomConditionsNullOverride — commandlineparser_test.go:151.
void TestCustomConditionsNullOverride(T* t) {
	std::unordered_map<std::string, std::string> files = {
	    {"/project/tsconfig.json", R"({
  "compilerOptions": {
    "customConditions": ["condition1", "condition2"]
  }
})"},
	    {"/project/index.ts", R"(console.log("Hello, World!");)"},
	};

	auto host = tsoptionstest::NewVFSParseConfigHost(files, "/project", true);

	// Parse command line with --customConditions null
	auto* cmdLine = tsoptions::ParseCommandLine(
	    {"--project", "/project", "--customConditions", "null"}, host.get());

	// Check that the raw options contain null for customConditions
	auto* rawMap =
	    std::get_if<tsoptions::JsonObjectPtr>(&cmdLine->Raw.v);
	if (rawMap != nullptr && *rawMap != nullptr) {
		auto [customConditionsRaw, exists] =
		    (*rawMap)->Get("customConditions");
		assert::Assert(t, exists,
		               "customConditions should exist in raw options");
		assert::Assert(
		    t, std::holds_alternative<std::monostate>(
		           customConditionsRaw->v),
		    "customConditions should be nil in raw options");
	} else {
		t->Fatalf("Raw options should be an OrderedMap", {});
	}

	// Now parse the config file with the command line options
	// Wrap command line options in "compilerOptions" key to match tsconfig.json structure
	auto wrappedRaw = std::make_shared<tsoptions::JsonObject>();
	wrappedRaw->Set("compilerOptions", cmdLine->Raw);
	auto [parsedConfig, errors] =
	    tsoptions::GetParsedCommandLineOfConfigFile(
	        "/project/tsconfig.json", cmdLine->CompilerOptions(), wrappedRaw,
	        host.get(), nullptr);

	assert::Assert(t, errors.empty(), "Should not have errors");

	// Check that customConditions is nil (overridden by command line)
	auto customConditions =
	    parsedConfig->CompilerOptions()->CustomConditions;
	assert::Assert(t, customConditions.empty(),
	               "customConditions should be nil after override");
}

// TestParseCommandLineVerifyNull — commandlineparser_test.go:196.
void TestParseCommandLineVerifyNull(T* t) {
	// run test for boolean
	subScenarioInput{"allows setting option type boolean to false",
	                 {"--composite", "false", "0.ts"}}
	    .createSubScenario("parseCommandLine")
	    .assertParseResult(t);

	std::vector<verifyNull> verifyNullSubScenarios = {
	    {.subScenario = "option of type boolean",
	     .optionName = "composite",
	     .nonNullValue = "true"},
	    {.subScenario = "option of type object", .optionName = "paths"},
	    {.subScenario = "option of type list",
	     .optionName = "rootDirs",
	     .nonNullValue = "abc,xyz"},
	    createVerifyNullForNonNullIncluded(
	        "option of type string",
	        tsoptions::CommandLineOptionTypeString, "hello"),
	    createVerifyNullForNonNullIncluded(
	        "option of type number",
	        tsoptions::CommandLineOptionTypeNumber, "10"),
	};

	for (const auto& verifyNullCase : verifyNullSubScenarios) {
		createSubScenario("parseCommandLine",
		                  verifyNullCase.subScenario +
		                      " allows setting it to null",
		                  {"--" + verifyNullCase.optionName, "null", "0.ts"},
		                  verifyNullCase.optDecls)
		    .assertParseResult(t);

		if (!verifyNullCase.nonNullValue.empty()) {
			createSubScenario(
			    "parseCommandLine",
			    verifyNullCase.subScenario +
			        " errors if non null value is passed",
			    {"--" + verifyNullCase.optionName,
			     verifyNullCase.nonNullValue, "0.ts"},
			    verifyNullCase.optDecls)
			    .assertParseResult(t);
		}

		createSubScenario(
		    "parseCommandLine",
		    verifyNullCase.subScenario +
		        " errors if its followed by another option",
		    {"0.ts", "--strictNullChecks", "--" + verifyNullCase.optionName},
		    verifyNullCase.optDecls)
		    .assertParseResult(t);

		createSubScenario(
		    "parseCommandLine",
		    verifyNullCase.subScenario + " errors if its last option",
		    {"0.ts", "--" + verifyNullCase.optionName},
		    verifyNullCase.optDecls)
		    .assertParseResult(t);
	}
}

// TestParseBuildCommandLine — commandlineparser_test.go:540.
void TestParseBuildCommandLine(T* t) {
	std::vector<subScenarioInput> parseCommandLineSubScenarios = {
	    {"parse build without any options ", {}},
	    {"Parse multiple options", {"--verbose", "--force", "tests"}},
	    {"Parse option with invalid option", {"--verbose", "--invalidOption"}},
	    {"Parse multiple flags with input projects at the end",
	     {"--force", "--verbose", "src", "tests"}},
	    {"Parse multiple flags with input projects in the middle",
	     {"--force", "src", "tests", "--verbose"}},
	    {"Parse multiple flags with input projects in the beginning",
	     {"src", "tests", "--force", "--verbose"}},
	    {"parse build with --incremental", {"--incremental", "tests"}},
	    {"parse build with --locale en-us", {"--locale", "en-us", "src"}},
	    {"parse build with --tsBuildInfoFile",
	     {"--tsBuildInfoFile", "build.tsbuildinfo", "tests"}},
	    {"reports other common may not be used with --build flags",
	     {"--strict"}},
	    {"--clean and --force together is invalid", {"--clean", "--force"}},
	    {"--clean and --verbose together is invalid",
	     {"--clean", "--verbose"}},
	    {"--clean and --watch together is invalid", {"--clean", "--watch"}},
	    {"--watch and --dry together is invalid", {"--watch", "--dry"}},
	    {"parse --watchFile", {"--watchFile", "UseFsEvents", "--verbose"}},
	    {"parse --watchDirectory",
	     {"--watchDirectory", "FixedPollingInterval", "--verbose"}},
	    {"parse --fallbackPolling",
	     {"--fallbackPolling", "PriorityInterval", "--verbose"}},
	    {"parse --synchronousWatchDirectory",
	     {"--synchronousWatchDirectory", "--verbose"}},
	    {"errors on missing argument", {"--verbose", "--fallbackPolling"}},
	    {"errors on invalid excludeDirectories",
	     {"--excludeDirectories", "**/../*"}},
	    {"parse --excludeFiles", {"--excludeFiles", "**/temp/*.ts"}},
	    {"errors on invalid excludeFiles", {"--excludeFiles", "**/../*"}},
	};

	for (const auto& testCase : parseCommandLineSubScenarios) {
		testCase.createSubScenario("parseBuildOptions")
		    .assertBuildParseResult(t);
	}

	std::vector<subScenarioInput> extraScenarios = {
	    {"parse --builders", {"--builders", "2"}},
	    {"--singleThreaded and --builders together",
	     {"--singleThreaded", "--builders", "2"}},
	    {"reports error when --builders is 0", {"--builders", "0"}},
	    {"reports error when --builders is negative",
	     {"--builders", "-1"}},
	    {"reports error when --builders is invalid type",
	     {"--builders", "invalid"}},
	};

	for (const auto& testCase : extraScenarios) {
		testCase.createSubScenario("parseBuildOptions")
		    .assertBuildParseResultWithTsBaseline(t, {});
	}
}

// TestAffectsBuildInfo — commandlineparser_test.go:585.
void TestAffectsBuildInfo(T* t) {
	t->Run(
	    "should have affectsBuildInfo true for every option with affectsSemanticDiagnostics",
	    [&](T* t) {
		    for (const auto* option : tsoptions::OptionsDeclarations()) {
			    if (option->AffectsSemanticDiagnostics) {
				    // semantic diagnostics affect the build info, so ensure they're included
				    assert::Assert(t, option->AffectsBuildInfo);
			    }
		    }
	    });
}

}  // namespace

REGISTER_UNIT_TEST("tsoptions.TestCommandLineParseResult",
                   TestCommandLineParseResult);
REGISTER_UNIT_TEST("tsoptions.TestResponseFileDoesNotPanic",
                   TestResponseFileDoesNotPanic);
REGISTER_UNIT_TEST("tsoptions.TestResponseFileParsing",
                   TestResponseFileParsing);
REGISTER_UNIT_TEST("tsoptions.TestParseCommandLineTypeRootsRelativePath",
                   TestParseCommandLineTypeRootsRelativePath);
REGISTER_UNIT_TEST("tsoptions.TestCustomConditionsNullOverride",
                   TestCustomConditionsNullOverride);
REGISTER_UNIT_TEST("tsoptions.TestParseCommandLineVerifyNull",
                   TestParseCommandLineVerifyNull);
REGISTER_UNIT_TEST("tsoptions.TestParseBuildCommandLine",
                   TestParseBuildCommandLine);
REGISTER_UNIT_TEST("tsoptions.TestAffectsBuildInfo",
                   TestAffectsBuildInfo);
