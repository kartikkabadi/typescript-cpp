// buildtask.cpp — port of tsc/internal/execute/build/buildtask.go.
#include <charconv>
#include <thread>

#include "internal/execute/build/build.h"
#include "internal/compiler/program.h"
#include "internal/tsoptions/tsoptions.h"

namespace tsc::execute::build {

namespace {

// --- file-local helpers ----------------------------------------------------

// core.Memoize — cache-on-first-call closure (Go memoizes per call site; the
// closure escapes into per-iteration use only).
template <typename T>
std::function<T()> memoize(std::function<T()> create) {
	struct state {
		std::optional<T> value;
	};
	auto s = std::make_shared<state>();
	return [s, create = std::move(create)]() mutable -> T {
		if (!s->value.has_value()) {
			s->value = create();
		}
		return *s->value;
	};
}

// strings.CutPrefix — {rest, found}.
inline std::pair<std::string_view, bool> cutPrefix(std::string_view s,
                                                 std::string_view prefix) {
	if (s.substr(0, prefix.size()) == prefix) {
		return {s.substr(prefix.size()), true};
	}
	return {s, false};
}

// strings.Cut — {before, after, found} on the first occurrence of sep.
inline std::tuple<std::string_view, std::string_view, bool>
cut(std::string_view s, std::string_view sep) {
	auto pos = s.find(sep);
	if (pos == std::string_view::npos) {
		return {s, std::string_view{}, false};
	}
	return {s.substr(0, pos), s.substr(pos + sep.size()), true};
}

// strconv.Atoi — decimal int (Go accepts a leading sign); nullopt on error.
inline std::optional<int> atoi(std::string_view s) {
	if (s.empty()) {
		return std::nullopt;
	}
	std::size_t i = 0;
	bool neg = false;
	if (s[0] == '+' || s[0] == '-') {
		neg = s[0] == '-';
		i = 1;
	}
	if (i == s.size()) {
		return std::nullopt;
	}
	int64_t v = 0;
	for (; i < s.size(); i++) {
		if (s[i] < '0' || s[i] > '9') {
			return std::nullopt;
		}
		v = v * 10 + (s[i] - '0');
	}
	return neg ? static_cast<int>(-v) : static_cast<int>(v);
}

// iter.Seq[tspath.Path] as it appears in this codebase.
using pathSeq = std::function<void(
    const std::function<bool(const tspath::Path&)>&)>;

// buildtask.go:625 isContentMapperSupplementalBuildInfoPath.
bool isContentMapperSupplementalBuildInfoPath(const tspath::Path& inputPath,
                                              const pathSeq& roots) {
	bool result = false;
	roots([&](const tspath::Path& root) {
		auto [suffix, ok] =
		    cutPrefix(std::string_view(inputPath),
		              std::string(root) + ".");
		if (!ok) {
			return true;
		}
		auto [index, extension, ok2] = cut(suffix, ".");
		if (!ok2 || extension.empty()) {
			return true;
		}
		if (atoi(index).has_value() &&
		    contentmapper::IsSupportedVirtualExtension(
		        "." + std::string(extension))) {
			result = true;
			return false;
		}
		return true;
	});
	return result;
}

} // namespace

// buildtask.go:82 getContentMapperProject.
std::pair<contentmapper::Project*, gostd::Error>
BuildTask::getContentMapperProject(Orchestrator* orchestrator) {
	std::call_once(contentMapperProjectOnce, [&] {
		if (orchestrator->contentMapperHost == nullptr || resolved == nullptr ||
		    resolved->ContentMappers().empty()) {
			return;
		}
		contentMapperProject = orchestrator->contentMapperHost->Project(
		    contentmapper::ProjectSpec{
		        .ConfigFileName = resolved->ConfigName(),
		        .Mappers = resolved->ContentMappers(),
		        .CompilerOptions = resolved->CompilerOptions(),
		    });
	});
	return {contentMapperProject.get(), contentMapperProjectErr};
}

// buildtask.go:96 refreshContentMapperProject.
void BuildTask::refreshContentMapperProject(Orchestrator* orchestrator) {
	if (contentMapperProject != nullptr) {
		contentMapperProjectErr = contentMapperProject->Refresh();
	}
}

// buildtask.go:102 waitOnUpstream.
void BuildTask::waitOnUpstream() {
	for (auto* upstream : upStream) {
		upstream->task->done->wait();
	}
}

// buildtask.go:108 unblockDownstream.
void BuildTask::unblockDownstream() {
	pending.store(false);
	isInitialCycle = false;
	done->close();
}

// buildtask.go:114 reportDiagnostic.
void BuildTask::reportDiagnostic(Diagnostic* err) {
	errors.push_back(err);
	result->diagnosticReporter(err);
}

// buildtask.go:119 report.
void BuildTask::report(Orchestrator* orchestrator,
                       const tspath::Path& configPath,
                       OrchestratorResult* buildResult) {
	if (!errors.empty()) {
		buildResult->Errors.insert(buildResult->Errors.end(), errors.begin(),
		                           errors.end());
	}
	*orchestrator->opts.Sys->Writer() << result->builder.str();
	if (result->exitStatus > buildResult->Result.Status) {
		buildResult->Result.Status = result->exitStatus;
	}
	if (result->statistics != nullptr) {
		buildResult->Statistics.Aggregate(result->statistics);
	}
	// If we built the program, or updated timestamps, or had errors, we need
	// to delete files that are no longer needed
	switch (result->kind_) {
	case buildKindProgram:
		if (orchestrator->opts.Testing != nullptr) {
			orchestrator->opts.Testing->OnProgram(result->program);
		}
		buildResult->Statistics.ProjectsBuilt++;
		break;
	case buildKindPseudo:
		buildResult->Statistics.TimestampUpdates++;
		break;
	}
	buildResult->FilesToDelete.insert(buildResult->FilesToDelete.end(),
	                                  result->filesToDelete.begin(),
	                                  result->filesToDelete.end());
	result = nullptr;
}

// buildtask.go:145 buildProject.
void BuildTask::buildProject(Orchestrator* orchestrator,
                             const tspath::Path& path) {
	// Wait on upstream tasks to complete
	waitOnUpstream();
	if (pending.load()) {
		status = getUpToDateStatus(orchestrator, path);
		reportUpToDateStatus(orchestrator);
		if (!handleStatusThatDoesntRequireBuild(orchestrator)) {
			compileAndEmit(orchestrator, path);
			updateDownstream(orchestrator, path);
		} else {
			if (resolved != nullptr) {
				for (auto* diagnostic :
				     resolved->GetConfigFileParsingDiagnostics()) {
					reportDiagnostic(diagnostic);
				}
			}
			if (!errors.empty()) {
				result->exitStatus =
				    etsc::ExitStatusDiagnosticsPresent_OutputsSkipped;
			}
		}
	} else {
		if (!errors.empty()) {
			reportUpToDateStatus(orchestrator);
			for (auto* err : errors) {
				// Should not add the diagnostics so just reporting
				result->diagnosticReporter(err);
			}
		}
	}
	unblockDownstream();
}

// buildtask.go:176 updateDownstream.
void BuildTask::updateDownstream(Orchestrator* orchestrator,
                                 const tspath::Path& path) {
	if (isInitialCycle) {
		return;
	}
	if (tristateIsTrue(orchestrator->opts.Command->BuildOptions->StopBuildOnErrors) &&
	    status->isError()) {
		return;
	}
	if (result->program == nullptr) {
		for (auto* downStream : downStream) {
			std::lock_guard<std::mutex> lock(downStream->downStreamUpdateMu);
			downStream->resetStatus();
			downStream->pending.store(true);
		}
		return;
	}

	for (auto* downStream : downStream) {
		std::lock_guard<std::mutex> lock(downStream->downStreamUpdateMu);
		if (downStream->status != nullptr) {
			switch (downStream->status->kind) {
			case upToDateStatusType::UpToDate:
				if (!result->program->HasChangedDtsFile()) {
					downStream->status = new upToDateStatus{
					    upToDateStatusType::UpToDateWithUpstreamTypes,
					    downStream->status->data};
					break;
				}
				[[fallthrough]];
			case upToDateStatusType::UpToDateWithUpstreamTypes:
			case upToDateStatusType::UpToDateWithInputFileText:
				if (result->program->HasChangedDtsFile()) {
					downStream->status = new upToDateStatus{
					    upToDateStatusType::InputFileNewer,
					    inputOutputName{
					        config, downStream->status
					                    ->oldestOutputFileName()}};
				}
				break;
			case upToDateStatusType::UpstreamErrors: {
				auto* upstreamStatus = downStream->status->upstreamErrorsData();
				auto refConfig =
				    ::tsc::ResolveConfigFileNameOfProjectReference(
				        upstreamStatus->ref);
				if (orchestrator->toPath(refConfig) == path) {
					downStream->resetStatus();
				}
				break;
			}
			default:
				break;
			}
		}
		downStream->pending.store(true);
	}
}

// buildtask.go:221 compileAndEmit.
void BuildTask::compileAndEmit(Orchestrator* orchestrator,
                               const tspath::Path& path) {
	errors.clear();
	if (tristateIsTrue(orchestrator->opts.Command->BuildOptions->Verbose)) {
		result->reportStatus(tsoptions::newCompilerDiagnostic(
		    Building_project_0, {orchestrator->relativeFileName(config)}));
	}

	// Real build
	etsc::CompileTimes compileTimes;
	auto [configTime, _] = orchestrator->host_->configTimes.Load(path);
	compileTimes.ConfigTime = configTime;
	auto buildInfoReadStart = orchestrator->opts.Sys->Now();
	incremental::Program* oldProgram = nullptr;
	auto [contentMapperProject_, err] = getContentMapperProject(orchestrator);
	if (err) {
		reportDiagnostic(compiler::ContentMapperProjectDiagnostic(err));
		status = new upToDateStatus{upToDateStatusType::BuildErrors, {}};
		result->exitStatus = etsc::ExitStatusDiagnosticsPresent_OutputsSkipped;
		return;
	}
	auto* compilerHost_ = new compilerHost();
	compilerHost_->host_ = orchestrator->host_;
	compilerHost_->trace = etsc::GetTraceWithWriterFromSys(
	    &result->builder, orchestrator->opts.Command->Locale(),
	    orchestrator->opts.Testing);
	compilerHost_->contentMapperProject =
	    contentMapperProject_ == nullptr
	        ? nullptr
	        : std::shared_ptr<contentmapper::Project>(
	              contentMapperProject_, [](contentmapper::Project*) {});
	compilerHost_->currentDirectory =
	    orchestrator->host_->host_->currentDirectory;
	compilerHost_->fs = orchestrator->host_->host_->fs;
	compilerHost_->defaultLibraryPath =
	    orchestrator->host_->host_->defaultLibraryPath;
	compilerHost_->compilerOptions = resolved->CompilerOptions();
	compilerHost_->extendedConfigCache =
	    orchestrator->host_->extendedConfigCache.get();
	if (!tristateIsTrue(orchestrator->opts.Command->BuildOptions->Force)) {
		oldProgram = incremental::ReadBuildInfoProgram(
		    resolved, orchestrator->host_, compilerHost_);
	}
	compileTimes.BuildInfoReadTime = std::chrono::duration_cast<gostd::Duration>(
	    orchestrator->opts.Sys->Now() - buildInfoReadStart);
	auto parseStart = orchestrator->opts.Sys->Now();
	auto* program = new compiler::SimpleProgram(compilerHost_, resolved, false);
	compileTimes.ParseTime = std::chrono::duration_cast<gostd::Duration>(
	    orchestrator->opts.Sys->Now() - parseStart);
	auto changesComputeStart = orchestrator->opts.Sys->Now();
	auto* sys = orchestrator->opts.Sys;
	result->program = incremental::NewProgram(
	    program, oldProgram, orchestrator->host_,
	    [sys] { return sys->Now(); }, orchestrator->opts.Testing != nullptr);
	compileTimes.ChangesComputeTime = std::chrono::duration_cast<gostd::Duration>(
	    orchestrator->opts.Sys->Now() - changesComputeStart);

	etsc::EmitInput emitInput;
	emitInput.Sys = orchestrator->opts.Sys;
	emitInput.ProgramLike = result->program;
	emitInput.Program = program;
	emitInput.Config = resolved;
	emitInput.ReportDiagnostic = [this](Diagnostic* d) { reportDiagnostic(d); };
	emitInput.ReportErrorSummary = etsc::QuietDiagnosticsReporter;
	emitInput.Writer = &result->builder;
	emitInput.WriteFile =
	    [this, orchestrator](const std::string& fileName,
	                         const std::string& text,
	                         compiler::WriteFileData* data)
	    -> std::optional<std::string> {
		return writeFile(orchestrator, fileName, text, data);
	};
	emitInput.CompileTimes = &compileTimes;
	emitInput.Testing = orchestrator->opts.Testing;
	emitInput.TestingMTimesCache = orchestrator->host_->mTimes;
	auto [emitResult, statistics] = etsc::EmitAndReportStatistics(emitInput);
	result->exitStatus = emitResult.Status;
	result->statistics = statistics;
	packageJsons = result->program->PackageJsonLookupPaths();
	if ((!tristateIsTrue(program->Options()->NoEmitOnError) ||
	     emitResult.Diagnostics.empty()) &&
	    (!emitResult.EmitResult->EmittedFiles.empty() ||
	     status->kind != upToDateStatusType::OutOfDateBuildInfoWithErrors)) {
		// Update time stamps for rest of the outputs
		updateTimeStamps(orchestrator, emitResult.EmitResult->EmittedFiles,
		                 Updating_unchanged_output_timestamps_of_project_0);
	}
	result->kind_ = buildKindProgram;
	if (emitResult.Status ==
	            etsc::ExitStatusDiagnosticsPresent_OutputsSkipped ||
	    emitResult.Status ==
	            etsc::ExitStatusDiagnosticsPresent_OutputsGenerated) {
		status = new upToDateStatus{upToDateStatusType::BuildErrors, {}};
	} else {
		std::string oldestOutputFileName;
		if (!emitResult.EmitResult->EmittedFiles.empty()) {
			oldestOutputFileName = emitResult.EmitResult->EmittedFiles[0];
		} else {
			resolved->GetOutputFileNames(
			    [&](std::string_view outputName) {
				    oldestOutputFileName = std::string(outputName);
				    return false;
			    });
		}
		status = new upToDateStatus{upToDateStatusType::UpToDate,
		                            oldestOutputFileName};
	}
}

// buildtask.go:296 handleStatusThatDoesntRequireBuild.
bool BuildTask::handleStatusThatDoesntRequireBuild(Orchestrator* orchestrator) {
	switch (status->kind) {
	case upToDateStatusType::UpToDate:
		if (tristateIsTrue(orchestrator->opts.Command->BuildOptions->Dry)) {
			result->reportStatus(tsoptions::newCompilerDiagnostic(
			    Project_0_is_up_to_date, {config}));
		}
		return true;
	case upToDateStatusType::UpstreamErrors: {
		auto* upstreamStatus = status->upstreamErrorsData();
		if (tristateIsTrue(orchestrator->opts.Command->BuildOptions->Verbose)) {
			result->reportStatus(tsoptions::newCompilerDiagnostic(
			    upstreamStatus->refHasUpstreamErrors
			        ? Skipping_build_of_project_0_because_its_dependency_1_was_not_built
			        : Skipping_build_of_project_0_because_its_dependency_1_has_errors,
			    {orchestrator->relativeFileName(config),
			     orchestrator->relativeFileName(upstreamStatus->ref)}));
		}
		return true;
	}
	case upToDateStatusType::Solution:
		return true;
	case upToDateStatusType::ConfigFileNotFound:
		reportDiagnostic(
		    tsoptions::newCompilerDiagnostic(File_0_not_found, {config}));
		return true;
	default:
		break;
	}

	// update timestamps
	if (status->isPseudoBuild()) {
		if (tristateIsTrue(orchestrator->opts.Command->BuildOptions->Dry)) {
			result->reportStatus(tsoptions::newCompilerDiagnostic(
			    A_non_dry_build_would_update_timestamps_for_output_of_project_0,
			    {config}));
			status = new upToDateStatus{upToDateStatusType::UpToDate, {}};
			return true;
		}

		updateTimeStamps(orchestrator, {},
		                 Updating_output_timestamps_of_project_0);
		status = new upToDateStatus{upToDateStatusType::UpToDate,
		                            status->data};
		result->kind_ = buildKindPseudo;
		return true;
	}

	if (tristateIsTrue(orchestrator->opts.Command->BuildOptions->Dry)) {
		result->reportStatus(tsoptions::newCompilerDiagnostic(
		    A_non_dry_build_would_build_project_0, {config}));
		status = new upToDateStatus{upToDateStatusType::UpToDate, {}};
		return true;
	}
	return false;
}

// buildtask.go:346 getUpToDateStatus.
upToDateStatus* BuildTask::getUpToDateStatus(Orchestrator* orchestrator,
                                             const tspath::Path& configPath) {
	if (status != nullptr) {
		return status;
	}
	// Config file not found
	if (resolved == nullptr) {
		return new upToDateStatus{upToDateStatusType::ConfigFileNotFound, {}};
	}

	// Solution - nothing to build
	if (resolved->FileNames().empty() &&
	    !resolved->ProjectReferences().empty()) {
		return new upToDateStatus{upToDateStatusType::Solution, {}};
	}

	for (auto* upstream : upStream) {
		if (tristateIsTrue(
	        orchestrator->opts.Command->BuildOptions->StopBuildOnErrors) &&
		    upstream->task->status->isError()) {
			// Upstream project has errors, so we cannot build this project
			return new upToDateStatus{
			    upToDateStatusType::UpstreamErrors,
			    upstreamErrors{
			        resolved->ProjectReferences()[upstream->refIndex]->Path,
			        upstream->task->status->kind ==
			            upToDateStatusType::UpstreamErrors}};
		}
	}

	if (tristateIsTrue(orchestrator->opts.Command->BuildOptions->Force)) {
		return new upToDateStatus{upToDateStatusType::ForceBuild, {}};
	}

	// Check the build info
	auto buildInfoPath = resolved->GetBuildInfoFileName();
	auto getBuildInfoDirectory = memoize<std::string>([&]() -> std::string {
		return tspath::getDirectoryPath(tspath::getNormalizedAbsolutePath(
		    buildInfoPath,
		    orchestrator->comparePathsOptions.currentDirectory));
	});
	auto buildInfoResult =
	    loadOrStoreBuildInfo(orchestrator, configPath, buildInfoPath);
	auto* buildInfo = buildInfoResult.first;
	auto buildInfoTime = buildInfoResult.second;
	if (buildInfo == nullptr) {
		return new upToDateStatus{upToDateStatusType::OutputMissing,
		                          buildInfoPath};
	}

	// build info version
	if (!buildInfo->IsValidVersion()) {
		return new upToDateStatus{upToDateStatusType::TsVersionOutputOfDate,
		                          buildInfo->Version};
	}

	// If a configured content mapper's identity has changed, files it
	// produced may be stale.
	auto [contentMapperProject_, err] = getContentMapperProject(orchestrator);
	auto [contentMapperIdentities, identityErr] =
	    incremental::ContentMapperIdentities(contentMapperProject_);
	if (identityErr != nullptr) {
		contentMapperProjectErr = identityErr;
	}
	if (err != nullptr || identityErr != nullptr ||
	    !buildInfo->ContentMapperIdentitiesMatch(contentMapperIdentities)) {
		return new upToDateStatus{upToDateStatusType::OutOfDateOptions,
		                          buildInfoPath};
	}

	// Report errors if build info indicates errors
	if (buildInfo->Errors || // Errors that need to be reported irrespective of "--noCheck"
	    (!tristateIsTrue(resolved->CompilerOptions()->NoCheck) &&
	     (buildInfo->SemanticErrors ||
	      buildInfo->CheckPending))) { // Errors without --noCheck
		return new upToDateStatus{
		    upToDateStatusType::OutOfDateBuildInfoWithErrors, buildInfoPath};
	}

	if (resolved->CompilerOptions()->IsIncremental()) {
		if (!buildInfo->IsIncremental()) {
			// Program options out of date
			return new upToDateStatus{upToDateStatusType::OutOfDateOptions,
			                          buildInfoPath};
		}

		// Errors need to be reported if build info has errors
		if ((resolved->CompilerOptions()->GetEmitDeclarations() &&
		     !buildInfo->EmitDiagnosticsPerFile
		          .empty()) || // Always reported errors
		    (!tristateIsTrue(
		         resolved->CompilerOptions()
		             ->NoCheck) && // Semantic errors if not --noCheck
		     (!buildInfo->ChangeFileSet.empty() ||
		      !buildInfo->SemanticDiagnosticsPerFile.empty()))) {
			return new upToDateStatus{
			    upToDateStatusType::OutOfDateBuildInfoWithErrors,
			    buildInfoPath};
		}

		// Pending emit files
		if (!tristateIsTrue(resolved->CompilerOptions()->NoEmit) &&
		    (!buildInfo->ChangeFileSet.empty() ||
		     !buildInfo->AffectedFilesPendingEmit.empty())) {
			return new upToDateStatus{
			    upToDateStatusType::OutOfDateBuildInfoWithPendingEmit,
			    buildInfoPath};
		}

		// Some of the emit files like source map or dts etc are not yet done
		if (buildInfo->IsEmitPending(resolved, getBuildInfoDirectory())) {
			return new upToDateStatus{upToDateStatusType::OutOfDateOptions,
			                          buildInfoPath};
		}
	}
	bool inputTextUnchanged = false;
	fileAndTime oldestOutputFileAndTime{buildInfoPath, buildInfoTime};
	fileAndTime newestInputFileAndTime;
	collections::Set<tspath::Path> seenRoots;
	auto getBuildInfoRootInfoReader =
	    memoize<incremental::BuildInfoRootInfoReader*>(
	        [&]() -> incremental::BuildInfoRootInfoReader* {
		        return buildInfo->GetBuildInfoRootInfoReader(
		            getBuildInfoDirectory(),
		            orchestrator->comparePathsOptions);
	        });
	for (auto& inputFile : resolved->FileNames()) {
		auto inputTime = orchestrator->host_->GetMTime(inputFile);
		if (inputTime == fileTime{}) {
			return new upToDateStatus{upToDateStatusType::InputFileMissing,
			                          inputFile};
		}
		auto inputPath = orchestrator->toPath(inputFile);
		if (inputTime > oldestOutputFileAndTime.time) {
			std::string version;
			std::string currentVersion;
			if (buildInfo->IsIncremental()) {
				auto [buildInfoFileInfo, resolvedInputPath] =
				    getBuildInfoRootInfoReader()->GetBuildInfoFileInfo(
				        inputPath);
				auto* fileInfo = buildInfoFileInfo != nullptr
				                     ? buildInfoFileInfo->GetFileInfo()
				                     : nullptr;
				if (fileInfo != nullptr && !fileInfo->Version().empty()) {
					version = std::string(fileInfo->Version());
					if (auto [text, ok] =
					        orchestrator->host_->host_->fs->ReadFile(
					            std::string(resolvedInputPath));
					    ok) {
						currentVersion = incremental::ComputeHash(
						    text,
						    orchestrator->opts.Testing != nullptr);
						if (version == currentVersion) {
							inputTextUnchanged = true;
						}
					}
				}
			}

			if (version.empty() || version != currentVersion) {
				return new upToDateStatus{
				    upToDateStatusType::InputFileNewer,
				    inputOutputName{inputFile, buildInfoPath}};
			}
		}
		if (inputTime > newestInputFileAndTime.time) {
			newestInputFileAndTime = fileAndTime{inputFile, inputTime};
		}
		seenRoots.Add(inputPath);
	}

	// File was root file when project was built but its not any more
	{
		tspath::Path missingRoot;
		bool found = false;
		getBuildInfoRootInfoReader()->Roots([&](const tspath::Path& root) {
			if (!seenRoots.Has(root)) {
				missingRoot = root;
				found = true;
				return false;
			}
			return true;
		});
		if (found) {
			return new upToDateStatus{
			    upToDateStatusType::OutOfDateRoots,
			    inputOutputName{missingRoot, buildInfoPath}};
		}
	}

	if (buildInfo->IsIncremental()) {
		collections::Set<tspath::Path> resolvedRoots;
		getBuildInfoRootInfoReader()->Roots([&](const tspath::Path& root) {
			auto [_, resolved_] =
			    getBuildInfoRootInfoReader()->GetBuildInfoFileInfo(root);
			if (!resolved_.empty()) {
				resolvedRoots.Add(resolved_);
			}
			return true;
		});
		for (std::size_t index = 0; index < buildInfo->FileInfos.size();
		     index++) {
			auto* buildInfoFileInfo = buildInfo->FileInfos[index];
			auto& buildInfoFileName = buildInfo->FileNames[index];
			// Lib files bundled with the compiler can change only with the
			// version of the compiler, which is already verified with
			// buildInfo.Version
			if (incremental::IsBuildInfoFileNameDefaultLibrary(
			        buildInfoFileName)) {
				continue;
			}
			auto inputFile = tspath::getNormalizedAbsolutePath(
			    buildInfoFileName, getBuildInfoDirectory());
			auto inputPath = orchestrator->toPath(inputFile);
			// Root files are already checked
			if (seenRoots.Has(inputPath) || resolvedRoots.Has(inputPath)) {
				continue;
			}
			// A supplemental file that the mapper no longer produces is not
			// an input anymore; the build info will refresh on rebuild.
			bool isSupplemental = isContentMapperSupplementalBuildInfoPath(
			    inputPath,
			    [&](const std::function<bool(const tspath::Path&)>& yield) {
				    getBuildInfoRootInfoReader()->Roots(yield);
			    });
			if (isSupplemental &&
			    !orchestrator->host_->host_->fs->FileExists(inputFile)) {
				continue;
			}
			auto inputTime = orchestrator->host_->GetMTime(inputFile);
			if (inputTime == fileTime{}) {
				// Input file that was part of the program is missing (eg:
				// dependency was removed)
				return new upToDateStatus{
				    upToDateStatusType::InputFileMissing, inputFile};
			}

			if (inputTime > oldestOutputFileAndTime.time) {
				auto* fileInfo = buildInfoFileInfo != nullptr
				                     ? buildInfoFileInfo->GetFileInfo()
				                     : nullptr;
				std::string version =
				    fileInfo != nullptr
				        ? std::string(fileInfo->Version())
				        : std::string{};
				std::string currentVersion;
				if (!version.empty()) {
					if (auto [text, ok] =
					        orchestrator->host_->host_->fs->ReadFile(
					            inputFile);
					    ok) {
						currentVersion = incremental::ComputeHash(
						    text,
						    orchestrator->opts.Testing != nullptr);
					}
				}
				if (version.empty() || version != currentVersion) {
					return new upToDateStatus{
					    upToDateStatusType::InputFileNewer,
					    inputOutputName{inputFile, buildInfoPath}};
				}
				inputTextUnchanged = true;
			}
		}
	}

	if (!resolved->CompilerOptions()->IsIncremental()) {
		upToDateStatus* outputStatus = nullptr;
		// Check output file stamps
		resolved->GetOutputFileNames(
		    [&](std::string_view outputFile) -> bool {
			    auto outputTime =
			        orchestrator->host_->GetMTime(std::string(outputFile));
			    if (outputTime == fileTime{}) {
				    // Output file missing
				    outputStatus = new upToDateStatus{
				        upToDateStatusType::OutputMissing,
				        std::string(outputFile)};
				    return false;
			    }
			    if (outputTime < newestInputFileAndTime.time) {
				    // Output file is older than input file
				    outputStatus = new upToDateStatus{
				        upToDateStatusType::InputFileNewer,
				        inputOutputName{std::string(outputFile),
				                        newestInputFileAndTime.file}};
				    return false;
			    }
			    if (outputTime < oldestOutputFileAndTime.time) {
				    oldestOutputFileAndTime =
				        fileAndTime{std::string(outputFile), outputTime};
			    }
			    return true;
		    });
		if (outputStatus != nullptr) {
			return outputStatus;
		}
	}

	bool refDtsUnchanged = false;
	for (auto* upstream : upStream) {
		// Upstream project has errors
		if (upstream->task->status->isError()) {
			return new upToDateStatus{upToDateStatusType::OutOfDateOptions,
			                          buildInfoPath};
		}

		// inputTime will not be present if we just built this project or
		// updated timestamps
		// - in that case we do want to either build or update timestamps
		auto* refInputOutputFileAndTime =
		    upstream->task->status->inputOutputFileAndTimeData();
		if (refInputOutputFileAndTime != nullptr &&
		    refInputOutputFileAndTime->input.time != fileTime{} &&
		    refInputOutputFileAndTime->input.time <
		        oldestOutputFileAndTime.time) {
			continue;
		}

		// Check if tsbuildinfo path is shared, then we need to rebuild
		if (hasConflictingBuildInfo(orchestrator, upstream->task)) {
			// We have an output older than an upstream output - we are out
			// of date
			return new upToDateStatus{
			    upToDateStatusType::InputFileNewer,
			    inputOutputName{
			        resolved->ProjectReferences()[upstream->refIndex]->Path,
			        oldestOutputFileAndTime.file}};
		}

		// If the upstream project has only change .d.ts files, and we've built
		// *after* those files, then we're "pseudo up to date" and eligible
		// for a fast rebuild
		auto newestDtsChangeTime =
		    upstream->task->getLatestChangedDtsMTime(orchestrator);
		if (newestDtsChangeTime != fileTime{} &&
		    newestDtsChangeTime < oldestOutputFileAndTime.time) {
			refDtsUnchanged = true;
			continue;
		}

		// We have an output older than an upstream output - we are out of
		// date
		return new upToDateStatus{
		    upToDateStatusType::InputFileNewer,
		    inputOutputName{
		        resolved->ProjectReferences()[upstream->refIndex]->Path,
		        oldestOutputFileAndTime.file}};
	}

	auto checkInputFileTime =
	    [&](const std::string& inputFile) -> upToDateStatus* {
		auto inputTime = orchestrator->host_->GetMTime(inputFile);
		if (inputTime > oldestOutputFileAndTime.time) {
			// Output file is older than input file
			return new upToDateStatus{
			    upToDateStatusType::InputFileNewer,
			    inputOutputName{inputFile, oldestOutputFileAndTime.file}};
		}
		return nullptr;
	};

	if (auto* configStatus = checkInputFileTime(config);
	    configStatus != nullptr) {
		return configStatus;
	}

	for (auto& extendedConfig : resolved->ExtendedSourceFiles()) {
		auto* extendedConfigStatus = checkInputFileTime(extendedConfig);
		if (extendedConfigStatus != nullptr) {
			return extendedConfigStatus;
		}
	}

	{
		upToDateStatus* packageStatus = nullptr;
		buildInfo->GetPackageJsons(
		    getBuildInfoDirectory(), [&](const std::string& packageJson) {
			    auto packageJsonTime =
			        orchestrator->host_->GetMTime(packageJson);
			    if (packageJsonTime == fileTime{}) {
				    packageStatus = new upToDateStatus{
				        upToDateStatusType::InputFileMissing, packageJson};
				    return false;
			    }
			    if (packageJsonTime > oldestOutputFileAndTime.time) {
				    packageStatus = new upToDateStatus{
				        upToDateStatusType::InputFileNewer,
				        inputOutputName{packageJson,
				                        oldestOutputFileAndTime.file}};
				    return false;
			    }
			    return true;
		    });
		if (packageStatus != nullptr) {
			return packageStatus;
		}
		buildInfo->GetMissingPackageJsons(
		    getBuildInfoDirectory(), [&](const std::string& packageJson) {
			    if (orchestrator->host_->GetMTime(packageJson) !=
			        fileTime{}) {
				    packageStatus = new upToDateStatus{
				        upToDateStatusType::InputFileNewer,
				        inputOutputName{packageJson,
				                        oldestOutputFileAndTime.file}};
				    return false;
			    }
			    return true;
		    });
		if (packageStatus != nullptr) {
			return packageStatus;
		}
	}
	packageJsons.clear();
	buildInfo->GetPackageJsons(getBuildInfoDirectory(),
	                           [&](const std::string& packageJson) {
		                           packageJsons.push_back(packageJson);
		                           return true;
	                           });
	buildInfo->GetMissingPackageJsons(
	    getBuildInfoDirectory(), [&](const std::string& packageJson) {
		    packageJsons.push_back(packageJson);
		    return true;
	    });

	return new upToDateStatus{
	    refDtsUnchanged
	        ? upToDateStatusType::UpToDateWithUpstreamTypes
	        : (inputTextUnchanged
	               ? upToDateStatusType::UpToDateWithInputFileText
	               : upToDateStatusType::UpToDate),
	    inputOutputFileAndTime{newestInputFileAndTime,
	                           oldestOutputFileAndTime, buildInfoPath}};
}

// buildtask.go:638 reportUpToDateStatus.
void BuildTask::reportUpToDateStatus(Orchestrator* orchestrator) {
	if (!tristateIsTrue(orchestrator->opts.Command->BuildOptions->Verbose)) {
		return;
	}
	switch (status->kind) {
	case upToDateStatusType::ConfigFileNotFound:
		result->reportStatus(tsoptions::newCompilerDiagnostic(
		    Project_0_is_out_of_date_because_config_file_does_not_exist,
		    {orchestrator->relativeFileName(config)}));
		break;
	case upToDateStatusType::UpstreamErrors: {
		auto* upstreamStatus = status->upstreamErrorsData();
		result->reportStatus(tsoptions::newCompilerDiagnostic(
		    upstreamStatus->refHasUpstreamErrors
		        ? Project_0_can_t_be_built_because_its_dependency_1_was_not_built
		        : Project_0_can_t_be_built_because_its_dependency_1_has_errors,
		    {orchestrator->relativeFileName(config),
		     orchestrator->relativeFileName(upstreamStatus->ref)}));
		break;
	}
	case upToDateStatusType::BuildErrors:
		result->reportStatus(tsoptions::newCompilerDiagnostic(
		    Project_0_is_out_of_date_because_it_has_errors,
		    {orchestrator->relativeFileName(config)}));
		break;
	case upToDateStatusType::UpToDate:
		// This is to ensure skipping verbose log for projects that were
		// built, and then some other package changed but this package
		// doesnt need update
		if (auto* inputOutputFileAndTime_ =
		        status->inputOutputFileAndTimeData();
		    inputOutputFileAndTime_ != nullptr) {
			result->reportStatus(tsoptions::newCompilerDiagnostic(
			    Project_0_is_up_to_date_because_newest_input_1_is_older_than_output_2,
			    {orchestrator->relativeFileName(config),
			     orchestrator->relativeFileName(
			         inputOutputFileAndTime_->input.file),
			     orchestrator->relativeFileName(
			         inputOutputFileAndTime_->output.file)}));
		}
		break;
	case upToDateStatusType::UpToDateWithUpstreamTypes:
		result->reportStatus(tsoptions::newCompilerDiagnostic(
		    Project_0_is_up_to_date_with_d_ts_files_from_its_dependencies,
		    {orchestrator->relativeFileName(config)}));
		break;
	case upToDateStatusType::UpToDateWithInputFileText:
		result->reportStatus(tsoptions::newCompilerDiagnostic(
		    Project_0_is_up_to_date_but_needs_to_update_timestamps_of_output_files_that_are_older_than_input_files,
		    {orchestrator->relativeFileName(config)}));
		break;
	case upToDateStatusType::InputFileMissing:
		result->reportStatus(tsoptions::newCompilerDiagnostic(
		    Project_0_is_out_of_date_because_input_1_does_not_exist,
		    {orchestrator->relativeFileName(config),
		     orchestrator->relativeFileName(
		         std::get<std::string>(status->data))}));
		break;
	case upToDateStatusType::OutputMissing:
		result->reportStatus(tsoptions::newCompilerDiagnostic(
		    Project_0_is_out_of_date_because_output_file_1_does_not_exist,
		    {orchestrator->relativeFileName(config),
		     orchestrator->relativeFileName(
		         std::get<std::string>(status->data))}));
		break;
	case upToDateStatusType::InputFileNewer: {
		auto* inputOutput = status->inputOutputNameData();
		result->reportStatus(tsoptions::newCompilerDiagnostic(
		    Project_0_is_out_of_date_because_output_1_is_older_than_input_2,
		    {orchestrator->relativeFileName(config),
		     orchestrator->relativeFileName(inputOutput->output),
		     orchestrator->relativeFileName(inputOutput->input)}));
		break;
	}
	case upToDateStatusType::OutOfDateBuildInfoWithPendingEmit:
		result->reportStatus(tsoptions::newCompilerDiagnostic(
		    Project_0_is_out_of_date_because_buildinfo_file_1_indicates_that_some_of_the_changes_were_not_emitted,
		    {orchestrator->relativeFileName(config),
		     orchestrator->relativeFileName(
		         std::get<std::string>(status->data))}));
		break;
	case upToDateStatusType::OutOfDateBuildInfoWithErrors:
		result->reportStatus(tsoptions::newCompilerDiagnostic(
		    Project_0_is_out_of_date_because_buildinfo_file_1_indicates_that_program_needs_to_report_errors,
		    {orchestrator->relativeFileName(config),
		     orchestrator->relativeFileName(
		         std::get<std::string>(status->data))}));
		break;
	case upToDateStatusType::OutOfDateOptions:
		result->reportStatus(tsoptions::newCompilerDiagnostic(
		    Project_0_is_out_of_date_because_buildinfo_file_1_indicates_there_is_change_in_compilerOptions,
		    {orchestrator->relativeFileName(config),
		     orchestrator->relativeFileName(
		         std::get<std::string>(status->data))}));
		break;
	case upToDateStatusType::OutOfDateRoots: {
		auto* inputOutput = status->inputOutputNameData();
		result->reportStatus(tsoptions::newCompilerDiagnostic(
		    Project_0_is_out_of_date_because_buildinfo_file_1_indicates_that_file_2_was_root_file_of_compilation_but_not_any_more,
		    {orchestrator->relativeFileName(config),
		     orchestrator->relativeFileName(inputOutput->output),
		     orchestrator->relativeFileName(inputOutput->input)}));
		break;
	}
	case upToDateStatusType::TsVersionOutputOfDate:
		result->reportStatus(tsoptions::newCompilerDiagnostic(
		    Project_0_is_out_of_date_because_output_for_it_was_generated_with_version_1_that_differs_with_current_version_2,
		    {orchestrator->relativeFileName(config),
		     orchestrator->relativeFileName(
		         std::get<std::string>(status->data)),
		     std::string(::tsc::version())}));
		break;
	case upToDateStatusType::ForceBuild:
		result->reportStatus(tsoptions::newCompilerDiagnostic(
		    Project_0_is_being_forcibly_rebuilt,
		    {orchestrator->relativeFileName(config)}));
		break;
	case upToDateStatusType::Solution:
		// Does not need to report status
		break;
	default:
		TSC_UNREACHABLE(("Unknown up to date status kind: " +
		                 std::to_string(static_cast<int>(status->kind)))
		                    .c_str());
	}
}

// buildtask.go:716 canUpdateJsDtsOutputTimestamps.
bool BuildTask::canUpdateJsDtsOutputTimestamps() {
	return !tristateIsTrue(resolved->CompilerOptions()->NoEmit) &&
	       !resolved->CompilerOptions()->IsIncremental();
}

// buildtask.go:720 updateTimeStamps.
void BuildTask::updateTimeStamps(
    Orchestrator* orchestrator, const std::vector<std::string>& emittedFiles,
    const DiagnosticMessage* verboseMessage) {
	collections::Set<std::string> emitted;
	for (auto& f : emittedFiles) {
		emitted.Add(f);
	}
	bool verboseMessageReported = false;
	auto buildInfoName = resolved->GetBuildInfoFileName();
	auto now = std::chrono::file_clock::from_sys(orchestrator->opts.Sys->Now());
	auto updateTimeStamp = [&](const std::string& file) {
		if (emitted.Has(file)) {
			return;
		}
		if (!verboseMessageReported &&
		    tristateIsTrue(orchestrator->opts.Command->BuildOptions->Verbose)) {
			result->reportStatus(tsoptions::newCompilerDiagnostic(
			    verboseMessage, {orchestrator->relativeFileName(config)}));
			verboseMessageReported = true;
		}
		auto err = orchestrator->host_->SetMTime(file, now);
		if (!err.has_value()) {
			if (file == buildInfoName) {
				std::lock_guard<std::mutex> lock(buildInfoEntryMu);
				if (buildInfoEntry_ != nullptr) {
					buildInfoEntry_->mTime = now;
				}
			} else if (storeOutputTimeStamp(orchestrator)) {
				orchestrator->host_->storeMTime(file, now);
			}
		}
	};

	if (canUpdateJsDtsOutputTimestamps()) {
		resolved->GetOutputFileNames([&](std::string_view outputFile) {
			updateTimeStamp(std::string(outputFile));
			return true;
		});
	}
	updateTimeStamp(resolved->GetBuildInfoFileName());
}

// buildtask.go:753 cleanProject.
void BuildTask::cleanProject(Orchestrator* orchestrator,
                             const tspath::Path& path) {
	if (resolved == nullptr) {
		reportDiagnostic(tsoptions::newCompilerDiagnostic(File_0_not_found,
		                                                  {config}));
		result->exitStatus = etsc::ExitStatusDiagnosticsPresent_OutputsSkipped;
		return;
	}

	collections::Set<tspath::Path> inputs;
	for (auto& fileName : resolved->FileNames()) {
		inputs.Add(orchestrator->toPath(fileName));
	}
	resolved->GetOutputFileNames([&](std::string_view outputFile) {
		cleanProjectOutput(orchestrator, std::string(outputFile), &inputs);
		return true;
	});
	cleanProjectOutput(orchestrator, resolved->GetBuildInfoFileName(),
	                   &inputs);
}

// buildtask.go:765 cleanProjectOutput.
void BuildTask::cleanProjectOutput(
    Orchestrator* orchestrator, const std::string& outputFile,
    collections::Set<tspath::Path>* inputs) {
	auto outputPath = orchestrator->toPath(outputFile);
	// If output name is same as input file name, do not delete and ignore
	// the error
	if (inputs->Has(outputPath)) {
		return;
	}
	if (orchestrator->host_->host_->fs->FileExists(outputFile)) {
		if (!tristateIsTrue(orchestrator->opts.Command->BuildOptions->Dry)) {
			auto err = orchestrator->host_->host_->fs->Remove(outputFile);
			if (err) {
				reportDiagnostic(tsoptions::newCompilerDiagnostic(
				    Failed_to_delete_file_0, {outputFile}));
			}
		} else {
			result->filesToDelete.push_back(outputFile);
		}
	}
}

// buildtask.go:782 updateWatch.
void BuildTask::updateWatch(
    Orchestrator* orchestrator,
    collections::SyncMap<tspath::Path, fileTime>* oldCache) {
	if (resolved != nullptr) {
		if (canUpdateJsDtsOutputTimestamps()) {
			resolved->GetOutputFileNames([&](std::string_view outputFile) {
				orchestrator->host_->storeMTimeFromOldCache(
				    std::string(outputFile), oldCache);
				return true;
			});
		}
	}
}

// buildtask.go:792 resetStatus.
void BuildTask::resetStatus() {
	status = nullptr;
	pending.store(true);
	errors.clear();
}

// buildtask.go:798 resetConfig.
void BuildTask::resetConfig(Orchestrator* orchestrator,
                            const tspath::Path& path) {
	dirty = true;
	orchestrator->host_->resolvedReferences.del(path);
}

// buildtask.go:803 loadOrStoreBuildInfo.
std::pair<incremental::BuildInfo*, fileTime>
BuildTask::loadOrStoreBuildInfo(Orchestrator* orchestrator,
                                const tspath::Path& configPath,
                                const std::string& buildInfoFileName) {
	auto path = orchestrator->toPath(buildInfoFileName);
	std::lock_guard<std::mutex> lock(buildInfoEntryMu);
	if (buildInfoEntry_ != nullptr && buildInfoEntry_->path == path) {
		return {buildInfoEntry_->buildInfo, buildInfoEntry_->mTime};
	}
	buildInfoEntry_ = new buildInfoEntry{
	    incremental::NewBuildInfoReader(orchestrator->host_->FS())
	        ->ReadBuildInfo(resolved),
	    path,
	};
	fileTime mTime{};
	if (buildInfoEntry_->buildInfo != nullptr) {
		mTime = orchestrator->host_->GetMTime(buildInfoFileName);
	}
	buildInfoEntry_->mTime = mTime;
	return {buildInfoEntry_->buildInfo, mTime};
}

// buildtask.go:822 onBuildInfoEmit.
void BuildTask::onBuildInfoEmit(Orchestrator* orchestrator,
                                const std::string& buildInfoFileName,
                                incremental::BuildInfo* buildInfo,
                                bool hasChangedDtsFile) {
	std::lock_guard<std::mutex> lock(buildInfoEntryMu);
	std::optional<fileTime> dtsTime;
	auto mTime =
	    std::chrono::file_clock::from_sys(orchestrator->opts.Sys->Now());
	if (hasChangedDtsFile) {
		dtsTime = mTime;
	} else if (buildInfoEntry_ != nullptr) {
		dtsTime = buildInfoEntry_->dtsTime;
	}
	buildInfoEntry_ = new buildInfoEntry{buildInfo,
	                                     orchestrator->toPath(buildInfoFileName),
	                                     mTime, dtsTime};
}

// buildtask.go:838 hasConflictingBuildInfo.
bool BuildTask::hasConflictingBuildInfo(Orchestrator* orchestrator,
                                        BuildTask* upstream) {
	if (buildInfoEntry_ != nullptr &&
	    upstream->buildInfoEntry_ != nullptr) {
		return buildInfoEntry_->path == upstream->buildInfoEntry_->path;
	}
	return false;
}

// buildtask.go:846 getLatestChangedDtsMTime.
fileTime BuildTask::getLatestChangedDtsMTime(Orchestrator* orchestrator) {
	std::lock_guard<std::mutex> lock(buildInfoEntryMu);
	if (buildInfoEntry_->dtsTime.has_value()) {
		return *buildInfoEntry_->dtsTime;
	}
	auto dtsTime = orchestrator->host_->GetMTime(
	    tspath::getNormalizedAbsolutePath(
	        buildInfoEntry_->buildInfo->LatestChangedDtsFile,
	        tspath::getDirectoryPath(std::string(buildInfoEntry_->path))));
	buildInfoEntry_->dtsTime = dtsTime;
	return dtsTime;
}

// buildtask.go:861 storeOutputTimeStamp.
bool BuildTask::storeOutputTimeStamp(Orchestrator* orchestrator) {
	return tristateIsTrue(orchestrator->opts.Command->CompilerOptions->Watch) &&
	       !resolved->CompilerOptions()->IsIncremental();
}

// buildtask.go:866 writeFile.
std::optional<std::string>
BuildTask::writeFile(Orchestrator* orchestrator, const std::string& fileName,
                     const std::string& text,
                     compiler::WriteFileData* data) {
	if (auto err = orchestrator->host_->host_->fs->WriteFile(fileName, text)) {
		return err.str();
	}
	if (data != nullptr && data->BuildInfo != nullptr) {
		onBuildInfoEmit(orchestrator, fileName,
		                static_cast<incremental::BuildInfo*>(data->BuildInfo),
		                result->program->HasChangedDtsFile());
	} else if (storeOutputTimeStamp(orchestrator)) {
		// Store time stamps
		orchestrator->host_->storeMTime(
		    fileName,
		    std::chrono::file_clock::from_sys(orchestrator->opts.Sys->Now()));
	}
	return std::nullopt;
}

} // namespace tsc::execute::build
