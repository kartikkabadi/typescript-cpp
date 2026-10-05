// Port of tsc/internal/execute/incremental/emitfileshandler.go — emitFiles
// and the emitFilesHandler that incrementally emits affected files and
// maintains emit signatures / pending emits in the snapshot.
#include <atomic>
#include <utility>

#include "internal/checker/checker.h"
#include "internal/execute/incremental/incremental.h"

namespace tsc::execute::incremental {
namespace {

struct emitUpdate {
	FileEmitKind pendingKind = FileEmitKindNone;
	compiler::EmitResult* result = nullptr;
	bool dtsErrorsFromCache = false;
};

// emitfileshandler.go:26 emitFilesHandler. Go runs the emit loop through a
// WorkGroup; this port runs those queues inline (single-threaded) — the
// mutex-guarded collections remain since they model the Go state layout.
struct emitFilesHandler {
	Program* program;
	bool isForDtsErrors;
	collections::SyncMap<tspath::Path, std::string> signatures;
	collections::SyncMap<tspath::Path, emitSignature*> emitSignatures;
	collections::SyncMap<tspath::Path, std::string> latestChangedDtsFiles;
	collections::Set<tspath::Path> deletedPendingKinds;
	collections::SyncMap<tspath::Path, emitUpdate*> emitUpdates;
	std::atomic<bool> hasEmitDiagnostics{false};

	// emitfileshandler.go:44 getPendingEmitKindForEmitOptions.
	FileEmitKind getPendingEmitKindForEmitOptions(
	    FileEmitKind emitKind,
	    const compiler::EmitOptions& options) {
		auto pendingKind = getPendingEmitKind(emitKind, 0);
		if (options.EmitOnly == compiler::EmitOnly::EmitOnlyDts) {
			pendingKind &= FileEmitKindAllDts;
		}
		if (isForDtsErrors) {
			pendingKind &= FileEmitKindDtsErrors;
		}
		return pendingKind;
	}

	// emitfileshandler.go:57 emitAllAffectedFiles.
	compiler::EmitResult* emitAllAffectedFiles(
	    const compiler::EmitOptions& options) {
		// Emit all affected files
		if (program->snapshot_->canUseIncrementalState()) {
			auto results = emitFilesIncremental(options);
			if (isForDtsErrors) {
				if (!options.TargetSourceFiles.empty()) {
					// Result from cache
					auto* result = new compiler::EmitResult();
					result->EmitSkipped = true;
					for (auto* targetFile : options.TargetSourceFiles) {
						auto [diagnostics, _] =
						    program->snapshot_
						        ->emitDiagnosticsPerFile.Load(
						            targetFile->Path());
						auto diags = diagnostics->getDiagnostics(
						    program->program_, targetFile);
						result->Diagnostics.insert(
						    result->Diagnostics.end(), diags.begin(),
						    diags.end());
					}
					updateHasEmitDiagnostics(result);
					return result;
				}
				for (auto* result : results) {
					updateHasEmitDiagnostics(result);
				}
				return compiler::CombineEmitResults(results);
			} else {
				// Combine results and update buildInfo
				auto* result = compiler::CombineEmitResults(results);
				updateHasEmitDiagnostics(result);
				emitBuildInfo(options, result);
				return result;
			}
		} else if (!isForDtsErrors) {
			auto emitOptions = getEmitOptions(options);
			auto* result = program->program_->Emit(&emitOptions);
			updateHasEmitDiagnostics(result);
			updateSnapshot();
			emitBuildInfo(options, result);
			return result;
		} else {
			std::vector<Diagnostic*> diagnostics;
			if (options.TargetSourceFiles.empty()) {
				diagnostics =
				    program->program_->GetDeclarationDiagnostics(
				        nullptr);
			} else {
				for (auto* targetSourceFile :
				     options.TargetSourceFiles) {
					auto diags =
					    program->program_->GetDeclarationDiagnostics(
					        targetSourceFile);
					diagnostics.insert(diagnostics.end(),
					                   diags.begin(), diags.end());
				}
			}
			auto* result = new compiler::EmitResult();
			result->EmitSkipped = true;
			result->Diagnostics = std::move(diagnostics);
			if (!result->Diagnostics.empty()) {
				updateHasEmitDiagnostics(result);
				program->snapshot_->hasEmitDiagnostics = true;
			}
			return result;
		}
	}

