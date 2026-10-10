// Port of tsc/internal/execute/incremental/affectedfileshandler.go —
// affected-file tracking: signature updates, semantic-diagnostic removal,
// and d.ts-may-change propagation through the reference map.
#include <mutex>
#include <utility>

#include "internal/checker/checker.h"
#include "internal/core/utilities.h"
#include "internal/execute/incremental/incremental.h"

namespace tsc::execute::incremental {
namespace {

using dtsMayChange = std::unordered_map<tspath::Path, FileEmitKind>;

struct updatedSignature {
	std::mutex mu;
	std::string signature;
	SignatureUpdateKind kind = SignatureUpdateKind::ComputedDts;
};

// affectedfileshandler.go:26 affectedFilesHandler. Go fans work out through
// a WorkGroup when multi-threaded; this port executes inline — the
// SyncMap/SyncSet fields keep the Go layout.
struct affectedFilesHandler {
	Program* program;
	std::atomic<bool> hasAllFilesExcludingDefaultLibraryFile{false};
	collections::SyncMap<tspath::Path, updatedSignature*> updatedSignatures;
	std::vector<dtsMayChange> dtsMayChanges;
	collections::SyncSet<tspath::Path> filesToRemoveDiagnostics;
	OnceFlag cleanedDiagnosticsOfLibFiles;
	collections::SyncMap<tspath::Path, bool> seenFileAndReferences;

	dtsMayChange* getDtsMayChange(tspath::Path affectedFilePath,
	                              FileEmitKind affectedFileEmitKind) {
		// Go: dtsMayChange{affectedFilePath: affectedFileEmitKind}
		dtsMayChanges.push_back(
		    {{std::move(affectedFilePath), affectedFileEmitKind}});
		return &dtsMayChanges.back();
	}

	bool isChangedSignature(const tspath::Path& path) {
		auto [newSignature, _] = updatedSignatures.Load(path);
		// This method is called after updating signatures of that path,
		// so signature is present in updatedSignatures and is already
		// calculated, so no need to lock and unlock mutex on the entry
		auto [oldInfo, _2] =
		    program->snapshot_->fileInfos.Load(path);
		return newSignature->signature != oldInfo->signature;
	}

	void removeSemanticDiagnosticsOf(const tspath::Path& path) {
		filesToRemoveDiagnostics.Add(path);
	}

	void removeDiagnosticsOfLibraryFiles() {
		cleanedDiagnosticsOfLibFiles.run([&]() {
			for (auto* file : program->GetSourceFiles()) {
				if (program->program_->IsSourceFileDefaultLibrary(
				        file->Path()) &&
				    !program->program_->SkipTypeChecking(file, true)) {
					removeSemanticDiagnosticsOf(file->Path());
				}
			}
		});
	}

	std::string computeDtsSignature(SourceFile* file) {
		std::string signature;
		auto done = program->beginNestedEmit();
		compiler::EmitOptions emitOptions;
		emitOptions.TargetSourceFiles = {file};
		emitOptions.EmitOnly = compiler::EmitOnly::EmitOnlyBuilderSignature;
		emitOptions.WriteFile =
		    [&](const std::string& fileName, const std::string& text,
		        compiler::WriteFileData* data)
		    -> std::optional<std::string> {
			if (!tspath::isDeclarationFileName(fileName)) {
				TSC_UNREACHABLE(
				    "File extension for signature expected to be dts");
			}
			signature = program->snapshot_
			                ->computeSignatureWithDiagnostics(file, text,
			                                                  data);
			return std::nullopt;
		};
		program->program_->Emit(&emitOptions);
		done();
		return signature;
	}

