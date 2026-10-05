// Port of tsc/internal/transformers/moduletransforms/utilities.go.
#include "internal/transformers/moduletransforms/moduletransforms.h"

#include "internal/core/utilities.h"
#include "internal/outputpaths/outputpaths.h"
#include "internal/tspath/tspath.h"

namespace tsc::transformers::moduletransforms {

// isDeclarationNameOfEnumOrNamespace — utilities.go:12
bool isDeclarationNameOfEnumOrNamespace(printer::EmitContext* emitContext,
                                       Node* node) {
	Node* original = emitContext->mostOriginal(node);
	if (original != nullptr && original->parent != nullptr) {
		switch (original->parent->kind) {
		case Kind::EnumDeclaration:
		case Kind::ModuleDeclaration:
			return original == original->parent->name();
		default:
			break;
		}
	}
	return false;
}

// rewriteModuleSpecifier — utilities.go:22
Node* rewriteModuleSpecifier(printer::EmitContext* emitContext, Node* node,
                             const CompilerOptions* compilerOptions) {
	if (node == nullptr || !isStringLiteral(node) ||
	    !tsc::shouldRewriteModuleSpecifier(node->text(), compilerOptions)) {
		return node;
	}
	std::string updatedText(tspath::changeExtension(
		node->text(),
		outputpaths::GetOutputExtension(node->text(), compilerOptions->Jsx)));
	if (updatedText != node->text()) {
		Node* updated = emitContext->factory.newStringLiteral(
			updatedText, node->as<StringLiteral>()->TokenFlags);
		emitContext->setOriginal(updated, node);
		emitContext->assignCommentAndSourceMapRanges(updated, node);
		return updated;
	}
	return node;
}

// createEmptyImports — utilities.go:36
Node* createEmptyImports(printer::NodeFactory* factory) {
	return factory->newExportDeclaration(
		nullptr, /*modifiers*/
		false,   /*isTypeOnly*/
		factory->newNamedExports(factory->newNodeList({})),
		nullptr, /*moduleSpecifier*/
		nullptr  /*attributes*/
	);
}

// getExternalModuleNameLiteral — utilities.go:53
Node* getExternalModuleNameLiteral(printer::NodeFactory* factory,
                                   Node* importNode, SourceFile* sourceFile,
                                   void* host,
                                   checker::EmitResolver* resolver,
                                   const CompilerOptions* compilerOptions) {
	Node* moduleName = getExternalModuleName(importNode);
	if (moduleName != nullptr && isStringLiteral(moduleName)) {
		Node* name = tryGetModuleNameFromDeclaration(
			importNode, host, factory, resolver, compilerOptions);
		if (name == nullptr) {
			name = tryRenameExternalModule(factory, moduleName, sourceFile);
		}
		if (name == nullptr) { // !!! propagate token flags (will produce new diffs)
			name = factory->newStringLiteral(moduleName->text(),
			                                 TokenFlagsNone);
		}
		return name;
	}
	return nullptr;
}

// tryGetModuleNameFromFile — utilities.go:74 (upstream `!!!` stub)
Node* tryGetModuleNameFromFile(printer::NodeFactory* /*factory*/,
                               SourceFile* /*file*/, void* /*host*/,
                               const CompilerOptions* /*options*/) {
	// !!!
	// if file.moduleName {
	// 	return factory.createStringLiteral(file.moduleName)
	// }
	return nullptr;
}

// tryGetModuleNameFromDeclaration — utilities.go:85
Node* tryGetModuleNameFromDeclaration(Node* declaration, void* host,
                                      printer::NodeFactory* factory,
                                      checker::EmitResolver* resolver,
                                      const CompilerOptions* compilerOptions) {
	if (resolver == nullptr) {
		return nullptr;
	}
	return tryGetModuleNameFromFile(
		factory, resolver->GetExternalModuleFileFromDeclaration(declaration),
		host, compilerOptions);
}

// getExternalModuleNameFromPath — utilities.go:93 (upstream `!!!` stub)
std::string getExternalModuleNameFromPath(void* /*host*/,
                                          std::string /*fileName*/,
                                          std::string /*referencePath*/) {
	// !!!
	return "";
}

// tryRenameExternalModule — utilities.go:100 (upstream `!!!` stub)
Node* tryRenameExternalModule(printer::NodeFactory* /*factory*/,
                              Node* /*moduleName*/,
                              SourceFile* /*sourceFile*/) {
	// !!!
	return nullptr;
}

// isFileLevelReservedGeneratedIdentifier — utilities.go:105
bool isFileLevelReservedGeneratedIdentifier(
	printer::EmitContext* emitContext, Node* name) {
	printer::AutoGenerateInfo* info = emitContext->getAutoGenerateInfo(name);
	return info != nullptr &&
	       printer::generatedIdentifierFlagsIsFileLevel(info->Flags) &&
	       printer::generatedIdentifierFlagsIsOptimistic(info->Flags) &&
	       printer::generatedIdentifierFlagsIsReservedInNestedScopes(
		       info->Flags);
}

// isSimpleInlineableExpression — utilities.go:116
bool isSimpleInlineableExpression(Node* expression) {
	return !isIdentifier(expression) &&
	       transformers::isSimpleCopiableExpression(expression);
}

}  // namespace tsc::transformers::moduletransforms