	// emitfileshandler.go:105 updateHasEmitDiagnostics.
	void updateHasEmitDiagnostics(compiler::EmitResult* result) {
		if (result != nullptr && !result->Diagnostics.empty()) {
			hasEmitDiagnostics.store(true);
		}
	}

	// emitfileshandler.go:111 emitBuildInfo (handler-local).
	void emitBuildInfo(const compiler::EmitOptions& options,
	                   compiler::EmitResult* result) {
		auto emitBuildInfoOptions = options;
		auto* buildInfoResult =
		    program->emitBuildInfo(&emitBuildInfoOptions);
		if (buildInfoResult != nullptr) {
			result->Diagnostics.insert(
			    result->Diagnostics.end(),
			    buildInfoResult->Diagnostics.begin(),
			    buildInfoResult->Diagnostics.end());
			result->EmittedFiles.insert(
			    result->EmittedFiles.end(),
			    buildInfoResult->EmittedFiles.begin(),
			    buildInfoResult->EmittedFiles.end());
		}
	}

	// emitfileshandler.go:120 emitFilesIncremental.
	std::vector<compiler::EmitResult*> emitFilesIncremental(
	    const compiler::EmitOptions& options) {
		// Get all affected files
		collectAllAffectedFiles(program);

		std::vector<compiler::EmitResult*> pendingEmits;
		program->snapshot_->affectedFilesPendingEmit.Range(
		    [&](const tspath::Path& path, FileEmitKind emitKind) {
			    auto* affectedFile =
			        program->program_->GetSourceFileByPath(path);
			    if (affectedFile == nullptr ||
			        !program->program_->SourceFileMayBeEmitted(
			            affectedFile, false)) {
				    deletedPendingKinds.Add(path);
				    return true;
			    }
			    auto pendingKind =
			        getPendingEmitKindForEmitOptions(emitKind, options);
			    if (pendingKind != 0) {
				    // Determine if we can do partial emit
				    auto emitOnly = compiler::EmitOnly::EmitAll;
				    bool emitOnlyJs = false;
				    if ((pendingKind & FileEmitKindAllJs) != 0) {
					    emitOnly = compiler::EmitOnly::EmitOnlyJs;
					    emitOnlyJs = true;
				    }
				    if ((pendingKind & FileEmitKindAllDts) != 0) {
					    if (emitOnlyJs) {
						    emitOnly = compiler::EmitOnly::EmitAll;
					    } else {
						    emitOnly = compiler::EmitOnly::EmitOnlyDts;
					    }
				    }
				    compiler::EmitResult* result = nullptr;
				    if (!isForDtsErrors) {
					    compiler::EmitOptions emitOptions;
					    emitOptions.TargetSourceFiles = {affectedFile};
					    emitOptions.EmitOnly = emitOnly;
					    emitOptions.WriteFile = options.WriteFile;
					    auto opts = getEmitOptions(emitOptions);
					    result =
					        program->program_->Emit(&opts);
				    } else {
					    result = new compiler::EmitResult();
					    result->EmitSkipped = true;
					    result->Diagnostics =
					        program->program_
					            ->GetDeclarationDiagnostics(
					                affectedFile);
				    }
				    updateHasEmitDiagnostics(result);

				    // Update the pendingEmit for the file
				    auto* update = new emitUpdate();
				    update->pendingKind =
				        getPendingEmitKind(emitKind, pendingKind);
				    update->result = result;
				    emitUpdates.Store(path, update);
			    }
			    return true;
		    });

		// Get updated errors that were not included in affected files
		// emit
		program->snapshot_->emitDiagnosticsPerFile.Range(
		    [&](const tspath::Path& path,
		        DiagnosticsOrBuildInfoDiagnosticsWithFileName*
		            diagnostics) {
			    auto [existing, ok] = emitUpdates.Load(path);
			    if (!ok) {
				    auto* affectedFile =
				        program->program_->GetSourceFileByPath(path);
				    if (affectedFile == nullptr ||
				        !program->program_->SourceFileMayBeEmitted(
				            affectedFile, false)) {
					    deletedPendingKinds.Add(path);
					    return true;
				    }
				    auto [pendingKind, _] =
				        program->snapshot_
				            ->affectedFilesPendingEmit.Load(path);
				    auto* update = new emitUpdate();
				    update->pendingKind = pendingKind;
				    auto* result = new compiler::EmitResult();
				    result->EmitSkipped = true;
				    result->Diagnostics =
				        diagnostics->getDiagnostics(
				            program->program_, affectedFile);
				    update->result = result;
				    update->dtsErrorsFromCache = true;
				    emitUpdates.Store(path, update);
			    }
			    return true;
		    });

		return updateSnapshot();
	}