	bool updateShapeSignature(SourceFile* file,
	                          bool useFileVersionAsSignature) {
		auto* update = new updatedSignature();
		update->mu.lock();
		std::unique_lock<std::mutex> updateLock(update->mu,
		                                        std::adopt_lock);
		// If we have cached the result for this file, that means hence
		// forth we should assume file shape is uptodate
		auto [existing, ok] =
		    updatedSignatures.LoadOrStore(file->Path(), update);
		if (ok) {
			// Ensure calculations for existing ones are complete before
			// using the value
			std::lock_guard<std::mutex> existingLock(existing->mu);
			return false;
		}

		auto [info, _] =
		    program->snapshot_->fileInfos.Load(file->Path());
		auto prevSignature = info->signature;
		// JSON files have no declaration output from which to compute a
		// shape signature, so use the file version to conservatively
		// invalidate dependents.
		if (!file->IsDeclarationFile && !isJsonSourceFile(file) &&
		    !useFileVersionAsSignature) {
			update->signature = computeDtsSignature(file);
		}
		// Default is to use file version as signature
		if (update->signature.empty()) {
			update->signature = info->version;
			update->kind = SignatureUpdateKind::UsedVersion;
		}
		return update->signature != prevSignature;
	}

	std::vector<SourceFile*> getFilesAffectedBy(const tspath::Path& path) {
		auto* file = program->program_->GetSourceFileByPath(path);
		if (file == nullptr) {
			return {};
		}

		if (!updateShapeSignature(file, false)) {
			return {file};
		}

		auto [info, _] =
		    program->snapshot_->fileInfos.Load(file->Path());
		if (info->affectsGlobalScope) {
			hasAllFilesExcludingDefaultLibraryFile.store(true);
			return program->snapshot_
			    ->getAllFilesExcludingDefaultLibraryFile(
			        program->program_, file);
		}

		if (program->snapshot_->options->IsolatedModules ==
		    Tristate::True) {
			return {file};
		}

		// Now we need to if each file in the referencedBy list has a
		// shape change as well. Because if so, its own referencedBy
		// files need to be saved as well to make the emitting result
		// consistent with files on disk.
		auto seenFileNamesMap = forEachFileReferencedBy(
		    file, [&](SourceFile* currentFile,
		              const tspath::Path& currentPath) {
			    // If the current file is not nil and has a shape
			    // change, we need to queue it for processing
			    if (currentFile != nullptr &&
			        updateShapeSignature(currentFile, false)) {
				    return std::pair{true, false};
			    }
			    return std::pair{false, false};
		    });
		// Return array of values that needs emit
		std::vector<SourceFile*> result;
		for (const auto& kv : seenFileNamesMap) {
			if (kv.second != nullptr) {
				result.push_back(kv.second);
			}
		}
		return result;
	}

	// affectedfileshandler.go:153 forEachFileReferencedBy.
	std::unordered_map<tspath::Path, SourceFile*>
	forEachFileReferencedBy(
	    SourceFile* file,
	    const std::function<std::pair<bool, bool>(
	        SourceFile* currentFile, const tspath::Path& currentPath)>& fn) {
		// Now we need to if each file in the referencedBy list has a
		// shape change as well. Because if so, its own referencedBy
		// files need to be saved as well to make the emitting result
		// consistent with files on disk.
		std::unordered_map<tspath::Path, SourceFile*> seenFileNamesMap;
		// Start with the paths this file was referenced by
		seenFileNamesMap[file->Path()] = file;
		std::vector<tspath::Path> queue;
		program->snapshot_->referencedMap.getReferencedBy(
		    file->Path(), [&](const tspath::Path& p) {
			    queue.push_back(p);
			    return true;
		    });
		while (!queue.empty()) {
			auto currentPath = queue.back();
			queue.pop_back();
			if (seenFileNamesMap.find(currentPath) ==
			    seenFileNamesMap.end()) {
				auto* currentFile =
				    program->program_->GetSourceFileByPath(
				        currentPath);
				seenFileNamesMap[currentPath] = currentFile;
				auto [queueForFile, fastReturn] =
				    fn(currentFile, currentPath);
				if (fastReturn) {
					return seenFileNamesMap;
				}
				if (queueForFile) {
					program->snapshot_->referencedMap
					    .getReferencedBy(
					        currentFile->Path(),
					        [&](const tspath::Path& ref) {
						        queue.push_back(ref);
						        return true;
					        });
				}
			}
		}
		return seenFileNamesMap;
	}

