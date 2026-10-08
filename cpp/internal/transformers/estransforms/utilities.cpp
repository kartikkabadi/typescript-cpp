// Port of tsc/internal/transformers/estransforms/utilities.go — shared helpers
// used across the ES transforms. Also ports the two package-level helpers from
// async.go (assignmentTargetContainsSuperProperty, isUpdateExpression) that
// trackSuperAccess references.
#include "internal/transformers/estransforms/estransforms.h"

namespace tsc::transformers::estransforms {

// convertClassDeclarationToClassExpression — utilities.go:10
Node* convertClassDeclarationToClassExpression(
	printer::EmitContext* emitContext, Node* node) {
	Node* updated = emitContext->factory.newClassExpression(
		extractModifiers(emitContext, node->modifiers(),
		                 ~ModifierFlagsExportDefault),
		node->name(), node->typeParameterList(),
		node->as<ClassDeclaration>()->HeritageClauses, node->memberList());
	emitContext->setOriginal(updated, node->asNode());
	updated->loc = node->loc;
	return updated;
}

// createNotNullCondition — utilities.go:23
Node* createNotNullCondition(printer::EmitContext* emitContext, Node* left,
                             Node* right, bool invert) {
	Kind token = Kind::ExclamationEqualsEqualsToken;
	Kind op = Kind::AmpersandAmpersandToken;
	if (invert) {
		token = Kind::EqualsEqualsEqualsToken;
		op = Kind::BarBarToken;
	}

	return emitContext->factory.newBinaryExpression(
		nullptr,
		emitContext->factory.newBinaryExpression(
			nullptr, left, nullptr, emitContext->factory.newToken(token),
			emitContext->factory.newKeywordExpression(Kind::NullKeyword)),
		nullptr, emitContext->factory.newToken(op),
		emitContext->factory.newBinaryExpression(
			nullptr, right, nullptr, emitContext->factory.newToken(token),
			emitContext->factory.newVoidZeroExpression()));
}

// initSuperAccessVisitor — utilities.go:69
void superAccessState::initSuperAccessVisitor(
	printer::EmitContext* emitContext, printer::NodeFactory* factory_) {
	factory = factory_;
	superAccessVisitor = emitContext->newNodeVisitor(
		[this](Node* node) { return visitSuperAccessNode(node); });
}

// visitSuperAccessNode — utilities.go:77
Node* superAccessState::visitSuperAccessNode(Node* node) {
	switch (node->kind) {
	case Kind::CallExpression: {
		Node* callExpr = node->expression();
		if (isSuperProperty(callExpr)) {
			return substituteCallExpressionWithSuperAccess(node,
			                                               superAccessVisitor);
		}
		return superAccessVisitor->visitEachChild(node);
	}
	case Kind::PropertyAccessExpression:
		if (node->expression()->kind == Kind::SuperKeyword) {
			// super.x → _super.x
			return factory->newPropertyAccessExpression(
				superBinding, nullptr, node->name(), NodeFlagsNone);
		}
		return superAccessVisitor->visitEachChild(node);
	case Kind::ElementAccessExpression:
		if (node->expression()->kind == Kind::SuperKeyword) {
			// super[x] → _superIndex(x) or _superIndex(x).value
			return createSuperElementAccessInAsyncMethod(
				node->as<ElementAccessExpression>()->ArgumentExpression);
		}
		return superAccessVisitor->visitEachChild(node);
	// Don't recurse into non-arrow function scopes or classes
	case Kind::FunctionExpression:
	case Kind::FunctionDeclaration:
	case Kind::MethodDeclaration:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::Constructor:
	case Kind::ClassDeclaration:
	case Kind::ClassExpression:
		return node;
	default:
		return superAccessVisitor->visitEachChild(node);
	}
}

// substituteSuperAccessesInBody — utilities.go:111
Node* superAccessState::substituteSuperAccessesInBody(Node* body) {
	return superAccessVisitor->visitNode(body);
}

// substituteCallExpressionWithSuperAccess — utilities.go:116
Node* superAccessState::substituteCallExpressionWithSuperAccess(
	Node* call, NodeVisitor* visitor) {
	Node* expression = call->expression();
	Node* target = nullptr;

	if (isPropertyAccessExpression(expression)) {
		// super.x(args) → _super.x.call(this, args)
		target = factory->newPropertyAccessExpression(
			superBinding, nullptr, expression->name(), NodeFlagsNone);
	} else if (isElementAccessExpression(expression)) {
		// super[x](args) → _superIndex(x).call(this, args) or
		// _superIndex(x).value.call(this, args)
		target = createSuperElementAccessInAsyncMethod(
			expression->as<ElementAccessExpression>()->ArgumentExpression);
	} else {
		return visitor->visitEachChild(call);
	}

	Node* callTarget = factory->newPropertyAccessExpression(
		target, nullptr, factory->newIdentifier("call"), NodeFlagsNone);

	std::vector<Node*> allArgs;
	allArgs.push_back(factory->newThisExpression());
	if (call->argumentList() != nullptr) {
		NodeList* visitedArgs = visitor->visitNodes(call->argumentList());
		if (visitedArgs != nullptr) {
			allArgs.insert(allArgs.end(), visitedArgs->nodes.begin(),
			               visitedArgs->nodes.end());
		}
	}

	Node* result = factory->newCallExpression(
		callTarget, nullptr, nullptr, factory->newNodeList(allArgs),
		NodeFlagsNone);
	result->loc = call->loc;
	return result;
}

// createSuperElementAccessInAsyncMethod — utilities.go:158
Node* superAccessState::createSuperElementAccessInAsyncMethod(
	Node* argumentExpression) {
	Node* superIndexCall = factory->newCallExpression(
		superIndexBinding, nullptr, nullptr,
		factory->newNodeList({argumentExpression}), NodeFlagsNone);
	if (hasSuperPropertyAssignment) {
		return factory->newPropertyAccessExpression(
			superIndexCall, nullptr, factory->newIdentifier("value"),
			NodeFlagsNone);
	}
	return superIndexCall;
}

// createSuperAccessVariableStatement — utilities.go:182
//
// Create a variable declaration with a getter/setter (if binding) definition
// for each name:
//
//	const _super = Object.create(null, {
//	    x: { get: () => super.x },                           // read-only
//	    x: { get: () => super.x, set: (v) => super.x = v }, // read-write
//	});
Node* superAccessState::createSuperAccessVariableStatement() {
	printer::NodeFactory* f = factory;
	std::vector<Node*> accessors;

	for (const std::string& name : capturedSuperProperties->elements) {
		std::vector<Node*> descriptorProperties;

		// getter: get: () => super.name
		Node* getterBody = f->newPropertyAccessExpression(
			f->newKeywordExpression(Kind::SuperKeyword), nullptr,
			f->newIdentifier(name), NodeFlagsNone);
		Node* getterArrow = f->newArrowFunction(
			nullptr, nullptr, f->newNodeList({}), nullptr, nullptr,
			f->newToken(Kind::EqualsGreaterThanToken), getterBody);
		Node* getter = f->newPropertyAssignment(
			nullptr, f->newIdentifier("get"), nullptr, nullptr, getterArrow);
		descriptorProperties.push_back(getter);

		if (hasSuperPropertyAssignment) {
			// setter: set: v => super.name = v
			Node* vParam = f->newParameterDeclaration(
				nullptr, nullptr, f->newIdentifier("v"), nullptr, nullptr,
				nullptr);
			Node* superProp = f->newPropertyAccessExpression(
				f->newKeywordExpression(Kind::SuperKeyword), nullptr,
				f->newIdentifier(name), NodeFlagsNone);
			Node* assignExpr = f->newAssignmentExpression(
				superProp, f->newIdentifier("v"));
			Node* setterArrow = f->newArrowFunction(
				nullptr, nullptr, f->newNodeList({vParam}), nullptr, nullptr,
				f->newToken(Kind::EqualsGreaterThanToken), assignExpr);
			Node* setter = f->newPropertyAssignment(
				nullptr, f->newIdentifier("set"), nullptr, nullptr,
				setterArrow);
			descriptorProperties.push_back(setter);
		}

		Node* descriptor = f->newObjectLiteralExpression(
			f->newNodeList(descriptorProperties), false);
		Node* accessor = f->newPropertyAssignment(
			nullptr, f->newIdentifier(name), nullptr, nullptr, descriptor);
		accessors.push_back(accessor);
	}

	Node* descriptorsObject =
		f->newObjectLiteralExpression(f->newNodeList(accessors), true);

	Node* objectCreateCall = f->newCallExpression(
		f->newPropertyAccessExpression(f->newIdentifier("Object"), nullptr,
		                               f->newIdentifier("create"),
		                               NodeFlagsNone),
		nullptr, nullptr,
		f->newNodeList(
			{f->newKeywordExpression(Kind::NullKeyword), descriptorsObject}),
		NodeFlagsNone);

	Node* decl =
		f->newVariableDeclaration(superBinding, nullptr, nullptr,
		                          objectCreateCall);
	Node* declList = f->newVariableDeclarationList(f->newNodeList({decl}),
	                                             NodeFlagsConst);
	return f->newVariableStatement(nullptr, declList);
}

// trackSuperAccess — utilities.go:251
void superAccessState::trackSuperAccess(Node* node) {
	if (capturedSuperProperties == nullptr) {
		return;
	}
	switch (node->kind) {
	case Kind::PropertyAccessExpression:
		if (node->expression()->kind == Kind::SuperKeyword) {
			capturedSuperProperties->add(node->name()->text());
		}
		break;
	case Kind::ElementAccessExpression:
		if (node->expression()->kind == Kind::SuperKeyword) {
			hasSuperElementAccess = true;
		}
		break;
	case Kind::BinaryExpression:
		if (isAssignmentOperator(
				node->as<BinaryExpression>()->OperatorToken->kind) &&
		    assignmentTargetContainsSuperProperty(
				node->as<BinaryExpression>()->Left)) {
			hasSuperPropertyAssignment = true;
		}
		break;
	case Kind::PrefixUnaryExpression:
		if (isUpdateExpression(node) &&
		    assignmentTargetContainsSuperProperty(
				node->as<PrefixUnaryExpression>()->Operand)) {
			hasSuperPropertyAssignment = true;
		}
		break;
	case Kind::PostfixUnaryExpression:
		if (isUpdateExpression(node) &&
		    assignmentTargetContainsSuperProperty(
				node->as<PostfixUnaryExpression>()->Operand)) {
			hasSuperPropertyAssignment = true;
		}
		break;
	default:
		break;
	}
}

// assignmentTargetContainsSuperProperty — async.go:899
bool assignmentTargetContainsSuperProperty(Node* node) {
	switch (node->kind) {
	case Kind::PropertyAccessExpression:
	case Kind::ElementAccessExpression:
		return node->expression()->kind == Kind::SuperKeyword;
	case Kind::ParenthesizedExpression:
		return assignmentTargetContainsSuperProperty(node->expression());
	case Kind::ArrayLiteralExpression:
		for (Node* e : node->as<ArrayLiteralExpression>()->Elements->nodes) {
			if (assignmentTargetContainsSuperProperty(e)) {
				return true;
			}
		}
		return false;
	case Kind::ObjectLiteralExpression:
		for (Node* prop :
		     node->as<ObjectLiteralExpression>()->Properties->nodes) {
			switch (prop->kind) {
			case Kind::PropertyAssignment:
				if (assignmentTargetContainsSuperProperty(
						prop->initializer())) {
					return true;
				}
				break;
			case Kind::ShorthandPropertyAssignment:
				if (assignmentTargetContainsSuperProperty(prop->name())) {
					return true;
				}
				break;
			case Kind::SpreadAssignment:
				if (assignmentTargetContainsSuperProperty(
						prop->expression())) {
					return true;
				}
				break;
			default:
				break;
			}
		}
		return false;
	case Kind::SpreadElement:
		return assignmentTargetContainsSuperProperty(node->expression());
	default:
		return false;
	}
}

// isUpdateExpression — async.go:931
bool isUpdateExpression(Node* node) {
	if (isPrefixUnaryExpression(node)) {
		Kind op = node->as<PrefixUnaryExpression>()->Operator;
		return op == Kind::PlusPlusToken || op == Kind::MinusMinusToken;
	}
	if (isPostfixUnaryExpression(node)) {
		Kind op = node->as<PostfixUnaryExpression>()->Operator;
		return op == Kind::PlusPlusToken || op == Kind::MinusMinusToken;
	}
	return false;
}

// createAccessorPropertyBackingField — utilities.go:280
Node* createAccessorPropertyBackingField(printer::NodeFactory* f, Node* node,
                                         ModifierList* modifiers,
                                         Node* initializer) {
	return f->updatePropertyDeclaration(
		node->as<PropertyDeclaration>(), modifiers,
		f->newGeneratedPrivateNameForNode(
			node->name(),
			printer::AutoGenerateOptions{0, "", "_accessor_storage"}),
		nullptr /*postfixToken*/, nullptr /*typeNode*/, initializer);
}

}  // namespace tsc::transformers::estransforms
