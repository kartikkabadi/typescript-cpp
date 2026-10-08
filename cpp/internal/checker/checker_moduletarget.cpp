// checker_moduletarget.cpp — moduletarget slice: port of the
// getTargetOf*/module-member resolution family from tsc/internal/checker/checker.go
// (14277 addDeprecatedSuggestionWorker; 14667-15302 import/export target
// resolution; 15855-16053 ambient-module + ES-module symbol resolution and
// synthetic-default machinery; 16574 ResolveAlias; 16630
// resolveAliasWithDeprecationCheck), plus three ast helpers they depend on
// (GetNamespaceDeclarationNode, GetSourceFileOfModule, GetJSDocDeprecatedTag,
// GetNonAugmentationDeclaration) and isContainedByNamespace (checker.go:5740).

#include "internal/checker/checker.h"

#include <algorithm>
#include <string>
#include <vector>

namespace tsc {
namespace checker {

namespace {

// core.Find — first element satisfying pred, or nullptr.
template <class T, class F>
T* findOrNull(const std::vector<T*>& v, F&& f) {
	for (T* x : v) {
		if (f(x)) {
			return x;
		}
	}
	return nullptr;
}

// core ModuleKind.String (generated stringer ported as a switch)
const char* moduleKindToString(ModuleKind k) {
	switch (k) {
	case ModuleKind::None: return "None";
	case ModuleKind::CommonJS: return "CommonJS";
	case ModuleKind::AMD: return "AMD";
	case ModuleKind::UMD: return "UMD";
	case ModuleKind::System: return "System";
	case ModuleKind::ES2015: return "ES2015";
	case ModuleKind::ES2020: return "ES2020";
	case ModuleKind::ES2022: return "ES2022";
	case ModuleKind::ESNext: return "ESNext";
	case ModuleKind::Node16: return "Node16";
	case ModuleKind::Node18: return "Node18";
	case ModuleKind::Node20: return "Node20";
	case ModuleKind::NodeNext: return "NodeNext";
	case ModuleKind::Preserve: return "Preserve";
	default: return "";
	}
}

// utilities.go:38 — core.findInMap
Symbol* findInMap(const SymbolTable& m, const std::function<bool(Symbol*)>& predicate) {
	for (auto& [key, value] : m) {
		(void)key;
		if (predicate(value)) {
			return value;
		}
	}
	return nullptr;
}

// checker.go:5740 — isContainedByNamespace
bool isContainedByNamespace(Node* node) {
	Node* container = node->parent;
	if (!isSourceFile(container)) {
		container = container->parent;
	}
	return isModuleDeclaration(container) && !isAmbientModule(container);
}

// ast/utilities.go:2568 — GetNamespaceDeclarationNode
Node* getNamespaceDeclarationNode(Node* node) {
	switch (node->kind) {
	case Kind::ImportDeclaration:
	case Kind::JSImportDeclaration: {
		Node* importClause = node->importClause();
		if (importClause != nullptr &&
			importClause->as<ImportClause>()->NamedBindings != nullptr &&
			isNamespaceImport(importClause->as<ImportClause>()->NamedBindings)) {
			return importClause->as<ImportClause>()->NamedBindings;
		}
		break;
	}
	case Kind::ImportEqualsDeclaration:
		return node;
	case Kind::ExportDeclaration: {
		Node* exportClause = node->as<ExportDeclaration>()->ExportClause;
		if (exportClause != nullptr && isNamespaceExport(exportClause)) {
			return exportClause;
		}
		break;
	}
	default:
		break;
	}
	return nullptr;
}

// ast/utilities.go:1223 — GetJSDocDeprecatedTag
Node* getJSDocDeprecatedTag(Node* node) {
	for (Node* jsdoc : node->jsDoc(nullptr)) {
		NodeList* tags = jsdoc->as<JSDoc>()->Tags;
		if (tags != nullptr) {
			for (Node* tag : tags->nodes) {
				if (isJSDocDeprecatedTag(tag)) {
					return tag;
				}
			}
		}
	}
	return nullptr;
}

// checker.go:15294 — getModuleSpecifierFromNode (free function)
Node* getModuleSpecifierFromNode(Node* node) {
	switch (node->kind) {
	case Kind::ImportDeclaration:
	case Kind::JSImportDeclaration:
		return node->moduleSpecifier();
	case Kind::ExportDeclaration:
		return node->moduleSpecifier();
	default:
		break;
	}
	TSC_UNREACHABLE("Unhandled case in getModuleSpecifierFromNode");
}

// checker.go:15947 — isESMFormatImportImportingCommonjsFormatFile
bool isESMFormatImportImportingCommonjsFormatFile(ResolutionMode usageMode,
												  ResolutionMode targetMode) {
	return usageMode == ModuleKind::ESNext && targetMode == ModuleKind::CommonJS;
}

}  // namespace

// checker.go:14277 — addDeprecatedSuggestionWorker
Diagnostic* Checker::addDeprecatedSuggestionWorker(const std::vector<Node*>& declarations,
												   Diagnostic* diagnostic) {
	for (Node* declaration : declarations) {
		Node* deprecatedTag = getJSDocDeprecatedTag(declaration);
		if (deprecatedTag != nullptr) {
			diagnostic->AddRelatedInfo(NewDiagnosticForNode(
				deprecatedTag, The_declaration_was_marked_as_deprecated_here, {}));
			break;
		}
	}
	return addSuggestionDiagnostic(diagnostic);
}

// checker.go:14667 — getTargetOfImportEqualsDeclaration
Symbol* Checker::getTargetOfImportEqualsDeclaration(Node* node) {
	// Node is ImportEqualsDeclaration | VariableDeclaration
	if (isVariableDeclaration(node) ||
		node->as<ImportEqualsDeclaration>()->ModuleReference->kind ==
			Kind::ExternalModuleReference) {
		Node* moduleReference = getExternalModuleRequireArgument(node);
		if (moduleReference == nullptr) {
			moduleReference = getExternalModuleImportEqualsDeclarationExpression(node);
		}
		Symbol* immediate = resolveExternalModuleName(node, moduleReference,
													false /*ignoreErrors*/,
													nullptr /*importAttributesType*/);
		Symbol* resolved = resolveExternalModuleSymbol(immediate, true /*dontResolveAlias*/);
		if (resolved != nullptr && ModuleKind::Node20 <= moduleKind &&
			moduleKind <= ModuleKind::NodeNext) {
			Symbol* moduleExports =
				getExportOfModule(resolved, InternalSymbolNameModuleExports, node,
								  true /*dontResolveAlias*/);
			if (moduleExports != nullptr) {
				return moduleExports;
			}
		}
		markSymbolOfAliasDeclarationIfTypeOnly(node, nullptr);
		return resolved;
	}
	Symbol* resolved = getSymbolOfPartOfRightHandSideOfImportEquals(
		node->as<ImportEqualsDeclaration>()->ModuleReference);
	checkAndReportErrorForResolvingImportAliasToTypeOnlySymbol(node, resolved);
	return resolved;
}

// checker.go:14690 — resolveExternalModuleTypeByLiteral
Type* Checker::resolveExternalModuleTypeByLiteral(Node* name) {
	Symbol* moduleSym = resolveExternalModuleName(name, name, false /*ignoreErrors*/,
												nullptr /*importAttributesType*/);
	if (moduleSym != nullptr) {
		Symbol* resolvedModuleSymbol =
			resolveExternalModuleSymbol(moduleSym, false /*dontResolveAlias*/);
		if (resolvedModuleSymbol != nullptr) {
			return getTypeOfSymbol(resolvedModuleSymbol);
		}
	}
	return anyType;
}

// checker.go:14722 — checkAndReportErrorForResolvingImportAliasToTypeOnlySymbol
void Checker::checkAndReportErrorForResolvingImportAliasToTypeOnlySymbol(Node* node,
																		 Symbol* resolved) {
	ImportEqualsDeclaration* decl = node->as<ImportEqualsDeclaration>();
	Node* name = decl->ModuleReference;
	for (;;) {
		if (Node* typeOnlyDeclaration = getTypeOnlyDeclarationOfEntityName(name);
			typeOnlyDeclaration != nullptr) {
			bool isExport = nodeKindIs(typeOnlyDeclaration, Kind::ExportSpecifier,
									   Kind::ExportDeclaration);
			const DiagnosticMessage* message =
				isExport ? 
							  An_import_alias_cannot_reference_a_declaration_that_was_exported_using_export_type
						 : 
							  An_import_alias_cannot_reference_a_declaration_that_was_imported_using_import_type;
			const DiagnosticMessage* relatedMessage =
				isExport ? X_0_was_exported_here
						 : X_0_was_imported_here;
			// TODO: how to get name for export *?
			std::string name = "*";
			if (!isExportDeclaration(typeOnlyDeclaration)) {
				name = typeOnlyDeclaration->name()->text();
			}
			error(decl->ModuleReference, message, std::vector<std::string>{})
				->AddRelatedInfo(
					createDiagnosticForNode(typeOnlyDeclaration, relatedMessage, {name}));
			break;
		}
		if (isIdentifier(name)) {
			break;
		}
		name = name->as<QualifiedName>()->Left;
	}
}

// checker.go:14749 — getTypeOnlyDeclarationOfEntityName
Node* Checker::getTypeOnlyDeclarationOfEntityName(Node* name) {
	if (Symbol* symbol =
			resolveEntityName(name,
							  SymbolFlagsValue | SymbolFlagsType | SymbolFlagsNamespace,
							  true /*ignoreErrors*/, true /*dontResolveAlias*/,
							  nullptr /*location*/);
		symbol != nullptr) {
		return getTypeOnlyAliasDeclaration(symbol);
	}
	return nullptr;
}

// checker.go:14756 — getTargetOfImportClause
Symbol* Checker::getTargetOfImportClause(Node* node) {
	Symbol* moduleSymbol = resolveExternalModuleName(
		node, getModuleSpecifierFromNode(node->parent), false /*ignoreErrors*/,
		getTypeFromImportAttributes(getImportAttributes(node->parent)));
	if (moduleSymbol != nullptr) {
		return getTargetOfModuleDefault(moduleSymbol, node, true /*dontResolveAlias*/);
	}
	return nullptr;
}

// checker.go:14764 — getTargetOfModuleDefault
Symbol* Checker::getTargetOfModuleDefault(Symbol* moduleSymbol, Node* node,
										  bool dontResolveAlias) {
	Node* file = findOrNull(moduleSymbol->declarations, [](Node* d) { return isSourceFile(d); });
	Node* specifier = getModuleSpecifierForImportOrExport(node);
	Symbol* exportDefaultSymbol = nullptr;
	Symbol* exportModuleDotExportsSymbol = nullptr;
	if (isShorthandAmbientModuleSymbol(moduleSymbol)) {
		// !!! exportDefaultSymbol = moduleSymbol
		// Does nothing?
	} else if (file != nullptr && specifier != nullptr &&
			   ModuleKind::Node20 <= moduleKind && moduleKind <= ModuleKind::NodeNext &&
			   getEmitSyntaxForModuleSpecifierExpression(specifier) == ModuleKind::CommonJS &&
			   program->GetImpliedNodeFormatForEmit(static_cast<SourceFile*>(file)) ==
				   ModuleKind::ESNext) {
		exportModuleDotExportsSymbol = resolveExportByName(
			moduleSymbol, InternalSymbolNameModuleExports, node, dontResolveAlias);
	}
	if (exportModuleDotExportsSymbol != nullptr) {
		// We have a transpiled default import where the `require` resolves to an ES
		// module with a `module.exports` named export. With `esModuleInterop` (always
		// enabled), this will work:
		//
		// const dep_1 = __importDefault(require("./dep.mjs")); // wraps like { default: require("./dep.mjs") }
		// dep_1.default; // require("./dep.mjs") -> the `module.exports` export value
		markSymbolOfAliasDeclarationIfTypeOnly(node, nullptr);
		return exportModuleDotExportsSymbol;
	} else {
		exportDefaultSymbol = resolveExportByName(moduleSymbol, InternalSymbolNameDefault,
												  node, dontResolveAlias);
	}
	if (specifier == nullptr) {
		return exportDefaultSymbol;
	}
	// node is ImportClause | ImportSpecifier | ExportSpecifier
	Node* attributes = nullptr;
	if (isImportClause(node)) {
		attributes = getImportAttributes(node->parent);
	} else if (isImportSpecifier(node)) {
		attributes = getImportAttributes(node->parent->parent->parent);
	} else if (isExportSpecifier(node)) {
		attributes = getImportAttributes(node->parent->parent);
	}
	bool hasDefaultOnly = isOnlyImportableAsDefault(
		specifier, moduleSymbol, getTypeFromImportAttributes(attributes));
	bool hasSyntheticDefault =
		canHaveSyntheticDefault(file, moduleSymbol, dontResolveAlias, specifier);
	if (exportDefaultSymbol == nullptr && !hasSyntheticDefault && !hasDefaultOnly) {
		if (isImportClause(node)) {
			reportNonDefaultExport(moduleSymbol, node);
		} else {
			Node* name;
			if (isImportOrExportSpecifier(node)) {
				name = node->propertyNameOrName();
			} else {
				name = node->name();
			}
			errorNoModuleMemberSymbol(moduleSymbol, moduleSymbol, node, name);
		}
	} else if (hasSyntheticDefault || hasDefaultOnly) {
		// per emit behavior, a synthetic default overrides a "real" .default member if
		// `__esModule` is not present
		Symbol* resolved = resolveExternalModuleSymbol(moduleSymbol, dontResolveAlias);
		if (resolved == nullptr) {
			resolved = resolveSymbolEx(moduleSymbol, dontResolveAlias);
		}
		markSymbolOfAliasDeclarationIfTypeOnly(node, nullptr);
		return resolved;
	}
	markSymbolOfAliasDeclarationIfTypeOnly(node, nullptr);
	return exportDefaultSymbol;
}

// checker.go:14828 — reportNonDefaultExport
void Checker::reportNonDefaultExport(Symbol* moduleSymbol, Node* node) {
	if (getSymbolFromTable(moduleSymbol->exports, node->symbol()->name) != nullptr) {
		error(node,
			  Module_0_has_no_default_export_Did_you_mean_to_use_import_1_from_0_instead,
			  {symbolToString(moduleSymbol), symbolToString(node->symbol())});
	} else {
		Diagnostic* diagnostic = error(node->name(),
									   Module_0_has_no_default_export,
									   {symbolToString(moduleSymbol)});
		Symbol* exportStar = nullptr;
		auto it = moduleSymbol->exports.find(InternalSymbolNameExportStar);
		if (it != moduleSymbol->exports.end()) {
			exportStar = it->second;
		}
		if (exportStar != nullptr) {
			Node* defaultExport = findOrNull(exportStar->declarations, [&](Node* decl) {
				if (!(isExportDeclaration(decl) && decl->moduleSpecifier() != nullptr)) {
					return false;
				}
				Symbol* resolvedExternalModuleName = resolveExternalModuleName(
					decl, decl->moduleSpecifier(), false /*ignoreErrors*/,
					getTypeFromImportAttributes(getImportAttributes(decl)));
				return resolvedExternalModuleName != nullptr &&
					   getSymbolFromTable(resolvedExternalModuleName->exports,
										  InternalSymbolNameDefault) != nullptr;
			});
			if (defaultExport != nullptr) {
				diagnostic->AddRelatedInfo(createDiagnosticForNode(
					defaultExport, X_export_Asterisk_does_not_re_export_a_default,
					{}));
			}
		}
	}
}

// checker.go:14852 — resolveExportByName
Symbol* Checker::resolveExportByName(Symbol* moduleSymbol, const std::string& name,
									 Node* sourceNode, bool dontResolveAlias) {
	Symbol* exportValue = nullptr;
	auto it = moduleSymbol->exports.find(InternalSymbolNameExportEquals);
	if (it != moduleSymbol->exports.end()) {
		exportValue = it->second;
	}
	Symbol* exportSymbol = nullptr;
	if (exportValue != nullptr) {
		exportSymbol = getPropertyOfTypeEx(getTypeOfSymbol(exportValue), name,
										   true /*skipObjectFunctionPropertyAugment*/,
										   false /*includeTypeOnlyMembers*/);
	} else {
		auto it2 = moduleSymbol->exports.find(name);
		if (it2 != moduleSymbol->exports.end()) {
			exportSymbol = it2->second;
		}
	}
	Symbol* resolved = resolveSymbolEx(exportSymbol, dontResolveAlias);
	markSymbolOfAliasDeclarationIfTypeOnly(sourceNode, nullptr);
	return resolved;
}

// checker.go:14865 — getTargetOfNamespaceImport
Symbol* Checker::getTargetOfNamespaceImport(Node* node) {
	Node* moduleSpecifier = getModuleSpecifierForImportOrExport(node);
	Symbol* immediate = resolveExternalModuleName(
		node, moduleSpecifier, false /*ignoreErrors*/,
		getTypeFromImportAttributes(getImportAttributes(node->parent->parent)));
	Symbol* resolved = resolveESModuleSymbol(immediate, node, moduleSpecifier);
	markSymbolOfAliasDeclarationIfTypeOnly(node, nullptr);
	return resolved;
}

// checker.go:14873 — getTargetOfNamespaceExport
Symbol* Checker::getTargetOfNamespaceExport(Node* node) {
	Node* moduleSpecifier = getModuleSpecifierForImportOrExport(node);
	if (moduleSpecifier != nullptr) {
		Symbol* immediate = resolveExternalModuleName(
			node, moduleSpecifier, false /*ignoreErrors*/,
			getTypeFromImportAttributes(getImportAttributes(node->parent)));
		Symbol* resolved = resolveESModuleSymbol(immediate, node, moduleSpecifier);
		markSymbolOfAliasDeclarationIfTypeOnly(node, nullptr);
		return resolved;
	}
	return nullptr;
}

// checker.go:14884 — getTargetOfImportSpecifier
Symbol* Checker::getTargetOfImportSpecifier(Node* node) {
	Node* name = node->propertyNameOrName();
	if (isImportSpecifier(node) && moduleExportNameIsDefault(name)) {
		Node* specifier = getModuleSpecifierForImportOrExport(node);
		if (specifier != nullptr) {
			Symbol* moduleSymbol = resolveExternalModuleName(
				node, specifier, false /*ignoreErrors*/,
				getTypeFromImportAttributes(
					getImportAttributes(node->parent->parent->parent)));
			if (moduleSymbol != nullptr) {
				return getTargetOfModuleDefault(moduleSymbol, node,
												true /*dontResolveAlias*/);
			}
		}
	}
	Node* root = node->parent->parent->parent;  // ImportDeclaration
	if (isBindingElement(node)) {
		root = getRootDeclaration(node);
	}
	Symbol* resolved = getExternalModuleMember(root, node, true /*dontResolveAlias*/);
	markSymbolOfAliasDeclarationIfTypeOnly(node, nullptr);
	return resolved;
}

// checker.go:14904 — getExternalModuleMember
Symbol* Checker::getExternalModuleMember(Node* node, Node* specifier,
										 bool dontResolveAlias) {
	// node is ImportDeclaration | ExportDeclaration | VariableDeclaration
	// specifier is ImportSpecifier | ExportSpecifier | BindingElement |
	// PropertyAccessExpression
	Node* moduleSpecifier = getExternalModuleRequireArgument(node);
	if (moduleSpecifier == nullptr) {
		moduleSpecifier = getExternalModuleName(node);
	}
	Node* attributes = nullptr;
	if (hasImportAttributes(node)) {
		attributes = getImportAttributes(node);
	}
	Type* importAttributesType = getTypeFromImportAttributes(attributes);
	Symbol* moduleSymbol = resolveExternalModuleName(
		node, moduleSpecifier, false /*ignoreErrors*/, importAttributesType);
	Node* name;
	if (!isPropertyAccessExpression(specifier)) {
		name = specifier->propertyNameOrName();
	} else {
		name = specifier->name();
	}
	if (!isIdentifier(name) && !isStringLiteral(name)) {
		return nullptr;
	}
	std::string nameText = name->text();
	Symbol* targetSymbol = resolveESModuleSymbol(moduleSymbol, specifier, moduleSpecifier);
	if (targetSymbol != nullptr) {
		// Note: The empty string is a valid module export name:
		//
		//   import { "" as foo } from "./foo";
		//   export { foo as "" };
		//
		if (nameText != "" || name->kind == Kind::StringLiteral) {
			if (isShorthandAmbientModuleSymbol(moduleSymbol)) {
				return moduleSymbol;
			}
			Symbol* symbolFromVariable = nullptr;
			// First check if module was specified with "export=". If so, get the
			// member from the resolved type
			if (moduleSymbol != nullptr &&
				getSymbolFromTable(moduleSymbol->exports,
								   InternalSymbolNameExportEquals) != nullptr) {
				symbolFromVariable = getPropertyOfTypeEx(
					getTypeOfSymbol(targetSymbol), nameText,
					true /*skipObjectFunctionPropertyAugment*/,
					false /*includeTypeOnlyMembers*/);
			} else {
				symbolFromVariable = getPropertyOfVariable(targetSymbol, nameText);
			}
			// if symbolFromVariable is export - get its final target
			symbolFromVariable = resolveSymbolEx(symbolFromVariable, dontResolveAlias);
			Symbol* exportContainer = targetSymbol;
			if (moduleSymbol != nullptr &&
				getSymbolFromTable(moduleSymbol->exports,
								   InternalSymbolNameExportEquals) != nullptr) {
				// For `export =` modules, supplemental type/namespace exports live on
				// the original module symbol.
				exportContainer = moduleSymbol;
			}
			Symbol* symbolFromModule = getExportOfModule(exportContainer, nameText,
													   specifier, dontResolveAlias);
			if (symbolFromModule == nullptr && nameText == InternalSymbolNameDefault) {
				Node* file = findOrNull(moduleSymbol->declarations,
										[](Node* d) { return isSourceFile(d); });
				if (isOnlyImportableAsDefault(moduleSpecifier, moduleSymbol,
											  importAttributesType) ||
					canHaveSyntheticDefault(file, moduleSymbol, dontResolveAlias,
											moduleSpecifier)) {
					symbolFromModule =
						resolveExternalModuleSymbol(moduleSymbol, dontResolveAlias);
					if (symbolFromModule == nullptr) {
						symbolFromModule = resolveSymbolEx(moduleSymbol, dontResolveAlias);
					}
				}
			}
			Symbol* symbol = symbolFromVariable;
			if (symbolFromModule != nullptr) {
				symbol = symbolFromModule;
				if (symbolFromVariable != nullptr) {
					symbol = combineValueAndTypeSymbols(symbolFromVariable, symbolFromModule);
				}
			}
			if (isImportOrExportSpecifier(specifier) &&
				isOnlyImportableAsDefault(moduleSpecifier, moduleSymbol,
										  importAttributesType) &&
				nameText != InternalSymbolNameDefault) {
				error(name,
					  
						  Named_imports_from_a_JSON_file_into_an_ECMAScript_module_are_not_allowed_when_module_is_set_to_0,
					  std::vector<std::string>{moduleKindToString(moduleKind)});
			} else if (symbol == nullptr) {
				errorNoModuleMemberSymbol(moduleSymbol, targetSymbol, node, name);
			}
			return symbol;
		}
	}
	return nullptr;
}

// checker.go:14980 — getPropertyOfVariable
Symbol* Checker::getPropertyOfVariable(Symbol* symbol, const std::string& name) {
	if (symbol->flags & SymbolFlagsVariable) {
		Node* typeAnnotation = symbol->valueDeclaration->type();
		if (typeAnnotation != nullptr) {
			return resolveSymbol(
				getPropertyOfType(getTypeFromTypeNode(typeAnnotation), name));
		}
	}
	return nullptr;
}

// checker.go:15008 — combineValueAndTypeSymbols
Symbol* Checker::combineValueAndTypeSymbols(Symbol* valueSymbol, Symbol* typeSymbol) {
	if (valueSymbol == unknownSymbol && typeSymbol == unknownSymbol) {
		return unknownSymbol;
	}
	if (typeSymbol->flags & SymbolFlagsValue) {
		return typeSymbol;
	}
	if (valueSymbol->flags & (SymbolFlagsType | SymbolFlagsNamespace)) {
		return valueSymbol;
	}
	Symbol* result = newSymbol(valueSymbol->flags | typeSymbol->flags, valueSymbol->name);
	TSC_ASSERT(!valueSymbol->declarations.empty() || !typeSymbol->declarations.empty(),
			   "declarations");
	result->declarations = valueSymbol->declarations;
	for (Node* d : typeSymbol->declarations) {
		result->declarations.push_back(d);
	}
	auto last = std::unique(result->declarations.begin(), result->declarations.end());
	result->declarations.erase(last, result->declarations.end());
	result->parent = valueSymbol->parent;
	if (result->parent == nullptr) {
		result->parent = typeSymbol->parent;
	}
	result->valueDeclaration = valueSymbol->valueDeclaration;
	result->members = typeSymbol->members;
	result->exports = valueSymbol->exports;
	return result;
}

// checker.go:15031 — getExportOfModule
Symbol* Checker::getExportOfModule(Symbol* symbol, const std::string& nameText,
								   Node* specifier, bool dontResolveAlias) {
	if (symbol->flags & SymbolFlagsModule) {
		const SymbolTable& exports = getExportsOfSymbol(symbol);
		Symbol* exportSymbol = nullptr;
		auto it = exports.find(nameText);
		if (it != exports.end()) {
			exportSymbol = it->second;
		}
		Symbol* resolved = resolveSymbolEx(exportSymbol, dontResolveAlias);
		Node* exportStarDeclaration = nullptr;
		auto mapIt = moduleSymbolLinks.Get(symbol)->typeOnlyExportStarMap.find(nameText);
		if (mapIt != moduleSymbolLinks.Get(symbol)->typeOnlyExportStarMap.end()) {
			exportStarDeclaration = mapIt->second;
		}
		markSymbolOfAliasDeclarationIfTypeOnly(specifier, exportStarDeclaration);
		return resolved;
	}
	return nullptr;
}

// checker.go:15042 — isOnlyImportableAsDefault
bool Checker::isOnlyImportableAsDefault(Node* usage, Symbol* resolvedModule,
										Type* importAttributesType) {
	// In Node.js, JSON modules don't get named exports
	if (ModuleKind::Node16 <= moduleKind && moduleKind <= ModuleKind::NodeNext) {
		ResolutionMode usageMode = getEmitSyntaxForModuleSpecifierExpression(usage);
		if (usageMode == ModuleKind::ESNext) {
			if (resolvedModule == nullptr) {
				resolvedModule = resolveExternalModuleName(usage, usage,
														   true /*ignoreErrors*/,
														   importAttributesType);
			}
			SourceFile* targetFile = nullptr;
			if (resolvedModule != nullptr) {
				targetFile = getSourceFileOfModule(resolvedModule);
			}
			return targetFile != nullptr &&
				   (isJsonSourceFile(targetFile) ||
					tspath::getDeclarationFileExtension(targetFile->FileName()) == ".d.json.ts");
		}
	}
	return false;
}

// checker.go:15060 — canHaveSyntheticDefault
bool Checker::canHaveSyntheticDefault(Node* file, Symbol* moduleSymbol,
									  bool dontResolveAlias, Node* usage) {
	ResolutionMode usageMode = ModuleKind::None;
	if (file != nullptr) {
		usageMode = getEmitSyntaxForModuleSpecifierExpression(usage);
	}
	if (file != nullptr && usageMode != ModuleKind::None) {
		ModuleKind targetMode =
			program->GetImpliedNodeFormatForEmit(static_cast<SourceFile*>(file));
		if (usageMode == ModuleKind::ESNext && targetMode == ModuleKind::CommonJS &&
			ModuleKind::Node16 <= moduleKind && moduleKind <= ModuleKind::NodeNext) {
			// In Node.js, CommonJS modules always have a synthetic default when
			// imported into ESM
			return true;
		}
		if (usageMode == ModuleKind::ESNext && targetMode == ModuleKind::ESNext) {
			// No matter what the `module` setting is, if we're confident that both
			// files are ESM, there cannot be a synthetic default.
			return false;
		}
		// For other files (not node16/nodenext with impliedNodeFormat), check if we
		// can determine the module format from project references
		if (targetMode == ModuleKind::None &&
			static_cast<SourceFile*>(file)->IsDeclarationFile) {
			// Try to get the project reference - try both source file mapping and
			// output file mapping since declaration files can be mapped either way
			// depending on how they're resolved
			if (program->GetRedirectForResolution(static_cast<SourceFile*>(file)) !=
					nullptr ||
				program->GetProjectReferenceFromOutputDts(
					static_cast<SourceFile*>(file)->Path()) != nullptr) {
				// This is a declaration file from a project reference, so we can
				// determine its module format from the referenced project's options
				ModuleKind targetModuleKind = program->GetEmitModuleFormatOfFile(
					static_cast<SourceFile*>(file));
				if (usageMode == ModuleKind::ESNext &&
					ModuleKind::ES2015 <= targetModuleKind &&
					targetModuleKind <= ModuleKind::ESNext) {
					return false;
				}
			}
		}
	}
	// Declaration files (and ambient modules)
	if (file == nullptr || static_cast<SourceFile*>(file)->IsDeclarationFile) {
		// Definitely cannot have a synthetic default if they have a syntactic
		// default member specified
		Symbol* defaultExportSymbol =
			resolveExportByName(moduleSymbol, InternalSymbolNameDefault,
								nullptr /*sourceNode*/, true /*dontResolveAlias*/);  // Dont resolve alias because we want the immediately exported symbol's declaration
		if (defaultExportSymbol != nullptr) {
			bool anySyntactic = false;
			for (Node* d : defaultExportSymbol->declarations) {
				if (isSyntacticDefault(d)) {
					anySyntactic = true;
					break;
				}
			}
			if (anySyntactic) {
				return false;
			}
		}
		// It _might_ still be incorrect to assume there is no __esModule marker on
		// the import at runtime, even if there is no `default` member. So we check a
		// bit more,
		if (resolveExportByName(moduleSymbol, "__esModule", nullptr /*sourceNode*/,
								dontResolveAlias) != nullptr) {
			// If there is an `__esModule` specified in the declaration (meaning
			// someone explicitly added it or wrote it in their code), it definitely
			// is a module and does not have a synthetic default
			return false;
		}
		// There are _many_ declaration files not written with esmodules in mind that
		// still get compiled into a format with __esModule set. Meaning there may be
		// no default at runtime - however to be on the permissive side, we allow
		// access to a synthetic default member as there is no marker to indicate if
		// the accompanying JS has `__esModule` or not, or is even native esm
		return true;
	}
	// TypeScript files never have a synthetic default (as they are always emitted
	// with an __esModule marker) _unless_ they contain an export= statement
	if (!isInJSFile(file)) {
		return hasExportAssignmentSymbol(moduleSymbol);
	}

	// JS files have a synthetic default if they do not contain ES2015+ module
	// syntax (export = is not valid in js) _and_ do not have an __esModule marker
	return (static_cast<SourceFile*>(file)->ExternalModuleIndicator == nullptr ||
			static_cast<SourceFile*>(file)->ExternalModuleIndicator == file) &&
		   resolveExportByName(moduleSymbol, "__esModule", nullptr /*sourceNode*/,
							   dontResolveAlias) == nullptr;
}

// checker.go:15119 — getEmitSyntaxForModuleSpecifierExpression
ResolutionMode Checker::getEmitSyntaxForModuleSpecifierExpression(Node* usage) {
	if (isStringLiteralLike(usage)) {
		return program->GetEmitSyntaxForUsageLocation(getSourceFileOfNode(usage), usage);
	}
	return ModuleKind::None;
}

// checker.go:15126 — errorNoModuleMemberSymbol
void Checker::errorNoModuleMemberSymbol(Symbol* moduleSymbol, Symbol* targetSymbol,
										Node* node, Node* name) {
	if (compilerOptions->NoCheck == Tristate::True) {
		return;
	}
	std::string moduleName = getFullyQualifiedName(moduleSymbol, node);
	std::string declarationName = declarationNameToString(name);
	Symbol* suggestion = nullptr;
	if (isIdentifier(name)) {
		suggestion = getSuggestedSymbolForNonexistentModule(name, targetSymbol);
	}
	if (suggestion != nullptr) {
		std::string suggestionName = symbolToString(suggestion);
		Diagnostic* diagnostic =
			error(name, X_0_has_no_exported_member_named_1_Did_you_mean_2,
				  {moduleName, declarationName, suggestionName});
		if (suggestion->valueDeclaration != nullptr) {
			diagnostic->AddRelatedInfo(createDiagnosticForNode(
				suggestion->valueDeclaration, X_0_is_declared_here,
				{suggestionName}));
		}
	} else {
		if (getSymbolFromTable(moduleSymbol->exports,
								  InternalSymbolNameDefault) != nullptr) {
			error(name,
				  
					  Module_0_has_no_exported_member_1_Did_you_mean_to_use_import_1_from_0_instead,
				  {moduleName, declarationName});
		} else {
			reportNonExportedMember(name, declarationName, moduleSymbol, moduleName);
		}
	}
}

// checker.go:15151 — reportNonExportedMember
void Checker::reportNonExportedMember(Node* name, const std::string& declarationName,
									  Symbol* moduleSymbol,
									  const std::string& moduleName) {
	Symbol* localSymbol = nullptr;
	if (SymbolTable* locals = moduleSymbol->valueDeclaration->locals()) {
		auto it = locals->find(name->text());
		if (it != locals->end()) {
			localSymbol = it->second;
		}
	}
	SymbolTable& exports = moduleSymbol->exports;
	if (localSymbol != nullptr) {
		Symbol* exportedEqualsSymbol = nullptr;
		auto it = exports.find(InternalSymbolNameExportEquals);
		if (it != exports.end()) {
			exportedEqualsSymbol = it->second;
		}
		if (exportedEqualsSymbol != nullptr) {
			if (getSymbolIfSameReference(exportedEqualsSymbol, localSymbol) != nullptr) {
				reportInvalidImportEqualsExportMember(name, declarationName, moduleName);
			} else {
				error(name, Module_0_has_no_exported_member_1,
					  {moduleName, declarationName});
			}
		} else {
			Symbol* exportedSymbol = findInMap(exports, [&](Symbol* symbol) {
				return getSymbolIfSameReference(symbol, localSymbol) != nullptr;
			});
			Diagnostic* diagnostic;
			if (exportedSymbol != nullptr) {
				diagnostic = error(
					name,
					Module_0_declares_1_locally_but_it_is_exported_as_2,
					{moduleName, declarationName, symbolToString(exportedSymbol)});
			} else {
				diagnostic = error(
					name, Module_0_declares_1_locally_but_it_is_not_exported,
					{moduleName, declarationName});
			}
			int i = 0;
			for (Node* decl : localSymbol->declarations) {
				diagnostic->AddRelatedInfo(createDiagnosticForNode(
					decl,
					i == 0 ? X_0_is_declared_here : X_and_here,
					{declarationName}));
				i++;
			}
		}
	} else {
		error(name, Module_0_has_no_exported_member_1,
			  {moduleName, declarationName});
	}
}

// checker.go:15183 — reportInvalidImportEqualsExportMember
void Checker::reportInvalidImportEqualsExportMember(Node* name,
													const std::string& declarationName,
													const std::string& moduleName) {
	if (moduleKind >= ModuleKind::ES2015) {
		error(name, X_0_can_only_be_imported_by_using_a_default_import,
			  {declarationName});
	} else if (isInJSFile(name)) {
		error(name,
			  
				  X_0_can_only_be_imported_by_using_a_require_call_or_by_using_a_default_import,
			  {declarationName});
	} else {
		error(name,
			  
				  X_0_can_only_be_imported_by_using_import_1_require_2_or_a_default_import,
			  {declarationName, declarationName, moduleName});
	}
}

// checker.go:15193 — getTargetOfExportSpecifier
Symbol* Checker::getTargetOfExportSpecifier(Node* node, SymbolFlags meaning,
											bool dontResolveAlias) {
	Node* name = node->propertyNameOrName();
	if (moduleExportNameIsDefault(name)) {
		Node* specifier = getModuleSpecifierForImportOrExport(node);
		if (specifier != nullptr) {
			Symbol* moduleSymbol = resolveExternalModuleName(
				node, specifier, false /*ignoreErrors*/,
				getTypeFromImportAttributes(getImportAttributes(node->parent->parent)));
			if (moduleSymbol != nullptr) {
				return getTargetOfModuleDefault(moduleSymbol, node, dontResolveAlias);
			}
		}
	}
	Node* exportDeclaration = node->parent->parent;
	Symbol* resolved = nullptr;
	if (exportDeclaration->moduleSpecifier() != nullptr) {
		resolved = getExternalModuleMember(exportDeclaration, node, dontResolveAlias);
	} else if (isStringLiteral(name)) {
		resolved = nullptr;
	} else {
		resolved = resolveEntityName(name, meaning, false /*ignoreErrors*/,
									 dontResolveAlias, nullptr /*location*/);
	}
	markSymbolOfAliasDeclarationIfTypeOnly(node, nullptr);
	return resolved;
}

// checker.go:15218 — getTargetOfExportAssignment
Symbol* Checker::getTargetOfExportAssignment(Node* node) {
	// An `export =` / `export default` inside a namespace/module block is a grammar
	// error; checkExportAssignment reports it and returns without resolving the
	// expression. Mirror that bail-out here (using the same container computation)
	// so that alias resolution triggered by the emit resolver does not resolve —
	// and report "Cannot find name" diagnostics on — the expression, which would
	// produce diagnostics inconsistent with checking.
	if (isContainedByNamespace(node)) {
		return nullptr;
	}
	Symbol* resolved = getTargetOfAliasLikeExpression(node->expression());
	markSymbolOfAliasDeclarationIfTypeOnly(node, nullptr);
	return resolved;
}

// checker.go:15232 — getTargetOfBinaryExpression
Symbol* Checker::getTargetOfBinaryExpression(Node* node) {
	Symbol* resolved =
		getTargetOfAliasLikeExpression(node->as<BinaryExpression>()->Right);
	markSymbolOfAliasDeclarationIfTypeOnly(node, nullptr);
	return resolved;
}

// checker.go:15238 — getTargetOfAliasLikeExpression
Symbol* Checker::getTargetOfAliasLikeExpression(Node* expression) {
	if (isClassExpression(expression)) {
		return checkExpressionCached(expression)->symbol;
	}
	if (!isEntityName(expression) && !isEntityNameExpression(expression)) {
		return nullptr;
	}
	Symbol* aliasLike =
		resolveEntityName(expression,
						  SymbolFlagsValue | SymbolFlagsType | SymbolFlagsNamespace,
						  true /*ignoreErrors*/, true /*dontResolveAlias*/,
						  nullptr /*location*/);
	if (aliasLike != nullptr) {
		return aliasLike;
	}
	checkExpressionCached(expression);
	return getResolvedSymbolOrNil(expression);
}

// checker.go:15253 — getTargetOfNamespaceExportDeclaration
Symbol* Checker::getTargetOfNamespaceExportDeclaration(Node* node) {
	if (canHaveSymbol(node->parent)) {
		Symbol* resolved = resolveExternalModuleSymbol(node->parent->symbol(),
													 true /*dontResolveAlias*/);
		markSymbolOfAliasDeclarationIfTypeOnly(node, nullptr);
		return resolved;
	}
	return nullptr;
}

// checker.go:15262 — getTargetOfAccessExpression
Symbol* Checker::getTargetOfAccessExpression(Node* node) {
	if (isBinaryExpression(node->parent)) {
		BinaryExpression* expr = node->parent->as<BinaryExpression>();
		if (expr->Left == node && expr->OperatorToken->kind == Kind::EqualsToken) {
			return getTargetOfAliasLikeExpression(expr->Right);
		}
	}
	return nullptr;
}

// checker.go:15272 — getModuleSpecifierForImportOrExport
Node* Checker::getModuleSpecifierForImportOrExport(Node* node) {
	switch (node->kind) {
	case Kind::ImportClause:
		return getModuleSpecifierFromNode(node->parent);
	case Kind::ImportEqualsDeclaration:
		if (isExternalModuleReference(
				node->as<ImportEqualsDeclaration>()->ModuleReference)) {
			return node->as<ImportEqualsDeclaration>()->ModuleReference->expression();
		} else {
			return nullptr;
		}
	case Kind::NamespaceImport:
		return getModuleSpecifierFromNode(node->parent->parent);
	case Kind::ImportSpecifier:
		return getModuleSpecifierFromNode(node->parent->parent->parent);
	case Kind::NamespaceExport:
		return getModuleSpecifierFromNode(node->parent);
	case Kind::ExportSpecifier:
		return getModuleSpecifierFromNode(node->parent->parent);
	default:
		break;
	}
	TSC_UNREACHABLE("Unhandled case in getModuleSpecifierForImportOrExport");
}

// checker.go:15855 — GetAmbientModules
std::vector<Symbol*> Checker::GetAmbientModules() {
	std::call_once(ambientModulesOnce, [&] {
		std::unordered_set<Symbol*> seen;
		for (auto& [sym, global] : globals) {
			if (isAmbientModuleSymbolName(sym)) {
				ambientModules.push_back(global);
				seen.insert(global);
			}
		}
		for (auto& module : patternAmbientModules) {
			Symbol* symbol = getMergedSymbol(module.symbol);
			if (seen.count(symbol) == 0) {
				ambientModules.push_back(symbol);
				seen.insert(symbol);
			}
		}
	});
	return ambientModules;
}

// checker.go:15887 — resolveESModuleSymbol
Symbol* Checker::resolveESModuleSymbol(Symbol* moduleSymbol, Node* node,
									  Node* moduleSpecifier) {
	Symbol* symbol = resolveExternalModuleSymbol(moduleSymbol, true /*dontResolveAlias*/);
	if (isNonLocalAlias(symbol, SymbolFlagsValue | SymbolFlagsType | SymbolFlagsNamespace)) {
		// When the module has an export= with a pure alias, we transitively resolve
		// and propagate any typeOnlyDeclaration
		symbol = getMergedSymbol(
			resolveIndirectionAlias(getSymbolOfDeclaration(node), symbol));
	}
	if (symbol != nullptr) {
		Node* referenceParent = moduleSpecifier->parent;
		Node* namespaceImport = nullptr;
		if (isImportDeclaration(referenceParent)) {
			namespaceImport = getNamespaceDeclarationNode(referenceParent);
		}
		if (namespaceImport != nullptr || isImportCall(referenceParent)) {
			Node* reference;
			if (isImportCall(referenceParent)) {
				reference = referenceParent->arguments()[0];
			} else {
				reference = referenceParent->moduleSpecifier();
			}
			Type* typ = getTypeOfSymbol(symbol);
			Type* defaultOnlyType = getTypeWithSyntheticDefaultOnly(
				typ, symbol, moduleSymbol, reference,
				getImportAttributesTypeForModuleSpecifier(reference));
			if (defaultOnlyType != nullptr) {
				return cloneTypeAsModuleType(symbol, defaultOnlyType, referenceParent);
			}

			Node* targetFile = findOrNull(moduleSymbol->declarations,
										  [](Node* d) { return isSourceFile(d); });
			ResolutionMode usageMode =
				getEmitSyntaxForModuleSpecifierExpression(reference);
			Symbol* exportModuleDotExportsSymbol = nullptr;
			if (namespaceImport != nullptr && targetFile != nullptr &&
				ModuleKind::Node20 <= moduleKind && moduleKind <= ModuleKind::NodeNext &&
				usageMode == ModuleKind::CommonJS &&
				program->GetImpliedNodeFormatForEmit(
					static_cast<SourceFile*>(targetFile)) == ModuleKind::ESNext) {
				exportModuleDotExportsSymbol =
					getExportOfModule(symbol, InternalSymbolNameModuleExports,
									  namespaceImport, true /*dontResolveAlias*/);
			}
			if (exportModuleDotExportsSymbol != nullptr) {
				if (hasSignatures(typ)) {
					return cloneTypeAsModuleType(exportModuleDotExportsSymbol, typ,
												 referenceParent);
				}
				return exportModuleDotExportsSymbol;
			}

			bool isEsmCjsRef =
				targetFile != nullptr &&
				isESMFormatImportImportingCommonjsFormatFile(
					usageMode, program->GetImpliedNodeFormatForEmit(
								   static_cast<SourceFile*>(targetFile)));
			if (hasSignatures(typ) ||
				getPropertyOfTypeEx(typ, InternalSymbolNameDefault,
									true /*skipObjectFunctionPropertyAugment*/,
									false /*includeTypeOnlyMembers*/) != nullptr ||
				isEsmCjsRef) {
				Type* moduleType;
				if (typ->flags & TypeFlagsStructuredType) {
					moduleType = getTypeWithSyntheticDefaultImportType(
						typ, symbol, moduleSymbol, reference);
				} else {
					moduleType = createDefaultPropertyWrapperForModule(
						symbol, symbol->parent, nullptr);
				}
				return cloneTypeAsModuleType(symbol, moduleType, referenceParent);
			}
		}
	}
	return symbol;
}

// checker.go:15943 — hasSignatures
bool Checker::hasSignatures(Type* t) {
	return !getSignaturesOfStructuredType(t, SignatureKind::Call).empty() ||
		   !getSignaturesOfStructuredType(t, SignatureKind::Construct).empty();
}

// checker.go:15951 — getTypeWithSyntheticDefaultOnly
Type* Checker::getTypeWithSyntheticDefaultOnly(Type* t, Symbol* symbol,
											 Symbol* originalSymbol,
											 Node* moduleSpecifier,
											 Type* importAttributesType) {
	bool hasDefaultOnly =
		isOnlyImportableAsDefault(moduleSpecifier, nullptr, importAttributesType);
	if (hasDefaultOnly && t != nullptr && !isErrorType(t)) {
		CachedTypeKey key{CachedTypeKind::DefaultOnlyType, t->id};
		if (auto it = cachedTypes.find(key); it != cachedTypes.end()) {
			return it->second;
		}
		Type* result = createDefaultPropertyWrapperForModule(symbol, originalSymbol,
															 nullptr);
		cachedTypes[key] = result;
		return result;
	}
	return nullptr;
}

// checker.go:15965 — getTypeWithSyntheticDefaultImportType
Type* Checker::getTypeWithSyntheticDefaultImportType(Type* t, Symbol* symbol,
													 Symbol* originalSymbol,
													 Node* moduleSpecifier) {
	if (t != nullptr && !isErrorType(t)) {
		CachedTypeKey key{CachedTypeKind::SyntheticType, t->id};
		if (auto it = cachedTypes.find(key); it != cachedTypes.end()) {
			return it->second;
		}
		Node* file = findOrNull(originalSymbol->declarations,
								[](Node* d) { return isSourceFile(d); });
		bool hasSyntheticDefault = canHaveSyntheticDefault(
			file, originalSymbol, false /*dontResolveAlias*/, moduleSpecifier);
		Type* syntheticType;
		if (hasSyntheticDefault) {
			Symbol* anonymousSymbol =
				newSymbol(SymbolFlagsTypeLiteral, InternalSymbolNameType);
			anonymousSymbol->declarations = originalSymbol->declarations;
			Type* defaultContainingObject = createDefaultPropertyWrapperForModule(
				symbol, originalSymbol, anonymousSymbol);
			valueSymbolLinks.Get(anonymousSymbol)->resolvedType =
				defaultContainingObject;
			if (isValidSpreadType(t)) {
				syntheticType = getSpreadType(t, defaultContainingObject, anonymousSymbol,
											  (ObjectFlags)0 /*objectFlags*/,
											  false /*readonly*/);
			} else {
				syntheticType = defaultContainingObject;
			}
		} else {
			syntheticType = t;
		}
		cachedTypes[key] = syntheticType;
		return syntheticType;
	}
	return t;
}

// checker.go:16026 — createDefaultPropertyWrapperForModule
Type* Checker::createDefaultPropertyWrapperForModule(Symbol* symbol,
													 Symbol* originalSymbol,
													 Symbol* anonymousSymbol) {
	SymbolTable memberTable;
	Symbol* newSym = newSymbol(SymbolFlagsAlias, InternalSymbolNameDefault);
	newSym->parent = originalSymbol;
	valueSymbolLinks.Get(newSym)->nameType = getStringLiteralType("default");
	aliasSymbolLinks.Get(newSym)->aliasTarget = resolveSymbol(symbol);
	memberTable[InternalSymbolNameDefault] = newSym;
	if (anonymousSymbol == nullptr && originalSymbol != nullptr) {
		anonymousSymbol = newSymbol(SymbolFlagsObjectLiteral, InternalSymbolNameObject);
		anonymousSymbol->declarations = originalSymbol->declarations;
	}
	return newAnonymousType(anonymousSymbol, memberTable, {}, {}, {});
}

// checker.go:16040 — cloneTypeAsModuleType
Symbol* Checker::cloneTypeAsModuleType(Symbol* symbol, Type* moduleType,
									   Node* referenceParent) {
	Symbol* result = newSymbol(symbol->flags, symbol->name);
	result->declarations = symbol->declarations;
	result->valueDeclaration = symbol->valueDeclaration;
	result->members = symbol->members;
	result->exports = symbol->exports;
	result->parent = symbol->parent;
	ExportTypeLinks* links = exportTypeLinks.Get(result);
	links->target = symbol;
	links->originatingImport = referenceParent;
	StructuredType* resolvedModuleType = resolveStructuredTypeMembers(moduleType);
	valueSymbolLinks.Get(result)->resolvedType =
		newAnonymousType(result, resolvedModuleType->members, {}, {},
						 resolvedModuleType->indexInfos);
	return result;
}

// checker.go:16574 — ResolveAlias
std::pair<Symbol*, bool> Checker::ResolveAlias(Symbol* symbol) {
	if (symbol == nullptr) {
		return {nullptr, false};
	}
	Symbol* resolved = resolveAlias(symbol);
	return {resolved, resolved != unknownSymbol};
}

// checker.go:16630 — resolveAliasWithDeprecationCheck
Symbol* Checker::resolveAliasWithDeprecationCheck(Symbol* symbol, Node* location) {
	if (!(symbol->flags & SymbolFlagsAlias) || isDeprecatedSymbol(symbol) ||
		getDeclarationOfAliasSymbol(symbol) == nullptr) {
		return symbol;
	}
	Symbol* targetSymbol = resolveAlias(symbol);
	if (targetSymbol == unknownSymbol) {
		return targetSymbol;
	}
	while (symbol->flags & SymbolFlagsAlias) {
		Symbol* target = getImmediateAliasedSymbol(symbol);
		if (target != nullptr) {
			if (target == targetSymbol) {
				break;
			}
			if (!target->declarations.empty()) {
				if (isDeprecatedSymbol(target)) {
					addDeprecatedSuggestion(location, target->declarations, target->name);
					break;
				} else {
					if (symbol == targetSymbol) {
						break;
					}
					symbol = target;
				}
			}
		} else {
			break;
		}
	}
	return targetSymbol;
}

}  // namespace checker
}  // namespace tsc
