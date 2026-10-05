// Port of tsc/internal/format/rules.go.
#include "internal/format/format.h"

namespace tsc::format {

std::vector<ruleSpec> getAllRules() {
	std::vector<Kind> allTokens;
	allTokens.reserve(static_cast<int>(KindLastToken) - static_cast<int>(KindFirstToken) + 1);
	for (int token = static_cast<int>(KindFirstToken);
		 token <= static_cast<int>(KindLastToken); token++) {
		if (static_cast<Kind>(token) != Kind::EndOfFile) {
			allTokens.push_back(static_cast<Kind>(token));
		}
	}

	auto anyTokenExcept = [&allTokens](std::initializer_list<Kind> tokens) -> tokenRange {
		std::vector<Kind> newTokens;
		newTokens.reserve(allTokens.size());
		for (Kind token : allTokens) {
			bool skip = false;
			for (Kind ex : tokens) {
				if (ex == token) {
					skip = true;
					break;
				}
			}
			if (skip) {
				continue;
			}
			newTokens.push_back(token);
		}
		return tokenRange{std::move(newTokens), false};
	};

	tokenRange anyToken{
		allTokens,
		false,
	};

	tokenRange anyTokenIncludingMultilineComments =
		tokenRangeFromEx(allTokens, {Kind::MultiLineCommentTrivia});
	tokenRange anyTokenIncludingEOF = tokenRangeFromEx(allTokens, {Kind::EndOfFile});
	tokenRange keywords = tokenRangeFromRange(KindFirstKeyword, KindLastKeyword);
	tokenRange binaryOperators = tokenRangeFromRange(KindFirstBinaryOperator, KindLastBinaryOperator);
	std::vector<Kind> binaryKeywordOperators = {
		Kind::InKeyword,
		Kind::InstanceOfKeyword,
		Kind::OfKeyword,
		Kind::AsKeyword,
		Kind::IsKeyword,
		Kind::SatisfiesKeyword,
	};
	std::vector<Kind> unaryPrefixOperators = {
		Kind::PlusPlusToken, Kind::MinusToken, Kind::TildeToken, Kind::ExclamationToken};
	std::vector<Kind> unaryPrefixExpressions = {
		Kind::NumericLiteral,
		Kind::BigIntLiteral,
		Kind::Identifier,
		Kind::OpenParenToken,
		Kind::OpenBracketToken,
		Kind::OpenBraceToken,
		Kind::ThisKeyword,
		Kind::NewKeyword,
	};
	std::vector<Kind> unaryPreincrementExpressions = {
		Kind::Identifier, Kind::OpenParenToken, Kind::ThisKeyword, Kind::NewKeyword};
	std::vector<Kind> unaryPostincrementExpressions = {
		Kind::Identifier, Kind::CloseParenToken, Kind::CloseBracketToken, Kind::NewKeyword};
	std::vector<Kind> unaryPredecrementExpressions = {
		Kind::Identifier, Kind::OpenParenToken, Kind::ThisKeyword, Kind::NewKeyword};
	std::vector<Kind> unaryPostdecrementExpressions = {
		Kind::Identifier, Kind::CloseParenToken, Kind::CloseBracketToken, Kind::NewKeyword};
	std::vector<Kind> comments = {Kind::SingleLineCommentTrivia, Kind::MultiLineCommentTrivia};
	std::vector<Kind> typeKeywords = {
		Kind::AnyKeyword,
		Kind::AssertsKeyword,
		Kind::BigIntKeyword,
		Kind::BooleanKeyword,
		Kind::FalseKeyword,
		Kind::InferKeyword,
		Kind::KeyOfKeyword,
		Kind::NeverKeyword,
		Kind::NullKeyword,
		Kind::NumberKeyword,
		Kind::ObjectKeyword,
		Kind::ReadonlyKeyword,
		Kind::StringKeyword,
		Kind::SymbolKeyword,
		Kind::TypeOfKeyword,
		Kind::TrueKeyword,
		Kind::VoidKeyword,
		Kind::UndefinedKeyword,
		Kind::UniqueKeyword,
		Kind::UnknownKeyword,
	};
	std::vector<Kind> typeNames = {Kind::Identifier};
	typeNames.insert(typeNames.end(), typeKeywords.begin(), typeKeywords.end());

	// Place a space before open brace in a function declaration
	// TypeScript: Function can have return types, which can be made of tons of different token kinds
	tokenRange functionOpenBraceLeftTokenRange = anyTokenIncludingMultilineComments;

	// Place a space before open brace in a TypeScript declaration that has braces as children (class, module, enum, etc)
	tokenRange typeScriptOpenBraceLeftTokenRange =
		tokenRangeFrom({Kind::Identifier, Kind::GreaterThanToken, Kind::MultiLineCommentTrivia,
						Kind::ClassKeyword, Kind::ExportKeyword, Kind::ImportKeyword});

	// Place a space before open brace in a control flow construct
	tokenRange controlOpenBraceLeftTokenRange =
		tokenRangeFrom({Kind::CloseParenToken, Kind::MultiLineCommentTrivia, Kind::DoKeyword,
						Kind::TryKeyword, Kind::FinallyKeyword, Kind::ElseKeyword,
						Kind::CatchKeyword});

	// These rules are higher in priority than user-configurable
	std::vector<ruleSpec> highPriorityCommonRules = {
		// Leave comments alone
		rule("IgnoreBeforeComment", anyToken, comments, anyContext,
			 ruleActionStopProcessingSpaceActions),
		rule("IgnoreAfterLineComment", Kind::SingleLineCommentTrivia, anyToken, anyContext,
			 ruleActionStopProcessingSpaceActions),

		rule("NotSpaceBeforeColon", anyToken, Kind::ColonToken,
			 {isNonJsxSameLineTokenContext, isNotBinaryOpContext, isNotTypeAnnotationContext},
			 ruleActionDeleteSpace),
		rule("SpaceAfterColon", Kind::ColonToken, anyToken,
			 {isNonJsxSameLineTokenContext, isNotBinaryOpContext,
			  isNextTokenParentNotJsxNamespacedName},
			 ruleActionInsertSpace),
		rule("NoSpaceBeforeQuestionMark", anyToken, Kind::QuestionToken,
			 {isNonJsxSameLineTokenContext, isNotBinaryOpContext, isNotTypeAnnotationContext},
			 ruleActionDeleteSpace),
		// insert space after '?' only when it is used in conditional operator
		rule("SpaceAfterQuestionMarkInConditionalOperator", Kind::QuestionToken, anyToken,
			 {isNonJsxSameLineTokenContext, isConditionalOperatorContext}, ruleActionInsertSpace),

		// in other cases there should be no space between '?' and next token
		rule("NoSpaceAfterQuestionMark", Kind::QuestionToken, anyToken,
			 {isNonJsxSameLineTokenContext, isNonOptionalPropertyContext}, ruleActionDeleteSpace),

		rule("NoSpaceBeforeDot", anyToken, {Kind::DotToken, Kind::QuestionDotToken},
			 {isNonJsxSameLineTokenContext, isNotPropertyAccessOnIntegerLiteral},
			 ruleActionDeleteSpace),
		rule("NoSpaceAfterDot", {Kind::DotToken, Kind::QuestionDotToken}, anyToken,
			 {isNonJsxSameLineTokenContext}, ruleActionDeleteSpace),

		rule("NoSpaceBetweenImportParenInImportType", Kind::ImportKeyword, Kind::OpenParenToken,
			 {isNonJsxSameLineTokenContext, isImportTypeContext}, ruleActionDeleteSpace),

		// Special handling of unary operators.
		// Prefix operators generally shouldn't have a space between
		// them and their target unary expression.
		rule("NoSpaceAfterUnaryPrefixOperator", unaryPrefixOperators, unaryPrefixExpressions,
			 {isNonJsxSameLineTokenContext, isNotBinaryOpContext}, ruleActionDeleteSpace),
		rule("NoSpaceAfterUnaryPreincrementOperator", Kind::PlusPlusToken,
			 unaryPreincrementExpressions, {isNonJsxSameLineTokenContext}, ruleActionDeleteSpace),
		rule("NoSpaceAfterUnaryPredecrementOperator", Kind::MinusMinusToken,
			 unaryPredecrementExpressions, {isNonJsxSameLineTokenContext}, ruleActionDeleteSpace),
		rule("NoSpaceBeforeUnaryPostincrementOperator", unaryPostincrementExpressions,
			 Kind::PlusPlusToken, {isNonJsxSameLineTokenContext, isNotStatementConditionContext},
			 ruleActionDeleteSpace),
		rule("NoSpaceBeforeUnaryPostdecrementOperator", unaryPostdecrementExpressions,
			 Kind::MinusMinusToken, {isNonJsxSameLineTokenContext, isNotStatementConditionContext},
			 ruleActionDeleteSpace),

		// More unary operator special-casing.
		// DevDiv 181814: Be careful when removing leading whitespace
		// around unary operators.  Examples:
		//      1 - -2  --X--> 1--2
		//      a + ++b --X--> a+++b
		rule("SpaceAfterPostincrementWhenFollowedByAdd", Kind::PlusPlusToken, Kind::PlusToken,
			 {isNonJsxSameLineTokenContext, isBinaryOpContext}, ruleActionInsertSpace),
		rule("SpaceAfterAddWhenFollowedByUnaryPlus", Kind::PlusToken, Kind::PlusToken,
			 {isNonJsxSameLineTokenContext, isBinaryOpContext}, ruleActionInsertSpace),
		rule("SpaceAfterAddWhenFollowedByPreincrement", Kind::PlusToken, Kind::PlusPlusToken,
			 {isNonJsxSameLineTokenContext, isBinaryOpContext}, ruleActionInsertSpace),
		rule("SpaceAfterPostdecrementWhenFollowedBySubtract", Kind::MinusMinusToken,
			 Kind::MinusToken, {isNonJsxSameLineTokenContext, isBinaryOpContext},
			 ruleActionInsertSpace),
		rule("SpaceAfterSubtractWhenFollowedByUnaryMinus", Kind::MinusToken, Kind::MinusToken,
			 {isNonJsxSameLineTokenContext, isBinaryOpContext}, ruleActionInsertSpace),
		rule("SpaceAfterSubtractWhenFollowedByPredecrement", Kind::MinusToken, Kind::MinusMinusToken,
			 {isNonJsxSameLineTokenContext, isBinaryOpContext}, ruleActionInsertSpace),

		rule("NoSpaceAfterCloseBrace", Kind::CloseBraceToken,
			 {Kind::CommaToken, Kind::SemicolonToken}, {isNonJsxSameLineTokenContext},
			 ruleActionDeleteSpace),
		// For functions and control block place } on a new line
		rule("NewLineBeforeCloseBraceInBlockContext", anyTokenIncludingMultilineComments,
			 Kind::CloseBraceToken, {isMultilineBlockContext}, ruleActionInsertNewLine),

		// Space/new line after }.
		rule("SpaceAfterCloseBrace", Kind::CloseBraceToken, anyTokenExcept({Kind::CloseParenToken}),
			 {isNonJsxSameLineTokenContext, isAfterCodeBlockContext}, ruleActionInsertSpace),
		// Special case for (}, else) and (}, while) since else & while tokens are not part of the tree which makes SpaceAfterCloseBrace rule not applied
		// Also should not apply to })
		rule("SpaceBetweenCloseBraceAndElse", Kind::CloseBraceToken, Kind::ElseKeyword,
			 {isNonJsxSameLineTokenContext}, ruleActionInsertSpace),
		rule("SpaceBetweenCloseBraceAndWhile", Kind::CloseBraceToken, Kind::WhileKeyword,
			 {isNonJsxSameLineTokenContext}, ruleActionInsertSpace),
		rule("NoSpaceBetweenEmptyBraceBrackets", Kind::OpenBraceToken, Kind::CloseBraceToken,
			 {isNonJsxSameLineTokenContext, isObjectContext}, ruleActionDeleteSpace),

		// Add a space after control dec context if the next character is an open bracket ex: 'if (false){a, b} = {1, 2};' -> 'if (false) {a, b} = {1, 2};'
		rule("SpaceAfterConditionalClosingParen", Kind::CloseParenToken, Kind::OpenBracketToken,
			 {isControlDeclContext}, ruleActionInsertSpace),

		rule("NoSpaceBetweenFunctionKeywordAndStar", Kind::FunctionKeyword, Kind::AsteriskToken,
			 {isFunctionDeclarationOrFunctionExpressionContext}, ruleActionDeleteSpace),
		rule("SpaceAfterStarInGeneratorDeclaration", Kind::AsteriskToken, Kind::Identifier,
			 {isFunctionDeclarationOrFunctionExpressionContext}, ruleActionInsertSpace),

		rule("SpaceAfterFunctionInFuncDecl", Kind::FunctionKeyword, anyToken,
			 {isFunctionDeclContext}, ruleActionInsertSpace),
		// Insert new line after { and before } in multi-line contexts.
		rule("NewLineAfterOpenBraceInBlockContext", Kind::OpenBraceToken, anyToken,
			 {isMultilineBlockContext}, ruleActionInsertNewLine),

		// For get/set members, we check for (identifier,identifier) since get/set don't have tokens and they are represented as just an identifier token.
		// Though, we do extra check on the context to make sure we are dealing with get/set node. Example:
		//      get x() {}
		//      set x(val) {}
		rule("SpaceAfterGetSetInMember", {Kind::GetKeyword, Kind::SetKeyword}, Kind::Identifier,
			 {isFunctionDeclContext}, ruleActionInsertSpace),

		rule("NoSpaceBetweenYieldKeywordAndStar", Kind::YieldKeyword, Kind::AsteriskToken,
			 {isNonJsxSameLineTokenContext, isYieldOrYieldStarWithOperand}, ruleActionDeleteSpace),
		rule("SpaceBetweenYieldOrYieldStarAndOperand", {Kind::YieldKeyword, Kind::AsteriskToken},
			 anyToken, {isNonJsxSameLineTokenContext, isYieldOrYieldStarWithOperand},
			 ruleActionInsertSpace),

		rule("NoSpaceBetweenReturnAndSemicolon", Kind::ReturnKeyword, Kind::SemicolonToken,
			 {isNonJsxSameLineTokenContext}, ruleActionDeleteSpace),
		rule("SpaceAfterCertainKeywords",
			 {Kind::VarKeyword, Kind::ThrowKeyword, Kind::NewKeyword, Kind::DeleteKeyword,
			  Kind::ReturnKeyword, Kind::TypeOfKeyword, Kind::AwaitKeyword},
			 anyToken, {isNonJsxSameLineTokenContext}, ruleActionInsertSpace),
		rule("SpaceAfterLetConstInVariableDeclaration", {Kind::LetKeyword, Kind::ConstKeyword},
			 anyToken, {isNonJsxSameLineTokenContext, isStartOfVariableDeclarationList},
			 ruleActionInsertSpace),
		rule("NoSpaceBeforeOpenParenInFuncCall", anyToken, Kind::OpenParenToken,
			 {isNonJsxSameLineTokenContext, isFunctionCallOrNewContext, isPreviousTokenNotComma},
			 ruleActionDeleteSpace),

		// Special case for binary operators (that are keywords). For these we have to add a space and shouldn't follow any user options.
		rule("SpaceBeforeBinaryKeywordOperator", anyToken, binaryKeywordOperators,
			 {isNonJsxSameLineTokenContext, isBinaryOpContext}, ruleActionInsertSpace),
		rule("SpaceAfterBinaryKeywordOperator", binaryKeywordOperators, anyToken,
			 {isNonJsxSameLineTokenContext, isBinaryOpContext}, ruleActionInsertSpace),

		rule("SpaceAfterVoidOperator", Kind::VoidKeyword, anyToken,
			 {isNonJsxSameLineTokenContext, isVoidOpContext}, ruleActionInsertSpace),

		// Async-await
		rule("SpaceBetweenAsyncAndOpenParen", Kind::AsyncKeyword, Kind::OpenParenToken,
			 {isArrowFunctionContext, isNonJsxSameLineTokenContext}, ruleActionInsertSpace),
		rule("SpaceBetweenAsyncAndFunctionKeyword", Kind::AsyncKeyword,
			 {Kind::FunctionKeyword, Kind::Identifier}, {isNonJsxSameLineTokenContext},
			 ruleActionInsertSpace),

		// Template string
		rule("NoSpaceBetweenTagAndTemplateString", {Kind::Identifier, Kind::CloseParenToken},
			 {Kind::NoSubstitutionTemplateLiteral, Kind::TemplateHead},
			 {isNonJsxSameLineTokenContext}, ruleActionDeleteSpace),

		// JSX opening elements
		rule("SpaceBeforeJsxAttribute", anyToken, Kind::Identifier,
			 {isNextTokenParentJsxAttribute, isNonJsxSameLineTokenContext}, ruleActionInsertSpace),
		rule("SpaceBeforeSlashInJsxOpeningElement", anyToken, Kind::SlashToken,
			 {isJsxSelfClosingElementContext, isNonJsxSameLineTokenContext}, ruleActionInsertSpace),
		rule("NoSpaceBeforeGreaterThanTokenInJsxOpeningElement", Kind::SlashToken,
			 Kind::GreaterThanToken, {isJsxSelfClosingElementContext, isNonJsxSameLineTokenContext},
			 ruleActionDeleteSpace),
		rule("NoSpaceBeforeEqualInJsxAttribute", anyToken, Kind::EqualsToken,
			 {isJsxAttributeContext, isNonJsxSameLineTokenContext}, ruleActionDeleteSpace),
		rule("NoSpaceAfterEqualInJsxAttribute", Kind::EqualsToken, anyToken,
			 {isJsxAttributeContext, isNonJsxSameLineTokenContext}, ruleActionDeleteSpace),
		rule("NoSpaceBeforeJsxNamespaceColon", Kind::Identifier, Kind::ColonToken,
			 {isNextTokenParentJsxNamespacedName}, ruleActionDeleteSpace),
		rule("NoSpaceAfterJsxNamespaceColon", Kind::ColonToken, Kind::Identifier,
			 {isNextTokenParentJsxNamespacedName}, ruleActionDeleteSpace),

		// TypeScript-specific rules
		// Use of module as a function call. e.g.: import m2 = module("m2");
		rule("NoSpaceAfterModuleImport", {Kind::ModuleKeyword, Kind::RequireKeyword},
			 Kind::OpenParenToken, {isNonJsxSameLineTokenContext}, ruleActionDeleteSpace),
		// Add a space around certain TypeScript keywords
		rule(
			"SpaceAfterCertainTypeScriptKeywords",
			{Kind::AbstractKeyword,
			 Kind::AccessorKeyword,
			 Kind::ClassKeyword,
			 Kind::DeclareKeyword,
			 Kind::DefaultKeyword,
			 Kind::EnumKeyword,
			 Kind::ExportKeyword,
			 Kind::ExtendsKeyword,
			 Kind::GetKeyword,
			 Kind::ImplementsKeyword,
			 Kind::ImportKeyword,
			 Kind::InterfaceKeyword,
			 Kind::ModuleKeyword,
			 Kind::NamespaceKeyword,
			 Kind::OverrideKeyword,
			 Kind::PrivateKeyword,
			 Kind::PublicKeyword,
			 Kind::ProtectedKeyword,
			 Kind::ReadonlyKeyword,
			 Kind::SetKeyword,
			 Kind::StaticKeyword,
			 Kind::TypeKeyword,
			 Kind::FromKeyword,
			 Kind::KeyOfKeyword,
			 Kind::InferKeyword},
			anyToken,
			{isNonJsxSameLineTokenContext},
			ruleActionInsertSpace
		),
		rule(
			"SpaceBeforeCertainTypeScriptKeywords",
			anyToken,
			{Kind::ExtendsKeyword, Kind::ImplementsKeyword, Kind::FromKeyword},
			{isNonJsxSameLineTokenContext},
			ruleActionInsertSpace
		),
		// Treat string literals in module names as identifiers, and add a space between the literal and the opening Brace braces, e.g.: module "m2" {
		rule("SpaceAfterModuleName", Kind::StringLiteral, Kind::OpenBraceToken,
			 {isModuleDeclContext}, ruleActionInsertSpace),

		// Lambda expressions
		rule("SpaceBeforeArrow", anyToken, Kind::EqualsGreaterThanToken,
			 {isNonJsxSameLineTokenContext}, ruleActionInsertSpace),
		rule("SpaceAfterArrow", Kind::EqualsGreaterThanToken, anyToken,
			 {isNonJsxSameLineTokenContext}, ruleActionInsertSpace),

		// Optional parameters and let args
		rule("NoSpaceAfterEllipsis", Kind::DotDotDotToken, Kind::Identifier,
			 {isNonJsxSameLineTokenContext}, ruleActionDeleteSpace),
		rule("NoSpaceAfterOptionalParameters", Kind::QuestionToken,
			 {Kind::CloseParenToken, Kind::CommaToken},
			 {isNonJsxSameLineTokenContext, isNotBinaryOpContext}, ruleActionDeleteSpace),

		// Remove spaces in empty interface literals. e.g.: x: {}
		rule("NoSpaceBetweenEmptyInterfaceBraceBrackets", Kind::OpenBraceToken,
			 Kind::CloseBraceToken, {isNonJsxSameLineTokenContext, isObjectTypeContext},
			 ruleActionDeleteSpace),

		// generics and type assertions
		rule("NoSpaceBeforeOpenAngularBracket", typeNames, Kind::LessThanToken,
			 {isNonJsxSameLineTokenContext, isTypeArgumentOrParameterOrAssertionContext},
			 ruleActionDeleteSpace),
		rule("NoSpaceBetweenCloseParenAndAngularBracket", Kind::CloseParenToken,
			 Kind::LessThanToken,
			 {isNonJsxSameLineTokenContext, isTypeArgumentOrParameterOrAssertionContext},
			 ruleActionDeleteSpace),
		rule("NoSpaceAfterOpenAngularBracket", Kind::LessThanToken, anyToken,
			 {isNonJsxSameLineTokenContext, isTypeArgumentOrParameterOrAssertionContext},
			 ruleActionDeleteSpace),
		rule("NoSpaceBeforeCloseAngularBracket", anyToken, Kind::GreaterThanToken,
			 {isNonJsxSameLineTokenContext, isTypeArgumentOrParameterOrAssertionContext},
			 ruleActionDeleteSpace),
		rule("NoSpaceAfterCloseAngularBracket", Kind::GreaterThanToken,
			 {Kind::OpenParenToken, Kind::OpenBracketToken, Kind::GreaterThanToken,
			  Kind::CommaToken},
			 {isNonJsxSameLineTokenContext,
			  isTypeArgumentOrParameterOrAssertionContext,
			  isNotFunctionDeclContext, /*To prevent an interference with the SpaceBeforeOpenParenInFuncDecl rule*/
			  isNonTypeAssertionContext},
			 ruleActionDeleteSpace),

		// decorators
		rule("SpaceBeforeAt", {Kind::CloseParenToken, Kind::Identifier}, Kind::AtToken,
			 {isNonJsxSameLineTokenContext}, ruleActionInsertSpace),
		rule("NoSpaceAfterAt", Kind::AtToken, anyToken, {isNonJsxSameLineTokenContext},
			 ruleActionDeleteSpace),
		// Insert space after @ in decorator
		rule(
			"SpaceAfterDecorator",
			anyToken,
			{Kind::AbstractKeyword,
			 Kind::Identifier,
			 Kind::ExportKeyword,
			 Kind::DefaultKeyword,
			 Kind::ClassKeyword,
			 Kind::StaticKeyword,
			 Kind::PublicKeyword,
			 Kind::PrivateKeyword,
			 Kind::ProtectedKeyword,
			 Kind::GetKeyword,
			 Kind::SetKeyword,
			 Kind::OpenBracketToken,
			 Kind::AsteriskToken},
			{isEndOfDecoratorContextOnSameLine},
			ruleActionInsertSpace
		),

		rule("NoSpaceBeforeNonNullAssertionOperator", anyToken, Kind::ExclamationToken,
			 {isNonJsxSameLineTokenContext, isNonNullAssertionContext}, ruleActionDeleteSpace),
		rule("NoSpaceAfterNewKeywordOnConstructorSignature", Kind::NewKeyword, Kind::OpenParenToken,
			 {isNonJsxSameLineTokenContext, isConstructorSignatureContext}, ruleActionDeleteSpace),
		rule("SpaceLessThanAndNonJSXTypeAnnotation", Kind::LessThanToken, Kind::LessThanToken,
			 {isNonJsxSameLineTokenContext}, ruleActionInsertSpace),
	};

	// These rules are applied after high priority
	std::vector<ruleSpec> userConfigurableRules = {
		// Treat constructor as an identifier in a function declaration, and remove spaces between constructor and following left parentheses
		rule("SpaceAfterConstructor", Kind::ConstructorKeyword, Kind::OpenParenToken,
			 {isOptionEnabled(insertSpaceAfterConstructorOption), isNonJsxSameLineTokenContext},
			 ruleActionInsertSpace),
		rule("NoSpaceAfterConstructor", Kind::ConstructorKeyword, Kind::OpenParenToken,
			 {isOptionDisabledOrUndefined(insertSpaceAfterConstructorOption),
			  isNonJsxSameLineTokenContext},
			 ruleActionDeleteSpace),

		rule("SpaceAfterComma", Kind::CommaToken, anyToken,
			 {isOptionEnabled(insertSpaceAfterCommaDelimiterOption), isNonJsxSameLineTokenContext,
			  isNonJsxElementOrFragmentContext, isNextTokenNotCloseBracket,
			  isNextTokenNotCloseParen},
			 ruleActionInsertSpace),
		rule("NoSpaceAfterComma", Kind::CommaToken, anyToken,
			 {isOptionDisabledOrUndefined(insertSpaceAfterCommaDelimiterOption),
			  isNonJsxSameLineTokenContext, isNonJsxElementOrFragmentContext},
			 ruleActionDeleteSpace),

		// Insert space after function keyword for anonymous functions
		rule("SpaceAfterAnonymousFunctionKeyword", {Kind::FunctionKeyword, Kind::AsteriskToken},
			 Kind::OpenParenToken,
			 {isOptionEnabled(insertSpaceAfterFunctionKeywordForAnonymousFunctionsOption),
			  isFunctionDeclContext},
			 ruleActionInsertSpace),
		rule("NoSpaceAfterAnonymousFunctionKeyword", {Kind::FunctionKeyword, Kind::AsteriskToken},
			 Kind::OpenParenToken,
			 {isOptionDisabledOrUndefined(
				  insertSpaceAfterFunctionKeywordForAnonymousFunctionsOption),
			  isFunctionDeclContext},
			 ruleActionDeleteSpace),

		// Insert space after keywords in control flow statements
		rule("SpaceAfterKeywordInControl", keywords, Kind::OpenParenToken,
			 {isOptionEnabled(insertSpaceAfterKeywordsInControlFlowStatementsOption),
			  isControlDeclContext},
			 ruleActionInsertSpace),
		rule("NoSpaceAfterKeywordInControl", keywords, Kind::OpenParenToken,
			 {isOptionDisabledOrUndefined(insertSpaceAfterKeywordsInControlFlowStatementsOption),
			  isControlDeclContext},
			 ruleActionDeleteSpace),

		// Insert space after opening and before closing nonempty parenthesis
		rule("SpaceAfterOpenParen", Kind::OpenParenToken, anyToken,
			 {isOptionEnabled(insertSpaceAfterOpeningAndBeforeClosingNonemptyParenthesisOption),
			  isNonJsxSameLineTokenContext},
			 ruleActionInsertSpace),
		rule("SpaceBeforeCloseParen", anyToken, Kind::CloseParenToken,
			 {isOptionEnabled(insertSpaceAfterOpeningAndBeforeClosingNonemptyParenthesisOption),
			  isNonJsxSameLineTokenContext},
			 ruleActionInsertSpace),
		rule("SpaceBetweenOpenParens", Kind::OpenParenToken, Kind::OpenParenToken,
			 {isOptionEnabled(insertSpaceAfterOpeningAndBeforeClosingNonemptyParenthesisOption),
			  isNonJsxSameLineTokenContext},
			 ruleActionInsertSpace),
		rule("NoSpaceBetweenParens", Kind::OpenParenToken, Kind::CloseParenToken,
			 {isNonJsxSameLineTokenContext}, ruleActionDeleteSpace),
		rule("NoSpaceAfterOpenParen", Kind::OpenParenToken, anyToken,
			 {isOptionDisabledOrUndefined(
				  insertSpaceAfterOpeningAndBeforeClosingNonemptyParenthesisOption),
			  isNonJsxSameLineTokenContext},
			 ruleActionDeleteSpace),
		rule("NoSpaceBeforeCloseParen", anyToken, Kind::CloseParenToken,
			 {isOptionDisabledOrUndefined(
				  insertSpaceAfterOpeningAndBeforeClosingNonemptyParenthesisOption),
			  isNonJsxSameLineTokenContext},
			 ruleActionDeleteSpace),

		// Insert space after opening and before closing nonempty brackets
		rule("SpaceAfterOpenBracket", Kind::OpenBracketToken, anyToken,
			 {isOptionEnabled(insertSpaceAfterOpeningAndBeforeClosingNonemptyBracketsOption),
			  isNonJsxSameLineTokenContext},
			 ruleActionInsertSpace),
		rule("SpaceBeforeCloseBracket", anyToken, Kind::CloseBracketToken,
			 {isOptionEnabled(insertSpaceAfterOpeningAndBeforeClosingNonemptyBracketsOption),
			  isNonJsxSameLineTokenContext},
			 ruleActionInsertSpace),
		rule("NoSpaceBetweenBrackets", Kind::OpenBracketToken, Kind::CloseBracketToken,
			 {isNonJsxSameLineTokenContext}, ruleActionDeleteSpace),
		rule("NoSpaceAfterOpenBracket", Kind::OpenBracketToken, anyToken,
			 {isOptionDisabledOrUndefined(
				  insertSpaceAfterOpeningAndBeforeClosingNonemptyBracketsOption),
			  isNonJsxSameLineTokenContext},
			 ruleActionDeleteSpace),
		rule("NoSpaceBeforeCloseBracket", anyToken, Kind::CloseBracketToken,
			 {isOptionDisabledOrUndefined(
				  insertSpaceAfterOpeningAndBeforeClosingNonemptyBracketsOption),
			  isNonJsxSameLineTokenContext},
			 ruleActionDeleteSpace),

		// Insert a space after { and before } in single-line contexts, but remove space from empty object literals {}.
		rule("SpaceAfterOpenBrace", Kind::OpenBraceToken, anyToken,
			 {isOptionEnabledOrUndefined(insertSpaceAfterOpeningAndBeforeClosingNonemptyBracesOption),
			  isBraceWrappedContext},
			 ruleActionInsertSpace),
		rule("SpaceBeforeCloseBrace", anyToken, Kind::CloseBraceToken,
			 {isOptionEnabledOrUndefined(insertSpaceAfterOpeningAndBeforeClosingNonemptyBracesOption),
			  isBraceWrappedContext},
			 ruleActionInsertSpace),
		rule("NoSpaceBetweenEmptyBraceBrackets", Kind::OpenBraceToken, Kind::CloseBraceToken,
			 {isNonJsxSameLineTokenContext, isObjectContext}, ruleActionDeleteSpace),
		rule("NoSpaceAfterOpenBrace", Kind::OpenBraceToken, anyToken,
			 {isOptionDisabled(insertSpaceAfterOpeningAndBeforeClosingNonemptyBracesOption),
			  isNonJsxSameLineTokenContext},
			 ruleActionDeleteSpace),
		rule("NoSpaceBeforeCloseBrace", anyToken, Kind::CloseBraceToken,
			 {isOptionDisabled(insertSpaceAfterOpeningAndBeforeClosingNonemptyBracesOption),
			  isNonJsxSameLineTokenContext},
			 ruleActionDeleteSpace),

		// Insert a space after opening and before closing empty brace brackets
		rule("SpaceBetweenEmptyBraceBrackets", Kind::OpenBraceToken, Kind::CloseBraceToken,
			 {isOptionEnabled(insertSpaceAfterOpeningAndBeforeClosingEmptyBracesOption)},
			 ruleActionInsertSpace),
		rule("NoSpaceBetweenEmptyBraceBrackets", Kind::OpenBraceToken, Kind::CloseBraceToken,
			 {isOptionDisabled(insertSpaceAfterOpeningAndBeforeClosingEmptyBracesOption),
			  isNonJsxSameLineTokenContext},
			 ruleActionDeleteSpace),

		// Insert space after opening and before closing template string braces
		rule("SpaceAfterTemplateHeadAndMiddle", {Kind::TemplateHead, Kind::TemplateMiddle}, anyToken,
			 {isOptionEnabled(insertSpaceAfterOpeningAndBeforeClosingTemplateStringBracesOption),
			  isNonJsxTextContext},
			 ruleActionInsertSpace, ruleFlagsCanDeleteNewLines),
		rule("SpaceBeforeTemplateMiddleAndTail", anyToken,
			 {Kind::TemplateMiddle, Kind::TemplateTail},
			 {isOptionEnabled(insertSpaceAfterOpeningAndBeforeClosingTemplateStringBracesOption),
			  isNonJsxSameLineTokenContext},
			 ruleActionInsertSpace),
		rule("NoSpaceAfterTemplateHeadAndMiddle", {Kind::TemplateHead, Kind::TemplateMiddle},
			 anyToken,
			 {isOptionDisabledOrUndefined(
				  insertSpaceAfterOpeningAndBeforeClosingTemplateStringBracesOption),
			  isNonJsxTextContext},
			 ruleActionDeleteSpace, ruleFlagsCanDeleteNewLines),
		rule("NoSpaceBeforeTemplateMiddleAndTail", anyToken,
			 {Kind::TemplateMiddle, Kind::TemplateTail},
			 {isOptionDisabledOrUndefined(
				  insertSpaceAfterOpeningAndBeforeClosingTemplateStringBracesOption),
			  isNonJsxSameLineTokenContext},
			 ruleActionDeleteSpace),

		// No space after { and before } in JSX expression
		rule("SpaceAfterOpenBraceInJsxExpression", Kind::OpenBraceToken, anyToken,
			 {isOptionEnabled(insertSpaceAfterOpeningAndBeforeClosingJsxExpressionBracesOption),
			  isNonJsxSameLineTokenContext, isJsxExpressionContext},
			 ruleActionInsertSpace),
		rule("SpaceBeforeCloseBraceInJsxExpression", anyToken, Kind::CloseBraceToken,
			 {isOptionEnabled(insertSpaceAfterOpeningAndBeforeClosingJsxExpressionBracesOption),
			  isNonJsxSameLineTokenContext, isJsxExpressionContext},
			 ruleActionInsertSpace),
		rule("NoSpaceAfterOpenBraceInJsxExpression", Kind::OpenBraceToken, anyToken,
			 {isOptionDisabledOrUndefined(
				  insertSpaceAfterOpeningAndBeforeClosingJsxExpressionBracesOption),
			  isNonJsxSameLineTokenContext, isJsxExpressionContext},
			 ruleActionDeleteSpace),
		rule("NoSpaceBeforeCloseBraceInJsxExpression", anyToken, Kind::CloseBraceToken,
			 {isOptionDisabledOrUndefined(
				  insertSpaceAfterOpeningAndBeforeClosingJsxExpressionBracesOption),
			  isNonJsxSameLineTokenContext, isJsxExpressionContext},
			 ruleActionDeleteSpace),

		// Insert space after semicolon in for statement
		rule("SpaceAfterSemicolonInFor", Kind::SemicolonToken, anyToken,
			 {isOptionEnabled(insertSpaceAfterSemicolonInForStatementsOption),
			  isNonJsxSameLineTokenContext, isForContext},
			 ruleActionInsertSpace),
		rule("NoSpaceAfterSemicolonInFor", Kind::SemicolonToken, anyToken,
			 {isOptionDisabledOrUndefined(insertSpaceAfterSemicolonInForStatementsOption),
			  isNonJsxSameLineTokenContext, isForContext},
			 ruleActionDeleteSpace),

		// Insert space before and after binary operators
		rule("SpaceBeforeBinaryOperator", anyToken, binaryOperators,
			 {isOptionEnabled(insertSpaceBeforeAndAfterBinaryOperatorsOption),
			  isNonJsxSameLineTokenContext, isBinaryOpContext},
			 ruleActionInsertSpace),
		rule("SpaceAfterBinaryOperator", binaryOperators, anyToken,
			 {isOptionEnabled(insertSpaceBeforeAndAfterBinaryOperatorsOption),
			  isNonJsxSameLineTokenContext, isBinaryOpContext},
			 ruleActionInsertSpace),
		rule("NoSpaceBeforeBinaryOperator", anyToken, binaryOperators,
			 {isOptionDisabledOrUndefined(insertSpaceBeforeAndAfterBinaryOperatorsOption),
			  isNonJsxSameLineTokenContext, isBinaryOpContext},
			 ruleActionDeleteSpace),
		rule("NoSpaceAfterBinaryOperator", binaryOperators, anyToken,
			 {isOptionDisabledOrUndefined(insertSpaceBeforeAndAfterBinaryOperatorsOption),
			  isNonJsxSameLineTokenContext, isBinaryOpContext},
			 ruleActionDeleteSpace),

		rule("SpaceBeforeOpenParenInFuncDecl", anyToken, Kind::OpenParenToken,
			 {isOptionEnabled(insertSpaceBeforeFunctionParenthesisOption),
			  isNonJsxSameLineTokenContext, isFunctionDeclContext},
			 ruleActionInsertSpace),
		rule("NoSpaceBeforeOpenParenInFuncDecl", anyToken, Kind::OpenParenToken,
			 {isOptionDisabledOrUndefined(insertSpaceBeforeFunctionParenthesisOption),
			  isNonJsxSameLineTokenContext, isFunctionDeclContext},
			 ruleActionDeleteSpace),

		// Open Brace braces after control block
		rule("NewLineBeforeOpenBraceInControl", controlOpenBraceLeftTokenRange,
			 Kind::OpenBraceToken,
			 {isOptionEnabled(placeOpenBraceOnNewLineForControlBlocksOption), isControlDeclContext,
			  isBeforeMultilineBlockContext},
			 ruleActionInsertNewLine, ruleFlagsCanDeleteNewLines),

		// Open Brace braces after function
		// TypeScript: Function can have return types, which can be made of tons of different token kinds
		rule("NewLineBeforeOpenBraceInFunction", functionOpenBraceLeftTokenRange,
			 Kind::OpenBraceToken,
			 {isOptionEnabled(placeOpenBraceOnNewLineForFunctionsOption), isFunctionDeclContext,
			  isBeforeMultilineBlockContext},
			 ruleActionInsertNewLine, ruleFlagsCanDeleteNewLines),
		// Open Brace braces after TypeScript module/class/interface
		rule("NewLineBeforeOpenBraceInTypeScriptDeclWithBlock", typeScriptOpenBraceLeftTokenRange,
			 Kind::OpenBraceToken,
			 {isOptionEnabled(placeOpenBraceOnNewLineForFunctionsOption),
			  isTypeScriptDeclWithBlockContext, isBeforeMultilineBlockContext},
			 ruleActionInsertNewLine, ruleFlagsCanDeleteNewLines),

		rule("SpaceAfterTypeAssertion", Kind::GreaterThanToken, anyToken,
			 {isOptionEnabled(insertSpaceAfterTypeAssertionOption), isNonJsxSameLineTokenContext,
			  isTypeAssertionContext},
			 ruleActionInsertSpace),
		rule("NoSpaceAfterTypeAssertion", Kind::GreaterThanToken, anyToken,
			 {isOptionDisabledOrUndefined(insertSpaceAfterTypeAssertionOption),
			  isNonJsxSameLineTokenContext, isTypeAssertionContext},
			 ruleActionDeleteSpace),

		rule("SpaceBeforeTypeAnnotation", anyToken, {Kind::QuestionToken, Kind::ColonToken},
			 {isOptionEnabled(insertSpaceBeforeTypeAnnotationOption), isNonJsxSameLineTokenContext,
			  isTypeAnnotationContext},
			 ruleActionInsertSpace),
		rule("NoSpaceBeforeTypeAnnotation", anyToken, {Kind::QuestionToken, Kind::ColonToken},
			 {isOptionDisabledOrUndefined(insertSpaceBeforeTypeAnnotationOption),
			  isNonJsxSameLineTokenContext, isTypeAnnotationContext},
			 ruleActionDeleteSpace),

		rule("NoOptionalSemicolon", Kind::SemicolonToken, anyTokenIncludingEOF,
			 {optionEquals(semicolonOption, lsutil::SemicolonPreferenceRemove),
			  isSemicolonDeletionContext},
			 ruleActionDeleteToken),
		rule("OptionalSemicolon", anyToken, anyTokenIncludingEOF,
			 {optionEquals(semicolonOption, lsutil::SemicolonPreferenceInsert),
			  isSemicolonInsertionContext},
			 ruleActionInsertTrailingSemicolon),
	};

	// These rules are lower in priority than user-configurable. Rules earlier in this list have priority over rules later in the list.
	std::vector<ruleSpec> lowPriorityCommonRules = {
		// Space after keyword but not before ; or : or ?
		rule("NoSpaceBeforeSemicolon", anyToken, Kind::SemicolonToken,
			 {isNonJsxSameLineTokenContext}, ruleActionDeleteSpace),

		rule("SpaceBeforeOpenBraceInControl", controlOpenBraceLeftTokenRange, Kind::OpenBraceToken,
			 {isOptionDisabledOrUndefinedOrTokensOnSameLine(
				  placeOpenBraceOnNewLineForControlBlocksOption),
			  isControlDeclContext, isNotFormatOnEnter, isSameLineTokenOrBeforeBlockContext},
			 ruleActionInsertSpace, ruleFlagsCanDeleteNewLines),
		rule("SpaceBeforeOpenBraceInFunction", functionOpenBraceLeftTokenRange,
			 Kind::OpenBraceToken,
			 {isOptionDisabledOrUndefinedOrTokensOnSameLine(placeOpenBraceOnNewLineForFunctionsOption),
			  isFunctionDeclContext, isBeforeBlockContext, isNotFormatOnEnter,
			  isSameLineTokenOrBeforeBlockContext},
			 ruleActionInsertSpace, ruleFlagsCanDeleteNewLines),
		rule("SpaceBeforeOpenBraceInTypeScriptDeclWithBlock", typeScriptOpenBraceLeftTokenRange,
			 Kind::OpenBraceToken,
			 {isOptionDisabledOrUndefinedOrTokensOnSameLine(placeOpenBraceOnNewLineForFunctionsOption),
			  isTypeScriptDeclWithBlockContext, isNotFormatOnEnter,
			  isSameLineTokenOrBeforeBlockContext},
			 ruleActionInsertSpace, ruleFlagsCanDeleteNewLines),

		rule("NoSpaceBeforeComma", anyToken, Kind::CommaToken, {isNonJsxSameLineTokenContext},
			 ruleActionDeleteSpace),

		// No space before and after indexer `x[]{}`
		rule("NoSpaceBeforeOpenBracket",
			 anyTokenExcept({Kind::AsyncKeyword, Kind::CaseKeyword}), Kind::OpenBracketToken,
			 {isNonJsxSameLineTokenContext}, ruleActionDeleteSpace),
		rule("NoSpaceAfterCloseBracket", Kind::CloseBracketToken, anyToken,
			 {isNonJsxSameLineTokenContext, isNotBeforeBlockInFunctionDeclarationContext},
			 ruleActionDeleteSpace),
		rule("SpaceAfterSemicolon", Kind::SemicolonToken, anyToken, {isNonJsxSameLineTokenContext},
			 ruleActionInsertSpace),

		// Remove extra space between for and await
		rule("SpaceBetweenForAndAwaitKeyword", Kind::ForKeyword, Kind::AwaitKeyword,
			 {isNonJsxSameLineTokenContext}, ruleActionInsertSpace),

		// Remove extra spaces between ... and type name in tuple spread
		rule("SpaceBetweenDotDotDotAndTypeName", Kind::DotDotDotToken, typeNames,
			 {isNonJsxSameLineTokenContext}, ruleActionDeleteSpace),

		// Add a space between statements. All keywords except (do,else,case) has open/close parens after them.
		// So, we have a rule to add a space for {),Any}, {do,Any}, {else,Any}, and {case,Any}
		rule(
			"SpaceBetweenStatements",
			{Kind::CloseParenToken, Kind::DoKeyword, Kind::ElseKeyword, Kind::CaseKeyword},
			anyToken,
			{isNonJsxSameLineTokenContext, isNonJsxElementOrFragmentContext, isNotForContext},
			ruleActionInsertSpace
		),
		// This low-pri rule takes care of "try {", "catch {" and "finally {" in case the rule SpaceBeforeOpenBraceInControl didn't execute on FormatOnEnter.
		rule("SpaceAfterTryCatchFinally", {Kind::TryKeyword, Kind::CatchKeyword, Kind::FinallyKeyword},
			 Kind::OpenBraceToken, {isNonJsxSameLineTokenContext}, ruleActionInsertSpace),
	};

	std::vector<ruleSpec> result;
	result.reserve(highPriorityCommonRules.size() + userConfigurableRules.size() +
				   lowPriorityCommonRules.size());
	result.insert(result.end(), highPriorityCommonRules.begin(), highPriorityCommonRules.end());
	result.insert(result.end(), userConfigurableRules.begin(), userConfigurableRules.end());
	result.insert(result.end(), lowPriorityCommonRules.begin(), lowPriorityCommonRules.end());
	return result;
}

} // namespace tsc::format
