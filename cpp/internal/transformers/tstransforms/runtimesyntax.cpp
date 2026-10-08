// Port of tsc/internal/transformers/tstransforms/runtimesyntax.go.

// !!! SourceMaps and Comments need to be validated
#include "internal/transformers/tstransforms/tstransforms.h"

#include <unordered_map>

#include "internal/binder/referenceresolver.h" // binder::ReferenceResolver
#include "internal/checker/checker.h"          // checker::EmitResolver
#include "internal/evaluator/evaluator.h"      // EvalResult

namespace tsc::transformers::tstransforms {
namespace {

// !!! SourceMaps and Comments need to be validated

// Transforms TypeScript-specific runtime syntax into JavaScript-compatible
// syntax.
struct RuntimeSyntaxTransformer : Transformer {
	const CompilerOptions* compilerOptions;
	Node* parentNode{};
	Node* currentNode{};
	Node* currentSourceFile{};
	Node* currentScope{}; // SourceFile | Block | ModuleBlock | CaseBlock
	std::unordered_map<std::string, Node*> currentScopeFirstDeclarationsOfName;
	Node* currentEnum{};      // EnumDeclaration
	Node* currentNamespace{}; // ModuleDeclaration
	binder::ReferenceResolver* resolver{};
	checker::EmitResolver* emitResolver{};

	static Transformer* create(TransformOptions* opt) {
		const CompilerOptions* compilerOptions = opt->CompilerOptions;
		printer::EmitContext* emitContext = opt->Context;
		auto* tx = new RuntimeSyntaxTransformer;
		tx->compilerOptions = compilerOptions;
		tx->resolver = opt->Resolver;
		tx->emitResolver = opt->EmitResolver;
		return tx->newTransformer(
			[tx](Node* node) -> Node* { return tx->visit(node); },
			emitContext);
	}

	// Pushes a new child node onto the ancestor tracking stack, returning the
	// grandparent node to be restored later via `popNode`.
	Node* pushNode(Node* node) {
		Node* grandparentNode = parentNode;
		parentNode = currentNode;
		currentNode = node;
		return grandparentNode;
	}

	// Pops the last child node off the ancestor tracking stack, restoring the
	// grandparent node.
	void popNode(Node* grandparentNode) {
		currentNode = parentNode;
		parentNode = grandparentNode;
	}

	std::pair<Node*, std::unordered_map<std::string, Node*>> pushScope(
		Node* node) {
		Node* savedCurrentScope = currentScope;
		auto savedCurrentScopeFirstDeclarationsOfName =
			currentScopeFirstDeclarationsOfName;
		switch (node->kind) {
		case Kind::SourceFile:
			currentScope = node;
			currentSourceFile = node;
			currentScopeFirstDeclarationsOfName = {};
			break;
		case Kind::CaseBlock:
		case Kind::ModuleBlock:
		case Kind::Block:
			currentScope = node;
			currentScopeFirstDeclarationsOfName = {};
			break;
		case Kind::FunctionDeclaration:
		case Kind::ClassDeclaration:
		case Kind::VariableStatement:
			recordDeclarationInScope(node);
			break;
		}
		return {savedCurrentScope,
				std::move(savedCurrentScopeFirstDeclarationsOfName)};
	}

	void popScope(Node* savedCurrentScope,
				  const std::unordered_map<std::string, Node*>&
					  savedCurrentScopeFirstDeclarationsOfName) {
		if (currentScope != savedCurrentScope) {
			// only reset the first declaration for a name if we are exiting
			// the scope in which it was declared
			currentScopeFirstDeclarationsOfName =
				savedCurrentScopeFirstDeclarationsOfName;
		}

		currentScope = savedCurrentScope;
	}

	// RAII helper for the `defer popNode`/`defer popScope` pair in visit().
	struct ancestorAndScopeGuard {
		RuntimeSyntaxTransformer* tx;
		Node* grandparentNode;
		Node* savedCurrentScope;
		std::unordered_map<std::string, Node*>
			savedCurrentScopeFirstDeclarationsOfName;
		ancestorAndScopeGuard(RuntimeSyntaxTransformer* tx, Node* node)
			: tx(tx), grandparentNode(tx->pushNode(node)) {
			auto saved = tx->pushScope(node);
			savedCurrentScope = saved.first;
			savedCurrentScopeFirstDeclarationsOfName =
				std::move(saved.second);
		}
		~ancestorAndScopeGuard() {
			tx->popScope(savedCurrentScope,
						 savedCurrentScopeFirstDeclarationsOfName);
			tx->popNode(grandparentNode);
		}
	};

