// tsc.go — port of tsc/internal/execute/tsc.go.

#include "internal/execute/execute.h"

#include "internal/execute/tsc/emit.h"
#include "internal/execute/tsc/help.h"
#include "internal/execute/tsc/init.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <ostream>

#include "internal/core/textchange.h"
#include "internal/execute/build/build.h"
#include "internal/execute/watcher.h"
#include "internal/format/format.h"
#include "internal/json/json.h"
#include "internal/parser/parser.h"
#include "internal/pprof/pprof.h"
#include "internal/tspath/tspath.h"

namespace tsc::pprof {

// dep-stub: pprof.h forward-declares ProfileSession without members; the
// real definition lives in internal/pprof/pprof.cpp (owned by infra1) and
// provides stop(). tsc.go's `defer profileSession.Stop()` needs the member
// declaration visible here.
class ProfileSession {
public:
	void stop();
};

}  // namespace tsc::pprof
namespace tsc::execute {
namespace {

// writeJsonValue — token-level encoding/json marshal of a
// CompilerOptionsValue for the --showConfig output (mirrors
// tsoptions::jsonMarshal's compact output but through the Encoder so
// indentation applies at every level — writeValue would emit nested values
// verbatim).
std::string writeJsonValue(json::Encoder& enc,
                           const tsoptions::CompilerOptionsValue& v) {
	if (v.isNil()) {
		return enc.writeToken(json::Null);
	}
	if (auto* p = v.get<bool>()) {
		return enc.writeToken(json::tokenBool(*p));
	}
	if (auto* p = v.get<int64_t>()) {
		return enc.writeToken(json::tokenInt(*p));
	}
	if (auto* p = v.get<double>()) {
		// encoding/json: integer-valued floats marshal without a fraction.
		if (*p == std::floor(*p) && std::abs(*p) < 1e15) {
			return enc.writeToken(
			    json::tokenInt(static_cast<int64_t>(*p)));
		}
		return enc.writeToken(json::tokenFloat(*p));
	}
	if (auto* p = v.get<Tristate>()) {
		return enc.writeToken(
		    json::tokenInt(static_cast<int64_t>(*p)));
	}
	if (auto* p = v.get<const DiagnosticMessage*>()) {
		// *diagnostics.Message marshals as its message text.
		return enc.writeToken(json::tokenString((*p)->text));
	}
	if (auto* p = v.get<std::string>()) {
		return enc.writeToken(json::tokenString(*p));
	}
	if (auto* p = v.get<tsoptions::JsonStrList>()) {
		auto err = enc.writeToken(json::BeginArray);
		if (!err.empty()) {
			return err;
		}
		for (const auto& e : *p) {
			if ((err = enc.writeToken(json::tokenString(e))),
			    !err.empty()) {
				return err;
			}
		}
		return enc.writeToken(json::EndArray);
	}
	if (auto* p = v.get<tsoptions::JsonArray>()) {
		auto err = enc.writeToken(json::BeginArray);
		if (!err.empty()) {
			return err;
		}
		for (const auto& e : *p) {
			if ((err = writeJsonValue(enc, e)), !err.empty()) {
				return err;
			}
		}
		return enc.writeToken(json::EndArray);
	}
	if (auto* p = v.get<tsoptions::JsonObjectPtr>()) {
		if (*p == nullptr) {
			return enc.writeToken(json::Null);
		}
		auto err = enc.writeToken(json::BeginObject);
		if (!err.empty()) {
			return err;
		}
		for (const auto& k : (*p)->Keys()) {
			if ((err = enc.writeToken(json::tokenString(k))),
			    !err.empty()) {
				return err;
			}
			if ((err = writeJsonValue(enc, *(*p)->Get(k).first)),
			    !err.empty()) {
				return err;
			}
		}
		return enc.writeToken(json::EndObject);
	}
	if (auto* p = v.get<tsoptions::JsonGoMapPtr>()) {
		if (*p == nullptr) {
			return enc.writeToken(json::Null);
		}
		// Go marshals map[string]any with keys in sorted order.
		std::vector<std::string> keys;
		keys.reserve((*p)->size());
		for (const auto& [k, unused] : **p) {
			(void)unused;
			keys.push_back(k);
		}
		std::sort(keys.begin(), keys.end());
		auto err = enc.writeToken(json::BeginObject);
		if (!err.empty()) {
			return err;
		}
		for (const auto& k : keys) {
			if ((err = enc.writeToken(json::tokenString(k))),
			    !err.empty()) {
				return err;
			}
			if ((err = writeJsonValue(enc, (*p)->at(k))),
			    !err.empty()) {
				return err;
			}
		}
		return enc.writeToken(json::EndObject);
	}
	return enc.writeToken(json::Null);
}

}  // namespace
}  // namespace tsc::execute

