// Port of tsc/internal/transformers/utilities.go — shared helpers used by the
// individual transforms.
#include <algorithm>

#include "internal/transformers/transformers.h"
#include "internal/scanner/scanner.h"

namespace tsc::transformers {

// IsGeneratedIdentifier — utilities.go:12
bool isGeneratedIdentifier(printer::EmitContext* emitContext, Node* name) {
	return emitContext->hasAutoGenerateInfo(name);
}

// IsHelperName — utilities.go:16
bool isHelperName(printer::EmitContext* emitContext, Node* name) {
	return (emitContext->emitFlags(name) & printer::EFHelperName) != 0;
}

// IsLocalName — utilities.go:20
bool isLocalName(printer::EmitContext* emitContext, Node* name) {
	return (emitContext->emitFlags(name) & printer::EFLocalName) != 0;
}

// IsExportName — utilities.go:24
bool isExportName(printer::EmitContext* emitContext, Node* name) {
	return (emitContext->emitFlags(name) & printer::EFExportName) != 0;
}

// IsIdentifierReference — utilities.go:28
bool isIdentifierReference(Node* name, Node* parent) {
	switch (parent->kind) {
	case Kind::BinaryExpression:
	case Kind::PrefixUnaryExpression:
	case Kind::PostfixUnaryExpression:
	case Kind::YieldExpression:
	case Kind::AsExpression:
	case Kind::SatisfiesExpression:
	case Kind::ElementAccessExpression:
	case Kind::NonNullExpression:
	case Kind::SpreadElement:
	case Kind::SpreadAssignment:
	case Kind::ParenthesizedExpression:
	case Kind::ArrayLiteralExpression:
	case Kind::DeleteExpression:
	case Kind::TypeOfExpression:
	case Kind::VoidExpression:
	case Kind::AwaitExpression:
	case Kind::TypeAssertionExpression:
	case Kind::ExpressionWithTypeArguments:
	case Kind::JsxSelfClosingElement:
	case Kind::JsxSpreadAttribute:
	case Kind::JsxExpression:
	case Kind::PartiallyEmittedExpression:
		// all immediate children that can be `Identifier` would be instances
		// of `IdentifierReference`
		return true;
	case Kind::ComputedPropertyName:
	case Kind::Decorator:
	case Kind::IfStatement:
	case Kind::DoStatement:
	case Kind::WhileStatement:
	case Kind::WithStatement:
	case Kind::ReturnStatement:
	case Kind::SwitchStatement:
	case Kind::CaseClause:
	case Kind::ThrowStatement:
	case Kind::ExpressionStatement:
	case Kind::ExportAssignment:
	case Kind::PropertyAccessExpression:
	case Kind::TemplateSpan:
		// only an `Expression()` child that can be `Identifier` would be an
		// instance of `IdentifierReference`
		return parent->expression() == name;
	case Kind::VariableDeclaration:
	case Kind::Parameter:
	case Kind::BindingElement:
	case Kind::PropertyDeclaration:
	case Kind::PropertySignature:
	case Kind::PropertyAssignment:
	case Kind::EnumMember:
	case Kind::JsxAttribute:
		// only an `Initializer()` child that can be `Identifier` would be an
		// instance of `IdentifierReference`
		return parent->initializer() == name;
	case Kind::ShorthandPropertyAssignment:
		return parent->as<ShorthandPropertyAssignment>()
		           ->ObjectAssignmentInitializer == name;
	case Kind::ForStatement:
		return parent->initializer() == name ||
		       parent->as<ForStatement>()->Condition == name ||
		       parent->as<ForStatement>()->Incrementor == name;
	case Kind::ForInStatement:
	case Kind::ForOfStatement:
		return parent->initializer() == name ||
		       parent->expression() == name;
	case Kind::ImportEqualsDeclaration:
		return parent->as<ImportEqualsDeclaration>()->ModuleReference == name;
	case Kind::ArrowFunction:
		return parent->body() == name;
	case Kind::ConditionalExpression:
		return parent->as<ConditionalExpression>()->Condition == name ||
		       parent->as<ConditionalExpression>()->WhenTrue == name ||
		       parent->as<ConditionalExpression>()->WhenFalse == name;
	case Kind::CallExpression:
	case Kind::NewExpression:
		if (parent->expression() == name) {
			return true;
		}
		return std::find(parent->arguments().begin(),
		                 parent->arguments().end(),
		                 name) != parent->arguments().end();
	case Kind::TaggedTemplateExpression:
		return parent->as<TaggedTemplateExpression>()->Tag == name;
	case Kind::ImportAttribute:
		return parent->as<ImportAttribute>()->Value == name;
	case Kind::JsxOpeningElement:
	case Kind::JsxClosingElement:
		return parent->tagName() == name;
	default:
		return false;
	}
}

static Node* convertBindingNameToAssignmentElementTarget(
	printer::EmitContext* emitContext, Node* element);

// convertBindingElementToArrayAssignmentElement — utilities.go:112
static Node* convertBindingElementToArrayAssignmentElement(
	printer::EmitContext* emitContext, Node* element) {
	if (element->name() == nullptr) {
		Node* elision = emitContext->factory.newOmittedExpression();
		emitContext->setOriginal(elision, element->asNode());
		emitContext->assignCommentAndSourceMapRanges(elision,
		                                           element->asNode());
		return elision;
	}
	if (element->as<BindingElement>()->DotDotDotToken != nullptr) {
		Node* spread =
			emitContext->factory.newSpreadElement(element->name());
		emitContext->setOriginal(spread, element->asNode());
		emitContext->assignCommentAndSourceMapRanges(spread,
		                                           element->asNode());
		return spread;
	}
	Node* expression = convertBindingNameToAssignmentElementTarget(
		emitContext, element->name());
	if (element->initializer() != nullptr) {
		Node* assignment = emitContext->factory.newAssignmentExpression(
			expression, element->initializer());
		emitContext->setOriginal(assignment, element->asNode());
		emitContext->assignCommentAndSourceMapRanges(assignment,
		                                           element->asNode());
		return assignment;
	}
	return expression;
}

// convertBindingElementToObjectAssignmentElement — utilities.go:135
static Node* convertBindingElementToObjectAssignmentElement(
	printer::EmitContext* emitContext, Node* element) {
	if (element->as<BindingElement>()->DotDotDotToken != nullptr) {
		Node* spread =
			emitContext->factory.newSpreadAssignment(element->name());
		emitContext->setOriginal(spread, element->asNode());
		emitContext->assignCommentAndSourceMapRanges(spread,
		                                           element->asNode());
		return spread;
	}
	if (element->as<BindingElement>()->PropertyName != nullptr) {
		Node* expression = convertBindingNameToAssignmentElementTarget(
			emitContext, element->name());
		if (element->initializer() != nullptr) {
			expression = emitContext->factory.newAssignmentExpression(
				expression, element->initializer());
		}
		Node* assignment = emitContext->factory.newPropertyAssignment(
			nullptr /*modifiers*/,
			element->as<BindingElement>()->PropertyName,
			nullptr /*postfixToken*/, nullptr /*typeNode*/, expression);
		emitContext->setOriginal(assignment, element->asNode());
		emitContext->assignCommentAndSourceMapRanges(assignment,
		                                           element->asNode());
		return assignment;
	}
	Node* equalsToken = nullptr;
	if (element->initializer() != nullptr) {
		equalsToken = emitContext->factory.newToken(Kind::EqualsToken);
	}
	Node* assignment = emitContext->factory.newShorthandPropertyAssignment(
		nullptr /*modifiers*/, element->name(), nullptr /*postfixToken*/,
		nullptr /*typeNode*/, equalsToken, element->initializer());
	emitContext->setOriginal(assignment, element->asNode());
	emitContext->assignCommentAndSourceMapRanges(assignment,
	                                           element->asNode());
	return assignment;
}

static Node* convertBindingElementToObjectAssignmentPattern(
	printer::EmitContext* emitContext, Node* element);
static Node* convertBindingElementToArrayAssignmentPattern(
	printer::EmitContext* emitContext, Node* element);

// ConvertBindingPatternToAssignmentPattern — utilities.go:169
Node* convertBindingPatternToAssignmentPattern(
	printer::EmitContext* emitContext, Node* element) {
	switch (element->kind) {
	case Kind::ArrayBindingPattern:
		return convertBindingElementToArrayAssignmentPattern(emitContext,
		                                                     element);
	case Kind::ObjectBindingPattern:
		return convertBindingElementToObjectAssignmentPattern(emitContext,
		                                                      element);
	default:
		TSC_UNREACHABLE("Unknown binding pattern");
	}
}

// convertBindingElementToObjectAssignmentPattern — utilities.go:180
static Node* convertBindingElementToObjectAssignmentPattern(
	printer::EmitContext* emitContext, Node* element) {
	std::vector<Node*> properties;
	for (Node* el : element->as<BindingPattern>()->Elements->nodes) {
		properties.push_back(convertBindingElementToObjectAssignmentElement(
			emitContext, el->as<BindingElement>()));
	}
	NodeList* propertyList = emitContext->factory.newNodeList(properties);
	propertyList->loc = element->as<BindingPattern>()->Elements->loc;
	Node* object = emitContext->factory.newObjectLiteralExpression(
		propertyList, false /*multiLine*/);
	emitContext->setOriginal(object, element->asNode());
	emitContext->assignCommentAndSourceMapRanges(object, element->asNode());
	return object;
}

// convertBindingElementToArrayAssignmentPattern — utilities.go:193
static Node* convertBindingElementToArrayAssignmentPattern(
	printer::EmitContext* emitContext, Node* element) {
	std::vector<Node*> elements;
	for (Node* el : element->as<BindingPattern>()->Elements->nodes) {
		elements.push_back(convertBindingElementToArrayAssignmentElement(
			emitContext, el->as<BindingElement>()));
	}
	NodeList* elementList = emitContext->factory.newNodeList(elements);
	elementList->loc = element->as<BindingPattern>()->Elements->loc;
	Node* object = emitContext->factory.newArrayLiteralExpression(
		elementList, false /*multiLine*/);
	emitContext->setOriginal(object, element->asNode());
	emitContext->assignCommentAndSourceMapRanges(object, element->asNode());
	return object;
}

// convertBindingNameToAssignmentElementTarget — utilities.go:206
static Node* convertBindingNameToAssignmentElementTarget(
	printer::EmitContext* emitContext, Node* element) {
	if (isBindingPattern(element)) {
		return convertBindingPatternToAssignmentPattern(
			emitContext, element->as<BindingPattern>());
	}
	return element;
}

// ConvertVariableDeclarationToAssignmentExpression — utilities.go:213
Node* convertVariableDeclarationToAssignmentExpression(
	printer::EmitContext* emitContext, Node* element) {
	if (element->initializer() == nullptr) {
		return nullptr;
	}
	Node* expression = convertBindingNameToAssignmentElementTarget(
		emitContext, element->name());
	Node* assignment = emitContext->factory.newAssignmentExpression(
		expression, element->initializer());
	emitContext->setOriginal(assignment, element->asNode());
	emitContext->assignCommentAndSourceMapRanges(assignment,
	                                           element->asNode());
	return assignment;
}

// SingleOrMany — utilities.go:224
Node* singleOrMany(std::vector<Node*> nodes, printer::NodeFactory* factory) {
	if (nodes.empty()) {
		return nullptr;
	}
	if (nodes.size() == 1) {
		return nodes[0];
	}
	return factory->newSyntaxList(nodes);
}

// IsSimpleCopiableExpression — utilities.go:241
bool isSimpleCopiableExpression(Node* expression) {
	return isStringLiteralLike(expression) ||
	       isNumericLiteral(expression) ||
	       isKeywordKind(expression->kind) || isIdentifier(expression);
}

// IsOriginalNodeSingleLine — utilities.go:248
bool isOriginalNodeSingleLine(printer::EmitContext* emitContext, Node* node) {
	if (node == nullptr) {
		return false;
	}
	Node* original = emitContext->mostOriginal(node);
	if (original == nullptr) {
		return false;
	}
	SourceFile* source = getSourceFileOfNode(original);
	if (source == nullptr) {
		return false;
	}
	int startLine = getECMALineOfPosition(source, original->loc.pos());
	int endLine = getECMALineOfPosition(source, original->loc.end());
	return startLine == endLine;
}

// IsSimpleInlineableExpression — utilities.go:270
bool isSimpleInlineableExpression(Node* expression) {
	return !isIdentifier(expression) && isSimpleCopiableExpression(expression);
}

static std::vector<int> findSuperStatementIndexPathWorker(
	const std::vector<Node*>& statements, int start,
	std::vector<int> indices);

// FindSuperStatementIndexPath — utilities.go:275
std::vector<int> findSuperStatementIndexPath(std::vector<Node*> statements,
                                             int start) {
	std::vector<int> indices =
		findSuperStatementIndexPathWorker(statements, start, {});
	std::reverse(indices.begin(), indices.end());
	return indices;
}

// findSuperStatementIndexPathWorker — utilities.go:281
static std::vector<int> findSuperStatementIndexPathWorker(
	const std::vector<Node*>& statements, int start,
	std::vector<int> indices) {
	for (int i = start; i < static_cast<int>(statements.size()); i++) {
		Node* statement = statements[i];
		if (getSuperCallFromStatement(statement) != nullptr) {
			indices.push_back(i);
			return indices;
		}
		if (isTryStatement(statement)) {
			std::vector<int> result = findSuperStatementIndexPathWorker(
				statement->as<TryStatement>()
				    ->TryBlock->as<Block>()
				    ->statements(),
				0, indices);
			if (!result.empty()) {
				result.push_back(i);
				return result;
			}
		}
	}
	return {};
}

// GetSuperCallFromStatement — utilities.go:296
Node* getSuperCallFromStatement(Node* statement) {
	if (!isExpressionStatement(statement)) {
		return nullptr;
	}
	Node* expression = skipParentheses(statement->expression());
	if (isSuperCall(expression)) {
		return expression;
	}
	return nullptr;
}

// MoveRangePastModifiers — utilities.go:308
TextRange moveRangePastModifiers(Node* node) {
	if (isPropertyDeclaration(node) || isMethodDeclaration(node)) {
		return TextRange{node->name()->pos(), node->end()};
	}

	Node* lastModifier = nullptr;
	if (canHaveModifiers(node)) {
		std::vector<Node*> modifiers = node->modifierNodes();
		if (!modifiers.empty()) {
			lastModifier = modifiers.back();
		}
	}

	if (lastModifier != nullptr &&
	    !positionIsSynthesized(lastModifier->end())) {
		return TextRange{lastModifier->end(), node->end()};
	}
	return moveRangePastDecorators(node);
}

// MoveRangePastDecorators — utilities.go:325
TextRange moveRangePastDecorators(Node* node) {
	Node* lastDecorator = nullptr;
	if (canHaveModifiers(node)) {
		std::vector<Node*> nodes = node->modifierNodes();
		for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
			if (isDecorator(*it)) {
				lastDecorator = *it;
				break;
			}
		}
	}