	// Visits each node in the AST
	Node* visit(Node* node) {
		ancestorAndScopeGuard guard(this, node);

		if ((node->subtreeFacts() & SubtreeContainsTypeScript) == 0 &&
			((currentNamespace == nullptr && currentEnum == nullptr) ||
			 (node->subtreeFacts() & SubtreeContainsIdentifier) == 0)) {
			return node;
		}

		switch (node->kind) {
		// TypeScript parameter property modifiers are elided
		case Kind::PublicKeyword:
		case Kind::PrivateKeyword:
		case Kind::ProtectedKeyword:
		case Kind::ReadonlyKeyword:
		case Kind::OverrideKeyword:
			node = nullptr;
			break;
		case Kind::EnumDeclaration:
			node = visitEnumDeclaration(node->as<EnumDeclaration>());
			break;
		case Kind::ModuleDeclaration:
			node = visitModuleDeclaration(node->as<ModuleDeclaration>());
			break;
		case Kind::ClassDeclaration:
			node = visitClassDeclaration(node->as<ClassDeclaration>());
			break;
		case Kind::ClassExpression:
			node = visitClassExpression(node->as<ClassExpression>());
			break;
		case Kind::Constructor:
			node = visitConstructorDeclaration(
				node->as<ConstructorDeclaration>());
			break;
		case Kind::FunctionDeclaration:
			node = visitFunctionDeclaration(node->as<FunctionDeclaration>());
			break;
		case Kind::VariableStatement:
			node = visitVariableStatement(node->as<VariableStatement>());
			break;
		case Kind::ExportDeclaration:
		case Kind::ImportDeclaration:
		case Kind::ImportClause:
			if (currentNamespace != nullptr && currentScope != nullptr &&
				currentScope->kind != Kind::Block) {
				// do not emit ES6 imports and exports since they are illegal
				// inside a namespace
				node = nullptr;
			} else {
				node = visitor()->visitEachChild(node);
			}
			break;
		case Kind::ImportEqualsDeclaration:
			if (currentNamespace != nullptr && currentScope != nullptr &&
				currentScope->kind != Kind::Block &&
				node->as<ImportEqualsDeclaration>()->ModuleReference->kind ==
					Kind::ExternalModuleReference) {
				// do not emit ES6 imports and exports since they are illegal
				// inside a namespace
				node = nullptr;
			} else if (currentNamespace != nullptr && currentScope != nullptr &&
					   currentScope->kind == Kind::Block &&
					   node->as<ImportEqualsDeclaration>()
							   ->ModuleReference->kind !=
						   Kind::ExternalModuleReference) {
				// inside a block within a namespace, elide internal import
				// aliases
				node = nullptr;
			} else {
				node = visitImportEqualsDeclaration(
					node->as<ImportEqualsDeclaration>());
			}
			break;
		case Kind::Identifier:
			node = visitIdentifier(node);
			break;
		case Kind::ShorthandPropertyAssignment:
			node = visitShorthandPropertyAssignment(
				node->as<ShorthandPropertyAssignment>());
			break;
		default:
			node = visitor()->visitEachChild(node);
			break;
		}
		return node;
	}

	// Records that a declaration was emitted in the current scope, if it was
	// the first declaration for the provided symbol.
	void recordDeclarationInScope(Node* node) {
		switch (node->kind) {
		case Kind::VariableStatement:
			recordDeclarationInScope(
				node->as<VariableStatement>()->DeclarationList);
			return;
		case Kind::VariableDeclarationList:
			for (Node* decl :
				 node->as<VariableDeclarationList>()->Declarations->nodes) {
				recordDeclarationInScope(decl);
			}
			return;
		case Kind::ArrayBindingPattern:
		case Kind::ObjectBindingPattern:
			for (Node* element : node->elements()) {
				recordDeclarationInScope(element);
			}
			return;
		}
		Node* name = node->name();
		if (name != nullptr) {
			if (isIdentifier(name)) {
				const std::string& text = name->text();
				if (currentScopeFirstDeclarationsOfName.find(text) ==
					currentScopeFirstDeclarationsOfName.end()) {
					currentScopeFirstDeclarationsOfName[text] = node;
				}
			} else if (isBindingPattern(name)) {
				recordDeclarationInScope(name);
			}
		}
	}

	// Determines whether a declaration is the first declaration with the same
	// name emitted in the current scope.
	bool isFirstDeclarationInScope(Node* node) {
		Node* name = node->name();
		if (name != nullptr && isIdentifier(name)) {
			const std::string& text = name->text();
			auto it = currentScopeFirstDeclarationsOfName.find(text);
			if (it != currentScopeFirstDeclarationsOfName.end()) {
				return it->second == node;
			}
		}
		return false;
	}

	bool isExportOfNamespace(Node* node) {
		return currentNamespace != nullptr &&
			   (currentScope == nullptr ||
				currentScope->kind != Kind::Block) &&
			   (node->modifierFlags() & ModifierFlagsExport) != 0;
	}

	// Gets an expression that represents a property name, such as `"foo"` for
	// the identifier `foo`.
	Node* getExpressionForPropertyName(EnumMember* member) {
		Node* name = member->name;
		switch (name->kind) {
		case Kind::PrivateIdentifier:
			return factory()->newIdentifier("");
		case Kind::ComputedPropertyName: {
			ComputedPropertyName* n = name->as<ComputedPropertyName>();
			// enums don't support computed properties so we always generate
			// the 'expression' part of the name as-is.
			return visitor()->visitNode(n->Expression);
		}
		case Kind::Identifier:
			return factory()->newStringLiteral(name->text(), TokenFlagsNone);
		case Kind::StringLiteral: // !!! propagate token flags (will produce new
								  // diffs)
			return factory()->newStringLiteral(name->text(), TokenFlagsNone);
		case Kind::NumericLiteral:
			return factory()->newNumericLiteral(name->text(), TokenFlagsNone);
		default:
			return name;
		}
	}

	// Gets an expression like `E["A"]` that references an enum member.
	Node* getEnumQualifiedElement(EnumDeclaration* enum_, EnumMember* member) {
		Node* prop = getNamespaceQualifiedElement(
			getNamespaceContainerName(enum_->asNode()),
			getExpressionForPropertyName(member));
		emitContext()->addEmitFlags(prop, printer::EFNoComments | printer::EFNoNestedComments |
											  printer::EFNoSourceMap |
											  printer::EFNoNestedSourceMaps);
		return prop;
	}

	// Gets an expression used to refer to a namespace or enum from within the
	// body of its declaration.
	Node* getNamespaceContainerName(Node* node) {
		return factory()->newGeneratedNameForNode(node);
	}

	// Gets an expression used to refer to an export of a namespace or a member
	// of an enum by property name.
	Node* getNamespaceQualifiedProperty(Node* ns, Node* name) {
		return factory()->getNamespaceMemberName(
			ns, name, printer::NameOptions{false, true});
	}

	// Gets an expression used to refer to an export of a namespace or a member
	// of an enum by indexed access.
	Node* getNamespaceQualifiedElement(Node* ns, Node* expression) {
		Node* qualifiedName =
			emitContext()->factory.newElementAccessExpression(
				ns, nullptr /*questionDotToken*/, expression, NodeFlagsNone);
		emitContext()->assignCommentAndSourceMapRanges(qualifiedName,
													   expression);
		return qualifiedName;
	}

