// Port of tsc/internal/execute/incremental/programtosnapshot.go —
// programToSnapshot: builds a fresh snapshot from a SimpleProgram, reusing
// state from the old incremental program where possible.
#include <algorithm>
#include <utility>

#include "internal/binder/binder.h"
#include "internal/checker/checker.h"
#include "internal/execute/incremental/incremental.h"

namespace tsc::execute::incremental {
namespace {

// Forward declarations — programtosnapshot.go file-local helpers.
bool fileAffectsGlobalScope(SourceFile* file);
collections::Set<tspath::Path>* getReferencedFiles(
    compiler::SimpleProgram* program, SourceFile* file);
DiagnosticsOrBuildInfoDiagnosticsWithFileName* repopulateDiagnosticsOfFile(
    DiagnosticsOrBuildInfoDiagnosticsWithFileName* diags,
    compiler::SimpleProgram* p, SourceFile* file);
std::vector<Diagnostic*>* repopulateDiagnosticsList(
    const std::vector<Diagnostic*>& diags, compiler::SimpleProgram* p,
    SourceFile* file);
std::vector<Diagnostic*>* repopulateDiagnosticMessageChain(
    const std::vector<Diagnostic*>& chain, compiler::SimpleProgram* p,
    SourceFile* file);

// set.Equals — set.go:104. Same pointer -> true; either nil -> false (unless
// both nil); otherwise maps.Equal on the members.
bool setEquals(collections::Set<tspath::Path>* a,
               collections::Set<tspath::Path>* b) {
	if (a == b) {
		return true;
	}
	if (a == nullptr || b == nullptr) {
		return false;
	}
	auto ka = a->Keys();
	auto kb = b->Keys();
	if (ka.size() != kb.size()) {
		return false;
	}
	for (const auto& k : ka) {
		if (!b->Has(k)) {
			return false;
		}
	}
	return true;
}

struct toProgramSnapshot {
	compiler::SimpleProgram* program;
	Program* oldProgram;
	incremental::snapshot* snapshot;
	bool globalFileRemoved = false;

	// programtosnapshot.go:43 reuseFromOldProgram.
	void reuseFromOldProgram() {
		if (oldProgram != nullptr) {
			if (snapshot->options->Composite == Tristate::True) {
				snapshot->latestChangedDtsFile =
				    oldProgram->snapshot_->latestChangedDtsFile;
			}
			// Copy old snapshot's changed files set
			oldProgram->snapshot_->changedFilesSet.Range(
			    [&](const tspath::Path& key) {
				    snapshot->changedFilesSet.Add(key);
				    return true;
			    });
			oldProgram->snapshot_->affectedFilesPendingEmit.Range(
			    [&](const tspath::Path& key, FileEmitKind emitKind) {
				    snapshot->affectedFilesPendingEmit.Store(key,
				                                             emitKind);
				    return true;
			    });
			snapshot->buildInfoEmitPending.store(
			    oldProgram->snapshot_->buildInfoEmitPending.load());
			snapshot->hasErrorsFromOldState =
			    oldProgram->snapshot_->hasErrors;
			snapshot->hasSemanticErrorsFromOldState =
			    oldProgram->snapshot_->hasSemanticErrors;
			snapshot->packageJsonsFromOldState =
			    oldProgram->snapshot_->packageJsons.value_or(
			        std::vector<std::string>{});
			snapshot->missingPackageJsonsFromOldState =
			    oldProgram->snapshot_->missingPackageJsons.value_or(
			        std::vector<std::string>{});
		} else {
			snapshot->buildInfoEmitPending.store(
			    snapshot->options->IsIncremental());
		}
	}