	// affectedfileshandler.go:190 handleDtsMayChangeOfAffectedFile —
	// Handles semantic diagnostics and dts emit for affectedFile and
	// files, that are referencing modules that export entities from
	// affected file. This is because even though js emit doesnt change,
	// dts emit / type used can change resulting in need for dts emit and
	// js change.
	void handleDtsMayChangeOfAffectedFile(dtsMayChange* dtsMayChangeArg,
	                                      SourceFile* affectedFile) {
		removeSemanticDiagnosticsOf(affectedFile->Path());

		// If affected files is everything except default library, then
		// nothing more to do
		if (hasAllFilesExcludingDefaultLibraryFile.load()) {
			removeDiagnosticsOfLibraryFiles();
			// When a change affects the global scope, all files are
			// considered to be affected without updating their
			// signature. That means when affected file is handled, its
			// signature can be out of date. To avoid this, ensure that
			// we update the signature for any affected file in this
			// scenario.
			updateShapeSignature(affectedFile, false);
			return;
		}

		if (program->snapshot_->options
		        ->AssumeChangesOnlyAffectDirectDependencies ==
		    Tristate::True) {
			return;
		}

		// Iterate on referencing modules that export entities from
		// affected file and delete diagnostics and add pending emit
		// If there was change in signature (dts output) for the changed
		// file, then only we need to handle pending file emit
		if (!program->snapshot_->changedFilesSet.Has(
		        affectedFile->Path()) ||
		    !isChangedSignature(affectedFile->Path())) {
			return;
		}

		// At this point affectedFile is actually one of the changed
		// files that has some change in its .d.ts signature.

		// Since isolated modules dont change js files, files affected
		// by change in signature is itself. But we need to cleanup
		// semantic diagnostics and queue dts emit for affected files
		if (program->snapshot_->options->IsolatedModules ==
		    Tristate::True) {
			forEachFileReferencedBy(
			    affectedFile, [&](SourceFile* currentFile,
			                      const tspath::Path& currentPath) {
				    if (handleDtsMayChangeOfGlobalScope(
				            dtsMayChangeArg, currentPath,
				            /*invalidateJsFiles*/ false)) {
					    return std::pair{false, true};
				    }
				    handleDtsMayChangeOf(dtsMayChangeArg, currentPath,
				                         /*invalidateJsFiles*/ false);
				    if (isChangedSignature(currentPath)) {
					    return std::pair{true, false};
				    }
				    return std::pair{false, false};
			    });
		}

		bool invalidateJsFiles = false;
		checker::Checker* typeChecker = nullptr;
		std::function<void()> done;
		// If exported const enum, we need to ensure that js files are
		// emitted as well since the const enum value changed
		if (affectedFile->Symbol != nullptr) {
			for (const auto& [name, exported] :
			     affectedFile->Symbol->data->exports) {
				if ((exported->flags & SymbolFlagsConstEnum) != 0) {
					invalidateJsFiles = true;
					break;
				}
				if (typeChecker == nullptr) {
					auto p = program->program_
					             ->GetTypeCheckerForFileExclusive(
					                 affectedFile);
					typeChecker = p.first;
					done = p.second;
				}
				auto* aliased =
				    checker::SkipAlias(exported, typeChecker);
				if (aliased == exported) {
					continue;
				}
				if ((aliased->flags & SymbolFlagsConstEnum) != 0) {
					bool found = false;
					for (auto* d : aliased->data->declarations) {
						if (getSourceFileOfNode(d) ==
						    affectedFile) {
							found = true;
							break;
						}
					}
					if (found) {
						invalidateJsFiles = true;
						break;
					}
				}
			}
		}
		if (done) {
			done();
		}

		// Go through files that reference affected file and handle dts
		// emit and semantic diagnostics for them and their references
		bool earlyReturn = false;
		program->snapshot_->referencedMap.getReferencedBy(
		    affectedFile->Path(), [&](const tspath::Path&
		                                  fileReferencingChangedFile) {
			    if (earlyReturn) return false;
			    if (handleDtsMayChangeOfGlobalScope(
			            dtsMayChangeArg, fileReferencingChangedFile,
			            invalidateJsFiles)) {
				    earlyReturn = true;
				    return false;
			    }
			    // Since references of changed file = affected files
			    // - we would have already handled d.ts emit and
			    // semantic diagnostics for those files. Now we need
			    // to handle files referencing those affected files
			    // to ensure correctness.
			    program->snapshot_->referencedMap.getReferencedBy(
			        fileReferencingChangedFile,
			        [&](const tspath::Path&
			                fileReferencingAffectedFile) {
				        if (handleDtsMayChangeOfFileAndReferences(
				                dtsMayChangeArg,
				                fileReferencingAffectedFile,
				                invalidateJsFiles)) {
					        earlyReturn = true;
					        return false;
				        }
				        return true;
			        });
			    return !earlyReturn;
		    });
	}