	if (lastDecorator != nullptr &&
	    !positionIsSynthesized(lastDecorator->end())) {
		return TextRange{lastDecorator->end(), node->end()};
	}
	return node->loc;
}

// GetNonAssignmentOperatorForCompoundAssignment — utilities.go:341
Kind getNonAssignmentOperatorForCompoundAssignment(Kind kind) {
	switch (kind) {
	case Kind::PlusEqualsToken:
		return Kind::PlusToken;
	case Kind::MinusEqualsToken:
		return Kind::MinusToken;
	case Kind::AsteriskEqualsToken:
		return Kind::AsteriskToken;
	case Kind::AsteriskAsteriskEqualsToken:
		return Kind::AsteriskAsteriskToken;
	case Kind::SlashEqualsToken:
		return Kind::SlashToken;
	case Kind::PercentEqualsToken:
		return Kind::PercentToken;
	case Kind::LessThanLessThanEqualsToken:
		return Kind::LessThanLessThanToken;
	case Kind::GreaterThanGreaterThanEqualsToken:
		return Kind::GreaterThanGreaterThanToken;
	case Kind::GreaterThanGreaterThanGreaterThanEqualsToken:
		return Kind::GreaterThanGreaterThanGreaterThanToken;
	case Kind::AmpersandEqualsToken:
		return Kind::AmpersandToken;
	case Kind::BarEqualsToken:
		return Kind::BarToken;
	case Kind::CaretEqualsToken:
		return Kind::CaretToken;
	case Kind::BarBarEqualsToken:
		return Kind::BarBarToken;
	case Kind::AmpersandAmpersandEqualsToken:
		return Kind::AmpersandAmpersandToken;
	case Kind::QuestionQuestionEqualsToken:
		return Kind::QuestionQuestionToken;
	default:
		return kind;
	}
}

}  // namespace tsc::transformers