	// Gets an expression used within the provided node's container for any
	// exported references.
	Node* getExportQualifiedReferenceToDeclaration(Node* node) {
		if (isExportOfNamespace(node)) {
			return factory()->getExternalModuleOrNamespaceExportName(
				getNamespaceContainerName(currentNamespace), node,
				false /*allowComments*/, true /*allowSourceMaps*/);
		}
		return factory()->getDeclarationName(node,
											 printer::NameOptions{false, true});
	}

	std::pair<std::vector<Node*>, bool> addVarForDeclaration(
		std::vector<Node*> statements, Node* node) {
		recordDeclarationInScope(node);
		if (!isFirstDeclarationInScope(node)) {
			return {statements, false};
		}

		// var name;
		Node* name = factory()->getLocalName(
			node, printer::AssignedNameOptions{false, true, false});
		Node* varDecl =
			factory()->newVariableDeclaration(name, nullptr, nullptr, nullptr);
		NodeFlags varFlags = currentScope == currentSourceFile
								 ? NodeFlagsNone
								 : NodeFlagsLet;
		Node* varDecls = factory()->newVariableDeclarationList(
			factory()->newNodeList({varDecl}), varFlags);
		// Replicate modifierVisitor: strip decorators, TypeScript modifiers,
		// and export when in namespace.
		ModifierFlags modifierMask =
			~(ModifierFlagsTypeScriptModifier | ModifierFlagsDecorator);
		if (currentNamespace != nullptr) {
			modifierMask &= ~ModifierFlagsExport;
		}
		ModifierList* modifiers = extractModifiers(
			emitContext(), node->modifiers(), modifierMask);
		Node* varStatement =
			factory()->newVariableStatement(modifiers, varDecls);

		emitContext()->setOriginal(varDecl, node);
		// !!! synthetic comments
		emitContext()->setOriginal(varStatement, node);

		// Adjust the source map emit to match the old emitter.
		if (isEnumDeclaration(node)) {
			emitContext()->setSourceMapRange(varDecls, node->loc);
		} else {
			emitContext()->setSourceMapRange(varStatement, node->loc);
		}

		// Trailing comments for enum declaration should be emitted after the
		// function closure instead of the variable statement:
		//
		//     /** Leading comment*/
		//     enum E {
		//         A
		//     } // trailing comment
		//
		// Should emit:
		//
		//     /** Leading comment*/
		//     var E;
		//     (function (E) {
		//         E[E["A"] = 0] = "A";
		//     })(E || (E = {})); // trailing comment
		//
		emitContext()->setCommentRange(varStatement, node->loc);
		emitContext()->addEmitFlags(varStatement, printer::EFNoTrailingComments);
		statements.push_back(varStatement);

		return {statements, true};
	}

	Node* visitEnumDeclaration(EnumDeclaration* node) {
		if (!shouldEmitEnumDeclaration(node)) {
			return emitContext()->newNotEmittedStatement(node->asNode());
		}

		std::vector<Node*> statements;

		// If needed, we should emit a variable declaration for the enum:
		//  var name;
		bool varAdded;
		{
			auto pair = addVarForDeclaration(std::move(statements),
											 node->asNode());
			statements = std::move(pair.first);
			varAdded = pair.second;
		}

		// If we emit a leading variable declaration, we should not emit
		// leading comments for the enum body, but we should still emit the
		// comments if we are emitting to a System module.
		printer::EmitFlags emitFlags = printer::EFNone;
		if (varAdded &&
			(compilerOptions->GetEmitModuleKind() != ModuleKind::System ||
			 currentScope != currentSourceFile)) {
			emitFlags = emitFlags | printer::EFNoLeadingComments;
		}

		//  x || (x = {})
		//  exports.x || (exports.x = {})
		Node* enumArg = factory()->newLogicalORExpression(
			getExportQualifiedReferenceToDeclaration(node->asNode()),
			factory()->newAssignmentExpression(
				getExportQualifiedReferenceToDeclaration(node->asNode()),
				factory()->newObjectLiteralExpression(
					factory()->newNodeList({}), false)));

		if (isExportOfNamespace(node->asNode())) {
			// `localName` is the expression used within this node's
			// containing scope for any local references.
			Node* localName = factory()->getLocalName(
				node->asNode(),
				printer::AssignedNameOptions{false, true, false});

			//  x = (exports.x || (exports.x = {}))
			enumArg = factory()->newAssignmentExpression(localName, enumArg);
		}

		// (function (name) { ... })(name || (name = {}))
		Node* enumParamName = factory()->newGeneratedNameForNode(node->asNode());
		emitContext()->setSourceMapRange(enumParamName, node->name->loc);

		Node* enumParam = factory()->newParameterDeclaration(
			nullptr, nullptr, enumParamName, nullptr, nullptr, nullptr);
		Node* enumBody = transformEnumBody(node);
		Node* enumFunc = factory()->newFunctionExpression(
			nullptr, nullptr, nullptr, nullptr,
			factory()->newNodeList({enumParam}), nullptr, nullptr, enumBody);
		Node* enumCall = factory()->newCallExpression(
			factory()->newParenthesizedExpression(enumFunc), nullptr, nullptr,
			factory()->newNodeList({enumArg}), NodeFlagsNone);
		Node* enumStatement = factory()->newExpressionStatement(enumCall);
		emitContext()->setOriginal(enumStatement, node->asNode());
		emitContext()->assignCommentAndSourceMapRanges(enumStatement,
													   node->asNode());
		emitContext()->addEmitFlags(enumStatement, emitFlags);
		statements.push_back(enumStatement);
		return factory()->newSyntaxList(std::move(statements));
	}

