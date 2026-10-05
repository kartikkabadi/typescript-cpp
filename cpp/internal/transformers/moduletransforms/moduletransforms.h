// Port of tsc/internal/transformers/moduletransforms — package-level decls.
#pragma once

#include "internal/checker/checker.h" // tsc::EmitResolver
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

// --- impliedmodule.go -------------------------------------------------------

// NewImpliedModuleTransformer — impliedmodule.go:19
Transformer* newImpliedModuleTransformer(TransformOptions* opts);

// --- esmodule.go / commonjsmodule.go (owned by moduletransforms slice) ------

Transformer* newESModuleTransformer(TransformOptions* opts);
Transformer* newCommonJSModuleTransformer(TransformOptions* opts);

}  // namespace tsc::transformers::moduletransforms
