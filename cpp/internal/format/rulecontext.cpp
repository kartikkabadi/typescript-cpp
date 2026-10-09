// Port of tsc/internal/format/rulecontext.go.
#include "internal/format/format.h"

#include "internal/astnav/tokens.h"
#include "internal/stringutil/stringutil.h"

namespace tsc::format {

// Go: ast.IsTrivia / ast.IsTokenKind.
static bool isTrivia(Kind kind) {
	return kind >= KindFirstTriviaToken && kind <= KindLastTriviaToken;
}
[[maybe_unused]] static bool isTokenKind(Kind kind) {
	return kind >= KindFirstToken && kind <= KindLastToken;
}

///
/// Contexts
///

lsutil::SemicolonPreference semicolonOption(const lsutil::FormatCodeSettings& options) {
	return options.Semicolons;
}

Tristate insertSpaceAfterCommaDelimiterOption(const lsutil::FormatCodeSettings& options) {
	return options.InsertSpaceAfterCommaDelimiter;
}

Tristate insertSpaceAfterSemicolonInForStatementsOption(const lsutil::FormatCodeSettings& options) {
	return options.InsertSpaceAfterSemicolonInForStatements;
}

Tristate insertSpaceBeforeAndAfterBinaryOperatorsOption(const lsutil::FormatCodeSettings& options) {
	return options.InsertSpaceBeforeAndAfterBinaryOperators;
}

Tristate insertSpaceAfterConstructorOption(const lsutil::FormatCodeSettings& options) {
	return options.InsertSpaceAfterConstructor;
}

Tristate insertSpaceAfterKeywordsInControlFlowStatementsOption(
	const lsutil::FormatCodeSettings& options) {
	return options.InsertSpaceAfterKeywordsInControlFlowStatements;
}

Tristate insertSpaceAfterFunctionKeywordForAnonymousFunctionsOption(
	const lsutil::FormatCodeSettings& options) {
	return options.InsertSpaceAfterFunctionKeywordForAnonymousFunctions;
}

Tristate insertSpaceAfterOpeningAndBeforeClosingNonemptyParenthesisOption(
	const lsutil::FormatCodeSettings& options) {
	return options.InsertSpaceAfterOpeningAndBeforeClosingNonemptyParenthesis;
}

Tristate insertSpaceAfterOpeningAndBeforeClosingNonemptyBracketsOption(
	const lsutil::FormatCodeSettings& options) {
	return options.InsertSpaceAfterOpeningAndBeforeClosingNonemptyBrackets;
}

Tristate insertSpaceAfterOpeningAndBeforeClosingNonemptyBracesOption(
	const lsutil::FormatCodeSettings& options) {
	return options.InsertSpaceAfterOpeningAndBeforeClosingNonemptyBraces;
}

Tristate insertSpaceAfterOpeningAndBeforeClosingEmptyBracesOption(
	const lsutil::FormatCodeSettings& options) {
	return options.InsertSpaceAfterOpeningAndBeforeClosingEmptyBraces;
}

Tristate insertSpaceAfterOpeningAndBeforeClosingTemplateStringBracesOption(
	const lsutil::FormatCodeSettings& options) {
	return options.InsertSpaceAfterOpeningAndBeforeClosingTemplateStringBraces;
}

Tristate insertSpaceAfterOpeningAndBeforeClosingJsxExpressionBracesOption(
	const lsutil::FormatCodeSettings& options) {
	return options.InsertSpaceAfterOpeningAndBeforeClosingJsxExpressionBraces;
}

Tristate insertSpaceAfterTypeAssertionOption(const lsutil::FormatCodeSettings& options) {
	return options.InsertSpaceAfterTypeAssertion;
}

Tristate insertSpaceBeforeFunctionParenthesisOption(const lsutil::FormatCodeSettings& options) {
	return options.InsertSpaceBeforeFunctionParenthesis;
}

Tristate placeOpenBraceOnNewLineForFunctionsOption(const lsutil::FormatCodeSettings& options) {
	return options.PlaceOpenBraceOnNewLineForFunctions;
}

Tristate placeOpenBraceOnNewLineForControlBlocksOption(const lsutil::FormatCodeSettings& options) {
	return options.PlaceOpenBraceOnNewLineForControlBlocks;
}

Tristate insertSpaceBeforeTypeAnnotationOption(const lsutil::FormatCodeSettings& options) {
	return options.InsertSpaceBeforeTypeAnnotation;
}

Tristate indentMultiLineObjectLiteralBeginningOnBlankLineOption(
	const lsutil::FormatCodeSettings& options) {
	return options.IndentMultiLineObjectLiteralBeginningOnBlankLine;
}

Tristate indentSwitchCaseOption(const lsutil::FormatCodeSettings& options) {
	return options.IndentSwitchCase;
}

contextPredicate isOptionEnabled(optionSelector optionName) {
	return [optionName](FormattingContext* context) {
		return tristateIsTrue(optionName(context->Options));
	};
}

contextPredicate isOptionDisabled(optionSelector optionName) {
	return [optionName](FormattingContext* context) {
		return tristateIsFalse(optionName(context->Options));
	};
}

contextPredicate isOptionDisabledOrUndefined(optionSelector optionName) {
	return [optionName](FormattingContext* context) {
		return tristateIsFalseOrUnknown(optionName(context->Options));
	};
}

contextPredicate isOptionDisabledOrUndefinedOrTokensOnSameLine(optionSelector optionName) {
	return [optionName](FormattingContext* context) {
		return tristateIsFalseOrUnknown(optionName(context->Options)) ||
			context->TokensAreOnSameLine();
	};
}

contextPredicate isOptionEnabledOrUndefined(optionSelector optionName) {
	return [optionName](FormattingContext* context) {
		return tristateIsTrueOrUnknown(optionName(context->Options));
	};
}

bool isForContext(FormattingContext* context) {
	return context->contextNode->kind == Kind::ForStatement;
}

bool isNotForContext(FormattingContext* context) {
	return !isForContext(context);
}

bool isBinaryOpContext(FormattingContext* context) {
	switch (context->contextNode->kind) {
	case Kind::BinaryExpression:
		return context->contextNode->as<BinaryExpression>()->OperatorToken->kind !=
			Kind::CommaToken;
	case Kind::ConditionalExpression:
	case Kind::ConditionalType:
	case Kind::AsExpression:
	case Kind::ExportSpecifier:
	case Kind::ImportSpecifier:
	case Kind::TypePredicate:
	case Kind::UnionType:
	case Kind::IntersectionType:
	case Kind::SatisfiesExpression:
		return true;

	// equals in binding elements func foo([[x, y] = [1, 2]])
	case Kind::BindingElement:
		// equals in type X = ...
		[[fallthrough]];
	case Kind::TypeAliasDeclaration:
		// equal in import a = module('a');
		[[fallthrough]];
	case Kind::ImportEqualsDeclaration:
		// equal in export = 1
		[[fallthrough]];
	case Kind::ExportAssignment:
		// equal in let a = 0
		[[fallthrough]];
	case Kind::VariableDeclaration:
		// equal in p = 0
		[[fallthrough]];
	case Kind::Parameter:
	case Kind::EnumMember:
	case Kind::PropertyDeclaration:
	case Kind::PropertySignature:
		return context->currentTokenSpan.kind == Kind::EqualsToken ||
			context->nextTokenSpan.kind == Kind::EqualsToken;
	// "in" keyword in for (let x in []) { }
	case Kind::ForInStatement:
		// "in" keyword in [P in keyof T] T[P]
		[[fallthrough]];
	case Kind::TypeParameter:
		return context->currentTokenSpan.kind == Kind::InKeyword ||
			context->nextTokenSpan.kind == Kind::InKeyword ||
			context->currentTokenSpan.kind == Kind::EqualsToken ||
			context->nextTokenSpan.kind == Kind::EqualsToken;
	// Technically, "of" is not a binary operator, but format it the same way as "in"
	case Kind::ForOfStatement:
		return context->currentTokenSpan.kind == Kind::OfKeyword ||
			context->nextTokenSpan.kind == Kind::OfKeyword;
	default:
		break;
	}
	return false;
}

bool isNotBinaryOpContext(FormattingContext* context) {
	return !isBinaryOpContext(context);
}

bool isNotTypeAnnotationContext(FormattingContext* context) {
	return !isTypeAnnotationContext(context);
}

bool isTypeAnnotationContext(FormattingContext* context) {
	Kind contextKind = context->contextNode->kind;
	return contextKind == Kind::PropertyDeclaration ||
		contextKind == Kind::PropertySignature ||
		contextKind == Kind::Parameter ||
		contextKind == Kind::VariableDeclaration ||
		isFunctionLikeKind(contextKind);
}

bool isOptionalPropertyContext(FormattingContext* context) {
	return isPropertyDeclaration(context->contextNode) && hasQuestionToken(context->contextNode);
}

bool isNonOptionalPropertyContext(FormattingContext* context) {
	return !isOptionalPropertyContext(context);
}

bool isConditionalOperatorContext(FormattingContext* context) {
	return context->contextNode->kind == Kind::ConditionalExpression ||
		context->contextNode->kind == Kind::ConditionalType;
}

bool isSameLineTokenOrBeforeBlockContext(FormattingContext* context) {
	return context->TokensAreOnSameLine() || isBeforeBlockContext(context);
}

bool isBraceWrappedContext(FormattingContext* context) {
	return context->contextNode->kind == Kind::ObjectBindingPattern ||
		context->contextNode->kind == Kind::MappedType ||
		isSingleLineBlockContext(context);
}

// This check is done before an open brace in a control construct, a function, or a typescript block declaration
bool isBeforeMultilineBlockContext(FormattingContext* context) {
	return isBeforeBlockContext(context) &&
		!(context->NextNodeAllOnSameLine() || context->NextNodeBlockIsOnOneLine());
}

bool isMultilineBlockContext(FormattingContext* context) {
	return isBlockContext(context) &&
		!(context->ContextNodeAllOnSameLine() || context->ContextNodeBlockIsOnOneLine());
}

bool isSingleLineBlockContext(FormattingContext* context) {
	return isBlockContext(context) &&
		(context->ContextNodeAllOnSameLine() || context->ContextNodeBlockIsOnOneLine());
}

bool isBlockContext(FormattingContext* context) {
	return nodeIsBlockContext(context->contextNode);
}

bool isBeforeBlockContext(FormattingContext* context) {
	return nodeIsBlockContext(context->nextTokenParent);
}

// IMPORTANT!!! This method must return true ONLY for nodes with open and close braces as immediate children
bool nodeIsBlockContext(Node* node) {
	if (nodeIsTypeScriptDeclWithBlockContext(node)) {
		// This means we are in a context that looks like a block to the user, but in the grammar is actually not a node (it's a class, module, enum, object type literal, etc).
		return true;
	}

	switch (node->kind) {
	case Kind::Block:
	case Kind::CaseBlock:
	case Kind::ObjectLiteralExpression:
	case Kind::ModuleBlock:
		return true;
	default:
		break;
	}

	return false;
}

bool isFunctionDeclContext(FormattingContext* context) {
	switch (context->contextNode->kind) {
	case Kind::FunctionDeclaration:
	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
		// case Kind::MemberFunctionDeclaration:
		[[fallthrough]];
	case Kind::GetAccessor:
	case Kind::SetAccessor:
		// case Kind::MethodSignature:
		[[fallthrough]];
	case Kind::CallSignature:
	case Kind::FunctionExpression:
	case Kind::Constructor:
	case Kind::ArrowFunction:
		// case Kind::ConstructorDeclaration:
		// case Kind::SimpleArrowFunctionExpression:
		// case Kind::ParenthesizedArrowFunctionExpression:
		[[fallthrough]];
	case Kind::InterfaceDeclaration: // This one is not truly a function, but for formatting purposes, it acts just like one
		return true;
	default:
		break;
	}

	return false;
}

bool isNotFunctionDeclContext(FormattingContext* context) {
	return !isFunctionDeclContext(context);
}

bool isFunctionDeclarationOrFunctionExpressionContext(FormattingContext* context) {
	return context->contextNode->kind == Kind::FunctionDeclaration ||
		context->contextNode->kind == Kind::FunctionExpression;
}

bool isTypeScriptDeclWithBlockContext(FormattingContext* context) {
	return nodeIsTypeScriptDeclWithBlockContext(context->contextNode);
}

bool nodeIsTypeScriptDeclWithBlockContext(Node* node) {
	switch (node->kind) {
	case Kind::ClassDeclaration:
	case Kind::ClassExpression:
	case Kind::InterfaceDeclaration:
	case Kind::EnumDeclaration:
	case Kind::TypeLiteral:
	case Kind::ModuleDeclaration:
	case Kind::ExportDeclaration:
	case Kind::NamedExports:
	case Kind::ImportDeclaration:
	case Kind::NamedImports:
		return true;
	default:
		break;
	}

	return false;
}

bool isAfterCodeBlockContext(FormattingContext* context) {
	switch (context->currentTokenParent->kind) {
	case Kind::ClassDeclaration:
	case Kind::ModuleDeclaration:
	case Kind::EnumDeclaration:
	case Kind::CatchClause:
	case Kind::ModuleBlock:
	case Kind::SwitchStatement:
		return true;
	case Kind::Block: {
		Node* blockParent = context->currentTokenParent->parent;
		// In a codefix scenario, we can't rely on parents being set. So just always return true.
		if (blockParent == nullptr ||
			(blockParent->kind != Kind::ArrowFunction &&
			 blockParent->kind != Kind::FunctionExpression)) {
			return true;
		}
		break;
	}
	default:
		break;
	}
	return false;
}

bool isControlDeclContext(FormattingContext* context) {
	switch (context->contextNode->kind) {
	case Kind::IfStatement:
	case Kind::SwitchStatement:
	case Kind::ForStatement:
	case Kind::ForInStatement:
	case Kind::ForOfStatement:
	case Kind::WhileStatement:
	case Kind::TryStatement:
	case Kind::DoStatement:
	case Kind::WithStatement:
		// TODO
		// case Kind::ElseClause:
		[[fallthrough]];
	case Kind::CatchClause:
		return true;

	default:
		return false;
	}
}

bool isObjectContext(FormattingContext* context) {
	return context->contextNode->kind == Kind::ObjectLiteralExpression;
}

bool isFunctionCallContext(FormattingContext* context) {
	return context->contextNode->kind == Kind::CallExpression;
}

bool isNewContext(FormattingContext* context) {
	return context->contextNode->kind == Kind::NewExpression;
}

bool isFunctionCallOrNewContext(FormattingContext* context) {
	return isFunctionCallContext(context) || isNewContext(context);
}

bool isPreviousTokenNotComma(FormattingContext* context) {
	return context->currentTokenSpan.kind != Kind::CommaToken;
}

bool isNextTokenNotCloseBracket(FormattingContext* context) {
	return context->nextTokenSpan.kind != Kind::CloseBracketToken;
}

bool isNextTokenNotCloseParen(FormattingContext* context) {
	return context->nextTokenSpan.kind != Kind::CloseParenToken;
}

bool isArrowFunctionContext(FormattingContext* context) {
	return context->contextNode->kind == Kind::ArrowFunction;
}

bool isImportTypeContext(FormattingContext* context) {
	return context->contextNode->kind == Kind::ImportType;
}

bool isNonJsxSameLineTokenContext(FormattingContext* context) {
	return context->TokensAreOnSameLine() && context->contextNode->kind != Kind::JsxText;
}

bool isNonJsxTextContext(FormattingContext* context) {
	return context->contextNode->kind != Kind::JsxText;
}

bool isNonJsxElementOrFragmentContext(FormattingContext* context) {
	return context->contextNode->kind != Kind::JsxElement &&
		context->contextNode->kind != Kind::JsxFragment;
}

bool isJsxExpressionContext(FormattingContext* context) {
	return context->contextNode->kind == Kind::JsxExpression ||
		context->contextNode->kind == Kind::JsxSpreadAttribute;
}

bool isNextTokenParentJsxAttribute(FormattingContext* context) {
	return context->nextTokenParent->kind == Kind::JsxAttribute ||
		(context->nextTokenParent->kind == Kind::JsxNamespacedName &&
		 context->nextTokenParent->parent->kind == Kind::JsxAttribute);
}

bool isJsxAttributeContext(FormattingContext* context) {
	return context->contextNode->kind == Kind::JsxAttribute;
}

bool isNextTokenParentNotJsxNamespacedName(FormattingContext* context) {
	return context->nextTokenParent->kind != Kind::JsxNamespacedName;
}

bool isNextTokenParentJsxNamespacedName(FormattingContext* context) {
	return context->nextTokenParent->kind == Kind::JsxNamespacedName;
}

bool isJsxSelfClosingElementContext(FormattingContext* context) {
	return context->contextNode->kind == Kind::JsxSelfClosingElement;
}

bool isNotBeforeBlockInFunctionDeclarationContext(FormattingContext* context) {
	return !isFunctionDeclContext(context) && !isBeforeBlockContext(context);
}

bool isEndOfDecoratorContextOnSameLine(FormattingContext* context) {
	return context->TokensAreOnSameLine() &&
		hasDecorators(context->contextNode) &&
		nodeIsInDecoratorContext(context->currentTokenParent) &&
		!nodeIsInDecoratorContext(context->nextTokenParent);
}

bool nodeIsInDecoratorContext(Node* node) {
	while (node != nullptr && isExpression(node)) {
		node = node->parent;
	}
	return node != nullptr && node->kind == Kind::Decorator;
}

bool isStartOfVariableDeclarationList(FormattingContext* context) {
	return context->currentTokenParent->kind == Kind::VariableDeclarationList &&
		getTokenPosOfNode(context->currentTokenParent, context->SourceFile, false) ==
		context->currentTokenSpan.Loc.pos();
}

bool isNotFormatOnEnter(FormattingContext* context) {
	return context->FormattingRequestKind != FormatRequestKind::FormatOnEnter;
}

bool isModuleDeclContext(FormattingContext* context) {
	return context->contextNode->kind == Kind::ModuleDeclaration;
}

bool isObjectTypeContext(FormattingContext* context) {
	return context->contextNode->kind == Kind::TypeLiteral; // && context->contextNode->parent->kind != Kind::InterfaceDeclaration;
}

bool isConstructorSignatureContext(FormattingContext* context) {
	return context->contextNode->kind == Kind::ConstructSignature;
}

bool isTypeArgumentOrParameterOrAssertion(TextRangeWithKind token, Node* parent) {
	if (token.kind != Kind::LessThanToken && token.kind != Kind::GreaterThanToken) {
		return false;
	}
	switch (parent->kind) {
	case Kind::TypeReference:
	case Kind::TypeAssertionExpression:
	case Kind::TypeAliasDeclaration:
	case Kind::ClassDeclaration:
	case Kind::ClassExpression:
	case Kind::InterfaceDeclaration:
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
	case Kind::ArrowFunction:
	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
	case Kind::CallSignature:
	case Kind::ConstructSignature:
	case Kind::CallExpression:
	case Kind::NewExpression:
	case Kind::ExpressionWithTypeArguments:
		return true;
	default:
		return false;
	}
}

bool isTypeArgumentOrParameterOrAssertionContext(FormattingContext* context) {
	return isTypeArgumentOrParameterOrAssertion(context->currentTokenSpan,
												context->currentTokenParent) ||
		isTypeArgumentOrParameterOrAssertion(context->nextTokenSpan, context->nextTokenParent);
}

bool isTypeAssertionContext(FormattingContext* context) {
	return context->contextNode->kind == Kind::TypeAssertionExpression;
}

bool isNonTypeAssertionContext(FormattingContext* context) {
	return !isTypeAssertionContext(context);
}

bool isVoidOpContext(FormattingContext* context) {
	return context->currentTokenSpan.kind == Kind::VoidKeyword &&
		context->currentTokenParent->kind == Kind::VoidExpression;
}

bool isYieldOrYieldStarWithOperand(FormattingContext* context) {
	return context->contextNode->kind == Kind::YieldExpression &&
		context->contextNode->expression() != nullptr;
}

bool isNonNullAssertionContext(FormattingContext* context) {
	return context->contextNode->kind == Kind::NonNullExpression;
}

bool isNotStatementConditionContext(FormattingContext* context) {
	return !isStatementConditionContext(context);
}

bool isStatementConditionContext(FormattingContext* context) {
	switch (context->contextNode->kind) {
	case Kind::IfStatement:
	case Kind::ForStatement:
	case Kind::ForInStatement:
	case Kind::ForOfStatement:
	case Kind::DoStatement:
	case Kind::WhileStatement:
		return true;

	default:
		return false;
	}
}

bool isSemicolonDeletionContext(FormattingContext* context) {
	Kind nextTokenKind = context->nextTokenSpan.kind;
	int nextTokenStart = context->nextTokenSpan.Loc.pos();
	if (isTrivia(nextTokenKind)) {
		Node* nextRealToken = nullptr;
		if (context->nextTokenParent == context->currentTokenParent) {
			// !!! TODO: very different from strada, but strada's logic here is wonky - find the first ancestor without a parent? that's just the source file.
			nextRealToken = astnav::findNextToken(context->nextTokenParent,
												  context->SourceFile->asNode(),
												  context->SourceFile);
		} else {
			nextRealToken = lsutil::GetFirstToken(context->nextTokenParent, context->SourceFile);
		}

		if (nextRealToken == nullptr) {
			return true;
		}
		nextTokenKind = nextRealToken->kind;
		nextTokenStart = getTokenPosOfNode(nextRealToken, context->SourceFile, false);
	}

	int startLine = getECMALineOfPosition(context->SourceFile, context->currentTokenSpan.Loc.pos());
	int endLine = getECMALineOfPosition(context->SourceFile, nextTokenStart);
	if (startLine == endLine) {
		return nextTokenKind == Kind::CloseBraceToken || nextTokenKind == Kind::EndOfFile;
	}

	if (nextTokenKind == Kind::SemicolonToken &&
		context->currentTokenSpan.kind == Kind::SemicolonToken) {
		return true;
	}

	if (nextTokenKind == Kind::SemicolonClassElement ||
		nextTokenKind == Kind::SemicolonToken) {
		return false;
	}

	if (context->contextNode->kind == Kind::InterfaceDeclaration ||
		context->contextNode->kind == Kind::TypeAliasDeclaration) {
		// Can't remove semicolon after `foo`; it would parse as a method declaration:
		//
		// interface I {
		//   foo;
		//   () void
		// }
		return context->currentTokenParent->kind != Kind::PropertySignature ||
			context->currentTokenParent->type() != nullptr ||
			nextTokenKind != Kind::OpenParenToken;
	}

	if (isPropertyDeclaration(context->currentTokenParent)) {
		return context->currentTokenParent->initializer() == nullptr;
	}

	return context->currentTokenParent->kind != Kind::ForStatement &&
		context->currentTokenParent->kind != Kind::EmptyStatement &&
		context->currentTokenParent->kind != Kind::SemicolonClassElement &&
		nextTokenKind != Kind::OpenBracketToken &&
		nextTokenKind != Kind::OpenParenToken &&
		nextTokenKind != Kind::PlusToken &&
		nextTokenKind != Kind::MinusToken &&
		nextTokenKind != Kind::SlashToken &&
		nextTokenKind != Kind::RegularExpressionLiteral &&
		nextTokenKind != Kind::CommaToken &&
		nextTokenKind != Kind::TemplateExpression &&
		nextTokenKind != Kind::TemplateHead &&
		nextTokenKind != Kind::NoSubstitutionTemplateLiteral &&
		nextTokenKind != Kind::DotToken;
}

bool isSemicolonInsertionContext(FormattingContext* context) {
	return lsutil::PositionIsASICandidate(context->currentTokenSpan.Loc.end(),
										  context->currentTokenParent, context->SourceFile);
}

bool isNotPropertyAccessOnIntegerLiteral(FormattingContext* context) {
	return !isPropertyAccessExpression(context->contextNode) ||
		!isNumericLiteral(context->contextNode->expression()) ||
		context->contextNode->expression()->text().find('.') != std::string::npos;
}

} // namespace tsc::format