	// Transforms the body of an enum declaration.
	Node* transformEnumBody(EnumDeclaration* node) {
		Node* savedCurrentEnum = currentEnum;
		currentEnum = node->asNode();

		// visit the children of `node` in advance to capture any references
		// to enum members
		node = visitor()->visitEachChild(node->asNode())
				   ->as<EnumDeclaration>();

		std::vector<Node*> statements;
		for (size_t i = 0; i < node->Members->nodes.size(); i++) {
			//  E[E["A"] = 0] = "A";
			statements = transformEnumMember(std::move(statements), node, i);
		}

		NodeList* statementList = factory()->newNodeList(statements);
		statementList->loc = node->Members->loc;

		currentEnum = savedCurrentEnum;
		return factory()->newBlock(statementList, true /*multiline*/);
	}

	// Transforms an enum member into a statement. It is expected that `enum`
	// has already been visited.
	std::vector<Node*> transformEnumMember(std::vector<Node*> statements,
										   EnumDeclaration* enum_,
										   size_t index) {
		Node* memberNode = enum_->Members->nodes[index];
		EnumMember* member = memberNode->as<EnumMember>();

		Node* savedParent = parentNode;
		parentNode = currentNode;
		currentNode = memberNode;

		//  E[E["A"] = x] = "A";
		//             ^
		Node* expression = member->Initializer; // NOTE: already visited

		bool useExplicitReverseMapping = false;

		Node* parseNode = emitContext()->parseNode(memberNode);
		EvalResult result = emitResolver->GetEnumMemberValue(parseNode);
		if (const auto* number =
				std::get_if<Number>(&result.Value)) {
			Node* ce = constantExpression(*number, factory());
			if (ce != nullptr) {
				expression = ce;
			}
			useExplicitReverseMapping = true;
		} else if (const auto* str =
					   std::get_if<std::string>(&result.Value)) {
			Node* ce = constantExpression(*str, factory());
			if (ce != nullptr) {
				expression = ce;
			}
		} else {
			if (expression == nullptr) {
				expression = factory()->newVoidZeroExpression();
			}
			useExplicitReverseMapping = !result.IsSyntacticallyString;
		}

		// Define the enum member property:
		//  E[E["A"] = 0] = "A";
		//    ^^^^^^^^--_____
		expression = factory()->newAssignmentExpression(
			getEnumQualifiedElement(enum_, member), expression);

		if (useExplicitReverseMapping) {
			//  E[E["A"] = 0] = "A";
			//  ^^--------------^^^^^
			expression = factory()->newAssignmentExpression(
				emitContext()->factory.newElementAccessExpression(
					getNamespaceContainerName(enum_->asNode()),
					nullptr /*questionDotToken*/, expression, NodeFlagsNone),
				getExpressionForPropertyName(member));
		}

		Node* memberStatement = factory()->newExpressionStatement(expression);
		emitContext()->assignCommentAndSourceMapRanges(expression,
													   member->asNode());
		emitContext()->assignCommentAndSourceMapRanges(memberStatement,
													   member->asNode());
		statements.push_back(memberStatement);

		currentNode = parentNode;
		parentNode = savedParent;
		return statements;
	}

	Node* visitModuleDeclaration(ModuleDeclaration* node) {
		if (!shouldEmitModuleDeclaration(node)) {
			return emitContext()->newNotEmittedStatement(node->asNode());
		}

		std::vector<Node*> statements;

		// If needed, we should emit a variable declaration for the module:
		//  var name;
		bool varAdded;
		{
			auto pair = addVarForDeclaration(std::move(statements),
											 node->asNode());
			statements = std::move(pair.first);
			varAdded = pair.second;
		}

		// If we emit a leading variable declaration, we should not emit
		// leading comments for the module body, but we should still emit the
		// comments if we are emitting to a System module.
		printer::EmitFlags emitFlags = printer::EFNone;
		if (varAdded &&
			(compilerOptions->GetEmitModuleKind() != ModuleKind::System ||
			 currentScope != currentSourceFile)) {
			emitFlags = emitFlags | printer::EFNoLeadingComments;
		}

		//  x || (x = {})
		//  exports.x || (exports.x = {})
		Node* moduleArg = factory()->newLogicalORExpression(
			getExportQualifiedReferenceToDeclaration(node->asNode()),
			factory()->newAssignmentExpression(
				getExportQualifiedReferenceToDeclaration(node->asNode()),
				factory()->newObjectLiteralExpression(
					factory()->newNodeList({}), false)));

		if (isExportOfNamespace(node->asNode())) {
			// `localName` is the expression used within this node's
			// containing scope for any local references.
			Node* localName = factory()->getLocalName(
				node->asNode(),
				printer::AssignedNameOptions{false, true, false});

			//  x = (exports.x || (exports.x = {}))
			moduleArg =
				factory()->newAssignmentExpression(localName, moduleArg);
		}

		// (function (name) { ... })(name || (name = {}))
		Node* moduleParamName =
			factory()->newGeneratedNameForNode(node->asNode());
		emitContext()->setSourceMapRange(moduleParamName, node->name->loc);

		Node* moduleParam = factory()->newParameterDeclaration(
			nullptr, nullptr, moduleParamName, nullptr, nullptr, nullptr);
		Node* moduleBody = transformModuleBody(
			node, getNamespaceContainerName(node->asNode()));
		Node* moduleFunc = factory()->newFunctionExpression(
			nullptr, nullptr, nullptr, nullptr,
			factory()->newNodeList({moduleParam}), nullptr, nullptr,
			moduleBody);
		Node* moduleCall = factory()->newCallExpression(
			factory()->newParenthesizedExpression(moduleFunc), nullptr,
			nullptr, factory()->newNodeList({moduleArg}), NodeFlagsNone);
		Node* moduleStatement = factory()->newExpressionStatement(moduleCall);
		emitContext()->setOriginal(moduleStatement, node->asNode());
		emitContext()->assignCommentAndSourceMapRanges(moduleStatement,
													   node->asNode());
		emitContext()->addEmitFlags(moduleStatement, emitFlags);
		statements.push_back(moduleStatement);
		return factory()->newSyntaxList(std::move(statements));
	}