	// emitfileshandler.go:197 getEmitOptions — wraps WriteFile to track
	// d.ts signatures.
	compiler::EmitOptions getEmitOptions(
	    const compiler::EmitOptions& options) {
		if (!program->snapshot_->options->GetEmitDeclarations()) {
			return options;
		}
		bool canUseIncrementalState =
		    program->snapshot_->canUseIncrementalState();
		compiler::EmitOptions result;
		result.TargetSourceFiles = options.TargetSourceFiles;
		result.EmitOnly = options.EmitOnly;
		result.ForceEmit = options.ForceEmit;
		result.WriteFile =
		    [this, canUseIncrementalState, &options](
		        const std::string& fileName, const std::string& text,
		        compiler::WriteFileData* data)
		    -> std::optional<std::string> {
			bool differsOnlyInMap = false;
			if (tspath::isDeclarationFileName(fileName)) {
				if (canUseIncrementalState) {
					std::string emitSignatureText;
					auto [info, _] =
					    program->snapshot_->fileInfos.Load(
					        data->SourceFile->Path());
					if (info->signature == info->version) {
						auto signature =
						    program->snapshot_
						        ->computeSignatureWithDiagnostics(
						            data->SourceFile, text, data);
						// With d.ts diagnostics they are also part of
						// the signature so emitSignature will be
						// different from it since its just hash of
						// d.ts
						if (data->Diagnostics.empty()) {
							emitSignatureText = signature;
						}
						if (signature != info->version) {
							// Update it
							signatures.Store(data->SourceFile->Path(),
							                 signature);
						}
					}

					// Store d.ts emit hash so later can be compared
					// to check if d.ts has changed. Currently we do
					// this only for composite projects since these
					// are the only projects that can be referenced
					// by other projects and would need their d.ts
					// change time in --build mode
					if (skipDtsOutputOfComposite(
					        data->SourceFile, fileName, text, data,
					        emitSignatureText, &differsOnlyInMap)) {
						return std::nullopt;
					}
				}
			}

			std::filesystem::file_time_type aTime;
			if (differsOnlyInMap) {
				aTime = program->host_->GetMTime(fileName);
			}
			std::optional<std::string> err;
			if (options.WriteFile) {
				err = options.WriteFile(fileName, text, data);
			} else {
				err = program->program_->Host()->WriteFile(fileName,
				                                         text);
			}
			if (!err.has_value() && differsOnlyInMap) {
				// Revert the time to original one
				err = program->host_->SetMTime(fileName, aTime);
			}
			return err;
		};
		return result;
	}

	// emitfileshandler.go:244 skipDtsOutputOfComposite — returns true when
	// the d.ts output should be skipped (contents unchanged).
	bool skipDtsOutputOfComposite(
	    SourceFile* file, const std::string& outputFileName,
	    std::string_view text, compiler::WriteFileData* data,
	    const std::string& newSignatureIn, bool* differsOnlyInMap) {
		if (program->snapshot_->options->Composite != Tristate::True) {
			return false;
		}
		std::string newSignature = newSignatureIn;
		std::string oldSignature;
		auto [oldSignatureFormat, ok] =
		    program->snapshot_->emitSignatures.Load(file->Path());
		if (ok) {
			if (!oldSignatureFormat->signature.empty()) {
				oldSignature = oldSignatureFormat->signature;
			} else {
				oldSignature = oldSignatureFormat
				                   ->signatureWithDifferentOptions
				                   .value()[0];
			}
		}
		if (newSignature.empty()) {
			newSignature = program->snapshot_->computeHash(
			    getTextHandlingSourceMapForSignature(text, data));
		}
		// Dont write dts files if they didn't change
		if (newSignature == oldSignature) {
			// If the signature was encoded as string the dts map options
			// match so nothing to do
			if (oldSignatureFormat != nullptr &&
			    oldSignatureFormat->signature == oldSignature) {
				data->SkippedDtsWrite = true;
				return true;
			} else {
				// Mark as differsOnlyInMap so that we can reverse the
				// timestamp with --build so that the downstream
				// projects dont detect this as change in d.ts file
				*differsOnlyInMap =
				    program->Options()->Build == Tristate::True;
			}
		} else {
			latestChangedDtsFiles.Store(file->Path(), outputFileName);
		}
		auto* e = new emitSignature();
		e->signature = newSignature;
		emitSignatures.Store(file->Path(), e);
		return false;
	}

