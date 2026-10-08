// Port of tsc/internal/transformers/moduletransforms — package-level decls.
#pragma once

#include "internal/checker/checker.h" // tsc::EmitResolver
#include "internal/collections/collections.h"
#include "internal/transformers/transformers.h"

namespace tsc::transformers::moduletransforms {

// isDeclarationNameOfEnumOrNamespace — utilities.go:12
bool isDeclarationNameOfEnumOrNamespace(printer::EmitContext* emitContext,
                                       Node* node);

// rewriteModuleSpecifier — utilities.go:22
Node* rewriteModuleSpecifier(printer::EmitContext* emitContext, Node* node,
                             const CompilerOptions* compilerOptions);

// createEmptyImports — utilities.go:36
Node* createEmptyImports(printer::NodeFactory* factory);

// getExternalModuleNameLiteral — utilities.go:53. `host` is Go's `any
// /*EmitHost*/` — currently unused (the file-level helpers it feeds are
// upstream `!!!` stubs); kept to preserve the signature.
Node* getExternalModuleNameLiteral(printer::NodeFactory* factory,
                                   Node* importNode, SourceFile* sourceFile,
                                   void* host,
                                   checker::EmitResolver* resolver,
                                   const CompilerOptions* compilerOptions);

// tryGetModuleNameFromFile — utilities.go:74 (upstream `!!!` stub)
Node* tryGetModuleNameFromFile(printer::NodeFactory* factory,
                               SourceFile* file, void* host,
                               const CompilerOptions* options);

// tryGetModuleNameFromDeclaration — utilities.go:85
Node* tryGetModuleNameFromDeclaration(Node* declaration, void* host,
                                      printer::NodeFactory* factory,
                                      checker::EmitResolver* resolver,
                                      const CompilerOptions* compilerOptions);

// getExternalModuleNameFromPath — utilities.go:93 (upstream `!!!` stub)
std::string getExternalModuleNameFromPath(void* host, std::string fileName,
                                          std::string referencePath);

// tryRenameExternalModule — utilities.go:100 (upstream `!!!` stub)
Node* tryRenameExternalModule(printer::NodeFactory* factory,
                              Node* moduleName, SourceFile* sourceFile);

// isFileLevelReservedGeneratedIdentifier — utilities.go:105
bool isFileLevelReservedGeneratedIdentifier(
	printer::EmitContext* emitContext, Node* name);

// isSimpleInlineableExpression — utilities.go:116. NB: differs from
// transformers::isSimpleInlineableExpression (this one requires NOT an
// identifier).
bool isSimpleInlineableExpression(Node* expression);

// ast/utilities.go replicas shared by the moduletransforms TUs (defined once in
// externalmoduleinfo.cpp; checker_moduletarget.cpp has its own file-local copy
// of getNamespaceDeclarationNode).
Node* getNamespaceDeclarationNode(Node* node);
bool isDefaultImport(
	Node* node /*ImportDeclaration | ImportEqualsDeclaration |
	              ExportDeclaration*/);

// --- externalmoduleinfo.go --------------------------------------------------

// externalModuleInfo — externalmoduleinfo.go:15. Information about the
// external module references and exports collected from a SourceFile.
struct externalModuleInfo {
	std::vector<Node*> externalImports; // ImportDeclaration |
	                                    // ImportEqualsDeclaration |
	                                    // ExportDeclaration. imports and
	                                    // reexports of other external modules
	collections::MultiMap<std::string, Node*>
		exportSpecifiers; // Maps local names to their associated export
	                      // specifiers (excludes reexports)
	collections::MultiMap<Node*, Node*>
		exportedBindings; // Maps local declarations to their associated
	                      // export aliases
	std::vector<Node*> exportedNames; // all exported names in the module, both
	                                  // local and re-exported, excluding the
	                                  // names of locally exported function
	                                  // declarations
	printer::OrderedSet<Node*>
		exportedFunctions; // all of the top-level exported function
	                       // declarations
	Node* exportEquals =
		nullptr; // an export=/module.exports= declaration if one was present
	bool hasExportStarsToExportValues =
		false; // whether this module contains export*
};

// collectExternalModuleInfo — externalmoduleinfo.go:35
externalModuleInfo* collectExternalModuleInfo(
	SourceFile* sourceFile, const CompilerOptions* compilerOptions,
	printer::EmitContext* emitContext, binder::ReferenceResolver* resolver);

// createExternalHelpersImportDeclarationIfNeeded — externalmoduleinfo.go:262
Node* createExternalHelpersImportDeclarationIfNeeded(
	printer::EmitContext* emitContext, SourceFile* sourceFile,
	const CompilerOptions* compilerOptions, ModuleKind fileModuleKind,
	bool hasExportStarsToExportValues, bool hasImportStar,
	bool hasImportDefault);

// getExportNeedsImportStarHelper — externalmoduleinfo.go:356
bool getExportNeedsImportStarHelper(ExportDeclaration* node);

// getImportNeedsImportStarHelper — externalmoduleinfo.go:360
bool getImportNeedsImportStarHelper(ImportDeclaration* node);

// getImportNeedsImportDefaultHelper — externalmoduleinfo.go:385
bool getImportNeedsImportDefaultHelper(ImportDeclaration* node);

// --- impliedmodule.go -------------------------------------------------------

// NewImpliedModuleTransformer — impliedmodule.go:19
Transformer* NewImpliedModuleTransformer(TransformOptions* opts);

// --- esmodule.go / commonjsmodule.go (owned by moduletransforms slice) ------

Transformer* NewESModuleTransformer(TransformOptions* opts);
Transformer* NewCommonJSModuleTransformer(TransformOptions* opts);

}  // namespace tsc::transformers::moduletransforms