	// affectedfileshandler.go:258
	// handleDtsMayChangeOfFileAndReferences.
	bool handleDtsMayChangeOfFileAndReferences(
	    dtsMayChange* dtsMayChangeArg, const tspath::Path& filePath,
	    bool invalidateJsFiles) {
		auto [existing, loaded] =
		    seenFileAndReferences.LoadOrStore(filePath,
		                                      invalidateJsFiles);
		if (loaded && (existing || !invalidateJsFiles)) {
			return false;
		}
		if (loaded && invalidateJsFiles) {
			seenFileAndReferences.Store(filePath, true);
		}

		if (handleDtsMayChangeOfGlobalScope(dtsMayChangeArg, filePath,
		                                    invalidateJsFiles)) {
			return true;
		}
		handleDtsMayChangeOf(dtsMayChangeArg, filePath,
		                     invalidateJsFiles);

		// Remove the diagnostics of files that import this file and any
		// files that are referenced by it (directly or indirectly)
		bool earlyReturn = false;
		program->snapshot_->referencedMap.getReferencedBy(
		    filePath, [&](const tspath::Path& referencingFilePath) {
			    if (handleDtsMayChangeOfFileAndReferences(
			            dtsMayChangeArg, referencingFilePath,
			            invalidateJsFiles)) {
				    earlyReturn = true;
				    return false;
			    }
			    return true;
		    });
		return earlyReturn;
	}

	// affectedfileshandler.go:276 handleDtsMayChangeOfGlobalScope.
	bool handleDtsMayChangeOfGlobalScope(dtsMayChange* dtsMayChangeArg,
	                                     const tspath::Path& filePath,
	                                     bool invalidateJsFiles) {
		auto [info, ok] =
		    program->snapshot_->fileInfos.Load(filePath);
		if (!ok || !info->affectsGlobalScope) {
			return false;
		}
		// Every file needs to be handled
		for (auto* file :
		     program->snapshot_->getAllFilesExcludingDefaultLibraryFile(
		         program->program_, nullptr)) {
			handleDtsMayChangeOf(dtsMayChangeArg, file->Path(),
			                     invalidateJsFiles);
		}
		removeDiagnosticsOfLibraryFiles();
		return true;
	}