	// programtosnapshot.go:64 computeProgramFileChanges. Go runs the loop
	// through a WorkGroup; this port runs it inline (single-threaded).
	void computeProgramFileChanges() {
		bool canCopySemanticDiagnostics =
		    oldProgram != nullptr &&
		    !tsoptions::CompilerOptionsAffectSemanticDiagnostics(
		        oldProgram->snapshot_->options, program->Options());
		// We can only reuse emit signatures (i.e. .d.ts signatures) if the
		// .d.ts file is unchanged, which will eg be depedent on change in
		// options like declarationDir and outDir options are unchanged.
		// We need to look in oldState.compilerOptions, rather than
		// oldCompilerOptions (i.e.we need to disregard useOldState)
		// because oldCompilerOptions can be undefined if there was change
		// in say module from None to some other option which would make
		// useOldState as false since we can now use reference maps that
		// are needed to track what to emit, what to check etc but that
		// option change does not affect d.ts file name so emitSignatures
		// should still be reused.
		bool canCopyEmitSignatures =
		    snapshot->options->Composite == Tristate::True &&
		    oldProgram != nullptr &&
		    !tsoptions::CompilerOptionsAffectDeclarationPath(
		        oldProgram->snapshot_->options, program->Options());
		bool copyDeclarationFileDiagnostics =
		    canCopySemanticDiagnostics &&
		    (snapshot->options->SkipLibCheck == Tristate::True) ==
		        (oldProgram->snapshot_->options->SkipLibCheck ==
		         Tristate::True);
		bool copyLibFileDiagnostics =
		    copyDeclarationFileDiagnostics &&
		    (snapshot->options->SkipDefaultLibCheck == Tristate::True) ==
		        (oldProgram->snapshot_->options->SkipDefaultLibCheck ==
		         Tristate::True);

		auto files = program->GetSourceFiles();
		for (auto* file : files) {
			std::string versionText = file->Text();
			if (!file->ContentMapper().empty()) {
				versionText = file->OriginalText() + std::string("\x00", 1) +
				              file->ContentMapperTransformIdentity();
			}
			std::string version = snapshot->computeHash(versionText);
			auto impliedNodeFormat =
			    program->GetSourceFileMetaData(file->Path())
			        .ImpliedNodeFormat;
			bool affectsGlobalScope = fileAffectsGlobalScope(file);
			std::string signature;
			auto* newReferences = getReferencedFiles(program, file);
			if (newReferences != nullptr) {
				snapshot->referencedMap.storeReferences(
				    file->Path(), newReferences);
			}
			if (oldProgram != nullptr) {
				auto [oldFileInfo, oldOk] =
				    oldProgram->snapshot_->fileInfos.Load(file->Path());
				if (oldOk) {
					signature = oldFileInfo->signature;
					bool changed = false;
					if (oldFileInfo->version != version ||
					    oldFileInfo->affectsGlobalScope !=
					        affectsGlobalScope ||
					    oldFileInfo->impliedNodeFormat !=
					        impliedNodeFormat) {
						changed = true;
					} else {
						auto [oldReferences, _] =
						    oldProgram->snapshot_->referencedMap
						        .getReferences(file->Path());
						if (!setEquals(newReferences, oldReferences)) {
							// Referenced files changed
							changed = true;
						} else if (newReferences != nullptr) {
							for (const auto& refPath :
							     newReferences->Keys()) {
								if (program->GetSourceFileByPath(
								        refPath) == nullptr) {
									auto [refInfo, refOk] =
									    oldProgram->snapshot_
									        ->fileInfos.Load(refPath);
									if (refOk) {
										// Referenced file was deleted
										// in the new program
										changed = true;
										break;
									}
								}
							}
						}
					}
					if (changed) {
						snapshot->addFileToChangeSet(file->Path());
					}
				} else {
					snapshot->addFileToChangeSet(file->Path());
				}
				if (!snapshot->changedFilesSet.Has(file->Path())) {
					auto [emitDiagnostics, emitOk] =
					    oldProgram->snapshot_->emitDiagnosticsPerFile
					        .Load(file->Path());
					if (emitOk) {
						snapshot->emitDiagnosticsPerFile.Store(
						    file->Path(),
						    repopulateDiagnosticsOfFile(
						        emitDiagnostics, program, file));
					}
					if (canCopySemanticDiagnostics) {
						if ((!file->IsDeclarationFile ||
						     copyDeclarationFileDiagnostics) &&
						    (!program->IsSourceFileDefaultLibrary(
						         file->Path()) ||
						     copyLibFileDiagnostics)) {
							// Unchanged file copy diagnostics
							auto [diagnostics, diagOk] =
							    oldProgram->snapshot_
							        ->semanticDiagnosticsPerFile
							        .Load(file->Path());
							if (diagOk) {
								snapshot->semanticDiagnosticsPerFile
								    .Store(file->Path(),
								           repopulateDiagnosticsOfFile(
								               diagnostics, program,
								               file));
							}
						}
					}
				}
				if (canCopyEmitSignatures) {
					auto [oldEmitSignature, sigOk] =
					    oldProgram->snapshot_->emitSignatures.Load(
					        file->Path());
					if (sigOk) {
						snapshot->emitSignatures.Store(
						    file->Path(),
						    oldEmitSignature
						        ->getNewEmitSignature(
						            oldProgram->snapshot_->options,
						            snapshot->options));
					}
				}
			} else {
				snapshot->addFileToAffectedFilesPendingEmit(
				    file->Path(), GetFileEmitKind(snapshot->options));
				signature = version;
			}
			auto* info = new FileInfo();
			info->version = version;
			info->signature = signature;
			info->affectsGlobalScope = affectsGlobalScope;
			info->impliedNodeFormat = impliedNodeFormat;
			snapshot->fileInfos.Store(file->Path(), info);
		}
	}

