// Port of tsc/internal/execute/incremental/program.go — the incremental
// Program: wraps a compiler::SimpleProgram plus a snapshot, owns the
// affected-file machinery and emitBuildInfo.
#include <algorithm>
#include <chrono>
#include <utility>

#include "internal/checker/checker.h"
#include "internal/execute/incremental/incremental.h"
#include "internal/outputpaths/outputpaths.h"
#include "internal/packagejson/packagejson.h"
#include "internal/tracing/tracing.h"

namespace tsc::execute::incremental {

// program.go:45 NewProgram.
Program* NewProgram(compiler::SimpleProgram* program,
                    Program* oldProgram, Host* host,
                    std::function<std::chrono::system_clock::time_point()>
                        nestedEmitNow,
                    bool testing) {
	auto* incrementalProgram = new Program();
	incrementalProgram->snapshot_ =
	    programToSnapshot(program, oldProgram, testing);
	incrementalProgram->program_ = program;
	incrementalProgram->host_ = host;
	incrementalProgram->nestedEmitNow = std::move(nestedEmitNow);

	if (testing) {
		incrementalProgram->testingData_ = new TestingData();
		incrementalProgram->testingData_->SemanticDiagnosticsPerFile =
		    &incrementalProgram->snapshot_->semanticDiagnosticsPerFile;
		if (oldProgram != nullptr) {
			incrementalProgram->testingData_
			    ->OldProgramSemanticDiagnosticsPerFile =
			    &oldProgram->snapshot_->semanticDiagnosticsPerFile;
		} else {
			incrementalProgram->testingData_
			    ->OldProgramSemanticDiagnosticsPerFile =
			    new collections::SyncMap<
			        tspath::Path,
			        DiagnosticsOrBuildInfoDiagnosticsWithFileName*>();
		}
		incrementalProgram->testingData_->UpdatedSignatureKinds =
		    std::unordered_map<tspath::Path, SignatureUpdateKind>();
	}
	return incrementalProgram;
}

// program.go:178 testingData.
TestingData* Program::testingData() {
	if (testingData_ == nullptr) {
		testingData_ = new TestingData();
	}
	return testingData_;
}

TestingData* Program::GetTestingData() { return testingData_; }

// program.go:201 beginNestedEmit — Go's ctx; file reads and nested emits
// are timed together. Returns the Done function.
std::function<void()> Program::beginNestedEmit() {
	nestedEmitMu.lock();
	auto now = nestedEmitNow;
	if (!now) {
		nestedEmitMu.unlock();
		return []() {};
	}
	if (nestedEmitDepth == 0) {
		nestedEmitStart = now();
	}
	nestedEmitDepth++;
	nestedEmitMu.unlock();

	return [this, now]() {
		nestedEmitMu.lock();
		nestedEmitDepth--;
		if (nestedEmitDepth == 0) {
			nestedEmitTime += now() - nestedEmitStart;
		}
		nestedEmitMu.unlock();
	};
}

// program.go:240 TakeNestedEmitTime.
std::chrono::nanoseconds Program::TakeNestedEmitTime() {
	nestedEmitMu.lock();
	auto nestedEmitTime = this->nestedEmitTime;
	this->nestedEmitTime = std::chrono::nanoseconds{0};
	nestedEmitMu.unlock();
	return nestedEmitTime;
}

void Program::panicIfNoProgram(const char* method) {
	if (program_ == nullptr) {
		auto msg = std::string(method) +
		           ": should not be called without program";
		TSC_UNREACHABLE(msg.c_str());
	}
}

compiler::SimpleProgram* Program::GetProgram() {
	panicIfNoProgram("GetProgram");
	return program_;
}

bool Program::HasChangedDtsFile() {
	return snapshot_->hasChangedDtsFile;
}

// Options implements compiler.ProgramLike interface.
const CompilerOptions* Program::Options() { return snapshot_->options; }

// CommonSourceDirectory implements compiler.ProgramLike interface.
std::string Program::CommonSourceDirectory() {
	panicIfNoProgram("CommonSourceDirectory");
	return program_->CommonSourceDirectory();
}

// IsSourceFileDefaultLibrary implements compiler.ProgramLike interface.
bool Program::IsSourceFileDefaultLibrary(const tspath::Path& path) const {
	const_cast<Program*>(this)->panicIfNoProgram(
	    "IsSourceFileDefaultLibrary");
	return program_->IsSourceFileDefaultLibrary(path);
}

// GetSourceFiles implements compiler.ProgramLike interface.
std::vector<SourceFile*> Program::GetSourceFiles() {
	panicIfNoProgram("GetSourceFiles");
	return program_->GetSourceFiles();
}

// GetSourceFile implements compiler.ProgramLike interface.
SourceFile* Program::GetSourceFile(const std::string& path) {
	panicIfNoProgram("GetSourceFile");
	return program_->GetSourceFile(path);
}

// GetConfigFileParsingDiagnostics implements compiler.ProgramLike
// interface.
std::vector<Diagnostic*> Program::GetConfigFileParsingDiagnostics() {
	panicIfNoProgram("GetConfigFileParsingDiagnostics");
	return program_->GetConfigFileParsingDiagnostics();
}

// GetSyntacticDiagnostics implements compiler.ProgramLike interface.
std::vector<Diagnostic*> Program::GetSyntacticDiagnostics(
    SourceFile* file) {
	panicIfNoProgram("GetSyntacticDiagnostics");
	return program_->GetSyntacticDiagnostics(file);
}

// GetBindDiagnostics implements compiler.ProgramLike interface.
std::vector<Diagnostic*> Program::GetBindDiagnostics(SourceFile* file) {
	panicIfNoProgram("GetBindDiagnostics");
	return program_->GetBindDiagnostics(file);
}

std::vector<Diagnostic*> Program::GetProgramDiagnostics() {
	panicIfNoProgram("GetProgramDiagnostics");
	return program_->GetProgramDiagnostics();
}

std::vector<Diagnostic*> Program::GetGlobalDiagnostics() {
	panicIfNoProgram("GetGlobalDiagnostics");
	return program_->GetGlobalDiagnostics();
}

// GetSemanticDiagnostics implements compiler.ProgramLike interface.
std::vector<Diagnostic*> Program::GetSemanticDiagnostics(SourceFile* file) {
	panicIfNoProgram("GetSemanticDiagnostics");
	if (snapshot_->options->NoCheck == Tristate::True) {
		return {};
	}

	// Ensure all the diagnsotics are cached
	collectSemanticDiagnosticsOfAffectedFiles(file);

	// Return result from cache
	if (file != nullptr) {
		return getSemanticDiagnosticsOfFile(file);
	}

	std::vector<Diagnostic*> diagnostics;
	for (auto* f : program_->GetSourceFiles()) {
		for (auto* d : getSemanticDiagnosticsOfFile(f)) {
			diagnostics.push_back(d);
		}
	}
	return diagnostics;
}

std::vector<Diagnostic*> Program::getSemanticDiagnosticsOfFile(
    SourceFile* file) {
	auto [cachedDiagnostics, ok] =
	    snapshot_->semanticDiagnosticsPerFile.Load(file->Path());
	if (!ok) {
		TSC_UNREACHABLE(
		    "After handling all the affected files, there shouldnt "
		    "be more changes");
	}
	// slices.Concat
	auto filtered = compiler::filterNoEmitSemanticDiagnostics(
	    cachedDiagnostics->getDiagnostics(program_, file),
	    snapshot_->options);
	auto includeDiags = program_->GetIncludeProcessorDiagnostics(file);
	filtered.insert(filtered.end(), includeDiags.begin(),
	                includeDiags.end());
	return filtered;
}

// GetDeclarationDiagnostics implements compiler.ProgramLike interface.
std::vector<Diagnostic*> Program::GetDeclarationDiagnostics(
    SourceFile* file) {
	panicIfNoProgram("GetDeclarationDiagnostics");
	compiler::EmitOptions emitOptions;
	// core.SingleElementSlice(file): nil file -> empty slice.
	if (file != nullptr) {
		emitOptions.TargetSourceFiles = {file};
	}
	auto* result = emitFiles(this, emitOptions, true);
	if (result != nullptr) {
		return result->Diagnostics;
	}
	return {};
}

// GetSuggestionDiagnostics implements compiler.ProgramLike interface.
std::vector<Diagnostic*> Program::GetSuggestionDiagnostics(
    SourceFile* file) {
	panicIfNoProgram("GetSuggestionDiagnostics");
	return program_->GetSuggestionDiagnostics(
	    file); // TODO: incremental suggestion diagnostics (only relevant
	           // in editor incremental builder?)
}

// program.go:265 Emit.
compiler::EmitResult* Program::Emit(compiler::EmitOptions* options) {
	panicIfNoProgram("Emit");

	compiler::EmitResult* result = nullptr;
	if (!options->ForceEmit &&
	    options->EmitOnly !=
	        compiler::EmitOnly::EmitOnlyBuilderSignature) {
		std::function<compiler::EmitResult*()> emitBuildInfoFn;
		if (Options()->NoEmit == Tristate::True) {
			emitBuildInfoFn = [&]() -> compiler::EmitResult* {
				return emitBuildInfo(options);
			};
		}
		result = compiler::HandleNoEmitOptions(
		    this, options->TargetSourceFiles.empty()
		              ? nullptr
		              : &options->TargetSourceFiles,
		    emitBuildInfoFn);
	}
	if (result != nullptr) {
		if (!options->TargetSourceFiles.empty() ||
		    Options()->NoEmit == Tristate::True) {
			return result;
		}

		// Emit buildInfo and combine result
		auto* buildInfoResult = emitBuildInfo(options);
		if (buildInfoResult != nullptr) {
			for (auto* d : buildInfoResult->Diagnostics) {
				result->Diagnostics.push_back(d);
			}
			for (const auto& f : buildInfoResult->EmittedFiles) {
				result->EmittedFiles.push_back(f);
			}
		}
		return result;
	}
	return emitFiles(this, *options, false);
}

// program.go:294 collectSemanticDiagnosticsOfAffectedFiles — Handle
// affected files and cache the semantic diagnostics for all of them or
// the file asked for.
void Program::collectSemanticDiagnosticsOfAffectedFiles(SourceFile* file) {
	if (snapshot_->canUseIncrementalState()) {
		// Get all affected files
		collectAllAffectedFiles(this);

		if (snapshot_->semanticDiagnosticsPerFile.Size() ==
		    program_->GetSourceFiles().size()) {
			// If we have all the files,
			return;
		}
	}

	std::vector<SourceFile*> affectedFiles;
	if (file != nullptr) {
		auto [_, ok] =
		    snapshot_->semanticDiagnosticsPerFile.Load(file->Path());
		if (ok) {
			return;
		}
		affectedFiles = {file};
	} else {
		for (auto* f : program_->GetSourceFiles()) {
			auto [_, ok] =
			    snapshot_->semanticDiagnosticsPerFile.Load(f->Path());
			if (!ok) {
				affectedFiles.push_back(f);
			}
		}
	}

	// Get their diagnostics and cache them
	auto diagnosticsPerFile =
	    program_->GetSemanticDiagnosticsForIncremental(affectedFiles);

	// Commit changes to snapshot
	for (const auto& kv : diagnosticsPerFile) {
		snapshot_->semanticDiagnosticsPerFile.Store(
		    kv.first->Path(),
		    new DiagnosticsOrBuildInfoDiagnosticsWithFileName{
		        /*diagnostics*/ kv.second, /*buildInfoDiagnostics*/ {}});
	}
	if (snapshot_->semanticDiagnosticsPerFile.Size() ==
	        program_->GetSourceFiles().size() &&
	    snapshot_->checkPending &&
	    snapshot_->options->NoCheck != Tristate::True) {
		snapshot_->checkPending = false;
	}
	snapshot_->buildInfoEmitPending.store(true);
}

// program.go:352 emitBuildInfo.
compiler::EmitResult* Program::emitBuildInfo(compiler::EmitOptions* options) {
	std::function<void()> trPop;
	if (auto* tr = program_->Tracing(); tr != nullptr) {
		trPop = tr->Push(tracing::PhaseEmit, "emitBuildInfo", tracing::TraceArgs{},
		                 true);
	}
	auto trDone = [&]() {
		if (trPop) trPop();
	};
	struct deferPop {
		std::function<void()> f;
		~deferPop() { f(); }
	} trDefer{trDone};

	auto buildInfoFileName = outputpaths::GetBuildInfoFileName(
	    snapshot_->options,
	    tspath::ComparePathsOptions{
	        program_->UseCaseSensitiveFileNames(),
	        program_->GetCurrentDirectory()});
	if (buildInfoFileName.empty() ||
	    program_->IsEmitBlocked(buildInfoFileName)) {
		return nullptr;
	}
	if (snapshot_->hasErrors == Tristate::Unknown) {
		ensureHasErrorsForState(program_);
		if (snapshot_->hasErrors != snapshot_->hasErrorsFromOldState ||
		    snapshot_->hasSemanticErrors !=
		        snapshot_->hasSemanticErrorsFromOldState) {
			snapshot_->buildInfoEmitPending.store(true);
		}
	}
	if (!snapshot_->packageJsons.has_value()) {
		ensurePackageJsonsForState();
		if (*snapshot_->packageJsons !=
		        snapshot_->packageJsonsFromOldState ||
		    *snapshot_->missingPackageJsons !=
		        snapshot_->missingPackageJsonsFromOldState) {
			snapshot_->buildInfoEmitPending.store(true);
		}
	}
	if (!snapshot_->buildInfoEmitPending.load()) {
		return nullptr;
	}
	auto [buildInfo, err] = snapshotToBuildInfo(snapshot_, program_,
	                                            buildInfoFileName);
	if (err != nullptr) {
		// incremental/program.go:353 — EmitResult{EmitSkipped: true,
		// Diagnostics: {compiler.ContentMapperProjectDiagnostic(err)}}.
		auto* r = new compiler::EmitResult();
		r->EmitSkipped = true;
		r->Diagnostics = {compiler::ContentMapperProjectDiagnostic(err)};
		return r;
	}
	std::string text = marshalBuildInfo(buildInfo);
	std::optional<std::string> writeErr;
	if (options->WriteFile) {
		compiler::WriteFileData data;
		data.BuildInfo = buildInfo;
		writeErr = options->WriteFile(buildInfoFileName, text, &data);
	} else {
		writeErr =
		    program_->Host()->WriteFile(buildInfoFileName, text);
	}
	if (writeErr.has_value()) {
		auto* r = new compiler::EmitResult();
		r->EmitSkipped = true;
		r->Diagnostics.push_back(tsoptions::newCompilerDiagnostic(
		    Could_not_write_file_0_Colon_1,
		    {buildInfoFileName, *writeErr}));
		return r;
	}
	snapshot_->buildInfoEmitPending.store(false);
	auto* r = new compiler::EmitResult();
	r->EmitSkipped = false;
	r->EmittedFiles = {buildInfoFileName};
	return r;
}

// program.go:424 ensureHasErrorsForState.
void Program::ensureHasErrorsForState(compiler::SimpleProgram* program) {
	std::function<bool()> hasIncludeProcessingDiagnostics;
	bool hasEmitDiagnostics = false;
	if (snapshot_->canUseIncrementalState()) {
		for (auto* file : program->GetSourceFiles()) {
			auto [_, ok] = snapshot_->emitDiagnosticsPerFile.Load(
			    file->Path());
			if (ok) {
				// emit diagnostics will be encoded in buildInfo;
				hasEmitDiagnostics = true;
				break;
			}
			if (!hasIncludeProcessingDiagnostics &&
			    !program_
			         ->GetIncludeProcessorDiagnostics(file)
			         .empty()) {
				hasIncludeProcessingDiagnostics = []() {
					return true;
				};
			}
		}
		if (!hasIncludeProcessingDiagnostics) {
			hasIncludeProcessingDiagnostics = []() { return false; };
		}
	} else {
		hasEmitDiagnostics = snapshot_->hasEmitDiagnostics;
		hasIncludeProcessingDiagnostics = [&]() {
			for (auto* file : program->GetSourceFiles()) {
				if (!program_
				         ->GetIncludeProcessorDiagnostics(file)
				         .empty()) {
					return true;
				}
			}
			return false;
		};
	}

	if (hasEmitDiagnostics) {
		// Record this for only non incremental build info
		snapshot_->hasErrors = snapshot_->options->IsIncremental()
		                           ? Tristate::False
		                           : Tristate::True;
		// Dont need to encode semantic errors state since the emit
		// diagnostics are encoded
		snapshot_->hasSemanticErrors = false;
		return;
	}

	if (hasIncludeProcessingDiagnostics() ||
	    !program->GetConfigFileParsingDiagnostics().empty() ||
	    !program->GetSyntacticDiagnostics(nullptr).empty() ||
	    !program->GetProgramDiagnostics().empty() ||
	    !program->GetGlobalDiagnostics().empty()) {
		snapshot_->hasErrors = Tristate::True;
		// Dont need to encode semantic errors state since the syntax
		// and program diagnostics are encoded as present
		snapshot_->hasSemanticErrors = false;
		return;
	}

	snapshot_->hasErrors = Tristate::False;
	// Check semantic and emit diagnostics first as we dont need to ask
	// program about it
	for (auto* file : program_->GetSourceFiles()) {
		auto [semanticDiagnostics, ok] =
		    snapshot_->semanticDiagnosticsPerFile.Load(file->Path());
		if (!ok) {
			// Missing semantic diagnostics in cache will be encoded in
			// incremental buildInfo
			if (snapshot_->options->IsIncremental()) {
				snapshot_->hasSemanticErrors = true;
				return;
			}
			continue;
		}
		if (!semanticDiagnostics->diagnostics.empty() ||
		    !semanticDiagnostics->buildInfoDiagnostics.empty()) {
			// cached semantic diagnostics will be encoded in
			// buildInfo — encode as errors in non incremental
			// buildInfo
			snapshot_->hasSemanticErrors =
			    !snapshot_->options->IsIncremental();
			return;
		}
	}
}

// program.go:465 normalizePackageJsons — fwd decl (Go order kept below).
std::optional<std::vector<std::string>> normalizePackageJsons(
    const std::optional<std::vector<std::string>>& packageJsons);

// program.go:435 ensurePackageJsonsForState.
void Program::ensurePackageJsonsForState() {
	auto config = tspath::getDirectoryPath(
	    program_->CommandLine()->ConfigName());
	if (!config.empty()) {
		program_->PackageJsonCacheEntries(
		    [&](tspath::Path key,
		        const std::shared_ptr<packagejson::InfoCacheEntry>&
		            value) {
			    if (value == nullptr) {
				    return true;
			    }
			    auto packageJson = tspath::combinePaths(
			        value->PackageDirectory, {"package.json"});
			    if (value->Exists() || value->DirectoryExists) {
				    packageJson =
				        program_->Host()->Realpath(packageJson);
			    }
			    if (value->Exists()) {
				    if (!snapshot_->packageJsons.has_value()) {
					    snapshot_->packageJsons =
					        std::vector<std::string>{};
				    }
				    snapshot_->packageJsons->push_back(
				        packageJson);
			    } else if (packageJson.find("/node_modules/") !=
			               std::string::npos) {
				    if (!snapshot_->missingPackageJsons
				             .has_value()) {
					    snapshot_->missingPackageJsons =
					        std::vector<std::string>{};
				    }
				    snapshot_->missingPackageJsons
				        ->push_back(packageJson);
			    }
			    return true;
		    });
	}
	snapshot_->packageJsons =
	    normalizePackageJsons(snapshot_->packageJsons);
	snapshot_->missingPackageJsons =
	    normalizePackageJsons(snapshot_->missingPackageJsons);
}

// program.go:465 normalizePackageJsons — Go takes/returns a nil-able
// []string; C++ models as optional<vector>.
std::optional<std::vector<std::string>> normalizePackageJsons(
    const std::optional<std::vector<std::string>>& packageJsons) {
	if (!packageJsons.has_value()) {
		return std::vector<std::string>{};
	}
	auto out = *packageJsons;
	std::sort(out.begin(), out.end());
	// core.Deduplicate — order-preserving
	std::vector<std::string> deduped;
	std::unordered_set<std::string> seen;
	for (const auto& s : out) {
		if (seen.insert(s).second) {
			deduped.push_back(s);
		}
	}
	return deduped;
}

// program.go:474 PackageJsonLookupPaths.
std::vector<std::string> Program::PackageJsonLookupPaths() {
	auto config = tspath::getDirectoryPath(
	    program_->CommandLine()->ConfigName());
	if (config.empty()) {
		return {};
	}

	std::vector<std::string> packageJsons;
	program_->PackageJsonCacheEntries(
	    [&](tspath::Path key,
	        const std::shared_ptr<packagejson::InfoCacheEntry>& value) {
		    if (value == nullptr) {
			    return true;
		    }
		    auto packageJson = tspath::combinePaths(
		        value->PackageDirectory, {"package.json"});
		    if (value->Exists() || value->DirectoryExists) {
			    packageJson =
			        program_->Host()->Realpath(packageJson);
		    }
		    packageJsons.push_back(packageJson);
		    return true;
	    });
	std::sort(packageJsons.begin(), packageJsons.end());
	std::vector<std::string> deduped;
	std::unordered_set<std::string> seen;
	for (const auto& s : packageJsons) {
		if (seen.insert(s).second) {
			deduped.push_back(s);
		}
	}
	return deduped;
}

}  // namespace tsc::execute::incremental