	// emitfileshandler.go:281 updateSnapshot.
	std::vector<compiler::EmitResult*> updateSnapshot() {
		if (program->snapshot_->canUseIncrementalState()) {
			signatures.Range(
			    [&](const tspath::Path& file, const std::string& sig) {
				    auto [info, _] =
				        program->snapshot_->fileInfos.Load(file);
				    info->signature = sig;
				    if (program->testingData_ != nullptr) {
					    program->testingData_->UpdatedSignatureKinds
					        [file] =
					            SignatureUpdateKind::StoredAtEmit;
				    }
				    program->snapshot_->buildInfoEmitPending.store(true);
				    return true;
			    });
			emitSignatures.Range(
			    [&](const tspath::Path& file, incremental::emitSignature* sig) {
				    program->snapshot_->emitSignatures.Store(file,
				                                             sig);
				    program->snapshot_->buildInfoEmitPending.store(true);
				    return true;
			    });
			for (const auto& file : deletedPendingKinds.Keys()) {
				program->snapshot_->affectedFilesPendingEmit.Delete(
				    file);
				program->snapshot_->buildInfoEmitPending.store(true);
			}
			// Always use correct order when to collect the result
			std::vector<compiler::EmitResult*> results;
			for (auto* file : program->GetSourceFiles()) {
				auto [latestChangedDtsFile, dtsOk] =
				    latestChangedDtsFiles.Load(file->Path());
				if (dtsOk) {
					program->snapshot_->latestChangedDtsFile =
					    latestChangedDtsFile;
					program->snapshot_->buildInfoEmitPending.store(
					    true);
					program->snapshot_->hasChangedDtsFile = true;
				}
				auto [update, ok] = emitUpdates.Load(file->Path());
				if (ok) {
					if (!update->dtsErrorsFromCache) {
						if (update->pendingKind == 0) {
							program->snapshot_
							    ->affectedFilesPendingEmit.Delete(
							        file->Path());
						} else {
							program->snapshot_
							    ->affectedFilesPendingEmit.Store(
							        file->Path(), update->pendingKind);
						}
						program->snapshot_->buildInfoEmitPending.store(
						    true);
					}
					if (update->result != nullptr) {
						results.push_back(update->result);
						if (!update->result->Diagnostics.empty()) {
							auto* diags =
							    new DiagnosticsOrBuildInfoDiagnosticsWithFileName();
							diags->diagnostics =
							    update->result->Diagnostics;
							program->snapshot_
							    ->emitDiagnosticsPerFile.Store(
							        file->Path(), diags);
						}
					}
				}
			}
			return results;
		} else if (hasEmitDiagnostics.load()) {
			program->snapshot_->hasEmitDiagnostics = true;
		}
		return {};
	}
};

}  // namespace

// emitfileshandler.go:375 emitFiles.
compiler::EmitResult* emitFiles(Program* p,
                                const compiler::EmitOptions& options,
                                bool isForDtsErrors) {
	emitFilesHandler emitHandler{p, isForDtsErrors};

	// Single file emit - do direct from program
	if (!isForDtsErrors && !options.TargetSourceFiles.empty()) {
		auto emitOptions = emitHandler.getEmitOptions(options);
		auto* result = p->program_->Emit(&emitOptions);
		emitHandler.updateHasEmitDiagnostics(result);
		emitHandler.updateSnapshot();
		return result;
	}

	// Emit only affected files if using builder for emit
	return emitHandler.emitAllAffectedFiles(options);
}

}  // namespace tsc::execute::incremental
