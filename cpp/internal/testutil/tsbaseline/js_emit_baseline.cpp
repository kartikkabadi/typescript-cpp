// js_emit_baseline.go — the JS emit baseliner: DoJSEmitBaseline +
// fileOutput, declarationCompilationContext plumbing.
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/collections/collections.h"
#include "internal/core/types.h"
#include "internal/diagnosticwriter/diagnosticwriter.h"
#include "internal/outputpaths/outputpaths.h"
#include "internal/parser/parser.h"
#include "internal/testutil/baseline/baseline.h"
#include "internal/testutil/harnessutil/harnessutil.h"
#include "internal/testutil/tsbaseline/tsbaseline.h"
#include "internal/testutil/tsbaseline/tsbaselineutil.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tspath/tspath.h"

namespace tsc::testutil::tsbaseline {

using harnessutil::CompilationResult;
using harnessutil::HarnessOptions;
using harnessutil::TestFile;

namespace {

// declarationCompilationContext — js_emit_baseline.go:143.
struct declarationCompilationContext {
	std::vector<TestFile*> declInputFiles;
	std::vector<TestFile*> declOtherFiles;
	HarnessOptions* harnessSettings;
	CompilerOptions* options;
	std::string currentDirectory;
	tsoptions::ParsedCommandLine* config;
};

// declarationCompilationResult — js_emit_baseline.go:265.
struct declarationCompilationResult {
	std::vector<TestFile*> declInputFiles;
	std::vector<TestFile*> declOtherFiles;
	CompilationResult* declResult;
};

// prepareDeclarationCompilationContext — js_emit_baseline.go:153.
declarationCompilationContext* prepareDeclarationCompilationContext(
    const std::vector<TestFile*>& inputFiles,
    const std::vector<TestFile*>& otherFiles, CompilationResult* result,
    HarnessOptions* harnessSettings, CompilerOptions* options,
    // Current directory is needed for rwcRunner to be able to use
    // currentDirectory defined in json file
    const std::string& currentDirectory) {
	if (options->Declaration == Tristate::True &&
	    result->Diagnostics.empty()) {
		if (options->EmitDeclarationOnly == Tristate::True) {
			if (result->JS.Size() > 0) {
				throw std::runtime_error(
				    "Only declaration files should be generated when "
				    "emitDeclarationOnly:true");
			}
			if (result->DTS.Size() == 0 &&
			    options->NoEmit != Tristate::True) {
				throw std::runtime_error(
				    "Expected at least one declaration file to be "
				    "emitted when emitDeclarationOnly:true and no "
				    "errors were generated");
			}
		} else {
			bool anyContentMapped = false;
			for (auto* file : result->Program->GetSourceFiles()) {
				if (!file->ContentMapper().empty()) {
					anyContentMapped = true;
					break;
				}
			}
			if (!anyContentMapped &&
			    result->DTS.Size() !=
			        (size_t)result->GetNumberOfJSFiles(
			            false /*includeJson*/)) {
				throw std::runtime_error(
				    "There were no errors and declFiles generated did "
				    "not match number of js files generated");
			}
		}
	}

	std::vector<TestFile*> declInputFiles;
	std::vector<TestFile*> declOtherFiles;

	auto findUnit = [](const std::string& fileName,
	                   const std::vector<TestFile*>& units) -> TestFile* {
		for (auto* unit : units) {
			if (unit->UnitName == fileName) {
				return unit;
			}
		}
		return nullptr;
	};

	auto findResultCodeFile =
	    [&](const std::string& fileName) -> TestFile* {
		auto* sourceFile = result->Program->GetSourceFile(fileName);
		if (sourceFile == nullptr) {
			throw std::runtime_error("Program has no source file with name '" +
			                         fileName + "'");
		}
		// Is this file going to be emitted separately
		std::string sourceFileName;

		if (!options->OutDir.empty()) {
			auto sourceFilePath = tspath::getNormalizedAbsolutePath(
			    sourceFile->FileName(),
			    result->Host->GetCurrentDirectory());
			auto pos = sourceFilePath.find(
			    result->Program->CommonSourceDirectory());
			if (pos != std::string::npos) {
				sourceFilePath.replace(
				    pos, result->Program->CommonSourceDirectory().size(), "");
			}
			sourceFileName =
			    tspath::combinePaths(options->OutDir, {sourceFilePath});
		} else {
			sourceFileName = sourceFile->FileName();
		}

		auto dTsFileName = outputpaths::ChangeToDeclarationExtension(
		    sourceFileName, result->Program->GetProgram());
		return result->DTS.GetOrZero(dTsFileName);
	};

	auto addDtsFile = [&](auto& self, const TestFile* file,
	                      std::vector<TestFile*>& dtsFiles)
	    -> std::vector<TestFile*>& {
		if (tspath::isDeclarationFileName(file->UnitName) ||
		    tspath::hasJSONFileExtension(file->UnitName)) {
			dtsFiles.push_back(const_cast<TestFile*>(file));
		} else if (auto* sourceFile =
		               result->Program->GetSourceFile(file->UnitName);
		           sourceFile != nullptr &&
		           (tspath::hasTSFileExtension(file->UnitName) ||
		            (tspath::hasJSFileExtension(file->UnitName) &&
		             options->GetAllowJS()) ||
		            !sourceFile->ContentMapper().empty())) {
			auto* declFile = findResultCodeFile(file->UnitName);
			if (declFile != nullptr &&
			    findUnit(declFile->UnitName, declInputFiles) == nullptr &&
			    findUnit(declFile->UnitName, declOtherFiles) == nullptr) {
				auto content = declFile->Content;
				if (!content.empty() && content.substr(0, 3) == "\xef\xbb\xbf") {
					content = content.substr(3);
				}
				dtsFiles.push_back(new TestFile{declFile->UnitName, content});
			}
		}
		return dtsFiles;
	};

	// if the .d.ts is non-empty, confirm it compiles correctly as well
	if (options->Declaration == Tristate::True &&
	    result->Diagnostics.empty() && result->DTS.Size() > 0) {
		for (auto* file : inputFiles) {
			addDtsFile(addDtsFile, file, declInputFiles);
		}
		for (auto* file : otherFiles) {
			addDtsFile(addDtsFile, file, declOtherFiles);
		}
		auto* ctx = new declarationCompilationContext();
		ctx->declInputFiles = declInputFiles;
		ctx->declOtherFiles = declOtherFiles;
		ctx->harnessSettings = harnessSettings;
		ctx->options = options;
		ctx->currentDirectory = !currentDirectory.empty()
		                            ? currentDirectory
		                            : harnessSettings->CurrentDirectory;
		ctx->config = result->Program->GetProgram()->CommandLine();
		return ctx;
	}
	return nullptr;
}

// compileDeclarationFiles — js_emit_baseline.go:271.
declarationCompilationResult* compileDeclarationFiles(
    gostd::testing::T* t, declarationCompilationContext* context,
    const std::unordered_map<std::string, std::string>& symlinks) {
	if (context == nullptr) {
		return nullptr;
	}
	tsoptions::ParsedCommandLine* tsconfig = nullptr;
	if (context->config != nullptr && context->config->ConfigFile != nullptr) {
		tsconfig = new tsoptions::ParsedCommandLine();
		auto* parsed = new tsoptions::ParsedOptions();
		parsed->ContentMappers = context->config->ContentMappers();
		tsconfig->ParsedConfig = parsed;
		tsconfig->ConfigFile = context->config->ConfigFile;
	}
	auto* declFileCompilationResult =
	    harnessutil::CompileFilesEx(t, context->declInputFiles,
	                                context->declOtherFiles,
	                                context->harnessSettings, context->options,
	                                context->currentDirectory, symlinks,
	                                tsconfig);
	auto* r = new declarationCompilationResult();
	r->declInputFiles = context->declInputFiles;
	r->declOtherFiles = context->declOtherFiles;
	r->declResult = declFileCompilationResult;
	return r;
}

}  // namespace

// DoJSEmitBaseline — js_emit_baseline.go:19.
void DoJSEmitBaseline(
    gostd::testing::T* t, const std::string& baselinePathIn,
    const std::string& header, CompilerOptions* options,
    harnessutil::CompilationResult* result,
    const std::vector<harnessutil::TestFile*>& tsConfigFiles,
    const std::vector<harnessutil::TestFile*>& toBeCompiled,
    const std::vector<harnessutil::TestFile*>& otherFiles,
    harnessutil::HarnessOptions* harnessSettings,
    const baseline::Options& opts) {
	auto baselinePath = baselinePathIn;
	if (options->NoEmit != Tristate::True &&
	    options->EmitDeclarationOnly != Tristate::True &&
	    result->JS.Size() == 0 && result->Diagnostics.empty()) {
		t->Fatal({"Expected at least one js file to be emitted or at least "
		          "one error to be created."});
	}

	// check js output
	std::string tsCode;
	// tsSources := core.Concatenate(otherFiles, toBeCompiled)
	std::vector<TestFile*> tsSources = otherFiles;
	tsSources.insert(tsSources.end(), toBeCompiled.begin(),
	                 toBeCompiled.end());
	tsCode += "//// [";
	tsCode += header;
	tsCode += "] ////\r\n\r\n";

	for (size_t i = 0; i < tsSources.size(); i++) {
		auto* file = tsSources[i];
		tsCode += "//// [";
		tsCode += tspath::getBaseFileName(file->UnitName);
		tsCode += "]\r\n";
		tsCode += file->Content;
		if (i < tsSources.size() - 1) {
			tsCode += "\r\n";
		}
	}

	std::string jsCode;
	for (auto* file : result->JS.Values()) {
		if (jsCode.size() > 0 &&
		    (jsCode.empty() || jsCode.back() != '\n')) {
			jsCode += "\r\n";
		}
		if (result->Diagnostics.empty() &&
		    tspath::fileExtensionIs(file->UnitName, tspath::extensionJson)) {
			SourceFileParseOptions parseOpts;
			parseOpts.FileName = file->UnitName;
			parseOpts.Path = file->UnitName;
			auto* fileParseResult = parseSourceFile(
			    parseOpts, file->Content, ScriptKind::JSON);
			if (!fileParseResult->diagnostics.empty()) {
				std::vector<TestFile*> files{
				    const_cast<TestFile*>(file)};
				jsCode += GetErrorBaseline(
				    t, files,
				    diagnosticwriter::wrapASTDiagnostics(
				        fileParseResult->diagnostics),
				    diagnosticwriter::compareASTDiagnostics,
				    false /*pretty*/);
				continue;
			}
		}
		jsCode += fileOutput(file, harnessSettings);
	}

	if (result->DTS.Size() > 0) {
		jsCode += "\r\n\r\n";
		for (auto* declFile : result->DTS.Values()) {
			jsCode += fileOutput(declFile, harnessSettings);
		}
	}

	auto* declFileContext = prepareDeclarationCompilationContext(
	    toBeCompiled, otherFiles, result, harnessSettings, options,
	    "" /*currentDirectory*/);
	auto* declFileCompilationResult =
	    compileDeclarationFiles(t, declFileContext, result->Symlinks);

	if (declFileCompilationResult != nullptr &&
	    !declFileCompilationResult->declResult->Diagnostics.empty()) {
		jsCode += "\r\n\r\n//// [DtsFileErrors]\r\n";
		jsCode += "\r\n\r\n";
		// slices.Concat(tsConfigFiles, declInputFiles, declOtherFiles)
		std::vector<TestFile*> allFiles = tsConfigFiles;
		allFiles.insert(
		    allFiles.end(),
		    declFileCompilationResult->declInputFiles.begin(),
		    declFileCompilationResult->declInputFiles.end());
		allFiles.insert(
		    allFiles.end(),
		    declFileCompilationResult->declOtherFiles.begin(),
		    declFileCompilationResult->declOtherFiles.end());
		jsCode += GetErrorBaseline(
		    t, allFiles,
		    diagnosticwriter::wrapASTDiagnostics(
		        declFileCompilationResult->declResult->Diagnostics),
		    diagnosticwriter::compareASTDiagnostics, false /*pretty*/);
	}

	if (options->NoCheck != Tristate::True &&
	    options->NoEmit != Tristate::True) {
		harnessutil::TestConfiguration testConfig;
		testConfig["noCheck"] = "true";
		auto* withoutChecking = result->Repeat(testConfig);
		auto compareResultFileSets =
		    [&](collections::OrderedMap<std::string, TestFile*>* a,
		        collections::OrderedMap<std::string, TestFile*>* b) {
			    for (auto& key : a->Keys()) {
				    auto* doc = a->GetOrZero(key);
				    auto* original = b->GetOrZero(key);
				    if (original == nullptr) {
					    jsCode += "\r\n\r\n!!!! File ";
					    jsCode += removeTestPathPrefixes(
					        doc->UnitName,
					        false /*retainTrailingDirectorySeparator*/);
					    jsCode +=
					        " missing from original emit, but "
					        "present in noCheck emit\r\n";
					    jsCode += fileOutput(doc, harnessSettings);
				    } else if (original->Content != doc->Content) {
					    jsCode += "\r\n\r\n!!!! File ";
					    jsCode += removeTestPathPrefixes(
					        doc->UnitName,
					        false /*retainTrailingDirectorySeparator*/);
					    jsCode +=
					        " differs from original emit in noCheck "
					        "emit\r\n";
					    std::string fileName;
					    if (harnessSettings->FullEmitPaths) {
						    fileName = removeTestPathPrefixes(
						        doc->UnitName,
						        false /*retainTrailingDirectorySeparator*/);
					    } else {
						    fileName =
						        std::string(tspath::getBaseFileName(
						            doc->UnitName));
					    }
					    jsCode += "//// [";
					    jsCode += fileName;
					    jsCode += "]\r\n";
					    auto& expected = original->Content;
					    auto& actual = doc->Content;
					    jsCode += baseline::DiffText(
					        "Expected\tThe full check baseline",
					        "Actual\twith noCheck set", expected, actual);
				    }
			    }
		    };
		compareResultFileSets(&withoutChecking->DTS, &result->DTS);
		compareResultFileSets(&withoutChecking->JS, &result->JS);
	}

	if (tspath::fileExtensionIsOneOf(
	        baselinePath, {tspath::extensionTs, tspath::extensionTsx})) {
		baselinePath =
		    tspath::changeExtension(baselinePath, tspath::extensionJs);
	}

	std::string actual;
	if (jsCode.size() > 0) {
		actual = tsCode + "\r\n\r\n" + jsCode;
	} else {
		actual = std::string(baseline::NoContent);
	}

	baseline::Run(t, baselinePath, actual, opts);
}

}  // namespace tsc::testutil::tsbaseline
