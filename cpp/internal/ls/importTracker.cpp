// importTracker.go — tracks imports/re-exports of a module's symbols across
// the program's source files for find-all-references and rename.
#include "internal/ls/ls.h"

#include "internal/ast/ast.h"
#include "internal/ast/nodes_generated.h"
#include "internal/checker/checker.h"
#include "internal/compiler/program.h"
#include "internal/debug/debug.h"
#include "internal/module/types.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <unordered_map>

namespace tsc::ls {

namespace {

// --- local stand-ins for helpers with no C++ equivalent (PORTING.md) ---

// core.Some — true when pred(x) holds for any element.
template <typename T, typename F>
bool some(const std::vector<T>& v, F&& pred) {
	for (auto& x : v) {
		if (pred(x)) {
			return true;
		}
	}
	return false;
}

// slices.Contains
template <typename T>
bool contains(const std::vector<T>& v, const T& x) {
	return std::find(v.begin(), v.end(), x) != v.end();
}

// ast.ImportFromModuleSpecifier — utilities.go:4195.
Node* importFromModuleSpecifier(Node* node) {
	if (auto* result = tryGetImportFromModuleSpecifier(node); result != nullptr) {
		return result;
	}
	debug::failBadSyntaxKind(node->parent);
}

// ast.WalkUpBindingElementsAndPatterns — utilities.go:1286.
Node* walkUpBindingElementsAndPatterns(Node* binding) {
	auto* node = binding->parent;
	while (isBindingElement(node->parent)) {
		node = node->parent->parent;
	}
	return node->parent;
}

// ast.IsDefaultImport — utilities.go:2592.
bool isDefaultImport(Node* node) {
	switch (node->kind) {
	case Kind::ImportDeclaration:
	case Kind::JSImportDeclaration: {
		auto* importClause = node->importClause();
		return importClause != nullptr &&
		       importClause->as<ImportClause>()->name != nullptr;
	}
	}
	return false;
}

// program.getModeForTypeReferenceDirectiveInFile —
// program.go:2188 (compiler slice helper, ported locally).
ResolutionMode getModeForTypeReferenceDirectiveInFile(
    FileReference* ref, SourceFile* sourceFile,
    compiler::SimpleProgram* program) {
	if (ref->ResolutionMode != ResolutionMode::None) {
		return ref->ResolutionMode;
	}
	return program->GetDefaultResolutionModeForFile(sourceFile);
}

// program.GetResolvedTypeReferenceDirectiveFromTypeReferenceDirective —
// program.go:2171.
module::ResolvedTypeReferenceDirective*
getResolvedTypeReferenceDirectiveFromTypeReferenceDirective(
    FileReference* typeRef, SourceFile* sourceFile,
    compiler::SimpleProgram* program) {
	return program->GetResolvedTypeReferenceDirective(
	    sourceFile, typeRef->FileName,
	    getModeForTypeReferenceDirectiveInFile(typeRef, sourceFile, program));
}

} // namespace

// Forward declarations — Go package-level functions defined later in this
// file but referenced earlier.
std::unordered_map<Symbol*, std::vector<Node*>> getDirectImportsMap(
    const gostd::Context& ctx, compiler::SimpleProgram* program,
    const std::vector<SourceFile*>& sourceFiles, checker::Checker* ch);
void forEachImport(compiler::SimpleProgram* program, SourceFile* sourceFile,
                   const std::function<void(Node*, Node*)>& action);
bool forEachPossibleImportOrExportStatement(
    Node* sourceFileLike, const std::function<bool(Node*)>& action);
Node* getSourceFileLikeForImportDeclaration(Node* node);
bool isAmbientModuleDeclaration(Node* node);
std::vector<Node*> getStatementsOfSourceFileLike(Node* node);
std::pair<std::vector<Node*>, std::vector<SourceFile*>> getImportersForExport(
    const std::vector<SourceFile*>& sourceFiles,
    collections::Set<std::string>* sourceFilesSet,
    const std::unordered_map<Symbol*, std::vector<Node*>>& allDirectImports,
    ExportInfo* exportInfo, checker::Checker* ch);
Symbol* getContainingModuleSymbol(Node* importer, checker::Checker* ch);
bool findNamespaceReExports(Node* sourceFileLike, Node* name,
                            checker::Checker* ch);
std::pair<std::vector<LocationAndSymbol>, std::vector<Node*>>
getSearchesFromDirectImports(const std::vector<Node*>& directImports,
                             Symbol* exportSymbol, ExportKind exportKind,
                             checker::Checker* ch, bool isForRename);
Node* getExportNode(Node* parent, Node* node);
bool isNodeImport(Node* node);
bool isExternalModuleImportEquals(Node* node);
Symbol* skipExportSpecifierSymbol(Symbol* symbol, checker::Checker* ch);
Symbol* getExportEqualsLocalSymbol(Symbol* importedSymbol,
                                   checker::Checker* ch);
std::string symbolNameNoDefault(Symbol* symbol);

// createImportTracker — importTracker.go:74. Creates the imports map and
// returns an ImportTracker that uses it. Call this lazily to avoid calling
// `getDirectImportsMap` unnecessarily.
ImportTracker createImportTracker(
    const gostd::Context& ctx, compiler::SimpleProgram* program,
    const std::vector<SourceFile*>& sourceFiles,
    collections::Set<std::string>* sourceFilesSet,
    checker::Checker* ch) {
	auto allDirectImports =
	    std::make_shared<std::unordered_map<Symbol*, std::vector<Node*>>>(
	        getDirectImportsMap(ctx, program, sourceFiles, ch));
	return [sourceFiles, sourceFilesSet, allDirectImports,
	        ch](Symbol* exportSymbol, ExportInfo* exportInfo,
	            bool isForRename) -> ImportsResult* {
		auto [directImports, indirectUsers] =
		    getImportersForExport(sourceFiles, sourceFilesSet,
		                          *allDirectImports, exportInfo, ch);
		auto [importSearches, singleReferences] =
		    getSearchesFromDirectImports(directImports, exportSymbol,
		                                 exportInfo->exportKind, ch,
		                                 isForRename);
		return new ImportsResult{importSearches, singleReferences,
		                       indirectUsers};
	};
}

// getDirectImportsMap — importTracker.go:84. Returns a map from a module
// symbol to all import statements that directly reference the module.
std::unordered_map<Symbol*, std::vector<Node*>> getDirectImportsMap(
    const gostd::Context& ctx, compiler::SimpleProgram* program,
    const std::vector<SourceFile*>& sourceFiles, checker::Checker* ch) {
	std::unordered_map<Symbol*, std::vector<Node*>> result;
	for (auto* sourceFile : sourceFiles) {
		if (gostd::ctxErr(ctx) != nullptr) {
			return result;
		}
		forEachImport(program, sourceFile,
		              [&](Node* importDecl, Node* moduleSpecifier) {
			              if (auto* moduleSymbol =
			                      ch->getSymbolAtLocation(moduleSpecifier, false);
			                  moduleSymbol != nullptr) {
				              result[moduleSymbol].push_back(importDecl);
			              }
		              });
	}
	return result;
}

// forEachImport — importTracker.go:100. Calls `action` for each import,
// re-export, or require() in a file.
void forEachImport(compiler::SimpleProgram* program, SourceFile* sourceFile,
                   const std::function<void(Node*, Node*)>& action) {
	std::vector<Node*> implicitImports; // []*ast.LiteralLikeNode
	auto [_, jsxSpecifier] =
	    program->GetJSXRuntimeImportSpecifier(sourceFile->Path());
	if (jsxSpecifier != nullptr) {
		implicitImports.push_back(jsxSpecifier);
	}
	auto* importHelpersSpecifier =
	    program->GetImportHelpersImportSpecifier(sourceFile->Path());
	if (importHelpersSpecifier != nullptr) {
		implicitImports.push_back(importHelpersSpecifier);
	}
	if (sourceFile->ExternalModuleIndicator != nullptr ||
	    sourceFile->Node::imports().size() + implicitImports.size() != 0) {
		for (auto* i : sourceFile->Node::imports()) {
			action(importFromModuleSpecifier(i), i);
		}
		for (auto* i : implicitImports) {
			action(importFromModuleSpecifier(i), i);
		}
	} else {
		forEachPossibleImportOrExportStatement(
		    sourceFile->asNode(), [&](Node* node) -> bool {
			    switch (node->kind) {
			    case Kind::ExportDeclaration:
			    case Kind::ImportDeclaration:
			    case Kind::JSImportDeclaration:
				    if (auto* specifier = node->moduleSpecifier();
				        specifier != nullptr &&
				        isStringLiteral(specifier)) {
					    action(node, specifier);
				    }
				    break;
			    case Kind::ImportEqualsDeclaration:
				    if (isExternalModuleImportEquals(node)) {
					    action(node, node->as<ImportEqualsDeclaration>()
					                    ->ModuleReference->expression());
				    }
				    break;
			    }
			    return false;
		    });
	}
}

// forEachPossibleImportOrExportStatement — importTracker.go:134.
bool forEachPossibleImportOrExportStatement(
    Node* sourceFileLike,
    const std::function<bool(Node*)>& action) {
	for (auto* statement : getStatementsOfSourceFileLike(sourceFileLike)) {
		if (action(statement) ||
		    (isAmbientModuleDeclaration(statement) &&
		     forEachPossibleImportOrExportStatement(statement, action))) {
			return true;
		}
	}
	return false;
}

// getSourceFileLikeForImportDeclaration — importTracker.go:143.
Node* getSourceFileLikeForImportDeclaration(Node* node) {
	if (isCallExpression(node) || isJSDocImportTag(node)) {
		return getSourceFileOfNode(node)->asNode();
	}
	auto* parent = node->parent;
	if (isSourceFile(parent)) {
		return parent;
	}
	debug::assert(isModuleBlock(parent) &&
	              isAmbientModuleDeclaration(parent->parent));
	return parent->parent;
}

// isAmbientModuleDeclaration — importTracker.go:155.
bool isAmbientModuleDeclaration(Node* node) {
	return isModuleDeclaration(node) && isStringLiteral(node->name());
}

// getStatementsOfSourceFileLike — importTracker.go:159.
std::vector<Node*> getStatementsOfSourceFileLike(Node* node) {
	if (isSourceFile(node)) {
		return node->statements();
	}
	if (auto* body = node->body(); body != nullptr) {
		return body->statements();
	}
	return {};
}

// getImportersForExport — importTracker.go:169.
std::pair<std::vector<Node*>, std::vector<SourceFile*>> getImportersForExport(
    const std::vector<SourceFile*>& sourceFiles,
    collections::Set<std::string>* sourceFilesSet,
    const std::unordered_map<Symbol*, std::vector<Node*>>& allDirectImports,
    ExportInfo* exportInfo, checker::Checker* ch) {
	std::vector<Node*> directImports;
	std::vector<Node*> indirectUserDeclarations;
	auto markSeenDirectImport = nodeSeenTracker();
	auto markSeenIndirectUser = nodeSeenTracker();
	bool isAvailableThroughGlobal = isSourceFileWithGlobalExports(
	    exportInfo->exportingModuleSymbol->valueDeclaration);

	auto getDirectImports = [&](Symbol* moduleSymbol) -> std::vector<Node*> {
		auto it = allDirectImports.find(moduleSymbol);
		return it != allDirectImports.end() ? it->second
		                                    : std::vector<Node*>{};
	};

	// Adds a module and all of its transitive dependencies as possible
	// indirect users.
	std::function<void(Node*, bool)> addIndirectUser =
	    [&](Node* sourceFileLike, bool addTransitiveDependencies) {
		    // When isAvailableThroughGlobal, getIndirectUsers already returns
		    // all source files, so indirectUserDeclarations is never
		    // consulted. Nothing to do here.
		    if (isAvailableThroughGlobal) {
			    return;
		    }
		    if (!markSeenIndirectUser(sourceFileLike)) {
			    return;
		    }
		    indirectUserDeclarations.push_back(sourceFileLike);
		    if (!addTransitiveDependencies) {
			    return;
		    }
		    auto* moduleSymbol =
		        ch->getMergedSymbol(sourceFileLike->symbol());
		    if (moduleSymbol == nullptr) {
			    return;
		    }
		    debug::assert(moduleSymbol->flags & SymbolFlagsModule);
		    for (auto* directImport : getDirectImports(moduleSymbol)) {
			    if (!isImportTypeNode(directImport)) {
				    addIndirectUser(
				        getSourceFileLikeForImportDeclaration(
				            directImport),
				        true /*addTransitiveDependencies*/);
			    }
		    }
	    };

	auto isExported = [](Node* node, bool stopAtAmbientModule) {
		while (node != nullptr &&
		       !(stopAtAmbientModule &&
		         isAmbientModuleDeclaration(node))) {
			if (hasSyntacticModifier(node, ModifierFlagsExport)) {
				return true;
			}
			node = node->parent;
		}
		return false;
	};

	auto handleImportCall = [&](Node* importCall) {
		auto* top = findAncestor(importCall, [](Node* n) {
			return isAmbientModuleDeclaration(n);
		});
		if (top == nullptr) {
			top = getSourceFileOfNode(importCall)->asNode();
		}
		addIndirectUser(top, isExported(importCall,
		                                true /*stopAtAmbientModule*/));
	};

	auto handleNamespaceImport = [&](Node* importDeclaration, Node* name,
	                                 bool isReExport,
	                                 bool alreadyAddedDirect) {
		if (exportInfo->exportKind == ExportKindExportEquals) {
			// This is a direct import, not import-as-namespace.
			if (!alreadyAddedDirect) {
				directImports.push_back(importDeclaration);
			}
		} else if (!isAvailableThroughGlobal) {
			auto* sourceFileLike =
			    getSourceFileLikeForImportDeclaration(importDeclaration);
			debug::assert(isSourceFile(sourceFileLike) ||
			              isModuleDeclaration(sourceFileLike));
			addIndirectUser(
			    sourceFileLike,
			    isReExport ||
			        findNamespaceReExports(sourceFileLike, name, ch));
		}
	};

	std::function<void(Symbol*)> handleDirectImports =
	    [&](Symbol* exportingModuleSymbol) {
		    auto theseDirectImports = getDirectImports(exportingModuleSymbol);
		    for (auto* direct : theseDirectImports) {
			    if (!markSeenDirectImport(direct)) {
				    continue;
			    }
			    // !!! cancellation
			    switch (direct->kind) {
			    case Kind::CallExpression:
				    if (isImportCall(direct)) {
					    handleImportCall(direct);
				    } else if (!isAvailableThroughGlobal) {
					    auto* parent = direct->parent;
					    if (exportInfo->exportKind ==
					            ExportKindExportEquals &&
					        isVariableDeclaration(parent)) {
						    auto* name = parent->name();
						    if (isIdentifier(name)) {
							    directImports.push_back(name);
						    }
					    }
				    }
				    break;
			    case Kind::Identifier:
				    // Nothing
				    break;
			    case Kind::ImportEqualsDeclaration:
				    handleNamespaceImport(
				        direct, direct->name(),
				        hasSyntacticModifier(direct,
				                             ModifierFlagsExport),
				        false /*alreadyAddedDirect*/);
				    break;
			    case Kind::ImportDeclaration:
			    case Kind::JSImportDeclaration:
			    case Kind::JSDocImportTag:
				    directImports.push_back(direct);
				    if (auto* importClause = direct->importClause();
				        importClause != nullptr) {
					    if (auto* namedBindings =
					            importClause->as<ImportClause>()
					                ->NamedBindings;
					        namedBindings != nullptr &&
					        isNamespaceImport(namedBindings)) {
						    handleNamespaceImport(
						        direct, namedBindings->name(),
						        false /*isReExport*/,
						        true /*alreadyAddedDirect*/);
						    break;
					    }
				    }
				    if (!isAvailableThroughGlobal &&
				        isDefaultImport(direct)) {
					    addIndirectUser(
					        getSourceFileLikeForImportDeclaration(
					            direct),
					        false);
					    // Add a check for indirect uses to handle
					    // synthetic default imports
				    }
				    break;
			    case Kind::ExportDeclaration: {
				    auto* exportClause =
				        direct->as<ExportDeclaration>()->ExportClause;
				    if (exportClause == nullptr) {
					    // This is `export * from "foo"`, so imports of this
					    // module may import the export too.
					    handleDirectImports(
					        getContainingModuleSymbol(direct, ch));
				    } else if (isNamespaceExport(exportClause)) {
					    // `export * as foo from "foo"` add to indirect uses
					    addIndirectUser(
					        getSourceFileLikeForImportDeclaration(
					            direct),
					        true /*addTransitiveDependencies*/);
				    } else {
					    // This is `export { foo } from "foo"` and creates
					    // an alias symbol, so recursive search will get
					    // handle re-exports.
					    directImports.push_back(direct);
				    }
				    break;
			    }
			    case Kind::ImportType:
				    // Only check for typeof import('xyz')
				    if (!isAvailableThroughGlobal &&
				        direct->as<ImportTypeNode>()->IsTypeOf &&
				        direct->as<ImportTypeNode>()->Qualifier ==
				            nullptr &&
				        isExported(direct, false)) {
					    addIndirectUser(
					        getSourceFileOfNode(direct)->asNode(),
					        true /*addTransitiveDependencies*/);
				    }
				    directImports.push_back(direct);
				    break;
			    default:
				    debug::failBadSyntaxKind(direct,
				                             "Unexpected import kind.");
			    }
		    }
	    };

	auto getIndirectUsers = [&]() -> std::vector<SourceFile*> {
		if (isAvailableThroughGlobal) {
			// It has `export as namespace`, so anything could
			// potentially use it.
			return sourceFiles;
		}
		// Module augmentations may use this module's exports without
		// importing it.
		for (auto* decl : exportInfo->exportingModuleSymbol->declarations) {
			if (isExternalModuleAugmentation(decl) &&
			    sourceFilesSet->Has(
			        getSourceFileOfNode(decl)->FileName())) {
				addIndirectUser(decl, false);
			}
		}
		// This may return duplicates (if there are multiple module
		// declarations in a single source file, all importing the same
		// thing as a namespace), but `State.markSearchedSymbol` will
		// handle that.
		std::vector<SourceFile*> result;
		result.reserve(indirectUserDeclarations.size());
		for (auto* decl : indirectUserDeclarations) {
			result.push_back(getSourceFileOfNode(decl));
		}
		return result;
	};

	handleDirectImports(exportInfo->exportingModuleSymbol);
	return {directImports, getIndirectUsers()};
}

// getContainingModuleSymbol — importTracker.go:324.
Symbol* getContainingModuleSymbol(Node* importer, checker::Checker* ch) {
	return ch->getMergedSymbol(
	    getSourceFileLikeForImportDeclaration(importer)->symbol());
}

// findNamespaceReExports — importTracker.go:329. Returns 'true' if the
// namespace 'name' is re-exported from this module, and 'false' if it is only
// used locally.
bool findNamespaceReExports(Node* sourceFileLike, Node* name,
                            checker::Checker* ch) {
	auto* namespaceImportSymbol = ch->getSymbolAtLocation(name, false);
	return forEachPossibleImportOrExportStatement(
	    sourceFileLike, [&](Node* statement) -> bool {
		    if (!isExportDeclaration(statement)) {
			    return false;
		    }
		    auto* exportClause =
		        statement->as<ExportDeclaration>()->ExportClause;
		    auto* moduleSpecifier = statement->moduleSpecifier();
		    return moduleSpecifier == nullptr &&
		           exportClause != nullptr &&
		           isNamedExports(exportClause) &&
		           some(exportClause->elements(), [&](Node* element) {
			           return ch->GetExportSpecifierLocalTargetSymbol(
			                      element) == namespaceImportSymbol;
		           });
	    });
}

// getSearchesFromDirectImports — importTracker.go:343.
std::pair<std::vector<LocationAndSymbol>, std::vector<Node*>>
getSearchesFromDirectImports(const std::vector<Node*>& directImports,
                             Symbol* exportSymbol, ExportKind exportKind,
                             checker::Checker* ch, bool isForRename) {
	std::vector<LocationAndSymbol> importSearches;
	std::vector<Node*> singleReferences;

	auto addSearch = [&](Node* location, Symbol* symbol) {
		importSearches.push_back(LocationAndSymbol{location, symbol});
	};

	auto isNameMatch = [&](const std::string& name) {
		// Use name of "default" even in `export =` case because we may
		// have allowSyntheticDefaultImports
		return name == exportSymbol->name ||
		       (exportKind != ExportKindNamed &&
		        name == InternalSymbolNameDefault);
	};

	// `import x = require("./x")` or `import * as x from "./x"`.
	// An `export =` may be imported by this syntax, so it may be a
	// direct import.
	// If it's not a direct import, it will be in `indirectUsers`, so
	// we don't have to do anything here.
	auto handleNamespaceImportLike = [&](Node* importName) {
		// Don't rename an import that already has a different name
		// than the export.
		if (exportKind == ExportKindExportEquals &&
		    (!isForRename || isNameMatch(importName->text()))) {
			addSearch(importName,
			          ch->getSymbolAtLocation(importName, false));
		}
	};

	auto searchForNamedImport = [&](Node* namedBindings) {
		if (namedBindings == nullptr) {
			return;
		}
		for (auto* element : namedBindings->elements()) {
			auto* name = element->name();
			auto* propertyName = element->propertyName();
			if (!isNameMatch((propertyName != nullptr ? propertyName
			                                          : name)
			                     ->text())) {
				continue;
			}
			if (propertyName != nullptr) {
				// This is `import { foo as bar } from "./a"` or
				// `export { foo as bar } from "./a"`. `foo` isn't a
				// local in the file, so just add it as a single
				// reference.
				singleReferences.push_back(propertyName);
				// If renaming `{ foo as bar }`, don't touch `bar`,
				// just `foo`.
				// But do rename `foo` in ` { default as foo }` if
				// that's the original export name.
				if (!isForRename ||
				    name->text() == exportSymbol->name) {
					// Search locally for `bar`.
					addSearch(name, ch->getSymbolAtLocation(name,
					                                        false));
				}
			} else {
				Symbol* localSymbol = nullptr;
				if (isExportSpecifier(element) &&
				    element->propertyName() != nullptr) {
					localSymbol =
					    ch->GetExportSpecifierLocalTargetSymbol(
					        element);
				} else {
					localSymbol =
					    ch->getSymbolAtLocation(name, false);
				}
				addSearch(name, localSymbol);
			}
		}
	};

	auto handleImport = [&](Node* decl) {
		if (isImportEqualsDeclaration(decl)) {
			if (isExternalModuleImportEquals(decl)) {
				handleNamespaceImportLike(decl->name());
			}
			return;
		}
		if (isIdentifier(decl)) {
			handleNamespaceImportLike(decl);
			return;
		}
		if (isImportTypeNode(decl)) {
			if (auto* qualifier = decl->as<ImportTypeNode>()->Qualifier;
			    qualifier != nullptr) {
				auto* firstIdentifier = getFirstIdentifier(qualifier);
				if (firstIdentifier->text() == symbolName(exportSymbol)) {
					singleReferences.push_back(firstIdentifier);
				}
			} else if (exportKind == ExportKindExportEquals) {
				singleReferences.push_back(
				    decl->as<ImportTypeNode>()
				        ->Argument->as<LiteralTypeNode>()
				        ->Literal);
			}
			return;
		}
		// Ignore if there's a grammar error
		if (!isStringLiteral(decl->moduleSpecifier())) {
			return;
		}
		if (isExportDeclaration(decl)) {
			if (auto* exportClause =
			        decl->as<ExportDeclaration>()->ExportClause;
			    exportClause != nullptr &&
			    isNamedExports(exportClause)) {
				searchForNamedImport(exportClause);
			}
			return;
		}
		if (auto* importClause = decl->importClause();
		    importClause != nullptr) {
			if (auto* namedBindings =
			        importClause->as<ImportClause>()->NamedBindings;
			    namedBindings != nullptr) {
				switch (namedBindings->kind) {
				case Kind::NamespaceImport:
					handleNamespaceImportLike(
					    namedBindings->name());
					break;
				case Kind::NamedImports:
					// 'default' might be accessed as a named
					// import `{ default as foo }`.
					if (exportKind == ExportKindNamed ||
					    exportKind == ExportKindDefault) {
						searchForNamedImport(namedBindings);
					}
					break;
				}
			}
			// `export =` might be imported by a default import if
			// `--allowSyntheticDefaultImports` is on, so this handles
			// both ExportKind.Default and ExportKind.ExportEquals.
			// If a default import has the same name as the default
			// export, allow to rename it.
			// Given `import f` and `export default function f`, we
			// will rename both, but for `import g` we will rename
			// just that.
			if (auto* name = importClause->name();
			    name != nullptr &&
			    (exportKind == ExportKindDefault ||
			     exportKind == ExportKindExportEquals) &&
			    (!isForRename ||
			     name->text() == symbolNameNoDefault(exportSymbol))) {
				auto* defaultImportAlias =
				    ch->getSymbolAtLocation(name, false);
				addSearch(name, defaultImportAlias);
			}
		}
	};
	for (auto* decl : directImports) {
		handleImport(decl);
	}
	return {importSearches, singleReferences};
}

// getImportOrExportSymbol — importTracker.go:462.
ImportExportSymbol* getImportOrExportSymbol(Node* node, Symbol* symbol,
                                            checker::Checker* ch,
                                            bool comingFromExport) {
	auto exportInfo = [&](Symbol* symbol,
	                      ExportKind kind) -> ImportExportSymbol* {
		if (auto* exportInfo = getExportInfo(symbol, kind, ch);
		    exportInfo != nullptr) {
			return new ImportExportSymbol{
			    .kind = ImpExpKindExport,
			    .symbol = symbol,
			    .exportInfo = exportInfo,
			};
		}
		return nullptr;
	};

	auto getExport = [&]() -> ImportExportSymbol* {
		auto getExportAssignmentExport =
		    [&](Node* ex) -> ImportExportSymbol* {
			// Get the symbol for the `export =` node; its parent is
			// the module it's the export of.
			if (ex->symbol()->parent == nullptr) {
				return nullptr;
			}
			auto exportKind =
			    ex->as<ExportAssignment>()->IsExportEquals
			        ? ExportKindExportEquals
			        : ExportKindDefault;
			return new ImportExportSymbol{
			    .kind = ImpExpKindExport,
			    .symbol = symbol,
			    .exportInfo = new ExportInfo{
			        .exportingModuleSymbol = ex->symbol()->parent,
			        .exportKind = exportKind,
			    },
			};
		};

		// Not meant for use with export specifiers or export assignment.
		auto getExportKindForDeclaration = [](Node* node) {
			return hasSyntacticModifier(node, ModifierFlagsDefault)
			           ? ExportKindDefault
			           : ExportKindNamed;
		};

		auto getSpecialPropertyExport =
		    [&](Node* node, bool useLhsSymbol) -> ImportExportSymbol* {
			ExportKind kind;
			switch (getAssignmentDeclarationKind(node)) {
			case JSDeclarationKind::ExportsProperty:
				kind = ExportKindNamed;
				break;
			case JSDeclarationKind::ModuleExports:
				kind = ExportKindExportEquals;
				break;
			default:
				return nullptr;
			}
			auto* sym = symbol;
			if (useLhsSymbol) {
				sym = node->symbol();
			}
			if (sym == nullptr) {
				return nullptr;
			}
			return exportInfo(sym, kind);
		};

		auto* parent = node->parent;
		auto* grandparent = parent->parent;
		if (symbol->exportSymbol != nullptr) {
			if (isPropertyAccessExpression(parent)) {
				// When accessing an export of a JS module, there's
				// no alias. The symbol will still be flagged as an
				// export even though we're at the use.
				// So check that we are at the declaration.
				if (isBinaryExpression(grandparent) &&
				    contains(symbol->declarations, parent)) {
					return getSpecialPropertyExport(
					    grandparent, false /*useLhsSymbol*/);
				}
				return nullptr;
			}
			return exportInfo(symbol->exportSymbol,
			                  getExportKindForDeclaration(parent));
		} else {
			auto* exportNode = getExportNode(parent, node);
			if (exportNode != nullptr &&
			    (hasSyntacticModifier(exportNode,
			                          ModifierFlagsExport) ||
			     isImplicitlyExportedJSDocDeclaration(exportNode))) {
				if (isImportEqualsDeclaration(exportNode) &&
				    exportNode->as<ImportEqualsDeclaration>()
				            ->ModuleReference == node) {
					// We're at `Y` in `export import X = Y`. This
					// is not the exported symbol, the
					// left-hand-side is. So treat this as an
					// import statement.
					if (comingFromExport) {
						return nullptr;
					}
					auto* lhsSymbol = ch->getSymbolAtLocation(
					    exportNode->name(), false);
					return new ImportExportSymbol{
					    .kind = ImpExpKindImport,
					    .symbol = lhsSymbol,
					};
				}
				return exportInfo(
				    symbol, getExportKindForDeclaration(exportNode));
			} else if (isNamespaceExport(parent)) {
				return exportInfo(symbol, ExportKindNamed);
			} else if (isExportAssignment(parent)) {
				return getExportAssignmentExport(parent);
			} else if (isExportAssignment(grandparent)) {
				return getExportAssignmentExport(grandparent);
			} else if (isBinaryExpression(parent)) {
				return getSpecialPropertyExport(
				    parent, true /*useLhsSymbol*/);
			} else if (isBinaryExpression(grandparent)) {
				return getSpecialPropertyExport(
				    grandparent, true /*useLhsSymbol*/);
			} else if (isJSDocTypedefTag(parent) ||
			           isJSDocCallbackTag(parent)) {
				return exportInfo(symbol, ExportKindNamed);
			}
		}
		return nullptr;
	};

	auto getImport = [&]() -> ImportExportSymbol* {
		if (!isNodeImport(node)) {
			return nullptr;
		}
		// JS destructuring from `require(...)` is import-like for
		// references, but the binding element
		// itself is still a local variable symbol rather than an
		// alias.
		Symbol* importedSymbol;
		if (symbol->flags & SymbolFlagsAlias) {
			importedSymbol = ch->GetImmediateAliasedSymbol(symbol);
		} else {
			importedSymbol =
			    getPropertySymbolOfObjectBindingPatternWithoutPropertyName(
			        symbol, ch);
		}
		if (importedSymbol == nullptr) {
			return nullptr;
		}
		// Search on the local symbol in the exporting module, not the
		// exported symbol.
		importedSymbol = skipExportSpecifierSymbol(importedSymbol, ch);
		if (importedSymbol == nullptr) {
			return nullptr;
		}
		// Similarly, skip past the symbol for 'export ='
		if (importedSymbol->name == "export=") {
			importedSymbol =
			    getExportEqualsLocalSymbol(importedSymbol, ch);
			if (importedSymbol == nullptr) {
				return nullptr;
			}
		}
		// If the import has a different name than the export, do not
		// continue searching.
		// If `importedName` is undefined, do continue searching as
		// the export is anonymous.
		// (All imports returned from this function will be ignored
		// anyway if we are in rename and this is a not a named
		// export.)
		auto importedName = symbolNameNoDefault(importedSymbol);
		if (importedName == "" ||
		    importedName == InternalSymbolNameDefault ||
		    importedName == symbol->name) {
			return new ImportExportSymbol{
			    .kind = ImpExpKindImport,
			    .symbol = importedSymbol,
			};
		}
		return nullptr;
	};

	auto* result = getExport();
	if (result == nullptr && !comingFromExport) {
		result = getImport();
	}
	return result;
}

// getExportInfo — importTracker.go:611.
ExportInfo* getExportInfo(Symbol* exportSymbol, ExportKind exportKind,
                          checker::Checker* ch) {
	// Parent can be nil if an `export` is not at the top-level (which is
	// a compile error).
	if (exportSymbol->parent != nullptr) {
		auto* exportingModuleSymbol =
		    ch->getMergedSymbol(exportSymbol->parent);
		// `export` may appear in a namespace. In that case, just rely
		// on global search.
		if (checker::isExternalModuleSymbol(exportingModuleSymbol)) {
			return new ExportInfo{
			    .exportingModuleSymbol = exportingModuleSymbol,
			    .exportKind = exportKind,
			};
		}
	}
	return nullptr;
}

// getExportNode — importTracker.go:628. If a reference is a class
// expression, the exported node would be its parent. If a reference is a
// variable declaration, the exported node would be the variable statement.
Node* getExportNode(Node* parent, Node* node) {
	Node* declaration = nullptr;
	if (isVariableDeclaration(parent)) {
		declaration = parent;
	} else if (isBindingElement(parent)) {
		declaration = walkUpBindingElementsAndPatterns(parent);
	}
	if (declaration != nullptr) {
		if (parent->name() == node &&
		    !isCatchClause(declaration->parent) &&
		    isVariableStatement(declaration->parent->parent)) {
			return declaration->parent->parent;
		}
		return nullptr;
	}
	return parent;
}

// isNodeImport — importTracker.go:645.
bool isNodeImport(Node* node) {
	auto* parent = node->parent;
	switch (parent->kind) {
	case Kind::ImportEqualsDeclaration:
		return parent->name() == node &&
		       isExternalModuleImportEquals(parent);
	case Kind::ImportSpecifier:
		// For a rename import `{ foo as bar }`, don't search for the
		// imported symbol. Just find local uses of `bar`.
		return parent->propertyName() == nullptr;
	case Kind::ImportClause:
	case Kind::NamespaceImport:
		debug::assert(parent->name() == node);
		return true;
	case Kind::BindingElement:
		return isInJSFile(node) &&
		       isVariableDeclarationInitializedToBareOrAccessedRequire(
		           parent->parent->parent);
	}
	return false;
}

// isExternalModuleImportEquals — importTracker.go:662.
bool isExternalModuleImportEquals(Node* node) {
	auto* moduleReference =
	    node->as<ImportEqualsDeclaration>()->ModuleReference;
	return isExternalModuleReference(moduleReference) &&
	       moduleReference->expression()->kind == Kind::StringLiteral;
}

// skipExportSpecifierSymbol — importTracker.go:668. If at an export
// specifier, go to the symbol it refers to.
Symbol* skipExportSpecifierSymbol(Symbol* symbol, checker::Checker* ch) {
	// For `export { foo } from './bar", there's nothing to skip, because
	// it does not create a new alias. But `export { foo } does.
	for (auto* declaration : symbol->declarations) {
		if (isExportSpecifier(declaration) &&
		    declaration->propertyName() == nullptr &&
		    declaration->parent->parent->moduleSpecifier() == nullptr) {
			if (auto* s = ch->GetExportSpecifierLocalTargetSymbol(
			        declaration);
			    s != nullptr) {
				return s;
			}
			return symbol;
		} else if (isPropertyAccessExpression(declaration) &&
		           isModuleExportsAccessExpression(
		               declaration->expression()) &&
		           !isPrivateIdentifier(declaration->name())) {
			// Export of form 'module.exports.propName = expr';
			return ch->getSymbolAtLocation(declaration, false);
		} else if (isShorthandPropertyAssignment(declaration) &&
		           isBinaryExpression(declaration->parent->parent) &&
		           getAssignmentDeclarationKind(
		               declaration->parent->parent) ==
		               JSDeclarationKind::ModuleExports) {
			return ch->GetExportSpecifierLocalTargetSymbol(
			    declaration->name());
		}
	}
	return symbol;
}

// getExportEqualsLocalSymbol — importTracker.go:684.
Symbol* getExportEqualsLocalSymbol(Symbol* importedSymbol,
                                   checker::Checker* ch) {
	if (importedSymbol->flags & SymbolFlagsAlias) {
		return ch->GetImmediateAliasedSymbol(importedSymbol);
	}
	auto* decl = importedSymbol->valueDeclaration;
	debug::assert(decl != nullptr);
	if (isExportAssignment(decl)) {
		return decl->expression()->symbol();
	}
	if (isBinaryExpression(decl)) {
		return decl->as<BinaryExpression>()->Right->symbol();
	}
	if (isSourceFile(decl)) {
		return decl->symbol();
	}
	return nullptr;
}

// symbolNameNoDefault — importTracker.go:701.
std::string symbolNameNoDefault(Symbol* symbol) {
	if (symbol->name != InternalSymbolNameDefault) {
		return symbol->name;
	}
	for (auto* decl : symbol->declarations) {
		auto* name = getNameOfDeclaration(decl);
		if (name != nullptr && isIdentifier(name)) {
			return name->text();
		}
	}
	return "";
}

// findModuleReferences — importTracker.go:716. Finds all references to a
// module symbol across the given source files. This includes import
// statements, <reference> directives, and implicit references (e.g., JSX
// runtime imports).
std::vector<ModuleReference> findModuleReferences(
    compiler::SimpleProgram* program,
    const std::vector<SourceFile*>& sourceFiles, Symbol* searchModuleSymbol,
    checker::Checker* ch) {
	std::vector<ModuleReference> refs;

	for (auto* referencingFile : sourceFiles) {
		auto* searchSourceFile =
		    searchModuleSymbol->valueDeclaration;
		if (searchSourceFile != nullptr &&
		    searchSourceFile->kind == Kind::SourceFile) {
			// Check <reference path> directives
			for (auto* ref : referencingFile->ReferencedFiles) {
				if (program->GetSourceFileFromReference(
				        referencingFile, ref) ==
				    searchSourceFile->as<SourceFile>()) {
					refs.push_back(ModuleReference{
					    .kind = ModuleReferenceKindReference,
					    .referencingFile = referencingFile,
					    .ref = ref,
					});
				}
			}

			// Check <reference types> directives
			for (auto* ref :
			     referencingFile->TypeReferenceDirectives) {
				auto* referenced =
				    getResolvedTypeReferenceDirectiveFromTypeReferenceDirective(
				        ref, referencingFile, program);
				if (referenced != nullptr &&
				    referenced->ResolvedFileName ==
				        searchSourceFile->as<SourceFile>()
				            ->FileName()) {
					refs.push_back(ModuleReference{
					    .kind = ModuleReferenceKindReference,
					    .referencingFile = referencingFile,
					    .ref = ref,
					});
				}
			}
		}

		// Check all imports (including require() calls)
		forEachImport(program, referencingFile,
		              [&](Node* importDecl, Node* moduleSpecifier) {
			              auto* moduleSymbol =
			                  ch->getSymbolAtLocation(moduleSpecifier,
			                                          false);
			              if (moduleSymbol == searchModuleSymbol) {
				              if (nodeIsSynthesized(importDecl)) {
					              refs.push_back(ModuleReference{
					                  .kind = ModuleReferenceKindImplicit,
					                  .literal = moduleSpecifier,
					                  .referencingFile =
					                      referencingFile,
					              });
				              } else {
					              refs.push_back(ModuleReference{
					                  .kind = ModuleReferenceKindImport,
					                  .literal = moduleSpecifier,
					              });
				              }
			              }
		              });
	}

	return refs;
}

// === dep stubs — removed when owner slice lands ===

// getPropertySymbolOfObjectBindingPatternWithoutPropertyName — utilities.go
// (ls-coreA).
Symbol* getPropertySymbolOfObjectBindingPatternWithoutPropertyName(
    Symbol* symbol, checker::Checker* ch) {
	TSC_UNREACHABLE(
	    "getPropertySymbolOfObjectBindingPatternWithoutPropertyName — owned by ls-coreA");
}

// isSourceFileWithGlobalExports — utilities.go:1409 (ls-coreA).
bool isSourceFileWithGlobalExports(Node* node) {
	TSC_UNREACHABLE(
	    "isSourceFileWithGlobalExports — owned by ls-coreA");
}

} // namespace tsc::ls