	Node* transformModuleBody(ModuleDeclaration* node,
							  Node* namespaceLocalName) {
		Node* savedCurrentNamespace = currentNamespace;
		Node* savedCurrentScope = currentScope;
		auto savedCurrentScopeFirstDeclarationsOfName =
			currentScopeFirstDeclarationsOfName;

		currentNamespace = node->asNode();
		currentScopeFirstDeclarationsOfName = {};

		std::vector<Node*> statements;
		emitContext()->startVariableEnvironment();

		TextRange statementsLocation{};
		TextRange blockLocation{};
		if (node->Body != nullptr) {
			if (node->Body->kind == Kind::ModuleBlock) {
				// visit the children of `node` in advance to capture any
				// references to namespace members
				node = visitor()->visitEachChild(node->asNode())
						   ->as<ModuleDeclaration>();
				ModuleBlock* body = node->Body->as<ModuleBlock>();
				statements = body->Statements->nodes;
				statementsLocation = body->Statements->loc;
				blockLocation = body->loc;
			} else { // node.Body.Kind == ast.KindModuleDeclaration
				// !!! Strada didn't do this; why?
				// tx.currentScope = node.AsNode()
				statements =
					visitor()->visitSlice({node->Body}).first;
				ModuleBlock* moduleBlock =
					getInnermostModuleDeclarationFromDottedModule(node)
						->Body->as<ModuleBlock>();
				statementsLocation =
					moduleBlock->Statements->loc.withPos(-1);
			}
		}

		currentNamespace = savedCurrentNamespace;
		currentScope = savedCurrentScope;
		currentScopeFirstDeclarationsOfName =
			std::move(savedCurrentScopeFirstDeclarationsOfName);

		statements =
			emitContext()->endAndMergeVariableEnvironment(statements);
		NodeList* statementList = factory()->newNodeList(statements);
		statementList->loc = statementsLocation;
		Node* block = factory()->newBlock(statementList, true /*multiline*/);
		block->loc = blockLocation;

		//  namespace hello.hi.world {
		//       function foo() {}
		//
		//       // TODO, blah
		//  }
		//
		// should be emitted as
		//
		//  var hello;
		//  (function (hello) {
		//      var hi;
		//      (function (hi) {
		//          var world;
		//          (function (world) {
		//              function foo() { }
		//              // TODO, blah
		//          })(world = hi.world || (hi.world = {}));
		//      })(hi = hello.hi || (hello.hi = {}));
		//  })(hello || (hello = {}));
		//
		// We only want to emit comment on the namespace which contains block
		// body itself, not the containing namespaces.
		if (node->Body == nullptr ||
			node->Body->kind != Kind::ModuleBlock) {
			emitContext()->addEmitFlags(block, printer::EFNoComments);
		}
		return block;
	}

	Node* visitImportEqualsDeclaration(ImportEqualsDeclaration* node) {
		if (node->ModuleReference->kind == Kind::ExternalModuleReference) {
			return visitor()->visitEachChild(node->asNode());
		}

		Node* moduleReference =
			factory()->createExpressionFromEntityName(node->ModuleReference);
		emitContext()->setEmitFlags(moduleReference,
									printer::EFNoComments | printer::EFNoNestedComments);
		if (!isExportOfNamespace(node->asNode())) {
			//  export var ${name} = ${moduleReference};
			//  var ${name} = ${moduleReference};
			Node* varDecl = factory()->newVariableDeclaration(
				node->name, nullptr /*exclamationToken*/,
				nullptr /*type*/, moduleReference);
			emitContext()->setOriginal(varDecl, node->asNode());
			Node* varList = factory()->newVariableDeclarationList(
				factory()->newNodeList({varDecl}), NodeFlagsNone);
			ModifierList* varModifiers =
				extractModifiers(emitContext(), node->modifiers,
								 ModifierFlagsExport);
			Node* varStatement =
				factory()->newVariableStatement(varModifiers, varList);
			emitContext()->setOriginal(varStatement, node->asNode());
			emitContext()->assignCommentAndSourceMapRanges(varStatement,
														   node->asNode());
			return varStatement;
		} else {
			// exports.${name} = ${moduleReference};
			Node* statement =
				createExportStatement(node->name, moduleReference, node->loc,
									  node->loc, node->asNode());
			statement->loc = node->loc;
			return statement;
		}
	}

	Node* visitVariableStatement(VariableStatement* node) {
		if (isExportOfNamespace(node->asNode())) {
			std::vector<Node*> expressions;
			for (Node* declaration :
				 node->DeclarationList->as<VariableDeclarationList>()
					 ->Declarations->nodes) {
				VariableDeclaration* v =
					declaration->as<VariableDeclaration>();
				if (v->Initializer == nullptr) {
					continue;
				}
				if (isBindingPattern(v->name)) {
					Node* expression = flattenDestructuringAssignment(
						this, visitor()->visitNode(declaration),
						false /*needsValue*/, FlattenLevel::All,
						[this](Node* name, Node* value,
							   TextRange* location) -> Node* {
							return createNamespaceExportExpression(
								name, value, location);
						});
					if (expression != nullptr) {
						expressions.push_back(expression);
					}
				} else {
					Node* expression =
						convertVariableDeclarationToAssignmentExpression(
							emitContext(), v);
					if (expression != nullptr) {
						expressions.push_back(expression);
					}
				}
			}
			if (expressions.empty()) {
				return nullptr;
			}
			Node* expression = factory()->inlineExpressions(expressions);
			Node* statement = factory()->newExpressionStatement(expression);
			emitContext()->setOriginal(statement, node->asNode());
			emitContext()->assignCommentAndSourceMapRanges(statement,
														   node->asNode());

			// re-visit as the new node
			Node* savedCurrent = currentNode;
			currentNode = statement;
			statement = visitor()->visitEachChild(statement);
			currentNode = savedCurrent;
			return statement;
		}
		return visitor()->visitEachChild(node->asNode());
	}

	// createNamespaceExportExpression creates an assignment to a namespace
	// member for use as a callback during destructuring flattening.
	Node* createNamespaceExportExpression(Node* exportName, Node* exportValue,
										  TextRange* location) {
		Node* memberName = getNamespaceQualifiedProperty(
			getNamespaceContainerName(currentNamespace), exportName);
		Node* expression =
			factory()->newAssignmentExpression(memberName, exportValue);
		if (location != nullptr) {
			expression->loc = *location;
		}
		return expression;
	}

