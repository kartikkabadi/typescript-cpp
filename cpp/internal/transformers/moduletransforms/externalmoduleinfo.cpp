// Port of tsc/internal/transformers/moduletransforms/externalmoduleinfo.go —
// externalModuleInfo struct + the collectExternalModuleInfo worker, plus the
// shared import/export helper predicates the module transformers use.
#include "internal/transformers/moduletransforms/moduletransforms.h"

#include <algorithm>

#include "internal/stringutil/stringutil.h"

namespace tsc::transformers::moduletransforms {

static bool containsDefaultReference(Node* node);

// ---------------------------------------------------------------------------
// ast/utilities.go replicas — shared by the moduletransforms TUs (declared in
// moduletransforms.h); the checker slice keeps its own file-local copy of
// getNamespaceDeclarationNode.
// ---------------------------------------------------------------------------

// ast/utilities.go:2568 — GetNamespaceDeclarationNode
Node* getNamespaceDeclarationNode(Node* node) {
	switch (node->kind) {
	case Kind::ImportDeclaration:
	case Kind::JSImportDeclaration: {
		Node* importClause = node->importClause();
		if (importClause != nullptr &&
			importClause->as<ImportClause>()->NamedBindings != nullptr &&
			isNamespaceImport(
				importClause->as<ImportClause>()->NamedBindings)) {
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
		TSC_UNREACHABLE("Unhandled case in getNamespaceDeclarationNode");
	}
	return nullptr;
}

// ast/utilities.go:2592 — IsDefaultImport
bool isDefaultImport(Node* node /*ImportDeclaration | ImportEqualsDeclaration |
                                 ExportDeclaration*/) {
	switch (node->kind) {
	case Kind::ImportDeclaration:
	case Kind::JSImportDeclaration: {
		Node* importClause = node->importClause();
		return importClause != nullptr &&
		       importClause->as<ImportClause>()->name != nullptr;
	}
	default:
		break;
	}
	return false;
}

// ---------------------------------------------------------------------------
// externalmoduleinfo.go
// ---------------------------------------------------------------------------

// externalModuleInfoCollector — externalmoduleinfo.go:25
struct externalModuleInfoCollector {
	SourceFile* sourceFile = nullptr;
	const CompilerOptions* compilerOptions = nullptr;
	printer::EmitContext* emitContext = nullptr;
	binder::ReferenceResolver* resolver = nullptr;
	collections::Set<std::string> uniqueExports;
	bool hasExportDefault = false;
	externalModuleInfo* output = nullptr;

	externalModuleInfo* collect();
	bool addUniqueExport(std::string name);
	void addExportedBinding(Node* decl, Node* name);
	void addExternalImport(
		Node* node /*ImportDeclaration | ImportEqualsDeclaration |
	               ExportDeclaration*/);
	void addExportedName(Node* name);
	void addExportedNamesForExportDeclaration(ExportDeclaration* node);
	void addExportedFunctionDeclaration(FunctionDeclaration* node, Node* name,
	                                    bool isDefault);
	void collectExportedVariableInfo(
		Node* decl /*VariableDeclaration | BindingElement*/);
};

// collectExternalModuleInfo — externalmoduleinfo.go:35
externalModuleInfo* collectExternalModuleInfo(
	SourceFile* sourceFile, const CompilerOptions* compilerOptions,
	printer::EmitContext* emitContext, binder::ReferenceResolver* resolver) {
	externalModuleInfoCollector c;
	c.sourceFile = sourceFile;
	c.compilerOptions = compilerOptions;
	c.emitContext = emitContext;
	c.resolver = resolver;
	c.output = new externalModuleInfo();
	return c.collect();
}

// collect — externalmoduleinfo.go:46
externalModuleInfo* externalModuleInfoCollector::collect() {
	bool hasImportStar = false;
	bool hasImportDefault = false;
	for (Node* node : sourceFile->Statements->nodes) {
		// Look through NotEmittedStatement to find elided export= declarations
		// (e.g., `declare export = x` is elided by the type eraser but must
		// still be collected)
		if (isNotEmittedStatement(node)) {
			Node* original = emitContext->mostOriginal(node);
			if (original != nullptr && isExportAssignment(original)) {
				auto* n = original->as<ExportAssignment>();
				if (n->IsExportEquals && output->exportEquals == nullptr) {
					output->exportEquals = n;
				}
			}
			continue;
		}
		switch (node->kind) {
		case Kind::ImportDeclaration: {
			// import "mod"
			// import x from "mod"
			// import * as x from "mod"
			// import { x, y } from "mod"
			auto* n = node->as<ImportDeclaration>();
			addExternalImport(node);
			if (!hasImportStar && getImportNeedsImportStarHelper(n)) {
				hasImportStar = true;
			}
			if (!hasImportDefault && getImportNeedsImportDefaultHelper(n)) {
				hasImportDefault = true;
			}
			break;
		}
		case Kind::ImportEqualsDeclaration: {
			auto* n = node->as<ImportEqualsDeclaration>();
			if (isExternalModuleReference(n->ModuleReference)) {
				// import x = require("mod")
				addExternalImport(node);
			}
			break;
		}
		case Kind::ExportDeclaration: {
			auto* n = node->as<ExportDeclaration>();
			if (n->ModuleSpecifier != nullptr) {
				// export * from "mod"
				// export * as ns from "mod"
				// export { x, y } from "mod"
				addExternalImport(node);
				if (n->ExportClause == nullptr) {
					// export * from "mod"
					output->hasExportStarsToExportValues = true;
				} else if (isNamedExports(n->ExportClause)) {
					// export { x, y } from "mod"
					addExportedNamesForExportDeclaration(n);
					if (!hasImportDefault) {
						hasImportDefault =
							containsDefaultReference(n->ExportClause);
					}
				} else {
					// export * as ns from "mod"
					Node* name = n->ExportClause->as<NamespaceExport>()->name;
					std::string nameText = name->text();
					if (addUniqueExport(nameText)) {
						addExportedBinding(node, name);
						addExportedName(name);
					}
					// we use the same helpers for `export * as ns` as we do
					// for `import * as ns`
					hasImportStar = true;
				}
			} else {
				// export { x, y }
				addExportedNamesForExportDeclaration(
					node->as<ExportDeclaration>());
			}
			break;
		}
		case Kind::ExportAssignment: {
			auto* n = node->as<ExportAssignment>();
			if (n->IsExportEquals && output->exportEquals == nullptr) {
				// export = x
				output->exportEquals = n;
			}
			break;
		}
		case Kind::VariableStatement: {
			auto* n = node->as<VariableStatement>();
			if (hasSyntacticModifier(node, ModifierFlagsExport)) {
				for (Node* decl :
				     n->DeclarationList->as<VariableDeclarationList>()
					     ->Declarations->nodes) {
					collectExportedVariableInfo(decl);
				}
			}
			break;
		}
		case Kind::FunctionDeclaration: {
			auto* n = node->as<FunctionDeclaration>();
			if (hasSyntacticModifier(node, ModifierFlagsExport)) {
				addExportedFunctionDeclaration(
					n, nullptr /*name*/,
					hasSyntacticModifier(node, ModifierFlagsDefault));
			}
			break;
		}
		case Kind::ClassDeclaration: {
			auto* n = node->as<ClassDeclaration>();
			if (hasSyntacticModifier(node, ModifierFlagsExport)) {
				if (hasSyntacticModifier(node, ModifierFlagsDefault)) {
					// export default class { }
					if (!hasExportDefault) {
						Node* name = n->name;
						if (name == nullptr) {
							name = emitContext->factory
								.newGeneratedNameForNode(node);
						}
						addExportedBinding(node, name);
						hasExportDefault = true;
					}
				} else {
					// export class x { }
					Node* name = n->name;
					if (name != nullptr) {
						if (addUniqueExport(name->text())) {
							addExportedBinding(node, name);
							addExportedName(name);
						}
					}
				}
			}
			break;
		}
		default:
			break;
		}
	}

	return output;
}

// addUniqueExport — externalmoduleinfo.go:167
bool externalModuleInfoCollector::addUniqueExport(std::string name) {
	if (!uniqueExports.Has(name)) {
		uniqueExports.Add(name);
		return true;
	}
	return false;
}

// addExportedBinding — externalmoduleinfo.go:175
void externalModuleInfoCollector::addExportedBinding(Node* decl, Node* name) {
	output->exportedBindings.Add(emitContext->mostOriginal(decl), name);
}

// addExternalImport — externalmoduleinfo.go:179
void externalModuleInfoCollector::addExternalImport(Node* node) {
	output->externalImports.push_back(node);
}

// addExportedName — externalmoduleinfo.go:183
void externalModuleInfoCollector::addExportedName(Node* name) {
	output->exportedNames.push_back(name);
}

// addExportedNamesForExportDeclaration — externalmoduleinfo.go:187
void externalModuleInfoCollector::addExportedNamesForExportDeclaration(
	ExportDeclaration* node) {
	for (Node* specifier : node->ExportClause->elements()) {
		std::string specifierNameText = specifier->name()->text();
		if (addUniqueExport(specifierNameText)) {
			Node* name = specifier->propertyNameOrName();
			if (name->kind != Kind::StringLiteral) {
				if (node->ModuleSpecifier == nullptr) {
					output->exportSpecifiers.Add(name->text(),
					                             specifier->as<ExportSpecifier>());
				}

				Node* decl = resolver->GetReferencedImportDeclaration(
					emitContext->mostOriginal(name));
				if (decl == nullptr) {
					decl = resolver->GetReferencedValueDeclaration(
						emitContext->mostOriginal(name));
				}
				if (decl != nullptr) {
					if (decl->kind == Kind::FunctionDeclaration) {
						uniqueExports.Delete(specifierNameText);
						addExportedFunctionDeclaration(
							decl->as<FunctionDeclaration>(), specifier->name(),
							moduleExportNameIsDefault(specifier->name()));
						continue;
					}
					addExportedBinding(decl, specifier->name());
				}
			}

			addExportedName(specifier->name());
		}
	}
}

// addExportedFunctionDeclaration — externalmoduleinfo.go:216
void externalModuleInfoCollector::addExportedFunctionDeclaration(
	FunctionDeclaration* node, Node* name, bool isDefault) {
	output->exportedFunctions.add(emitContext->mostOriginal(node->asNode()));
	if (isDefault) {
		// export default function() { }
		// function x() { } + export { x as default };
		if (!hasExportDefault) {
			if (name == nullptr) {
				name = emitContext->factory.newGeneratedNameForNode(
					node->asNode());
			}
			addExportedBinding(node->asNode(), name);
			hasExportDefault = true;
		}
	} else {
		// export function x() { }
		// function x() { } + export { x }
		if (name == nullptr) {
			name = node->name;
		}
		std::string nameText = name->text();
		if (addUniqueExport(nameText)) {
			addExportedBinding(node->asNode(), name);
		}
	}
}

// collectExportedVariableInfo — externalmoduleinfo.go:241
void externalModuleInfoCollector::collectExportedVariableInfo(
	Node* decl /*VariableDeclaration | BindingElement*/) {
	if (isBindingPattern(decl->name())) {
		for (Node* element : decl->name()->elements()) {
			auto* e = element->as<BindingElement>();
			if (e->name != nullptr) {
				collectExportedVariableInfo(element);
			}
		}
	} else if (!emitContext->hasAutoGenerateInfo(decl->name())) {
		std::string text = decl->name()->text();
		if (addUniqueExport(text)) {
			addExportedName(decl->name());
			if (transformers::isLocalName(emitContext, decl->name())) {
				addExportedBinding(decl, decl->name());
			}
		}
	}
}

// externalHelpersModuleNameText — externalmoduleinfo.go:260
static const char* externalHelpersModuleNameText = "tslib";

// getImportedHelpers — externalmoduleinfo.go:320
static std::vector<printer::EmitHelper*> getImportedHelpers(
	printer::EmitContext* emitContext, SourceFile* sourceFile) {
	std::vector<printer::EmitHelper*> helpers;
	for (printer::EmitHelper* helper :
	     emitContext->getEmitHelpers(sourceFile->asNode())) {
		if (!helper->Scoped) {
			helpers.push_back(helper);
		}
	}
	return helpers;
}

// getOrCreateExternalHelpersModuleNameIfNeeded — externalmoduleinfo.go:330
static Node* getOrCreateExternalHelpersModuleNameIfNeeded(
	printer::EmitContext* emitContext, SourceFile* node,
	const CompilerOptions* compilerOptions,
	std::vector<printer::EmitHelper*> helpers, bool hasExportStarsToExportValues,
	bool hasImportStarOrImportDefault, ModuleKind fileModuleKind) {
	Node* externalHelpersModuleName =
		emitContext->getExternalHelpersModuleName(node);
	if (externalHelpersModuleName != nullptr) {
		return externalHelpersModuleName;
	}

	bool create = !helpers.empty() ||
	              ((hasExportStarsToExportValues ||
	                hasImportStarOrImportDefault) &&
	               fileModuleKind < ModuleKind::System);

	if (create) {
		externalHelpersModuleName =
			emitContext->factory.newUniqueName(externalHelpersModuleNameText);
		emitContext->setExternalHelpersModuleName(node,
	                                          externalHelpersModuleName);
	}

	return externalHelpersModuleName;
}

// createExternalHelpersImportDeclarationIfNeeded — externalmoduleinfo.go:262
Node* createExternalHelpersImportDeclarationIfNeeded(
	printer::EmitContext* emitContext, SourceFile* sourceFile,
	const CompilerOptions* compilerOptions, ModuleKind fileModuleKind,
	bool hasExportStarsToExportValues, bool hasImportStar,
	bool hasImportDefault) {
	if (tristateIsTrue(compilerOptions->ImportHelpers) &&
	    isEffectiveExternalModule(sourceFile, compilerOptions)) {
		ModuleKind moduleKind = compilerOptions->GetEmitModuleKind();
		std::vector<printer::EmitHelper*> helpers =
			getImportedHelpers(emitContext, sourceFile);
		if (fileModuleKind == ModuleKind::CommonJS ||
		    (fileModuleKind == ModuleKind::None &&
		     moduleKind == ModuleKind::CommonJS)) {
			// When we emit to a non-ES module, generate a synthetic `import
			// tslib = require("tslib")` to be further transformed.
			Node* externalHelpersModuleName =
				getOrCreateExternalHelpersModuleNameIfNeeded(
					emitContext, sourceFile, compilerOptions, helpers,
					hasExportStarsToExportValues,
					hasImportStar || hasImportDefault, fileModuleKind);
			if (externalHelpersModuleName != nullptr) {
				Node* externalHelpersImportDeclaration =
					emitContext->factory.newImportEqualsDeclaration(
						nullptr, /*modifiers*/
						false,   /*isTypeOnly*/
						externalHelpersModuleName,
						emitContext->factory.newExternalModuleReference(
							emitContext->factory.newStringLiteral(
								externalHelpersModuleNameText,
								TokenFlagsNone)));
				emitContext->addEmitFlags(externalHelpersImportDeclaration,
				                          printer::EFCustomPrologue);
				return externalHelpersImportDeclaration;
			}
		} else {
			// When we emit as an ES module, generate an `import` declaration
			// that uses named imports for helpers.
			// If we cannot determine the implied module kind under `module:
			// preserve` we assume ESM.
			std::vector<std::string> helperNames;
			for (printer::EmitHelper* helper : helpers) {
				std::string importName = helper->ImportName;
				if (!importName.empty()) {
					printer::appendIfUnique(helperNames, importName);
				}
			}
			if (!helperNames.empty()) {
				std::sort(helperNames.begin(), helperNames.end(),
				          [](const std::string& a, const std::string& b) {
					          return stringutil::CompareStringsCaseSensitive(
						                 a, b) < stringutil::ComparisonEqual;
				          });
				// Alias the imports if the names are used somewhere in the
				// file.
				// NOTE: We don't need to care about global import collisions
				// as this is a module.

				std::vector<Node*> importSpecifiers;
				for (const std::string& name : helperNames) {
					if (emitContext->isFileLevelUniqueName(
						    sourceFile, name, {} /*hasGlobalName*/)) {
						importSpecifiers.push_back(
							emitContext->factory.newImportSpecifier(
								false /*isTypeOnly*/,
								nullptr /*propertyName*/,
								emitContext->factory.newIdentifier(name)));
					} else {
						importSpecifiers.push_back(
							emitContext->factory.newImportSpecifier(
								false /*isTypeOnly*/,
								emitContext->factory.newIdentifier(name),
								emitContext->factory.newUnscopedHelperName(
									name)));
					}
				}
				Node* namedBindings = emitContext->factory.newNamedImports(
					emitContext->factory.newNodeList(importSpecifiers));
				Node* parseNode =
					emitContext->mostOriginal(sourceFile->asNode());
				emitContext->addEmitFlags(parseNode,
				                          printer::EFExternalHelpers);

				Node* externalHelpersImportDeclaration =
					emitContext->factory.newImportDeclaration(
						nullptr, /*modifiers*/
						emitContext->factory.newImportClause(
							Kind::Unknown /*phaseModifier*/,
							nullptr /*name*/, namedBindings),
						emitContext->factory.newStringLiteral(
							externalHelpersModuleNameText, TokenFlagsNone),
						nullptr /*attributes*/);

				emitContext->addEmitFlags(externalHelpersImportDeclaration,
				                          printer::EFCustomPrologue);
				return externalHelpersImportDeclaration;
			}
		}
	}
	return nullptr;
}

// isNamedDefaultReference — externalmoduleinfo.go:348
static bool isNamedDefaultReference(
	Node* e /*ImportSpecifier | ExportSpecifier*/) {
	return moduleExportNameIsDefault(e->propertyNameOrName());
}

// containsDefaultReference — externalmoduleinfo.go:352
static bool containsDefaultReference(
	Node* node /*NamedImportBindings | NamedExportBindings*/) {
	if (node == nullptr ||
	    (!isNamedImports(node) && !isNamedExports(node))) {
		return false;
	}
	for (Node* e : node->elements()) {
		if (isNamedDefaultReference(e)) {
			return true;
		}
	}
	return false;
}

// getExportNeedsImportStarHelper — externalmoduleinfo.go:356
bool getExportNeedsImportStarHelper(ExportDeclaration* node) {
	return getNamespaceDeclarationNode(node->asNode()) != nullptr;
}

// getImportNeedsImportStarHelper — externalmoduleinfo.go:360
bool getImportNeedsImportStarHelper(ImportDeclaration* node) {
	if (getNamespaceDeclarationNode(node->asNode()) != nullptr) {
		return true;
	}
	if (node->ImportClause == nullptr) {
		return false;
	}
	Node* bindings = node->ImportClause->as<ImportClause>()->NamedBindings;
	if (bindings == nullptr) {
		return false;
	}
	if (!isNamedImports(bindings)) {
		return false;
	}
	auto* namedImports = bindings->as<NamedImports>();
	int defaultRefCount = 0;
	for (Node* binding : namedImports->Elements->nodes) {
		if (isNamedDefaultReference(binding)) {
			defaultRefCount++;
		}
	}
	// Import star is required if there's default named refs mixed with
	// non-default refs, or if theres non-default refs and it has a default
	// import
	return (defaultRefCount > 0 &&
	        defaultRefCount != (int)namedImports->Elements->nodes.size()) ||
	       (((int)namedImports->Elements->nodes.size() - defaultRefCount) != 0 &&
	        isDefaultImport(node->asNode()));
}

// getImportNeedsImportDefaultHelper — externalmoduleinfo.go:385
bool getImportNeedsImportDefaultHelper(ImportDeclaration* node) {
	// Import default is needed if there's a default import or a default ref
	// and no other refs (meaning an import star helper wasn't requested)
	return !getImportNeedsImportStarHelper(node) &&
	       (isDefaultImport(node->asNode()) ||
	        (node->ImportClause != nullptr &&
	         isNamedImports(
		         node->ImportClause->as<ImportClause>()->NamedBindings) &&
	         containsDefaultReference(
		         node->ImportClause->as<ImportClause>()->NamedBindings)));
}

}  // namespace tsc::transformers::moduletransforms