	// programtosnapshot.go:166 handleFileDelete.
	void handleFileDelete() {
		if (oldProgram != nullptr) {
			// If the global file is removed, add all files as changed
			oldProgram->snapshot_->fileInfos.Range(
			    [&](const tspath::Path& filePath, FileInfo* oldInfo) {
				    auto [_, ok] =
				        snapshot->fileInfos.Load(filePath);
				    if (!ok) {
					    if (oldInfo->affectsGlobalScope) {
						    for (auto* file :
						         snapshot
						             ->getAllFilesExcludingDefaultLibraryFile(
						                 program, nullptr)) {
							    snapshot->addFileToChangeSet(
							        file->Path());
						    }
						    globalFileRemoved = true;
					    } else {
						    snapshot->buildInfoEmitPending.store(true);
					    }
					    return false;
				    }
				    return true;
			    });
		}
	}

	// programtosnapshot.go:184 handleGlobalScopeChange.
	void handleGlobalScopeChange() {
		if (oldProgram == nullptr || globalFileRemoved) {
			return;
		}
		bool globalScopeLost = false;
		oldProgram->snapshot_->fileInfos.Range(
		    [&](const tspath::Path& filePath, FileInfo* oldInfo) {
			    if (!oldInfo->affectsGlobalScope) {
				    return true;
			    }
			    auto [newInfo, ok] =
			        snapshot->fileInfos.Load(filePath);
			    if (ok && !newInfo->affectsGlobalScope) {
				    globalScopeLost = true;
				    return false;
			    }
			    return true;
		    });
		if (globalScopeLost) {
			for (auto* file :
			     snapshot->getAllFilesExcludingDefaultLibraryFile(
			         program, nullptr)) {
				snapshot->addFileToChangeSet(file->Path());
			}
		}
	}

	// programtosnapshot.go:202 handlePendingEmit.
	void handlePendingEmit() {
		if (oldProgram != nullptr && !globalFileRemoved) {
			// If options affect emit, then we need to do complete emit
			// per compiler options otherwise only the js or dts that
			// needs to emitted because its different from previously
			// emitted options
			FileEmitKind pendingEmitKind;
			if (tsoptions::CompilerOptionsAffectEmit(
			        oldProgram->snapshot_->options,
			        snapshot->options)) {
				pendingEmitKind = GetFileEmitKind(snapshot->options);
			} else {
				pendingEmitKind = getPendingEmitKindWithOptions(
				    snapshot->options, oldProgram->snapshot_->options);
			}
			if (pendingEmitKind != FileEmitKindNone) {
				// Add all files to affectedFilesPendingEmit since emit
				// changed
				for (auto* file : program->GetSourceFiles()) {
					// Add to affectedFilesPending emit only if not
					// changed since any changed file will do full emit
					if (!snapshot->changedFilesSet.Has(file->Path())) {
						snapshot->addFileToAffectedFilesPendingEmit(
						    file->Path(), pendingEmitKind);
					}
				}
				snapshot->buildInfoEmitPending.store(true);
			}
		}
	}