	Node* visitFunctionDeclaration(FunctionDeclaration* node) {
		if (isExportOfNamespace(node->asNode())) {
			Node* updated = factory()->updateFunctionDeclaration(
				node,
				visitor()->visitModifiers(extractModifiers(
					emitContext(), node->modifiers, ~ModifierFlagsExport)),
				node->AsteriskToken, visitor()->visitNode(node->name),
				nullptr /*typeParameters*/,
				visitor()->visitNodes(node->Parameters),
				nullptr /*returnType*/, nullptr /*fullSignature*/,
				visitor()->visitNode(node->Body));
			Node* export_ =
				createExportStatementForDeclaration(node->asNode());
			if (export_ != nullptr) {
				return factory()->newSyntaxList({updated, export_});
			}
			return updated;
		}
		return visitor()->visitEachChild(node->asNode());
	}

	std::vector<ParameterDeclaration*> getParameterProperties(
		Node* constructor) {
		std::vector<ParameterDeclaration*> parameterProperties;
		if (constructor != nullptr) {
			for (Node* parameter : constructor->parameters()) {
				if (isParameterPropertyDeclaration(parameter, constructor)) {
					parameterProperties.push_back(
						parameter->as<ParameterDeclaration>());
				}
			}
		}
		return parameterProperties;
	}

	Node* visitClassDeclaration(ClassDeclaration* node) {
		bool exported = isExportOfNamespace(node->asNode());
		ModifierList* modifiers;
		if (exported) {
			modifiers = visitor()->visitModifiers(
				extractModifiers(emitContext(), node->modifiers,
								 ~ModifierFlagsExportDefault));
		} else {
			modifiers = visitor()->visitModifiers(node->modifiers);
		}

		Node* name = visitor()->visitNode(node->name);
		if (name == nullptr &&
			(exported ||
			 childIsDecorated(compilerOptions->ExperimentalDecorators ==
								  Tristate::True,
							  node->asNode(), nullptr))) {
			name = factory()->newGeneratedNameForNode(node->asNode());
		}
		NodeList* heritageClauses =
			visitor()->visitNodes(node->HeritageClauses);
		NodeList* members = visitor()->visitNodes(node->Members);
		Node* constructor = nullptr;
		for (Node* m : node->Members->nodes) {
			if (isConstructorDeclaration(m)) {
				constructor = m;
				break;
			}
		}
		std::vector<ParameterDeclaration*> parameterProperties =
			getParameterProperties(constructor);

		if (!parameterProperties.empty()) {
			std::vector<Node*> newMembers;
			for (ParameterDeclaration* parameter : parameterProperties) {
				if (isIdentifier(parameter->name)) {
					Node* parameterProperty =
						factory()->newPropertyDeclaration(
							nullptr /*modifiers*/,
							parameter->name->clone(*factory()),
							nullptr /*questionOrExclamationToken*/,
							nullptr /*type*/, nullptr /*initializer*/);
					emitContext()->setOriginal(parameterProperty,
											   parameter->asNode());
					newMembers.push_back(parameterProperty);
				}
			}
			if (!newMembers.empty()) {
				for (Node* m : members->nodes) {
					newMembers.push_back(m);
				}
				members = factory()->newNodeList(newMembers);
				members->loc = node->Members->loc;
			}
		}

		Node* updated = factory()->updateClassDeclaration(
			node, modifiers, name, nullptr /*typeParameters*/,
			heritageClauses, members);
		if (exported) {
			Node* export_ =
				createExportStatementForDeclaration(node->asNode());
			if (export_ != nullptr) {
				return factory()->newSyntaxList({updated, export_});
			}
		}
		return updated;
	}

	Node* visitClassExpression(ClassExpression* node) {
		ModifierList* modifiers = visitor()->visitModifiers(
			extractModifiers(emitContext(), node->modifiers,
							 ~ModifierFlagsExportDefault));
		Node* name = visitor()->visitNode(node->name);
		NodeList* heritageClauses =
			visitor()->visitNodes(node->HeritageClauses);
		NodeList* members = visitor()->visitNodes(node->Members);
		Node* constructor = nullptr;
		for (Node* m : node->Members->nodes) {
			if (isConstructorDeclaration(m)) {
				constructor = m;
				break;
			}
		}
		std::vector<ParameterDeclaration*> parameterProperties =
			getParameterProperties(constructor);

		if (!parameterProperties.empty()) {
			std::vector<Node*> newMembers;
			for (ParameterDeclaration* parameter : parameterProperties) {
				if (isIdentifier(parameter->name)) {
					Node* parameterProperty =
						factory()->newPropertyDeclaration(
							nullptr /*modifiers*/,
							parameter->name->clone(*factory()),
							nullptr /*questionOrExclamationToken*/,
							nullptr /*type*/, nullptr /*initializer*/);
					emitContext()->setOriginal(parameterProperty,
											   parameter->asNode());
					newMembers.push_back(parameterProperty);
				}
			}
			if (!newMembers.empty()) {
				for (Node* m : members->nodes) {
					newMembers.push_back(m);
				}
				members = factory()->newNodeList(newMembers);
				members->loc = node->Members->loc;
			}
		}

		return factory()->updateClassExpression(
			node, modifiers, name, nullptr /*typeParameters*/,
			heritageClauses, members);
	}

	Node* visitConstructorDeclaration(ConstructorDeclaration* node) {
		ModifierList* modifiers = visitor()->visitModifiers(node->modifiers);
		NodeList* parameters =
			emitContext()->visitParameters(node->parameterList(), visitor());
		Node* body = visitConstructorBody(
			node->Body != nullptr ? node->Body->as<Block>() : nullptr,
			node->asNode());
		return factory()->updateConstructorDeclaration(
			node, modifiers, nullptr /*typeParameters*/, parameters,
			nullptr /*returnType*/, nullptr /*fullSignature*/, body);
	}

