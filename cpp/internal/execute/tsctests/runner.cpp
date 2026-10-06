// runner.cpp — port of tsctests/runner.go: tscInput::run — the e2e driver
// that runs a scenario, applies edits, compares incremental-vs-clean
// output, and baselines the transcript.
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "internal/core/utilities.h"
#include "internal/execute/execute.h"
#include "internal/execute/tsctests/tsctests.h"
#include "internal/testutil/baseline/baseline.h"

namespace tsc::execute::tsctests {

namespace {
// strings.Join(commandLineArgs, " ").
std::string joinArgs(const std::vector<std::string>& args) {
	std::string out;
	for (size_t i = 0; i < args.size(); i++) {
		if (i) out += ' ';
		out += args[i];
	}
	return out;
}
// strings.ReplaceAll(test.subScenario, " ", "-").
std::string replaceAllSpaces(std::string_view s) {
	std::string out(s);
	std::replace(out.begin(), out.end(), ' ', '-');
	return out;
}
}  // namespace

// executeCommand — runner.go:45.
etsc::CommandLineResult tscInput::executeCommand(
    gostd::Context ctx, TestSys* sys, stringWriter* baselineBuilder,
    const std::vector<std::string>& commandLineArgs_) {
	baselineBuilder->WriteString("tsgo " + joinArgs(commandLineArgs_) +
	                             "\n");
	auto result = execute::CommandLine(ctx, sys, commandLineArgs_, sys);
	switch (result.Status) {
	case etsc::ExitStatusSuccess:
		baselineBuilder->WriteString("ExitStatus:: Success");
		break;
	case etsc::ExitStatusDiagnosticsPresent_OutputsSkipped:
		baselineBuilder->WriteString(
		    "ExitStatus:: DiagnosticsPresent_OutputsSkipped");
		break;
	case etsc::ExitStatusDiagnosticsPresent_OutputsGenerated:
		baselineBuilder->WriteString(
		    "ExitStatus:: DiagnosticsPresent_OutputsGenerated");
		break;
	case etsc::ExitStatusInvalidProject_OutputsSkipped:
		baselineBuilder->WriteString(
		    "ExitStatus:: InvalidProject_OutputsSkipped");
		break;
	case etsc::ExitStatusProjectReferenceCycle_OutputsSkipped:
		baselineBuilder->WriteString(
		    "ExitStatus:: ProjectReferenceCycle_OutputsSkipped");
		break;
	case etsc::ExitStatusNotImplemented:
		baselineBuilder->WriteString("ExitStatus:: NotImplemented");
		break;
	default:
		TSC_UNREACHABLE(
		    gostd::sprintf("UnknownExitStatus %d",
		                   {static_cast<int>(result.Status)})
		        .c_str());
	}
	return result;
}

// run — runner.go:67.
void tscInput::run(gostd::testing::T* t, const std::string& scenario) {
	t->Helper();
	t->Run(getBaselineSubFolder() + "/" + subScenario,
	       [this, scenario](gostd::testing::T* t) {
		       t->Parallel();
		       // ctx is cancelled when the subtest ends, tearing down any
		       // content mapper host created during the run.
		       auto ctx = t->Context();
		       // initial test tsc compile
		       stringWriter baselineBuilder;
		       auto sys = newTestSys(this, false);
		       baselineBuilder.WriteString(
		           "currentDirectory::" + sys->GetCurrentDirectory() +
		           "\nuseCaseSensitiveFileNames::" +
		           (sys->fs()->UseCaseSensitiveFileNames() ? "true"
		                                                   : "false") +
		           "\nInput::\n");
		       sys->baselineFSwithDiff(&baselineBuilder);
		       auto result = executeCommand(ctx, sys.get(),
		                                    &baselineBuilder,
		                                    commandLineArgs);
		       sys->serializeState(&baselineBuilder);
		       if (result.Watcher != nullptr &&
		           sys->mockWatchBackend->HasWatches()) {
			       baselineBuilder.WriteString(
			           sys->mockWatchBackend->WatchState());
		       }
		       stringWriter unexpectedDiff;
		       unexpectedDiff.WriteString(sys->baselinePrograms(
		           &baselineBuilder, "Initial build"));

		       for (int index = 0; index < static_cast<int>(edits.size());
		            index++) {
			       tscEdit* do_ = edits[index];
			       sys->clearOutput();
			       std::unique_ptr<::tsc::workGroup> wg(
			           ::tsc::newWorkGroup(false));
			       std::unique_ptr<TestSys> nonIncrementalSys;
			       const std::vector<std::string> commandLineArgs =
			           do_->commandLineArgs ? *do_->commandLineArgs
			                                : this->commandLineArgs;
			       wg->Queue([&, index, do_, commandLineArgs] {
				       baselineBuilder.WriteString(gostd::sprintf(
				           "\n\nEdit [%d]:: %s\n", {index, do_->caption}));
				       if (do_->edit) {
					       do_->edit(sys.get());
				       }
				       std::vector<testutil::fsbaselineutil::FileChange>
				           changedPaths = sys->fsDiffer->ChangedPaths();
				       sys->baselineFSwithDiff(&baselineBuilder);

				       if (result.Watcher == nullptr) {
					       executeCommand(ctx, sys.get(),
					                      &baselineBuilder,
					                      commandLineArgs);
				       } else {
					       sys->mockWatchBackend
					           ->SendChangedPaths(changedPaths);
					       result.Watcher->DoCycle();
				       }
				       sys->serializeState(&baselineBuilder);
				       if (result.Watcher != nullptr &&
				           sys->mockWatchBackend->HasWatches()) {
					       baselineBuilder.WriteString(
					           sys->mockWatchBackend->WatchState());
				       }
				       unexpectedDiff.WriteString(
				           sys->baselinePrograms(
				               &baselineBuilder,
				               gostd::sprintf("Edit [%d]:: %s\n",
				                              {index, do_->caption})));
			       });
			       wg->Queue([&, commandLineArgs] {
				       // Compute build with all the edits
				       nonIncrementalSys = newTestSys(this, true);
				       for (int i = 0; i < index + 1; i++) {
					       if (edits[i]->edit) {
						       edits[i]->edit(
						           nonIncrementalSys.get());
					       }
				       }
				       execute::CommandLine(ctx,
				                            nonIncrementalSys.get(),
				                            commandLineArgs,
				                            nonIncrementalSys.get());
			       });
			       wg->RunAndWait();

			       std::string diff = getDiffForIncremental(
			           sys.get(), nonIncrementalSys.get());
			       if (!diff.empty()) {
				       baselineBuilder.WriteString(gostd::sprintf(
				           "\n\nDiff:: %s\n",
				           {do_->expectedDiff.empty()
				                ? std::string(
				                      "!!! Unexpected diff, please "
				                      "review and either fix or write "
				                      "explanation as expectedDiff !!!")
				                : do_->expectedDiff}));
				       baselineBuilder.WriteString(diff);
				       if (do_->expectedDiff.empty()) {
					       unexpectedDiff.WriteString(
					           gostd::sprintf(
					               "Edit [%d]:: %s\n!!! Unexpected "
					               "diff, please review and either fix "
					               "or write explanation as "
					               "expectedDiff !!!\n%s\n",
					               {index, do_->caption, diff}));
				       }
			       } else if (!do_->expectedDiff.empty()) {
				       baselineBuilder.WriteString(gostd::sprintf(
				           "\n\nDiff:: %s !!! Diff not found but "
				           "explanation present, please review and "
				           "remove the explanation !!!\n",
				           {do_->expectedDiff}));
				       unexpectedDiff.WriteString(gostd::sprintf(
				           "Edit [%d]:: %s\n!!! Diff not found but "
				           "explanation present, please review and "
				           "remove the explanation !!!\n",
				           {index, do_->caption}));
			       }
		       }
		       testutil::baseline::Options opts;
		       opts.Subfolder = tspath::combinePaths(
		           getBaselineSubFolder(), {scenario});
		       testutil::baseline::Run(t, replaceAllSpaces(subScenario) + ".js",
		                     baselineBuilder.String(), opts);
		       if (!unexpectedDiff.String().empty()) {
			       t->Errorf(
			           "Test %s has unexpected diff %s with "
			           "incremental build, please review the "
			           "baseline file",
			           {subScenario, unexpectedDiff.String()});
		       }
	       });
}

// getDiffForIncremental — runner.go:149.
std::string getDiffForIncremental(TestSys* incrementalSys,
                                  TestSys* nonIncrementalSys) {
	stringWriter diffBuilder;

	std::vector<std::string> nonIncrementalOutputs =
	    nonIncrementalSys->fs_->writtenFiles.ToSlice();
	std::sort(nonIncrementalOutputs.begin(), nonIncrementalOutputs.end());
	for (const auto& nonIncrementalOutput : nonIncrementalOutputs) {
		if (tspath::fileExtensionIs(nonIncrementalOutput,
		                            tspath::extensionTsBuildInfo) ||
		    nonIncrementalOutput.ends_with(".readable.baseline.txt")) {
			// Just check existence
			if (!incrementalSys->fsFromFileMap()->FileExists(
			        nonIncrementalOutput)) {
				diffBuilder.WriteString(testutil::baseline::DiffText(
				    "nonIncremental " + nonIncrementalOutput,
				    "incremental " + nonIncrementalOutput,
				    "Exists", ""));
				diffBuilder.WriteString("\n");
			}
		} else {
			auto [nonIncrementalText, ok1] =
			    nonIncrementalSys->fsFromFileMap()->ReadFile(
			        nonIncrementalOutput);
			if (!ok1) {
				TSC_UNREACHABLE(
				    ("Written file not found " +
				     nonIncrementalOutput)
				        .c_str());
			}
			auto [incrementalText, ok2] =
			    incrementalSys->fsFromFileMap()->ReadFile(
			        nonIncrementalOutput);
			if (!ok2 || incrementalText != nonIncrementalText) {
				diffBuilder.WriteString(testutil::baseline::DiffText(
				    "nonIncremental " + nonIncrementalOutput,
				    "incremental " + nonIncrementalOutput,
				    nonIncrementalText, incrementalText));
				diffBuilder.WriteString("\n");
			}
		}
	}

	std::string incrementalOutput = incrementalSys->getOutput(true);
	std::string nonIncrementalOutput = nonIncrementalSys->getOutput(true);
	if (incrementalOutput != nonIncrementalOutput) {
		diffBuilder.WriteString(testutil::baseline::DiffText(
		    "nonIncremental.output.txt", "incremental.output.txt",
		    nonIncrementalOutput, incrementalOutput));
	}
	return diffBuilder.String();
}

// getBaselineSubFolder — runner.go:183.
std::string tscInput::getBaselineSubFolder() {
	std::string commandName = "tsc";
	if (std::any_of(commandLineArgs.begin(), commandLineArgs.end(),
	                [](const std::string& arg) {
		                return arg == "-b" || arg == "--b" ||
		                       arg == "-build" || arg == "--build";
	                })) {
		commandName = "tsbuild";
	}
	std::string w;
	if (std::any_of(commandLineArgs.begin(), commandLineArgs.end(),
	                [](const std::string& arg) {
		                return arg == "-w" || arg == "--w" ||
		                       arg == "-watch" || arg == "--watch";
	                })) {
		w = "Watch";
	}
	return commandName + w;
}

}  // namespace tsc::execute::tsctests
