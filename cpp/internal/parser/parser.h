// parser.go / jsdoc.go / reparser.go / types.go / utilities.go — port to C++
#pragma once

#include "internal/ast/ast.h"
#include "internal/ast/precedence.h"
#include "internal/core/text.h"
#include "internal/core/types.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/scanner/scanner.h"

#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tsc {

// types.go
using ParseFlags = int32_t;
inline constexpr ParseFlags ParseFlagsNone = 0;
inline constexpr ParseFlags ParseFlagsYield = 1 << 0;
inline constexpr ParseFlags ParseFlagsAwait = 1 << 1;
inline constexpr ParseFlags ParseFlagsType = 1 << 2;
// Bit 3 is unused in Go's enum — keep the same gap so flag values match.
inline constexpr ParseFlags ParseFlagsIgnoreMissingOpenBrace = 1 << 4;
inline constexpr ParseFlags ParseFlagsJSDoc = 1 << 5;

// parser.go — ParsingContext / ParsingContexts
using ParsingContext = int32_t;
inline constexpr ParsingContext PCSourceElements = 0;
inline constexpr ParsingContext PCBlockStatements = 1;
inline constexpr ParsingContext PCSwitchClauses = 2;
inline constexpr ParsingContext PCSwitchClauseStatements = 3;
inline constexpr ParsingContext PCTypeMembers = 4;
inline constexpr ParsingContext PCClassMembers = 5;
inline constexpr ParsingContext PCEnumMembers = 6;
inline constexpr ParsingContext PCHeritageClauseElement = 7;
inline constexpr ParsingContext PCVariableDeclarations = 8;
inline constexpr ParsingContext PCObjectBindingElements = 9;
inline constexpr ParsingContext PCArrayBindingElements = 10;
inline constexpr ParsingContext PCArgumentExpressions = 11;
inline constexpr ParsingContext PCObjectLiteralMembers = 12;
inline constexpr ParsingContext PCJsxAttributes = 13;
inline constexpr ParsingContext PCJsxChildren = 14;
inline constexpr ParsingContext PCArrayLiteralMembers = 15;
inline constexpr ParsingContext PCParameters = 16;
inline constexpr ParsingContext PCJSDocParameters = 17;
inline constexpr ParsingContext PCRestProperties = 18;
inline constexpr ParsingContext PCTypeParameters = 19;
inline constexpr ParsingContext PCTypeArguments = 20;
inline constexpr ParsingContext PCTupleElementTypes = 21;
inline constexpr ParsingContext PCHeritageClauses = 22;
inline constexpr ParsingContext PCImportOrExportSpecifiers = 23;
inline constexpr ParsingContext PCImportAttributes = 24;
inline constexpr ParsingContext PCJSDocComment = 25;
inline constexpr ParsingContext PCCount = 26;
using ParsingContexts = uint32_t;

using JSDocScannerInfo = uint8_t;
inline constexpr JSDocScannerInfo JSDocScannerInfoHasJSDoc = 1 << 0;
inline constexpr JSDocScannerInfo JSDocScannerInfoHasDeprecated = 1 << 1;
inline constexpr JSDocScannerInfo JSDocScannerInfoHasSeeOrLink = 1 << 2;

struct JSDocInfo {
	Node* parent = nullptr;
	std::vector<Node*> jsDocs;
};

struct ParserState {
	ScannerState scannerState;
	NodeFlags contextFlags = 0;
	int diagnosticsLen = 0;
	int jsDiagnosticsLen = 0;
	int jsdocInfosLen = 0;
	int reparsedClonesLen = 0;
	bool statementHasAwaitIdentifier = false;
	bool hasParseError = false;
};

// jsdoc.go
using JSDocState = int32_t;
inline constexpr JSDocState JSDocStateBeginningOfLine = 0;
inline constexpr JSDocState JSDocStateSawAsterisk = 1;
inline constexpr JSDocState JSDocStateSavingComments = 2;
inline constexpr JSDocState JSDocStateSavingBackticks = 3;

using PropertyLikeParse = int32_t;
inline constexpr PropertyLikeParse PropertyLikeParseProperty = 1 << 0;
inline constexpr PropertyLikeParse PropertyLikeParseParameter = 1 << 1;
inline constexpr PropertyLikeParse PropertyLikeParseCallbackParameter = 1 << 2;

struct Parser {
	Scanner* scanner = nullptr;
	NodeFactory factory;

	SourceFileParseOptions opts;
	std::string_view sourceText;

	ScriptKind scriptKind = ScriptKind::Unknown;
	LanguageVariant languageVariant = LanguageVariant::Standard;
	std::vector<Diagnostic*> diagnostics;
	std::vector<Diagnostic*> jsDiagnostics;
	std::vector<Diagnostic*> jsdocDiagnostics;

