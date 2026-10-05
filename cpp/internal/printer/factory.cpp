// factory.go — port of tsc/internal/printer/factory.go
//
// NodeFactory wraps ast::NodeFactory with emit-tracking hooks from an
// EmitContext, plus convenience constructors for generated identifiers and
// emit-helper call sites.
#include "internal/printer/emitcontext.h"
#include "internal/printer/printer.h"

#include <cassert>
#include <string>
#include <vector>

namespace tsc::printer {

// NewNodeFactory — factory.go:18
NodeFactory::NodeFactory(EmitContext* context) : emitContext(context) {
	hooks.onCreate = [context](Node* n) { context->onCreate(n); };
	hooks.onUpdate = [context](Node* updated, Node* original) {
		context->onUpdate(updated, original);
	};
	hooks.onClone = [context](Node* updated, Node* original) {
		context->onClone(updated, original);
	};
}

// newGeneratedIdentifier — factory.go:29
Node* NodeFactory::newGeneratedIdentifier(
	GeneratedIdentifierFlags kind, std::string text, Node* node,
	const AutoGenerateOptions& options) {
	AutoGenerateId id = AutoGenerateId(nextAutoGenerateId());

	if (text.empty()) {
		if (node == nullptr) {
			text = "(auto@" + std::to_string(id) + ")";
		} else if (isMemberName(node)) {
			text = node->text();
		} else {
			text = "(generated@" +
			       std::to_string(getNodeId(
					   emitContext->getNodeForGeneratedNameWorker(
						   node, id))) +
			       ")";
		}
		text = FormatGeneratedName(false /*privateName*/, options.Prefix,
		                           text, options.Suffix);
	}

	Node* name = newIdentifier(text);
	auto autoGenerate = AutoGenerateInfo{
		.Id = id,
		.Flags = kind | (options.Flags & ~GeneratedIdentifierFlagsKindMask),
		.Prefix = options.Prefix,
		.Suffix = options.Suffix,
		.Node = node,
	};
	emitContext->autoGenerate[name] = autoGenerate;
	return name;
}

// NewTempVariable — factory.go:62
Node* NodeFactory::newTempVariable(const AutoGenerateOptions& options) {
	return newGeneratedIdentifier(GeneratedIdentifierFlagsAuto, "",
	                              nullptr /*node*/, options);
}

// NewLoopVariable — factory.go:74
Node* NodeFactory::newLoopVariable(const AutoGenerateOptions& options) {
	return newGeneratedIdentifier(GeneratedIdentifierFlagsLoop, "",
	                              nullptr /*node*/, options);
}

// NewUniqueName — factory.go:84
Node* NodeFactory::newUniqueName(std::string text,
                                 const AutoGenerateOptions& options) {
	return newGeneratedIdentifier(GeneratedIdentifierFlagsUnique,
	                              std::move(text), nullptr /*node*/, options);
}

// NewGeneratedNameForNode — factory.go:94
Node* NodeFactory::newGeneratedNameForNode(
	Node* node, const AutoGenerateOptions& options) {
	AutoGenerateOptions opts = options;
	if (!opts.Prefix.empty() || !opts.Suffix.empty()) {
		opts.Flags |= GeneratedIdentifierFlagsOptimistic;
	}
	return newGeneratedIdentifier(GeneratedIdentifierFlagsNode, "", node,
	                              opts);
}

// newGeneratedPrivateIdentifier — factory.go:107
Node* NodeFactory::newGeneratedPrivateIdentifier(
	GeneratedIdentifierFlags kind, std::string text, Node* node,
	const AutoGenerateOptions& options) {
	AutoGenerateId id = AutoGenerateId(nextAutoGenerateId());

	if (text.empty()) {
		if (node == nullptr) {
			text = "(auto@" + std::to_string(id) + ")";
		} else if (isMemberName(node)) {
			text = node->text();
		} else {
			text = "(generated@" +
			       std::to_string(getNodeId(
					   emitContext->getNodeForGeneratedNameWorker(
						   node, id))) +
			       ")";
		}
		text = FormatGeneratedName(true /*privateName*/, options.Prefix,
		                           text, options.Suffix);
	} else if (text.front() != '#') {
		assert(false && "First character of private identifier must be #");
	}

	Node* name = newPrivateIdentifier(text);
	auto autoGenerate = AutoGenerateInfo{
		.Id = id,
		.Flags = kind | (options.Flags & ~GeneratedIdentifierFlagsKindMask),
		.Prefix = options.Prefix,
		.Suffix = options.Suffix,
		.Node = node,
	};
	emitContext->autoGenerate[name] = autoGenerate;
	return name;
}

// NewUniquePrivateName — factory.go:140
Node* NodeFactory::newUniquePrivateName(std::string text,
                                        const AutoGenerateOptions& options) {
	return newGeneratedPrivateIdentifier(GeneratedIdentifierFlagsUnique,
	                                     std::move(text), nullptr /*node*/,
	                                     options);
}

// NewGeneratedPrivateNameForNode — factory.go:150
Node* NodeFactory::newGeneratedPrivateNameForNode(
	Node* node, const AutoGenerateOptions& options) {
	AutoGenerateOptions opts = options;
	if (!opts.Prefix.empty() || !opts.Suffix.empty()) {
		opts.Flags |= GeneratedIdentifierFlagsOptimistic;
	}
	return newGeneratedPrivateIdentifier(GeneratedIdentifierFlagsNode, "",
	                                     node, opts);
}

// NewStringLiteralFromNode — factory.go:165
Node* NodeFactory::newStringLiteralFromNode(Node* textSourceNode) {
	std::string text;
	switch (textSourceNode->kind) {
	case Kind::Identifier:
	case Kind::PrivateIdentifier:
	case Kind::JsxNamespacedName:
	case Kind::StringLiteral:
	case Kind::NumericLiteral:
	case Kind::BigIntLiteral:
	case Kind::NoSubstitutionTemplateLiteral:
	case Kind::TemplateHead:
	case Kind::TemplateMiddle:
	case Kind::TemplateTail:
	case Kind::RegularExpressionLiteral:
		text = textSourceNode->text();
	default:
		break;
	}
	Node* node = newStringLiteral(text, TokenFlagsNone);
	emitContext->textSource[node] = textSourceNode;
	return node;
}

//
// Common Tokens
//

// NewThisExpression — factory.go:193
Node* NodeFactory::newThisExpression() {
	return newKeywordExpression(Kind::ThisKeyword);
}

// NewTrueExpression — factory.go:197
Node* NodeFactory::newTrueExpression() {
	return newKeywordExpression(Kind::TrueKeyword);
}

// NewFalseExpression — factory.go:201
Node* NodeFactory::newFalseExpression() {
	return newKeywordExpression(Kind::FalseKeyword);
}

//
// Common Operators
//

// NewCommaExpression — factory.go:209
Node* NodeFactory::newCommaExpression(Node* left, Node* right) {
	return newBinaryExpression(nullptr /*modifiers*/, left, nullptr /*typeNode*/,
	                           newToken(Kind::CommaToken), right);
}

// NewAssignmentExpression — factory.go:213
Node* NodeFactory::newAssignmentExpression(Node* left, Node* right) {
	return newBinaryExpression(nullptr /*modifiers*/, left, nullptr /*typeNode*/,
	                           newToken(Kind::EqualsToken), right);
}

// NewLogicalORExpression — factory.go:217
Node* NodeFactory::newLogicalORExpression(Node* left, Node* right) {
	return newBinaryExpression(nullptr /*modifiers*/, left, nullptr /*typeNode*/,
	                           newToken(Kind::BarBarToken), right);
}

// NewLogicalANDExpression — factory.go:221
Node* NodeFactory::newLogicalANDExpression(Node* left, Node* right) {
	return newBinaryExpression(nullptr /*modifiers*/, left, nullptr /*typeNode*/,
	                           newToken(Kind::AmpersandAmpersandToken), right);
}

// NewStrictEqualityExpression — factory.go:229
Node* NodeFactory::newStrictEqualityExpression(Node* left, Node* right) {
	return newBinaryExpression(nullptr /*modifiers*/, left, nullptr /*typeNode*/,
	                           newToken(Kind::EqualsEqualsEqualsToken), right);
}

// NewStrictInequalityExpression — factory.go:233
Node* NodeFactory::newStrictInequalityExpression(Node* left, Node* right) {
	return newBinaryExpression(
		nullptr /*modifiers*/, left, nullptr /*typeNode*/,
		newToken(Kind::ExclamationEqualsEqualsToken), right);
}

//
// Compound Nodes
//

// NewVoidZeroExpression — factory.go:241
Node* NodeFactory::newVoidZeroExpression() {
	return newVoidExpression(newNumericLiteral("0", TokenFlagsNone));
}

// flattenCommaElement — factory.go:245
static void flattenCommaElement(Node* node, std::vector<Node*>& expressions) {
	if (node->kind == Kind::BinaryExpression && nodeIsSynthesized(node) &&
	    node->as<BinaryExpression>()->OperatorToken->kind ==
		    Kind::CommaToken) {
		flattenCommaElement(node->as<BinaryExpression>()->Left, expressions);
		flattenCommaElement(node->as<BinaryExpression>()->Right, expressions);
	} else {
		expressions.push_back(node);
	}
}

// flattenCommaElements — factory.go:255
static std::vector<Node*> flattenCommaElements(
	const std::vector<Node*>& expressions) {
	std::vector<Node*> result;
	for (Node* expression : expressions) {
		flattenCommaElement(expression, result);
	}
	return result;
}

// InlineExpressions — factory.go:264
Node* NodeFactory::inlineExpressions(
	const std::vector<Node*>& expressions) {
	if (expressions.empty()) {
		return nullptr;
	}
	if (expressions.size() == 1) {
		return expressions[0];
	}
	auto flattened = flattenCommaElements(expressions);
	Node* expression = flattened[0];
	for (size_t i = 1; i < flattened.size(); i++) {
		expression = newCommaExpression(expression, flattened[i]);
	}
	return expression;
}

//
// Utilities
//

// CreateExpressionFromEntityName — factory.go:283
Node* NodeFactory::createExpressionFromEntityName(Node* node) {
	if (isQualifiedName(node)) {
		Node* left =
			createExpressionFromEntityName(node->as<QualifiedName>()->Left);
		Node* right = node->as<QualifiedName>()->Right->clone(*this);
		right->loc = node->as<QualifiedName>()->Right->loc;
		// TODO(rbuckton): Does this need to be parented?
		right->parent = node->as<QualifiedName>()->Right->parent;
		Node* propAccess = newPropertyAccessExpression(
			left, nullptr, right, NodeFlagsNone);
		propAccess->loc = node->loc;
		return propAccess;
	}
	Node* res = node->clone(*this);
	res->loc = node->loc;
	// TODO(rbuckton): Does this need to be parented?
	res->parent = node->parent;
	return res;
}

// RestoreEnclosingLabel — factory.go:301
Node* NodeFactory::restoreEnclosingLabel(Node* node,
                                         Node* outermostLabeledStatement) {
	if (outermostLabeledStatement == nullptr) {
		return node;
	}
	Node* innerLabel = node;
	auto* ols = outermostLabeledStatement->as<LabeledStatement>();
	if (ols->Statement->kind == Kind::LabeledStatement) {
		innerLabel =
			restoreEnclosingLabel(node, ols->Statement);
	}
	return updateLabeledStatement(ols, ols->Label, innerLabel);
}

// CreateForOfBindingStatement — factory.go:317
Node* NodeFactory::createForOfBindingStatement(Node* node, Node* boundValue) {
	if (node->kind == Kind::VariableDeclarationList) {
		auto* vdl = node->as<VariableDeclarationList>();
		Node* firstDeclaration = vdl->Declarations->nodes[0];
		Node* updatedDeclaration = updateVariableDeclaration(
			firstDeclaration->as<VariableDeclaration>(),
			firstDeclaration->name(), nullptr /*exclamationToken*/,
			nullptr /*type*/, boundValue);
		Node* statement = newVariableStatement(
			nullptr,
			updateVariableDeclarationList(
				vdl, newNodeList({updatedDeclaration}), vdl->flags));
		statement->loc = node->loc;
		return statement;
	}
	Node* updatedExpression = newAssignmentExpression(node, boundValue);
	updatedExpression->loc = node->loc;
	Node* statement = newExpressionStatement(updatedExpression);
	statement->loc = node->loc;
	return statement;
}

// NewTypeCheck — factory.go:345
Node* NodeFactory::newTypeCheck(Node* value, const std::string& tag) {
	if (tag == "null") {
		return newStrictEqualityExpression(
			value, newKeywordExpression(Kind::NullKeyword));
	} else if (tag == "undefined") {
		return newStrictEqualityExpression(value, newVoidZeroExpression());
	} else {
		return newStrictEqualityExpression(
			newTypeOfExpression(value),
			newStringLiteral(tag, TokenFlagsNone));
	}
}

// NewMethodCall — factory.go:355
Node* NodeFactory::newMethodCall(Node* object, Node* methodName,
                                 std::vector<Node*> argumentsList) {
	// Preserve the optionality of `object`.
	if (object->kind == Kind::CallExpression &&
	    (object->flags & NodeFlagsOptionalChain) != 0) {
		return newCallExpression(
			newPropertyAccessExpression(object, nullptr, methodName,
			                            NodeFlagsNone),
			nullptr, nullptr, newNodeList(std::move(argumentsList)),
			NodeFlagsOptionalChain);
	}
	return newCallExpression(
		newPropertyAccessExpression(object, nullptr, methodName,
		                            NodeFlagsNone),
		nullptr, nullptr, newNodeList(std::move(argumentsList)),
		NodeFlagsNone);
}

// NewGlobalMethodCall — factory.go:375
Node* NodeFactory::newGlobalMethodCall(const std::string& globalObjectName,
                                       const std::string& methodName,
                                       std::vector<Node*> argumentsList) {
	return newMethodCall(newIdentifier(globalObjectName),
	                     newIdentifier(methodName),
	                     std::move(argumentsList));
}

// NewFunctionCallCall — factory.go:379
Node* NodeFactory::newFunctionCallCall(Node* target, Node* thisArg,
                                       std::vector<Node*> argumentsList) {
	assert(thisArg != nullptr &&
	       "Attempted to construct function call call without this argument "
	       "expression");
	std::vector<Node*> args;
	args.reserve(1 + argumentsList.size());
	args.push_back(thisArg);
	args.insert(args.end(), argumentsList.begin(), argumentsList.end());
	return newMethodCall(target, newIdentifier("call"), args);
}

// NewArraySliceCall — factory.go:387
Node* NodeFactory::newArraySliceCall(Node* array, int start) {
	std::vector<Node*> args;
	if (start != 0) {
		args.push_back(
			newNumericLiteral(std::to_string(start), TokenFlagsNone));
	}
	return newMethodCall(array, newIdentifier("slice"), args);
}

// isIgnorableParen — factory.go:407
bool NodeFactory::isIgnorableParen(Node* node) {
	return node->kind == Kind::ParenthesizedExpression &&
	       nodeIsSynthesized(node) &&
	       rangeIsSynthesized(emitContext->sourceMapRange(node)) &&
	       rangeIsSynthesized(emitContext->commentRange(node)); // &&
	// len(emitContext.SyntheticLeadingComments(node)) == 0 &&
	// len(emitContext.SyntheticTrailingComments(node)) == 0
}

// updateOuterExpression — factory.go:416
Node* NodeFactory::updateOuterExpression(Node* outerExpression,
                                         Node* expression) {
	switch (outerExpression->kind) {
	case Kind::ParenthesizedExpression:
		return updateParenthesizedExpression(
			outerExpression->as<ParenthesizedExpression>(), expression);
	case Kind::TypeAssertionExpression:
		return updateTypeAssertion(outerExpression->as<TypeAssertion>(),
		                           outerExpression->type(), expression);
	case Kind::AsExpression:
		return updateAsExpression(outerExpression->as<AsExpression>(),
		                          expression, outerExpression->type());
	case Kind::SatisfiesExpression:
		return updateSatisfiesExpression(
			outerExpression->as<SatisfiesExpression>(), expression,
			outerExpression->type());
	case Kind::NonNullExpression:
		return updateNonNullExpression(
			outerExpression->as<NonNullExpression>(), expression,
			outerExpression->flags);
	case Kind::ExpressionWithTypeArguments:
		return updateExpressionWithTypeArguments(
			outerExpression->as<ExpressionWithTypeArguments>(), expression,
			outerExpression->typeArgumentList());
	case Kind::PartiallyEmittedExpression:
		return updatePartiallyEmittedExpression(
			outerExpression->as<PartiallyEmittedExpression>(), expression);
	default:
		assert(false && "Unexpected outer expression kind");
		return nullptr;
	}
}

// RestoreOuterExpressions — factory.go:437
Node* NodeFactory::restoreOuterExpressions(Node* outerExpression,
                                           Node* innerExpression,
                                           OuterExpressionKinds kinds) {
	if (outerExpression != nullptr &&
	    isOuterExpression(outerExpression, kinds) &&
	    !isIgnorableParen(outerExpression)) {
		return updateOuterExpression(
			outerExpression,
			restoreOuterExpressions(outerExpression->expression(),
			                        innerExpression, OEKAll));
	}
	return innerExpression;
}

// EnsureUseStrict — factory.go:448
std::vector<Node*> NodeFactory::ensureUseStrict(
	std::vector<Node*> statements) {
	for (Node* statement : statements) {
		if (isPrologueDirective(statement) &&
		    statement->expression()->text() == "use strict") {
			return statements;
		} else {
			break;
		}
	}
	Node* useStrictPrologue = newExpressionStatement(
		newStringLiteral("use strict", TokenFlagsNone));
	statements.insert(statements.begin(), useStrictPrologue);
	return statements;
}

// SplitStandardPrologue — factory.go:462
std::pair<std::vector<Node*>, std::vector<Node*>>
NodeFactory::splitStandardPrologue(const std::vector<Node*>& source) {
	for (size_t i = 0; i < source.size(); i++) {
		if (!isPrologueDirective(source[i])) {
			return {std::vector<Node*>(source.begin(), source.begin() + i),
			        std::vector<Node*>(source.begin() + i, source.end())};
		}
	}
	return {source, {}};
}

// SplitCustomPrologue — factory.go:472
std::pair<std::vector<Node*>, std::vector<Node*>>
NodeFactory::splitCustomPrologue(const std::vector<Node*>& source) {
	for (size_t i = 0; i < source.size(); i++) {
		if (!isPrologueDirective(source[i]) ||
		    (emitContext->emitFlags(source[i]) & EFCustomPrologue) == 0) {
			return {std::vector<Node*>(source.begin(), source.begin() + i),
			        std::vector<Node*>(source.begin() + i, source.end())};
		}
	}
	return {{}, source};
}

//
// Declaration Names
//

// getName — factory.go:496
Node* NodeFactory::getName(Node* node, EmitFlags emitFlags,
                           const AssignedNameOptions& opts) {
	Node* nodeName = nullptr;
	if (node != nullptr) {
		if (opts.IgnoreAssignedName) {
			nodeName = getNonAssignedNameOfDeclaration(node);
		} else {
			nodeName = getNameOfDeclaration(node);
		}
	}

	if (nodeName != nullptr) {
		Node* name = nodeName->clone(*this);
		if (!opts.AllowComments) {
			emitFlags |= EFNoComments;
		}
		if (!opts.AllowSourceMaps) {
			emitFlags |= EFNoSourceMap;
		}
		emitContext->addEmitFlags(name, emitFlags);
		return name;
	}

	return newGeneratedNameForNode(node);
}

// GetLocalName — factory.go:524
Node* NodeFactory::getLocalName(Node* node, const AssignedNameOptions& opts) {
	return getName(node, EFLocalName, opts);
}

// GetExportName — factory.go:539
Node* NodeFactory::getExportName(Node* node, const AssignedNameOptions& opts) {
	return getName(node, EFExportName, opts);
}

// GetDeclarationName — factory.go:552
Node* NodeFactory::getDeclarationName(Node* node, const NameOptions& opts) {
	return getName(node, EFNone,
	               AssignedNameOptions{.AllowComments = opts.AllowComments,
	                                   .AllowSourceMaps = opts.AllowSourceMaps});
}

// GetNamespaceMemberName — factory.go:561
Node* NodeFactory::getNamespaceMemberName(Node* ns, Node* name,
                                          const NameOptions& opts) {
	if (!emitContext->hasAutoGenerateInfo(name)) {
		name = name->clone(*this);
	}
	Node* qualifiedName =
		newPropertyAccessExpression(ns, nullptr /*questionDotToken*/, name,
		                            NodeFlagsNone);
	emitContext->assignCommentAndSourceMapRanges(qualifiedName, name);
	if (!opts.AllowComments) {
		emitContext->addEmitFlags(qualifiedName, EFNoComments);
	}
	if (!opts.AllowSourceMaps) {
		emitContext->addEmitFlags(qualifiedName, EFNoSourceMap);
	}
	return qualifiedName;
}

// GetExternalModuleOrNamespaceExportName — factory.go:580
Node* NodeFactory::getExternalModuleOrNamespaceExportName(
	Node* ns, Node* node, bool allowComments, bool allowSourceMaps) {
	if (ns != nullptr &&
	    hasSyntacticModifier(node, ModifierFlagsExport)) {
		NameOptions nameOpts{.AllowComments = allowComments,
		                     .AllowSourceMaps = allowSourceMaps};
		return getNamespaceMemberName(
			ns, getDeclarationName(node, nameOpts), nameOpts);
	}
	return getExportName(
		node, AssignedNameOptions{.AllowComments = allowComments,
		                          .AllowSourceMaps = allowSourceMaps});
}

//
// Emit Helpers
//

// NewUnscopedHelperName — factory.go:593
Node* NodeFactory::newUnscopedHelperName(const std::string& name) {
	Node* node = newIdentifier(name);
	emitContext->setEmitFlags(node, EFHelperName);
	return node;
}

// TypeScript Helpers

// NewDecorateHelper — factory.go:601
Node* NodeFactory::newDecorateHelper(std::vector<Node*> decoratorExpressions,
                                     Node* target, Node* memberName,
                                     Node* descriptor) {
	emitContext->requestEmitHelper(decorateHelper);

	std::vector<Node*> argumentsArray;
	argumentsArray.push_back(newArrayLiteralExpression(
		newNodeList(std::move(decoratorExpressions)), true));
	argumentsArray.push_back(target);
	if (memberName != nullptr) {
		argumentsArray.push_back(memberName);
		if (descriptor != nullptr) {
			argumentsArray.push_back(descriptor);
		}
	}

	return newCallExpression(newUnscopedHelperName("__decorate"),
	                         nullptr /*questionDotToken*/,
	                         nullptr /*typeArguments*/,
	                         newNodeList(std::move(argumentsArray)),
	                         NodeFlagsNone);
}

// NewMetadataHelper — factory.go:623
Node* NodeFactory::newMetadataHelper(const std::string& metadataKey,
                                     Node* metadataValue) {
	emitContext->requestEmitHelper(metadataHelper);

	return newCallExpression(
		newUnscopedHelperName("__metadata"), nullptr /*questionDotToken*/,
		nullptr /*typeArguments*/,
		newNodeList({newStringLiteral(metadataKey, TokenFlagsNone),
		             metadataValue}),
		NodeFlagsNone);
}

// NewParamHelper — factory.go:638
Node* NodeFactory::newParamHelper(Node* expression, int parameterOffset,
                                  TextRange location) {
	emitContext->requestEmitHelper(paramHelper);
	Node* helper = newCallExpression(
		newUnscopedHelperName("__param"), nullptr /*questionDotToken*/,
		nullptr /*typeArguments*/,
		newNodeList({newNumericLiteral(std::to_string(parameterOffset),
		                               TokenFlagsNone),
		             expression}),
		NodeFlagsNone);
	helper->loc = location;
	return helper;
}

// ESNext Helpers

// NewAddDisposableResourceHelper — factory.go:653
Node* NodeFactory::newAddDisposableResourceHelper(Node* envBinding, Node* value,
                                                  bool async_) {
	emitContext->requestEmitHelper(addDisposableResourceHelper);
	return newCallExpression(
		newUnscopedHelperName("__addDisposableResource"),
		nullptr /*questionDotToken*/, nullptr /*typeArguments*/,
		newNodeList({envBinding, value,
		             newKeywordExpression(async_ ? Kind::TrueKeyword
		                                         : Kind::FalseKeyword)}),
		NodeFlagsNone);
}

// NewDisposeResourcesHelper — factory.go:664
Node* NodeFactory::newDisposeResourcesHelper(Node* envBinding) {
	emitContext->requestEmitHelper(disposeResourcesHelper);
	return newCallExpression(
		newUnscopedHelperName("__disposeResources"),
		nullptr /*questionDotToken*/, nullptr /*typeArguments*/,
		newNodeList({envBinding}), NodeFlagsNone);
}

// Class Fields Helpers

// NewClassPrivateFieldGetHelper — factory.go:686
Node* NodeFactory::newClassPrivateFieldGetHelper(Node* receiver, Node* state,
                                                 PrivateIdentifierKind kind,
                                                 Node* fn) {
	emitContext->requestEmitHelper(classPrivateFieldGetHelper);
	std::vector<Node*> args;
	if (fn == nullptr) {
		args = {receiver, state,
		        newStringLiteral(privateIdentifierKindString(kind),
		                         TokenFlagsNone)};
	} else {
		args = {receiver, state,
		        newStringLiteral(privateIdentifierKindString(kind),
		                         TokenFlagsNone),
		        fn};
	}
	return newCallExpression(newUnscopedHelperName("__classPrivateFieldGet"),
	                         nullptr /*questionDotToken*/,
	                         nullptr /*typeArguments*/,
	                         newNodeList(std::move(args)), NodeFlagsNone);
}

// NewClassPrivateFieldSetHelper — factory.go:703
Node* NodeFactory::newClassPrivateFieldSetHelper(Node* receiver, Node* state,
                                                 Node* value,
                                                 PrivateIdentifierKind kind,
                                                 Node* fn) {
	emitContext->requestEmitHelper(classPrivateFieldSetHelper);
	std::vector<Node*> args;
	if (fn == nullptr) {
		args = {receiver, state, value,
		        newStringLiteral(privateIdentifierKindString(kind),
		                         TokenFlagsNone)};
	} else {
		args = {receiver, state, value,
		        newStringLiteral(privateIdentifierKindString(kind),
		                         TokenFlagsNone),
		        fn};
	}
	return newCallExpression(newUnscopedHelperName("__classPrivateFieldSet"),
	                         nullptr /*questionDotToken*/,
	                         nullptr /*typeArguments*/,
	                         newNodeList(std::move(args)), NodeFlagsNone);
}

// NewClassPrivateFieldInHelper — factory.go:720
Node* NodeFactory::newClassPrivateFieldInHelper(Node* state, Node* receiver) {
	emitContext->requestEmitHelper(classPrivateFieldInHelper);
	return newCallExpression(newUnscopedHelperName("__classPrivateFieldIn"),
	                         nullptr /*questionDotToken*/,
	                         nullptr /*typeArguments*/,
	                         newNodeList({state, receiver}), NodeFlagsNone);
}

// NewObjectDefinePropertyCall — factory.go:732
Node* NodeFactory::newObjectDefinePropertyCall(Node* target, Node* name,
                                               Node* descriptor) {
	return newCallExpression(
		newPropertyAccessExpression(
			newIdentifier("Object"), nullptr,
			newIdentifier("defineProperty"), NodeFlagsNone),
		nullptr /*questionDotToken*/, nullptr /*typeArguments*/,
		newNodeList({target, name, descriptor}), NodeFlagsNone);
}

// NewReflectGetCall — factory.go:748
Node* NodeFactory::newReflectGetCall(Node* target, Node* propertyKey,
                                     Node* receiver) {
	return newCallExpression(
		newPropertyAccessExpression(newIdentifier("Reflect"), nullptr,
		                            newIdentifier("get"), NodeFlagsNone),
		nullptr /*questionDotToken*/, nullptr /*typeArguments*/,
		newNodeList({target, propertyKey, receiver}), NodeFlagsNone);
}

// NewReflectSetCall — factory.go:763
Node* NodeFactory::newReflectSetCall(Node* target, Node* propertyKey,
                                     Node* value, Node* receiver) {
	return newCallExpression(
		newPropertyAccessExpression(newIdentifier("Reflect"), nullptr,
		                            newIdentifier("set"), NodeFlagsNone),
		nullptr /*questionDotToken*/, nullptr /*typeArguments*/,
		newNodeList({target, propertyKey, value, receiver}), NodeFlagsNone);
}

// NewFunctionBindCall — factory.go:780
Node* NodeFactory::newFunctionBindCall(Node* target, Node* thisArg,
                                       std::vector<Node*> argumentsList) {
	std::vector<Node*> args;
	args.reserve(1 + argumentsList.size());
	args.push_back(thisArg);
	args.insert(args.end(), argumentsList.begin(), argumentsList.end());
	return newMethodCall(target, newIdentifier("bind"), std::move(args));
}

// NewImmediatelyInvokedArrowFunction — factory.go:788
Node* NodeFactory::newImmediatelyInvokedArrowFunction(
	std::vector<Node*> statements) {
	Node* arrow = newArrowFunction(
		nullptr /*modifiers*/, nullptr /*typeParameters*/,
		newNodeList({}) /*parameters*/, nullptr /*returnType*/,
		nullptr /*fullSignature*/,
		newToken(Kind::EqualsGreaterThanToken) /*equalsGreaterThanToken*/,
		newBlock(newNodeList(std::move(statements)), true));
	return newCallExpression(newParenthesizedExpression(arrow),
	                         nullptr /*questionDotToken*/,
	                         nullptr /*typeArguments*/, newNodeList({}),
	                         NodeFlagsNone);
}

// NewExportDefault — factory.go:808
Node* NodeFactory::newExportDefault(Node* expression) {
	return newExportAssignment(nullptr, false, nullptr, expression);
}

// NewExternalModuleExport — factory.go:813
Node* NodeFactory::newExternalModuleExport(Node* name) {
	Node* specifier = newExportSpecifier(false, nullptr, name);
	Node* namedExports = newNamedExports(newNodeList({specifier}));
	return newExportDeclaration(nullptr, false, namedExports, nullptr,
	                            nullptr);
}

// ES2018 Helpers

// NewAssignHelper — factory.go:821
Node* NodeFactory::newAssignHelper(std::vector<Node*> attributesSegments,
                                   ScriptTarget scriptTarget) {
	return newCallExpression(
		newPropertyAccessExpression(newIdentifier("Object"), nullptr,
		                            newIdentifier("assign"), NodeFlagsNone),
		nullptr, nullptr, newNodeList(std::move(attributesSegments)),
		NodeFlagsNone);
}

// ES2018 Destructuring Helpers

// NewRestHelper — factory.go:827
Node* NodeFactory::newRestHelper(Node* value,
                                 const std::vector<Node*>& elements,
                                 std::vector<Node*> computedTempVariables,
                                 TextRange location) {
	emitContext->requestEmitHelper(restHelper);
	std::vector<Node*> propertyNames;
	size_t computedTempVariableOffset = 0;
	for (size_t i = 0; i < elements.size(); i++) {
		if (i == elements.size() - 1) {
			break;
		}
		Node* propertyName = tryGetPropertyNameOfBindingOrAssignmentElement(
			elements[i]);
		if (propertyName != nullptr) {
			if (propertyName->kind == Kind::ComputedPropertyName) {
				assert(computedTempVariables.size() >
					       computedTempVariableOffset &&
				       "Encountered computed property name but "
				       "'computedTempVariables' argument was not provided.");
				Node* temp =
					computedTempVariables[computedTempVariableOffset];
				computedTempVariableOffset++;
				// typeof _tmp === "symbol" ? _tmp : _tmp + ""
				propertyNames.push_back(newConditionalExpression(
					newTypeCheck(temp, "symbol"),
					newToken(Kind::QuestionToken), temp,
					newToken(Kind::ColonToken),
					newBinaryExpression(
						nullptr, temp, nullptr,
						newToken(Kind::PlusToken),
						newStringLiteral("", TokenFlagsNone))));
			} else {
				propertyNames.push_back(
					newStringLiteralFromNode(propertyName));
			}
		}
	}
	Node* propNames =
		newArrayLiteralExpression(newNodeList(std::move(propertyNames)),
		                          false);
	propNames->loc = location;
	return newCallExpression(newUnscopedHelperName("__rest"), nullptr,
	                         nullptr,
	                         newNodeList({value, propNames}), NodeFlagsNone);
}

// ES2018 Helpers

// NewAwaitHelper — factory.go:871
Node* NodeFactory::newAwaitHelper(Node* expression) {
	emitContext->requestEmitHelper(awaitHelper);
	return newCallExpression(newUnscopedHelperName("__await"),
	                         nullptr /*questionDotToken*/,
	                         nullptr /*typeArguments*/,
	                         newNodeList({expression}), NodeFlagsNone);
}

// NewAsyncGeneratorHelper — factory.go:883
Node* NodeFactory::newAsyncGeneratorHelper(Node* generatorFunc,
                                           bool hasLexicalThis) {
	emitContext->requestEmitHelper(awaitHelper);
	emitContext->requestEmitHelper(asyncGeneratorHelper);

	// Mark this node as originally an async function body
	emitContext->addEmitFlags(generatorFunc,
	                          EFAsyncFunctionBody | EFReuseTempVariableScope);

	Node* thisArg;
	if (hasLexicalThis) {
		thisArg = newKeywordExpression(Kind::ThisKeyword);
	} else {
		thisArg = newVoidZeroExpression();
	}

	return newCallExpression(newUnscopedHelperName("__asyncGenerator"),
	                         nullptr /*questionDotToken*/,
	                         nullptr /*typeArguments*/,
	                         newNodeList({thisArg, newIdentifier("arguments"),
			                          generatorFunc}),
	                         NodeFlagsNone);
}

// NewAsyncDelegatorHelper — factory.go:914
Node* NodeFactory::newAsyncDelegatorHelper(Node* expression) {
	emitContext->requestEmitHelper(awaitHelper);
	emitContext->requestEmitHelper(asyncDelegatorHelper);
	return newCallExpression(newUnscopedHelperName("__asyncDelegator"),
	                         nullptr /*questionDotToken*/,
	                         nullptr /*typeArguments*/,
	                         newNodeList({expression}), NodeFlagsNone);
}

// NewAsyncValuesHelper — factory.go:927
Node* NodeFactory::newAsyncValuesHelper(Node* expression) {
	emitContext->requestEmitHelper(asyncValuesHelper);
	return newCallExpression(newUnscopedHelperName("__asyncValues"),
	                         nullptr /*questionDotToken*/,
	                         nullptr /*typeArguments*/,
	                         newNodeList({expression}), NodeFlagsNone);
}

// !!! ES2017 Helpers

// NewAwaiterHelper — factory.go:941
Node* NodeFactory::newAwaiterHelper(bool hasLexicalThis,
                                    Node* argumentsExpression,
                                    NodeList* parameters, Node* body) {
	emitContext->requestEmitHelper(awaiterHelper);

	NodeList* params;
	if (parameters != nullptr) {
		params = parameters;
	} else {
		params = newNodeList({});
	}

	Node* generatorFunc = newFunctionExpression(
		nullptr /*modifiers*/, newToken(Kind::AsteriskToken), nullptr /*name*/,
		nullptr /*typeParameters*/, params, nullptr /*returnType*/,
		nullptr /*fullSignature*/, body);

	// Mark this node as originally an async function body
	emitContext->addEmitFlags(generatorFunc,
	                          EFAsyncFunctionBody | EFReuseTempVariableScope);

	Node* thisArg;
	if (hasLexicalThis) {
		thisArg = newKeywordExpression(Kind::ThisKeyword);
	} else {
		thisArg = newVoidZeroExpression();
	}

	Node* argsArg;
	if (argumentsExpression != nullptr) {
		argsArg = argumentsExpression;
	} else {
		argsArg = newVoidZeroExpression();
	}

	return newCallExpression(newUnscopedHelperName("__awaiter"),
	                         nullptr /*questionDotToken*/,
	                         nullptr /*typeArguments*/,
	                         newNodeList({thisArg, argsArg,
			                          newVoidZeroExpression(),
			                          generatorFunc}),
	                         NodeFlagsNone);
}

// ES Decorator Helpers

// NewESDecorateClassContextObject — factory.go:1000
Node* NodeFactory::newESDecorateClassContextObject(Node* nameExpr,
                                                   Node* metadata) {
	std::vector<Node*> props = {
		newPropertyAssignment(nullptr, newIdentifier("kind"), nullptr,
		                      nullptr,
		                      newStringLiteral("class", TokenFlagsNone)),
		newPropertyAssignment(nullptr, newIdentifier("name"), nullptr,
		                      nullptr, nameExpr),
		newPropertyAssignment(nullptr, newIdentifier("metadata"), nullptr,
		                      nullptr, metadata),
	};
	return newObjectLiteralExpression(newNodeList(std::move(props)), false);
}

// NewESDecorateClassElementAccessGetMethod — factory.go:1009
Node* NodeFactory::newESDecorateClassElementAccessGetMethod(
	bool nameComputed, Node* nameExpr) {
	Node* accessor;
	if (nameComputed) {
		accessor = newElementAccessExpression(newIdentifier("obj"), nullptr,
		                                      nameExpr, NodeFlagsNone);
	} else {
		accessor = newPropertyAccessExpression(newIdentifier("obj"), nullptr,
		                                       nameExpr, NodeFlagsNone);
	}

	Node* objParam = newParameterDeclaration(nullptr, nullptr,
	                                         newIdentifier("obj"), nullptr,
	                                         nullptr, nullptr);

	Node* arrow = newArrowFunction(nullptr, nullptr,
	                               newNodeList({objParam}), nullptr, nullptr,
	                               newToken(Kind::EqualsGreaterThanToken),
	                               accessor);

	return newPropertyAssignment(nullptr, newIdentifier("get"), nullptr,
	                             nullptr, arrow);
}

// NewESDecorateClassElementAccessSetMethod — factory.go:1033
Node* NodeFactory::newESDecorateClassElementAccessSetMethod(
	bool nameComputed, Node* nameExpr) {
	Node* accessor;
	if (nameComputed) {
		accessor = newElementAccessExpression(newIdentifier("obj"), nullptr,
		                                      nameExpr, NodeFlagsNone);
	} else {
		accessor = newPropertyAccessExpression(newIdentifier("obj"), nullptr,
		                                       nameExpr, NodeFlagsNone);
	}

	Node* assignment =
		newAssignmentExpression(accessor, newIdentifier("value"));
	Node* stmt = newExpressionStatement(assignment);
	Node* body = newBlock(newNodeList({stmt}), false);

	Node* objParam = newParameterDeclaration(nullptr, nullptr,
	                                         newIdentifier("obj"), nullptr,
	                                         nullptr, nullptr);
	Node* valueParam = newParameterDeclaration(nullptr, nullptr,
	                                           newIdentifier("value"),
	                                           nullptr, nullptr, nullptr);

	Node* arrow = newArrowFunction(nullptr, nullptr,
	                               newNodeList({objParam, valueParam}),
	                               nullptr, nullptr,
	                               newToken(Kind::EqualsGreaterThanToken),
	                               body);

	return newPropertyAssignment(nullptr, newIdentifier("set"), nullptr,
	                             nullptr, arrow);
}

// NewESDecorateClassElementAccessHasMethod — factory.go:1062
Node* NodeFactory::newESDecorateClassElementAccessHasMethod(
	bool nameComputed, Node* nameExpr) {
	// The property name for the "in" expression
	Node* propertyName;
	if (!nameComputed && nameExpr != nullptr &&
	    nameExpr->kind == Kind::Identifier) {
		propertyName = newStringLiteralFromNode(nameExpr);
	} else {
		propertyName = nameExpr;
	}

	Node* objParam = newParameterDeclaration(nullptr, nullptr,
	                                         newIdentifier("obj"), nullptr,
	                                         nullptr, nullptr);
	Node* inExpr =
		newBinaryExpression(nullptr, propertyName, nullptr,
		                    newToken(Kind::InKeyword), newIdentifier("obj"));

	Node* arrow = newArrowFunction(nullptr, nullptr,
	                               newNodeList({objParam}), nullptr, nullptr,
	                               newToken(Kind::EqualsGreaterThanToken),
	                               inExpr);

	return newPropertyAssignment(nullptr, newIdentifier("has"), nullptr,
	                             nullptr, arrow);
}

// NewESDecorateClassElementAccessObject — factory.go:1098
Node* NodeFactory::newESDecorateClassElementAccessObject(
	bool nameComputed, Node* nameExpr, bool hasGet, bool hasSet) {
	std::vector<Node*> accessProps;

	// "has" method: obj => name in obj
	accessProps.push_back(
		newESDecorateClassElementAccessHasMethod(nameComputed, nameExpr));

	// "get" method: obj => obj.name or obj => obj[name]
	if (hasGet) {
		accessProps.push_back(
			newESDecorateClassElementAccessGetMethod(nameComputed,
			                                       nameExpr));
	}

	// "set" method: (obj, value) => { obj.name = value; } or
	// (obj, value) => { obj[name] = value; }
	if (hasSet) {
		accessProps.push_back(
			newESDecorateClassElementAccessSetMethod(nameComputed,
			                                       nameExpr));
	}

	return newObjectLiteralExpression(newNodeList(std::move(accessProps)),
	                                  false);
}

// NewESDecorateClassElementContextObject — factory.go:1122
Node* NodeFactory::newESDecorateClassElementContextObject(
	const std::string& kind, bool nameComputed, Node* nameExpr,
	bool isStatic, bool isPrivate, bool hasGet, bool hasSet, Node* metadata) {
	// Build the name value for the context's "name" property
	Node* nameValue;
	if (!nameComputed && nameExpr != nullptr &&
	    (nameExpr->kind == Kind::PrivateIdentifier ||
	     nameExpr->kind == Kind::Identifier)) {
		nameValue = newStringLiteralFromNode(nameExpr);
	} else {
		nameValue = nameExpr;
	}

	// Build the access object with has/get/set arrow functions
	Node* accessObj = newESDecorateClassElementAccessObject(
		nameComputed, nameExpr, hasGet, hasSet);

	Node* staticExpr = isStatic ? newTrueExpression() : newFalseExpression();
	Node* privateExpr =
		isPrivate ? newTrueExpression() : newFalseExpression();

	std::vector<Node*> props = {
		newPropertyAssignment(nullptr, newIdentifier("kind"), nullptr,
		                      nullptr,
		                      newStringLiteral(kind, TokenFlagsNone)),
		newPropertyAssignment(nullptr, newIdentifier("name"), nullptr,
		                      nullptr, nameValue),
		newPropertyAssignment(nullptr, newIdentifier("static"), nullptr,
		                      nullptr, staticExpr),
		newPropertyAssignment(nullptr, newIdentifier("private"), nullptr,
		                      nullptr, privateExpr),
		newPropertyAssignment(nullptr, newIdentifier("access"), nullptr,
		                      nullptr, accessObj),
		newPropertyAssignment(nullptr, newIdentifier("metadata"), nullptr,
		                      nullptr, metadata),
	};
	return newObjectLiteralExpression(newNodeList(std::move(props)), false);
}

// NewESDecorateHelper — factory.go:1168
Node* NodeFactory::newESDecorateHelper(Node* ctor, Node* descriptorIn,
                                       Node* decorators, Node* contextIn,
                                       Node* initializers,
                                       Node* extraInitializers) {
	emitContext->requestEmitHelper(esDecorateHelper);
	return newCallExpression(
		newUnscopedHelperName("__esDecorate"),
		nullptr /*questionDotToken*/, nullptr /*typeArguments*/,
		newNodeList({ctor, descriptorIn, decorators, contextIn,
		             initializers, extraInitializers}),
		NodeFlagsNone);
}

// NewRunInitializersHelper — factory.go:1179
Node* NodeFactory::newRunInitializersHelper(Node* thisArg,
                                            Node* initializers, Node* value) {
	emitContext->requestEmitHelper(runInitializersHelper);
	std::vector<Node*> arguments;
	if (value != nullptr) {
		arguments = {thisArg, initializers, value};
	} else {
		arguments = {thisArg, initializers};
	}
	return newCallExpression(newUnscopedHelperName("__runInitializers"),
	                         nullptr /*questionDotToken*/,
	                         nullptr /*typeArguments*/,
	                         newNodeList(std::move(arguments)),
	                         NodeFlagsNone);
}

// ES2015 Helpers

// NewTemplateObjectHelper — factory.go:1198
Node* NodeFactory::newTemplateObjectHelper(Node* cookedArray,
                                           Node* rawArray) {
	emitContext->requestEmitHelper(makeTemplateObjectHelper);
	return newCallExpression(newUnscopedHelperName("__makeTemplateObject"),
	                         nullptr /*questionDotToken*/,
	                         nullptr /*typeArguments*/,
	                         newNodeList({cookedArray, rawArray}),
	                         NodeFlagsNone);
}

// NewPropKeyHelper — factory.go:1209
Node* NodeFactory::newPropKeyHelper(Node* expr) {
	emitContext->requestEmitHelper(propKeyHelper);
	return newCallExpression(newUnscopedHelperName("__propKey"),
	                         nullptr /*questionDotToken*/,
	                         nullptr /*typeArguments*/,
	                         newNodeList({expr}), NodeFlagsNone);
}

// NewSetFunctionNameHelper — factory.go:1220
Node* NodeFactory::newSetFunctionNameHelper(Node* fn, Node* name,
                                            const std::string& prefix) {
	emitContext->requestEmitHelper(setFunctionNameHelper);
	std::vector<Node*> arguments;
	if (!prefix.empty()) {
		arguments = {fn, name, newStringLiteral(prefix, TokenFlagsNone)};
	} else {
		arguments = {fn, name};
	}
	return newCallExpression(newUnscopedHelperName("__setFunctionName"),
	                         nullptr /*questionDotToken*/,
	                         nullptr /*typeArguments*/,
	                         newNodeList(std::move(arguments)),
	                         NodeFlagsNone);
}

// ES Module Helpers

// NewImportDefaultHelper — factory.go:1240
Node* NodeFactory::newImportDefaultHelper(Node* expression) {
	emitContext->requestEmitHelper(importDefaultHelper);
	return newCallExpression(newUnscopedHelperName("__importDefault"),
	                         nullptr /*questionDotToken*/,
	                         nullptr /*typeArguments*/,
	                         newNodeList({expression}), NodeFlagsNone);
}

// NewImportStarHelper — factory.go:1252
Node* NodeFactory::newImportStarHelper(Node* expression) {
	emitContext->requestEmitHelper(importStarHelper);
	return newCallExpression(newUnscopedHelperName("__importStar"),
	                         nullptr /*questionDotToken*/,
	                         nullptr /*typeArguments*/,
	                         newNodeList({expression}), NodeFlagsNone);
}

// NewExportStarHelper — factory.go:1264
Node* NodeFactory::newExportStarHelper(Node* moduleExpression,
                                       Node* exportsExpression) {
	emitContext->requestEmitHelper(exportStarHelper);
	return newCallExpression(newUnscopedHelperName("__exportStar"),
	                         nullptr /*questionDotToken*/,
	                         nullptr /*typeArguments*/,
	                         newNodeList({moduleExpression, exportsExpression}),
	                         NodeFlagsNone);
}

// NewAssignmentTargetWrapper — factory.go:1275
Node* NodeFactory::newAssignmentTargetWrapper(Node* paramName,
                                              Node* expression) {
	Node* setAccessor = newSetAccessorDeclaration(
		nullptr /*modifiers*/, newIdentifier("value"),
		nullptr /*typeParameters*/,
		newNodeList({newParameterDeclaration(nullptr, nullptr, paramName,
		                                     nullptr, nullptr, nullptr)}),
		nullptr /*returnType*/, nullptr /*fullSignature*/,
		newBlock(newNodeList({newExpressionStatement(expression)}), false));
	Node* objLiteral =
		newObjectLiteralExpression(newNodeList({setAccessor}), false);
	// Explicit parens required because of v8 regression
	// (https://bugs.chromium.org/p/v8/issues/detail?id=9560)
	return newPropertyAccessExpression(newParenthesizedExpression(objLiteral),
	                                   nullptr /*questionDotToken*/,
	                                   newIdentifier("value"),
	                                   NodeFlagsNone);
}

// NewRewriteRelativeImportExtensionsHelper — factory.go:1300
Node* NodeFactory::newRewriteRelativeImportExtensionsHelper(
	Node* firstArgument, bool preserveJsx) {
	emitContext->requestEmitHelper(rewriteRelativeImportExtensionsHelper);
	std::vector<Node*> arguments;
	if (preserveJsx) {
		arguments = {firstArgument, newToken(Kind::TrueKeyword)};
	} else {
		arguments = {firstArgument};
	}
	return newCallExpression(
		newUnscopedHelperName("__rewriteRelativeImportExtension"),
		nullptr /*questionDotToken*/, nullptr /*typeArguments*/,
		newNodeList(std::move(arguments)), NodeFlagsNone);
}

}  // namespace tsc::printer