	// programtosnapshot.go:220 handlePendingCheck.
	void handlePendingCheck() {
		if (oldProgram != nullptr &&
		    snapshot->semanticDiagnosticsPerFile.Size() !=
		        static_cast<int64_t>(program->GetSourceFiles().size()) &&
		    oldProgram->snapshot_->checkPending != snapshot->checkPending) {
			snapshot->buildInfoEmitPending.store(true);
		}
	}
};

// programtosnapshot.go:230 fileAffectsGlobalScope.
bool fileAffectsGlobalScope(SourceFile* file) {
	bindSourceFile(file);
	// if file contains anything that augments to global scope we need to
	// build them as if they are global files as well as module
	for (auto* augmentation : file->ModuleAugmentations) {
		if (isGlobalScopeAugmentation(augmentation->parent)) {
			return true;
		}
	}

	if (isExternalOrCommonJSModule(file) || isJsonSourceFile(file)) {
		return false;
	}

	// For script files that contains only ambient external modules,
	// although they are not actually external module files, they can only
	// be consumed via importing elements from them. Regular script files
	// cannot consume them. Therefore, there are no point to rebuild all
	// script files if these special files have changed. However, if any
	// statement in the file is not ambient external module, we treat it
	// as a regular script file.
	if (file->Statements == nullptr) {
		return false;
	}
	for (auto* stmt : file->Statements->nodes) {
		if (!isModuleWithStringLiteralName(stmt)) {
			return true;
		}
	}
	return false;
}

// programtosnapshot.go:251 addReferencedFilesFromSymbol.
void addReferencedFilesFromSymbol(
    SourceFile* file, collections::Set<tspath::Path>* referencedFiles,
    Symbol* symbol) {
	if (symbol == nullptr) {
		return;
	}
	for (auto* declaration : symbol->declarations) {
		auto* fileOfDecl = getSourceFileOfNode(declaration);
		if (fileOfDecl == nullptr) {
			continue;
		}
		if (file != fileOfDecl) {
			referencedFiles->Add(fileOfDecl->Path());
		}
	}
}

// programtosnapshot.go:264 addReferencedFilesFromImportLiteral.
void addReferencedFilesFromImportLiteral(
    SourceFile* file, collections::Set<tspath::Path>* referencedFiles,
    checker::Checker* checker, Node* importName) {
	auto* symbol = checker->GetSymbolAtLocation(importName);
	addReferencedFilesFromSymbol(file, referencedFiles, symbol);
}

// programtosnapshot.go:271 addReferencedFileFromFileName.
void addReferencedFileFromFileName(
    compiler::SimpleProgram* program, const std::string& fileName,
    collections::Set<tspath::Path>* referencedFiles,
    const std::string& sourceFileDirectory) {
	auto redirect = program->GetParseFileRedirect(fileName);
	if (!redirect.empty()) {
		referencedFiles->Add(tspath::toPath(
		    redirect, program->GetCurrentDirectory(),
		    program->UseCaseSensitiveFileNames()));
	} else {
		referencedFiles->Add(tspath::toPath(
		    fileName, sourceFileDirectory,
		    program->UseCaseSensitiveFileNames()));
	}
}

// programtosnapshot.go:291 getReferencedFiles.
collections::Set<tspath::Path>* getReferencedFiles(
    compiler::SimpleProgram* program, SourceFile* file) {
	collections::Set<tspath::Path> referencedFiles;

	// We need to use a set here since the code can contain the same
	// import twice, but that will only be one dependency.
	// To avoid invernal conversion, the key of the referencedFiles map
	// must be of type Path
	auto [checker, done] = program->GetTypeCheckerForFileExclusive(file);
	for (auto* importName : file->imports) {
		addReferencedFilesFromImportLiteral(file, &referencedFiles,
		                                    checker, importName);
	}

	auto sourceFileDirectory = tspath::getDirectoryPath(file->FileName());
	// Handle triple slash references
	for (auto* referencedFile : file->ReferencedFiles) {
		addReferencedFileFromFileName(program, referencedFile->FileName,
		                              &referencedFiles,
		                              sourceFileDirectory);
	}

	// Handle type reference directives
	auto& typeRefs = program->GetResolvedTypeReferenceDirectives();
	auto typeRefsIt = typeRefs.find(file->Path());
	if (typeRefsIt != typeRefs.end()) {
		for (const auto& [key, typeRef] : typeRefsIt->second) {
			if (!typeRef->ResolvedFileName.empty()) {
				addReferencedFileFromFileName(
				    program, typeRef->ResolvedFileName,
				    &referencedFiles, sourceFileDirectory);
			}
		}
	}

	// Add module augmentation as references
	for (auto* moduleName : file->ModuleAugmentations) {
		if (!isStringLiteralLike(moduleName)) {
			continue;
		}
		addReferencedFilesFromImportLiteral(file, &referencedFiles,
		                                    checker, moduleName);
	}

	// From ambient modules
	for (auto* ambientModule : checker->GetAmbientModules()) {
		addReferencedFilesFromSymbol(file, &referencedFiles,
		                             ambientModule);
	}
	done();
	return referencedFiles.Size() > 0
	           ? new collections::Set<tspath::Path>(referencedFiles)
	           : nullptr;
}

// programtosnapshot.go:334 repopulateDiagnosticsOfFile.
DiagnosticsOrBuildInfoDiagnosticsWithFileName* repopulateDiagnosticsOfFile(
    DiagnosticsOrBuildInfoDiagnosticsWithFileName* diags,
    compiler::SimpleProgram* p, SourceFile* file) {
	if (!diags->diagnostics.empty()) {
		auto* repopulated =
		    repopulateDiagnosticsList(diags->diagnostics, p, file);
		if (repopulated == nullptr) {
			return diags;
		}
		auto* result = new DiagnosticsOrBuildInfoDiagnosticsWithFileName();
		result->diagnostics = *repopulated;
		return result;
	}
	// buildInfoDiagnostics will be repopulated via toDiagnostic's
	// repopulateInfo handling
	return diags;
}

// programtosnapshot.go:349 repopulateDiagnosticsList — returns nullptr if
// no diagnostics needed repopulation.
std::vector<Diagnostic*>* repopulateDiagnosticsList(
    const std::vector<Diagnostic*>& diags, compiler::SimpleProgram* p,
    SourceFile* file) {
	bool changed = false;
	std::vector<Diagnostic*> result(diags.size());
	for (size_t i = 0; i < diags.size(); i++) {
		auto* d = diags[i];
		auto* repopulated =
		    repopulateDiagnosticMessageChain(d->MessageChain(), p, file);
		if (repopulated != nullptr) {
			auto* clone = d->Clone();
			clone->SetMessageChain(*repopulated);
			result[i] = clone;
			changed = true;
		} else {
			result[i] = d;
		}
	}
	if (!changed) {
		return nullptr;
	}
	return new std::vector<Diagnostic*>(std::move(result));
}

// programtosnapshot.go:369 repopulateDiagnosticMessageChain.
std::vector<Diagnostic*>* repopulateDiagnosticMessageChain(
    const std::vector<Diagnostic*>& chain, compiler::SimpleProgram* p,
    SourceFile* file) {
	if (chain.empty()) {
		return nullptr;
	}
	bool changed = false;
	std::vector<Diagnostic*> result(chain.size());
	for (size_t i = 0; i < chain.size(); i++) {
		auto* c = chain[i];
		if (c->RepopulateInfo() != nullptr) {
			// Convert to buildInfoDiagnosticWithFileName and repopulate
			auto* b = new buildInfoDiagnosticWithFileName();
			b->pos = c->Pos();
			b->end = c->End();
			b->code = c->Code();
			b->category = c->Category();
			b->source = c->Source();
			b->messageText = c->MessageText();
			b->messageKey = c->MessageKey();
			b->messageArgs = c->MessageArgs();
			b->repopulateInfo = c->RepopulateInfo();
			// Recursively handle nested chains
			for (auto* nested : c->MessageChain()) {
				b->messageChain.push_back(astDiagToBuildInfoDiag(nested));
			}
			result[i] = repopulateDiagnosticChain(b, p, file);
			changed = true;
		} else {
			// Check nested chains
			auto* nested = repopulateDiagnosticMessageChain(
			    c->MessageChain(), p, file);
			if (nested != nullptr) {
				auto* clone = c->Clone();
				clone->SetMessageChain(*nested);
				result[i] = clone;
				changed = true;
			} else {
				result[i] = c;
			}
		}
	}
	if (!changed) {
		return nullptr;
	}
	return new std::vector<Diagnostic*>(std::move(result));
}

}  // namespace

// programtosnapshot.go:410 astDiagToBuildInfoDiag.
buildInfoDiagnosticWithFileName* astDiagToBuildInfoDiag(Diagnostic* d) {
	auto* b = new buildInfoDiagnosticWithFileName();
	b->pos = d->Pos();
	b->end = d->End();
	b->code = d->Code();
	b->category = d->Category();
	b->source = d->Source();
	b->messageText = d->MessageText();
	b->messageKey = d->MessageKey();
	b->messageArgs = d->MessageArgs();
	b->repopulateInfo = d->RepopulateInfo();
	for (auto* nested : d->MessageChain()) {
		b->messageChain.push_back(astDiagToBuildInfoDiag(nested));
	}
	return b;
}


// programtosnapshot.go:16 programToSnapshot.
snapshot* programToSnapshot(compiler::SimpleProgram* program,
                            Program* oldProgram, bool hashWithText) {
	if (oldProgram != nullptr && oldProgram->program_ == program) {
		return oldProgram->snapshot_;
	}
	auto* snapshot = new incremental::snapshot();
	snapshot->options = program->Options();
	snapshot->hashWithText = hashWithText;
	snapshot->checkPending =
	    program->Options()->NoCheck == Tristate::True;
	toProgramSnapshot to{program, oldProgram, snapshot, false};

	if (to.snapshot->canUseIncrementalState()) {
		to.reuseFromOldProgram();
		to.computeProgramFileChanges();
		to.handleFileDelete();
		to.handleGlobalScopeChange();
		to.handlePendingEmit();
		to.handlePendingCheck();
	}
	return snapshot;
}

}  // namespace tsc::execute::incremental
