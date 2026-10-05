// Port of tsc/internal/transformers/moduletransforms/esmodule.go —
// ESModuleTransformer.
#include "internal/transformers/moduletransforms/moduletransforms.h"

namespace tsc::transformers::moduletransforms {

namespace {

// importRequireStatements — esmodule.go:23
struct importRequireStatements {
	std::vector<Node*> statements;
	Node* requireHelperName = nullptr;
};

// ast/utilities.go:1703 — IsExternalModuleIndicator (file-local replica)
bool isExternalModuleIndicator(Node* node) {
	// Exported top-level member indicates moduleness
	return isAnyImportOrReExport(node) || isExportAssignment(node) ||
	       hasSyntacticModifier(node, ModifierFlagsExport);
}

// ast/utilities.go:1708 — IsExportNamespaceAsDefaultDeclaration (file-local
// replica)
bool isExportNamespaceAsDefaultDeclaration(Node* node) {
	if (isExportDeclaration(node)) {
		auto* decl = node->as<ExportDeclaration>();
		return isNamespaceExport(decl->ExportClause) &&
		       moduleExportNameIsDefault(
			       decl->ExportClause->as<NamespaceExport>()->name);
	}
	return false;
}

// ESModuleTransformer — esmodule.go:13
struct ESModuleTransformer : Transformer {
	const CompilerOptions* compilerOptions = nullptr;
	binder::ReferenceResolver* resolver = nullptr;
	std::function<ModuleKind(SourceFile*)> getEmitModuleFormatOfFile;
	SourceFile* currentSourceFile = nullptr;
	importRequireStatements* importRequireStatements_ = nullptr;
	std::unordered_map<std::string, Node*> helperNameSubstitutions;