	Node* visitConstructorBody(Block* body, Node* constructor) {
		std::vector<ParameterDeclaration*> parameterProperties =
			getParameterProperties(constructor);
		if (parameterProperties.empty()) {
			return emitContext()->visitFunctionBody(
				body != nullptr ? body->asNode() : nullptr, visitor());
		}

		Node* grandparentOfBody = pushNode(body->asNode());
		auto savedScope = pushScope(body->asNode());

		emitContext()->startVariableEnvironment();
		auto prologuePair =
			factory()->splitStandardPrologue(body->Statements->nodes);
		std::vector<Node*> statements = prologuePair.first;
		std::vector<Node*> rest = prologuePair.second;

		// Transform parameters into property assignments. Transforms this:
		//
		//  constructor (public x, public y) {
		//  }
		//
		// Into this:
		//
		//  constructor (x, y) {
		//      this.x = x;
		//      this.y = y;
		//  }
		//

		std::vector<Node*> parameterPropertyAssignments;
		for (ParameterDeclaration* parameter : parameterProperties) {
			if (isIdentifier(parameter->name)) {
				Node* propertyName = parameter->name->clone(*factory());
				//nolint // .Parent set to get node to printback using text
				// from original file instead of processed text; TODO: this
				// should be achievable via EmitFlags instead
				propertyName->parent = parameter->name->parent;
				emitContext()->addEmitFlags(propertyName,
											printer::EFNoComments | printer::EFNoSourceMap);

				Node* localName = parameter->name->clone(*factory());
				localName->parent = parameter->name->parent;
				emitContext()->addEmitFlags(localName, printer::EFNoComments);

				Node* parameterProperty =
					factory()->newExpressionStatement(
						factory()->newAssignmentExpression(
							factory()->newPropertyAccessExpression(
								factory()->newThisExpression(),
								nullptr /*questionDotToken*/, propertyName,
								NodeFlagsNone),
							localName));
				emitContext()->setOriginal(parameterProperty,
										   parameter->asNode());
				emitContext()->addEmitFlags(parameterProperty,
											printer::EFStartOnNewLine);
				parameterPropertyAssignments.push_back(parameterProperty);
			}
		}

		std::vector<int> superPath = findSuperStatementIndexPath(rest, 0);

		if (!superPath.empty()) {
			auto transformed = transformConstructorBodyWorker(
				rest, superPath, parameterPropertyAssignments);
			for (Node* s : transformed) {
				statements.push_back(s);
			}
		} else {
			for (Node* s : parameterPropertyAssignments) {
				statements.push_back(s);
			}
			for (Node* s : visitor()->visitSlice(rest).first) {
				statements.push_back(s);
			}
		}

		statements =
			emitContext()->endAndMergeVariableEnvironment(statements);
		NodeList* statementList = factory()->newNodeList(statements);
		statementList->loc = body->Statements->loc;

		popScope(savedScope.first, savedScope.second);
		popNode(grandparentOfBody);
		Node* updated =
			factory()->newBlock(statementList /*multiline*/, true);
		emitContext()->setOriginal(updated, body->asNode());
		updated->loc = body->loc;
		return updated;
	}

	std::vector<Node*> transformConstructorBodyWorker(
		const std::vector<Node*>& statementsIn,
		const std::vector<int>& superPath,
		const std::vector<Node*>& initializerStatements) {
		std::vector<Node*> statementsOut;
		int superStatementIndex = superPath[0];
		Node* superStatement = statementsIn[superStatementIndex];

		// visit up to the statement containing `super`
		auto visited = visitor()->visitSlice(std::vector<Node*>(
			statementsIn.begin(), statementsIn.begin() + superStatementIndex));
		for (Node* s : visited.first) {
			statementsOut.push_back(s);
		}

		// if the statement containing `super` is a `try` statement, transform
		// the body of the `try` block
		if (isTryStatement(superStatement)) {
			TryStatement* tryStatement = superStatement->as<TryStatement>();
			Block* tryBlock = tryStatement->TryBlock->as<Block>();

			// keep track of hierarchy as we descend
			Node* grandparentOfTryStatement =
				pushNode(tryStatement->asNode());
			Node* grandparentOfTryBlock = pushNode(tryBlock->asNode());
			auto savedScope = pushScope(tryBlock->asNode());

			// visit the `try` block
			std::vector<Node*> tryBlockStatements =
				transformConstructorBodyWorker(
					tryBlock->Statements->nodes,
					std::vector<int>(superPath.begin() + 1, superPath.end()),
					initializerStatements);

			// restore hierarchy as we ascend to the `try` statement
			popScope(savedScope.first, savedScope.second);
			popNode(grandparentOfTryBlock);

			NodeList* tryBlockStatementList =
				factory()->newNodeList(tryBlockStatements);
			tryBlockStatementList->loc = tryBlock->Statements->loc;
			statementsOut.push_back(factory()->updateTryStatement(
				tryStatement,
				factory()->updateBlock(tryBlock, tryBlockStatementList,
									   tryBlock->MultiLine),
				visitor()->visitNode(tryStatement->CatchClause),
				visitor()->visitNode(tryStatement->FinallyBlock)));

			// restore hierarchy as we ascend to the parent of the `try`
			// statement
			popNode(grandparentOfTryStatement);
		} else {
			// visit the statement containing `super`
			auto visitedSuper = visitor()->visitSlice(std::vector<Node*>(
				statementsIn.begin() + superStatementIndex,
				statementsIn.begin() + superStatementIndex + 1));
			for (Node* s : visitedSuper.first) {
				statementsOut.push_back(s);
			}

			// insert the initializer statements
			for (Node* s : initializerStatements) {
				statementsOut.push_back(s);
			}
		}

		// visit the statements after `super`
		auto visitedRest = visitor()->visitSlice(std::vector<Node*>(
			statementsIn.begin() + superStatementIndex + 1,
			statementsIn.end()));
		for (Node* s : visitedRest.first) {
			statementsOut.push_back(s);
		}
		return statementsOut;
	}

