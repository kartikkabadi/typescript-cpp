// Port of tsc/internal/transformers/estransforms/namedevaluation.go —
// ECMAScript NamedEvaluation transform (`__setFunctionName`) for anonymous
// class/function expressions.
#include "internal/transformers/estransforms/estransforms.h"

namespace tsc::transformers::estransforms {
namespace {

void debugFail_(const char* msg) { (void)msg; TSC_UNREACHABLE(msg); }

// ast.utilities.go — IsProtoSetter (file-local replica; the checker's copy is
// TU-local in checker_expressions_a.cpp).
bool isProtoSetter(Node* node) {
	return (isIdentifier(node) || isStringLiteral(node)) &&
		node->text() == "__proto__";
}

// ast.utilities.go:4595 — IsNamedEvaluationSource (file-local replica; the
// checker's copy is TU-local in checker_expressions_a.cpp).
bool isNamedEvaluationSource_(Node* node) {
	switch (node->kind) {
	case Kind::PropertyAssignment:
		return !isProtoSetter(node->as<PropertyAssignment>()->name);
	case Kind::ShorthandPropertyAssignment:
		return node->as<ShorthandPropertyAssignment>()
				->ObjectAssignmentInitializer != nullptr;
	case Kind::VariableDeclaration:
		return isIdentifier(node->as<VariableDeclaration>()->name) &&
			node->initializer() != nullptr;
	case Kind::Parameter:
		return isIdentifier(node->as<ParameterDeclaration>()->name) &&
			node->initializer() != nullptr &&
			node->as<ParameterDeclaration>()->DotDotDotToken == nullptr;
	case Kind::BindingElement:
		return isIdentifier(node->as<BindingElement>()->name) &&
			node->initializer() != nullptr &&
			node->as<BindingElement>()->DotDotDotToken == nullptr;
	case Kind::PropertyDeclaration:
		return node->initializer() != nullptr;
	case Kind::BinaryExpression:
		switch (node->as<BinaryExpression>()->OperatorToken->kind) {
		case Kind::EqualsToken:
		case Kind::AmpersandAmpersandEqualsToken:
		case Kind::BarBarEqualsToken:
		case Kind::QuestionQuestionEqualsToken:
			return isIdentifier(node->as<BinaryExpression>()->Left);
		}
		break;
	case Kind::ExportAssignment:
		return true;
	default:
		break;
	}
	return false;
}

// isAnonymousFunctionDefinition — namedevaluation.go:63.
// Indicates whether an expression is an anonymous function definition.
//
// See https://tc39.es/ecma262/#sec-isanonymousfunctiondefinition
// anonymousFunctionDefinition: ClassExpression | FunctionExpression |
// ArrowFunction
bool isAnonymousFunctionDefinition(
	printer::EmitContext* emitContext, Node* node,
	const std::function<bool(Node*)>& cb) {
	node = skipOuterExpressions(node, OEKAll);
	switch (node->kind) {
	case Kind::ClassExpression:
		if (classHasDeclaredOrExplicitlyAssignedName(emitContext, node)) {
			return false;
		}
		break;
	case Kind::FunctionExpression:
		if (node->name() != nullptr) {
			return false;
		}
		break;
	case Kind::ArrowFunction:
		// arrow functions are always anonymous
		break;
	default:
		return false;
	}
	if (cb) {
		return cb(node);
	}
	return true;
}

// getAssignedNameOfIdentifier — namedevaluation.go:109. Gets a string literal
// to use as the assigned name of an anonymous class or function declaration.
Node* getAssignedNameOfIdentifier(printer::EmitContext* emitContext,
                                  Node* name, Node* expression) {
	Node* original = emitContext->mostOriginal(
		skipOuterExpressions(expression, OEKAll));
	if ((isClassDeclaration(original) || isFunctionDeclaration(original)) &&
		original->name() == nullptr &&
		hasSyntacticModifier(original, ModifierFlagsDefault)) {
		return emitContext->factory.newStringLiteral("default",
		                                             TokenFlagsNone);
	}
	return emitContext->factory.newStringLiteralFromNode(name);
}

// getAssignedNameOfPropertyName — namedevaluation.go:118.
std::pair<Node* /*assignedName*/, Node* /*updatedName*/>
getAssignedNameOfPropertyName(printer::EmitContext* emitContext, Node* name,
                              const std::string& assignedNameText) {
	printer::NodeFactory& factory = emitContext->factory;
	if (assignedNameText.length() > 0) {
		Node* assignedName =
			factory.newStringLiteral(assignedNameText, TokenFlagsNone);
		return {assignedName, name};
	}

	if (isPropertyNameLiteral(name) || isPrivateIdentifier(name)) {
		Node* assignedName = factory.newStringLiteralFromNode(name);
		return {assignedName, name};
	}

	Node* expression = name->expression();
	if (isPropertyNameLiteral(expression) && !isIdentifier(expression)) {
		Node* assignedName = factory.newStringLiteralFromNode(expression);
		return {assignedName, name};
	}

	// debug.Assert(ast.IsComputedPropertyName(name),
	//              "Expected computed property name")
	if (!isComputedPropertyName(name)) {
		TSC_UNREACHABLE("Expected computed property name");
	}

	Node* assignedName = factory.newGeneratedNameForNode(name);
	emitContext->addVariableDeclaration(assignedName);

	Node* key = factory.newPropKeyHelper(expression);
	Node* assignment = factory.newAssignmentExpression(assignedName, key);
	Node* updatedName = factory.updateComputedPropertyName(
		name->as<ComputedPropertyName>(), assignment);
	return {assignedName, updatedName};
}

// createClassNamedEvaluationHelperBlock — namedevaluation.go:153. Creates a
// class `static {}` block used to dynamically set the name of a class.
//
// The assignedName parameter is the expression used to resolve the assigned
// name at runtime. This expression should not produce side effects.
// The thisExpression parameter overrides the expression to use for the actual
// `this` reference. This can be used to provide an expression that has already
// had its `EmitFlags` set or may have been tracked to prevent substitution.
Node* createClassNamedEvaluationHelperBlock(printer::EmitContext* emitContext,
                                            Node* assignedName,
                                            Node* thisExpression) {
	// produces:
	//
	//  static { __setFunctionName(this, "C"); }
	//

	if (thisExpression == nullptr) {
		thisExpression = emitContext->factory.newThisExpression();
	}

	printer::NodeFactory& factory = emitContext->factory;
	Node* expression = factory.newSetFunctionNameHelper(
		thisExpression, assignedName, "" /*prefix*/);
	Node* statement = factory.newExpressionStatement(expression);
	Node* body = factory.newBlock(factory.newNodeList({statement}),
	                            false /*multiLine*/);
	Node* block = factory.newClassStaticBlockDeclaration(
		nullptr /*modifiers*/, body);

	// We use `emitNode.assignedName` to indicate this is a NamedEvaluation
	// helper block and to stash the expression used to resolve the assigned
	// name.
	emitContext->setAssignedName(block, assignedName);
	return block;
}

// finishTransformNamedEvaluation — namedevaluation.go:250.
Node* finishTransformNamedEvaluation(printer::EmitContext* emitContext,
                                     Node* expression,
                                     Node* assignedName,
                                     bool ignoreEmptyStringLiteral) {
	if (ignoreEmptyStringLiteral && isStringLiteral(assignedName) &&
		assignedName->text().empty()) {
		return expression;
	}

	printer::NodeFactory& factory = emitContext->factory;
	Node* innerExpression = skipOuterExpressions(expression, OEKAll);

	Node* updatedExpression;
	if (isClassExpression(innerExpression)) {
		updatedExpression =
			injectClassNamedEvaluationHelperBlockIfMissing(
				emitContext, innerExpression, assignedName,
				nullptr /*thisExpression*/);
	} else {
		updatedExpression = factory.newSetFunctionNameHelper(
			innerExpression, assignedName, "" /*prefix*/);
	}

	return factory.restoreOuterExpressions(expression, updatedExpression,
	                                       OEKAll);
}

// transformNamedEvaluationOfPropertyAssignment — namedevaluation.go:273
Node* transformNamedEvaluationOfPropertyAssignment(
	printer::EmitContext* context, PropertyAssignment* node,
	bool ignoreEmptyStringLiteral, const std::string& assignedNameText) {
	// 13.2.5.5 RS: PropertyDefinitionEvaluation
	//   PropertyAssignment : PropertyName `:` AssignmentExpression
	//     ...
	//     5. If IsAnonymousFunctionDefinition(|AssignmentExpression|) is *true*
	//        and _isProtoSetter_ is *false*, then
	//        a. Let _popValue_ be ? NamedEvaluation of |AssignmentExpression|
	//           with argument _propKey_.
	//     ...

	printer::NodeFactory& factory = context->factory;
	auto [assignedName, name] =
		getAssignedNameOfPropertyName(context, node->name,
		                              assignedNameText);
	Node* initializer =
		finishTransformNamedEvaluation(context, node->Initializer,
		                               assignedName, ignoreEmptyStringLiteral);
	return factory.updatePropertyAssignment(node, nullptr /*modifiers*/, name,
	                                        nullptr /*postfixToken*/,
	                                        nullptr /*typeNode*/, initializer);
}

// transformNamedEvaluationOfShorthandAssignmentProperty —
// namedevaluation.go:287
Node* transformNamedEvaluationOfShorthandAssignmentProperty(
	printer::EmitContext* emitContext, ShorthandPropertyAssignment* node,
	bool ignoreEmptyStringLiteral, const std::string& assignedNameText) {
	// 13.15.5.3 RS: PropertyDestructuringAssignmentEvaluation
	//   AssignmentProperty : IdentifierReference Initializer?
	//     ...
	//     4. If |Initializer?| is present and _v_ is *undefined*, then
	//        a. If IsAnonymousFunctionDefinition(|Initializer|) is *true*,
	//           then
	//           i. Set _v_ to ? NamedEvaluation of |Initializer| with argument
	//              _P_.
	//     ...

	printer::NodeFactory& factory = emitContext->factory;
	Node* assignedName;
	if (assignedNameText.length() > 0) {
		assignedName =
			factory.newStringLiteral(assignedNameText, TokenFlagsNone);
	} else {
		assignedName = getAssignedNameOfIdentifier(
			emitContext, node->name, node->ObjectAssignmentInitializer);
	}
	Node* objectAssignmentInitializer = finishTransformNamedEvaluation(
		emitContext, node->ObjectAssignmentInitializer, assignedName,
		ignoreEmptyStringLiteral);
	return factory.updateShorthandPropertyAssignment(
		node,
		nullptr, /*modifiers*/
		node->name,
		nullptr, /*postfixToken*/
		nullptr, /*typeNode*/
		node->EqualsToken,
		objectAssignmentInitializer);
}

// transformNamedEvaluationOfVariableDeclaration — namedevaluation.go:315
Node* transformNamedEvaluationOfVariableDeclaration(
	printer::EmitContext* emitContext, VariableDeclaration* node,
	bool ignoreEmptyStringLiteral, const std::string& assignedNameText) {
	// 14.3.1.2 RS: Evaluation
	//   LexicalBinding : BindingIdentifier Initializer
	//     ...
	//     3. If IsAnonymousFunctionDefinition(|Initializer|) is *true*, then
	//        a. Let _value_ be ? NamedEvaluation of |Initializer| with argument
	//           _bindingId_.
	//     ...
	//
	// 14.3.2.1 RS: Evaluation
	//   VariableDeclaration : BindingIdentifier Initializer
	//     ...
	//     3. If IsAnonymousFunctionDefinition(|Initializer|) is *true*, then
	//        a. Let _value_ be ? NamedEvaluation of |Initializer| with argument
	//           _bindingId_.
	//     ...

	printer::NodeFactory& factory = emitContext->factory;
	Node* assignedName;
	if (assignedNameText.length() > 0) {
		assignedName =
			factory.newStringLiteral(assignedNameText, TokenFlagsNone);
	} else {
		assignedName = getAssignedNameOfIdentifier(emitContext, node->name,
		                                           node->Initializer);
	}
	Node* initializer =
		finishTransformNamedEvaluation(emitContext, node->Initializer,
		                               assignedName, ignoreEmptyStringLiteral);
	return factory.updateVariableDeclaration(
		node,
		node->name,
		nullptr, /*exclamationToken*/
		nullptr, /*typeNode*/
		initializer);
}

// transformNamedEvaluationOfParameterDeclaration — namedevaluation.go:347
Node* transformNamedEvaluationOfParameterDeclaration(
	printer::EmitContext* emitContext, ParameterDeclaration* node,
	bool ignoreEmptyStringLiteral, const std::string& assignedNameText) {
	// 8.6.3 RS: IteratorBindingInitialization
	//   SingleNameBinding : BindingIdentifier Initializer?
	//     ...
	//     5. If |Initializer| is present and _v_ is *undefined*, then
	//        a. If IsAnonymousFunctionDefinition(|Initializer|) is *true*,
	//           then
	//           i. Set _v_ to ? NamedEvaluation of |Initializer| with argument
	//              _bindingId_.
	//     ...
	//
	// 14.3.3.3 RS: KeyedBindingInitialization
	//   SingleNameBinding : BindingIdentifier Initializer?
	//     ...
	//     4. If |Initializer| is present and _v_ is *undefined*, then
	//        a. If IsAnonymousFunctionDefinition(|Initializer|) is *true*,
	//           then
	//           i. Set _v_ to ? NamedEvaluation of |Initializer| with argument
	//              _bindingId_.
	//     ...

	printer::NodeFactory& factory = emitContext->factory;
	Node* assignedName;
	if (assignedNameText.length() > 0) {
		assignedName =
			factory.newStringLiteral(assignedNameText, TokenFlagsNone);
	} else {
		assignedName = getAssignedNameOfIdentifier(emitContext, node->name,
		                                           node->Initializer);
	}
	Node* initializer =
		finishTransformNamedEvaluation(emitContext, node->Initializer,
		                               assignedName, ignoreEmptyStringLiteral);
	return factory.updateParameterDeclaration(
		node,
		nullptr, /*modifiers*/
		node->DotDotDotToken,
		node->name,
		nullptr, /*questionToken*/
		nullptr, /*typeNode*/
		initializer);
}

// transformNamedEvaluationOfBindingElement — namedevaluation.go:383
Node* transformNamedEvaluationOfBindingElement(
	printer::EmitContext* emitContext, BindingElement* node,
	bool ignoreEmptyStringLiteral, const std::string& assignedNameText) {
	// 8.6.3 RS: IteratorBindingInitialization
	//   SingleNameBinding : BindingIdentifier Initializer?
	//     ...
	//     5. If |Initializer| is present and _v_ is *undefined*, then
	//        a. If IsAnonymousFunctionDefinition(|Initializer|) is *true*,
	//           then
	//           i. Set _v_ to ? NamedEvaluation of |Initializer| with argument
	//              _bindingId_.
	//     ...
	//
	// 14.3.3.3 RS: KeyedBindingInitialization
	//   SingleNameBinding : BindingIdentifier Initializer?
	//     ...
	//     4. If |Initializer| is present and _v_ is *undefined*, then
	//        a. If IsAnonymousFunctionDefinition(|Initializer|) is *true*,
	//           then
	//           i. Set _v_ to ? NamedEvaluation of |Initializer| with argument
	//              _bindingId_.
	//     ...

	printer::NodeFactory& factory = emitContext->factory;
	Node* assignedName;
	if (assignedNameText.length() > 0) {
		assignedName =
			factory.newStringLiteral(assignedNameText, TokenFlagsNone);
	} else {
		assignedName = getAssignedNameOfIdentifier(emitContext, node->name,
		                                           node->Initializer);
	}
	Node* initializer =
		finishTransformNamedEvaluation(emitContext, node->Initializer,
		                               assignedName, ignoreEmptyStringLiteral);
	return factory.updateBindingElement(
		node,
		node->DotDotDotToken,
		node->PropertyName,
		node->name,
		initializer);
}

// transformNamedEvaluationOfPropertyDeclaration — namedevaluation.go:417
Node* transformNamedEvaluationOfPropertyDeclaration(
	printer::EmitContext* emitContext, PropertyDeclaration* node,
	bool ignoreEmptyStringLiteral, const std::string& assignedNameText) {
	// 10.2.1.3 RS: EvaluateBody
	//   Initializer : `=` AssignmentExpression
	//     ...
	//     3. If IsAnonymousFunctionDefinition(|AssignmentExpression|) is
	//        *true*, then
	//        a. Let _value_ be ? NamedEvaluation of |Initializer| with argument
	//           _functionObject_.[[ClassFieldInitializerName]].
	//     ...

	printer::NodeFactory& factory = emitContext->factory;
	auto [assignedName, name] =
		getAssignedNameOfPropertyName(emitContext, node->name,
		                              assignedNameText);
	Node* initializer =
		finishTransformNamedEvaluation(emitContext, node->Initializer,
		                               assignedName, ignoreEmptyStringLiteral);
	return factory.updatePropertyDeclaration(
		node,
		node->modifiers,
		name,
		nullptr, /*postfixToken*/
		nullptr, /*typeNode*/
		initializer);
}

// transformNamedEvaluationOfAssignmentExpression — namedevaluation.go:438
Node* transformNamedEvaluationOfAssignmentExpression(
	printer::EmitContext* emitContext, BinaryExpression* node,
	bool ignoreEmptyStringLiteral, const std::string& assignedNameText) {
	// 13.15.2 RS: Evaluation
	//   AssignmentExpression : LeftHandSideExpression `=` AssignmentExpression
	//     1. If |LeftHandSideExpression| is neither an |ObjectLiteral| nor an
	//        |ArrayLiteral|, then
	//        a. Let _lref_ be ? Evaluation of |LeftHandSideExpression|.
	//        b. If IsAnonymousFunctionDefinition(|AssignmentExpression|) and
	//           IsIdentifierRef of |LeftHandSideExpression| are both *true*,
	//           then
	//           i. Let _rval_ be ? NamedEvaluation of |AssignmentExpression|
	//              with argument _lref_.[[ReferencedName]].
	//     ...
	//
	//   AssignmentExpression : LeftHandSideExpression `&&=` AssignmentExpression
	//     ...
	//     5. If IsAnonymousFunctionDefinition(|AssignmentExpression|) is *true*
	//        and IsIdentifierRef of |LeftHandSideExpression| is *true*, then
	//        a. Let _rval_ be ? NamedEvaluation of |AssignmentExpression| with
	//           argument _lref_.[[ReferencedName]].
	//     ...
	//
	//   AssignmentExpression : LeftHandSideExpression `||=` AssignmentExpression
	//     ...
	//     5. If IsAnonymousFunctionDefinition(|AssignmentExpression|) is *true*
	//        and IsIdentifierRef of |LeftHandSideExpression| is *true*, then
	//        a. Let _rval_ be ? NamedEvaluation of |AssignmentExpression| with
	//           argument _lref_.[[ReferencedName]].
	//     ...
	//
	//   AssignmentExpression : LeftHandSideExpression `??=` AssignmentExpression
	//     ...
	//     4. If IsAnonymousFunctionDefinition(|AssignmentExpression|) is *true*
	//        and IsIdentifierRef of |LeftHandSideExpression| is *true*, then
	//        a. Let _rval_ be ? NamedEvaluation of |AssignmentExpression| with
	//           argument _lref_.[[ReferencedName]].
	//     ...

	printer::NodeFactory& factory = emitContext->factory;
	Node* assignedName;
	if (assignedNameText.length() > 0) {
		assignedName =
			factory.newStringLiteral(assignedNameText, TokenFlagsNone);
	} else {
		assignedName = getAssignedNameOfIdentifier(emitContext, node->Left,
		                                           node->Right);
	}
	Node* right = finishTransformNamedEvaluation(
		emitContext, node->Right, assignedName, ignoreEmptyStringLiteral);
	return factory.updateBinaryExpression(
		node,
		nullptr, /*modifiers*/
		node->Left,
		nullptr, /*typeNode*/
		node->OperatorToken,
		right);
}

// transformNamedEvaluationOfExportAssignment — namedevaluation.go:483
Node* transformNamedEvaluationOfExportAssignment(
	printer::EmitContext* emitContext, ExportAssignment* node,
	bool ignoreEmptyStringLiteral, const std::string& assignedNameText) {
	// 16.2.3.7 RS: Evaluation
	//   ExportDeclaration : `export` `default` AssignmentExpression `;`
	//     1. If IsAnonymousFunctionDefinition(|AssignmentExpression|) is
	//        *true*, then
	//        a. Let _value_ be ? NamedEvaluation of |AssignmentExpression| with
	//           argument `"default"`.
	//     ...

	// NOTE: Since emit for `export =` translates to `module.exports = ...`,
	// the assigned name of the class or function is `""`.

	printer::NodeFactory& factory = emitContext->factory;
	Node* assignedName;
	if (assignedNameText.length() > 0) {
		assignedName =
			factory.newStringLiteral(assignedNameText, TokenFlagsNone);
	} else if (node->IsExportEquals) {
		assignedName = factory.newStringLiteral("", TokenFlagsNone);
	} else {
		assignedName = factory.newStringLiteral("default", TokenFlagsNone);
	}
	Node* expression =
		finishTransformNamedEvaluation(emitContext, node->Expression,
		                               assignedName, ignoreEmptyStringLiteral);
	return factory.updateExportAssignment(
		node,
		nullptr, /*modifiers*/
		node->IsExportEquals,
		nullptr, /*typeNode*/
		expression);
}

}  // namespace

// isClassNamedEvaluationHelperBlock — namedevaluation.go:16. Gets whether a
// node is a `static {}` block containing only a single call to the
// `__setFunctionName` helper where that call's second argument is the value
// stored in the `assignedName` property of the block's `EmitNode`.
bool isClassNamedEvaluationHelperBlock(printer::EmitContext* emitContext,
                                       Node* node) {
	if (!isClassStaticBlockDeclaration(node) ||
		node->as<ClassStaticBlockDeclaration>()
				->Body->statements()
				.size() != 1) {
		return false;
	}

	Node* statement = node->as<ClassStaticBlockDeclaration>()
	                      ->Body->statements()[0];
	if (isExpressionStatement(statement)) {
		Node* expression = statement->expression();
		if (emitContext->isCallToHelper(expression, "__setFunctionName")) {
			NodeList* arguments =
				expression->as<CallExpression>()->Arguments;
			return arguments->nodes.size() >= 2 &&
				arguments->nodes[1] == emitContext->assignedNameOf(node);
		}
	}
	return false;
}

// classHasExplicitlyAssignedName — namedevaluation.go:38. Gets whether a
// `ClassLikeDeclaration` has a `static {}` block containing only a single call
// to the `__setFunctionName` helper.
bool classHasExplicitlyAssignedName(printer::EmitContext* emitContext,
                                    Node* node) {
	if (emitContext->assignedNameOf(node) != nullptr) {
		for (Node* member : node->members()) {
			if (isClassNamedEvaluationHelperBlock(emitContext, member)) {
				return true;
			}
		}
	}
	return false;
}

// classHasDeclaredOrExplicitlyAssignedName — namedevaluation.go:54. Gets
// whether a `ClassLikeDeclaration` has a declared name or contains a
// `static {}` block containing only a single call to the `__setFunctionName`
// helper.
bool classHasDeclaredOrExplicitlyAssignedName(
	printer::EmitContext* emitContext, Node* node) {
	return node->name() != nullptr ||
		classHasExplicitlyAssignedName(emitContext, node);
}

// isNamedEvaluation — namedevaluation.go:85
bool isNamedEvaluation(printer::EmitContext* emitContext, Node* node) {
	return isNamedEvaluationAnd(emitContext, node, nullptr);
}

// isNamedEvaluationAnd — namedevaluation.go:89
bool isNamedEvaluationAnd(printer::EmitContext* emitContext, Node* node,
                          const std::function<bool(Node*)>& cb) {
	if (!isNamedEvaluationSource_(node)) {
		return false;
	}
	switch (node->kind) {
	case Kind::ShorthandPropertyAssignment:
		return isAnonymousFunctionDefinition(
			emitContext,
			node->as<ShorthandPropertyAssignment>()
				->ObjectAssignmentInitializer,
			cb);
	case Kind::PropertyAssignment:
	case Kind::VariableDeclaration:
	case Kind::Parameter:
	case Kind::BindingElement:
	case Kind::PropertyDeclaration:
		return isAnonymousFunctionDefinition(
			emitContext, node->initializer(), cb);
	case Kind::BinaryExpression:
		return isAnonymousFunctionDefinition(
			emitContext, node->as<BinaryExpression>()->Right, cb);
	case Kind::ExportAssignment:
		return isAnonymousFunctionDefinition(
			emitContext, node->expression(), cb);
	default:
		debugFail_("Unhandled case in isNamedEvaluation");
		return false;
	}
}

// injectClassNamedEvaluationHelperBlockIfMissing — namedevaluation.go:176.
// Injects a class `static {}` block used to dynamically set the name of a
// class, if one does not already exist.
Node* injectClassNamedEvaluationHelperBlockIfMissing(
	printer::EmitContext* emitContext, Node* node, Node* assignedName,
	Node* thisExpression) {
	// given:
	//
	//  let C = class {
	//  };
	//
	// produces:
	//
	//  let C = class {
	//      static { __setFunctionName(this, "C"); }
	//  };

	// NOTE: If the class has a `_classThis` assignment block, this helper will
	// be injected after that block.

	if (classHasExplicitlyAssignedName(emitContext, node)) {
		return node;
	}

	printer::NodeFactory& factory = emitContext->factory;
	Node* namedEvaluationBlock = createClassNamedEvaluationHelperBlock(
		emitContext, assignedName, thisExpression);
	if (node->name() != nullptr) {
		emitContext->setSourceMapRange(
			namedEvaluationBlock->body()->statements()[0],
			node->name()->loc);
	}

	// slices.IndexFunc(node.Members(), isClassThisAssignmentBlock) + 1
	int insertionIndex = 0;
	for (Node* member : node->members()) {
		if (isClassThisAssignmentBlock(emitContext, member)) {
			break;
		}
		insertionIndex++;
	}
	insertionIndex += 1;

	const std::vector<Node*>& nodeMembers = node->members();
	std::vector<Node*> leading(nodeMembers.begin(),
	                           nodeMembers.begin() + insertionIndex);
	std::vector<Node*> trailing(nodeMembers.begin() + insertionIndex,
	                            nodeMembers.end());

	std::vector<Node*> members;
	members.insert(members.end(), leading.begin(), leading.end());
	members.push_back(namedEvaluationBlock);
	members.insert(members.end(), trailing.begin(), trailing.end());
	NodeList* membersList = factory.newNodeList(members);
	membersList->loc = node->memberList()->loc;

	Node* oldNode = node;
	if (isClassDeclaration(node)) {
		node = factory.updateClassDeclaration(
			node->as<ClassDeclaration>(),
			node->modifiers(),
			node->name(),
			node->typeParameterList(),
			node->as<ClassDeclaration>()->HeritageClauses,
			membersList);
	} else {
		node = factory.updateClassExpression(
			node->as<ClassExpression>(),
			node->modifiers(),
			node->name(),
			node->typeParameterList(),
			node->as<ClassExpression>()->HeritageClauses,
			membersList);
	}

	emitContext->setAssignedName(node, assignedName);

	// Transfer ClassThis from old to new node, since UpdateClassExpression
	// creates a new node that won't have ClassThis set on it.
	if (Node* ct = emitContext->classThisOf(oldNode); ct != nullptr) {
		emitContext->setClassThis(node, ct);
	}

	return node;
}

// transformNamedEvaluation — namedevaluation.go:513. Performs a shallow
// transformation of a `NamedEvaluation` node, such that a valid name will be
// assigned.
Node* transformNamedEvaluation(printer::EmitContext* context, Node* node,
                               bool ignoreEmptyStringLiteral,
                               const std::string& assignedName) {
	switch (node->kind) {
	case Kind::PropertyAssignment:
		return transformNamedEvaluationOfPropertyAssignment(
			context, node->as<PropertyAssignment>(),
			ignoreEmptyStringLiteral, assignedName);
	case Kind::ShorthandPropertyAssignment:
		return transformNamedEvaluationOfShorthandAssignmentProperty(
			context, node->as<ShorthandPropertyAssignment>(),
			ignoreEmptyStringLiteral, assignedName);
	case Kind::VariableDeclaration:
		return transformNamedEvaluationOfVariableDeclaration(
			context, node->as<VariableDeclaration>(),
			ignoreEmptyStringLiteral, assignedName);
	case Kind::Parameter:
		return transformNamedEvaluationOfParameterDeclaration(
			context, node->as<ParameterDeclaration>(),
			ignoreEmptyStringLiteral, assignedName);
	case Kind::BindingElement:
		return transformNamedEvaluationOfBindingElement(
			context, node->as<BindingElement>(), ignoreEmptyStringLiteral,
			assignedName);
	case Kind::PropertyDeclaration:
		return transformNamedEvaluationOfPropertyDeclaration(
			context, node->as<PropertyDeclaration>(),
			ignoreEmptyStringLiteral, assignedName);
	case Kind::BinaryExpression:
		return transformNamedEvaluationOfAssignmentExpression(
			context, node->as<BinaryExpression>(), ignoreEmptyStringLiteral,
			assignedName);
	case Kind::ExportAssignment:
		return transformNamedEvaluationOfExportAssignment(
			context, node->as<ExportAssignment>(), ignoreEmptyStringLiteral,
			assignedName);
	default:
		debugFail_("Unhandled case in transformNamedEvaluation");
		return node;
	}
}

}  // namespace tsc::transformers::estransforms