	Node* visit(Node* node);
	SourceFile* visitSourceFile(SourceFile* node);
	Node* visitImportDeclaration(ImportDeclaration* node);
	Node* visitImportEqualsDeclaration(ImportEqualsDeclaration* node);
	std::vector<Node*> appendExportsOfImportEqualsDeclaration(
		std::vector<Node*> statements, ImportEqualsDeclaration* node);
	Node* visitExportAssignment(ExportAssignment* node);
	Node* visitExportDeclaration(ExportDeclaration* node);
	Node* visitCallExpression(CallExpression* node);
	Node* visitImportOrRequireCall(CallExpression* node);
	Node* createRequireCall(Node* node);
};

}  // namespace

// NewESModuleTransformer — esmodule.go:28
Transformer* NewESModuleTransformer(TransformOptions* opts) {
	const CompilerOptions* compilerOptions = opts->CompilerOptions;
	auto* tx = new ESModuleTransformer();
	tx->compilerOptions = compilerOptions;
	tx->resolver = opts->Resolver;
	tx->getEmitModuleFormatOfFile = opts->GetEmitModuleFormatOfFile;
	return tx->newTransformer([tx](Node* node) { return tx->visit(node); },
	                          opts->Context);
}

// Visits source elements that are not top-level or top-level nested
// statements. — esmodule.go:35
Node* ESModuleTransformer::visit(Node* node) {
	switch (node->kind) {
	case Kind::SourceFile:
		node = visitSourceFile(node->as<SourceFile>());
		break;
	case Kind::ImportDeclaration:
		node = visitImportDeclaration(node->as<ImportDeclaration>());
		break;
	case Kind::ImportEqualsDeclaration:
		node = visitImportEqualsDeclaration(node->as<ImportEqualsDeclaration>());
		break;
	case Kind::ExportAssignment:
		node = visitExportAssignment(node->as<ExportAssignment>());
		break;
	case Kind::ExportDeclaration:
		node = visitExportDeclaration(node->as<ExportDeclaration>());
		break;
	case Kind::CallExpression:
		node = visitCallExpression(node->as<CallExpression>());
		break;
	default:
		node = visitor()->visitEachChild(node);
		break;
	}
	return node;
}

// visitSourceFile — esmodule.go:55
SourceFile* ESModuleTransformer::visitSourceFile(SourceFile* node) {
	if (node->IsDeclarationFile ||
	    !(isExternalModule(node) ||
	      compilerOptions->GetIsolatedModules())) {
		return node;
	}

	currentSourceFile = node;
	importRequireStatements_ = nullptr;

	SourceFile* result =
		visitor()->visitEachChild(node->asNode())->as<SourceFile>();
	for (printer::EmitHelper* helper : emitContext()->readEmitHelpers()) {
		emitContext()->addEmitHelper(result->asNode(), helper);
	}

	Node* externalHelpersImportDeclaration =
		createExternalHelpersImportDeclarationIfNeeded(
			emitContext(), result, compilerOptions,
			getEmitModuleFormatOfFile(node),
			false /*hasExportStarsToExportValues*/, false /*hasImportStar*/,
			false /*hasImportDefault*/);
	if (externalHelpersImportDeclaration != nullptr ||
	    importRequireStatements_ != nullptr) {
		auto [prologue, rest] =
			factory()->splitStandardPrologue(result->Statements->nodes);
		auto [custom, rest2] = factory()->splitCustomPrologue(rest);
		std::vector<Node*> statements = prologue;
		statements.insert(statements.end(), custom.begin(), custom.end());
		if (externalHelpersImportDeclaration != nullptr) {
			// The helpers import must be visited so that `import x =
			// require("tslib")` (TypeScript-only syntax) is transformed to
			// `const x = require("tslib")` for CJS output files via
			// visitImportEqualsDeclaration.
			statements.push_back(
				visitor()->visitNode(externalHelpersImportDeclaration));
		}
		if (importRequireStatements_ != nullptr) {
			statements.insert(statements.end(),
			                  importRequireStatements_->statements.begin(),
			                  importRequireStatements_->statements.end());
		}
		statements.insert(statements.end(), rest2.begin(), rest2.end());
		NodeList* statementList = factory()->newNodeList(statements);
		statementList->loc = result->Statements->loc;
		result = factory()
		             ->updateSourceFile(result, statementList,
		                                node->EndOfFileToken)
		             ->as<SourceFile>();
	}

	if (isExternalModule(result) &&
	    compilerOptions->GetEmitModuleKind() != ModuleKind::Preserve) {
		bool hasIndicator = false;
		for (Node* statement : result->Statements->nodes) {
			if (isExternalModuleIndicator(statement)) {
				hasIndicator = true;
				break;
			}
		}
		if (!hasIndicator) {
			std::vector<Node*> statements = result->Statements->nodes;
			statements.push_back(createEmptyImports(factory()));
			NodeList* statementList = factory()->newNodeList(statements);
			statementList->loc = result->Statements->loc;
			result = factory()
			             ->updateSourceFile(result, statementList,
			                                node->EndOfFileToken)
			             ->as<SourceFile>();
		}
	}

	importRequireStatements_ = nullptr;
	currentSourceFile = nullptr;
	return result;
}

// visitImportDeclaration — esmodule.go:103
Node* ESModuleTransformer::visitImportDeclaration(ImportDeclaration* node) {
	if (!tristateIsTrue(compilerOptions->RewriteRelativeImportExtensions)) {
		return node->asNode();
	}
	Node* updatedModuleSpecifier = rewriteModuleSpecifier(
		emitContext(), node->ModuleSpecifier, compilerOptions);
	return factory()->updateImportDeclaration(
		node, nullptr /*modifiers*/, visitor()->visitNode(node->ImportClause),
		updatedModuleSpecifier, visitor()->visitNode(node->Attributes));
}

// visitImportEqualsDeclaration — esmodule.go:117
Node* ESModuleTransformer::visitImportEqualsDeclaration(
	ImportEqualsDeclaration* node) {
	// Though an error in es2020 modules, in node-flavor es2020 modules, we
	// can helpfully transform this to a synthetic `require` call
	// To give easy access to a synchronous `require` in node-flavor esm. We
	// do the transform even in scenarios where we error, but `import.meta.url`
	// is available, just because the output is reasonable for a node-like
	// runtime.
	if (compilerOptions->GetEmitModuleKind() < ModuleKind::Node16) {
		return nullptr;
	}

	if (!isExternalModuleImportEqualsDeclaration(node->asNode())) {
		TSC_UNREACHABLE(
			"import= for internal module references should be handled in an "
			"earlier transformer.");
	}

	// NOTE: C++ argument evaluation order is unspecified — hoist the mutating
	// createRequireCall into a named local.
	Node* requireCall = createRequireCall(node->asNode());
	Node* varStatement = factory()->newVariableStatement(
		nullptr /*modifiers*/,
		factory()->newVariableDeclarationList(
			factory()->newNodeList({factory()->newVariableDeclaration(
				node->name->clone(*factory()), nullptr /*exclamationToken*/,
				nullptr /*type*/, requireCall)}),
			NodeFlagsConst));
	emitContext()->setOriginal(varStatement, node->asNode());
	emitContext()->assignCommentAndSourceMapRanges(varStatement,
	                                             node->asNode());

	std::vector<Node*> statements;
	statements.push_back(varStatement);
	statements = appendExportsOfImportEqualsDeclaration(statements, node);
	return transformers::singleOrMany(statements, factory());
}

// appendExportsOfImportEqualsDeclaration — esmodule.go:152
std::vector<Node*>
ESModuleTransformer::appendExportsOfImportEqualsDeclaration(
	std::vector<Node*> statements, ImportEqualsDeclaration* node) {
	if (hasSyntacticModifier(node->asNode(), ModifierFlagsExport)) {
		statements.push_back(factory()->newExportDeclaration(
			nullptr /*modifiers*/, false /*isTypeOnly*/,
			factory()->newNamedExports(factory()->newNodeList(
				{factory()->newExportSpecifier(
					false /*isTypeOnly*/, nullptr /*propertyName*/,
					node->name->clone(*factory()))})),
			nullptr /*moduleSpecifier*/, nullptr /*attributes*/));
	}
	return statements;
}

// visitExportAssignment — esmodule.go:173
Node* ESModuleTransformer::visitExportAssignment(ExportAssignment* node) {
	if (!node->IsExportEquals) {
		return visitor()->visitEachChild(node->asNode());
	}
	if (compilerOptions->GetEmitModuleKind() != ModuleKind::Preserve) {
		// Elide `export=` as it is not legal with --module ES6
		return nullptr;
	}
	Node* visitedExpression = visitor()->visitNode(node->Expression);
	Node* statement = factory()->newExpressionStatement(
		factory()->newAssignmentExpression(
			factory()->newPropertyAccessExpression(
				factory()->newIdentifier("module"),
				nullptr /*questionDotToken*/,
				factory()->newIdentifier("exports"), NodeFlagsNone),
			visitedExpression));
	emitContext()->setOriginal(statement, node->asNode());
	return statement;
}

// visitExportDeclaration — esmodule.go:196
Node* ESModuleTransformer::visitExportDeclaration(ExportDeclaration* node) {
	if (node->ModuleSpecifier == nullptr) {
		return node->asNode();
	}

	Node* updatedModuleSpecifier = rewriteModuleSpecifier(
		emitContext(), node->ModuleSpecifier, compilerOptions);
	if (compilerOptions->Module > ModuleKind::ES2015 ||
	    node->ExportClause == nullptr ||
	    !isNamespaceExport(node->ExportClause)) {
		// Either ill-formed or don't need to be transformed.
		return factory()->updateExportDeclaration(
			node, nullptr /*modifiers*/, false /*isTypeOnly*/,
			node->ExportClause, updatedModuleSpecifier,
			visitor()->visitNode(node->Attributes));
	}

	Node* oldIdentifier = node->ExportClause->as<NamespaceExport>()->name;
	Node* synthName = factory()->newGeneratedNameForNode(oldIdentifier);
	Node* visitedAttributes = visitor()->visitNode(node->Attributes);
	Node* importDecl = factory()->newImportDeclaration(
		nullptr /*modifiers*/,
		factory()->newImportClause(Kind::Unknown /*phaseModifier*/,
		                           nullptr /*name*/,
		                           factory()->newNamespaceImport(synthName)),
		updatedModuleSpecifier, visitedAttributes);
	emitContext()->setOriginal(importDecl, node->ExportClause);

	Node* exportDecl;
	if (isExportNamespaceAsDefaultDeclaration(node->asNode())) {
		exportDecl = factory()->newExportAssignment(
			nullptr /*modifiers*/, false /*isExportEquals*/,
			nullptr /*typeNode*/, synthName);
	} else {
		exportDecl = factory()->newExportDeclaration(
			nullptr /*modifiers*/, false /*isTypeOnly*/,
			factory()->newNamedExports(factory()->newNodeList(
				{factory()->newExportSpecifier(false /*isTypeOnly*/, synthName,
				                               oldIdentifier)})),
			nullptr /*moduleSpecifier*/, nullptr /*attributes*/);
	}
	emitContext()->setOriginal(exportDecl, node->asNode());
	return transformers::singleOrMany({importDecl, exportDecl}, factory());
}

// visitCallExpression — esmodule.go:248
Node* ESModuleTransformer::visitCallExpression(CallExpression* node) {
	if (tristateIsTrue(compilerOptions->RewriteRelativeImportExtensions)) {
		if ((isImportCall(node->asNode()) &&
		     !node->Arguments->nodes.empty()) ||
		    (isInJSFile(node->asNode()) &&
		     isRequireCall(node->asNode(),
		                   false /*requireStringLiteralLikeArgument*/))) {
			return visitImportOrRequireCall(node);
		}
	}
	return visitor()->visitEachChild(node->asNode());
}

// visitImportOrRequireCall — esmodule.go:258
Node* ESModuleTransformer::visitImportOrRequireCall(CallExpression* node) {
	if (node->Arguments->nodes.empty()) {
		return visitor()->visitEachChild(node->asNode());
	}

	Node* expression = visitor()->visitNode(node->Expression);

	Node* argument;
	if (isStringLiteralLike(node->Arguments->nodes[0])) {
		argument = rewriteModuleSpecifier(
			emitContext(), node->Arguments->nodes[0], compilerOptions);
	} else {
		argument = factory()->newRewriteRelativeImportExtensionsHelper(
			node->Arguments->nodes[0],
			compilerOptions->Jsx == JsxEmit::Preserve);
	}

	std::vector<Node*> arguments{argument};

	std::vector<Node*> restSource(node->Arguments->nodes.begin() + 1,
	                              node->Arguments->nodes.end());
	auto rest = visitor()->visitSlice(restSource).first;
	arguments.insert(arguments.end(), rest.begin(), rest.end());

	NodeList* argumentList = factory()->newNodeList(arguments);
	argumentList->loc = node->Arguments->loc;
	return factory()->updateCallExpression(
		node, expression, node->QuestionDotToken, nullptr /*typeArguments*/,
		argumentList, node->flags);
}

// createRequireCall — esmodule.go:290
Node* ESModuleTransformer::createRequireCall(Node* node) {
	Node* moduleName =
		getExternalModuleNameLiteral(factory(), node, currentSourceFile,
		                             nullptr /*host*/,
		                             nullptr /*emitResolver*/, compilerOptions);

	std::vector<Node*> args;
	if (moduleName != nullptr) {
		args.push_back(rewriteModuleSpecifier(emitContext(), moduleName,
		                                      compilerOptions));
	}

	if (compilerOptions->GetEmitModuleKind() == ModuleKind::Preserve) {
		return factory()->newCallExpression(factory()->newIdentifier("require"),
		                                    nullptr /*questionDotToken*/,
		                                    nullptr /*typeArguments*/,
		                                    factory()->newNodeList(args),
		                                    NodeFlagsNone);
	}

	if (importRequireStatements_ == nullptr) {
		Node* createRequireName = factory()->newUniqueName(
			"_createRequire",
			printer::AutoGenerateOptions{
				printer::GeneratedIdentifierFlagsOptimistic |
					printer::GeneratedIdentifierFlagsFileLevel,
				"", ""});
		Node* importStatement = factory()->newImportDeclaration(
			nullptr /*modifiers*/,
			factory()->newImportClause(
				Kind::Unknown /*phaseModifier*/, nullptr /*name*/,
				factory()->newNamedImports(factory()->newNodeList(
					{factory()->newImportSpecifier(
						false /*isTypeOnly*/,
						factory()->newIdentifier("createRequire"),
						createRequireName)}))),
			factory()->newStringLiteral("module", TokenFlagsNone),
			nullptr /*attributes*/);
		emitContext()->addEmitFlags(importStatement,
		                            printer::EFCustomPrologue);

		Node* requireHelperName = factory()->newUniqueName(
			"__require",
			printer::AutoGenerateOptions{
				printer::GeneratedIdentifierFlagsOptimistic |
					printer::GeneratedIdentifierFlagsFileLevel,
				"", ""});
		Node* metaUrl = factory()->newPropertyAccessExpression(
			factory()->newMetaProperty(
				Kind::ImportKeyword, factory()->newIdentifier("meta")),
			nullptr /*questionDotToken*/, factory()->newIdentifier("url"),
			NodeFlagsNone);
		Node* requireStatement = factory()->newVariableStatement(
			nullptr /*modifiers*/,
			factory()->newVariableDeclarationList(
				factory()->newNodeList({factory()->newVariableDeclaration(
					requireHelperName, nullptr /*exclamationToken*/,
					nullptr /*type*/,
					factory()->newCallExpression(
						createRequireName->clone(*factory()),
						nullptr /*questionDotToken*/,
						nullptr /*typeArguments*/,
						factory()->newNodeList({metaUrl}), NodeFlagsNone))}),
				NodeFlagsConst));
		emitContext()->addEmitFlags(requireStatement,
		                            printer::EFCustomPrologue);
		importRequireStatements_ = new importRequireStatements{
			{importStatement, requireStatement}, requireHelperName};
	}

	return factory()->newCallExpression(
		importRequireStatements_->requireHelperName->clone(*factory()),
		nullptr /*questionDotToken*/, nullptr /*typeArguments*/,
		factory()->newNodeList(args), NodeFlagsNone);
}

}  // namespace tsc::transformers::moduletransforms