	// affectedfileshandler.go:288 handleDtsMayChangeOf — Handle the dts
	// may change, so they need to be added to pending emit if dts emit
	// is enabled. Also we need to make sure signature is updated for
	// these files.
	void handleDtsMayChangeOf(dtsMayChange* dtsMayChangeArg,
	                          const tspath::Path& path,
	                          bool invalidateJsFiles) {
		if (program->snapshot_->changedFilesSet.Has(path)) {
			return;
		}
		auto* file = program->program_->GetSourceFileByPath(path);
		if (file == nullptr) {
			return;
		}
		removeSemanticDiagnosticsOf(path);
		// Even though the js emit doesnt change and we are already
		// handling dts emit and semantic diagnostics we need to update
		// the signature to reflect correctness of the signature(which
		// is output d.ts emit) of this file. This ensures that we dont
		// later during incremental builds considering wrong signature.
		// Eg where this also is needed to ensure that .tsbuildinfo
		// generated by incremental build should be same as if it was
		// first fresh build. But we avoid expensive full shape
		// computation, as using file version as shape is enough for
		// correctness.
		updateShapeSignature(file, true);
		// If not dts emit, nothing more to do
		if (invalidateJsFiles) {
			(*dtsMayChangeArg)[path] =
			    GetFileEmitKind(program->snapshot_->options);
		} else if (program->snapshot_->options->GetEmitDeclarations()) {
			(*dtsMayChangeArg)[path] =
			    program->snapshot_->options->DeclarationMap ==
			            Tristate::True
			        ? FileEmitKindAllDts
			        : FileEmitKindDts;
		}
	}

	// affectedfileshandler.go:312 updateSnapshot.
	void updateSnapshot() {
		updatedSignatures.Range(
		    [&](const tspath::Path& filePath, updatedSignature* update) {
			    auto [info, ok] =
			        program->snapshot_->fileInfos.Load(filePath);
			    if (ok) {
				    info->signature = update->signature;
				    if (program->testingData_ != nullptr) {
					    program->testingData_
					        ->UpdatedSignatureKinds[filePath] =
					        update->kind;
				    }
			    }
			    return true;
		    });
		filesToRemoveDiagnostics.Range([&](const tspath::Path& file) {
			program->snapshot_->semanticDiagnosticsPerFile.Delete(
			    file);
			return true;
		});
		for (auto& change : dtsMayChanges) {
			for (auto& [filePath, emitKind] : change) {
				program->snapshot_
				    ->addFileToAffectedFilesPendingEmit(filePath,
				                                        emitKind);
			}
		}
		program->snapshot_->changedFilesSet =
		    collections::SyncSet<tspath::Path>();
		program->snapshot_->buildInfoEmitPending.store(true);
	}
};

}  // namespace

// affectedfileshandler.go:335 collectAllAffectedFiles — ctx checks dropped
// (no cancellation in this port).
void collectAllAffectedFiles(Program* program) {
	if (program->snapshot_->changedFilesSet.Size() == 0) {
		return;
	}

	affectedFilesHandler handler;
	handler.program = program;
	// affectedfileshandler.go:365 — Go: `wg := core.NewWorkGroup(...)`.
	std::unique_ptr<workGroup> wg(
	    newWorkGroup(program->program_->SingleThreaded()));
	collections::SyncSet<SourceFile*> result;
	program->snapshot_->changedFilesSet.Range(
	    [&](const tspath::Path& file) {
		    wg->Queue([&, file] {
			    for (auto* affectedFile :
			         handler.getFilesAffectedBy(file)) {
				    result.Add(affectedFile);
			    }
		    });
		    return true;
	    });
	wg->RunAndWait();

	// For all the affected files, get all the files that would need to
	// change their dts or js files, update their diagnostics
	wg.reset(newWorkGroup(program->program_->SingleThreaded()));
	auto emitKind = GetFileEmitKind(program->snapshot_->options);
	result.Range([&](SourceFile* file) {
		// remove the cached semantic diagnostics and handle dts emit
		// and js emit if needed
		auto* change =
		    handler.getDtsMayChange(file->Path(), emitKind);
		wg->Queue([&, file, change] {
			handler.handleDtsMayChangeOfAffectedFile(change, file);
		});
		return true;
	});
	wg->RunAndWait();

	// Update the snapshot with the new state
	handler.updateSnapshot();
}

}  // namespace tsc::execute::incremental