namespace tsc::tsoptions {

// marshalJSONTo — showconfig.go:53-59. Emits fields in Go struct-declaration
// order: compilerOptions always, references/files/include/exclude/
// compileOnSave only when non-empty/non-null (`json:",omitzero"`).
std::string marshalJSONTo(json::Encoder& enc, const TSConfig& c) {
	auto err = enc.writeToken(json::BeginObject);
	if (!err.empty()) {
		return err;
	}
	if ((err = enc.writeToken(json::tokenString("compilerOptions"))),
	    !err.empty()) {
		return err;
	}
	if (c.CompilerOptions != nullptr) {
		if ((err = tsc::execute::writeJsonValue(
		         enc, tsoptions::CompilerOptionsValue(c.CompilerOptions))),
		    !err.empty()) {
			return err;
		}
	} else {
		if ((err = enc.writeToken(json::Null)), !err.empty()) {
			return err;
		}
	}
	if (!c.References.empty()) {
		if ((err = enc.writeToken(json::tokenString("references"))),
		    !err.empty()) {
			return err;
		}
		if ((err = tsc::execute::writeJsonValue(
		         enc, tsoptions::CompilerOptionsValue(c.References))),
		    !err.empty()) {
			return err;
		}
	}
	if (!c.Files.empty()) {
		if ((err = enc.writeToken(json::tokenString("files"))),
		    !err.empty()) {
			return err;
		}
		if ((err = tsc::execute::writeJsonValue(
		         enc, tsoptions::CompilerOptionsValue(
		                 tsoptions::JsonStrList(c.Files)))),
		    !err.empty()) {
			return err;
		}
	}
	if (!c.Include.empty()) {
		if ((err = enc.writeToken(json::tokenString("include"))),
		    !err.empty()) {
			return err;
		}
		if ((err = tsc::execute::writeJsonValue(
		         enc, tsoptions::CompilerOptionsValue(
		                 tsoptions::JsonStrList(c.Include)))),
		    !err.empty()) {
			return err;
		}
	}
	if (!c.Exclude.empty()) {
		if ((err = enc.writeToken(json::tokenString("exclude"))),
		    !err.empty()) {
			return err;
		}
		if ((err = tsc::execute::writeJsonValue(
		         enc, tsoptions::CompilerOptionsValue(
		                 tsoptions::JsonStrList(c.Exclude)))),
		    !err.empty()) {
			return err;
		}
	}
	if (c.CompileOnSave != nullptr) {
		if ((err = enc.writeToken(json::tokenString("compileOnSave"))),
		    !err.empty()) {
			return err;
		}
		if ((err = enc.writeToken(json::tokenBool(*c.CompileOnSave))),
		    !err.empty()) {
			return err;
		}
	}
	return enc.writeToken(json::EndObject);
}

}  // namespace tsc::tsoptions