	Node* visitShorthandPropertyAssignment(
		ShorthandPropertyAssignment* node) {
		Node* name = node->name;
		Node* exportedOrImportedName = visitExpressionIdentifier(name);
		if (exportedOrImportedName != name) {
			Node* expression = exportedOrImportedName;
			if (node->ObjectAssignmentInitializer != nullptr) {
				Node* equalsToken = node->EqualsToken;
				if (equalsToken == nullptr) {
					equalsToken = factory()->newToken(Kind::EqualsToken);
				}
				expression = factory()->newBinaryExpression(
					nullptr /*modifiers*/, expression,
					nullptr /*typeNode*/, equalsToken,
					visitor()->visitNode(node->ObjectAssignmentInitializer));
			}

			Node* updated = factory()->newPropertyAssignment(
				nullptr /*modifiers*/, node->name,
				nullptr /*postfixToken*/, nullptr /*typeNode*/, expression);
			updated->loc = node->loc;
			emitContext()->setOriginal(updated, node->asNode());
			emitContext()->assignCommentAndSourceMapRanges(updated,
														   node->asNode());
			return updated;
		}
		return factory()->updateShorthandPropertyAssignment(
			node, nullptr /*modifiers*/, exportedOrImportedName,
			nullptr /*postfixToken*/, nullptr /*typeNode*/, node->EqualsToken,
			visitor()->visitNode(node->ObjectAssignmentInitializer));
	}

	Node* visitIdentifier(Node* node) {
		if (isIdentifierReference(node, parentNode)) {
			return visitExpressionIdentifier(node);
		}
		return node;
	}

	Node* visitExpressionIdentifier(Node* node) {
		if ((currentEnum != nullptr || currentNamespace != nullptr) &&
			!isGeneratedIdentifier(emitContext(), node) &&
			!isLocalName(emitContext(), node)) {
			Node* location = emitContext()->mostOriginal(node);
			Node* container =
				resolver->GetReferencedExportContainer(location,
													   false /*prefixLocals*/);
			if (container != nullptr &&
				(isEnumDeclaration(container) ||
				 isModuleDeclaration(container))) {
				Node* containerName = getNamespaceContainerName(container);

				Node* memberName = node->clone(*factory());
				emitContext()->setEmitFlags(memberName,
											printer::EFNoComments | printer::EFNoSourceMap);

				Node* expression = factory()->getNamespaceMemberName(
					containerName, memberName,
					printer::NameOptions{false, true});
				emitContext()->assignCommentAndSourceMapRanges(expression,
															   node);
				return expression;
			}
		}
		return node;
	}

	Node* createExportStatementForDeclaration(Node* node) {
		Node* exportName =
			factory()->getExternalModuleOrNamespaceExportName(
				getNamespaceContainerName(currentNamespace), node,
				false /*allowComments*/, true /*allowSourceMaps*/);
		Node* localName = factory()->getLocalName(node);
		Node* expression =
			factory()->newAssignmentExpression(exportName, localName);
		TextRange exportAssignmentSourceMapRange = node->loc;
		if (node->name() != nullptr) {
			exportAssignmentSourceMapRange =
				exportAssignmentSourceMapRange.withPos(
					node->name()->pos());
		}
		emitContext()->setSourceMapRange(expression,
										 exportAssignmentSourceMapRange);

		Node* statement = factory()->newExpressionStatement(expression);
		TextRange exportStatementSourceMapRange = node->loc.withPos(-1);
		emitContext()->setSourceMapRange(statement,
										 exportStatementSourceMapRange);
		return statement;
	}

	Node* createExportAssignment(Node* name, Node* expression,
								 TextRange exportAssignmentSourceMapRange,
								 Node* original) {
		Node* exportName = getNamespaceQualifiedProperty(
			getNamespaceContainerName(currentNamespace), name);
		Node* exportAssignment =
			factory()->newAssignmentExpression(exportName, expression);
		emitContext()->setOriginal(exportAssignment, original);
		emitContext()->setSourceMapRange(exportAssignment,
										 exportAssignmentSourceMapRange);
		return exportAssignment;
	}

	Node* createExportStatement(Node* name, Node* expression,
								TextRange exportAssignmentSourceMapRange,
								TextRange exportStatementSourceMapRange,
								Node* original) {
		Node* exportStatement = factory()->newExpressionStatement(
			createExportAssignment(name, expression,
								   exportAssignmentSourceMapRange, original));
		emitContext()->setOriginal(exportStatement, original);
		emitContext()->setSourceMapRange(exportStatement,
										 exportStatementSourceMapRange);
		return exportStatement;
	}

	bool shouldEmitEnumDeclaration(EnumDeclaration* node) {
		return !isEnumConst(node->asNode()) ||
			   compilerOptions->ShouldPreserveConstEnums();
	}

	bool shouldEmitModuleDeclaration(ModuleDeclaration* node) {
		Node* pn = emitContext()->parseNode(node->asNode());
		if (pn == nullptr) {
			// If we can't find a parse tree node, assume the node is
			// instantiated.
			return true;
		}
		return isInstantiatedModule(
			pn, compilerOptions->ShouldPreserveConstEnums());
	}
};

} // namespace

Transformer* NewRuntimeSyntaxTransformer(TransformOptions* opt) {
	return RuntimeSyntaxTransformer::create(opt);
}

// getInnermostModuleDeclarationFromDottedModule — runtimesyntax.go:990
ModuleDeclaration* getInnermostModuleDeclarationFromDottedModule(
	ModuleDeclaration* moduleDeclaration) {
	while (moduleDeclaration->Body != nullptr &&
		   moduleDeclaration->Body->kind == Kind::ModuleDeclaration) {
		moduleDeclaration =
			moduleDeclaration->Body->as<ModuleDeclaration>();
	}
	return moduleDeclaration;
}

} // namespace tsc::transformers::tstransforms