	Kind token = Kind::Unknown;
	NodeFlags sourceFlags = 0;
	NodeFlags contextFlags = 0;
	ParsingContexts parsingContexts = 0;
	bool statementHasAwaitIdentifier = false;
	bool hasDeprecatedTag = false;
	bool hasParseError = false;

	int identifierCount = 0;
	std::unordered_set<int> notParenthesizedArrow;
	std::vector<JSDocInfo> jsdocInfos;
	std::vector<int> possibleAwaitSpans;
	std::vector<std::string> jsdocCommentsSpace;
	std::vector<CommentRange> jsdocCommentRangesSpace;
	std::vector<std::string> jsdocTagCommentsSpace;
	std::vector<Node*> jsdocTagCommentsPartsSpace;
	std::vector<Node*> reparseList;

	Node* currentParent = nullptr;
	std::vector<Node*> reparsedClones;
	std::function<bool(Node*)> setParentFromContext;

	Parser();
	void initializeClosures();
	bool isJavaScript();
	SourceFile* parseJSONText();
	void validateJsonValue(SourceFile* sourceFile, Node* valueExpression);
	void validateJsonObjectLiteral(SourceFile* sourceFile, Node* node);
	void initializeState(SourceFileParseOptions opts, std::string_view sourceText, ScriptKind scriptKind);
	void scanError(const DiagnosticMessage* message, int pos, int length,
                       const std::vector<std::string>& args = {});
	Diagnostic* parseErrorAt(int pos, int end, const DiagnosticMessage* message,
                                 const std::vector<std::string>& args = {});
	Diagnostic* parseErrorAtCurrentToken(
		const DiagnosticMessage* message,
		const std::vector<std::string>& args = {});
	Diagnostic* parseErrorAtRange(TextRange loc,
                                      const DiagnosticMessage* message,
                                      const std::vector<std::string>& args = {});
	ParserState mark();
	void rewind(ParserState state);
	Kind nextToken();
	Kind nextTokenWithoutCheck();
	Kind nextTokenJSDoc();
	Kind nextJSDocCommentTextToken(bool inBackticks);
	int nodePos();
	bool hasPrecedingLineBreak();
	JSDocScannerInfo jsdocScannerInfo();
	SourceFile* parseSourceFileWorker();
	void finishSourceFile(SourceFile* result, bool isDeclarationFile);
	std::unordered_map<Node*,std::vector<Node*>> createJSDocCache();
	Node* parseToplevelStatement(int i);
	Node* reparseTopLevelAwait(SourceFile* sourceFile);
	NodeList* parseEmptyNodeList();
	NodeList* createMissingList();
	bool abortParsingListOrMoveToNextToken(ParsingContext kind);
	bool isInSomeParsingContext();
	void parsingContextErrors(ParsingContext context);
	bool isListElement(ParsingContext parsingContext, bool inErrorRecovery);
	bool isListTerminator(ParsingContext kind);
	bool parseExpectedJSDoc(Kind kind);
	void parseExpectedMatchingBrackets(Kind openKind, Kind closeKind, bool openParsed, int openPosition);
	bool parseOptional(Kind token);
	bool parseExpected(Kind kind);
	bool parseExpectedWithoutAdvancing(Kind kind);
	bool parseExpectedWithDiagnostic(Kind kind, const DiagnosticMessage* message, bool shouldAdvance);
	Node* parseTokenNode();
	Node* parseExpectedToken(Kind kind);
	Node* parseOptionalToken(Kind kind);
	Node* parseExpectedTokenJSDoc(Kind kind);
	Node* parseOptionalTokenJSDoc(Kind kind);
	Node* parseStatement();
	Node* parseDeclaration();
	Node* parseDeclarationWorker(int pos, JSDocScannerInfo jsdoc, ModifierList* modifiers);
	bool isLetDeclaration();
	bool nextTokenIsBindingIdentifierOrStartOfDestructuring();
	Node* parseBlock(bool ignoreMissingOpenBrace, const DiagnosticMessage* diagnosticMessage);
	Node* parseEmptyStatement();
	Node* parseIfStatement();
	Node* parseDoStatement();
	Node* parseWhileStatement();
	Node* parseForOrForInOrForOfStatement();
	Node* parseBreakStatement();
	Node* parseContinueStatement();
	Node* parseIdentifierUnlessAtSemicolon();
	Node* parseReturnStatement();
	Node* parseWithStatement();
	Node* parseCaseClause();
	Node* parseDefaultClause();
	Node* parseCaseOrDefaultClause();
	Node* parseCaseBlock();
	Node* parseSwitchStatement();
	Node* parseThrowStatement();
	Node* parseTryStatement();
	Node* parseCatchClause();
	Node* parseDebuggerStatement();
	Node* parseExpressionOrLabeledStatement();
	Node* parseVariableStatement(int pos, JSDocScannerInfo jsdoc, ModifierList* modifiers);
	Node* parseVariableDeclarationList(bool inForStatementInitializer);
	bool nextIsIdentifierAndCloseParen();
	bool nextTokenIsIdentifier();
	Node* parseVariableDeclaration();
	Node* parseVariableDeclarationAllowExclamation();
	Node* parseVariableDeclarationWorker(bool allowExclamation);
	Node* parseIdentifierOrPattern();
	Node* parseIdentifierOrPatternWithDiagnostic(const DiagnosticMessage* privateIdentifierDiagnosticMessage);
	Node* parseArrayBindingPattern();
	Node* parseArrayBindingElement();
	Node* parseObjectBindingPattern();
	Node* parseObjectBindingElement();
	Node* parseInitializer();
	Node* parseTypeAnnotation();
	Node* parseFunctionDeclaration(int pos, JSDocScannerInfo jsdoc, ModifierList* modifiers);
	Node* parseClassDeclaration(int pos, JSDocScannerInfo jsdoc, ModifierList* modifiers);
	Node* parseClassExpression();
	Node* parseClassDeclarationOrExpression(int pos, JSDocScannerInfo jsdoc, ModifierList* modifiers, Kind kind);
	Node* parseNameOfClassDeclarationOrExpression();
	bool isImplementsClause();
	NodeList* parseHeritageClauses(bool isInterface);
	Node* parseHeritageClause(bool isInterface);
	Node* parseTypeHeritageClauseElement();
	Node* convertEntityNameExpressionToEntityName(Node* node);
	Node* parseExpressionWithTypeArguments();
	Node* parseClassElement();
	Node* parseClassStaticBlockDeclaration(int pos, JSDocScannerInfo jsdoc, ModifierList* modifiers);
	Node* parseClassStaticBlockBody();
	Node* tryParseConstructorDeclaration(int pos, JSDocScannerInfo jsdoc, ModifierList* modifiers);
	bool nextTokenIsOpenParen();
	Node* parsePropertyOrMethodDeclaration(int pos, JSDocScannerInfo jsdoc, ModifierList* modifiers);
	Node* parseMethodDeclaration(int pos, JSDocScannerInfo jsdoc, ModifierList* modifiers, Node* asteriskToken, Node* name, Node* questionToken, const DiagnosticMessage* diagnosticMessage);
	Node* parsePropertyDeclaration(int pos, JSDocScannerInfo jsdoc, ModifierList* modifiers, Node* name, Node* questionToken);
	void parseSemicolonAfterPropertyName(Node* name, Node* typeNode, Node* initializer);
	void parseErrorForMissingSemicolonAfter(Node* node);
	void parseErrorForInvalidName(const DiagnosticMessage* nameDiagnostic, const DiagnosticMessage* blankDiagnostic, Kind tokenIfBlankName);
	Node* parseInterfaceDeclaration(int pos, JSDocScannerInfo jsdoc, ModifierList* modifiers);
	Node* parseTypeAliasDeclaration(int pos, JSDocScannerInfo jsdoc, ModifierList* modifiers);
	bool nextIsNotDot();
	Node* parseEnumMember();
	Node* parseEnumDeclaration(int pos, JSDocScannerInfo jsdoc, ModifierList* modifiers);
	Node* parseModuleDeclaration(int pos, JSDocScannerInfo jsdoc, ModifierList* modifiers);
	Node* parseAmbientExternalModuleDeclaration(int pos, JSDocScannerInfo jsdoc, ModifierList* modifiers);
	Node* parseModuleBlock();
	Node* parseModuleOrNamespaceDeclaration(int pos, JSDocScannerInfo jsdoc, ModifierList* modifiers, bool nested, Kind keyword);
	Node* parseImportDeclarationOrImportEqualsDeclaration(int pos, JSDocScannerInfo jsdoc, ModifierList* modifiers);
	bool nextTokenIsFromKeywordOrEqualsToken();
	bool tokenAfterImportDefinitelyProducesImportDeclaration();
	bool tokenAfterImportedIdentifierDefinitelyProducesImportDeclaration();
	Node* parseImportEqualsDeclaration(int pos, JSDocScannerInfo jsdoc, ModifierList* modifiers, Node* identifier, bool isTypeOnly);
	Node* parseModuleReference();
	Node* parseExternalModuleReference();
	Node* parseModuleSpecifier();
	Node* tryParseImportClause(Node* identifier, int pos, Kind phaseModifier, bool skipJSDocLeadingAsterisks);
	Node* parseImportClause(Node* identifier, int pos, Kind phaseModifier, bool skipJSDocLeadingAsterisks);
	Node* parseNamespaceImport();
	Node* parseNamedImports();
	Node* parseImportSpecifier();
	struct ImportOrExportSpecifierResult {
		bool isTypeOnly;
		Node* propertyName;
		Node* name;
	};
	ImportOrExportSpecifierResult parseImportOrExportSpecifier(Kind kind);
	bool canParseModuleExportName();
	struct ModuleExportNameResult {
		Node* node;
		bool nameOk;
	};
	ModuleExportNameResult parseModuleExportName(bool disallowKeywords);
	Node* tryParseImportAttributes();
	Node* parseExportAssignment(int pos, JSDocScannerInfo jsdoc, ModifierList* modifiers);
	Node* parseNamespaceExportDeclaration(int pos, JSDocScannerInfo jsdoc, ModifierList* modifiers);
	Node* parseExportDeclaration(int pos, JSDocScannerInfo jsdoc, ModifierList* modifiers);
	Node* parseNamespaceExport(int pos);
	Node* parseNamedExports();
	Node* parseExportSpecifier();
	Node* parseType();
	Node* parseUnionTypeOrHigher();
	Node* parseIntersectionTypeOrHigher();
	Node* createUnionOrIntersectionTypeNode(Kind operator_, NodeList* types);
	Node* parseTypeOperatorOrHigher();
	Node* parseTypeOperator(Kind operator_);
	Node* parseInferType();
	Node* parseTypeParameterOfInferType();
	Node* tryParseConstraintOfInferType();
	Node* parsePostfixTypeOrHigher();
	bool nextIsStartOfType();
	Node* parseNonArrayType();
	Node* parseKeywordTypeNode();
	Node* parseThisTypeNode();
	Node* parseThisTypePredicate(Node* lhs);
	Node* parseJSDocAllType();
	Node* parseJSDocNonNullableType();
	Node* parseJSDocNullableType();
	Node* parseJSDocType();
	Node* parseLiteralTypeNode(bool negative);
	Node* parseTypeReference();
	Node* parseEntityNameOfTypeReference();
	Node* parseEntityName(bool allowReservedWords, bool allowPrivateName, const DiagnosticMessage* diagnosticMessage);
	Node* parseRightSideOfDot(bool allowIdentifierNames, bool allowPrivateIdentifiers, bool allowUnicodeEscapeSequenceInIdentifierName);
	Node* newIdentifier(std::string text);
	Node* createMissingIdentifier();
	Node* parsePrivateIdentifier();
	Kind reScanLessThanToken();
	Kind reScanGreaterThanToken();
	Kind reScanSlashToken();
	Kind reScanTemplateToken(bool isTaggedTemplate);
	NodeList* parseTypeArgumentsOfTypeReference();
	NodeList* parseTypeArguments();
	bool nextIsStartOfTypeOfImportType();
	Node* parseImportType();
	Node* parseImportAttribute();
	Node* parseImportAttributes(Kind token, bool skipKeyword);
	Node* parseTypeQuery();
	bool nextIsStartOfMappedType();
	Node* parseMappedType();
	Node* parseMappedTypeParameter();
	Node* parseTypeMember();
	bool nextTokenIsOpenParenOrLessThan();
	Node* parseSignatureMember(Kind kind);
	NodeList* parseTypeParameters();
	Node* parseTypeParameter();
	NodeList* parseParameters(ParseFlags flags);
	NodeList* parseParametersWorker(ParseFlags flags, bool allowAmbiguity);
	Node* parseParameter();
	Node* parseParameterEx(bool inOuterAwaitContext, bool allowAmbiguity);
	bool isParameterNameStart();
	Node* parseNameOfParameter(ModifierList* modifiers);
	Node* parseReturnType(Kind returnToken, bool isType);
	bool shouldParseReturnType(Kind returnToken, bool isType);
	Node* parseTypeOrTypePredicate();
	void parseTypeMemberSemicolon();
	Node* parseAccessorDeclaration(int pos, JSDocScannerInfo jsdoc, ModifierList* modifiers, Kind kind, ParseFlags flags);
	Node* parsePropertyName();
	Node* parsePropertyNameWorker(bool allowComputedPropertyNames);
	Node* parseComputedPropertyName();
	Node* parseFunctionBlockOrSemicolon(ParseFlags flags, const DiagnosticMessage* diagnosticMessage);
	Node* parseFunctionBlock(ParseFlags flags, const DiagnosticMessage* diagnosticMessage);
	bool isIndexSignature();
	bool nextIsUnambiguouslyIndexSignature();
	Node* parseIndexSignatureDeclaration(int pos, JSDocScannerInfo jsdoc, ModifierList* modifiers);
	Node* parsePropertyOrMethodSignature(int pos, JSDocScannerInfo jsdoc, ModifierList* modifiers);
	Node* parseTypeLiteral();
	NodeList* parseObjectTypeMembers();
	Node* parseTupleType();
	Node* parseTupleElementNameOrTupleElementType();
	bool scanStartOfNamedTupleElement();
	bool nextTokenIsColonOrQuestionColon();
	Node* parseTupleElementType();
	Node* parseParenthesizedType();
	Node* parseAssertsTypePredicate();
	Node* parseTemplateType();
	Node* parseTemplateHead(bool isTaggedTemplate);
	std::string getTemplateLiteralRawText(int endLength);
	NodeList* parseTemplateTypeSpans();
	Node* parseTemplateTypeSpan();
	Node* parseLiteralOfTemplateSpan(bool isTaggedTemplate);
	Node* parseTemplateMiddleOrTail();
	bool isStartOfFunctionTypeOrConstructorType();
	Node* parseFunctionOrConstructorType();
	ModifierList* parseModifiersForConstructorType();
	bool nextTokenIsNewKeyword();
	bool nextIsUnambiguouslyStartOfFunctionType();
	bool skipParameterStart();
	ModifierList* parseModifiers();
	ModifierList* parseModifiersEx(bool allowDecorators, bool permitConstAsModifier, bool stopOnStartOfClassStaticBlock);
	Node* parseDecorator();
	Node* parseDecoratorExpression();
	Node* tryParseModifier(bool hasSeenStaticModifier, bool permitConstAsModifier, bool stopOnStartOfClassStaticBlock);
	bool parseContextualModifier(Kind t);
	bool parseAnyContextualModifier();
	bool nextTokenCanFollowModifier();
	bool nextTokenCanFollowDefaultKeyword();
	bool nextTokenIsIdentifierOrKeyword();
	bool nextTokenIsIdentifierOrKeywordOrGreaterThan();
	bool nextTokenIsIdentifierOrKeywordOnSameLine();
	bool nextTokenIsIdentifierOrKeywordOrLiteralOnSameLine();
	bool nextTokenIsClassKeywordOnSameLine();
	bool nextTokenIsFunctionKeywordOnSameLine();
	bool nextTokenCanFollowExportModifier();
	bool canFollowExportModifier();
	bool canFollowModifier();
	bool canFollowGetOrSetKeyword();
	bool nextTokenIsOnSameLineAndCanFollowModifier();
	bool nextTokenIsOpenBrace();
	Node* parseExpression();
	Node* parseExpressionAllowIn();
	Node* parseAssignmentExpressionOrHigher();
	Node* parseAssignmentExpressionOrHigherWorker(bool allowReturnTypeInArrowFunction);
	bool isYieldExpression();
	Node* parseYieldExpression();
	Tristate isParenthesizedArrowFunctionExpression();
	Tristate nextIsParenthesizedArrowFunctionExpression();
	Node* tryParseParenthesizedArrowFunctionExpression(bool allowReturnTypeInArrowFunction);
	Node* parseParenthesizedArrowFunctionExpression(bool allowAmbiguity, bool allowReturnTypeInArrowFunction);
	ModifierList* parseModifiersForArrowFunction();
	Node* parseArrowFunctionExpressionBody(bool isAsync, bool allowReturnTypeInArrowFunction);
	bool isStartOfExpressionStatement();
	Node* parsePossibleParenthesizedArrowFunctionExpression(bool allowReturnTypeInArrowFunction);
	Node* tryParseAsyncSimpleArrowFunctionExpression(bool allowReturnTypeInArrowFunction);
	bool nextIsUnParenthesizedAsyncArrowFunction();
	Node* parseSimpleArrowFunctionExpression(int pos, Node* identifier, bool allowReturnTypeInArrowFunction, JSDocScannerInfo jsdoc, ModifierList* asyncModifier);
	Node* parseConditionalExpressionRest(Node* leftOperand, int pos, bool allowReturnTypeInArrowFunction);
	Node* parseBinaryExpressionOrHigher(OperatorPrecedence precedence);
	Node* parseBinaryExpressionRest(OperatorPrecedence precedence, Node* leftOperand, int pos);
	Node* makeSatisfiesExpression(Node* expression, Node* typeNode);
	Node* makeAsExpression(Node* left, Node* right);
	Node* makeBinaryExpression(Node* left, Node* operatorToken, Node* right, int pos);
	Node* parseUnaryExpressionOrHigher();
	bool isUpdateExpression();
	Node* parseUpdateExpression();
	Node* parseJsxElementOrSelfClosingElementOrFragment(bool inExpressionContext, int topInvalidNodePosition, Node* openingTag, bool mustBeUnary);
	NodeList* parseJsxChildren(Node* openingTag);
	Node* parseJsxChild(Node* openingTag, Kind token);
	Node* parseJsxText();
	Node* parseJsxExpression(bool inExpressionContext);
	Kind scanJsxText();
	Kind scanJsxIdentifier();
	Kind scanJsxAttributeValue();
	Node* parseJsxClosingElement(Node* open, bool inExpressionContext);
	Node* parseJsxOpeningOrSelfClosingElementOrOpeningFragment(bool inExpressionContext);
	Node* parseJsxElementName();
	Node* parseJsxTagName();
	Node* parseJsxAttributes();
	Node* parseJsxAttribute();
	Node* parseJsxSpreadAttribute();
	Node* parseJsxAttributeName();
	Node* parseJsxAttributeValue();
	Node* parseJsxClosingFragment(bool inExpressionContext);
	Node* parseSimpleUnaryExpression();
	Node* parsePrefixUnaryExpression();
	Node* parseDeleteExpression();
	Node* parseTypeOfExpression();
	Node* parseVoidExpression();
	bool isAwaitExpression();
	Node* parseAwaitExpression();
	Node* parseTypeAssertion();
	Node* parseLeftHandSideExpressionOrHigher();
	bool nextTokenIsDot();
	Node* parseSuperExpression();
	bool isTemplateStartOfTaggedTemplate();
	NodeList* tryParseTypeArgumentsInExpression();
	bool canFollowTypeArgumentsInExpression();
	Node* parseMemberExpressionOrHigher();
	Node* parseMemberExpressionRest(int pos, Node* expression, bool allowOptionalChain);
	bool isStartOfOptionalPropertyOrElementAccessChain();
	bool nextTokenIsIdentifierOrKeywordOrOpenBracketOrTemplate();
	Node* parsePropertyAccessExpressionRest(int pos, Node* expression, Node* questionDotToken);
	bool tryReparseOptionalChain(Node* node);
	Node* parseElementAccessExpressionRest(int pos, Node* expression, Node* questionDotToken);
	Node* parseCallExpressionRest(int pos, Node* expression);
	NodeList* parseArgumentList();
	Node* parseArgumentExpression();
	Node* parseArgumentOrArrayLiteralElement();
	Node* parseSpreadElement();
	Node* parseTaggedTemplateRest(int pos, Node* tag, Node* questionDotToken, NodeList* typeArguments);
	Node* parseTemplateExpression(bool isTaggedTemplate);
	NodeList* parseTemplateSpans(bool isTaggedTemplate);
	Node* parseTemplateSpan(bool isTaggedTemplate);
	Node* parsePrimaryExpression();
	Node* parseParenthesizedExpression();
	Node* parseArrayLiteralExpression();
	Node* parseObjectLiteralExpression();
	Node* parseObjectLiteralElement();
	Node* parseFunctionExpression();
	Node* parseOptionalBindingIdentifier();
	Node* parseDecoratedExpression();
	void unparseExpressionWithTypeArguments(Node* expression, NodeList* typeArguments, Node* result);
	Node* parseNewExpressionOrNewDotTarget();
	Node* parseKeywordExpression();
	Node* parseLiteralExpression();
	Node* parseIdentifierNameErrorOnUnicodeEscapeSequence();
	Node* parseBindingIdentifier();
	Node* parseBindingIdentifierWithDiagnostic(const DiagnosticMessage* privateIdentifierDiagnosticMessage);
	Node* parseIdentifierName();
	Node* parseIdentifierNameWithDiagnostic(const DiagnosticMessage* diagnosticMessage);
	Node* parseIdentifier();
	Node* parseIdentifierWithDiagnostic(const DiagnosticMessage* diagnosticMessage, const DiagnosticMessage* privateIdentifierDiagnosticMessage);
	Node* createIdentifier(bool isIdentifier);
	Node* createIdentifierWithDiagnostic(bool isIdentifier, const DiagnosticMessage* diagnosticMessage, const DiagnosticMessage* privateIdentifierDiagnosticMessage);
	NodeList* newNodeList(TextRange loc, std::vector<Node*> nodes);
	ModifierList* newModifierList(TextRange loc, std::vector<Node*> nodes);
	Node* finishNode(Node* node, int pos);
	Node* finishNodeWithEnd(Node* node, int pos, int end);
	void overrideParentInImmediateChildren(Node* node);
	bool nextTokenIsSlash();
	bool scanTypeMemberStart();
	bool scanClassMemberStart();
	bool canParseSemicolon();
	bool tryParseSemicolon();
	bool parseSemicolon();
	bool isLiteralPropertyName();
	bool isStartOfStatement();
	bool isStartOfDeclaration();
	bool scanStartOfDeclaration();
	bool isStartOfExpression();
	bool isStartOfLeftHandSideExpression();
	bool isStartOfType(bool inStartOfParameter);
	bool nextTokenIsNumericOrBigIntLiteral();
	bool nextIsParenthesizedOrFunctionType();
	bool isStartOfParameter(bool isJSDocParameter);
	bool isBindingIdentifierOrPrivateIdentifierOrPattern();
	bool isNextTokenOpenParenOrLessThanOrDot();
	bool nextTokenIsOpenParenOrLessThanOrDot();
	bool nextTokenIsIdentifierOnSameLine();
	bool nextTokenIsIdentifierOrStringLiteralOnSameLine();
	bool isIdentifier();
	bool isBindingIdentifier();
	bool isImportAttributeName();
	bool isBinaryOperator();
	bool isValidHeritageClauseObjectLiteral();
	bool nextIsValidHeritageClauseObjectLiteral();
	bool isHeritageClause();
	bool isHeritageClauseExtendsOrImplementsKeyword();
	bool nextIsStartOfExpression();
	bool isUsingDeclaration();
	bool nextTokenIsEqualsOrSemicolonOrColonToken();
	bool nextTokenIsBindingIdentifierOrStartOfDestructuringOnSameLine(bool disallowOf);
	bool nextTokenIsBindingIdentifierOrStartOfDestructuringOnSameLineDisallowOf();
	bool isAwaitUsingDeclaration();
	bool nextIsUsingKeywordThenBindingIdentifierOrStartOfObjectDestructuringOnSameLine();
	bool nextTokenIsTokenStringLiteral();
	void setContextFlags(NodeFlags flags, bool value);
	bool inYieldContext();
	bool inDisallowInContext();
	bool inDisallowConditionalTypesContext();
	bool inDecoratorContext();
	bool inAwaitContext();
	TextRange skipRangeTrivia(TextRange textRange);
	std::string getSpaceSuggestion(std::string_view expressionText);
	void processPragmasIntoFields(SourceFile* context);
	ResolutionMode parseResolutionMode(std::string_view mode, int pos, int end);
	void jsErrorAtRange(TextRange loc, const DiagnosticMessage* message,
	                    const std::vector<std::string>& args = {});
	void checkJSDecoratorSyntax(Node* node);
	Node* checkJSSyntax(Node* node);
	std::vector<Node*> withJSDoc(Node* node, JSDocScannerInfo info);
	Node* parseJSDocTypeExpression(bool mayOmitBraces);
	Node* parseJSDocNameReference();
	Node* parseJSDocComment(Node* parent, int start, int end, int fullStart);
	Node* parseJSDocCommentWorker(int start, int end, int fullStart, int indent);
	bool isNextNonwhitespaceTokenEndOfFile();
	void skipWhitespace();
	std::string skipWhitespaceOrAsterisk();
	Node* parseTag(std::vector<Node*> tags, int margin);
	NodeList* parseTrailingTagComments(int pos, int end, int margin, std::string_view indentText);
	NodeList* parseTagComments(int indent, std::optional<std::string> initialMargin);
	Node* parseJSDocLink(int start);
	Node* parseJSDocLinkName();
	std::pair<std::string,bool> parseJSDocLinkPrefix();
	Node* parseUnknownTag(int start, Node* tagName, int indent, std::string indentText);
	Node* tryParseTypeExpression();
	std::pair<Node*,bool> parseBracketNameInPropertyAndParamTag(PropertyLikeParse target);
	Node* parseParameterOrPropertyTag(int start, Node* tagName, PropertyLikeParse target, int indent);
	Node* parseNestedTypeLiteral(Node* typeExpression, Node* name, PropertyLikeParse target, int indent);
	Node* parseReturnTag(std::vector<Node*> previousTags, int start, Node* tagName, int indent, std::string indentText);
	Node* parseTypeTag(std::vector<Node*> previousTags, int start, Node* tagName, int indent, std::string indentText);
	Node* parseSeeTag(int start, Node* tagName, int indent, std::string indentText);
	Node* parseImplementsTag(int start, Node* tagName, int margin, std::string indentText);
	Node* parseAugmentsTag(int start, Node* tagName, int margin, std::string indentText);
	Node* parseSatisfiesTag(int start, Node* tagName, int margin, std::string indentText);
	Node* parseThrowsTag(int start, Node* tagName, int margin, std::string indentText);
	Node* parseImportTag(int start, Node* tagName, int margin, std::string indentText);
	Node* parseExpressionWithTypeArgumentsForAugments();
	Node* parsePropertyAccessEntityNameExpression();
	Node* parseThisTag(int start, Node* tagName, int margin, std::string indentText);
	Node* parseJSDocTypeNameWithNamespace(bool nested);
	Node* parseTypedefTag(int start, Node* tagName, int indent, std::string indentText);
	NodeList* parseCallbackTagParameters(int indent);
	Node* parseJSDocSignature(int start, int indent);
	Node* parseCallbackTag(int start, Node* tagName, int indent, std::string indentText);
	Node* parseOverloadTag(int start, Node* tagName, int indent, std::string indentText);
	Node* parseChildPropertyTag(int indent);
	Node* parseChildParameterOrPropertyTag(PropertyLikeParse target, int indent, Node* name);
	Node* tryParseChildTag(PropertyLikeParse target, int indent);
	Node* parseTemplateTagTypeParameter();
	NodeList* parseTemplateTagTypeParameters();
	Node* parseTemplateTag(int start, Node* tagName, int indent, std::string indentText);
	bool parseOptionalJsdoc(Kind t);
	Node* parseJSDocEntityName(const DiagnosticMessage* diagnosticMessage);
	Node* parseJSDocIdentifierName(const DiagnosticMessage* diagnosticMessage);
	void finishReparsedNode(Node* node, Node* locationNode);
	void finishMutatedNode(Node* node);
	Node* addDeepCloneReparse(Node* node);
	Node* addTransformedReparse(Node* newNode, Node* old);
	Node* checkNonIdentifierName(Node* name);
	void reparseTags(Node* parent, std::vector<Node*> jsDoc);
	void reparseUnhosted(Node* tag, Node* parent, Node* jsDoc);
	Node* reparseJSDocSignature(Node* jsSignature, Node* fun, Node* jsDoc, Node* tag, ModifierList* modifiers);
	Node* reparseJSDocTypeLiteral(Node* t);
	void reparseJSDocComment(Node* node, Node* tag);
	NodeList* gatherTypeParameters(Node* j, bool typedefOrCallback);
	void reparseHosted(Node* tag, Node* parent, Node* jsDoc);
	Node* makeQuestionIfOptional(Node* parameter);
	Node* makeNewCast(Node* t, Node* e, bool isAssertion);
	ModifierList* createExportModifier(Node* locationNode);
	Node* getInnermostNameOfJSDocNamespace(Node* fullName);
	Node* wrapInJSDocNamespace(Node* fullName, Node* statement, bool nested);

