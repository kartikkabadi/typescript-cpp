// file_rename.go — workspace edits when a file is renamed: tsconfig paths,
// relative imports, triple-slash references.
#include "internal/ls/ls.h"

#include "internal/ast/ast.h"
#include "internal/ast/nodes_generated.h"
#include "internal/checker/checker.h"
#include "internal/compiler/program.h"
#include "internal/debug/debug.h"
#include "internal/modulespecifiers/types.h"
#include "internal/scanner/scanner.h"
#include "internal/tsoptions/tsoptions.h"
#include "internal/tspath/tspath.h"

#include <algorithm>

namespace tsc::ls {

namespace {
// slices.Contains
template <typename T>
bool contains(const std::vector<T>& v, const T& x) {
	return std::find(v.begin(), v.end(), x) != v.end();
}
} // namespace

// Forward declarations — Go package-level functions defined later in this
// file but referenced earlier.
bool updatePathsProperty(SourceFile* configFile,
                         const std::string& configDir,
                         PropertyAssignment* property,
                         change::Tracker* changeTracker,
                         pathUpdater oldToNew,
                         lsconv::Converters* converters,
                         bool useCaseSensitiveFileNames);
bool tryUpdateConfigString(SourceFile* configFile,
                           const std::string& configDir, Node* element,
                           change::Tracker* changeTracker,
                           pathUpdater oldToNew,
                           lsconv::Converters* converters,
                           bool useCaseSensitiveFileNames);
toImport* getSourceFileToImport(compiler::SimpleProgram* program,
                                SourceFile* sourceFile,
                                Node* importLiteral,
                                pathUpdater oldToNew);
std::string getUpdatedImportSpecifierFromMovedSourceFiles(
    compiler::SimpleProgram* program, SourceFile* sourceFile,
    Node* importLiteral, const std::vector<movedFile>& movedFiles,
    const std::string& importingSourceFileName,
    const modulespecifiers::UserPreferences& userPreferences);
TextRange createStringTextRange(SourceFile* sourceFile, Node* node);
ObjectLiteralExpression* getTsConfigObjectLiteralExpression(
    SourceFile* tsConfigSourceFile);
void forEachObjectProperty(
    ObjectLiteralExpression* objectLiteral,
    const std::function<void(PropertyAssignment*, const std::string&)>& cb);
std::string relativePathFromDirectory(const std::string& fromDirectory,
                                      const std::string& to,
                                      bool useCaseSensitiveFileNames);
std::string relativeImportPathFromDirectory(
    const std::string& fromDirectory, const std::string& to,
    bool useCaseSensitiveFileNames);
bool isAmbientModuleSymbol(Symbol* symbol);

// GetEditsForFileRename — file_rename.go:34.
std::vector<lsproto::TextDocumentEditOrCreateFileOrRenameFileOrDeleteFile>
LanguageService::GetEditsForFileRename(const gostd::Context& ctx,
                                       lsproto::DocumentUri oldURI,
                                       lsproto::DocumentUri newURI) {
	auto* program = GetProgram();
	auto oldPath = lsproto::documentUriFileName(oldURI);
	auto newPath = lsproto::documentUriFileName(newURI);

	auto oldToNew = createPathUpdater(oldPath, newPath);

	auto* changeTracker =
	    new change::Tracker(format::FormatRequestContext{},
	                        program->Options(), FormatOptions(),
	                        converters);
	updateTsconfigFiles(program, changeTracker, oldToNew, oldPath,
	                    newPath);
	updateImportsForFileRename(program, changeTracker, oldToNew);

	std::vector<
	    lsproto::TextDocumentEditOrCreateFileOrRenameFileOrDeleteFile>
	    documentChanges;

	// When renaming e.g. `foo.d.css.ts` -> `bar.d.css.ts`, also rename
	// `foo.css` -> `bar.css` if it exists.
	if (tspath::isDeclarationFileName(oldPath) &&
	    tspath::isDeclarationFileName(newPath)) {
		auto dtsExt = tspath::getDeclarationFileExtension(oldPath);
		auto originalExtensions =
		    tspath::getPossibleOriginalInputExtensionForExtension(dtsExt);
		for (auto& ext : originalExtensions) {
			auto oldOriginalPath =
			    tspath::changeFullExtension(oldPath, ext);
			if (host->FileExists(oldOriginalPath)) {
				auto newDtsExt =
				    tspath::getDeclarationFileExtension(oldPath);
				auto newOriginalExtensions =
				    tspath::
				        getPossibleOriginalInputExtensionForExtension(
				            newDtsExt);
				if (contains(newOriginalExtensions, ext)) {
					auto newOriginalPath =
					    tspath::changeFullExtension(newPath, ext);
					documentChanges.push_back(
					    lsproto::
					        TextDocumentEditOrCreateFileOrRenameFileOrDeleteFile{
					            .RenameFile =
					                std::shared_ptr<lsproto::RenameFile>(
					                    new lsproto::RenameFile{
					                        .OldUri = lsconv::
					                            FileNameToDocumentURI(
					                                oldOriginalPath),
					                        .NewUri = lsconv::
					                            FileNameToDocumentURI(
					                                newOriginalPath),
					                    }),
					        });
				}
			}
		}
	}

	auto [changes, _] = changeTracker->GetChanges();
	for (auto& [fileName, edits] : changes) {
		auto uri = lsconv::FileNameToDocumentURI(fileName);
		std::vector<
		    lsproto::TextEditOrAnnotatedTextEditOrSnippetTextEdit>
		    lspEdits;
		lspEdits.reserve(edits.size());
		for (auto& edit : edits) {
			lspEdits.push_back(
			    lsproto::
			        TextEditOrAnnotatedTextEditOrSnippetTextEdit{
			            .TextEdit =
			                std::make_shared<lsproto::TextEdit>(
			                    std::move(edit)),
			        });
		}
		documentChanges.push_back(
		    lsproto::
		        TextDocumentEditOrCreateFileOrRenameFileOrDeleteFile{
		            .TextDocumentEdit =
		                std::shared_ptr<lsproto::TextDocumentEdit>(
		                    new lsproto::TextDocumentEdit{
		                        .TextDocument = lsproto::
		                            OptionalVersionedTextDocumentIdentifier{
		                                .Uri = uri},
		                        .Edits = lspEdits,
		                    }),
		        });
	}

	return documentChanges;
}

// createPathUpdater — file_rename.go:89.
pathUpdater LanguageService::createPathUpdater(
    const std::string& oldPath, const std::string& newPath) {
	tspath::ComparePathsOptions compareOptions{
	    .useCaseSensitiveFileNames = UseCaseSensitiveFileNames()};
	// removeTrailingDirectorySeparator returns a string_view: copy it
	// here so the lambda below can outlive the caller's oldPath.
	std::string trimmedOldPath{
	    tspath::removeTrailingDirectorySeparator(oldPath)};
	return [oldPath, newPath, compareOptions, trimmedOldPath,
	    ucsf = UseCaseSensitiveFileNames()](
	       const std::string& path) -> std::pair<std::string, bool> {
		if (tspath::comparePaths(path, oldPath, compareOptions) == 0) {
			return {newPath, true};
		}
		// Trim the directory prefix ourselves (rather than using
		// tspath.StartsWithDirectory followed by a separate slice on
		// len(oldPath)) so the containment check and the suffix we
		// return can never disagree, and so we don't slice path by a
		// byte count derived from a canonicalized/differently-cased
		// string: case-folding can change a path's UTF-8 byte length
		// without changing its rune count (e.g. the Kelvin sign
		// 'K' folds to the single-byte 'k'), which could otherwise
		// put len(oldPath) out of range of path.
		if (auto [suffix, ok] =
		        tspath::trimFilePathPrefix(path,
		                                   std::string(trimmedOldPath),
		                                   ucsf);
		    ok && (suffix.starts_with("/") ||
		           suffix.starts_with("\\"))) {
			return {newPath + suffix, true};
		}
		return {"", false};
	};
}

// updateTsconfigFiles — file_rename.go:112.
void LanguageService::updateTsconfigFiles(
    compiler::SimpleProgram* program, change::Tracker* changeTracker,
    pathUpdater oldToNew, const std::string& oldPath,
    const std::string& newPath) {
	auto* commandLine = program->CommandLine();
	if (commandLine == nullptr || commandLine->ConfigFile == nullptr) {
		return;
	}

	auto* configFile = commandLine->ConfigFile->SourceFile;
	if (configFile == nullptr) {
		return;
	}
	auto configDir = tspath::getDirectoryPath(configFile->FileName());
	auto* jsonObjectLiteral =
	    getTsConfigObjectLiteralExpression(configFile);
	if (jsonObjectLiteral == nullptr) {
		return;
	}

	forEachObjectProperty(
	    jsonObjectLiteral,
	    [&](PropertyAssignment* property,
	        const std::string& propertyName) {
		    if (propertyName == "files" ||
		        propertyName == "include" ||
		        propertyName == "exclude") {
			    bool foundExactMatch = updatePathsProperty(
			        configFile, configDir, property, changeTracker,
			        oldToNew, converters,
			        UseCaseSensitiveFileNames());
			    if (foundExactMatch || propertyName != "include" ||
			        !isArrayLiteralExpression(
			            property->Initializer)) {
				    return;
			    }
			    if (auto [oldSpec, isDefault] =
			            commandLine->GetMatchedIncludeSpec(oldPath);
			        oldSpec != "" && !isDefault) {
				    if (auto [newSpec, _] =
				            commandLine->GetMatchedIncludeSpec(
				                newPath);
				        newSpec == "") {
					    auto elements =
					        property->Initializer->elements();
					    if (elements.size() > 0) {
						    changeTracker->InsertNodeAfter(
						        configFile,
						        elements[elements.size() - 1],
						        changeTracker->nodeFactory
						            ->newStringLiteral(
						                relativePathFromDirectory(
						                    configDir,
						                    newPath,
						                    UseCaseSensitiveFileNames()),
						                TokenFlagsNone));
					    }
				    }
			    }
		    } else if (propertyName == "compilerOptions") {
			    if (!isObjectLiteralExpression(
			            property->Initializer)) {
				    return;
			    }
			    forEachObjectProperty(
			        property->Initializer
			            ->as<ObjectLiteralExpression>(),
			        [&](PropertyAssignment* property,
			            const std::string& propertyName) {
				        auto* option =
				            tsoptions::
				                CommandLineCompilerOptionsMap()
				                    .Get(propertyName);
				        if (option != nullptr) {
					        auto* elementOption =
					            option->Elements();
					        if (option->IsFilePath ||
					            (option->Kind ==
					                 tsoptions::
					                     CommandLineOptionTypeList &&
					             elementOption != nullptr &&
					             elementOption->IsFilePath)) {
						        updatePathsProperty(
						            configFile, configDir,
						            property, changeTracker,
						            oldToNew, converters,
						            UseCaseSensitiveFileNames());
						        return;
					        }
				        }

				        if (propertyName != "paths" ||
				            !isObjectLiteralExpression(
				                property->Initializer)) {
					        return;
				        }
				        forEachObjectProperty(
				            property->Initializer
				                ->as<ObjectLiteralExpression>(),
				            [&](PropertyAssignment* pathsProperty,
				                const std::string&) {
					            if (!isArrayLiteralExpression(
					                    pathsProperty
					                        ->Initializer)) {
						            return;
					            }
					            for (auto* element :
					                 pathsProperty->Initializer
					                     ->elements()) {
						            tryUpdateConfigString(
						                configFile, configDir,
						                element,
						                changeTracker, oldToNew,
						                converters,
						                UseCaseSensitiveFileNames());
					            }
				            });
			        });
		    }
	    });
}

// updatePathsProperty — file_rename.go:177.
bool updatePathsProperty(SourceFile* configFile,
                         const std::string& configDir,
                         PropertyAssignment* property,
                         change::Tracker* changeTracker,
                         pathUpdater oldToNew,
                         lsconv::Converters* converters,
                         bool useCaseSensitiveFileNames) {
	std::vector<Node*> elements{property->Initializer};
	if (isArrayLiteralExpression(property->Initializer)) {
		elements = property->Initializer->elements();
	}

	bool foundExactMatch = false;
	for (auto* element : elements) {
		foundExactMatch =
		    tryUpdateConfigString(configFile, configDir, element,
		                          changeTracker, oldToNew, converters,
		                          useCaseSensitiveFileNames) ||
		    foundExactMatch;
	}
	return foundExactMatch;
}

// tryUpdateConfigString — file_rename.go:190.
bool tryUpdateConfigString(SourceFile* configFile,
                           const std::string& configDir, Node* element,
                           change::Tracker* changeTracker,
                           pathUpdater oldToNew,
                           lsconv::Converters* converters,
                           bool useCaseSensitiveFileNames) {
	if (!isStringLiteral(element)) {
		return false;
	}

	auto elementFileName = tspath::normalizePath(
	    tspath::combinePaths(configDir, {element->text()}));
	auto [updated, ok] = oldToNew(elementFileName);
	if (!ok) {
		return false;
	}

	auto textRange =
	    TextRange{getTokenPosOfNode(element, configFile, false) + 1,
	              element->end() - 1};
	auto [lspRange, fidelity] =
	    converters->ToLSPRange(configFile, textRange);
	debug::assert(fidelity.IsExact(),
	              "config files are not content-mapped");
	changeTracker->ReplaceRangeWithText(
	    configFile, lspRange,
	    relativePathFromDirectory(configDir, updated,
	                              useCaseSensitiveFileNames));
	return true;
}

// updateRelativePath — file_rename.go:208.
std::string LanguageService::updateRelativePath(
    pathUpdater oldToNew, const std::string& oldImportFromPath,
    const std::string& newImportFromPath,
    const std::string& relativeSpecifier) {
	auto oldAbsolute = tspath::normalizePath(tspath::combinePaths(
	    tspath::getDirectoryPath(oldImportFromPath), {relativeSpecifier}));
	auto [newAbsolute, ok] = oldToNew(oldAbsolute);
	if (!ok) {
		newAbsolute = oldAbsolute;
	}
	return relativeImportPathFromDirectory(
	    tspath::getDirectoryPath(newImportFromPath), newAbsolute,
	    UseCaseSensitiveFileNames());
}

// updateImportsForFileRename — file_rename.go:217.
void LanguageService::updateImportsForFileRename(
    compiler::SimpleProgram* program, change::Tracker* changeTracker,
    pathUpdater oldToNew) {
	auto allFiles = program->GetSourceFiles();
	auto* ch = program->getChecker();
	auto moduleSpecifierPreferences =
	    UserPreferences().ModuleSpecifierPreferences();

	std::vector<movedFile> movedFiles;
	for (auto* sourceFile : allFiles) {
		if (auto [newFileName, ok] =
		        oldToNew(sourceFile->OriginalFileName());
		    ok) {
			movedFiles.push_back(
			    movedFile{sourceFile, newFileName});
		}
	}

	for (auto* sourceFile : allFiles) {
		auto oldFileName = sourceFile->OriginalFileName();
		auto [newFromOld, fileMoved] = oldToNew(oldFileName);
		auto newImportFromPath = oldFileName;
		if (fileMoved) {
			newImportFromPath = newFromOld;
		}

		for (auto* ref : sourceFile->ReferencedFiles) {
			if (!tspath::isExternalModuleNameRelative(ref->FileName)) {
				continue;
			}
			auto updated =
			    updateRelativePath(oldToNew, oldFileName,
			                       newImportFromPath, ref->FileName);
			if (updated != ref->FileName) {
				changeTracker->ReplaceTextRangeWithText(
				    sourceFile, *ref, updated);
			}
		}

		for (auto* importStringLiteral : sourceFile->Node::imports()) {
			auto updated = getUpdatedImportSpecifier(
			    program, ch, sourceFile, importStringLiteral,
			    oldToNew, movedFiles, newImportFromPath, fileMoved,
			    moduleSpecifierPreferences);
			if (updated != "" &&
			    updated != importStringLiteral->text()) {
				changeTracker->ReplaceTextRangeWithText(
				    sourceFile,
				    createStringTextRange(sourceFile,
				                          importStringLiteral),
				    updated);
			}
		}
	}
}

// getUpdatedImportSpecifier — file_rename.go:258. We assume the source file
// did not move to a different program.
std::string LanguageService::getUpdatedImportSpecifier(
    compiler::SimpleProgram* program, checker::Checker* ch,
    SourceFile* sourceFile, Node* importLiteral,
    pathUpdater oldToNew, const std::vector<movedFile>& movedFiles,
    const std::string& newImportFromPath, bool importingSourceFileMoved,
    const modulespecifiers::UserPreferences& userPreferences) {
	auto* importedModuleSymbol =
	    ch->getSymbolAtLocation(importLiteral, false);
	if (isAmbientModuleSymbol(importedModuleSymbol)) {
		return "";
	}

	auto* target = getSourceFileToImport(program, sourceFile,
	                                     importLiteral, oldToNew);

	if (target == nullptr) {
		// First fall back: try every file affected by the rename to see
		// if any of them would match the import specifier, and if so,
		// obtain the updated specifier for that file.
		if (auto updated =
		        getUpdatedImportSpecifierFromMovedSourceFiles(
		            program, sourceFile, importLiteral, movedFiles,
		            newImportFromPath, userPreferences);
		    updated != "" && updated != importLiteral->text()) {
			return updated;
		}
		// Fall back to a regular path update for unresolved module.
		if (tspath::isExternalModuleNameRelative(
		        importLiteral->text())) {
			return updateRelativePath(oldToNew,
			                          sourceFile->FileName(),
			                          newImportFromPath,
			                          importLiteral->text());
		}
		return "";
	}

	// Optimization: neither the importing or imported file changed.
	if (!target->updated &&
	    !(importingSourceFileMoved &&
	      tspath::isExternalModuleNameRelative(importLiteral->text()))) {
		return "";
	}

	auto updated = modulespecifiers::UpdateModuleSpecifier(
	    program->Options(), program, sourceFile, newImportFromPath,
	    importLiteral->text(), target->newFileName, userPreferences,
	    modulespecifiers::ModuleSpecifierOptions{
	        .OverrideImportMode = program->GetModeForUsageLocation(
	            sourceFile, importLiteral),
	    });
	return updated;
}

// getSourceFileToImport — file_rename.go:308.
toImport* getSourceFileToImport(compiler::SimpleProgram* program,
                                SourceFile* sourceFile,
                                Node* importLiteral,
                                pathUpdater oldToNew) {
	if (auto* resolved =
	        program->GetResolvedModuleFromModuleSpecifier(sourceFile,
	                                                      importLiteral);
	    resolved != nullptr && resolved->ResolvedFileName != "") {
		auto oldFileName = resolved->ResolvedFileName;
		if (auto [newFileName, ok] = oldToNew(oldFileName); ok) {
			return new toImport{.newFileName = newFileName,
			                    .updated = true};
		}
		return new toImport{.newFileName = oldFileName,
		                    .updated = false};
	}

	return nullptr;
}

// getUpdatedImportSpecifierFromMovedSourceFiles — file_rename.go:327. As a
// fall back for unresolved modules, we'll check every file affected by the
// rename to see if any of them would match the import specifier, and if so,
// we'll obtain the updated specifier for that file.
std::string getUpdatedImportSpecifierFromMovedSourceFiles(
    compiler::SimpleProgram* program, SourceFile* sourceFile,
    Node* importLiteral,
    const std::vector<movedFile>& movedFiles,
    const std::string& importingSourceFileName,
    const modulespecifiers::UserPreferences& userPreferences) {
	auto resolutionMode =
	    program->GetModeForUsageLocation(sourceFile, importLiteral);
	for (auto& candidate : movedFiles) {
		auto oldSpecifier = modulespecifiers::UpdateModuleSpecifier(
		    program->Options(), program, sourceFile,
		    importingSourceFileName, importLiteral->text(),
		    candidate.sourceFile->FileName(), userPreferences,
		    modulespecifiers::ModuleSpecifierOptions{
		        .OverrideImportMode = resolutionMode,
		    });
		if (oldSpecifier != importLiteral->text()) {
			continue;
		}

		return modulespecifiers::UpdateModuleSpecifier(
		    program->Options(), program, sourceFile,
		    importingSourceFileName, importLiteral->text(),
		    candidate.newFileName, userPreferences,
		    modulespecifiers::ModuleSpecifierOptions{
		        .OverrideImportMode = resolutionMode,
		    });
	}
	return "";
}

// createStringTextRange — file_rename.go:362.
TextRange createStringTextRange(SourceFile* sourceFile, Node* node) {
	return TextRange{getTokenPosOfNode(node, sourceFile, false) + 1,
	                 node->end() - 1};
}

// getTsConfigObjectLiteralExpression — file_rename.go:366.
ObjectLiteralExpression* getTsConfigObjectLiteralExpression(
    SourceFile* tsConfigSourceFile) {
	if (tsConfigSourceFile != nullptr &&
	    tsConfigSourceFile->Statements != nullptr &&
	    tsConfigSourceFile->Statements->nodes.size() > 0) {
		auto* expression =
		    tsConfigSourceFile->Statements->nodes[0]->expression();
		if (isObjectLiteralExpression(expression)) {
			return expression->as<ObjectLiteralExpression>();
		}
	}
	return nullptr;
}

// forEachObjectProperty — file_rename.go:376.
void forEachObjectProperty(
    ObjectLiteralExpression* objectLiteral,
    const std::function<void(PropertyAssignment*, const std::string&)>&
        cb) {
	if (objectLiteral == nullptr) {
		return;
	}
	for (auto* property : objectLiteral->Properties->nodes) {
		if (!isPropertyAssignment(property)) {
			continue;
		}
		if (std::string name;
		    tryGetTextOfPropertyName(property->name(), name)) {
			cb(property->as<PropertyAssignment>(), name);
		}
	}
}

// relativePathFromDirectory — file_rename.go:390.
std::string relativePathFromDirectory(const std::string& fromDirectory,
                                      const std::string& to,
                                      bool useCaseSensitiveFileNames) {
	return tspath::getRelativePathFromDirectory(
	    fromDirectory, to,
	    tspath::ComparePathsOptions{
	        .useCaseSensitiveFileNames = useCaseSensitiveFileNames});
}

// relativeImportPathFromDirectory — file_rename.go:394.
std::string relativeImportPathFromDirectory(
    const std::string& fromDirectory, const std::string& to,
    bool useCaseSensitiveFileNames) {
	return tspath::ensurePathIsNonModuleName(relativePathFromDirectory(
	    fromDirectory, to, useCaseSensitiveFileNames));
}

// isAmbientModuleSymbol — file_rename.go:398.
bool isAmbientModuleSymbol(Symbol* symbol) {
	if (symbol == nullptr) {
		return false;
	}
	return std::find_if(symbol->data->declarations.begin(),
	                    symbol->data->declarations.end(),
	                    [](Node* d) {
		                    return isModuleWithStringLiteralName(d);
	                    }) != symbol->data->declarations.end();
}

} // namespace tsc::ls