namespace tsc::execute {
namespace {

// profileSessionDefer — `defer profileSession.Stop()`: fires the stop on
// every exit path of the enclosing function (Go defer semantics).
struct profileSessionDefer {
	pprof::ProfileSession* session = nullptr;
	~profileSessionDefer() {
		if (session != nullptr) {
			session->stop();
		}
	}
};

}  // namespace

// startTracingIfNeeded — tsc.go:29.
tracing::Tracing* startTracingIfNeeded(
    tsc::System* sys, tsoptions::ParsedCommandLine* config,
    tsc::CommandLineTesting* testing) {
	std::string traceDir = config->CompilerOptions()->GenerateTrace;
	if (traceDir.empty()) {
		return nullptr;
	}
	std::string configFilePath;
	if (config->ConfigFile != nullptr &&
	    config->ConfigFile->SourceFile != nullptr) {
		configFilePath = config->ConfigFile->SourceFile->FileName();
	}
	auto [tr, err] = tracing::StartTracing(sys->fs().get(), traceDir,
	                                       configFilePath, testing != nullptr);
	if (err) {
		*sys->Writer()
		    << "Warning: Failed to start tracing: " << err->Error() << '\n';
	}
	return tr;
}

// stopTracing — tsc.go:45.
void stopTracing(tsc::System* sys, tracing::Tracing* tr) {
	if (tr == nullptr) {
		return;
	}
	if (auto err = tracing::StopTracing(tr); err) {
		*sys->Writer() << "Warning: Failed to stop tracing: " << err->Error()
		               << '\n';
	}
}

// CommandLine — tsc.go:55.
tsc::CommandLineResult CommandLine(
    gostd::Context ctx, tsc::System* sys,
    const std::vector<std::string>& commandLineArgs,
    tsc::CommandLineTesting* testing) {
	if (!commandLineArgs.empty()) {
		std::string first = commandLineArgs[0];
		for (auto& c : first) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		if (first == "-b" || first == "--b" || first == "-build" ||
		    first == "--build") {
			return tscBuildCompilation(
			    ctx, sys,
			    tsoptions::ParseBuildCommandLine(commandLineArgs, sys),
			    testing);
		}
		// case "-f":
		// 	return fmtMain(sys, commandLineArgs[1], commandLineArgs[1])
	}

	return tscCompilation(
	    ctx, sys, tsoptions::ParseCommandLine(commandLineArgs, sys), testing);
}

// fmtMain — tsc.go:69.
tsc::ExitStatus fmtMain(tsc::System* sys, std::string input,
                        std::string output) {
	auto ctx = format::WithFormatCodeSettings(
	    format::FormatRequestContext{},
	    lsutil::GetDefaultFormatCodeSettings(), "\n");
	input = tspath::toPath(input, sys->GetCurrentDirectory(),
	                       sys->fs()->UseCaseSensitiveFileNames());
	output = tspath::toPath(output, sys->GetCurrentDirectory(),
	                        sys->fs()->UseCaseSensitiveFileNames());
	auto [fileContent, ok] = sys->fs()->ReadFile(input);
	if (!ok) {
		*sys->Writer() << "File not found: " << input << '\n';
		return tsc::ExitStatusNotImplemented;
	}
	std::string text = fileContent;
	auto pathified = tspath::toPath(input, sys->GetCurrentDirectory(), true);
	SourceFileParseOptions parseOptions;
	parseOptions.FileName = pathified;
	parseOptions.Path = pathified;
	auto* sourceFile = ::tsc::parseSourceFile(
	    parseOptions, text, getScriptKindFromFileName(pathified));
	auto edits = format::FormatDocument(ctx, sourceFile);
	auto newText = ::tsc::ApplyBulkEdits(text, edits);

	if (auto err = sys->fs()->WriteFile(output, newText); err) {
		*sys->Writer() << err.str() << '\n';
		return tsc::ExitStatusNotImplemented;
	}
	return tsc::ExitStatusSuccess;
}

// tscBuildCompilation — tsc.go:95.
tsc::CommandLineResult tscBuildCompilation(
    gostd::Context ctx, tsc::System* sys,
    tsoptions::ParsedBuildCommandLine* buildCommand,
    tsc::CommandLineTesting* testing) {
	auto locale = buildCommand->Locale();
	auto reportDiagnostic = tsc::CreateDiagnosticReporter(
	    sys, sys->Writer(), locale, buildCommand->CompilerOptions);

	if (!buildCommand->Errors.empty()) {
		for (auto* err : buildCommand->Errors) {
			reportDiagnostic(err);
		}
		return {.Status = tsc::ExitStatusDiagnosticsPresent_OutputsSkipped};
	}

	profileSessionDefer profileSession;
	if (auto pprofDir = buildCommand->CompilerOptions->PprofDir;
	    !pprofDir.empty()) {
		// !!! stderr?
		profileSession.session =
		    pprof::beginProfiling(pprofDir, sys->Writer());
	}

	if (tristateIsTrue(buildCommand->CompilerOptions->Help)) {
		tsc::PrintVersion(sys, locale);
		tsc::PrintBuildHelp(sys, locale, tsoptions::BuildOpts());
		return {.Status = tsc::ExitStatusSuccess};
	}

	auto* orchestrator = build::NewOrchestrator(
	    build::Options{.Sys = sys, .Command = buildCommand,
	                   .Testing = testing});
	return orchestrator->Start();
}

// tscCompilation — tsc.go:122.
tsc::CommandLineResult tscCompilation(
    gostd::Context ctx, tsc::System* sys,
    tsoptions::ParsedCommandLine* commandLine,
    tsc::CommandLineTesting* testing) {
	std::string configFileName;
	auto locale = commandLine->Locale();
	auto reportDiagnostic = tsc::CreateDiagnosticReporter(
	    sys, sys->Writer(), locale, commandLine->CompilerOptions());

	if (!commandLine->Errors.empty()) {
		for (auto* e : commandLine->Errors) {
			reportDiagnostic(e);
		}
		return {.Status = tsc::ExitStatusDiagnosticsPresent_OutputsSkipped};
	}

	profileSessionDefer profileSession;
	if (auto pprofDir = commandLine->CompilerOptions()->PprofDir;
	    !pprofDir.empty()) {
		// !!! stderr?
		profileSession.session =
		    pprof::beginProfiling(pprofDir, sys->Writer());
	}

	if (tristateIsTrue(commandLine->CompilerOptions()->Init)) {
		tsc::WriteConfigFile(sys, locale, reportDiagnostic,
		                     commandLine->Raw.asObject());
		return {.Status = tsc::ExitStatusSuccess};
	}

	if (tristateIsTrue(commandLine->CompilerOptions()->Version)) {
		tsc::PrintVersion(sys, locale);
		return {.Status = tsc::ExitStatusSuccess};
	}

	if (tristateIsTrue(commandLine->CompilerOptions()->Help) ||
	    tristateIsTrue(commandLine->CompilerOptions()->All)) {
		tsc::PrintHelp(sys, locale, commandLine);
		return {.Status = tsc::ExitStatusSuccess};
	}

	if (tristateIsTrue(commandLine->CompilerOptions()->Watch) &&
	    tristateIsTrue(commandLine->CompilerOptions()->ListFilesOnly)) {
		reportDiagnostic(tsoptions::newCompilerDiagnostic(
		    Options_0_and_1_cannot_be_combined,
		    {"watch", "listFilesOnly"}));
		return {.Status = tsc::ExitStatusDiagnosticsPresent_OutputsSkipped};
	}

	if (!commandLine->CompilerOptions()->Project.empty()) {
		if (!commandLine->FileNames().empty()) {
			reportDiagnostic(tsoptions::newCompilerDiagnostic(
			    
			        Option_project_cannot_be_mixed_with_source_files_on_a_command_line));
			return {.Status =
			            tsc::ExitStatusDiagnosticsPresent_OutputsSkipped};
		}

		auto fileOrDirectory = tspath::normalizePath(
		    commandLine->CompilerOptions()->Project);
		if (sys->fs()->DirectoryExists(fileOrDirectory)) {
			configFileName =
			    tspath::combinePaths(fileOrDirectory, {"tsconfig.json"});
			if (!sys->fs()->FileExists(configFileName)) {
				reportDiagnostic(tsoptions::newCompilerDiagnostic(
				    
				        Cannot_find_a_tsconfig_json_file_at_the_current_directory_Colon_0,
				    {configFileName}));
				return {.Status = tsc::
				            ExitStatusDiagnosticsPresent_OutputsSkipped};
			}
		} else {
			configFileName = fileOrDirectory;
			if (!sys->fs()->FileExists(configFileName)) {
				reportDiagnostic(tsoptions::newCompilerDiagnostic(
				    The_specified_path_does_not_exist_Colon_0,
				    {fileOrDirectory}));
				return {.Status = tsc::
				            ExitStatusDiagnosticsPresent_OutputsSkipped};
			}
		}
	} else if (!tristateIsTrue(commandLine->CompilerOptions()->IgnoreConfig) ||
	           commandLine->FileNames().empty()) {
		auto searchPath =
		    tspath::normalizePath(sys->GetCurrentDirectory());
		configFileName = findConfigFile(
		    searchPath,
		    [sys](const std::string& f) { return sys->fs()->FileExists(f); },
		    "tsconfig.json");
		if (!commandLine->FileNames().empty()) {
			if (!configFileName.empty()) {
				// Error to not specify config file
				reportDiagnostic(tsoptions::newCompilerDiagnostic(
				    
				        X_tsconfig_json_is_present_but_will_not_be_loaded_if_files_are_specified_on_commandline_Use_ignoreConfig_to_skip_this_error));
				return {.Status = tsc::
				            ExitStatusDiagnosticsPresent_OutputsSkipped};
			}
		} else if (configFileName.empty()) {
			if (tristateIsTrue(commandLine->CompilerOptions()->ShowConfig)) {
				reportDiagnostic(tsoptions::newCompilerDiagnostic(
				    
				        Cannot_find_a_tsconfig_json_file_at_the_current_directory_Colon_0,
				    {tspath::normalizePath(
				        sys->GetCurrentDirectory())}));
			} else {
				tsc::PrintVersion(sys, locale);
				tsc::PrintHelp(sys, locale, commandLine);
			}
			return {.Status =
			            tsc::ExitStatusDiagnosticsPresent_OutputsSkipped};
		}
	}

	// !!! convert to options with absolute paths is usually done here, but for ease of implementation, it's done in `tsoptions.ParseCommandLine()`
	auto* compilerOptionsFromCommandLine = commandLine->CompilerOptions();
	auto* configForCompilation = commandLine;
	auto* extendedConfigCache = new tsc::ExtendedConfigCache();
	tsc::CompileTimes compileTimes;
	tsoptions::JsonObjectPtr commandLineRaw;
	if (!configFileName.empty()) {
		auto configStart = sys->Now();
		if (auto* raw = commandLine->Raw.get<tsoptions::JsonObjectPtr>();
		    raw != nullptr && *raw != nullptr) {
			// Wrap command line options in a "compilerOptions" key to match
			// tsconfig.json structure
			auto wrapped = std::make_shared<tsoptions::JsonObject>();
			wrapped->Set("compilerOptions", *raw);
			commandLineRaw = wrapped;
		}
		auto [configParseResult, errors] =
		    tsoptions::GetParsedCommandLineOfConfigFile(
		        configFileName, compilerOptionsFromCommandLine,
		        commandLineRaw, sys, extendedConfigCache);
		compileTimes.ConfigTime =
		    std::chrono::duration_cast<gostd::Duration>(sys->Now() -
		                                              configStart);
		if (!errors.empty()) {
			// these are unrecoverable errors--exit to report them as
			// diagnostics
			for (auto* e : errors) {
				reportDiagnostic(e);
			}
			return {.Status =
			            tsc::ExitStatusDiagnosticsPresent_OutputsGenerated};
		}
		configForCompilation = configParseResult;
		// Updater to reflect pretty
		reportDiagnostic = tsc::CreateDiagnosticReporter(
		    sys, sys->Writer(), locale, commandLine->CompilerOptions());
	}

	auto reportErrorSummary = tsc::CreateReportErrorSummary(
	    sys, locale, configForCompilation->CompilerOptions());
	if (tristateIsTrue(compilerOptionsFromCommandLine->ShowConfig)) {
		showConfig(sys, configForCompilation, configFileName);
		return {.Status = tsc::ExitStatusSuccess};
	}
	if (tristateIsTrue(configForCompilation->CompilerOptions()->Watch)) {
		auto* watcher = createWatcher(
		    sys, configForCompilation, compilerOptionsFromCommandLine,
		    commandLineRaw, reportDiagnostic, reportErrorSummary, testing);
		watcher->start(ctx);
		return {.Status = tsc::ExitStatusSuccess, .Watcher = watcher};
	}
	if (configForCompilation->CompilerOptions()->IsIncremental()) {
		return performIncrementalCompilation(
		    ctx, sys, configForCompilation, reportDiagnostic,
		    reportErrorSummary, extendedConfigCache, &compileTimes, testing);
	}
	return performCompilation(ctx, sys, configForCompilation,
	                          reportDiagnostic, reportErrorSummary,
	                          extendedConfigCache, &compileTimes, testing);
}

// findConfigFile — tsc.go:249.
std::string findConfigFile(
    const std::string& searchPath,
    const std::function<bool(const std::string&)>& fileExists,
    const std::string& configName) {
	auto [result, ok] = tspath::forEachAncestorDirectory<std::string>(
	    searchPath, [&](std::string_view ancestor) {
		    auto fullConfigName =
		        tspath::combinePaths(ancestor, {configName});
		    return std::pair<std::string, bool>{fullConfigName,
		                                        fileExists(fullConfigName)};
	    });
	if (!ok) {
		return "";
	}
	return result;
}

// getTraceFromSys — tsc.go:262.
tsc::TraceFn getTraceFromSys(tsc::System* sys, locale::Locale locale,
                             tsc::CommandLineTesting* testing) {
	return tsc::GetTraceWithWriterFromSys(sys->Writer(), locale, testing);
}

// performIncrementalCompilation — tsc.go:266.
tsc::CommandLineResult performIncrementalCompilation(
    gostd::Context ctx, tsc::System* sys,
    tsoptions::ParsedCommandLine* config,
    const tsc::DiagnosticReporter& reportDiagnostic,
    const tsc::DiagnosticsReporter& reportErrorSummary,
    tsoptions::ExtendedConfigCache* extendedConfigCache,
    tsc::CompileTimes* compileTimes, tsc::CommandLineTesting* testing) {
	auto contentMapperHost =
	    tsc::NewContentMapperHost(ctx, sys, config->CompilerOptions());
	auto contentMapperProject =
	    getContentMapperProject(contentMapperHost, config);

	auto* host = compiler::NewCachedFSCompilerHost(
	    sys->GetCurrentDirectory(), sys->fs(), sys->DefaultLibraryPath(),
	    extendedConfigCache,
	    getTraceFromSys(sys, config->Locale(), testing),
	    contentMapperProject);
	auto buildInfoReadStart = sys->Now();
	auto* oldProgram = incremental::ReadBuildInfoProgram(
	    config, incremental::NewBuildInfoReader(host), host);
	compileTimes->BuildInfoReadTime =
	    std::chrono::duration_cast<gostd::Duration>(sys->Now() -
	                                              buildInfoReadStart);

	auto* tr = startTracingIfNeeded(sys, config, testing);

	auto parseStart = sys->Now();
	compiler::ProgramOptions programOptions;
	programOptions.Config = config;
	programOptions.Host = host;
	programOptions.Tracing = tr;
	auto* program = compiler::NewProgram(programOptions);
	compileTimes->ParseTime =
	    std::chrono::duration_cast<gostd::Duration>(sys->Now() - parseStart);
	auto changesComputeStart = sys->Now();
	auto* incrementalProgram = incremental::NewProgram(
	    program, oldProgram, incremental::CreateHost(host),
	    [sys] { return sys->Now(); }, testing != nullptr);
	compileTimes->ChangesComputeTime =
	    std::chrono::duration_cast<gostd::Duration>(sys->Now() -
	                                              changesComputeStart);
	if (contentMapperHost != nullptr) {
		compileTimes->ContentMapperTimes = contentMapperHost->Timings();
	}
	tsc::EmitInput emitInput;
	emitInput.Sys = sys;
	emitInput.ProgramLike = incrementalProgram;
	emitInput.Program = incrementalProgram->GetProgram();
	emitInput.Config = config;
	emitInput.ReportDiagnostic = reportDiagnostic;
	emitInput.ReportErrorSummary = reportErrorSummary;
	emitInput.Writer = sys->Writer();
	emitInput.CompileTimes = compileTimes;
	emitInput.Testing = testing;
	emitInput.Tracing = tr;
	auto [result, _1] = tsc::EmitAndReportStatistics(emitInput);

	stopTracing(sys, tr);

	if (testing != nullptr) {
		testing->OnProgram(incrementalProgram);
	}
	if (contentMapperProject != nullptr) {
		// Go `defer contentMapperProject.Close()`.
		contentMapperProject->Close();
	}
	return {.Status = result.Status};
}

// performCompilation — tsc.go:330.
tsc::CommandLineResult performCompilation(
    gostd::Context ctx, tsc::System* sys,
    tsoptions::ParsedCommandLine* config,
    const tsc::DiagnosticReporter& reportDiagnostic,
    const tsc::DiagnosticsReporter& reportErrorSummary,
    tsoptions::ExtendedConfigCache* extendedConfigCache,
    tsc::CompileTimes* compileTimes, tsc::CommandLineTesting* testing) {
	auto contentMapperHost =
	    tsc::NewContentMapperHost(ctx, sys, config->CompilerOptions());
	auto contentMapperProject =
	    getContentMapperProject(contentMapperHost, config);

	auto* host = compiler::NewCachedFSCompilerHost(
	    sys->GetCurrentDirectory(), sys->fs(), sys->DefaultLibraryPath(),
	    extendedConfigCache,
	    getTraceFromSys(sys, config->Locale(), testing),
	    contentMapperProject);

	auto* tr = startTracingIfNeeded(sys, config, testing);

	auto parseStart = sys->Now();
	compiler::ProgramOptions programOptions;
	programOptions.Config = config;
	programOptions.Host = host;
	programOptions.Tracing = tr;
	auto* program = compiler::NewProgram(programOptions);
	compileTimes->ParseTime =
	    std::chrono::duration_cast<gostd::Duration>(sys->Now() - parseStart);
	if (contentMapperHost != nullptr) {
		compileTimes->ContentMapperTimes = contentMapperHost->Timings();
	}
	tsc::EmitInput emitInput;
	emitInput.Sys = sys;
	emitInput.ProgramLike = program;
	emitInput.Program = program;
	emitInput.Config = config;
	emitInput.ReportDiagnostic = reportDiagnostic;
	emitInput.ReportErrorSummary = reportErrorSummary;
	emitInput.Writer = sys->Writer();
	emitInput.CompileTimes = compileTimes;
	emitInput.Testing = testing;
	emitInput.Tracing = tr;
	auto [result, _1] = tsc::EmitAndReportStatistics(emitInput);

	stopTracing(sys, tr);

	if (contentMapperProject != nullptr) {
		contentMapperProject->Close();
	}
	return {.Status = result.Status};
}

// getContentMapperProject — tsc.go:389.
std::shared_ptr<contentmapper::Project> getContentMapperProject(
    const std::shared_ptr<contentmapper::Host>& host,
    tsoptions::ParsedCommandLine* config) {
	if (host == nullptr || config->ContentMappers().empty()) {
		return nullptr;
	}
	contentmapper::ProjectSpec spec;
	spec.ConfigFileName = config->ConfigName();
	spec.Mappers = config->ContentMappers();
	spec.CompilerOptions = config->CompilerOptions();
	return host->Project(spec);
}

// showConfig — tsc.go:399.
void showConfig(tsc::System* sys, tsoptions::ParsedCommandLine* config,
                const std::string& configFileName) {
	auto* tsConfig =
	    tsoptions::ConvertToTSConfig(config, configFileName);
	(void)json::marshalIndentWrite(*sys->Writer(), *tsConfig, "", "    ");
}

}  // namespace tsc::execute