	// methods with function-typed parameters (Go callbacks -> member ptrs)
	bool lookAhead(const std::function<bool(Parser*)>& callback);
	std::vector<Node*> parseListIndex(ParsingContext kind,
	                                Node* (Parser::*parseElement)(int));
	NodeList* parseList(ParsingContext kind, Node* (Parser::*parseElement)());
	NodeList* parseDelimitedList(ParsingContext kind,
	                           Node* (Parser::*parseElement)());
	NodeList* parseBracketedList(ParsingContext kind,
	                           Node* (Parser::*parseElement)(), Kind opening,
	                           Kind closing);
	Node* parseUnionOrIntersectionType(Kind operator_,
	                                 Node* (Parser::*parseConstituentType)());
	Node* parseFunctionOrConstructorTypeToError(
		bool isInUnionType, Node* (Parser::*parseConstituentType)());
	Node* (Parser::*listElementFn)() = nullptr;
	Node* doInContext(NodeFlags flags, bool value, Node* (Parser::*f)());
	bool scratchBool_ = false;
	bool scratchBool2_ = false;
	int scratchFlags_ = 0;
	Node* parseParameterInList();
	Node* parseHeritageClauseForList();
	Node* parseListElementTrampoline(int);

	Node* parseSimpleTag(
		int start,
		const std::function<Node*(Node* tagName, NodeList* comments)>&
			createTag,
		Node* tagName, int margin, std::string indentText);
};

// Free functions (parser.go / utilities.go / jsdoc.go / reparser.go)
Parser* getParser();
void putParser(Parser* p);
SourceFile* parseSourceFile(SourceFileParseOptions opts,
                            std::string_view sourceText, ScriptKind scriptKind);
Node* parseIsolatedEntityName(std::string_view text);
std::vector<CommentRange> getJSDocCommentRanges(
	std::vector<CommentRange>* commentRanges, Node* node,
	std::string_view text);
bool isJSDocLikeText(std::string_view text);
bool isKeywordOrPunctuation(Kind token);
TextRange getErrorSpanForNode(std::string_view sourceText, Node* node);
bool isMissingNodeList(NodeList* list);
bool isReservedWord(Kind token);

bool isDoubleQuotedString(Node* node);
LanguageVariant getLanguageVariant(ScriptKind scriptKind);

} // namespace tsc
