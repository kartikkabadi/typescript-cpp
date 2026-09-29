// Port of tsc/internal/parser/{parser,jsdoc,reparser,types,utilities}.go
// and tsc/internal/parser/references.go to C++.
//
// Translation conventions:
//   - `p.foo` field access stays `foo` (all Parser state is public members)
//   - `p.factory.NewX()` -> `factory.newX()`
//   - `p.scanner.Foo()` -> `scanner->foo()`
//   - `x.AsFoo()` -> `x->as<Foo>()`; `x.AsFoo().Field` -> `x->as<Foo>()->Field`
//   - `node.Name()` -> `node->name()` (lowercase Node accessors)
//   - `diagnostics.X` -> `X` (DiagnosticMessage* constants)
//   - `core.NewTextRange(a,b)` -> `TextRange{a,b}`; `core.TextRange{...}` same
//   - `ast.NewDiagnostic(nil, loc, msg, args...)` -> `newDetachedDiagnostic`
//   - `panic(...)` -> `TSC_UNREACHABLE` / fail
//   - `.Nodes` -> `->nodes`; `len(x)` -> `x.size()`; `append` -> `push_back`
//   - `core.IfElse` -> `?:`
#include "internal/parser/parser.h"

#include "internal/ast/ast.h"
#include "internal/ast/kind.h"
#include "internal/ast/nodes_generated.h"
#include "internal/core/text.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/jsnum/jsnum.h"
#include "internal/scanner/scanner.h"
#include "internal/stringutil/stringutil.h"
#include "internal/stringutil/unicode.h"
#include "internal/tspath/tspath.h"
#include "internal/core/spelling.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace tsc {

// ---------------------------------------------------------------------------
// Parser pool (Go sync.Pool -> thread_local freelist)
// ---------------------------------------------------------------------------

namespace {
thread_local std::vector<Parser*> g_parserPool;
} // namespace

Parser* getParser() {
	if (!g_parserPool.empty()) {
		Parser* p = g_parserPool.back();
		g_parserPool.pop_back();
		return p;
	}
	auto* p = new Parser();
	p->initializeClosures();
	return p;
}

void putParser(Parser* p) {
	Scanner* scanner = p->scanner;
	p->~Parser();
	new (p) Parser();
	p->scanner = scanner;
	p->initializeClosures();
	g_parserPool.push_back(p);
}

// fwd decls for helpers defined later in this file (Go order)
static bool isValidHeritageTypeReferenceExpression(Node* node);
std::vector<Diagnostic*> attachFileToDiagnostics(
	std::vector<Diagnostic*> diagnostics, SourceFile* file);
std::vector<Pragma> getCommentPragmas(NodeFactory* f,
                                    std::string_view sourceText);
void collectExternalModuleReferences(SourceFile* file);
void setExternalModuleIndicator(SourceFile* file,
                                const ExternalModuleIndicatorOptions& opts);


Parser::Parser() { initializeClosures(); }
void Parser::initializeClosures() {
	setParentFromContext = [this](Node* n) -> bool {
		n->parent = currentParent;
		return false;
	};
}

// isMissingNodeList — Go compares against a shared cap-1 backing array; in C++
// createMissingList() is the only place that produces an empty vector with
// capacity 1, so capacity is the sentinel.
bool isMissingNodeList(NodeList* list) {
	return list != nullptr && list->nodes.empty() && list->nodes.capacity() == 1;
}

static bool typeHasArrowFunctionBlockingParseError(Node* typeNode);


static bool tokenIsIdentifierOrKeywordOrGreaterThan(Kind token) {
	return token == Kind::GreaterThanToken || tokenIsIdentifierOrKeyword(token);
}

// ---------------------------------------------------------------------------
// parser.go
// ---------------------------------------------------------------------------

LanguageVariant getLanguageVariant(ScriptKind scriptKind) {
	// .tsx and .ts files are all in the standard variant, as are .js/.jsx/.json
	// files (which we parse in the JsxEmit variant context where the variant only
	// changes scanning semantics inside JSX).
	switch (scriptKind) {
	case ScriptKind::TSX:
	case ScriptKind::JSX:
	case ScriptKind::JS:
	case ScriptKind::JSON:
		// .tsx and .js files are all in the JSX variant.
		return LanguageVariant::JSX;
	default:
		return LanguageVariant::Standard;
	}
}

SourceFile* parseSourceFile(SourceFileParseOptions opts,
                            std::string_view sourceText, ScriptKind scriptKind) {
	Parser* p = getParser();
	p->initializeState(opts, sourceText, scriptKind);
	p->nextToken();
	SourceFile* result;
	if (p->scriptKind == ScriptKind::JSON) {
		result = p->parseJSONText();
	} else {
		result = p->parseSourceFileWorker();
	}
	putParser(p);
	return result;
}

bool Parser::isJavaScript() {
	return scriptKind == ScriptKind::JS || scriptKind == ScriptKind::JSX;
}

SourceFile* Parser::parseJSONText() {
	int pos = nodePos();
	NodeList* statements;
	Node* eof;

	if (token == Kind::EndOfFile) {
		statements = newNodeList(TextRange{pos, nodePos()}, {});
		eof = parseTokenNode();
	} else {
		std::vector<Node*> expressions;

		while (token != Kind::EndOfFile) {
			Node* expression;
			switch (token) {
			case Kind::OpenBracketToken:
				expression = parseArrayLiteralExpression();
				break;
			case Kind::TrueKeyword:
			case Kind::FalseKeyword:
			case Kind::NullKeyword:
				expression = parseTokenNode();
				break;
			case Kind::MinusToken:
				if (lookAhead([](Parser* p) {
					    auto t = p->nextToken();
					    return t == Kind::NumericLiteral &&
					           p->nextToken() != Kind::ColonToken;
				    })) {
					expression = parsePrefixUnaryExpression();
				} else {
					expression = parseObjectLiteralExpression();
				}
				break;
			case Kind::NumericLiteral:
			case Kind::StringLiteral:
				if (lookAhead([](Parser* p) {
					    return p->nextToken() != Kind::ColonToken;
				    })) {
					expression = parseLiteralExpression();
					break;
				}
				[[fallthrough]];
			default:
				expression = parseObjectLiteralExpression();
			}

			// Error recovery: collect multiple top-level expressions
			if (!expressions.empty()) {
				expressions.push_back(expression);
			} else {
				expressions.push_back(expression);
				if (token != Kind::EndOfFile) {
					parseErrorAtCurrentToken(Unexpected_token);
				}
			}
		}

		Node* expression;
		if (expressions.size() > 1) {
			expression = finishNode(
				factory.newArrayLiteralExpression(
					newNodeList(TextRange{pos, nodePos()}, expressions), false),
				pos);
		} else {
			expression = expressions.empty() ? nullptr : expressions[0];
		}
		Node* statement =
			finishNode(factory.newExpressionStatement(expression), pos);
		statements =
			newNodeList(TextRange{pos, nodePos()}, {statement});
		eof = parseExpectedToken(Kind::EndOfFile);
	}
	Node* node = finishNode(
		factory.newSourceFile(opts, std::string(sourceText), statements, eof), pos);
	SourceFile* result = node->as<SourceFile>();
	if (!result->Statements->nodes.empty()) {
		validateJsonValue(result,
		                result->Statements->nodes[0]->expression());
	}
	finishSourceFile(result, false);
	return result;
}

TextRange getErrorSpanForNode(std::string_view sourceText, Node* node) {
	int pos = node->pos();
	if (!nodeIsMissing(node)) {
		pos = skipTrivia(sourceText, pos);
	}
	return TextRange{pos, node->end()};
}

void Parser::validateJsonValue(SourceFile* sourceFile, Node* valueExpression) {
	if (valueExpression == nullptr) return;
	switch (valueExpression->kind) {
	case Kind::TrueKeyword:
	case Kind::FalseKeyword:
	case Kind::NullKeyword:
	case Kind::NumericLiteral:
		return;
	case Kind::StringLiteral:
		if (!isDoubleQuotedString(valueExpression)) {
			diagnostics.push_back(newDiagnostic(
				sourceFile,
				getErrorSpanForNode(sourceText, valueExpression),
				String_literal_with_double_quotes_expected));
		}
		return;
	case Kind::PrefixUnaryExpression:
		if (valueExpression->as<PrefixUnaryExpression>()->Operator !=
		        Kind::MinusToken ||
		    valueExpression->as<PrefixUnaryExpression>()->Operand->kind !=
		        Kind::NumericLiteral) {
			break; // not valid JSON syntax
		}
		return;
	case Kind::ObjectLiteralExpression:
		validateJsonObjectLiteral(
			sourceFile,
			static_cast<ObjectLiteralExpression*>(valueExpression));
		return;
	case Kind::ArrayLiteralExpression:
		for (Node* element : valueExpression->elements()) {
			validateJsonValue(sourceFile, element);
		}
		return;
	default:
		break;
	}
	diagnostics.push_back(newDiagnostic(
		sourceFile, getErrorSpanForNode(sourceText, valueExpression),
		Property_value_can_only_be_string_literal_numeric_literal_true_false_null_object_literal_or_array_literal));
}

bool isDoubleQuotedString(Node* node) {
	return isStringLiteral(node) &&
	       (node->as<StringLiteral>()->TokenFlags & TokenFlagsSingleQuote) == 0;
}

void Parser::validateJsonObjectLiteral(SourceFile* sourceFile,
                                       Node* node) {
	for (Node* element :
	     node->as<ObjectLiteralExpression>()->Properties->nodes) {
		if (element->kind != Kind::PropertyAssignment) {
			diagnostics.push_back(newDiagnostic(
				sourceFile, getErrorSpanForNode(sourceText, element),
				Property_assignment_expected));
			continue;
		}
		if (element->name() != nullptr &&
		    !isDoubleQuotedString(element->name())) {
			diagnostics.push_back(newDiagnostic(
				sourceFile,
				getErrorSpanForNode(sourceText, element->name()),
				String_literal_with_double_quotes_expected));
		}
		validateJsonValue(sourceFile,
		                  element->as<PropertyAssignment>()->Initializer);
	}
}

Node* parseIsolatedEntityName(std::string_view text) {
	Parser* p = getParser();
	p->initializeState({}, text, ScriptKind::JS);
	p->nextToken();
	Node* entityName = p->parseEntityName(true, false, nullptr);
	bool ok = p->token == Kind::EndOfFile && p->diagnostics.empty();
	putParser(p);
	return ok ? entityName : nullptr;
}

void Parser::initializeState(SourceFileParseOptions opts_,
                             std::string_view sourceText_, ScriptKind scriptKind_) {
	if (scriptKind_ == ScriptKind::Unknown) {
		TSC_UNREACHABLE("ScriptKind must be specified when parsing source file");
	}
	if (scanner == nullptr) {
		scanner = new Scanner();
	} else {
		scanner->reset();
	}
	opts = opts_;
	sourceText = sourceText_;
	scriptKind = scriptKind_;
	languageVariant = getLanguageVariant(scriptKind);
	switch (scriptKind) {
	case ScriptKind::JS:
	case ScriptKind::JSX:
		contextFlags = NodeFlagsJavaScriptFile;
		break;
	case ScriptKind::JSON:
		contextFlags = NodeFlagsJavaScriptFile | NodeFlagsJsonFile;
		break;
	default:
		contextFlags = NodeFlagsNone;
	}
	scanner->setText(sourceText);
	scanner->setOnError(
		[this](const DiagnosticMessage* m, int pos, int len,
		       const std::vector<std::string>& args) {
			scanError(m, pos, len, args);
		});
	scanner->setLanguageVariant(languageVariant);
}

void Parser::scanError(const DiagnosticMessage* message, int pos, int length,
                       const std::vector<std::string>& args) {
	parseErrorAtRange(TextRange{pos, pos + length}, message, args);
}

Diagnostic* Parser::parseErrorAt(int pos, int end,
                                 const DiagnosticMessage* message,
                                 const std::vector<std::string>& args) {
	return parseErrorAtRange(TextRange{pos, end}, message, args);
}

Diagnostic* Parser::parseErrorAtCurrentToken(
	const DiagnosticMessage* message,
	const std::vector<std::string>& args) {
	return parseErrorAtRange(scanner->tokenRange(), message, args);
}

Diagnostic* Parser::parseErrorAtRange(TextRange loc,
                                      const DiagnosticMessage* message,
                                      const std::vector<std::string>& args) {
	// Don't report another error if it would just be at the same location as
	// the last error
	Diagnostic* result = nullptr;
	if (diagnostics.empty() || diagnostics.back()->Pos() != loc.pos()) {
		result = newDetachedDiagnostic(loc, message, args);
		diagnostics.push_back(result);
	}
	hasParseError = true;
	return result;
}

ParserState Parser::mark() {
	return ParserState{scanner->mark(),
	                   contextFlags,
	                   static_cast<int>(diagnostics.size()),
	                   static_cast<int>(jsDiagnostics.size()),
	                   static_cast<int>(jsdocInfos.size()),
	                   static_cast<int>(reparsedClones.size()),
	                   statementHasAwaitIdentifier,
	                   hasParseError};
}

void Parser::rewind(ParserState state) {
	scanner->rewind(state.scannerState);
	token = scanner->token();
	contextFlags = state.contextFlags;
	diagnostics.resize(state.diagnosticsLen);
	jsDiagnostics.resize(state.jsDiagnosticsLen);
	jsdocInfos.resize(state.jsdocInfosLen);
	reparsedClones.resize(state.reparsedClonesLen);
	statementHasAwaitIdentifier = state.statementHasAwaitIdentifier;
	hasParseError = state.hasParseError;
}

bool Parser::lookAhead(const std::function<bool(Parser*)>& callback) {
	ParserState state = mark();
	bool result = callback(this);
	rewind(state);
	return result;
}

Kind Parser::nextToken() {
	// if the keyword had an escape
	if (isKeyword(token) &&
	    (scanner->hasUnicodeEscape() || scanner->hasExtendedUnicodeEscape())) {
		// issue a parse error for the escape
		parseErrorAtCurrentToken(Keywords_cannot_contain_escape_characters);
	}
	token = scanner->scan();
	return token;
}

Kind Parser::nextTokenWithoutCheck() {
	token = scanner->scan();
	return token;
}

Kind Parser::nextTokenJSDoc() {
	token = scanner->scanJSDocToken();
	return token;
}

Kind Parser::nextJSDocCommentTextToken(bool inBackticks) {
	token = scanner->scanJSDocCommentTextToken(inBackticks);
	return token;
}

int Parser::nodePos() { return scanner->tokenFullStart(); }

bool Parser::hasPrecedingLineBreak() {
	return scanner->hasPrecedingLineBreak();
}

JSDocScannerInfo Parser::jsdocScannerInfo() {
	if (!scanner->hasPrecedingJSDocComment()) return 0;
	JSDocScannerInfo info = JSDocScannerInfoHasJSDoc;
	if (scanner->hasPrecedingJSDocWithDeprecatedTag()) {
		info |= JSDocScannerInfoHasDeprecated;
	}
	if (scanner->hasPrecedingJSDocWithSeeOrLink()) {
		info |= JSDocScannerInfoHasSeeOrLink;
	}
	return info;
}

SourceFile* Parser::parseSourceFileWorker() {
	bool isDeclarationFile = tspath::isDeclarationFileName(opts.FileName);
	if (isDeclarationFile) {
		contextFlags |= NodeFlagsAmbient;
	}
	int pos = nodePos();
	std::vector<Node*> statements =
		parseListIndex(PCSourceElements, &Parser::parseToplevelStatement);
	int end = nodePos();
	JSDocScannerInfo endJSDoc = jsdocScannerInfo();
	Node* eof = parseTokenNode();
	withJSDoc(eof, endJSDoc);
	if (eof->kind != Kind::EndOfFile) {
		TSC_UNREACHABLE("Expected end of file token from scanner.");
	}
	if (!reparseList.empty()) {
		statements.insert(statements.end(), reparseList.begin(),
		                  reparseList.end());
		reparseList.clear();
	}
	Node* node = finishNode(
		factory.newSourceFile(opts, std::string(sourceText),
		                      newNodeList(TextRange{pos, end}, statements), eof),
		pos);
	SourceFile* result = node->as<SourceFile>();
	finishSourceFile(result, isDeclarationFile);
	if (!result->IsDeclarationFile && result->ExternalModuleIndicator &&
	    !possibleAwaitSpans.empty()) {
		Node* reparse = finishNode(reparseTopLevelAwait(result), pos);
		if (node != reparse) {
			result = reparse->as<SourceFile>();
			finishSourceFile(result, isDeclarationFile);
		}
	}
	collectExternalModuleReferences(result);
	if (isInJSFile(node)) {
		result->jsDiagnostics = attachFileToDiagnostics(jsDiagnostics, result);
	}
	return result;
}

void Parser::finishSourceFile(SourceFile* result, bool isDeclarationFile) {
	result->CommentDirectives = scanner->getCommentDirectives();
	result->Pragmas = getCommentPragmas(&factory, sourceText);
	processPragmasIntoFields(result);
	result->diagnostics = attachFileToDiagnostics(diagnostics, result);
	result->jsdocDiagnostics =
		attachFileToDiagnostics(jsdocDiagnostics, result);
	result->IsDeclarationFile = isDeclarationFile;
	result->LanguageVariant = languageVariant;
	result->ScriptKind = scriptKind;
	result->flags |= sourceFlags;
	result->NodeCount = factory.nodeCount();
	result->TextCount = factory.textCount();
	result->IdentifierCount = identifierCount;
	result->jsdocCache = createJSDocCache();
	// For non-JS files, enable lazy JSDoc parsing on demand
	if (!isJavaScript()) {
		result->setHasLazyJSDoc(true);
	}
	std::sort(reparsedClones.begin(), reparsedClones.end(),
	          [](Node* a, Node* b) { return a->pos() < b->pos(); });
	result->ReparsedClones = reparsedClones;
	setExternalModuleIndicator(result, opts.ExternalModuleIndicatorOptions);
}

std::unordered_map<Node*, std::vector<Node*>> Parser::createJSDocCache() {
	if (jsdocInfos.empty()) return {};
	std::unordered_map<Node*, std::vector<Node*>> result;
	result.reserve(jsdocInfos.size());
	for (auto& info : jsdocInfos) {
		result[info.parent] = info.jsDocs;
	}
	return result;
}

Node* Parser::parseToplevelStatement(int i) {
	statementHasAwaitIdentifier = false;
	Node* statement = parseStatement();
	// Reparsed nodes produced while parsing this statement are inserted into
	// the statement list before it — account for them in the index.
	i += static_cast<int>(reparseList.size());
	if (statementHasAwaitIdentifier &&
	    (statement->flags & NodeFlagsAwaitContext) == 0) {
		if (possibleAwaitSpans.empty() ||
		    possibleAwaitSpans.back() != i) {
			possibleAwaitSpans.push_back(i);
			possibleAwaitSpans.push_back(i + 1);
		} else {
			possibleAwaitSpans.back() = i + 1;
		}
	}
	return statement;
}

Node* Parser::reparseTopLevelAwait(SourceFile* sourceFile) {
	if (possibleAwaitSpans.size() % 2 == 1) {
		TSC_UNREACHABLE(
			"possibleAwaitSpans malformed: odd number of indices, not "
			"paired into spans.");
	}
	std::vector<Node*> statements;
	std::vector<Diagnostic*> savedParseDiagnostics = diagnostics;
	diagnostics.clear();

	int afterAwaitStatement = 0;
	for (size_t i = 0; i < possibleAwaitSpans.size(); i += 2) {
		int nextAwaitStatement = possibleAwaitSpans[i];
		Node* prevStatement =
			sourceFile->Statements->nodes[afterAwaitStatement];
		Node* nextStatement =
			sourceFile->Statements->nodes[nextAwaitStatement];
		statements.insert(
			statements.end(),
			sourceFile->Statements->nodes.begin() + afterAwaitStatement,
			sourceFile->Statements->nodes.begin() + nextAwaitStatement);

		// append all diagnostics associated with the copied range
		auto diagnosticStart = std::find_if(
			savedParseDiagnostics.begin(), savedParseDiagnostics.end(),
			[&](Diagnostic* d) { return d->Pos() >= prevStatement->pos(); });
		if (diagnosticStart != savedParseDiagnostics.end()) {
			auto diagnosticEnd =
				std::find_if(diagnosticStart, savedParseDiagnostics.end(),
			                 [&](Diagnostic* d) {
				                 return d->Pos() >= nextStatement->pos();
			                 });
			diagnostics.insert(diagnostics.end(), diagnosticStart,
			                   diagnosticEnd);
		}

		ParserState state = mark();
		contextFlags |= NodeFlagsAwaitContext;
		scanner->resetPos(nextStatement->pos());
		nextToken();

		afterAwaitStatement = possibleAwaitSpans[i + 1];
		while (token != Kind::EndOfFile) {
			int startPos = scanner->tokenFullStart();
			Node* statement = parseStatement();
			statements.push_back(statement);
			if (startPos == scanner->tokenFullStart()) {
				nextToken();
			}
			if (afterAwaitStatement <
			    static_cast<int>(sourceFile->Statements->nodes.size())) {
				Node* lastAwaitStatement =
					sourceFile->Statements->nodes[afterAwaitStatement - 1];
				if (statement->end() == lastAwaitStatement->end()) {
					break;
				}
				if (statement->end() > lastAwaitStatement->end()) {
					i += 2;
					if (i < possibleAwaitSpans.size()) {
						afterAwaitStatement = possibleAwaitSpans[i + 1];
					} else {
						afterAwaitStatement = static_cast<int>(
							sourceFile->Statements->nodes.size());
					}
				}
			}
		}

		state.diagnosticsLen = static_cast<int>(diagnostics.size());
		rewind(state);
	}

	if (afterAwaitStatement <
	    static_cast<int>(sourceFile->Statements->nodes.size())) {
		Node* prevStatement =
			sourceFile->Statements->nodes[afterAwaitStatement];
		statements.insert(
			statements.end(),
			sourceFile->Statements->nodes.begin() + afterAwaitStatement,
			sourceFile->Statements->nodes.end());
		auto diagnosticStart = std::find_if(
			savedParseDiagnostics.begin(), savedParseDiagnostics.end(),
			[&](Diagnostic* d) { return d->Pos() >= prevStatement->pos(); });
		diagnostics.insert(diagnostics.end(), diagnosticStart,
		                   savedParseDiagnostics.end());
	}

	Node* result = factory.newSourceFile(
		sourceFile->parseOptions, std::string(sourceText),
		newNodeList(sourceFile->Statements->loc, statements),
		sourceFile->EndOfFileToken);
	for (Node* s : statements) {
		s->parent = result;
	}
	return result;
}

std::vector<Node*> Parser::parseListIndex(
	ParsingContext kind, Node* (Parser::*parseElement)(int)) {
	ParsingContexts saveParsingContexts = parsingContexts;
	parsingContexts |= (1u << kind);
	std::vector<Node*> outerReparseList = std::move(reparseList);
	reparseList.clear();
	std::vector<Node*> list;
	list.reserve(16);
	for (int i = 0; !isListTerminator(kind); i++) {
		if (isListElement(kind, false)) {
			Node* elt = (this->*parseElement)(static_cast<int>(list.size()));
			if (!reparseList.empty()) {
				for (Node* e : reparseList) {
					// Propagate @typedef type alias declarations outwards.
					if ((isJSTypeAliasDeclaration(e) ||
					     isJSImportDeclaration(e)) &&
					    kind != PCSourceElements && kind != PCBlockStatements) {
						outerReparseList.push_back(e);
					} else {
						list.push_back(e);
					}
				}
				reparseList.clear();
			}
			list.push_back(elt);
			continue;
		}
		if (abortParsingListOrMoveToNextToken(kind)) {
			break;
		}
	}
	reparseList = std::move(outerReparseList);
	parsingContexts = saveParsingContexts;
	return list;
}

Node* Parser::parseListElementTrampoline(int) {
	return (this->*listElementFn)();
}

NodeList* Parser::parseList(ParsingContext kind,
                            Node* (Parser::*parseElement)()) {
	int pos = nodePos();
	// Save/restore so nested parseList calls (inside element parsers) leave
	// this list's element function intact for the trampoline.
	Node* (Parser::*saveListElementFn)() = listElementFn;
	listElementFn = parseElement;
	std::vector<Node*> nodes =
		parseListIndex(kind, &Parser::parseListElementTrampoline);
	listElementFn = saveListElementFn;
	return newNodeList(TextRange{pos, nodePos()}, nodes);
}

NodeList* Parser::parseDelimitedList(ParsingContext kind,
                                     Node* (Parser::*parseElement)()) {
	int pos = nodePos();
	ParsingContexts saveParsingContexts = parsingContexts;
	parsingContexts |= (1u << kind);
	std::vector<Node*> list;
	list.reserve(16);
	for (;;) {
		if (isListElement(kind, false)) {
			int startPos = nodePos();
			Node* element = (this->*parseElement)();
			if (element == nullptr) {
				parsingContexts = saveParsingContexts;
				return nullptr;
			}
			list.push_back(element);
			if (parseOptional(Kind::CommaToken)) {
				continue;
			}
			if (isListTerminator(kind)) {
				break;
			}
			if (token != Kind::CommaToken && kind == PCEnumMembers) {
				parseErrorAtCurrentToken(
					An_enum_member_name_must_be_followed_by_a_or);
			} else {
				parseExpected(Kind::CommaToken);
			}
			if ((kind == PCObjectLiteralMembers || kind == PCImportAttributes) &&
			    token == Kind::SemicolonToken && !hasPrecedingLineBreak()) {
				nextToken();
			}
			if (startPos == nodePos()) {
				nextToken();
			}
			continue;
		}
		if (isListTerminator(kind)) {
			break;
		}
		if (abortParsingListOrMoveToNextToken(kind)) {
			break;
		}
	}
	parsingContexts = saveParsingContexts;
	return newNodeList(TextRange{pos, nodePos()}, list);
}

NodeList* Parser::parseBracketedList(ParsingContext kind,
                                     Node* (Parser::*parseElement)(),
                                     Kind opening, Kind closing) {
	if (parseExpected(opening)) {
		NodeList* result = parseDelimitedList(kind, parseElement);
		parseExpected(closing);
		return result;
	}
	return createMissingList();
}

NodeList* Parser::parseEmptyNodeList() {
	return newNodeList(TextRange{nodePos(), nodePos()}, {});
}

NodeList* Parser::createMissingList() {
	NodeList* result = parseEmptyNodeList();
	result->nodes.reserve(1); // sentinel: cap 1 = missing
	return result;
}

bool Parser::abortParsingListOrMoveToNextToken(ParsingContext kind) {
	parsingContextErrors(kind);
	if (isInSomeParsingContext()) {
		return true;
	}
	nextToken();
	return false;
}

bool Parser::isInSomeParsingContext() {
	for (int kind = 0; kind < PCCount; kind++) {
		if ((parsingContexts & (1u << kind)) != 0) {
			if (isListElement(kind, true) || isListTerminator(kind)) {
				return true;
			}
		}
	}
	return false;
}

void Parser::parsingContextErrors(ParsingContext context) {
	switch (context) {
	case PCSourceElements:
		if (token == Kind::DefaultKeyword) {
			parseErrorAtCurrentToken(X_0_expected, {"export"});
		} else {
			parseErrorAtCurrentToken(Declaration_or_statement_expected);
		}
		break;
	case PCBlockStatements:
		parseErrorAtCurrentToken(Declaration_or_statement_expected);
		break;
	case PCSwitchClauses:
		parseErrorAtCurrentToken(X_case_or_default_expected);
		break;
	case PCSwitchClauseStatements:
		parseErrorAtCurrentToken(Statement_expected);
		break;
	case PCRestProperties:
	case PCTypeMembers:
		parseErrorAtCurrentToken(Property_or_signature_expected);
		break;
	case PCClassMembers:
		parseErrorAtCurrentToken(
			Unexpected_token_A_constructor_method_accessor_or_property_was_expected);
		break;
	case PCEnumMembers:
		parseErrorAtCurrentToken(Enum_member_expected);
		break;
	case PCHeritageClauseElement:
		parseErrorAtCurrentToken(Expression_expected);
		break;
	case PCVariableDeclarations:
		if (isKeyword(token)) {
			parseErrorAtCurrentToken(
				X_0_is_not_allowed_as_a_variable_declaration_name,
				{std::string(tokenToString(token))});
		} else {
			parseErrorAtCurrentToken(Variable_declaration_expected);
		}
		break;
	case PCObjectBindingElements:
		parseErrorAtCurrentToken(Property_destructuring_pattern_expected);
		break;
	case PCArrayBindingElements:
		parseErrorAtCurrentToken(
			Array_element_destructuring_pattern_expected);
		break;
	case PCArgumentExpressions:
		parseErrorAtCurrentToken(Argument_expression_expected);
		break;
	case PCObjectLiteralMembers:
		parseErrorAtCurrentToken(Property_assignment_expected);
		break;
	case PCArrayLiteralMembers:
		parseErrorAtCurrentToken(Expression_or_comma_expected);
		break;
	case PCJSDocParameters:
		parseErrorAtCurrentToken(Parameter_declaration_expected);
		break;
	case PCParameters:
		if (isKeyword(token)) {
			parseErrorAtCurrentToken(
				X_0_is_not_allowed_as_a_parameter_name,
				{std::string(tokenToString(token))});
		} else {
			parseErrorAtCurrentToken(Parameter_declaration_expected);
		}
		break;
	case PCTypeParameters:
		parseErrorAtCurrentToken(Type_parameter_declaration_expected);
		break;
	case PCTypeArguments:
		parseErrorAtCurrentToken(Type_argument_expected);
		break;
	case PCTupleElementTypes:
		parseErrorAtCurrentToken(Type_expected);
		break;
	case PCHeritageClauses:
		parseErrorAtCurrentToken(Unexpected_token_expected);
		break;
	case PCImportOrExportSpecifiers:
		if (token == Kind::FromKeyword) {
			parseErrorAtCurrentToken(X_0_expected, {"}"});
		} else {
			parseErrorAtCurrentToken(Identifier_expected);
		}
		break;
	case PCJsxAttributes:
	case PCJsxChildren:
	case PCJSDocComment:
		parseErrorAtCurrentToken(Identifier_expected);
		break;
	case PCImportAttributes:
		parseErrorAtCurrentToken(Identifier_or_string_literal_expected);
		break;
	default:
		TSC_UNREACHABLE("Unhandled case in parsingContextErrors");
	}
}

bool Parser::isListElement(ParsingContext parsingContext,
                           bool inErrorRecovery) {
	switch (parsingContext) {
	case PCSourceElements:
	case PCBlockStatements:
	case PCSwitchClauseStatements:
		// In error recovery, don't treat ';' as an empty statement.
		return !(token == Kind::SemicolonToken && inErrorRecovery) &&
		       isStartOfStatement();
	case PCSwitchClauses:
		return token == Kind::CaseKeyword || token == Kind::DefaultKeyword;
	case PCTypeMembers:
		return lookAhead(&Parser::scanTypeMemberStart);
	case PCClassMembers:
		return lookAhead(&Parser::scanClassMemberStart) ||
		       (token == Kind::SemicolonToken && !inErrorRecovery);
	case PCEnumMembers:
		return token == Kind::OpenBracketToken || isLiteralPropertyName();
	case PCObjectLiteralMembers:
		switch (token) {
		case Kind::OpenBracketToken:
		case Kind::AsteriskToken:
		case Kind::DotDotDotToken:
		case Kind::DotToken:
			return true;
		default:
			return isLiteralPropertyName();
		}
	case PCRestProperties:
		return isLiteralPropertyName();
	case PCObjectBindingElements:
		return token == Kind::OpenBracketToken ||
		       token == Kind::DotDotDotToken || isLiteralPropertyName();
	case PCImportAttributes:
		return isImportAttributeName();
	case PCHeritageClauseElement:
		if (token == Kind::OpenBraceToken) {
			return isValidHeritageClauseObjectLiteral();
		}
		if (!inErrorRecovery) {
			return isStartOfLeftHandSideExpression() &&
			       !isHeritageClauseExtendsOrImplementsKeyword();
		}
		return isIdentifier() &&
		       !isHeritageClauseExtendsOrImplementsKeyword();
	case PCVariableDeclarations:
		return isBindingIdentifierOrPrivateIdentifierOrPattern();
	case PCArrayBindingElements:
		return token == Kind::CommaToken || token == Kind::DotDotDotToken ||
		       isBindingIdentifierOrPrivateIdentifierOrPattern();
	case PCTypeParameters:
		return token == Kind::InKeyword || token == Kind::ConstKeyword ||
		       isIdentifier();
	case PCArrayLiteralMembers:
		if (token == Kind::CommaToken || token == Kind::DotToken) {
			return true;
		}
		[[fallthrough]];
	case PCArgumentExpressions:
		return token == Kind::DotDotDotToken || isStartOfExpression();
	case PCParameters:
		return isStartOfParameter(false);
	case PCJSDocParameters:
		return isStartOfParameter(true);
	case PCTypeArguments:
	case PCTupleElementTypes:
		return token == Kind::CommaToken || isStartOfType(false);
	case PCHeritageClauses:
		return isHeritageClause();
	case PCImportOrExportSpecifiers:
		// Bail out on `from "mod"` — better error message.
		if (token == Kind::FromKeyword &&
		    lookAhead(&Parser::nextTokenIsTokenStringLiteral)) {
			return false;
		}
		if (token == Kind::StringLiteral) {
			return true;
		}
		return tokenIsIdentifierOrKeyword(token);
	case PCJsxAttributes:
		return tokenIsIdentifierOrKeyword(token) ||
		       token == Kind::OpenBraceToken;
	case PCJsxChildren:
		return true;
	case PCJSDocComment:
		return true;
	}
	TSC_UNREACHABLE("Unhandled case in isListElement");
}

bool Parser::isListTerminator(ParsingContext kind) {
	if (token == Kind::EndOfFile) {
		return true;
	}
	switch (kind) {
	case PCBlockStatements:
	case PCSwitchClauses:
	case PCTypeMembers:
	case PCClassMembers:
	case PCEnumMembers:
	case PCObjectLiteralMembers:
	case PCObjectBindingElements:
	case PCImportOrExportSpecifiers:
	case PCImportAttributes:
		return token == Kind::CloseBraceToken;
	case PCSwitchClauseStatements:
		return token == Kind::CloseBraceToken || token == Kind::CaseKeyword ||
		       token == Kind::DefaultKeyword;
	case PCHeritageClauseElement:
		return token == Kind::OpenBraceToken || token == Kind::ExtendsKeyword ||
		       token == Kind::ImplementsKeyword;
	case PCVariableDeclarations:
		return canParseSemicolon() || token == Kind::InKeyword ||
		       token == Kind::OfKeyword ||
		       token == Kind::EqualsGreaterThanToken;
	case PCTypeParameters:
		return token == Kind::GreaterThanToken ||
		       token == Kind::OpenParenToken || token == Kind::OpenBraceToken ||
		       token == Kind::ExtendsKeyword ||
		       token == Kind::ImplementsKeyword;
	case PCArgumentExpressions:
		return token == Kind::CloseParenToken ||
		       token == Kind::SemicolonToken;
	case PCArrayLiteralMembers:
	case PCTupleElementTypes:
	case PCArrayBindingElements:
		return token == Kind::CloseBracketToken;
	case PCJSDocParameters:
	case PCParameters:
	case PCRestProperties:
		return token == Kind::CloseParenToken ||
		       token == Kind::CloseBracketToken;
	case PCTypeArguments:
		return token != Kind::CommaToken;
	case PCHeritageClauses:
		return token == Kind::OpenBraceToken || token == Kind::CloseBraceToken;
	case PCJsxAttributes:
		return token == Kind::GreaterThanToken || token == Kind::SlashToken;
	case PCJsxChildren:
		return token == Kind::LessThanToken &&
		       lookAhead(&Parser::nextTokenIsSlash);
	}
	return false;
}

bool Parser::parseExpectedJSDoc(Kind kind) {
	if (token == kind) {
		nextTokenJSDoc();
		return true;
	}
	if (!isKeywordOrPunctuation(kind)) {
		TSC_UNREACHABLE(
			"Invalid JSDoc kind: expected keyword or punctuation");
	}
	parseErrorAtCurrentToken(X_0_expected, {std::string(tokenToString(kind))});
	return false;
}

void Parser::parseExpectedMatchingBrackets(Kind openKind, Kind closeKind,
                                           bool openParsed, int openPosition) {
	if (token == closeKind) {
		nextToken();
		return;
	}
	Diagnostic* lastError = parseErrorAtCurrentToken(
		X_0_expected, {std::string(tokenToString(closeKind))});
	if (!openParsed) {
		return;
	}
	if (lastError != nullptr) {
		Diagnostic* related = newDetachedDiagnostic(
			TextRange{openPosition, openPosition},
			The_parser_expected_to_find_a_1_to_match_the_0_token_here,
			{std::string(tokenToString(openKind)),
	         std::string(tokenToString(closeKind))});
		lastError->messageChain.push_back(related);
	}
}

bool Parser::parseOptional(Kind token_) {
	if (token == token_) {
		nextToken();
		return true;
	}
	return false;
}

bool Parser::parseExpected(Kind kind) {
	return parseExpectedWithDiagnostic(kind, nullptr, true);
}

bool Parser::parseExpectedWithoutAdvancing(Kind kind) {
	return parseExpectedWithDiagnostic(kind, nullptr, false);
}

bool Parser::parseExpectedWithDiagnostic(Kind kind,
                                         const DiagnosticMessage* message,
                                         bool shouldAdvance) {
	if (token == kind) {
		if (shouldAdvance) {
			nextToken();
		}
		return true;
	}
	if (message != nullptr) {
		parseErrorAtCurrentToken(message);
	} else {
		parseErrorAtCurrentToken(X_0_expected,
		                         {std::string(tokenToString(kind))});
	}
	return false;
}

Node* Parser::parseTokenNode() {
	int pos = nodePos();
	Kind kind = token;
	nextToken();
	return finishNode(factory.newToken(kind), pos);
}

Node* Parser::parseExpectedToken(Kind kind) {
	Node* t = parseOptionalToken(kind);
	if (t == nullptr) {
		parseErrorAtCurrentToken(X_0_expected,
		                         {std::string(tokenToString(kind))});
		t = finishNode(factory.newToken(kind), nodePos());
	}
	return t;
}

Node* Parser::parseOptionalToken(Kind kind) {
	if (token == kind) {
		return parseTokenNode();
	}
	return nullptr;
}

Node* Parser::parseExpectedTokenJSDoc(Kind kind) {
	Node* optional = parseOptionalTokenJSDoc(kind);
	if (optional == nullptr) {
		if (!isKeywordOrPunctuation(kind)) {
			TSC_UNREACHABLE("expected keyword or punctuation");
		}
		parseErrorAtCurrentToken(X_0_expected,
		                         {std::string(tokenToString(kind))});
		optional = finishNode(factory.newToken(kind), nodePos());
	}
	return optional;
}

Node* Parser::parseOptionalTokenJSDoc(Kind kind) {
	if (token == kind) {
		return parseTokenNode();
	}
	return nullptr;
}

Node* Parser::parseStatement() {
	switch (token) {
	case Kind::SemicolonToken:
		return parseEmptyStatement();
	case Kind::OpenBraceToken:
		return parseBlock(false, nullptr);
	case Kind::VarKeyword:
		return parseVariableStatement(nodePos(), jsdocScannerInfo(),
		                              nullptr);
	case Kind::LetKeyword:
		if (isLetDeclaration()) {
			return parseVariableStatement(nodePos(), jsdocScannerInfo(),
			                              nullptr);
		}
		break;
	case Kind::AwaitKeyword:
		if (isAwaitUsingDeclaration()) {
			return parseVariableStatement(nodePos(), jsdocScannerInfo(),
			                              nullptr);
		}
		break;
	case Kind::UsingKeyword:
		if (isUsingDeclaration()) {
			return parseVariableStatement(nodePos(), jsdocScannerInfo(),
			                              nullptr);
		}
		break;
	case Kind::FunctionKeyword:
		return parseFunctionDeclaration(nodePos(), jsdocScannerInfo(),
		                                nullptr);
	case Kind::ClassKeyword:
		return parseClassDeclaration(nodePos(), jsdocScannerInfo(), nullptr);
	case Kind::IfKeyword:
		return parseIfStatement();
	case Kind::DoKeyword:
		return parseDoStatement();
	case Kind::WhileKeyword:
		return parseWhileStatement();
	case Kind::ForKeyword:
		return parseForOrForInOrForOfStatement();
	case Kind::ContinueKeyword:
		return parseContinueStatement();
	case Kind::BreakKeyword:
		return parseBreakStatement();
	case Kind::ReturnKeyword:
		return parseReturnStatement();
	case Kind::WithKeyword:
		return parseWithStatement();
	case Kind::SwitchKeyword:
		return parseSwitchStatement();
	case Kind::ThrowKeyword:
		return parseThrowStatement();
	case Kind::TryKeyword:
	case Kind::CatchKeyword:
	case Kind::FinallyKeyword:
		return parseTryStatement();
	case Kind::DebuggerKeyword:
		return parseDebuggerStatement();
	case Kind::AtToken:
		return parseDeclaration();
	case Kind::AsyncKeyword:
	case Kind::InterfaceKeyword:
	case Kind::TypeKeyword:
	case Kind::ModuleKeyword:
	case Kind::NamespaceKeyword:
	case Kind::DeclareKeyword:
	case Kind::ConstKeyword:
	case Kind::EnumKeyword:
	case Kind::ExportKeyword:
	case Kind::ImportKeyword:
	case Kind::PrivateKeyword:
	case Kind::ProtectedKeyword:
	case Kind::PublicKeyword:
	case Kind::AbstractKeyword:
	case Kind::AccessorKeyword:
	case Kind::StaticKeyword:
	case Kind::ReadonlyKeyword:
	case Kind::GlobalKeyword:
		if (isStartOfDeclaration()) {
			return parseDeclaration();
		}
		break;
	default:
		break;
	}
	return parseExpressionOrLabeledStatement();
}

static bool isDeclareModifier(Node* modifier) {
	return modifier->kind == Kind::DeclareKeyword;
}

Node* Parser::parseDeclaration() {
	// `parseListElement` attempted to get the reused node at this position,
	// but the ambient context flag was not yet set, so the node appeared
	// not reusable in that context.
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	ModifierList* modifiers =
		parseModifiersEx(true, false, false);
	bool isAmbient =
		modifiers != nullptr &&
		std::any_of(modifiers->nodes.begin(), modifiers->nodes.end(),
		            isDeclareModifier);
	if (isAmbient) {
		// !!! incremental parsing
		for (Node* m : modifiers->nodes) {
			m->flags |= NodeFlagsAmbient;
		}
		NodeFlags saveContextFlags = contextFlags;
		setContextFlags(NodeFlagsAmbient, true);
		Node* result = parseDeclarationWorker(pos, jsdoc, modifiers);
		contextFlags = saveContextFlags;
		return result;
	}
	return parseDeclarationWorker(pos, jsdoc, modifiers);
}

Node* Parser::parseDeclarationWorker(int pos, JSDocScannerInfo jsdoc,
                                     ModifierList* modifiers) {
	switch (token) {
	case Kind::VarKeyword:
	case Kind::LetKeyword:
	case Kind::ConstKeyword:
	case Kind::UsingKeyword:
		return parseVariableStatement(pos, jsdoc, modifiers);
	case Kind::AwaitKeyword:
		if (isAwaitUsingDeclaration()) {
			return parseVariableStatement(pos, jsdoc, modifiers);
		}
		break;
	case Kind::FunctionKeyword:
		return parseFunctionDeclaration(pos, jsdoc, modifiers);
	case Kind::ClassKeyword:
		return parseClassDeclaration(pos, jsdoc, modifiers);
	case Kind::InterfaceKeyword:
		return parseInterfaceDeclaration(pos, jsdoc, modifiers);
	case Kind::TypeKeyword:
		return parseTypeAliasDeclaration(pos, jsdoc, modifiers);
	case Kind::EnumKeyword:
		return parseEnumDeclaration(pos, jsdoc, modifiers);
	case Kind::GlobalKeyword:
	case Kind::ModuleKeyword:
	case Kind::NamespaceKeyword:
		return parseModuleDeclaration(pos, jsdoc, modifiers);
	case Kind::ImportKeyword:
		return parseImportDeclarationOrImportEqualsDeclaration(pos, jsdoc,
		                                                       modifiers);
	case Kind::ExportKeyword:
		nextToken();
		switch (token) {
		case Kind::DefaultKeyword:
		case Kind::EqualsToken:
			return parseExportAssignment(pos, jsdoc, modifiers);
		case Kind::AsKeyword:
			return parseNamespaceExportDeclaration(pos, jsdoc, modifiers);
		default:
			return parseExportDeclaration(pos, jsdoc, modifiers);
		}
	default:
		break;
	}
	if (modifiers != nullptr) {
		// Decorators/modifiers without a following declaration: recover with an
		// incomplete declaration.
		parseErrorAt(nodePos(), nodePos(), Declaration_expected);
		return finishNode(factory.newMissingDeclaration(modifiers), pos);
	}
	TSC_UNREACHABLE("Unhandled case in parseDeclarationWorker");
}

bool Parser::isLetDeclaration() {
	return lookAhead(&Parser::nextTokenIsBindingIdentifierOrStartOfDestructuring);
}

bool Parser::nextTokenIsBindingIdentifierOrStartOfDestructuring() {
	nextToken();
	return isBindingIdentifier() || token == Kind::OpenBraceToken ||
	       token == Kind::OpenBracketToken;
}

Node* Parser::parseBlock(bool ignoreMissingOpenBrace,
                         const DiagnosticMessage* diagnosticMessage) {
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	int openBracePosition = scanner->tokenStart();
	bool openBraceParsed = parseExpectedWithDiagnostic(
		Kind::OpenBraceToken, diagnosticMessage, true);
	bool multiline = false;
	if (openBraceParsed || ignoreMissingOpenBrace) {
		multiline = hasPrecedingLineBreak();
		NodeList* statements = parseList(PCBlockStatements, &Parser::parseStatement);
		parseExpectedMatchingBrackets(Kind::OpenBraceToken,
		                              Kind::CloseBraceToken, openBraceParsed,
		                              openBracePosition);
		Node* result = finishNode(factory.newBlock(statements, multiline), pos);
		withJSDoc(result, jsdoc);
		if (token == Kind::EqualsToken) {
			parseErrorAtCurrentToken(
				Declaration_or_statement_expected_This_follows_a_block_of_statements_so_if_you_intended_to_write_a_destructuring_assignment_you_might_need_to_wrap_the_whole_assignment_in_parentheses);
			nextToken();
		}
		return result;
	}
	Node* result =
		finishNode(factory.newBlock(createMissingList(), multiline), pos);
	withJSDoc(result, jsdoc);
	return result;
}

Node* Parser::parseEmptyStatement() {
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	parseExpected(Kind::SemicolonToken);
	Node* result = finishNode(factory.newEmptyStatement(), pos);
	withJSDoc(result, jsdoc);
	return result;
}

Node* Parser::parseIfStatement() {
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	parseExpected(Kind::IfKeyword);
	int openParenPosition = scanner->tokenStart();
	bool openParenParsed = parseExpected(Kind::OpenParenToken);
	Node* expression = parseExpressionAllowIn();
	parseExpectedMatchingBrackets(Kind::OpenParenToken, Kind::CloseParenToken,
	                              openParenParsed, openParenPosition);
	Node* thenStatement = parseStatement();
	Node* elseStatement = nullptr;
	if (parseOptional(Kind::ElseKeyword)) {
		elseStatement = parseStatement();
	}
	Node* result = finishNode(
		factory.newIfStatement(expression, thenStatement, elseStatement), pos);
	withJSDoc(result, jsdoc);
	return result;
}

Node* Parser::parseDoStatement() {
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	parseExpected(Kind::DoKeyword);
	Node* statement = parseStatement();
	parseExpected(Kind::WhileKeyword);
	int openParenPosition = scanner->tokenStart();
	bool openParenParsed = parseExpected(Kind::OpenParenToken);
	Node* expression = parseExpressionAllowIn();
	parseExpectedMatchingBrackets(Kind::OpenParenToken, Kind::CloseParenToken,
	                              openParenParsed, openParenPosition);
	parseOptional(Kind::SemicolonToken);
	Node* result =
		finishNode(factory.newDoStatement(statement, expression), pos);
	withJSDoc(result, jsdoc);
	return result;
}

Node* Parser::parseWhileStatement() {
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	parseExpected(Kind::WhileKeyword);
	int openParenPosition = scanner->tokenStart();
	bool openParenParsed = parseExpected(Kind::OpenParenToken);
	Node* expression = parseExpressionAllowIn();
	parseExpectedMatchingBrackets(Kind::OpenParenToken, Kind::CloseParenToken,
	                              openParenParsed, openParenPosition);
	Node* statement = parseStatement();
	Node* result =
		finishNode(factory.newWhileStatement(expression, statement), pos);
	withJSDoc(result, jsdoc);
	return result;
}

Node* Parser::parseForOrForInOrForOfStatement() {
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	parseExpected(Kind::ForKeyword);
	Node* awaitToken = parseOptionalToken(Kind::AwaitKeyword);
	parseExpected(Kind::OpenParenToken);
	Node* initializer = nullptr;
	if (token != Kind::SemicolonToken) {
		if (token == Kind::VarKeyword || token == Kind::LetKeyword ||
		    token == Kind::ConstKeyword ||
		    (token == Kind::UsingKeyword &&
		     lookAhead(
		         &Parser::nextTokenIsBindingIdentifierOrStartOfDestructuringOnSameLineDisallowOf)) ||
		    (token == Kind::AwaitKeyword &&
		     lookAhead(
		         &Parser::nextIsUsingKeywordThenBindingIdentifierOrStartOfObjectDestructuringOnSameLine))) {
			initializer = parseVariableDeclarationList(true);
		} else {
			initializer = doInContext(NodeFlagsDisallowInContext, true,
			                          &Parser::parseExpression);
		}
	}
	Node* result;
	if ((awaitToken != nullptr && parseExpected(Kind::OfKeyword)) ||
	    (awaitToken == nullptr && parseOptional(Kind::OfKeyword))) {
		Node* expression =
			doInContext(NodeFlagsDisallowInContext, false,
			            &Parser::parseAssignmentExpressionOrHigher);
		parseExpected(Kind::CloseParenToken);
		result = factory.newForInOrOfStatement(
			Kind::ForOfStatement, awaitToken, initializer, expression,
			parseStatement());
	} else if (parseOptional(Kind::InKeyword)) {
		Node* expression = parseExpressionAllowIn();
		parseExpected(Kind::CloseParenToken);
		result = factory.newForInOrOfStatement(
			Kind::ForInStatement, nullptr, initializer, expression,
			parseStatement());
	} else {
		parseExpected(Kind::SemicolonToken);
		Node* condition = nullptr;
		if (token != Kind::SemicolonToken &&
		    token != Kind::CloseParenToken) {
			condition = parseExpressionAllowIn();
		}
		parseExpected(Kind::SemicolonToken);
		Node* incrementor = nullptr;
		if (token != Kind::CloseParenToken) {
			incrementor = parseExpressionAllowIn();
		}
		parseExpected(Kind::CloseParenToken);
		result = factory.newForStatement(initializer, condition, incrementor,
		                                 parseStatement());
	}
	finishNode(result, pos);
	withJSDoc(result, jsdoc);
	return result;
}

Node* Parser::parseBreakStatement() {
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	parseExpected(Kind::BreakKeyword);
	Node* label = parseIdentifierUnlessAtSemicolon();
	parseSemicolon();
	Node* result = finishNode(factory.newBreakStatement(label), pos);
	withJSDoc(result, jsdoc);
	return result;
}

Node* Parser::parseContinueStatement() {
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	parseExpected(Kind::ContinueKeyword);
	Node* label = parseIdentifierUnlessAtSemicolon();
	parseSemicolon();
	Node* result = finishNode(factory.newContinueStatement(label), pos);
	withJSDoc(result, jsdoc);
	return result;
}

Node* Parser::parseIdentifierUnlessAtSemicolon() {
	if (!canParseSemicolon()) {
		return parseIdentifier();
	}
	return nullptr;
}

Node* Parser::parseReturnStatement() {
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	parseExpected(Kind::ReturnKeyword);
	Node* expression = nullptr;
	if (!canParseSemicolon()) {
		expression = parseExpressionAllowIn();
	}
	parseSemicolon();
	Node* result = finishNode(factory.newReturnStatement(expression), pos);
	withJSDoc(result, jsdoc);
	return result;
}

Node* Parser::parseWithStatement() {
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	parseExpected(Kind::WithKeyword);
	int openParenPosition = scanner->tokenStart();
	bool openParenParsed = parseExpected(Kind::OpenParenToken);
	Node* expression = parseExpressionAllowIn();
	parseExpectedMatchingBrackets(Kind::OpenParenToken, Kind::CloseParenToken,
	                              openParenParsed, openParenPosition);
	Node* statement = doInContext(NodeFlagsInWithStatement, true,
	                              &Parser::parseStatement);
	Node* result =
		finishNode(factory.newWithStatement(expression, statement), pos);
	withJSDoc(result, jsdoc);
	return result;
}

Node* Parser::doInContext(NodeFlags flags, bool value,
                          Node* (Parser::*f)()) {
	NodeFlags saveContextFlags = contextFlags;
	setContextFlags(flags, value);
	Node* result = (this->*f)();
	contextFlags = saveContextFlags;
	return result;
}

Node* Parser::parseCaseClause() {
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	parseExpected(Kind::CaseKeyword);
	Node* expression = parseExpressionAllowIn();
	parseExpected(Kind::ColonToken);
	NodeList* statements =
		parseList(PCSwitchClauseStatements, &Parser::parseStatement);
	Node* result = finishNode(
		factory.newCaseOrDefaultClause(Kind::CaseClause, expression, statements),
		pos);
	withJSDoc(result, jsdoc);
	return result;
}

Node* Parser::parseDefaultClause() {
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	parseExpected(Kind::DefaultKeyword);
	parseExpected(Kind::ColonToken);
	NodeList* statements =
		parseList(PCSwitchClauseStatements, &Parser::parseStatement);
	Node* result = finishNode(
		factory.newCaseOrDefaultClause(Kind::DefaultClause, nullptr, statements),
		pos);
	withJSDoc(result, jsdoc);
	return result;
}

Node* Parser::parseCaseOrDefaultClause() {
	if (token == Kind::CaseKeyword) {
		return parseCaseClause();
	}
	return parseDefaultClause();
}

Node* Parser::parseCaseBlock() {
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	parseExpected(Kind::OpenBraceToken);
	NodeList* clauses =
		parseList(PCSwitchClauses, &Parser::parseCaseOrDefaultClause);
	parseExpected(Kind::CloseBraceToken);
	Node* result = finishNode(factory.newCaseBlock(clauses), pos);
	withJSDoc(result, jsdoc);
	return result;
}

Node* Parser::parseSwitchStatement() {
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	parseExpected(Kind::SwitchKeyword);
	parseExpected(Kind::OpenParenToken);
	Node* expression = parseExpressionAllowIn();
	parseExpected(Kind::CloseParenToken);
	Node* caseBlock = parseCaseBlock();
	Node* result = finishNode(
		factory.newSwitchStatement(expression, caseBlock), pos);
	withJSDoc(result, jsdoc);
	return result;
}

Node* Parser::parseThrowStatement() {
	// ThrowStatement[Yield] :
	//      throw [no LineTerminator here]Expression[In, ?Yield];
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	parseExpected(Kind::ThrowKeyword);
	// Because of automatic semicolon insertion, we need to report error if this
	// throw could be terminated with a semicolon.  Note: we can't call 'parseExpression'
	// directly as that might consume an expression on the following line.
	// Instead, we create a "missing" identifier, but don't report an error. The actual error
	// will be reported in the grammar walker.
	Node* expression;
	if (!hasPrecedingLineBreak()) {
		expression = parseExpressionAllowIn();
	} else {
		expression = createMissingIdentifier();
	}
	if (!tryParseSemicolon()) {
		parseErrorForMissingSemicolonAfter(expression);
	}
	Node* result = finishNode(factory.newThrowStatement(expression), pos);
	withJSDoc(result, jsdoc);
	return result;
}

// TODO: Review for error recovery
Node* Parser::parseTryStatement() {
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	parseExpected(Kind::TryKeyword);
	Node* tryBlock = parseBlock(false, nullptr);
	Node* catchClause = nullptr;
	if (token == Kind::CatchKeyword) {
		catchClause = parseCatchClause();
	}
	// If we don't have a catch clause, then we must have a finally clause.  Try to parse
	// one out no matter what.
	Node* finallyBlock = nullptr;
	if (catchClause == nullptr || token == Kind::FinallyKeyword) {
		parseExpectedWithDiagnostic(Kind::FinallyKeyword,
		                            X_catch_or_finally_expected, true);
		finallyBlock = parseBlock(false, nullptr);
	}
	Node* result = finishNode(
		factory.newTryStatement(tryBlock, catchClause, finallyBlock), pos);
	withJSDoc(result, jsdoc);
	return result;
}

Node* Parser::parseCatchClause() {
	int pos = nodePos();
	parseExpected(Kind::CatchKeyword);
	Node* variableDeclaration = nullptr;
	if (parseOptional(Kind::OpenParenToken)) {
		variableDeclaration = parseVariableDeclaration();
		parseExpected(Kind::CloseParenToken);
	}
	Node* block = parseBlock(false, nullptr);
	Node* result =
		finishNode(factory.newCatchClause(variableDeclaration, block), pos);
	return result;
}

Node* Parser::parseDebuggerStatement() {
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	parseExpected(Kind::DebuggerKeyword);
	parseSemicolon();
	Node* result = finishNode(factory.newDebuggerStatement(), pos);
	withJSDoc(result, jsdoc);
	return result;
}

Node* Parser::parseExpressionOrLabeledStatement() {
	// Avoiding having to do the lookahead for a labeled statement by just trying to parse
	// out an expression, seeing if it is identifier and then seeing if it is followed by
	// a colon.
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	bool hasParen = token == Kind::OpenParenToken;
	Node* expression = parseExpression();

	if (expression->kind == Kind::Identifier &&
	    parseOptional(Kind::ColonToken)) {
		Node* result = finishNode(
			factory.newLabeledStatement(expression, parseStatement()), pos);
		withJSDoc(result, jsdoc);
		return result;
	}

	if (!tryParseSemicolon()) {
		parseErrorForMissingSemicolonAfter(expression);
	}
	Node* result =
		finishNode(factory.newExpressionStatement(expression), pos);
	if (hasParen) {
		jsdoc &= ~JSDocScannerInfoHasJSDoc;
	}
	withJSDoc(result, jsdoc);
	return result;
}

Node* Parser::parseVariableStatement(int pos, JSDocScannerInfo jsdoc,
                                     ModifierList* modifiers) {
	Node* declarationList = parseVariableDeclarationList(false);
	parseSemicolon();
	Node* result = finishNode(
		factory.newVariableStatement(modifiers, declarationList), pos);
	withJSDoc(result, jsdoc);
	checkJSSyntax(result);
	return result;
}

Node* Parser::parseVariableDeclarationList(bool inForStatementInitializer) {
	int pos = nodePos();
	NodeFlags flags;
	switch (token) {
	case Kind::VarKeyword:
		flags = NodeFlagsNone;
		break;
	case Kind::LetKeyword:
		flags = NodeFlagsLet;
		break;
	case Kind::ConstKeyword:
		flags = NodeFlagsConst;
		break;
	case Kind::UsingKeyword:
		flags = NodeFlagsUsing;
		break;
	case Kind::AwaitKeyword:
		if (!isAwaitUsingDeclaration()) {
			break;
		}
		flags = NodeFlagsAwaitUsing;
		nextToken();
		break;
	default:
		TSC_UNREACHABLE("Unhandled case in parseVariableDeclarationList");
	}
	nextToken();
	// for (let of X) { }
	// In this case, we want to parse an empty declaration list, and then parse
	// 'of' as a keyword.
	NodeList* declarations;
	if (token == Kind::OfKeyword &&
	    lookAhead(&Parser::nextIsIdentifierAndCloseParen)) {
		declarations = createMissingList();
	} else {
		NodeFlags saveContextFlags = contextFlags;
		setContextFlags(NodeFlagsDisallowInContext,
		                inForStatementInitializer);
		declarations = parseDelimitedList(
			PCVariableDeclarations,
			inForStatementInitializer
				? &Parser::parseVariableDeclaration
				: &Parser::parseVariableDeclarationAllowExclamation);
		contextFlags = saveContextFlags;
	}
	return finishNode(factory.newVariableDeclarationList(declarations, flags),
	                  pos);
}

bool Parser::nextIsIdentifierAndCloseParen() {
	return nextTokenIsIdentifier() && nextToken() == Kind::CloseParenToken;
}

bool Parser::nextTokenIsIdentifier() {
	nextToken();
	return isIdentifier();
}

Node* Parser::parseVariableDeclaration() {
	return parseVariableDeclarationWorker(false);
}

Node* Parser::parseVariableDeclarationAllowExclamation() {
	return parseVariableDeclarationWorker(true);
}

Node* Parser::parseVariableDeclarationWorker(bool allowExclamation) {
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	Node* name = parseIdentifierOrPatternWithDiagnostic(
		Private_identifiers_are_not_allowed_in_variable_declarations);
	Node* exclamationToken = nullptr;
	if (allowExclamation && name->kind == Kind::Identifier &&
	    token == Kind::ExclamationToken && !hasPrecedingLineBreak()) {
		exclamationToken = parseTokenNode();
	}
	Node* typeNode = parseTypeAnnotation();
	Node* initializer = nullptr;
	if (token != Kind::InKeyword && token != Kind::OfKeyword) {
		initializer = parseInitializer();
	}
	Node* result = finishNode(
		factory.newVariableDeclaration(name, exclamationToken, typeNode,
		                               initializer),
		pos);
	withJSDoc(result, jsdoc);
	checkJSSyntax(result);
	return result;
}

Node* Parser::parseIdentifierOrPattern() {
	return parseIdentifierOrPatternWithDiagnostic(nullptr);
}

Node* Parser::parseIdentifierOrPatternWithDiagnostic(
	const DiagnosticMessage* privateIdentifierDiagnosticMessage) {
	if (token == Kind::OpenBracketToken) {
		return parseArrayBindingPattern();
	}
	if (token == Kind::OpenBraceToken) {
		return parseObjectBindingPattern();
	}
	return parseBindingIdentifierWithDiagnostic(
		privateIdentifierDiagnosticMessage);
}

Node* Parser::parseArrayBindingPattern() {
	int pos = nodePos();
	parseExpected(Kind::OpenBracketToken);
	NodeFlags saveContextFlags = contextFlags;
	setContextFlags(NodeFlagsDisallowInContext, false);
	NodeList* elements = parseDelimitedList(PCArrayBindingElements,
	                                        &Parser::parseArrayBindingElement);
	contextFlags = saveContextFlags;
	parseExpected(Kind::CloseBracketToken);
	return finishNode(
		factory.newBindingPattern(Kind::ArrayBindingPattern, elements), pos);
}

Node* Parser::parseArrayBindingElement() {
	int pos = nodePos();
	Node* dotDotDotToken = nullptr;
	Node* name = nullptr;
	Node* initializer = nullptr;
	if (token != Kind::CommaToken) {
		// These are all nil for a missing element
		dotDotDotToken = parseOptionalToken(Kind::DotDotDotToken);
		name = parseIdentifierOrPattern();
		initializer = parseInitializer();
	}
	return finishNode(
		factory.newBindingElement(dotDotDotToken, nullptr, name, initializer),
		pos);
}

Node* Parser::parseObjectBindingPattern() {
	int pos = nodePos();
	parseExpected(Kind::OpenBraceToken);
	NodeFlags saveContextFlags = contextFlags;
	setContextFlags(NodeFlagsDisallowInContext, false);
	NodeList* elements = parseDelimitedList(PCObjectBindingElements,
	                                        &Parser::parseObjectBindingElement);
	contextFlags = saveContextFlags;
	parseExpected(Kind::CloseBraceToken);
	return finishNode(
		factory.newBindingPattern(Kind::ObjectBindingPattern, elements), pos);
}

Node* Parser::parseObjectBindingElement() {
	int pos = nodePos();
	Node* dotDotDotToken = parseOptionalToken(Kind::DotDotDotToken);
	bool tokenIsIdentifier = isBindingIdentifier();
	Node* propertyName = parsePropertyName();
	Node* name;
	if (tokenIsIdentifier && token != Kind::ColonToken) {
		name = propertyName;
		propertyName = nullptr;
	} else {
		parseExpected(Kind::ColonToken);
		name = parseIdentifierOrPattern();
	}
	Node* initializer = parseInitializer();
	return finishNode(
		factory.newBindingElement(dotDotDotToken, propertyName, name,
		                          initializer),
		pos);
}

Node* Parser::parseInitializer() {
	if (parseOptional(Kind::EqualsToken)) {
		return parseAssignmentExpressionOrHigher();
	}
	return nullptr;
}

Node* Parser::parseTypeAnnotation() {
	if (parseOptional(Kind::ColonToken)) {
		return parseType();
	}
	return nullptr;
}

Node* Parser::parseFunctionDeclaration(int pos, JSDocScannerInfo jsdoc,
                                       ModifierList* modifiers) {
	parseExpected(Kind::FunctionKeyword);
	Node* asteriskToken = parseOptionalToken(Kind::AsteriskToken);
	// We don't parse the name here in await context, instead we will report a
	// grammar error in the checker.
	Node* name = nullptr;
	if (modifiers == nullptr ||
	    (modifiers->ModifierFlags & ModifierFlagsDefault) == 0 ||
	    isBindingIdentifier()) {
		name = parseBindingIdentifier();
	}
	ParseFlags signatureFlags =
		(asteriskToken != nullptr ? ParseFlagsYield : ParseFlagsNone) |
		(modifiers != nullptr &&
		             (modifiers->ModifierFlags & ModifierFlagsAsync) != 0
		         ? ParseFlagsAwait
		         : ParseFlagsNone);
	NodeList* typeParameters = parseTypeParameters();
	NodeFlags saveContextFlags = contextFlags;
	if (modifiers != nullptr &&
	    (modifiers->ModifierFlags & ModifierFlagsExport) != 0) {
		setContextFlags(NodeFlagsAwaitContext, true);
	}
	NodeList* parameters = parseParameters(signatureFlags);
	Node* returnType = parseReturnType(Kind::ColonToken, false);
	Node* body =
		parseFunctionBlockOrSemicolon(signatureFlags, X_or_expected);
	contextFlags = saveContextFlags;
	Node* result = finishNode(
		factory.newFunctionDeclaration(modifiers, asteriskToken, name,
		                               typeParameters, parameters, returnType,
		                               nullptr, body),
		pos);
	withJSDoc(result, jsdoc);
	checkJSSyntax(result);
	return result;
}

Node* Parser::parseClassDeclaration(int pos, JSDocScannerInfo jsdoc,
                                    ModifierList* modifiers) {
	return parseClassDeclarationOrExpression(pos, jsdoc, modifiers,
	                                         Kind::ClassDeclaration);
}

Node* Parser::parseClassExpression() {
	return parseClassDeclarationOrExpression(nodePos(), jsdocScannerInfo(),
	                                         nullptr, Kind::ClassExpression);
}

static bool isExportModifier(Node* modifier) {
	return modifier->kind == Kind::ExportKeyword;
}

static bool isAsyncModifier(Node* modifier) {
	return modifier->kind == Kind::AsyncKeyword;
}

Node* Parser::parseClassDeclarationOrExpression(int pos,
                                                JSDocScannerInfo jsdoc,
                                                ModifierList* modifiers,
                                                Kind kind) {
	NodeFlags saveContextFlags = contextFlags;
	bool saveHasAwaitIdentifier = statementHasAwaitIdentifier;
	parseExpected(Kind::ClassKeyword);
	// We don't parse the name here in await context, instead we will report a
	// grammar error in the checker.
	Node* name = parseNameOfClassDeclarationOrExpression();
	NodeList* typeParameters = parseTypeParameters();
	if (modifiers != nullptr &&
	    (parsingContexts & (1u << PCSourceElements)) != 0 &&
	    (parsingContexts &
	     ((1u << PCBlockStatements) | (1u << PCSwitchClauseStatements))) == 0 &&
	    std::any_of(modifiers->nodes.begin(), modifiers->nodes.end(),
	                isExportModifier)) {
		setContextFlags(NodeFlagsAwaitContext, true);
	}
	NodeList* heritageClauses = parseHeritageClauses(false);
	NodeList* members;
	if (parseExpected(Kind::OpenBraceToken)) {
		// ClassTail[Yield,Await] : (Modified) See 14.5
		members = parseList(PCClassMembers, &Parser::parseClassElement);
		parseExpected(Kind::CloseBraceToken);
	} else {
		members = createMissingList();
	}
	contextFlags = saveContextFlags;
	Node* result;
	if (modifiers != nullptr &&
	    (NodeFactory::modifiersToFlags(modifiers->nodes) & ModifierFlagsAmbient) != 0) {
		statementHasAwaitIdentifier = saveHasAwaitIdentifier;
	}
	if (kind == Kind::ClassDeclaration) {
		result = factory.newClassDeclaration(modifiers, name, typeParameters,
		                                     heritageClauses, members);
	} else {
		result = factory.newClassExpression(modifiers, name, typeParameters,
		                                    heritageClauses, members);
	}
	finishNode(result, pos);
	withJSDoc(result, jsdoc);
	if ((result->flags & NodeFlagsJavaScriptFile) != 0) {
		checkJSSyntax(result);
		if (heritageClauses != nullptr) {
			for (Node* clause : heritageClauses->nodes) {
				if (clause->as<HeritageClause>()->Token ==
				    Kind::ExtendsKeyword) {
					for (Node* expr :
					     clause->as<HeritageClause>()->Types->nodes) {
						checkJSSyntax(expr);
					}
				}
			}
		}
	}
	return result;
}

Node* Parser::parseNameOfClassDeclarationOrExpression() {
	// implements is a future reserved word so
	// 'class implements' might mean either
	// - class expression with omitted name, 'implements' starts heritage clause
	// - class with name 'implements'
	// 'isImplementsClause' helps to disambiguate between these two cases
	if (isBindingIdentifier() && !isImplementsClause()) {
		bool saveHasAwaitIdentifier = statementHasAwaitIdentifier;
		Node* id = createIdentifier(isBindingIdentifier());
		statementHasAwaitIdentifier = saveHasAwaitIdentifier;
		return id;
	}
	return nullptr;
}

bool Parser::isImplementsClause() {
	return token == Kind::ImplementsKeyword &&
	       lookAhead(&Parser::nextTokenIsIdentifierOrKeyword);
}

Node* Parser::parseHeritageClauseForList() {
	return parseHeritageClause(scratchBool_);
}

NodeList* Parser::parseHeritageClauses(bool isInterface) {
	// ClassTail[Yield,Await] : (Modified) See 14.5
	//      ClassHeritage[?Yield,?Await]opt { ClassBody[?Yield,?Await]opt }
	if (isHeritageClause()) {
		bool saveScratchBool = scratchBool_;
		scratchBool_ = isInterface;
		NodeList* result = parseList(PCHeritageClauses,
		                             &Parser::parseHeritageClauseForList);
		scratchBool_ = saveScratchBool;
		return result;
	}
	return nullptr;
}

static bool isTypeHeritageClause(bool isInterface, Kind tok) {
	return (isInterface && tok == Kind::ExtendsKeyword) ||
	       (!isInterface && tok == Kind::ImplementsKeyword);
}

Node* Parser::parseHeritageClause(bool isInterface) {
	int pos = nodePos();
	Kind kind = token;
	nextToken();
	Node* (Parser::*parseElement)() = &Parser::parseExpressionWithTypeArguments;
	if (isTypeHeritageClause(isInterface, kind)) {
		parseElement = &Parser::parseTypeHeritageClauseElement;
	}
	NodeList* types = parseDelimitedList(PCHeritageClauseElement, parseElement);
	return checkJSSyntax(
		finishNode(factory.newHeritageClause(kind, types), pos));
}

Node* Parser::parseTypeHeritageClauseElement() {
	int pos = nodePos();
	Node* expressionWithTypeArguments = parseExpressionWithTypeArguments();
	auto* ewt = expressionWithTypeArguments->as<ExpressionWithTypeArguments>();
	if (!isValidHeritageTypeReferenceExpression(ewt->Expression)) {
		return expressionWithTypeArguments;
	}
	Node* typeName =
		convertEntityNameExpressionToEntityName(ewt->Expression);
	return finishNode(factory.newTypeReferenceNode(typeName, ewt->TypeArguments),
	                  pos);
}

static bool isValidHeritageTypeReferenceExpression(Node* node) {
	if (::tsc::isIdentifier(node)) {
		return nodeIsPresent(node);
	}
	return isPropertyAccessExpression(node) && !::tsc::isOptionalChain(node) &&
	       nodeIsPresent(node->name()) &&
	       isValidHeritageTypeReferenceExpression(node->expression());
}

Node* Parser::convertEntityNameExpressionToEntityName(Node* node) {
	if (::tsc::isIdentifier(node)) {
		return node;
	}
	auto* propertyAccess = node->as<PropertyAccessExpression>();
	Node* result = factory.newQualifiedName(
		convertEntityNameExpressionToEntityName(propertyAccess->Expression),
		propertyAccess->name);
	return finishNodeWithEnd(result, node->pos(), node->end());
}

Node* Parser::parseExpressionWithTypeArguments() {
	int pos = nodePos();
	Node* expression = parseLeftHandSideExpressionOrHigher();
	if (isExpressionWithTypeArguments(expression)) {
		return expression;
	}
	NodeList* typeArguments = parseTypeArguments();
	return finishNode(
		factory.newExpressionWithTypeArguments(expression, typeArguments),
		pos);
}

Node* Parser::parseClassElement() {
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	if (token == Kind::SemicolonToken) {
		nextToken();
		Node* result = finishNode(factory.newSemicolonClassElement(), pos);
		withJSDoc(result, jsdoc);
		return result;
	}
	ModifierList* modifiers = parseModifiersEx(true, true, true);
	if (token == Kind::StaticKeyword &&
	    lookAhead(&Parser::nextTokenIsOpenBrace)) {
		return parseClassStaticBlockDeclaration(pos, jsdoc, modifiers);
	}
	if (parseContextualModifier(Kind::GetKeyword)) {
		return parseAccessorDeclaration(pos, jsdoc, modifiers,
		                                Kind::GetAccessor, ParseFlagsNone);
	}
	if (parseContextualModifier(Kind::SetKeyword)) {
		return parseAccessorDeclaration(pos, jsdoc, modifiers,
		                                Kind::SetAccessor, ParseFlagsNone);
	}
	if (token == Kind::ConstructorKeyword || token == Kind::StringLiteral) {
		Node* constructorDeclaration =
			tryParseConstructorDeclaration(pos, jsdoc, modifiers);
		if (constructorDeclaration != nullptr) {
			return constructorDeclaration;
		}
	}
	if (isIndexSignature()) {
		return checkJSSyntax(
			parseIndexSignatureDeclaration(pos, jsdoc, modifiers));
	}
	// It is very important that we check this *after* checking indexers because
	// the [ token can start an index signature or a computed property name
	if (tokenIsIdentifierOrKeyword(token) || token == Kind::StringLiteral ||
	    token == Kind::NumericLiteral || token == Kind::BigIntLiteral ||
	    token == Kind::AsteriskToken || token == Kind::OpenBracketToken) {
		bool isAmbient = modifiers != nullptr &&
		                 std::any_of(modifiers->nodes.begin(),
		                             modifiers->nodes.end(),
		                             isDeclareModifier);
		if (isAmbient) {
			for (Node* m : modifiers->nodes) {
				m->flags |= NodeFlagsAmbient;
			}
			NodeFlags saveContextFlags = contextFlags;
			setContextFlags(NodeFlagsAmbient, true);
			Node* result =
				parsePropertyOrMethodDeclaration(pos, jsdoc, modifiers);
			contextFlags = saveContextFlags;
			return result;
		} else {
			return parsePropertyOrMethodDeclaration(pos, jsdoc, modifiers);
		}
	}
	if (modifiers != nullptr) {
		// treat this as a property declaration with a missing name.
		parseErrorAt(nodePos(), nodePos(), Declaration_expected);
		Node* name = createMissingIdentifier();
		return parsePropertyDeclaration(pos, jsdoc, modifiers, name, nullptr);
	}
	// 'isClassMemberStart' should have hinted not to attempt parsing.
	TSC_UNREACHABLE(
		"Should not have attempted to parse class member declaration.");
}

Node* Parser::parseClassStaticBlockDeclaration(int pos,
                                               JSDocScannerInfo jsdoc,
                                               ModifierList* modifiers) {
	parseExpectedToken(Kind::StaticKeyword);
	Node* body = parseClassStaticBlockBody();
	Node* result =
		finishNode(factory.newClassStaticBlockDeclaration(modifiers, body),
		           pos);
	withJSDoc(result, jsdoc);
	return result;
}

Node* Parser::parseClassStaticBlockBody() {
	NodeFlags saveContextFlags = contextFlags;
	setContextFlags(NodeFlagsYieldContext, false);
	setContextFlags(NodeFlagsAwaitContext, true);
	Node* body = parseBlock(false, nullptr);
	contextFlags = saveContextFlags;
	return body;
}

Node* Parser::tryParseConstructorDeclaration(int pos, JSDocScannerInfo jsdoc,
                                             ModifierList* modifiers) {
	ParserState state = mark();
	if (token == Kind::ConstructorKeyword ||
	    (token == Kind::StringLiteral &&
	     scanner->tokenValue() == "constructor" &&
	     lookAhead(&Parser::nextTokenIsOpenParen))) {
		nextToken();
		NodeList* typeParameters = parseTypeParameters();
		NodeList* parameters = parseParameters(ParseFlagsNone);
		Node* returnType = parseReturnType(Kind::ColonToken, false);
		Node* body = parseFunctionBlockOrSemicolon(ParseFlagsNone,
		                                         X_or_expected);
		Node* result = finishNode(
			factory.newConstructorDeclaration(modifiers, typeParameters,
		                                      parameters, returnType, nullptr,
		                                      body),
			pos);
		withJSDoc(result, jsdoc);
		checkJSSyntax(result);
		return result;
	}
	rewind(state);
	return nullptr;
}

bool Parser::nextTokenIsOpenParen() {
	return nextToken() == Kind::OpenParenToken;
}

Node* Parser::parsePropertyOrMethodDeclaration(int pos,
                                               JSDocScannerInfo jsdoc,
                                               ModifierList* modifiers) {
	Node* asteriskToken = parseOptionalToken(Kind::AsteriskToken);
	Node* name = parsePropertyName();
	// Note: this is not legal as per the grammar.  But we allow it in the
	// parser and report an error in the grammar checker.
	Node* questionToken = parseOptionalToken(Kind::QuestionToken);
	if (asteriskToken != nullptr || token == Kind::OpenParenToken ||
	    token == Kind::LessThanToken) {
		return parseMethodDeclaration(pos, jsdoc, modifiers, asteriskToken,
		                              name, questionToken, X_or_expected);
	}
	return parsePropertyDeclaration(pos, jsdoc, modifiers, name,
	                                questionToken);
}

static bool modifierListHasAsync(ModifierList* modifiers) {
	return modifiers != nullptr &&
	       std::any_of(modifiers->nodes.begin(), modifiers->nodes.end(),
	                   isAsyncModifier);
}

Node* Parser::parseMethodDeclaration(
	int pos, JSDocScannerInfo jsdoc, ModifierList* modifiers,
	Node* asteriskToken, Node* name, Node* questionToken,
	const DiagnosticMessage* diagnosticMessage) {
	ParseFlags signatureFlags =
		(asteriskToken != nullptr ? ParseFlagsYield : ParseFlagsNone) |
		(modifierListHasAsync(modifiers) ? ParseFlagsAwait
		                                 : ParseFlagsNone);
	NodeList* typeParameters = parseTypeParameters();
	NodeList* parameters = parseParameters(signatureFlags);
	Node* typeNode = parseReturnType(Kind::ColonToken, false);
	Node* body =
		parseFunctionBlockOrSemicolon(signatureFlags, diagnosticMessage);
	Node* result = finishNode(
		factory.newMethodDeclaration(modifiers, asteriskToken, name,
		                             questionToken, typeParameters, parameters,
		                             typeNode, nullptr, body),
		pos);
	withJSDoc(result, jsdoc);
	checkJSSyntax(result);
	return result;
}

Node* Parser::parsePropertyDeclaration(int pos, JSDocScannerInfo jsdoc,
                                       ModifierList* modifiers, Node* name,
                                       Node* questionToken) {
	Node* postfixToken = questionToken;
	if (postfixToken == nullptr && !hasPrecedingLineBreak()) {
		postfixToken = parseOptionalToken(Kind::ExclamationToken);
	}
	Node* typeNode = parseTypeAnnotation();
	Node* initializer =
		doInContext(NodeFlagsYieldContext | NodeFlagsAwaitContext |
		                NodeFlagsDisallowInContext,
		            false, &Parser::parseInitializer);
	parseSemicolonAfterPropertyName(name, typeNode, initializer);
	Node* result = finishNode(
		factory.newPropertyDeclaration(modifiers, name, postfixToken, typeNode,
		                               initializer),
		pos);
	withJSDoc(result, jsdoc);
	checkJSSyntax(result);
	return result;
}

void Parser::parseSemicolonAfterPropertyName(Node* name, Node* typeNode,
                                             Node* initializer) {
	if (token == Kind::AtToken && !hasPrecedingLineBreak()) {
		parseErrorAtCurrentToken(
			Decorators_must_precede_the_name_and_all_keywords_of_property_declarations);
		return;
	}
	if (token == Kind::OpenParenToken) {
		parseErrorAtCurrentToken(Cannot_start_a_function_call_in_a_type_annotation);
		nextToken();
		return;
	}
	if (typeNode != nullptr && !canParseSemicolon()) {
		if (initializer != nullptr) {
			parseErrorAtCurrentToken(
				X_0_expected,
				{std::string(tokenToString(Kind::SemicolonToken))});
		} else {
			parseErrorAtCurrentToken(Expected_for_property_initializer);
		}
		return;
	}
	if (tryParseSemicolon()) {
		return;
	}
	if (initializer != nullptr) {
		parseErrorAtCurrentToken(
			X_0_expected, {std::string(tokenToString(Kind::SemicolonToken))});
		return;
	}
	parseErrorForMissingSemicolonAfter(name);
}

void Parser::parseErrorForMissingSemicolonAfter(Node* node) {
	// Tagged template literals are sometimes used in places where only simple
	// strings are allowed, i.e.:
	//   module `M1` {
	//   ^^^^^^^^^^^ This block is parsed as a template literal like module`M1`.
	if (node->kind == Kind::TaggedTemplateExpression) {
		parseErrorAtRange(
			skipRangeTrivia(node->as<TaggedTemplateExpression>()->Template->loc),
			Module_declaration_names_may_only_use_or_quoted_strings);
		return;
	}
	// Otherwise, if this isn't a well-known keyword-like identifier, give the
	// generic fallback message.
	std::string expressionText;
	if (node->kind == Kind::Identifier) {
		expressionText = node->text();
	}
	if (expressionText.empty()) {
		parseErrorAtCurrentToken(
			X_0_expected, {std::string(tokenToString(Kind::SemicolonToken))});
		return;
	}
	int pos = skipTrivia(sourceText, node->pos());
	// Some known keywords are likely signs of syntax being used improperly.
	if (expressionText == "const" || expressionText == "let" ||
	    expressionText == "var") {
		parseErrorAt(pos, node->end(),
		             Variable_declaration_not_allowed_at_this_location);
		return;
	}
	if (expressionText == "declare") {
		// If a declared node failed to parse, it would have emitted a
		// diagnostic already.
		return;
	}
	if (expressionText == "interface") {
		parseErrorForInvalidName(Interface_name_cannot_be_0,
		                         Interface_must_be_given_a_name,
		                         Kind::OpenBraceToken);
		return;
	}
	if (expressionText == "is") {
		parseErrorAt(
			pos, scanner->tokenStart(),
			A_type_predicate_is_only_allowed_in_return_type_position_for_functions_and_methods);
		return;
	}
	if (expressionText == "module" || expressionText == "namespace") {
		parseErrorForInvalidName(Namespace_name_cannot_be_0,
		                         Namespace_must_be_given_a_name,
		                         Kind::OpenBraceToken);
		return;
	}
	if (expressionText == "type") {
		parseErrorForInvalidName(Type_alias_name_cannot_be_0,
		                         Type_alias_must_be_given_a_name,
		                         Kind::EqualsToken);
		return;
	}
	// The user alternatively might have misspelled or forgotten to add a space
	// after a common keyword.
	std::string suggestion = getSpellingSuggestionForStrings(
		expressionText, getViableKeywordSuggestions());
	if (suggestion.empty()) {
		suggestion = getSpaceSuggestion(expressionText);
	}
	if (!suggestion.empty()) {
		parseErrorAt(pos, node->end(), Unknown_keyword_or_identifier_Did_you_mean_0,
		             {suggestion});
		return;
	}
	// Unknown tokens are handled with their own errors in the scanner
	if (token == Kind::Unknown) {
		return;
	}
	// Otherwise, we know this some kind of unknown word, not just a missing
	// expected semicolon.
	parseErrorAt(pos, node->end(), Unexpected_keyword_or_identifier);
}

std::string Parser::getSpaceSuggestion(std::string_view expressionText) {
	for (const std::string_view& keyword : getViableKeywordSuggestions()) {
		if (expressionText.size() > keyword.size() + 2 &&
		    expressionText.substr(0, keyword.size()) == keyword) {
			std::string s(keyword);
			s += ' ';
			s += expressionText.substr(keyword.size());
			return s;
		}
	}
	return "";
}

void Parser::parseErrorForInvalidName(
	const DiagnosticMessage* nameDiagnostic,
	const DiagnosticMessage* blankDiagnostic, Kind tokenIfBlankName) {
	if (token == tokenIfBlankName) {
		parseErrorAtCurrentToken(blankDiagnostic);
	} else {
		parseErrorAtCurrentToken(nameDiagnostic,
		                         {std::string(scanner->tokenValue())});
	}
}

TextRange Parser::skipRangeTrivia(TextRange range) {
	return TextRange{skipTrivia(sourceText, range.pos()), range.end()};
}

Node* Parser::parseInterfaceDeclaration(int pos, JSDocScannerInfo jsdoc,
                                        ModifierList* modifiers) {
	parseExpected(Kind::InterfaceKeyword);
	Node* name = parseIdentifier();
	NodeList* typeParameters = parseTypeParameters();
	NodeList* heritageClauses = parseHeritageClauses(true);
	NodeList* members = parseObjectTypeMembers();
	Node* result = finishNode(
		factory.newInterfaceDeclaration(modifiers, name, typeParameters,
		                                heritageClauses, members),
		pos);
	withJSDoc(result, jsdoc);
	checkJSSyntax(result);
	return result;
}

Node* Parser::parseTypeAliasDeclaration(int pos, JSDocScannerInfo jsdoc,
                                        ModifierList* modifiers) {
	parseExpected(Kind::TypeKeyword);
	if (hasPrecedingLineBreak()) {
		parseErrorAtCurrentToken(Line_break_not_permitted_here);
	}
	Node* name = parseIdentifier();
	NodeList* typeParameters = parseTypeParameters();
	parseExpected(Kind::EqualsToken);
	Node* typeNode;
	if (token == Kind::IntrinsicKeyword && lookAhead(&Parser::nextIsNotDot)) {
		typeNode = parseKeywordTypeNode();
	} else {
		typeNode = parseType();
	}
	parseSemicolon();
	Node* result = finishNode(
		factory.newTypeAliasDeclaration(modifiers, name, typeParameters,
		                                typeNode),
		pos);
	withJSDoc(result, jsdoc);
	checkJSSyntax(result);
	return result;
}

bool Parser::nextIsNotDot() {
	return nextToken() != Kind::DotToken;
}

// In an ambient declaration, the grammar only allows integer literals as
// initializers. In a non-ambient declaration, the grammar allows uninitialized
// members only in a ConstantEnumMemberSection.
Node* Parser::parseEnumMember() {
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	Node* name = parsePropertyName();
	Node* initializer = doInContext(NodeFlagsDisallowInContext, false,
	                                &Parser::parseInitializer);
	Node* result = finishNode(factory.newEnumMember(name, initializer), pos);
	withJSDoc(result, jsdoc);
	return result;
}

Node* Parser::parseEnumDeclaration(int pos, JSDocScannerInfo jsdoc,
                                   ModifierList* modifiers) {
	bool saveHasAwaitIdentifier = statementHasAwaitIdentifier;
	parseExpected(Kind::EnumKeyword);
	Node* name = parseIdentifier();
	NodeList* members;
	if (parseExpected(Kind::OpenBraceToken)) {
		NodeFlags saveContextFlags = contextFlags;
		setContextFlags(NodeFlagsYieldContext | NodeFlagsAwaitContext, false);
		members = parseDelimitedList(PCEnumMembers, &Parser::parseEnumMember);
		contextFlags = saveContextFlags;
		parseExpected(Kind::CloseBraceToken);
	} else {
		members = createMissingList();
	}
	Node* result =
		finishNode(factory.newEnumDeclaration(modifiers, name, members), pos);
	withJSDoc(result, jsdoc);
	checkJSSyntax(result);
	statementHasAwaitIdentifier = saveHasAwaitIdentifier;
	return result;
}

Node* Parser::parseModuleDeclaration(int pos, JSDocScannerInfo jsdoc,
                                     ModifierList* modifiers) {
	Kind keyword = Kind::ModuleKeyword;
	if (token == Kind::GlobalKeyword) {
		// global augmentation
		return parseAmbientExternalModuleDeclaration(pos, jsdoc, modifiers);
	} else if (parseOptional(Kind::NamespaceKeyword)) {
		keyword = Kind::NamespaceKeyword;
	} else {
		parseExpected(Kind::ModuleKeyword);
		if (token == Kind::StringLiteral) {
			return parseAmbientExternalModuleDeclaration(pos, jsdoc,
			                                             modifiers);
		}
	}
	return parseModuleOrNamespaceDeclaration(pos, jsdoc, modifiers, false,
	                                         keyword);
}

Node* Parser::parseAmbientExternalModuleDeclaration(
	int pos, JSDocScannerInfo jsdoc, ModifierList* modifiers) {
	Node* name;
	Kind keyword = Kind::ModuleKeyword;
	bool saveHasAwaitIdentifier = statementHasAwaitIdentifier;
	if (token == Kind::GlobalKeyword) {
		// parse 'global' as name of global scope augmentation
		name = parseIdentifier();
		keyword = Kind::GlobalKeyword;
	} else {
		// parse string literal
		name = parseLiteralExpression();
	}
	Node* attributes = nullptr;
	if (keyword == Kind::ModuleKeyword && parseOptional(Kind::WithKeyword)) {
		attributes = parseTypeLiteral();
	}
	Node* body = nullptr;
	if (token == Kind::OpenBraceToken) {
		body = parseModuleBlock();
	} else {
		parseSemicolon();
	}
	Node* result = finishNode(
		factory.newModuleDeclaration(modifiers, keyword, name, attributes,
		                             body),
		pos);
	withJSDoc(result, jsdoc);
	statementHasAwaitIdentifier = saveHasAwaitIdentifier;
	return result;
}

Node* Parser::parseModuleBlock() {
	int pos = nodePos();
	NodeList* statements;
	if (parseExpected(Kind::OpenBraceToken)) {
		statements = parseList(PCBlockStatements, &Parser::parseStatement);
		parseExpected(Kind::CloseBraceToken);
	} else {
		statements = createMissingList();
	}
	return finishNode(factory.newModuleBlock(statements), pos);
}

Node* Parser::parseModuleOrNamespaceDeclaration(int pos,
                                                JSDocScannerInfo jsdoc,
                                                ModifierList* modifiers,
                                                bool nested, Kind keyword) {
	bool saveHasAwaitIdentifier = statementHasAwaitIdentifier;
	Node* name;
	if (nested) {
		name = parseIdentifierName();
	} else {
		name = parseIdentifier();
	}
	Node* body;
	if (parseOptional(Kind::DotToken)) {
		Node* implicitExport = factory.newToken(Kind::ExportKeyword);
		implicitExport->loc = TextRange{nodePos(), nodePos()};
		implicitExport->flags = NodeFlagsReparsed;
		ModifierList* implicitModifiers =
			newModifierList(implicitExport->loc, {implicitExport});
		body = parseModuleOrNamespaceDeclaration(nodePos(), {},
		                                         implicitModifiers, true,
		                                         keyword);
	} else {
		body = parseModuleBlock();
	}
	Node* result = finishNode(
		factory.newModuleDeclaration(modifiers, keyword, name, nullptr, body),
		pos);
	withJSDoc(result, jsdoc);
	checkJSSyntax(result);
	statementHasAwaitIdentifier = saveHasAwaitIdentifier;
	return result;
}

Node* Parser::parseImportDeclarationOrImportEqualsDeclaration(
	int pos, JSDocScannerInfo jsdoc, ModifierList* modifiers) {
	parseExpected(Kind::ImportKeyword);
	int afterImportPos = nodePos();
	// We don't parse the identifier here in await context, instead we will
	// report a grammar error in the checker.
	bool saveHasAwaitIdentifier = statementHasAwaitIdentifier;
	Node* identifier = nullptr;
	if (isIdentifier()) {
		identifier = parseIdentifier();
	}
	Kind phaseModifier = Kind::Unknown;
	if (identifier != nullptr && identifier->text() == "type" &&
	    (token != Kind::FromKeyword ||
	     (isIdentifier() &&
	      lookAhead(&Parser::nextTokenIsFromKeywordOrEqualsToken))) &&
	    (isIdentifier() ||
	     tokenAfterImportDefinitelyProducesImportDeclaration())) {
		phaseModifier = Kind::TypeKeyword;
		identifier = nullptr;
		if (isIdentifier()) {
			identifier = parseIdentifier();
		}
	} else if (identifier != nullptr && identifier->text() == "defer") {
		bool shouldParseAsDeferModifier;
		if (token == Kind::FromKeyword) {
			shouldParseAsDeferModifier =
				!lookAhead(&Parser::nextTokenIsTokenStringLiteral);
		} else {
			shouldParseAsDeferModifier = token != Kind::CommaToken &&
			                             token != Kind::EqualsToken;
		}
		if (shouldParseAsDeferModifier) {
			phaseModifier = Kind::DeferKeyword;
			identifier = nullptr;
			if (isIdentifier()) {
				identifier = parseIdentifier();
			}
		}
	}
	if (identifier != nullptr &&
	    !tokenAfterImportedIdentifierDefinitelyProducesImportDeclaration() &&
	    phaseModifier != Kind::DeferKeyword) {
		Node* importEquals = checkJSSyntax(parseImportEqualsDeclaration(
			pos, jsdoc, modifiers, identifier,
			phaseModifier == Kind::TypeKeyword));
		statementHasAwaitIdentifier =
			saveHasAwaitIdentifier;  // Import= is always parsed in an Await
		                             // context, no need to reparse
		return importEquals;
	}
	Node* importClause = tryParseImportClause(identifier, afterImportPos,
	                                          phaseModifier, false);
	statementHasAwaitIdentifier = saveHasAwaitIdentifier;  // import clause is
	                                                     // always parsed in
	                                                     // an Await context
	Node* moduleSpecifier = parseModuleSpecifier();
	Node* attributes = tryParseImportAttributes();
	parseSemicolon();
	Node* result = finishNode(
		factory.newImportDeclaration(modifiers, importClause, moduleSpecifier,
		                             attributes),
		pos);
	withJSDoc(result, jsdoc);
	checkJSSyntax(result);
	return result;
}

bool Parser::nextTokenIsFromKeywordOrEqualsToken() {
	nextToken();
	return token == Kind::FromKeyword || token == Kind::EqualsToken;
}

bool Parser::tokenAfterImportDefinitelyProducesImportDeclaration() {
	return token == Kind::AsteriskToken || token == Kind::OpenBraceToken;
}

bool Parser::tokenAfterImportedIdentifierDefinitelyProducesImportDeclaration() {
	// In `import id ___`, the current token decides whether to produce
	// an ImportDeclaration or ImportEqualsDeclaration.
	return token == Kind::CommaToken || token == Kind::FromKeyword;
}

Node* Parser::parseImportEqualsDeclaration(int pos, JSDocScannerInfo jsdoc,
                                           ModifierList* modifiers,
                                           Node* identifier,
                                           bool isTypeOnly) {
	parseExpected(Kind::EqualsToken);
	Node* moduleReference = parseModuleReference();
	parseSemicolon();
	Node* result = finishNode(
		factory.newImportEqualsDeclaration(modifiers, isTypeOnly, identifier,
		                                   moduleReference),
		pos);
	withJSDoc(result, jsdoc);
	return result;
}

Node* Parser::parseModuleReference() {
	if (token == Kind::RequireKeyword &&
	    lookAhead(&Parser::nextTokenIsOpenParen)) {
		return parseExternalModuleReference();
	}
	return parseEntityName(false, false, nullptr);
}

Node* Parser::parseExternalModuleReference() {
	bool saveHasAwaitIdentifier = statementHasAwaitIdentifier;
	int pos = nodePos();
	parseExpected(Kind::RequireKeyword);
	parseExpected(Kind::OpenParenToken);
	Node* expression = parseModuleSpecifier();
	parseExpected(Kind::CloseParenToken);
	Node* result = finishNode(factory.newExternalModuleReference(expression),
	                          pos);
	statementHasAwaitIdentifier = saveHasAwaitIdentifier;
	return result;
}

Node* Parser::parseModuleSpecifier() {
	if (token == Kind::StringLiteral) {
		return parseLiteralExpression();
	}
	// We allow arbitrary expressions here, even though the grammar only allows
	// string literals. We check to ensure that it is only a string literal
	// later in the grammar check pass.
	return parseExpression();
}

Node* Parser::tryParseImportClause(Node* identifier, int pos,
                                   Kind phaseModifier,
                                   bool skipJSDocLeadingAsterisks) {
	// ImportDeclaration:
	//  import ImportClause from ModuleSpecifier ;
	//  import ModuleSpecifier;
	if (identifier != nullptr || token == Kind::AsteriskToken ||
	    token == Kind::OpenBraceToken) {
		Node* importClause = parseImportClause(identifier, pos, phaseModifier,
		                                       skipJSDocLeadingAsterisks);
		parseExpected(Kind::FromKeyword);
		return importClause;
	}
	return nullptr;
}

Node* Parser::parseImportClause(Node* identifier, int pos, Kind phaseModifier,
                                bool skipJSDocLeadingAsterisks) {
	// ImportClause:
	//  ImportedDefaultBinding
	//  NameSpaceImport
	//  NamedImports
	//  ImportedDefaultBinding, NameSpaceImport
	//  ImportedDefaultBinding, NamedImports
	// If there was no default import or if there is comma token after default
	// import parse namespace or named imports
	Node* namedBindings = nullptr;
	bool saveHasAwaitIdentifier = statementHasAwaitIdentifier;
	if (identifier == nullptr || parseOptional(Kind::CommaToken)) {
		if (skipJSDocLeadingAsterisks) {
			scanner->setSkipJSDocLeadingAsterisks(true);
		}
		if (token == Kind::AsteriskToken) {
			namedBindings = parseNamespaceImport();
		} else {
			namedBindings = parseNamedImports();
		}
		if (skipJSDocLeadingAsterisks) {
			scanner->setSkipJSDocLeadingAsterisks(false);
		}
	}
	Node* result = finishNode(
		factory.newImportClause(phaseModifier, identifier, namedBindings),
		pos);
	statementHasAwaitIdentifier = saveHasAwaitIdentifier;
	return result;
}

Node* Parser::parseNamespaceImport() {
	// NameSpaceImport:
	//  * as ImportedBinding
	int pos = nodePos();
	parseExpected(Kind::AsteriskToken);
	parseExpected(Kind::AsKeyword);
	Node* name = parseIdentifier();
	return finishNode(factory.newNamespaceImport(name), pos);
}

Node* Parser::parseNamedImports() {
	int pos = nodePos();
	// NamedImports:
	//  { }
	//  { ImportsList }
	//  { ImportsList, }
	NodeList* imports = parseBracketedList(
		PCImportOrExportSpecifiers, &Parser::parseImportSpecifier,
		Kind::OpenBraceToken, Kind::CloseBraceToken);
	return finishNode(factory.newNamedImports(imports), pos);
}

Node* Parser::parseImportSpecifier() {
	int pos = nodePos();
	auto [isTypeOnly, propertyName, name] =
		parseImportOrExportSpecifier(Kind::ImportSpecifier);
	Node* identifierName;
	if (name->kind == Kind::Identifier) {
		identifierName = name;
	} else {
		parseErrorAtRange(skipRangeTrivia(name->loc), Identifier_expected);
		identifierName = newIdentifier("");
		finishNode(identifierName, name->pos());
	}
	Node* result = checkJSSyntax(
		finishNode(factory.newImportSpecifier(isTypeOnly, propertyName,
		                                      identifierName),
		           pos));
	return result;
}

Parser::ImportOrExportSpecifierResult Parser::parseImportOrExportSpecifier(
	Kind kind) {
	bool isTypeOnly = false;
	Node* propertyName = nullptr;
	Node* name = nullptr;
	// ImportSpecifier:
	//   BindingIdentifier
	//   ModuleExportName as BindingIdentifier
	// ExportSpecifier:
	//   ModuleExportName
	//   ModuleExportName as ModuleExportName
	bool canParseAsKeyword = true;
	bool disallowKeywords = kind == Kind::ImportSpecifier;
	auto [name_, nameOk] = parseModuleExportName(disallowKeywords);
	name = name_;
	if (name->kind == Kind::Identifier && name->text() == "type") {
		// If the first token of an import specifier is 'type', there are a lot
		// of possibilities, especially if we see 'as' afterwards.
		if (token == Kind::AsKeyword) {
			// { type as ...? }
			Node* firstAs = parseIdentifierName();
			if (token == Kind::AsKeyword) {
				// { type as as ...? }
				Node* secondAs = parseIdentifierName();
				if (canParseModuleExportName()) {
					// { type as as something }
					// { type as as "something" }
					isTypeOnly = true;
					propertyName = firstAs;
					auto r = parseModuleExportName(disallowKeywords);
					name = r.node;
					nameOk = r.nameOk;
					canParseAsKeyword = false;
				} else {
					// { type as as }
					propertyName = name;
					name = secondAs;
					canParseAsKeyword = false;
				}
			} else if (canParseModuleExportName()) {
				// { type as something }
				// { type as "something" }
				propertyName = name;
				canParseAsKeyword = false;
				auto r = parseModuleExportName(disallowKeywords);
				name = r.node;
				nameOk = r.nameOk;
			} else {
				// { type as }
				isTypeOnly = true;
				name = firstAs;
			}
		} else if (canParseModuleExportName()) {
			// { type something ...? }
			// { type "something" ...? }
			isTypeOnly = true;
			auto r = parseModuleExportName(disallowKeywords);
			name = r.node;
			nameOk = r.nameOk;
		}
	}
	if (canParseAsKeyword && token == Kind::AsKeyword) {
		propertyName = name;
		parseExpected(Kind::AsKeyword);
		auto r = parseModuleExportName(disallowKeywords);
		name = r.node;
		nameOk = r.nameOk;
	}

	if (!nameOk) {
		parseErrorAtRange(skipRangeTrivia(name->loc), Identifier_expected);
	}

	return {isTypeOnly, propertyName, name};
}

bool Parser::canParseModuleExportName() {
	return tokenIsIdentifierOrKeyword(token) || token == Kind::StringLiteral;
}

Parser::ModuleExportNameResult Parser::parseModuleExportName(
	bool disallowKeywords) {
	bool nameOk = true;
	Node* node;
	if (token == Kind::StringLiteral) {
		node = parseLiteralExpression();
		return {node, nameOk};
	}
	if (disallowKeywords && isKeyword(token) && !isIdentifier()) {
		nameOk = false;
	}
	node = parseIdentifierName();
	return {node, nameOk};
}

Node* Parser::tryParseImportAttributes() {
	if (token == Kind::WithKeyword ||
	    (token == Kind::AssertKeyword && !hasPrecedingLineBreak())) {
		if (token == Kind::AssertKeyword) {
			parseErrorAtCurrentToken(
				Import_assertions_have_been_replaced_by_import_attributes_Use_with_instead_of_assert);
		}
		return parseImportAttributes(token, false);
	}
	return nullptr;
}

Node* Parser::parseExportAssignment(int pos, JSDocScannerInfo jsdoc,
                                    ModifierList* modifiers) {
	NodeFlags saveContextFlags = contextFlags;
	bool saveHasAwaitIdentifier = statementHasAwaitIdentifier;
	setContextFlags(NodeFlagsAwaitContext, true);
	bool isExportEquals = false;
	if (parseOptional(Kind::EqualsToken)) {
		isExportEquals = true;
	} else {
		parseExpected(Kind::DefaultKeyword);
	}
	Node* expression = parseAssignmentExpressionOrHigher();
	parseSemicolon();
	contextFlags = saveContextFlags;
	statementHasAwaitIdentifier = saveHasAwaitIdentifier;
	Node* result = finishNode(
		factory.newExportAssignment(modifiers, isExportEquals, nullptr,
		                            expression),
		pos);
	withJSDoc(result, jsdoc);
	checkJSSyntax(result);
	return result;
}

Node* Parser::parseNamespaceExportDeclaration(int pos, JSDocScannerInfo jsdoc,
                                              ModifierList* modifiers) {
	parseExpected(Kind::AsKeyword);
	parseExpected(Kind::NamespaceKeyword);
	bool saveHasAwaitIdentifier = statementHasAwaitIdentifier;
	Node* name = parseIdentifier();
	statementHasAwaitIdentifier = saveHasAwaitIdentifier;
	parseSemicolon();
	// NamespaceExportDeclaration nodes cannot have decorators or modifiers, we
	// attach them here so we can report them in the grammar checker
	Node* result = finishNode(
		factory.newNamespaceExportDeclaration(modifiers, name), pos);
	withJSDoc(result, jsdoc);
	return result;
}

Node* Parser::parseExportDeclaration(int pos, JSDocScannerInfo jsdoc,
                                     ModifierList* modifiers) {
	NodeFlags saveContextFlags = contextFlags;
	bool saveHasAwaitIdentifier = statementHasAwaitIdentifier;
	setContextFlags(NodeFlagsAwaitContext, true);
	Node* exportClause = nullptr;
	Node* moduleSpecifier = nullptr;
	Node* attributes = nullptr;
	bool isTypeOnly = parseOptional(Kind::TypeKeyword);
	int namespaceExportPos = nodePos();
	if (parseOptional(Kind::AsteriskToken)) {
		if (parseOptional(Kind::AsKeyword)) {
			exportClause = parseNamespaceExport(namespaceExportPos);
		}
		parseExpected(Kind::FromKeyword);
		moduleSpecifier = parseModuleSpecifier();
	} else {
		exportClause = parseNamedExports();
		// It is not uncommon to accidentally omit the 'from' keyword.
		// Additionally, in editing scenarios, the 'from' keyword can be parsed
		// as a named export when the export clause is unterminated (i.e.
		// `export { from "moduleName";`) If we don't have a 'from' keyword,
		// see if we have a string literal such that ASI won't take effect.
		if (token == Kind::FromKeyword ||
		    (token == Kind::StringLiteral && !hasPrecedingLineBreak())) {
			parseExpected(Kind::FromKeyword);
			moduleSpecifier = parseModuleSpecifier();
		}
	}
	if (moduleSpecifier != nullptr &&
	    (token == Kind::WithKeyword || token == Kind::AssertKeyword) &&
	    !hasPrecedingLineBreak()) {
		if (token == Kind::AssertKeyword) {
			parseErrorAtCurrentToken(
				Import_assertions_have_been_replaced_by_import_attributes_Use_with_instead_of_assert);
		}
		attributes = parseImportAttributes(token, false);
	}
	parseSemicolon();
	contextFlags = saveContextFlags;
	statementHasAwaitIdentifier = saveHasAwaitIdentifier;
	Node* result = finishNode(
		factory.newExportDeclaration(modifiers, isTypeOnly, exportClause,
		                             moduleSpecifier, attributes),
		pos);
	withJSDoc(result, jsdoc);
	checkJSSyntax(result);
	return result;
}

Node* Parser::parseNamespaceExport(int pos) {
	auto [exportName, ok] = parseModuleExportName(false);
	(void)ok;
	return finishNode(factory.newNamespaceExport(exportName), pos);
}

Node* Parser::parseNamedExports() {
	int pos = nodePos();
	// NamedImports:
	//  { }
	//  { ImportsList }
	//  { ImportsList, }
	NodeList* exports = parseBracketedList(
		PCImportOrExportSpecifiers, &Parser::parseExportSpecifier,
		Kind::OpenBraceToken, Kind::CloseBraceToken);
	return finishNode(factory.newNamedExports(exports), pos);
}

Node* Parser::parseExportSpecifier() {
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	auto [isTypeOnly, propertyName, name] =
		parseImportOrExportSpecifier(Kind::ExportSpecifier);
	Node* result = finishNode(
		factory.newExportSpecifier(isTypeOnly, propertyName, name), pos);
	withJSDoc(result, jsdoc);
	checkJSSyntax(result);
	return result;
}

// TYPES

Node* Parser::parseType() {
	NodeFlags saveContextFlags = contextFlags;
	setContextFlags(NodeFlagsTypeExcludesFlags, false);
	Node* typeNode;
	if (isStartOfFunctionTypeOrConstructorType()) {
		typeNode = parseFunctionOrConstructorType();
	} else {
		int pos = nodePos();
		typeNode = parseUnionTypeOrHigher();
		if (!inDisallowConditionalTypesContext() &&
		    !hasPrecedingLineBreak() && parseOptional(Kind::ExtendsKeyword)) {
			// The type following 'extends' is not permitted to be another
			// conditional type
			Node* extendsType =
				doInContext(NodeFlagsDisallowConditionalTypesContext, true,
				            &Parser::parseType);
			parseExpected(Kind::QuestionToken);
			Node* trueType =
				doInContext(NodeFlagsDisallowConditionalTypesContext, false,
				            &Parser::parseType);
			parseExpected(Kind::ColonToken);
			Node* falseType =
				doInContext(NodeFlagsDisallowConditionalTypesContext, false,
				            &Parser::parseType);
			Node* conditionalType = factory.newConditionalTypeNode(
				typeNode, extendsType, trueType, falseType);
			finishNode(conditionalType, pos);
			typeNode = conditionalType;
		}
	}
	contextFlags = saveContextFlags;
	return typeNode;
}

Node* Parser::parseUnionTypeOrHigher() {
	return parseUnionOrIntersectionType(Kind::BarToken,
	                                    &Parser::parseIntersectionTypeOrHigher);
}

Node* Parser::parseIntersectionTypeOrHigher() {
	return parseUnionOrIntersectionType(Kind::AmpersandToken,
	                                    &Parser::parseTypeOperatorOrHigher);
}

Node* Parser::parseUnionOrIntersectionType(
	Kind operator_, Node* (Parser::*parseConstituentType)()) {
	int pos = nodePos();
	bool isUnionType = operator_ == Kind::BarToken;
	bool hasLeadingOperator = parseOptional(operator_);
	Node* typeNode;
	if (hasLeadingOperator) {
		typeNode = parseFunctionOrConstructorTypeToError(
			isUnionType, parseConstituentType);
	} else {
		typeNode = (this->*parseConstituentType)();
	}
	if (token == operator_ || hasLeadingOperator) {
		std::vector<Node*> types;
		types.reserve(8);
		types.push_back(typeNode);
		while (parseOptional(operator_)) {
			types.push_back(parseFunctionOrConstructorTypeToError(
				isUnionType, parseConstituentType));
		}
		typeNode = createUnionOrIntersectionTypeNode(
			operator_,
			newNodeList(TextRange{pos, nodePos()}, types));
		finishNode(typeNode, pos);
	}
	return typeNode;
}

Node* Parser::createUnionOrIntersectionTypeNode(Kind operator_,
                                                NodeList* types) {
	switch (operator_) {
	case Kind::BarToken:
		return factory.newUnionTypeNode(types);
	case Kind::AmpersandToken:
		return factory.newIntersectionTypeNode(types);
	default:
		TSC_UNREACHABLE(
			"Unhandled case in createUnionOrIntersectionType");
	}
}

Node* Parser::parseTypeOperatorOrHigher() {
	Kind operator_ = token;
	switch (operator_) {
	case Kind::KeyOfKeyword:
	case Kind::UniqueKeyword:
	case Kind::ReadonlyKeyword:
		return parseTypeOperator(operator_);
	case Kind::InferKeyword:
		return parseInferType();
	default:
		break;
	}
	return doInContext(NodeFlagsDisallowConditionalTypesContext, false,
	                   &Parser::parsePostfixTypeOrHigher);
}

Node* Parser::parseTypeOperator(Kind operator_) {
	int pos = nodePos();
	parseExpected(operator_);
	return finishNode(factory.newTypeOperatorNode(
			                  operator_, parseTypeOperatorOrHigher()),
	                  pos);
}

Node* Parser::parseInferType() {
	int pos = nodePos();
	parseExpected(Kind::InferKeyword);
	return finishNode(factory.newInferTypeNode(parseTypeParameterOfInferType()),
	                  pos);
}

Node* Parser::parseTypeParameterOfInferType() {
	int pos = nodePos();
	Node* name = parseIdentifier();
	Node* constraint = tryParseConstraintOfInferType();
	return finishNode(
		factory.newTypeParameterDeclaration(nullptr, name, constraint,
		                                    nullptr, nullptr),
		pos);
}

Node* Parser::tryParseConstraintOfInferType() {
	ParserState state = mark();
	if (parseOptional(Kind::ExtendsKeyword)) {
		Node* constraint =
			doInContext(NodeFlagsDisallowConditionalTypesContext, true,
			            &Parser::parseType);
		if (inDisallowConditionalTypesContext() ||
		    token != Kind::QuestionToken) {
			return constraint;
		}
	}
	rewind(state);
	return nullptr;
}

Node* Parser::parsePostfixTypeOrHigher() {
	int pos = nodePos();
	Node* typeNode = parseNonArrayType();
	while (!hasPrecedingLineBreak()) {
		switch (token) {
		case Kind::ExclamationToken:
			nextToken();
			typeNode = finishNode(factory.newJSDocNonNullableType(typeNode),
			                      pos);
			break;
		case Kind::QuestionToken:
			// If next token is start of a type we have a conditional type
			if (lookAhead(&Parser::nextIsStartOfType)) {
				return typeNode;
			}
			nextToken();
			typeNode =
				finishNode(factory.newJSDocNullableType(typeNode), pos);
			break;
		case Kind::OpenBracketToken:
			parseExpected(Kind::OpenBracketToken);
			if (isStartOfType(false)) {
				Node* indexType = parseType();
				parseExpected(Kind::CloseBracketToken);
				typeNode = finishNode(
					factory.newIndexedAccessTypeNode(typeNode, indexType),
					pos);
			} else {
				parseExpected(Kind::CloseBracketToken);
				typeNode = finishNode(factory.newArrayTypeNode(typeNode),
				                      pos);
			}
			break;
		default:
			return typeNode;
		}
	}
	return typeNode;
}

bool Parser::nextIsStartOfType() {
	nextToken();
	return isStartOfType(false);
}

Node* Parser::parseNonArrayType() {
	switch (token) {
	case Kind::AnyKeyword:
	case Kind::UnknownKeyword:
	case Kind::StringKeyword:
	case Kind::NumberKeyword:
	case Kind::BigIntKeyword:
	case Kind::SymbolKeyword:
	case Kind::BooleanKeyword:
	case Kind::UndefinedKeyword:
	case Kind::NeverKeyword:
	case Kind::ObjectKeyword: {
		ParserState state = mark();
		Node* keywordTypeNode = parseKeywordTypeNode();
		// If these are followed by a dot then parse these out as a dotted type
		// reference instead
		if (token != Kind::DotToken) {
			return keywordTypeNode;
		}
		rewind(state);
		return parseTypeReference();
	}
	case Kind::AsteriskEqualsToken:
		// If there is '*=', treat it as * followed by postfix =
		scanner->reScanAsteriskEqualsToken();
		[[fallthrough]];
	case Kind::AsteriskToken:
		return parseJSDocAllType();
	case Kind::QuestionQuestionToken:
		// If there is '??', treat it as prefix-'?' in JSDoc type.
		scanner->reScanQuestionToken();
		[[fallthrough]];
	case Kind::QuestionToken:
		return parseJSDocNullableType();
	case Kind::ExclamationToken:
		return parseJSDocNonNullableType();
	case Kind::NoSubstitutionTemplateLiteral:
	case Kind::StringLiteral:
	case Kind::NumericLiteral:
	case Kind::BigIntLiteral:
	case Kind::TrueKeyword:
	case Kind::FalseKeyword:
	case Kind::NullKeyword:
		return parseLiteralTypeNode(false);
	case Kind::MinusToken:
		if (lookAhead(&Parser::nextTokenIsNumericOrBigIntLiteral)) {
			return parseLiteralTypeNode(true);
		}
		return parseTypeReference();
	case Kind::VoidKeyword:
		return parseKeywordTypeNode();
	case Kind::ThisKeyword: {
		Node* thisKeyword = parseThisTypeNode();
		if (token == Kind::IsKeyword && !hasPrecedingLineBreak()) {
			return parseThisTypePredicate(thisKeyword);
		}
		return thisKeyword;
	}
	case Kind::TypeOfKeyword:
		if (lookAhead(&Parser::nextIsStartOfTypeOfImportType)) {
			return parseImportType();
		}
		return parseTypeQuery();
	case Kind::OpenBraceToken:
		if (lookAhead(&Parser::nextIsStartOfMappedType)) {
			return parseMappedType();
		}
		return parseTypeLiteral();
	case Kind::OpenBracketToken:
		return parseTupleType();
	case Kind::OpenParenToken:
		return parseParenthesizedType();
	case Kind::ImportKeyword:
		return parseImportType();
	case Kind::AssertsKeyword:
		if (lookAhead(
		        &Parser::nextTokenIsIdentifierOrKeywordOnSameLine)) {
			return parseAssertsTypePredicate();
		}
		return parseTypeReference();
	case Kind::TemplateHead:
		return parseTemplateType();
	default:
		return parseTypeReference();
	}
}

Node* Parser::parseKeywordTypeNode() {
	int pos = nodePos();
	Node* result = factory.newKeywordTypeNode(token);
	nextToken();
	return finishNode(result, pos);
}

Node* Parser::parseThisTypeNode() {
	int pos = nodePos();
	nextToken();
	return finishNode(factory.newThisTypeNode(), pos);
}

Node* Parser::parseThisTypePredicate(Node* lhs) {
	nextToken();
	return finishNode(
		factory.newTypePredicateNode(nullptr, lhs, parseType()), lhs->pos());
}

Node* Parser::parseJSDocAllType() {
	int pos = nodePos();
	nextToken();
	return finishNode(factory.newJSDocAllType(), pos);
}

Node* Parser::parseJSDocNonNullableType() {
	int pos = nodePos();
	nextToken();
	return finishNode(
		factory.newJSDocNonNullableType(parseTypeOperatorOrHigher()), pos);
}

Node* Parser::parseJSDocNullableType() {
	int pos = nodePos();
	// skip the ?
	nextToken();
	return finishNode(
		factory.newJSDocNullableType(parseTypeOperatorOrHigher()), pos);
}

Node* Parser::parseJSDocType() {
	scanner->setSkipJSDocLeadingAsterisks(true);
	int pos = nodePos();

	bool hasDotDotDot = parseOptional(Kind::DotDotDotToken);
	Node* t = parseTypeOrTypePredicate();
	scanner->setSkipJSDocLeadingAsterisks(false);
	if (hasDotDotDot) {
		t = finishNode(factory.newJSDocVariadicType(t), pos);
	}
	if (token == Kind::EqualsToken) {
		nextToken();
		return finishNode(factory.newJSDocOptionalType(t), pos);
	}
	return t;
}

Node* Parser::parseLiteralTypeNode(bool negative) {
	int pos = nodePos();
	if (negative) {
		nextToken();
	}
	Node* expression;
	if (token == Kind::TrueKeyword || token == Kind::FalseKeyword ||
	    token == Kind::NullKeyword) {
		expression = parseKeywordExpression();
	} else {
		expression = parseLiteralExpression();
	}
	if (negative) {
		expression = finishNode(
			factory.newPrefixUnaryExpression(Kind::MinusToken, expression),
			pos);
	}
	return finishNode(factory.newLiteralTypeNode(expression), pos);
}

Node* Parser::parseTypeReference() {
	int pos = nodePos();
	return finishNode(
		factory.newTypeReferenceNode(parseEntityNameOfTypeReference(),
		                             parseTypeArgumentsOfTypeReference()),
		pos);
}

Node* Parser::parseEntityNameOfTypeReference() {
	return parseEntityName(true, false, Type_expected);
}

Node* Parser::parseEntityName(bool allowReservedWords, bool allowPrivateName,
                              const DiagnosticMessage* diagnosticMessage) {
	int pos = nodePos();
	Node* entity;
	if (allowReservedWords) {
		entity = parseIdentifierNameWithDiagnostic(diagnosticMessage);
	} else {
		entity = parseIdentifierWithDiagnostic(diagnosticMessage, nullptr);
	}
	while (parseOptional(Kind::DotToken)) {
		if (token == Kind::LessThanToken) {
			// The entity is part of a JSDoc-style generic. We will use the gap
			// between `typeName` and `typeArguments` to report it as a grammar
			// error in the checker.
			break;
		}
		entity = finishNode(
			factory.newQualifiedName(
				entity, parseRightSideOfDot(
				            allowReservedWords, allowPrivateName, true)),
			pos);
	}
	return entity;
}

Node* Parser::parseRightSideOfDot(
	bool allowIdentifierNames, bool allowPrivateIdentifiers,
	bool allowUnicodeEscapeSequenceInIdentifierName) {
	// Technically a keyword is valid here as all identifiers and keywords are
	// identifier names. However, often we'll encounter this in error situations
	// when the identifier or keyword is actually starting another valid
	// construct.
	//
	// So, we check for the following specific case:
	//
	//      name.
	//      identifierOrKeyword identifierNameOrKeyword
	//
	// Note: the newlines are important here. For example, if that above code
	// were rewritten into:
	//
	//      name.identifierOrKeyword
	//      identifierNameOrKeyword
	//
	// Then we would consider it valid. That's because ASI would take effect
	// and the code would be implicitly: "name.identifierOrKeyword;
	// identifierNameOrKeyword". In the first case though, ASI will not take
	// effect because there is not a line terminator after the identifier or
	// keyword.
	if (hasPrecedingLineBreak() && tokenIsIdentifierOrKeyword(token) &&
	    lookAhead(&Parser::nextTokenIsIdentifierOrKeywordOnSameLine)) {
		// Report that we need an identifier. However, report it right after
		// the dot, and not on the next token. This is because the next token
		// might actually be an identifier and the error would be quite
		// confusing.
		parseErrorAt(nodePos(), nodePos(), Identifier_expected);
		return createMissingIdentifier();
	}
	if (token == Kind::PrivateIdentifier) {
		Node* node = parsePrivateIdentifier();
		if (allowPrivateIdentifiers) {
			return node;
		}
		parseErrorAt(nodePos(), nodePos(), Identifier_expected);
		return createMissingIdentifier();
	}
	if (allowIdentifierNames) {
		if (allowUnicodeEscapeSequenceInIdentifierName) {
			return parseIdentifierName();
		}
		return parseIdentifierNameErrorOnUnicodeEscapeSequence();
	}
	bool saveHasAwaitIdentifier = statementHasAwaitIdentifier;
	Node* id = parseIdentifier();
	statementHasAwaitIdentifier = saveHasAwaitIdentifier;
	return id;
}

Node* Parser::newIdentifier(std::string text) {
	identifierCount++;
	Node* id = factory.newIdentifier(text);
	if (text == "await") {
		statementHasAwaitIdentifier = true;
	}
	return id;
}

Node* Parser::createMissingIdentifier() {
	return finishNode(newIdentifier(""), nodePos());
}

Node* Parser::parsePrivateIdentifier() {
	int pos = nodePos();
	std::string text(scanner->tokenValue());
	nextToken();
	return finishNode(
		factory.newPrivateIdentifier(std::move(text)), pos);
}

Kind Parser::reScanLessThanToken() {
	token = scanner->reScanLessThanToken();
	return token;
}

Kind Parser::reScanGreaterThanToken() {
	token = scanner->reScanGreaterThanToken();
	return token;
}

Kind Parser::reScanSlashToken() {
	token = scanner->reScanSlashToken();
	return token;
}

Kind Parser::reScanTemplateToken(bool isTaggedTemplate) {
	token = scanner->reScanTemplateToken(isTaggedTemplate);
	return token;
}

NodeList* Parser::parseTypeArgumentsOfTypeReference() {
	if (!hasPrecedingLineBreak() &&
	    reScanLessThanToken() == Kind::LessThanToken) {
		return parseTypeArguments();
	}
	return nullptr;
}

NodeList* Parser::parseTypeArguments() {
	if (token == Kind::LessThanToken) {
		return parseBracketedList(PCTypeArguments, &Parser::parseType,
		                          Kind::LessThanToken,
		                          Kind::GreaterThanToken);
	}
	return nullptr;
}

bool Parser::nextIsStartOfTypeOfImportType() {
	nextToken();
	return token == Kind::ImportKeyword;
}

Node* Parser::parseImportType() {
	sourceFlags |= NodeFlagsPossiblyContainsDynamicImport;
	int pos = nodePos();
	bool isTypeOf = parseOptional(Kind::TypeOfKeyword);
	parseExpected(Kind::ImportKeyword);
	parseExpected(Kind::OpenParenToken);
	Node* typeNode = parseType();
	Node* attributes = nullptr;
	if (parseOptional(Kind::CommaToken)) {
		int openBracePosition = scanner->tokenStart();
		parseExpected(Kind::OpenBraceToken);
		Kind currentToken = token;
		if (currentToken == Kind::WithKeyword ||
		    currentToken == Kind::AssertKeyword) {
			if (currentToken == Kind::AssertKeyword) {
				parseErrorAtCurrentToken(
					Import_assertions_have_been_replaced_by_import_attributes_Use_with_instead_of_assert);
			}
			nextToken();
		} else {
			parseErrorAtCurrentToken(
				X_0_expected,
				{std::string(tokenToString(Kind::WithKeyword))});
		}
		parseExpected(Kind::ColonToken);
		attributes = parseImportAttributes(currentToken, true);
		parseOptional(Kind::CommaToken);
		if (!parseExpected(Kind::CloseBraceToken)) {
			if (!diagnostics.empty()) {
				Diagnostic* lastDiagnostic = diagnostics.back();
				if (lastDiagnostic->code == X_0_expected->code) {
					Diagnostic* related = newDetachedDiagnostic(
						TextRange{openBracePosition, openBracePosition},
						The_parser_expected_to_find_a_1_to_match_the_0_token_here,
						{"{", "}"});
					lastDiagnostic->messageChain.push_back(related);
				}
			}
		}
	}
	parseExpected(Kind::CloseParenToken);
	Node* qualifier = nullptr;
	if (parseOptional(Kind::DotToken)) {
		qualifier = parseEntityNameOfTypeReference();
	}
	NodeList* typeArguments = parseTypeArgumentsOfTypeReference();
	return finishNode(
		factory.newImportTypeNode(isTypeOf, typeNode, attributes, qualifier,
		                          typeArguments),
		pos);
}

Node* Parser::parseImportAttribute() {
	int pos = nodePos();
	Node* name = nullptr;
	if (tokenIsIdentifierOrKeyword(token)) {
		name = parseIdentifierName();
	} else if (token == Kind::StringLiteral) {
		name = parseLiteralExpression();
	}
	if (name != nullptr) {
		parseExpected(Kind::ColonToken);
	} else {
		parseErrorAtCurrentToken(Identifier_or_string_literal_expected);
	}
	Node* value = parseAssignmentExpressionOrHigher();
	return finishNode(factory.newImportAttribute(name, value), pos);
}

Node* Parser::parseImportAttributes(Kind token_, bool skipKeyword) {
	int pos = nodePos();
	if (!skipKeyword) {
		parseExpected(token_);
	}
	NodeList* elements;
	bool multiLine = false;
	int openBracePosition = scanner->tokenStart();
	if (parseExpected(Kind::OpenBraceToken)) {
		multiLine = hasPrecedingLineBreak();
		elements = parseDelimitedList(PCImportAttributes,
		                              &Parser::parseImportAttribute);
		if (!parseExpected(Kind::CloseBraceToken)) {
			if (!diagnostics.empty()) {
				Diagnostic* lastDiagnostic = diagnostics.back();
				if (lastDiagnostic->code == X_0_expected->code) {
					Diagnostic* related = newDetachedDiagnostic(
						TextRange{openBracePosition, openBracePosition},
						The_parser_expected_to_find_a_1_to_match_the_0_token_here,
						{"{", "}"});
					lastDiagnostic->messageChain.push_back(related);
				}
			}
		}
	} else {
		elements = parseEmptyNodeList();
	}
	return finishNode(factory.newImportAttributes(token_, elements, multiLine),
	                  pos);
}

Node* Parser::parseTypeQuery() {
	int pos = nodePos();
	parseExpected(Kind::TypeOfKeyword);
	Node* entityName = parseEntityName(true, true, nullptr);
	// Make sure we perform ASI to prevent parsing the next line's type
	// arguments as part of an instantiation expression
	NodeList* typeArguments = nullptr;
	if (!hasPrecedingLineBreak()) {
		typeArguments = parseTypeArguments();
	}
	return finishNode(factory.newTypeQueryNode(entityName, typeArguments),
	                  pos);
}

bool Parser::nextIsStartOfMappedType() {
	nextToken();
	if (token == Kind::PlusToken || token == Kind::MinusToken) {
		return nextToken() == Kind::ReadonlyKeyword;
	}
	if (token == Kind::ReadonlyKeyword) {
		nextToken();
	}
	return token == Kind::OpenBracketToken && nextTokenIsIdentifier() &&
	       nextToken() == Kind::InKeyword;
}

Node* Parser::parseMappedType() {
	int pos = nodePos();
	parseExpected(Kind::OpenBraceToken);
	Node* readonlyToken = nullptr;  // ReadonlyKeyword | PlusToken | MinusToken
	if (token == Kind::ReadonlyKeyword || token == Kind::PlusToken ||
	    token == Kind::MinusToken) {
		readonlyToken = parseTokenNode();
		if (readonlyToken->kind != Kind::ReadonlyKeyword) {
			parseExpected(Kind::ReadonlyKeyword);
		}
	}
	parseExpected(Kind::OpenBracketToken);
	Node* typeParameter = parseMappedTypeParameter();
	Node* nameType = nullptr;
	if (parseOptional(Kind::AsKeyword)) {
		nameType = parseType();
	}
	parseExpected(Kind::CloseBracketToken);
	Node* questionToken = nullptr;  // QuestionToken | PlusToken | MinusToken
	if (token == Kind::QuestionToken || token == Kind::PlusToken ||
	    token == Kind::MinusToken) {
		questionToken = parseTokenNode();
		if (questionToken->kind != Kind::QuestionToken) {
			parseExpected(Kind::QuestionToken);
		}
	}
	Node* typeNode = parseTypeAnnotation();
	parseSemicolon();
	NodeList* members = parseList(PCTypeMembers, &Parser::parseTypeMember);
	parseExpected(Kind::CloseBraceToken);
	return finishNode(factory.newMappedTypeNode(readonlyToken, typeParameter,
	                                            nameType, questionToken,
	                                            typeNode, members),
	                  pos);
}

Node* Parser::parseMappedTypeParameter() {
	int pos = nodePos();
	Node* name = parseIdentifierName();
	parseExpected(Kind::InKeyword);
	Node* typeNode = parseType();
	return finishNode(
		factory.newTypeParameterDeclaration(nullptr, name, typeNode, nullptr,
		                                    nullptr),
		pos);
}

Node* Parser::parseTypeMember() {
	if (token == Kind::OpenParenToken || token == Kind::LessThanToken) {
		return parseSignatureMember(Kind::CallSignature);
	}
	if (token == Kind::NewKeyword &&
	    lookAhead(&Parser::nextTokenIsOpenParenOrLessThan)) {
		return parseSignatureMember(Kind::ConstructSignature);
	}
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	ModifierList* modifiers = parseModifiers();
	if (parseContextualModifier(Kind::GetKeyword)) {
		return parseAccessorDeclaration(pos, jsdoc, modifiers,
		                                Kind::GetAccessor, ParseFlagsType);
	}
	if (parseContextualModifier(Kind::SetKeyword)) {
		return parseAccessorDeclaration(pos, jsdoc, modifiers,
		                                Kind::SetAccessor, ParseFlagsType);
	}
	if (isIndexSignature()) {
		return parseIndexSignatureDeclaration(pos, jsdoc, modifiers);
	}
	return parsePropertyOrMethodSignature(pos, jsdoc, modifiers);
}

bool Parser::nextTokenIsOpenParenOrLessThan() {
	nextToken();
	return token == Kind::OpenParenToken || token == Kind::LessThanToken;
}

Node* Parser::parseSignatureMember(Kind kind) {
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	if (kind == Kind::ConstructSignature) {
		parseExpected(Kind::NewKeyword);
	}
	NodeList* typeParameters = parseTypeParameters();
	NodeList* parameters = parseParameters(ParseFlagsType);

	Node* typeNode = parseReturnType(Kind::ColonToken, true);
	parseTypeMemberSemicolon();
	Node* result;
	if (kind == Kind::CallSignature) {
		result = factory.newCallSignatureDeclaration(typeParameters,
		                                             parameters, typeNode);
	} else {
		result = factory.newConstructSignatureDeclaration(typeParameters,
		                                                  parameters, typeNode);
	}
	finishNode(result, pos);
	withJSDoc(result, jsdoc);
	return result;
}

NodeList* Parser::parseTypeParameters() {
	if (token == Kind::LessThanToken) {
		return parseBracketedList(PCTypeParameters,
		                          &Parser::parseTypeParameter,
		                          Kind::LessThanToken, Kind::GreaterThanToken);
	}
	return nullptr;
}

Node* Parser::parseTypeParameter() {
	int pos = nodePos();
	ModifierList* modifiers = parseModifiersEx(false, true, false);
	Node* name = parseIdentifier();
	Node* constraint = nullptr;
	Node* expression = nullptr;
	if (parseOptional(Kind::ExtendsKeyword)) {
		// It's not uncommon for people to write improper constraints to a
		// generic. If the user writes a constraint that is an expression and
		// not an actual type, then parse it out as an expression (so we can
		// recover well), but report that a type is needed instead.
		if (isStartOfType(false) || !isStartOfExpression()) {
			constraint = parseType();
		} else {
			// It was not a type, and it looked like an expression. Parse out
			// an expression here so we recover well. Note: it is important
			// that we call parseUnaryExpression and not parseExpression here.
			// If the user has:
			//
			//      <T extends "">
			//
			// We do *not* want to consume the `>` as we're consuming the
			// expression for "".
			expression = parseUnaryExpressionOrHigher();
		}
	}
	Node* defaultType = nullptr;
	if (parseOptional(Kind::EqualsToken)) {
		defaultType = parseType();
	}
	Node* result = factory.newTypeParameterDeclaration(
		modifiers, name, constraint, expression, defaultType);
	return finishNode(result, pos);
}

NodeList* Parser::parseParameters(ParseFlags flags) {
	// FormalParameters [Yield,Await]: (modified)
	//      [empty]
	//      FormalParameterList[?Yield,Await]
	if (parseExpected(Kind::OpenParenToken)) {
		NodeList* parameters = parseParametersWorker(flags, true);
		parseExpected(Kind::CloseParenToken);
		return parameters;
	}
	return createMissingList();
}

Node* Parser::parseParameterInList() {
	Node* parameter = parseParameterEx(scratchBool2_, scratchBool_);
	if (parameter != nullptr && (scratchFlags_ & ParseFlagsType) == 0) {
		checkJSSyntax(parameter);
	}
	return parameter;
}

NodeList* Parser::parseParametersWorker(ParseFlags flags,
                                        bool allowAmbiguity) {
	// FormalParameter[Yield,Await]: (modified) BindingElement[?Yield,Await]
	bool inAwaitContext = (contextFlags & NodeFlagsAwaitContext) != 0;
	NodeFlags saveContextFlags = contextFlags;
	setContextFlags(NodeFlagsYieldContext, (flags & ParseFlagsYield) != 0);
	setContextFlags(NodeFlagsAwaitContext, (flags & ParseFlagsAwait) != 0);
	bool saveScratchBool = scratchBool_;
	bool saveScratchBool2 = scratchBool2_;
	int saveScratchFlags = scratchFlags_;
	scratchBool_ = allowAmbiguity;
	scratchBool2_ = inAwaitContext;
	scratchFlags_ = flags;
	NodeList* parameters =
		parseDelimitedList(PCParameters, &Parser::parseParameterInList);
	scratchBool_ = saveScratchBool;
	scratchBool2_ = saveScratchBool2;
	scratchFlags_ = saveScratchFlags;
	contextFlags = saveContextFlags;
	return parameters;
}

Node* Parser::parseParameter() {
	return parseParameterEx(false, true);
}

Node* Parser::parseParameterEx(bool inOuterAwaitContext,
                               bool allowAmbiguity) {
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	// FormalParameter [Yield,Await]:
	//      BindingElement[?Yield,?Await]
	// Decorators are parsed in the outer [Await] context, the rest of the
	// parameter is parsed in the function's [Await] context.
	NodeFlags saveContextFlags = contextFlags;
	setContextFlags(NodeFlagsAwaitContext, inOuterAwaitContext);
	ModifierList* modifiers = parseModifiersEx(true, false, false);
	contextFlags = saveContextFlags;
	if (token == Kind::ThisKeyword) {
		Node* result = factory.newParameterDeclaration(
			modifiers, nullptr /*dotDotDotToken*/,
			createIdentifier(true), nullptr /*questionToken*/,
			parseTypeAnnotation(), nullptr /*initializer*/);
		if (modifiers != nullptr) {
			parseErrorAtRange(
				modifiers->nodes[0]->loc,
				Neither_decorators_nor_modifiers_may_be_applied_to_this_parameters);
		}
		withJSDoc(finishNode(result, pos), jsdoc);
		return result;
	}
	Node* dotDotDotToken = parseOptionalToken(Kind::DotDotDotToken);
	if (!allowAmbiguity && !isParameterNameStart()) {
		return nullptr;
	}
	Node* result = factory.newParameterDeclaration(
		modifiers, dotDotDotToken, parseNameOfParameter(modifiers),
		parseOptionalToken(Kind::QuestionToken), parseTypeAnnotation(),
		parseInitializer());
	withJSDoc(finishNode(result, pos), jsdoc);
	return result;
}

bool Parser::isParameterNameStart() {
	// Be permissive about await and yield by calling isBindingIdentifier
	// instead of isIdentifier; disallowing them during a speculative parse
	// leads to many more follow-on errors than allowing the function to parse
	// then later complaining about the use of the keywords.
	return isBindingIdentifier() || token == Kind::OpenBracketToken ||
	       token == Kind::OpenBraceToken;
}

Node* Parser::parseNameOfParameter(ModifierList* modifiers) {
	// FormalParameter [Yield,Await]:
	//      BindingElement[?Yield,?Await]
	Node* name = parseIdentifierOrPatternWithDiagnostic(
		Private_identifiers_cannot_be_used_as_parameters);
	if (name->loc.len() == 0 && modifiers == nullptr &&
	    isModifierKind(token)) {
		// in cases like
		// 'use strict'
		// function foo(static)
		// isParameter('static') == true, because of isModifier('static')
		// however 'static' is not a legal identifier in a strict mode.
		// so result of this function will be Parameter (flags = 0, name =
		// missing, type = undefined, initializer = undefined) and current
		// token will not change => parsing of the enclosing parameter list
		// will last till the end of time (or OOM) to avoid this we'll advance
		// cursor to the next token.
		nextToken();
	}
	return name;
}

Node* Parser::parseReturnType(Kind returnToken, bool isType) {
	if (shouldParseReturnType(returnToken, isType)) {
		return doInContext(NodeFlagsDisallowConditionalTypesContext, false,
		                   &Parser::parseTypeOrTypePredicate);
	}
	return nullptr;
}

bool Parser::shouldParseReturnType(Kind returnToken, bool isType) {
	if (returnToken == Kind::EqualsGreaterThanToken) {
		parseExpected(returnToken);
		return true;
	} else if (parseOptional(Kind::ColonToken)) {
		return true;
	} else if (isType && token == Kind::EqualsGreaterThanToken) {
		// This is easy to get backward, especially in type contexts, so parse
		// the type anyway
		parseErrorAtCurrentToken(
			X_0_expected, {std::string(tokenToString(Kind::ColonToken))});
		nextToken();
		return true;
	}
	return false;
}

Node* Parser::parseTypeOrTypePredicate() {
	if (isIdentifier()) {
		ParserState state = mark();
		int pos = nodePos();
		Node* id = parseIdentifier();
		if (token == Kind::IsKeyword && !hasPrecedingLineBreak()) {
			nextToken();
			return finishNode(
				factory.newTypePredicateNode(nullptr, id, parseType()),
				pos);
		}
		rewind(state);
	}
	return parseType();
}

void Parser::parseTypeMemberSemicolon() {
	// We allow type members to be separated by commas or (possibly ASI)
	// semicolons. First check if it was a comma. If so, we're done with the
	// member.
	if (parseOptional(Kind::CommaToken)) {
		return;
	}
	// Didn't have a comma. We must have a (possible ASI) semicolon.
	parseSemicolon();
}

Node* Parser::parseAccessorDeclaration(int pos, JSDocScannerInfo jsdoc,
                                       ModifierList* modifiers, Kind kind,
                                       ParseFlags flags) {
	Node* name = parsePropertyName();
	NodeList* typeParameters = parseTypeParameters();
	NodeList* parameters = parseParameters(ParseFlagsNone);
	Node* returnType = parseReturnType(Kind::ColonToken, false);
	Node* body = parseFunctionBlockOrSemicolon(flags, nullptr);
	Node* result;
	// Keep track of `typeParameters` (for both) and `type` (for setters) if
	// they were parsed those indicate grammar errors
	if (kind == Kind::GetAccessor) {
		result = factory.newGetAccessorDeclaration(
			modifiers, name, typeParameters, parameters, returnType, nullptr,
			body);
	} else {
		result = factory.newSetAccessorDeclaration(
			modifiers, name, typeParameters, parameters, returnType, nullptr,
			body);
	}
	withJSDoc(finishNode(result, pos), jsdoc);
	if ((flags & ParseFlagsType) == 0) {
		checkJSSyntax(result);
	}
	return result;
}

Node* Parser::parsePropertyName() {
	bool saveHasAwaitIdentifier = statementHasAwaitIdentifier;
	Node* prop = parsePropertyNameWorker(true);
	statementHasAwaitIdentifier = saveHasAwaitIdentifier;
	return prop;
}

Node* Parser::parsePropertyNameWorker(bool allowComputedPropertyNames) {
	if (token == Kind::StringLiteral || token == Kind::NumericLiteral ||
	    token == Kind::BigIntLiteral) {
		return parseLiteralExpression();
	}
	if (allowComputedPropertyNames && token == Kind::OpenBracketToken) {
		return parseComputedPropertyName();
	}
	if (token == Kind::PrivateIdentifier) {
		return parsePrivateIdentifier();
	}
	return parseIdentifierName();
}

Node* Parser::parseComputedPropertyName() {
	// PropertyName [Yield]:
	//      LiteralPropertyName
	//      ComputedPropertyName[?Yield]
	int pos = nodePos();
	parseExpected(Kind::OpenBracketToken);
	// We parse any expression (including a comma expression). But the grammar
	// says that only an assignment expression is allowed, so the grammar
	// checker will error if it sees a comma expression.
	Node* expression = parseExpressionAllowIn();
	parseExpected(Kind::CloseBracketToken);
	return finishNode(factory.newComputedPropertyName(expression), pos);
}

Node* Parser::parseFunctionBlockOrSemicolon(
	ParseFlags flags, const DiagnosticMessage* diagnosticMessage) {
	if (token != Kind::OpenBraceToken) {
		if ((flags & ParseFlagsType) != 0) {
			parseTypeMemberSemicolon();
			return nullptr;
		}
		if (canParseSemicolon()) {
			parseSemicolon();
			return nullptr;
		}
	}
	return parseFunctionBlock(flags, diagnosticMessage);
}

Node* Parser::parseFunctionBlock(ParseFlags flags,
                                 const DiagnosticMessage* diagnosticMessage) {
	NodeFlags saveContextFlags = contextFlags;
	bool saveHasAwaitIdentifier = statementHasAwaitIdentifier;
	setContextFlags(NodeFlagsYieldContext, (flags & ParseFlagsYield) != 0);
	setContextFlags(NodeFlagsAwaitContext, (flags & ParseFlagsAwait) != 0);
	// We may be in a [Decorator] context when parsing a function expression or
	// arrow function. The body of the function is not in [Decorator] context.
	setContextFlags(NodeFlagsDecoratorContext, false);
	Node* block =
		parseBlock((flags & ParseFlagsIgnoreMissingOpenBrace) != 0,
		           diagnosticMessage);
	contextFlags = saveContextFlags;
	statementHasAwaitIdentifier = saveHasAwaitIdentifier;
	return block;
}

bool Parser::isIndexSignature() {
	return token == Kind::OpenBracketToken &&
	       lookAhead(&Parser::nextIsUnambiguouslyIndexSignature);
}

bool Parser::nextIsUnambiguouslyIndexSignature() {
	// The only allowed sequence is:
	//
	//   [id:
	//
	// However, for error recovery, we also check the following cases:
	//
	//   [...
	//   [id,
	//   [id?,
	//   [id?:
	//   [id?]
	//   [public id
	//   [private id
	//   [protected id
	//   []
	//
	nextToken();
	if (token == Kind::DotDotDotToken || token == Kind::CloseBracketToken) {
		return true;
	}
	if (isModifierKind(token)) {
		nextToken();
		if (isIdentifier()) {
			return true;
		}
	} else if (!isIdentifier()) {
		return false;
	} else {
		// Skip the identifier
		nextToken();
	}
	// A colon signifies a well formed indexer
	// A comma should be a badly formed indexer because comma expressions are
	// not allowed in computed properties.
	if (token == Kind::ColonToken || token == Kind::CommaToken) {
		return true;
	}
	// Question mark could be an indexer with an optional property,
	// or it could be a conditional expression in a computed property.
	if (token != Kind::QuestionToken) {
		return false;
	}
	// If any of the following tokens are after the question mark, it cannot
	// be a conditional expression, so treat it as an indexer.
	nextToken();
	return token == Kind::ColonToken || token == Kind::CommaToken ||
	       token == Kind::CloseBracketToken;
}

Node* Parser::parseIndexSignatureDeclaration(int pos, JSDocScannerInfo jsdoc,
                                             ModifierList* modifiers) {
	NodeList* parameters = parseBracketedList(PCParameters,
	                                          &Parser::parseParameter,
	                                          Kind::OpenBracketToken,
	                                          Kind::CloseBracketToken);
	Node* typeNode = parseTypeAnnotation();
	parseTypeMemberSemicolon();
	Node* result = finishNode(
		factory.newIndexSignatureDeclaration(modifiers, parameters, typeNode),
		pos);
	withJSDoc(result, jsdoc);
	return result;
}

Node* Parser::parsePropertyOrMethodSignature(int pos, JSDocScannerInfo jsdoc,
                                             ModifierList* modifiers) {
	Node* name = parsePropertyName();
	Node* questionToken = parseOptionalToken(Kind::QuestionToken);
	Node* result;
	if (token == Kind::OpenParenToken || token == Kind::LessThanToken) {
		// Method signatures don't exist in expression contexts. So they have
		// neither [Yield] nor [Await]
		NodeList* typeParameters = parseTypeParameters();
		NodeList* parameters = parseParameters(ParseFlagsType);
		Node* returnType = parseReturnType(Kind::ColonToken, true);
		result = factory.newMethodSignatureDeclaration(
			modifiers, name, questionToken, typeParameters, parameters,
			returnType);
	} else {
		Node* typeNode = parseTypeAnnotation();
		// Although type literal properties cannot not have initializers, we
		// attempt to parse an initializer so we can report in the checker
		// that an interface property or type literal property cannot have an
		// initializer.
		Node* initializer = nullptr;
		if (token == Kind::EqualsToken) {
			initializer = parseInitializer();
		}
		result = factory.newPropertySignatureDeclaration(
			modifiers, name, questionToken, typeNode, initializer);
	}
	parseTypeMemberSemicolon();
	withJSDoc(finishNode(result, pos), jsdoc);
	return result;
}

Node* Parser::parseTypeLiteral() {
	int pos = nodePos();
	Node* result = finishNode(
		factory.newTypeLiteralNode(parseObjectTypeMembers()), pos);
	return result;
}

NodeList* Parser::parseObjectTypeMembers() {
	if (parseExpected(Kind::OpenBraceToken)) {
		NodeList* members = parseList(PCTypeMembers, &Parser::parseTypeMember);
		parseExpected(Kind::CloseBraceToken);
		return members;
	}
	return createMissingList();
}

Node* Parser::parseTupleType() {
	int pos = nodePos();
	return finishNode(factory.newTupleTypeNode(parseBracketedList(
			                  PCTupleElementTypes,
			                  &Parser::parseTupleElementNameOrTupleElementType,
			                  Kind::OpenBracketToken,
			                  Kind::CloseBracketToken)),
	                  pos);
}

Node* Parser::parseTupleElementNameOrTupleElementType() {
	if (lookAhead(&Parser::scanStartOfNamedTupleElement)) {
		int pos = nodePos();
		JSDocScannerInfo jsdoc = jsdocScannerInfo();
		Node* dotDotDotToken = parseOptionalToken(Kind::DotDotDotToken);
		Node* name = parseIdentifierName();
		Node* questionToken = parseOptionalToken(Kind::QuestionToken);
		parseExpected(Kind::ColonToken);
		Node* typeNode = parseTupleElementType();
		Node* result = finishNode(
			factory.newNamedTupleMember(dotDotDotToken, name, questionToken,
		                                typeNode),
			pos);
		withJSDoc(result, jsdoc);
		return result;
	}
	return parseTupleElementType();
}

bool Parser::scanStartOfNamedTupleElement() {
	if (token == Kind::DotDotDotToken) {
		return tokenIsIdentifierOrKeyword(nextToken()) &&
		       nextTokenIsColonOrQuestionColon();
	}
	return tokenIsIdentifierOrKeyword(token) &&
	       nextTokenIsColonOrQuestionColon();
}

bool Parser::nextTokenIsColonOrQuestionColon() {
	return nextToken() == Kind::ColonToken ||
	       (token == Kind::QuestionToken && nextToken() == Kind::ColonToken);
}

Node* Parser::parseTupleElementType() {
	int pos = nodePos();
	if (parseOptional(Kind::DotDotDotToken)) {
		return finishNode(factory.newRestTypeNode(parseType()), pos);
	}
	Node* typeNode = parseType();
	if (isJSDocNullableType(typeNode) &&
	    typeNode->pos() == typeNode->type()->pos()) {
		Node* node = factory.newOptionalTypeNode(typeNode->type());
		node->flags = typeNode->flags;
		node->loc = typeNode->loc;
		typeNode->type()->parent = node;
		return node;
	}
	return typeNode;
}

Node* Parser::parseParenthesizedType() {
	int pos = nodePos();
	parseExpected(Kind::OpenParenToken);
	Node* typeNode = parseType();
	parseExpected(Kind::CloseParenToken);
	return finishNode(factory.newParenthesizedTypeNode(typeNode), pos);
}

Node* Parser::parseAssertsTypePredicate() {
	int pos = nodePos();
	Node* assertsModifier = parseExpectedToken(Kind::AssertsKeyword);
	Node* parameterName;
	if (token == Kind::ThisKeyword) {
		parameterName = parseThisTypeNode();
	} else {
		parameterName = parseIdentifier();
	}
	Node* typeNode = nullptr;
	if (parseOptional(Kind::IsKeyword)) {
		typeNode = parseType();
	}
	return finishNode(
		factory.newTypePredicateNode(assertsModifier, parameterName, typeNode),
		pos);
}

Node* Parser::parseTemplateType() {
	int pos = nodePos();
	return finishNode(
		factory.newTemplateLiteralTypeNode(parseTemplateHead(false),
		                                   parseTemplateTypeSpans()),
		pos);
}

Node* Parser::parseTemplateHead(bool isTaggedTemplate) {
	if (!isTaggedTemplate &&
	    (scanner->tokenFlags() & TokenFlagsIsInvalid) != 0) {
		reScanTemplateToken(false);
	}
	int pos = nodePos();
	Node* result = factory.newTemplateHead(
		std::string(scanner->tokenValue()), getTemplateLiteralRawText(2),
		scanner->tokenFlags());
	nextToken();
	return finishNode(result, pos);
}

std::string Parser::getTemplateLiteralRawText(int endLength) {
	std::string_view tokenText = scanner->tokenText();
	if ((scanner->tokenFlags() & TokenFlagsUnterminated) != 0) {
		endLength = 0;
	}
	return std::string(
		tokenText.substr(1, tokenText.size() - 1 - endLength));
}

NodeList* Parser::parseTemplateTypeSpans() {
	int pos = nodePos();
	std::vector<Node*> list;
	for (;;) {
		Node* span = parseTemplateTypeSpan();
		list.push_back(span);
		if (span->as<TemplateLiteralTypeSpan>()->Literal->kind !=
		    Kind::TemplateMiddle) {
			break;
		}
	}
	return newNodeList(TextRange{pos, nodePos()}, list);
}

Node* Parser::parseTemplateTypeSpan() {
	int pos = nodePos();
	return finishNode(factory.newTemplateLiteralTypeSpan(
			                  parseType(), parseLiteralOfTemplateSpan(false)),
	                  pos);
}

Node* Parser::parseLiteralOfTemplateSpan(bool isTaggedTemplate) {
	if (token == Kind::CloseBraceToken) {
		reScanTemplateToken(isTaggedTemplate);
		return parseTemplateMiddleOrTail();
	}
	parseErrorAtCurrentToken(
		X_0_expected, {std::string(tokenToString(Kind::CloseBraceToken))});
	return finishNode(factory.newTemplateTail("", "", TokenFlagsNone),
	                  nodePos());
}

Node* Parser::parseTemplateMiddleOrTail() {
	int pos = nodePos();
	Node* result;
	if (token == Kind::TemplateMiddle) {
		result = factory.newTemplateMiddle(
			std::string(scanner->tokenValue()), getTemplateLiteralRawText(2),
			scanner->tokenFlags());
	} else {
		result = factory.newTemplateTail(
			std::string(scanner->tokenValue()), getTemplateLiteralRawText(1),
			scanner->tokenFlags());
	}
	nextToken();
	return finishNode(result, pos);
}

Node* Parser::parseFunctionOrConstructorTypeToError(
	bool isInUnionType, Node* (Parser::*parseConstituentType)()) {
	// the function type and constructor type shorthand notation
	// are not allowed directly in unions and intersections, but we'll
	// try to parse them gracefully and issue a helpful message.
	if (isStartOfFunctionTypeOrConstructorType()) {
		Node* typeNode = parseFunctionOrConstructorType();
		const DiagnosticMessage* diagnostic;
		if (typeNode->kind == Kind::FunctionType) {
			diagnostic =
				isInUnionType
					? Function_type_notation_must_be_parenthesized_when_used_in_a_union_type
					: Function_type_notation_must_be_parenthesized_when_used_in_an_intersection_type;
		} else {
			diagnostic =
				isInUnionType
					? Constructor_type_notation_must_be_parenthesized_when_used_in_a_union_type
					: Constructor_type_notation_must_be_parenthesized_when_used_in_an_intersection_type;
		}
		parseErrorAtRange(typeNode->loc, diagnostic);
		return typeNode;
	}
	return (this->*parseConstituentType)();
}

bool Parser::isStartOfFunctionTypeOrConstructorType() {
	return token == Kind::LessThanToken ||
	       (token == Kind::OpenParenToken &&
	        lookAhead(&Parser::nextIsUnambiguouslyStartOfFunctionType)) ||
	       token == Kind::NewKeyword ||
	       (token == Kind::AbstractKeyword &&
	        lookAhead(&Parser::nextTokenIsNewKeyword));
}

Node* Parser::parseFunctionOrConstructorType() {
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	ModifierList* modifiers = parseModifiersForConstructorType();
	bool isConstructorType = parseOptional(Kind::NewKeyword);
	// Per isStartOfFunctionOrConstructorType, a function type cannot have
	// modifiers.
	NodeList* typeParameters = parseTypeParameters();
	NodeList* parameters = parseParameters(ParseFlagsType);
	Node* returnType = parseReturnType(Kind::EqualsGreaterThanToken, false);
	Node* result;
	if (isConstructorType) {
		result = factory.newConstructorTypeNode(modifiers, typeParameters,
		                                        parameters, returnType);
	} else {
		result =
			factory.newFunctionTypeNode(typeParameters, parameters, returnType);
	}
	finishNode(result, pos);
	withJSDoc(result, jsdoc);
	return result;
}

ModifierList* Parser::parseModifiersForConstructorType() {
	if (token == Kind::AbstractKeyword) {
		int pos = nodePos();
		Node* modifier = factory.newToken(token);
		nextToken();
		finishNode(modifier, pos);
		return newModifierList(modifier->loc, {modifier});
	}
	return nullptr;
}

bool Parser::nextTokenIsNewKeyword() {
	return nextToken() == Kind::NewKeyword;
}

bool Parser::nextIsUnambiguouslyStartOfFunctionType() {
	nextToken();
	if (token == Kind::CloseParenToken || token == Kind::DotDotDotToken) {
		// ( )
		// ( ...
		return true;
	}
	if (skipParameterStart()) {
		// We successfully skipped modifiers (if any) and an identifier or
		// binding pattern, now see if we have something that indicates a
		// parameter declaration
		if (token == Kind::ColonToken || token == Kind::CommaToken ||
		    token == Kind::QuestionToken || token == Kind::EqualsToken) {
			// ( xxx :
			// ( xxx ,
			// ( xxx ?
			// ( xxx =
			return true;
		}
		if (token == Kind::CloseParenToken &&
		    nextToken() == Kind::EqualsGreaterThanToken) {
			// ( xxx ) =>
			return true;
		}
	}
	return false;
}

bool Parser::skipParameterStart() {
	if (isModifierKind(token)) {
		// Skip modifiers
		parseModifiers();
	}
	parseOptional(Kind::DotDotDotToken);
	if (isIdentifier() || token == Kind::ThisKeyword) {
		nextToken();
		return true;
	}
	if (token == Kind::OpenBracketToken || token == Kind::OpenBraceToken) {
		// Return true if we can parse an array or object binding pattern with
		// no errors
		size_t previousErrorCount = diagnostics.size();
		parseIdentifierOrPattern();
		return previousErrorCount == diagnostics.size();
	}
	return false;
}

ModifierList* Parser::parseModifiers() {
	return parseModifiersEx(false, false, false);
}

ModifierList* Parser::parseModifiersEx(bool allowDecorators,
                                       bool permitConstAsModifier,
                                       bool stopOnStartOfClassStaticBlock) {
	bool hasLeadingModifier = false;
	bool hasTrailingDecorator = false;
	bool hasTrailingModifier = false;
	bool hasStaticModifier = false;
	// Decorators should be contiguous in a list of modifiers but can
	// potentially appear in two places (i.e., `[...leadingDecorators,
	// ...leadingModifiers, ...trailingDecorators, ...trailingModifiers]`).
	// The leading modifiers *should* only contain `export` and `default` when
	// trailingDecorators are present, but we'll handle errors for any other
	// leading modifiers in the checker. It is illegal to have both
	// leadingDecorators and trailingDecorators, but we will report that as a
	// grammar check in the checker.
	// parse leading decorators
	int pos = nodePos();
	std::vector<Node*> list;
	list.reserve(16);
	for (;;) {
		if (allowDecorators && token == Kind::AtToken &&
		    !hasTrailingModifier) {
			Node* decorator = parseDecorator();
			list.push_back(decorator);
			if (hasLeadingModifier) {
				hasTrailingDecorator = true;
			}
		} else {
			Node* modifier = tryParseModifier(
				hasStaticModifier, permitConstAsModifier,
				stopOnStartOfClassStaticBlock);
			if (modifier == nullptr) {
				break;
			}
			if (modifier->kind == Kind::StaticKeyword) {
				hasStaticModifier = true;
			}
			list.push_back(modifier);
			if (hasTrailingDecorator) {
				hasTrailingModifier = true;
			} else {
				hasLeadingModifier = true;
			}
		}
	}
	if (!list.empty()) {
		return newModifierList(TextRange{pos, nodePos()}, list);
	}
	return nullptr;
}

Node* Parser::parseDecorator() {
	int pos = nodePos();
	parseExpected(Kind::AtToken);
	Node* expression = doInContext(NodeFlagsDecoratorContext, true,
	                               &Parser::parseDecoratorExpression);
	return finishNode(factory.newDecorator(expression), pos);
}

Node* Parser::parseDecoratorExpression() {
	if (inAwaitContext() && token == Kind::AwaitKeyword) {
		// `@await` is disallowed in an [Await] context, but can cause parsing
		// to go off the rails. This simply parses the missing identifier and
		// moves on.
		int pos = nodePos();
		Node* awaitExpression =
			parseIdentifierWithDiagnostic(Expression_expected, nullptr);
		nextToken();
		Node* memberExpression =
			parseMemberExpressionRest(pos, awaitExpression, true);
		return parseCallExpressionRest(pos, memberExpression);
	}
	return parseLeftHandSideExpressionOrHigher();
}

Node* Parser::tryParseModifier(bool hasSeenStaticModifier,
                               bool permitConstAsModifier,
                               bool stopOnStartOfClassStaticBlock) {
	int pos = nodePos();
	Kind kind = token;
	if (token == Kind::ConstKeyword && permitConstAsModifier) {
		// We need to ensure that any subsequent modifiers appear on the same
		// line so that when 'const' is a standalone declaration, we don't
		// issue an error.
		if (!lookAhead(
		        &Parser::nextTokenIsOnSameLineAndCanFollowModifier)) {
			return nullptr;
		} else {
			nextToken();
		}
	} else if (stopOnStartOfClassStaticBlock &&
	           token == Kind::StaticKeyword &&
	           lookAhead(&Parser::nextTokenIsOpenBrace)) {
		return nullptr;
	} else if (hasSeenStaticModifier && token == Kind::StaticKeyword) {
		return nullptr;
	} else {
		if (!parseAnyContextualModifier()) {
			return nullptr;
		}
	}
	return finishNode(factory.newToken(kind), pos);
}

bool Parser::parseContextualModifier(Kind t) {
	ParserState state = mark();
	if (token == t && nextTokenCanFollowModifier()) {
		return true;
	}
	rewind(state);
	return false;
}

bool Parser::parseAnyContextualModifier() {
	ParserState state = mark();
	if (isModifierKind(token) && nextTokenCanFollowModifier()) {
		return true;
	}
	rewind(state);
	return false;
}

bool Parser::nextTokenCanFollowModifier() {
	switch (token) {
	case Kind::ConstKeyword:
		// 'const' is only a modifier if followed by 'enum'.
		return nextToken() == Kind::EnumKeyword;
	case Kind::ExportKeyword:
		nextToken();
		if (token == Kind::DefaultKeyword) {
			return lookAhead(&Parser::nextTokenCanFollowDefaultKeyword);
		}
		if (token == Kind::TypeKeyword) {
			return lookAhead(&Parser::nextTokenCanFollowExportModifier);
		}
		return canFollowExportModifier();
	case Kind::DefaultKeyword:
		return nextTokenCanFollowDefaultKeyword();
	case Kind::StaticKeyword:
		nextToken();
		return canFollowModifier();
	case Kind::GetKeyword:
	case Kind::SetKeyword:
		nextToken();
		return canFollowGetOrSetKeyword();
	default:
		return nextTokenIsOnSameLineAndCanFollowModifier();
	}
}

bool Parser::nextTokenCanFollowDefaultKeyword() {
	switch (nextToken()) {
	case Kind::ClassKeyword:
	case Kind::FunctionKeyword:
	case Kind::InterfaceKeyword:
	case Kind::AtToken:
		return true;
	case Kind::AbstractKeyword:
		return lookAhead(&Parser::nextTokenIsClassKeywordOnSameLine);
	case Kind::AsyncKeyword:
		return lookAhead(&Parser::nextTokenIsFunctionKeywordOnSameLine);
	default:
		break;
	}
	return false;
}

bool Parser::nextTokenIsIdentifierOrKeyword() {
	return tokenIsIdentifierOrKeyword(nextToken());
}

bool Parser::nextTokenIsIdentifierOrKeywordOrGreaterThan() {
	return tokenIsIdentifierOrKeywordOrGreaterThan(nextToken());
}

bool Parser::nextTokenIsIdentifierOrKeywordOnSameLine() {
	return nextTokenIsIdentifierOrKeyword() && !hasPrecedingLineBreak();
}

bool Parser::nextTokenIsIdentifierOrKeywordOrLiteralOnSameLine() {
	return (nextTokenIsIdentifierOrKeyword() ||
	        token == Kind::NumericLiteral || token == Kind::BigIntLiteral ||
	        token == Kind::StringLiteral) &&
	       !hasPrecedingLineBreak();
}

bool Parser::nextTokenIsClassKeywordOnSameLine() {
	return nextToken() == Kind::ClassKeyword && !hasPrecedingLineBreak();
}

bool Parser::nextTokenIsFunctionKeywordOnSameLine() {
	return nextToken() == Kind::FunctionKeyword && !hasPrecedingLineBreak();
}

bool Parser::nextTokenCanFollowExportModifier() {
	nextToken();
	return canFollowExportModifier();
}

bool Parser::canFollowExportModifier() {
	return token == Kind::AtToken ||
	       (token != Kind::AsteriskToken && token != Kind::AsKeyword &&
	        token != Kind::OpenBraceToken && canFollowModifier());
}

bool Parser::canFollowModifier() {
	return token == Kind::OpenBracketToken || token == Kind::OpenBraceToken ||
	       token == Kind::AsteriskToken || token == Kind::DotDotDotToken ||
	       isLiteralPropertyName();
}

bool Parser::canFollowGetOrSetKeyword() {
	return token == Kind::OpenBracketToken || isLiteralPropertyName();
}

bool Parser::nextTokenIsOnSameLineAndCanFollowModifier() {
	nextToken();
	if (hasPrecedingLineBreak()) {
		return false;
	}
	return canFollowModifier();
}

bool Parser::nextTokenIsOpenBrace() {
	return nextToken() == Kind::OpenBraceToken;
}

Node* Parser::parseExpression() {
	// Expression[in]:
	//      AssignmentExpression[in]
	//      Expression[in] , AssignmentExpression[in]

	// clear the decorator context when parsing Expression, as it should be
	// unambiguous when parsing a decorator
	NodeFlags saveContextFlags = contextFlags;
	contextFlags &= ~NodeFlagsDecoratorContext;
	int pos = nodePos();
	Node* expr = parseAssignmentExpressionOrHigher();
	for (;;) {
		Node* operatorToken = parseOptionalToken(Kind::CommaToken);
		if (operatorToken == nullptr) {
			break;
		}
		expr = makeBinaryExpression(expr, operatorToken,
		                            parseAssignmentExpressionOrHigher(), pos);
	}

	contextFlags = saveContextFlags;
	return expr;
}

Node* Parser::parseExpressionAllowIn() {
	return doInContext(NodeFlagsDisallowInContext, false,
	                   &Parser::parseExpression);
}

Node* Parser::parseAssignmentExpressionOrHigher() {
	return parseAssignmentExpressionOrHigherWorker(true);
}

Node* Parser::parseAssignmentExpressionOrHigherWorker(
	bool allowReturnTypeInArrowFunction) {
	//  AssignmentExpression[in,yield]:
	//      1) ConditionalExpression[?in,?yield]
	//      2) LeftHandSideExpression = AssignmentExpression[?in,?yield]
	//      3) LeftHandSideExpression AssignmentOperator
	//         AssignmentExpression[?in,?yield]
	//      4) ArrowFunctionExpression[?in,?yield]
	//      5) AsyncArrowFunctionExpression[in,yield,await]
	//      6) [+Yield] YieldExpression[?In]
	//
	// Note: for ease of implementation we treat productions '2' and '3' as
	// the same thing (i.e. they're both BinaryExpressions with an assignment
	// operator in it).
	// First, do the simple check if we have a YieldExpression (production '6').
	if (isYieldExpression()) {
		return parseYieldExpression();
	}
	// Then, check if we have an arrow function (production '4' and '5') that
	// starts with a parenthesized parameter list or is an async arrow
	// function. AsyncArrowFunctionExpression:
	//      1) async[no LineTerminator
	//      here]AsyncArrowBindingIdentifier[?Yield][no LineTerminator
	//      here]=>AsyncConciseBody[?In]
	//      2) CoverCallExpressionAndAsyncArrowHead[?Yield, ?Await][no
	//      LineTerminator here]=>AsyncConciseBody[?In]
	// Production (1) of AsyncArrowFunctionExpression is parsed in
	// "tryParseAsyncSimpleArrowFunctionExpression". And production (2) is
	// parsed in "tryParseParenthesizedArrowFunctionExpression".
	//
	// If we do successfully parse arrow-function, we must *not* recurse for
	// productions 1, 2 or 3. An ArrowFunction is not a LeftHandSideExpression,
	// nor does it start a ConditionalExpression. So we are done with
	// AssignmentExpression if we see one.
	Node* arrowExpression =
		tryParseParenthesizedArrowFunctionExpression(
			allowReturnTypeInArrowFunction);
	if (arrowExpression != nullptr) {
		return arrowExpression;
	}
	arrowExpression =
		tryParseAsyncSimpleArrowFunctionExpression(
			allowReturnTypeInArrowFunction);
	if (arrowExpression != nullptr) {
		return arrowExpression;
	}
	// Now try to see if we're in production '1', '2' or '3'. A conditional
	// expression can start with a LogicalOrExpression, while the assignment
	// productions can only start with LeftHandSideExpressions.
	//
	// So, first, we try to just parse out a BinaryExpression. If we get
	// something that is a LeftHandSide or higher, then we can try to parse
	// out the assignment expression part. Otherwise, we try to parse out the
	// conditional expression bit. We want to allow any binary expression
	// here, so we pass in the 'lowest' precedence here so that it matches
	// and consumes anything.
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	Node* expr = parseBinaryExpressionOrHigher(OperatorPrecedenceLowest);
	// To avoid a look-ahead, we did not handle the case of an arrow function
	// with a single un-parenthesized parameter ('x => ...') above. We handle
	// it here by checking if the parsed expression was a single identifier
	// and the current token is an arrow.
	if (expr->kind == Kind::Identifier &&
	    token == Kind::EqualsGreaterThanToken) {
		return parseSimpleArrowFunctionExpression(
			pos, expr, allowReturnTypeInArrowFunction, jsdoc, nullptr);
	}
	// Now see if we might be in cases '2' or '3'.
	// If the expression was a LHS expression, and we have an assignment
	// operator, then we're in '2' or '3'. Consume the assignment and return.
	//
	// Note: we call reScanGreaterToken so that we get an appropriately merged
	// token for cases like `> > =` becoming `>>=`
	if (isLeftHandSideExpression(expr) &&
	    isAssignmentOperator(reScanGreaterThanToken())) {
		return makeBinaryExpression(expr, parseTokenNode(),
		                            parseAssignmentExpressionOrHigherWorker(
		                                allowReturnTypeInArrowFunction),
		                            pos);
	}
	// It wasn't an assignment or a lambda. This is a conditional expression:
	return parseConditionalExpressionRest(expr, pos,
	                                      allowReturnTypeInArrowFunction);
}

bool Parser::isYieldExpression() {
	if (token == Kind::YieldKeyword) {
		// If we have a 'yield' keyword, and this is a context where yield
		// expressions are allowed, then definitely parse out a yield
		// expression.
		if (inYieldContext()) {
			return true;
		}

		// We're in a context where 'yield expr' is not allowed. However, if we
		// can definitely tell that the user was trying to parse a 'yield expr'
		// and not just a normal expr that start with a 'yield' identifier,
		// then parse out a 'yield expr'. We can then report an error later
		// that they are only allowed in generator expressions.
		//
		// for example, if we see 'yield(foo)', then we'll have to treat that
		// as an invocation expression of something called 'yield'. However, if
		// we have 'yield foo' then that is not legal as a normal expression,
		// so we can definitely recognize this as a yield expression.
		//
		// for now we just check if the next token is an identifier. More
		// heuristics can be added here later as necessary. We just need to
		// make sure that we don't accidentally consume something legal.
		return lookAhead(
			&Parser::nextTokenIsIdentifierOrKeywordOrLiteralOnSameLine);
	}
	return false;
}

Node* Parser::parseYieldExpression() {
	int pos = nodePos();
	// YieldExpression[In] :
	//      yield
	//      yield [no LineTerminator here] [Lexical goal
	//      InputElementRegExp]AssignmentExpression[?In, Yield]
	//      yield [no LineTerminator here] * [Lexical goal
	//      InputElementRegExp]AssignmentExpression[?In, Yield]
	nextToken();
	Node* result;
	if (!hasPrecedingLineBreak() &&
	    (token == Kind::AsteriskToken || isStartOfExpression())) {
		result = factory.newYieldExpression(
			parseOptionalToken(Kind::AsteriskToken),
			parseAssignmentExpressionOrHigher());
	} else {
		// if the next token is not on the same line as yield. or we don't
		// have an '*' or the start of an expression, then this is just a
		// simple "yield" expression.
		result = factory.newYieldExpression(nullptr, nullptr);
	}
	return finishNode(result, pos);
}

Tristate Parser::isParenthesizedArrowFunctionExpression() {
	if (token == Kind::OpenParenToken || token == Kind::LessThanToken ||
	    token == Kind::AsyncKeyword) {
		ParserState state = mark();
		Tristate result = nextIsParenthesizedArrowFunctionExpression();
		rewind(state);
		return result;
	}
	if (token == Kind::EqualsGreaterThanToken) {
		// ERROR RECOVERY TWEAK:
		// If we see a standalone => try to parse it as an arrow function
		// expression as that's likely what the user intended to write.
		return Tristate::True;
	}
	// Definitely not a parenthesized arrow function.
	return Tristate::False;
}

Tristate Parser::nextIsParenthesizedArrowFunctionExpression() {
	if (token == Kind::AsyncKeyword) {
		nextToken();
		if (hasPrecedingLineBreak()) {
			return Tristate::False;
		}
		if (token != Kind::OpenParenToken && token != Kind::LessThanToken) {
			return Tristate::False;
		}
	}
	Kind first = token;
	Kind second = nextToken();
	if (first == Kind::OpenParenToken) {
		if (second == Kind::CloseParenToken) {
			// Simple cases: "() =>", "(): ", and "() {".
			// This is an arrow function with no parameters.
			// The last one is not actually an arrow function,
			// but this is probably what the user intended.
			Kind third = nextToken();
			switch (third) {
			case Kind::EqualsGreaterThanToken:
			case Kind::ColonToken:
			case Kind::OpenBraceToken:
				return Tristate::True;
			default:
				break;
			}
			return Tristate::False;
		}
		// If encounter "([" or "({", this could be the start of a binding
		// pattern.
		if (second == Kind::OpenBracketToken ||
		    second == Kind::OpenBraceToken) {
			return Tristate::Unknown;
		}
		// Simple case: "(..."
		// This is an arrow function with a rest parameter.
		if (second == Kind::DotDotDotToken) {
			return Tristate::True;
		}
		// Check for "(xxx yyy", where xxx is a modifier and yyy is an
		// identifier. This isn't actually allowed, but we want to treat it as
		// a lambda so we can provide a good error message.
		if (isModifierKind(second) && second != Kind::AsyncKeyword &&
		    lookAhead(&Parser::nextTokenIsIdentifier)) {
			if (nextToken() == Kind::AsKeyword) {
				// https://github.com/microsoft/TypeScript/issues/44466
				return Tristate::False;
			}
			return Tristate::True;
		}
		// If we had "(" followed by something that's not an identifier,
		// then this definitely doesn't look like a lambda. "this" is not
		// valid, but we want to parse it and then give a semantic error.
		if (!isIdentifier() && second != Kind::ThisKeyword) {
			return Tristate::False;
		}
		switch (nextToken()) {
		case Kind::ColonToken:
			// If we have something like "(a:", then we must have a
			// type-annotated parameter in an arrow function expression.
			return Tristate::True;
		case Kind::QuestionToken:
			nextToken();
			// If we have "(a?:" or "(a?," or "(a?=" or "(a?)" then it is
			// definitely a lambda.
			if (token == Kind::ColonToken || token == Kind::CommaToken ||
			    token == Kind::EqualsToken ||
			    token == Kind::CloseParenToken) {
				return Tristate::True;
			}
			// Otherwise it is definitely not a lambda.
			return Tristate::False;
		case Kind::CommaToken:
		case Kind::EqualsToken:
		case Kind::CloseParenToken:
			// If we have "(a," or "(a=" or "(a)" this *could* be an arrow
			// function
			return Tristate::Unknown;
		default:
			break;
		}
		// It is definitely not an arrow function
		return Tristate::False;
	} else {
		// debug.Assert(first == ast.KindLessThanToken)
		// If we have "<" not followed by an identifier,
		// then this definitely is not an arrow function.
		if (!isIdentifier() && token != Kind::ConstKeyword) {
			return Tristate::False;
		}
		// JSX overrides
		if (languageVariant == LanguageVariant::JSX) {
			bool isArrowFunctionInJsx = lookAhead([](Parser* p) -> bool {
				p->parseOptional(Kind::ConstKeyword);
				Kind third = p->nextToken();
				if (third == Kind::ExtendsKeyword) {
					Kind fourth = p->nextToken();
					switch (fourth) {
					case Kind::EqualsToken:
					case Kind::GreaterThanToken:
					case Kind::SlashToken:
						return false;
					default:
						break;
					}
					return true;
				} else if (third == Kind::CommaToken ||
				           third == Kind::EqualsToken) {
					return true;
				}
				return false;
			});
			if (isArrowFunctionInJsx) {
				return Tristate::True;
			}
			return Tristate::False;
		}
		// This *could* be a parenthesized arrow function.
		return Tristate::Unknown;
	}
}

Node* Parser::tryParseParenthesizedArrowFunctionExpression(
	bool allowReturnTypeInArrowFunction) {
	Tristate tristate = isParenthesizedArrowFunctionExpression();
	if (tristate == Tristate::False) {
		// It's definitely not a parenthesized arrow function expression.
		return nullptr;
	}
	// If we definitely have an arrow function, then we can just parse one,
	// not requiring a following => or { token. Otherwise, we *might* have an
	// arrow function. Try to parse it out, but don't allow any ambiguity,
	// and return 'undefined' if this could be an expression instead.
	if (tristate == Tristate::True) {
		return parseParenthesizedArrowFunctionExpression(
			true, allowReturnTypeInArrowFunction);
	}
	ParserState state = mark();
	Node* result = parsePossibleParenthesizedArrowFunctionExpression(
		allowReturnTypeInArrowFunction);
	if (result == nullptr) {
		rewind(state);
	}
	return result;
}

Node* Parser::parseParenthesizedArrowFunctionExpression(
	bool allowAmbiguity, bool allowReturnTypeInArrowFunction) {
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	ModifierList* modifiers = parseModifiersForArrowFunction();
	bool isAsync = modifierListHasAsync(modifiers);
	ParseFlags signatureFlags =
		isAsync ? ParseFlagsAwait : ParseFlagsNone;
	// Arrow functions are never generators.
	//
	// If we're speculatively parsing a signature for a parenthesized arrow
	// function, then we have to have a complete parameter list. Otherwise we
	// might see something like a => (b => c) And think that "(b =>" was
	// actually a parenthesized arrow function with a missing close paren.
	NodeList* typeParameters = parseTypeParameters();
	NodeList* parameters;
	if (!parseExpected(Kind::OpenParenToken)) {
		if (!allowAmbiguity) {
			return nullptr;
		}
		parameters = createMissingList();
	} else {
		if (!allowAmbiguity) {
			NodeList* maybeParameters =
				parseParametersWorker(signatureFlags, allowAmbiguity);
			if (maybeParameters == nullptr) {
				return nullptr;
			}
			parameters = maybeParameters;
		} else {
			parameters = parseParametersWorker(signatureFlags, allowAmbiguity);
		}
		if (!parseExpected(Kind::CloseParenToken) && !allowAmbiguity) {
			return nullptr;
		}
	}
	bool hasReturnColon = token == Kind::ColonToken;
	Node* returnType = parseReturnType(Kind::ColonToken, false);
	if (returnType != nullptr && !allowAmbiguity &&
	    typeHasArrowFunctionBlockingParseError(returnType)) {
		return nullptr;
	}
	// Parsing a signature isn't enough.
	// Parenthesized arrow signatures often look like other valid expressions.
	// For instance:
	//  - "(x = 10)" is an assignment expression parsed as a signature with a
	//    default parameter value.
	//  - "(x,y)" is a comma expression parsed as a signature with two
	//    parameters.
	//  - "a ? (b): c" will have "(b):" parsed as a signature with a return
	//    type annotation.
	//  - "a ? (b): function() {}" will too, since function() is a valid JSDoc
	//    function type.
	//  - "a ? (b): (function() {})" as well, but inside of a parenthesized
	//    type with an arbitrary amount of nesting.
	//
	// So we need just a bit of lookahead to ensure that it can only be a
	// signature.
	Node* unwrappedType = returnType;
	while (unwrappedType != nullptr &&
	       unwrappedType->kind == Kind::ParenthesizedType) {
		unwrappedType = unwrappedType->type();  // Skip parens if need be
	}
	if (!allowAmbiguity && token != Kind::EqualsGreaterThanToken &&
	    token != Kind::OpenBraceToken) {
		// Returning nullptr here will cause our caller to rewind to where we
		// started from.
		return nullptr;
	}
	// If we have an arrow, then try to parse the body. Even if not, try to
	// parse if we have an opening brace, just in case we're in an error
	// state.
	Kind lastToken = token;
	Node* equalsGreaterThanToken =
		parseExpectedToken(Kind::EqualsGreaterThanToken);
	Node* body;
	if (lastToken == Kind::EqualsGreaterThanToken ||
	    lastToken == Kind::OpenBraceToken) {
		body = parseArrowFunctionExpressionBody(
			isAsync, allowReturnTypeInArrowFunction);
	} else {
		body = parseIdentifier();
	}
	// Given:
	//     x ? y => ({ y }) : z => ({ z })
	// We try to parse the body of the first arrow function by looking at:
	//     ({ y }) : z => ({ z })
	// This is a valid arrow function with "z" as the return type.
	//
	// But, if we're in the true side of a conditional expression, this colon
	// terminates the expression, so we cannot allow a return type if we
	// aren't certain whether or not the preceding text was parsed as a
	// parameter list.
	//
	// For example,
	//     a() ? (b: number, c?: string): void => d() : e
	// is determined by isParenthesizedArrowFunctionExpression to
	// unambiguously be an arrow expression, so we allow a return type.
	if (!allowReturnTypeInArrowFunction && hasReturnColon) {
		// However, if the arrow function we were able to parse is followed by
		// another colon as in:
		//     a ? (x): string => x : null
		// Then allow the arrow function, and treat the second colon as
		// terminating the conditional expression. It's okay to do this
		// because this code would be a syntax error in JavaScript (as the
		// second colon shouldn't be there).
		if (token != Kind::ColonToken) {
			return nullptr;
		}
	}
	Node* result = finishNode(
		factory.newArrowFunction(modifiers, typeParameters, parameters,
		                         returnType, nullptr, equalsGreaterThanToken,
		                         body),
		pos);
	withJSDoc(result, jsdoc);
	checkJSSyntax(result);
	return result;
}

ModifierList* Parser::parseModifiersForArrowFunction() {
	if (token == Kind::AsyncKeyword) {
		int pos = nodePos();
		nextToken();
		Node* modifier =
			finishNode(factory.newToken(Kind::AsyncKeyword), pos);
		return newModifierList(modifier->loc, {modifier});
	}
	return nullptr;
}

// If true, we should abort parsing an error function.
static bool typeHasArrowFunctionBlockingParseError(Node* node) {
	switch (node->kind) {
	case Kind::TypeReference:
		return nodeIsMissing(node->as<TypeReferenceNode>()->TypeName);
	case Kind::FunctionType:
	case Kind::ConstructorType:
		return isMissingNodeList(node->parameterList()) ||
		       typeHasArrowFunctionBlockingParseError(node->type());
	case Kind::ParenthesizedType:
		return typeHasArrowFunctionBlockingParseError(node->type());
	default:
		break;
	}
	return false;
}

Node* Parser::parseArrowFunctionExpressionBody(
	bool isAsync, bool allowReturnTypeInArrowFunction) {
	if (token == Kind::OpenBraceToken) {
		return parseFunctionBlock(
			isAsync ? ParseFlagsAwait : ParseFlagsNone, nullptr);
	}
	if (token != Kind::SemicolonToken && token != Kind::FunctionKeyword &&
	    token != Kind::ClassKeyword && isStartOfStatement() &&
	    !isStartOfExpressionStatement()) {
		// Check if we got a plain statement (i.e. no expression-statements,
		// no function/class expressions/declarations)
		//
		// Here we try to recover from a potential error situation in the case
		// where the user meant to supply a block. For example, if the user
		// wrote:
		//
		//  a =>
		//      let v = 0;
		//  }
		//
		// they may be missing an open brace. Check to see if that's the case
		// so we can try to recover better. If we don't do this, then the next
		// close curly we see may end up preemptively closing the containing
		// construct.
		//
		// Note: even when 'IgnoreMissingOpenBrace' is passed, parseBody will
		// still error.
		return parseFunctionBlock(
			ParseFlagsIgnoreMissingOpenBrace |
				(isAsync ? ParseFlagsAwait : ParseFlagsNone),
			nullptr);
	}
	NodeFlags saveContextFlags = contextFlags;
	setContextFlags(NodeFlagsAwaitContext, isAsync);
	setContextFlags(NodeFlagsYieldContext, false);
	Node* node = parseAssignmentExpressionOrHigherWorker(
		allowReturnTypeInArrowFunction);
	contextFlags = saveContextFlags;
	return node;
}

bool Parser::isStartOfExpressionStatement() {
	// As per the grammar, none of '{' or 'function' or 'class' can start an
	// expression statement.
	return token != Kind::OpenBraceToken && token != Kind::FunctionKeyword &&
	       token != Kind::ClassKeyword && token != Kind::AtToken &&
	       isStartOfExpression();
}

Node* Parser::parsePossibleParenthesizedArrowFunctionExpression(
	bool allowReturnTypeInArrowFunction) {
	int tokenPos = scanner->tokenStart();
	if (notParenthesizedArrow.count(tokenPos)) {
		return nullptr;
	}
	Node* result = parseParenthesizedArrowFunctionExpression(
		false, allowReturnTypeInArrowFunction);
	if (result == nullptr) {
		notParenthesizedArrow.insert(tokenPos);
	}
	return result;
}

Node* Parser::tryParseAsyncSimpleArrowFunctionExpression(
	bool allowReturnTypeInArrowFunction) {
	// We do a check here so that we won't be doing unnecessarily call to
	// "lookAhead"
	if (token == Kind::AsyncKeyword &&
	    lookAhead(&Parser::nextIsUnParenthesizedAsyncArrowFunction)) {
		int pos = nodePos();
		JSDocScannerInfo jsdoc = jsdocScannerInfo();
		ModifierList* asyncModifier = parseModifiersForArrowFunction();
		Node* expr =
			parseBinaryExpressionOrHigher(OperatorPrecedenceLowest);
		return parseSimpleArrowFunctionExpression(
			pos, expr, allowReturnTypeInArrowFunction, jsdoc, asyncModifier);
	}
	return nullptr;
}

bool Parser::nextIsUnParenthesizedAsyncArrowFunction() {
	// AsyncArrowFunctionExpression:
	//      1) async[no LineTerminator here]AsyncArrowBindingIdentifier[?Yield]
	//         [no LineTerminator here]=>AsyncConciseBody[?In]
	//      2) CoverCallExpressionAndAsyncArrowHead[?Yield, ?Await]
	//         [no LineTerminator here]=>AsyncConciseBody[?In]
	if (token == Kind::AsyncKeyword) {
		nextToken();
		// If the "async" is followed by "=>" token then it is not a beginning
		// of an async arrow-function but instead a simple arrow-function
		// which will be parsed inside "parseAssignmentExpressionOrHigher"

		if (hasPrecedingLineBreak() || token == Kind::EqualsGreaterThanToken) {
			return false;
		}
		// Check for un-parenthesized AsyncArrowFunction
		if (!isIdentifier()) {
			return false;
		}
		nextTokenWithoutCheck();
		return !hasPrecedingLineBreak() && token == Kind::EqualsGreaterThanToken;
	}
	return false;
}

Node* Parser::parseSimpleArrowFunctionExpression(
	int pos, Node* identifier, bool allowReturnTypeInArrowFunction,
	JSDocScannerInfo jsdoc, ModifierList* asyncModifier) {
	// debug.Assert(p.token == KindEqualsGreaterThanToken)
	Node* parameter = finishNode(
		factory.newParameterDeclaration(nullptr, nullptr, identifier, nullptr,
		                                nullptr, nullptr),
		identifier->pos());
	NodeList* parameters = newNodeList(parameter->loc, {parameter});
	Node* equalsGreaterThanToken =
		parseExpectedToken(Kind::EqualsGreaterThanToken);
	Node* body = parseArrowFunctionExpressionBody(
		asyncModifier != nullptr, allowReturnTypeInArrowFunction);
	Node* result = finishNode(
		factory.newArrowFunction(asyncModifier, nullptr, parameters, nullptr,
		                         nullptr, equalsGreaterThanToken, body),
		pos);
	withJSDoc(result, jsdoc);
	return result;
}

Node* Parser::parseConditionalExpressionRest(Node* leftOperand, int pos,
                                             bool allowReturnTypeInArrowFunction) {
	// Note: we are passed in an expression which was produced from
	// parseBinaryExpressionOrHigher.
	Node* questionToken = parseOptionalToken(Kind::QuestionToken);
	if (questionToken == nullptr) {
		return leftOperand;
	}
	// Note: we explicitly 'allowIn' in the whenTrue part of the condition
	// expression, and we do not that for the 'whenFalse' part.
	NodeFlags saveContextFlags = contextFlags;
	setContextFlags(NodeFlagsDisallowInContext, false);
	Node* trueExpression =
		parseAssignmentExpressionOrHigherWorker(false);
	contextFlags = saveContextFlags;
	Node* colonToken = parseExpectedToken(Kind::ColonToken);
	Node* falseExpression;
	if (nodeIsPresent(colonToken)) {
		falseExpression = parseAssignmentExpressionOrHigherWorker(
			allowReturnTypeInArrowFunction);
	} else {
		falseExpression = createMissingIdentifier();
	}
	return finishNode(
		factory.newConditionalExpression(leftOperand, questionToken,
		                                 trueExpression, colonToken,
		                                 falseExpression),
		pos);
}

Node* Parser::parseBinaryExpressionOrHigher(
	OperatorPrecedence precedence) {
	int pos = nodePos();
	Node* leftOperand = parseUnaryExpressionOrHigher();
	return parseBinaryExpressionRest(precedence, leftOperand, pos);
}

// shouldConsumeBinaryOperator reports whether an operator binds before the
// operator represented by currentPrecedence. At equal precedence, only the
// right-associative exponentiation operator binds first.
static bool shouldConsumeBinaryOperator(Kind operator_,
                                        OperatorPrecedence operatorPrecedence,
                                        OperatorPrecedence currentPrecedence) {
	if (operatorPrecedence > currentPrecedence) {
		return true;
	}
	return operatorPrecedence == currentPrecedence &&
	       operator_ == Kind::AsteriskAsteriskToken;
}

Node* Parser::parseBinaryExpressionRest(OperatorPrecedence precedence,
                                        Node* leftOperand, int pos) {
	Node* lastOperand = leftOperand;
	for (;;) {
		// We either have a binary operator here, or we're finished. We call
		// reScanGreaterThanToken so that we merge token sequences like > and
		// = into >=
		Kind operator_ = reScanGreaterThanToken();
		OperatorPrecedence newPrecedence =
			getBinaryOperatorPrecedence(operator_);
		// Check the precedence to see if we should "take" this operator
		// - For left associative operator (all operator but **), consume the
		//   operator, recursively call the function below, and parse
		//   binaryExpression as a rightOperand of the caller if the new
		//   precedence of the operator is greater then or equal to the
		//   current precedence.
		// - For right associative operator (**), consume the operator,
		//   recursively call the function and parse binaryExpression as a
		//   rightOperand of the caller if the new precedence of the operator
		//   is strictly greater than the current precedence
		if (!shouldConsumeBinaryOperator(operator_, newPrecedence,
		                                 precedence)) {
			break;
		}
		if (operator_ == Kind::InKeyword && inDisallowInContext()) {
			break;
		}
		if (operator_ == Kind::AsKeyword ||
		    operator_ == Kind::SatisfiesKeyword) {
			// Make sure we *do* perform ASI for constructs like this:
			//    var x = foo
			//    as (Bar)
			// This should be parsed as an initialized variable, followed
			// by a function call to 'as' with the argument 'Bar'
			if (hasPrecedingLineBreak()) {
				break;
			} else {
				nextToken();
				// When we have 'a ## b as SomeType $$ c' or 'a ## b
				// satisfies SomeType $$ c', where ## and $$ are binary
				// operators, we want to stop parsing when $$ would bind
				// before ## after erasing the assertion. See
				// https://github.com/microsoft/TypeScript/issues/63527.
				OperatorPrecedence lastPrecedence =
					OperatorPrecedenceHighest;
				if (isBinaryExpression(lastOperand)) {
					lastPrecedence = getBinaryOperatorPrecedence(
						lastOperand->as<BinaryExpression>()
							->OperatorToken->kind);
				}
				if (operator_ == Kind::SatisfiesKeyword) {
					leftOperand =
						makeSatisfiesExpression(leftOperand, parseType());
				} else {
					leftOperand =
						makeAsExpression(leftOperand, parseType());
				}
				// Stop if the next operator would bind before the last
				// operator when the assertion is erased.
				Kind nextOperator = reScanGreaterThanToken();
				OperatorPrecedence nextPrecedence =
					getBinaryOperatorPrecedence(nextOperator);
				if (shouldConsumeBinaryOperator(nextOperator,
				                                nextPrecedence,
				                                lastPrecedence)) {
					break;
				}
			}
		} else {
			leftOperand = makeBinaryExpression(
				leftOperand, parseTokenNode(),
				parseBinaryExpressionOrHigher(newPrecedence), pos);
			lastOperand = leftOperand;
		}
	}
	return leftOperand;
}

Node* Parser::makeSatisfiesExpression(Node* expression, Node* typeNode) {
	return checkJSSyntax(
		finishNode(factory.newSatisfiesExpression(expression, typeNode),
		           expression->pos()));
}

Node* Parser::makeAsExpression(Node* left, Node* right) {
	return checkJSSyntax(
		finishNode(factory.newAsExpression(left, right), left->pos()));
}

Node* Parser::makeBinaryExpression(Node* left, Node* operatorToken,
                                   Node* right, int pos) {
	return finishNode(factory.newBinaryExpression(nullptr, left, nullptr,
		                                      operatorToken, right),
	                  pos);
}

Node* Parser::parseUnaryExpressionOrHigher() {
	// ES7 UpdateExpression:
	//      1) LeftHandSideExpression[?Yield]
	//      2) LeftHandSideExpression[?Yield][no LineTerminator here]++
	//      3) LeftHandSideExpression[?Yield][no LineTerminator here]--
	//      4) ++UnaryExpression[?Yield]
	//      5) --UnaryExpression[?Yield]
	if (isUpdateExpression()) {
		int pos = nodePos();
		Node* updateExpression = parseUpdateExpression();
		if (token == Kind::AsteriskAsteriskToken) {
			return parseBinaryExpressionRest(
				getBinaryOperatorPrecedence(token), updateExpression, pos);
		}
		return updateExpression;
	}
	// ES7 UnaryExpression:
	//      1) UpdateExpression[?yield]
	//      2) delete UpdateExpression[?yield]
	//      3) void UpdateExpression[?yield]
	//      4) typeof UpdateExpression[?yield]
	//      5) + UpdateExpression[?yield]
	//      6) - UpdateExpression[?yield]
	//      7) ~ UpdateExpression[?yield]
	//      8) ! UpdateExpression[?yield]
	Kind unaryOperator = token;
	Node* simpleUnaryExpression = parseSimpleUnaryExpression();
	if (token == Kind::AsteriskAsteriskToken) {
		int pos = skipTrivia(sourceText, simpleUnaryExpression->pos());
		int end = simpleUnaryExpression->end();
		if (simpleUnaryExpression->kind == Kind::TypeAssertionExpression) {
			parseErrorAt(
				pos, end,
				A_type_assertion_expression_is_not_allowed_in_the_left_hand_side_of_an_exponentiation_expression_Consider_enclosing_the_expression_in_parentheses);
		} else {
			// debug.Assert(isKeywordOrPunctuation(unaryOperator))
			parseErrorAt(
				pos, end,
				An_unary_expression_with_the_0_operator_is_not_allowed_in_the_left_hand_side_of_an_exponentiation_expression_Consider_enclosing_the_expression_in_parentheses,
				{std::string(tokenToString(unaryOperator))});
		}
	}
	return simpleUnaryExpression;
}

bool Parser::isUpdateExpression() {
	switch (token) {
	case Kind::PlusToken:
	case Kind::MinusToken:
	case Kind::TildeToken:
	case Kind::ExclamationToken:
	case Kind::DeleteKeyword:
	case Kind::TypeOfKeyword:
	case Kind::VoidKeyword:
	case Kind::AwaitKeyword:
		return false;
	case Kind::LessThanToken:
		return languageVariant == LanguageVariant::JSX;
	default:
		break;
	}
	return true;
}

Node* Parser::parseUpdateExpression() {
	int pos = nodePos();
	if (token == Kind::PlusPlusToken || token == Kind::MinusMinusToken) {
		Kind operator_ = token;
		nextToken();
		return finishNode(
			factory.newPrefixUnaryExpression(
				operator_, parseLeftHandSideExpressionOrHigher()),
			pos);
	} else if (languageVariant == LanguageVariant::JSX &&
	           token == Kind::LessThanToken &&
	           lookAhead(
	               &Parser::nextTokenIsIdentifierOrKeywordOrGreaterThan)) {
		// JSXElement is part of primaryExpression
		return parseJsxElementOrSelfClosingElementOrFragment(true, -1,
		                                                   nullptr, false);
	}
	Node* expression = parseLeftHandSideExpressionOrHigher();
	if ((token == Kind::PlusPlusToken ||
	     token == Kind::MinusMinusToken) &&
	    !hasPrecedingLineBreak()) {
		Kind operator_ = token;
		nextToken();
		return finishNode(
			factory.newPostfixUnaryExpression(expression, operator_), pos);
	}
	return expression;
}

Node* Parser::parseJsxElementOrSelfClosingElementOrFragment(
	bool inExpressionContext, int topInvalidNodePosition, Node* openingTag,
	bool mustBeUnary) {
	int pos = nodePos();
	Node* opening = parseJsxOpeningOrSelfClosingElementOrOpeningFragment(
		inExpressionContext);
	Node* result;
	switch (opening->kind) {
	case Kind::JsxOpeningElement: {
		NodeList* children = parseJsxChildren(opening);
		Node* closingElement;
		Node* lastChild =
			children->nodes.empty() ? nullptr : children->nodes.back();
		if (lastChild != nullptr && lastChild->kind == Kind::JsxElement &&
		    !tagNamesAreEquivalent(
		        lastChild->as<JsxElement>()->OpeningElement->tagName(),
		        lastChild->as<JsxElement>()->ClosingElement->tagName()) &&
		    tagNamesAreEquivalent(
		        opening->tagName(),
		        lastChild->as<JsxElement>()->ClosingElement->tagName())) {
			// when an unclosed JsxOpeningElement incorrectly parses its
			// parent's JsxClosingElement, restructure
			// (<div>(...<span>...</div>)) --> (<div>(...<span>...</>)</div>)
			// (no need to error; the parent will error)
			int end = lastChild->children()->end();
			Node* missingIdentifier =
				finishNodeWithEnd(newIdentifier(""), end, end);
			Node* newClosingElement = finishNodeWithEnd(
				factory.newJsxClosingElement(missingIdentifier), end, end);
			Node* newLast = finishNodeWithEnd(
				factory.newJsxElement(
					lastChild->as<JsxElement>()->OpeningElement,
					lastChild->children(), newClosingElement),
				lastChild->as<JsxElement>()->OpeningElement->pos(), end);
			// force reset parent pointers from discarded parse result
			if (lastChild->as<JsxElement>()->OpeningElement != nullptr) {
				lastChild->as<JsxElement>()->OpeningElement->parent =
					newLast;
			}
			if (lastChild->children() != nullptr) {
				for (Node* c : lastChild->children()->nodes) {
					c->parent = newLast;
				}
			}
			newClosingElement->parent = newLast;
			std::vector<Node*> newNodes(children->nodes.begin(),
			                            children->nodes.end() - 1);
			newNodes.push_back(newLast);
			children = newNodeList(
				TextRange{children->pos(), newLast->end()}, newNodes);
			closingElement = lastChild->as<JsxElement>()->ClosingElement;
		} else {
			closingElement =
				parseJsxClosingElement(opening, inExpressionContext);
			if (!tagNamesAreEquivalent(opening->tagName(),
			                           closingElement->tagName())) {
				if (openingTag != nullptr &&
				    isJsxOpeningElement(openingTag) &&
				    tagNamesAreEquivalent(closingElement->tagName(),
				                          openingTag->tagName())) {
					// opening incorrectly matched with its parent's closing --
					// put error on opening
					parseErrorAtRange(
						opening->tagName()->loc,
						JSX_element_0_has_no_corresponding_closing_tag,
						{std::string(getTextOfNodeFromSourceText(
							sourceText, opening->tagName(), false))});
				} else {
					// other opening/closing mismatches -- put error on
					// closing
					parseErrorAtRange(
						closingElement->tagName()->loc,
						Expected_corresponding_JSX_closing_tag_for_0,
						{std::string(getTextOfNodeFromSourceText(
							sourceText, opening->tagName(), false))});
				}
			}
		}
		result = finishNode(
			factory.newJsxElement(opening, children, closingElement), pos);
		closingElement->parent =
			result;  // force reset parent pointers from possibly discarded
		             // parse result
		break;
	}
	case Kind::JsxOpeningFragment:
		result = finishNode(
			factory.newJsxFragment(opening, parseJsxChildren(opening),
		                           parseJsxClosingFragment(inExpressionContext)),
			pos);
		break;
	case Kind::JsxSelfClosingElement:
		// Nothing else to do for self-closing elements
		result = opening;
		break;
	default:
		TSC_UNREACHABLE(
			"Unhandled case in parseJsxElementOrSelfClosingElementOrFragment");
	}
	// If the user writes the invalid code '<div></div><div></div>' in an
	// expression context (i.e. not wrapped in an enclosing tag), we'll
	// naively try to parse ^ this as a 'less than' operator and the
	// remainder of the tag as garbage, which will cause the formatter to
	// badly mangle the JSX. Perform a speculative parse of a JSX element if
	// we see a < token so that we can wrap it in a synthetic binary
	// expression so the formatter does less damage and we can report a
	// better error. Since JSX elements are invalid < operands anyway, this
	// lookahead parse will only occur in error scenarios of one sort or
	// another. If we are in a unary context, we can't do this recovery; the
	// binary expression we return here is not a valid UnaryExpression and
	// will cause problems later.
	if (!mustBeUnary && inExpressionContext &&
	    token == Kind::LessThanToken) {
		int topBadPos = topInvalidNodePosition;
		if (topBadPos < 0) {
			topBadPos = result->pos();
		}
		Node* invalidElement =
			parseJsxElementOrSelfClosingElementOrFragment(true, topBadPos,
			                                              nullptr, false);
		Node* operatorToken = factory.newToken(Kind::CommaToken);
		operatorToken->loc =
			TextRange{invalidElement->pos(), invalidElement->pos()};
		parseErrorAt(skipTrivia(sourceText, topBadPos),
		             invalidElement->end(),
		             JSX_expressions_must_have_one_parent_element);
		result = finishNode(factory.newBinaryExpression(
			                    nullptr, result, nullptr, operatorToken,
			                    invalidElement),
		                    pos);
	}
	return result;
}

NodeList* Parser::parseJsxChildren(Node* openingTag) {
	int pos = nodePos();
	ParsingContexts saveParsingContexts = parsingContexts;
	parsingContexts |= 1u << PCJsxChildren;
	std::vector<Node*> list;
	for (;;) {
		Kind currentToken = scanner->reScanJsxToken(true);
		Node* child = parseJsxChild(openingTag, currentToken);
		if (child == nullptr) {
			break;
		}
		list.push_back(child);
		if (isJsxOpeningElement(openingTag) &&
		    child->kind == Kind::JsxElement &&
		    !tagNamesAreEquivalent(
		        child->as<JsxElement>()->OpeningElement->tagName(),
		        child->as<JsxElement>()->ClosingElement->tagName()) &&
		    tagNamesAreEquivalent(
		        openingTag->tagName(),
		        child->as<JsxElement>()->ClosingElement->tagName())) {
			// stop after parsing a mismatched child like
			// <div>...(<span></div>) in order to reattach the </div> higher
			break;
		}
	}
	parsingContexts = saveParsingContexts;
	return newNodeList(TextRange{pos, nodePos()}, list);
}

Node* Parser::parseJsxChild(Node* openingTag, Kind token_) {
	switch (token_) {
	case Kind::EndOfFile:
		// If we hit EOF, issue the error at the tag that lacks the closing
		// element rather than at the end of the file (which is useless)
		if (isJsxOpeningFragment(openingTag)) {
			parseErrorAtRange(openingTag->loc,
			                  JSX_fragment_has_no_corresponding_closing_tag);
		} else {
			// We want the error span to cover only 'Foo.Bar' in < Foo.Bar >
			// or to cover only 'Foo' in < Foo >
			Node* tag = openingTag->tagName();
			int start =
				std::min(skipTrivia(sourceText, tag->pos()), tag->end());
			parseErrorAt(
				start, tag->end(),
				JSX_element_0_has_no_corresponding_closing_tag,
				{std::string(getTextOfNodeFromSourceText(
					sourceText, openingTag->tagName(), false))});
		}
		return nullptr;
	case Kind::LessThanSlashToken:
	case Kind::ConflictMarkerTrivia:
		return nullptr;
	case Kind::JsxText:
	case Kind::JsxTextAllWhiteSpaces:
		return parseJsxText();
	case Kind::OpenBraceToken:
		return parseJsxExpression(false);
	case Kind::LessThanToken:
		return parseJsxElementOrSelfClosingElementOrFragment(false, -1,
		                                                   openingTag, false);
	default:
		TSC_UNREACHABLE("Unhandled case in parseJsxChild");
	}
}

Node* Parser::parseJsxText() {
	int pos = nodePos();
	Node* result = factory.newJsxText(
		std::string(scanner->tokenValue()),
		token == Kind::JsxTextAllWhiteSpaces);
	scanJsxText();
	return finishNode(result, pos);
}

Node* Parser::parseJsxExpression(bool inExpressionContext) {
	int pos = nodePos();
	if (!parseExpected(Kind::OpenBraceToken)) {
		return nullptr;
	}
	Node* dotDotDotToken = nullptr;
	Node* expression = nullptr;
	if (token != Kind::CloseBraceToken) {
		if (!inExpressionContext) {
			dotDotDotToken = parseOptionalToken(Kind::DotDotDotToken);
		}
		// Only an AssignmentExpression is valid here per the JSX spec,
		// but we can unambiguously parse a comma sequence and provide
		// a better error message in grammar checking.
		expression = parseExpression();
	}
	if (inExpressionContext) {
		parseExpected(Kind::CloseBraceToken);
	} else if (parseExpectedWithoutAdvancing(Kind::CloseBraceToken)) {
		scanJsxText();
	}
	return finishNode(factory.newJsxExpression(dotDotDotToken, expression),
	                  pos);
}

Kind Parser::scanJsxText() {
	token = scanner->scanJsxToken();
	return token;
}

Kind Parser::scanJsxIdentifier() {
	token = scanner->scanJsxIdentifier();
	return token;
}

Kind Parser::scanJsxAttributeValue() {
	token = scanner->scanJsxAttributeValue();
	return token;
}

Node* Parser::parseJsxClosingElement(Node* open, bool inExpressionContext) {
	int pos = nodePos();
	parseExpected(Kind::LessThanSlashToken);
	Node* tagName = parseJsxElementName();
	if (parseExpectedWithDiagnostic(Kind::GreaterThanToken, nullptr,
	                                false)) {
		// manually advance the scanner in order to look for jsx text inside
		// jsx
		if (inExpressionContext ||
		    !tagNamesAreEquivalent(open->tagName(), tagName)) {
			nextToken();
		} else {
			scanJsxText();
		}
	}
	return finishNode(factory.newJsxClosingElement(tagName), pos);
}

Node* Parser::parseJsxOpeningOrSelfClosingElementOrOpeningFragment(
	bool inExpressionContext) {
	int pos = nodePos();
	parseExpected(Kind::LessThanToken);
	if (token == Kind::GreaterThanToken) {
		// See below for explanation of scanJsxText
		scanJsxText();
		return finishNode(factory.newJsxOpeningFragment(), pos);
	}
	Node* tagName = parseJsxElementName();
	NodeList* typeArguments = nullptr;
	if ((contextFlags & NodeFlagsJavaScriptFile) == 0) {
		typeArguments = parseTypeArguments();
	}
	Node* attributes = parseJsxAttributes();
	Node* result;
	if (token == Kind::GreaterThanToken) {
		// Closing tag, so scan the immediately-following text with the JSX
		// scanning instead of regular scanning to avoid treating illegal
		// characters (e.g. '#') as immediate scanning errors
		scanJsxText();
		result = factory.newJsxOpeningElement(tagName, typeArguments,
		                                      attributes);
	} else {
		parseExpected(Kind::SlashToken);
		if (parseExpectedWithoutAdvancing(Kind::GreaterThanToken)) {
			if (inExpressionContext) {
				nextToken();
			} else {
				scanJsxText();
			}
		}
		result = factory.newJsxSelfClosingElement(tagName, typeArguments,
		                                          attributes);
	}
	return finishNode(result, pos);
}

Node* Parser::parseJsxElementName() {
	int pos = nodePos();
	// JsxElement can have name in the form of
	//      propertyAccessExpression
	//      primaryExpression in the form of an identifier and "this" keyword
	// We can't just simply use parseLeftHandSideExpressionOrHigher because
	// then we will start consider class,function etc as a keyword. We only
	// want to consider "this" as a primaryExpression
	Node* initialExpression = parseJsxTagName();
	if (isJsxNamespacedName(initialExpression)) {
		return initialExpression;  // `a:b.c` is invalid syntax, don't even
		                           // look for the `.` if we parse `a:b`, and
		                           // let `parseAttribute` report "unexpected
		                           // :" instead.
	}
	Node* expression = initialExpression;
	while (parseOptional(Kind::DotToken)) {
		expression = finishNode(
			factory.newPropertyAccessExpression(
				expression, nullptr,
				parseRightSideOfDot(true, false, false), NodeFlagsNone),
			pos);
	}
	return expression;
}

Node* Parser::parseJsxTagName() {
	int pos = nodePos();
	scanJsxIdentifier();
	bool isThis = token == Kind::ThisKeyword;
	Node* tagName = parseIdentifierNameErrorOnUnicodeEscapeSequence();
	if (parseOptional(Kind::ColonToken)) {
		scanJsxIdentifier();
		return finishNode(
			factory.newJsxNamespacedName(
				tagName, parseIdentifierNameErrorOnUnicodeEscapeSequence()),
			pos);
	}
	if (isThis) {
		Node* result = factory.newKeywordExpression(Kind::ThisKeyword);
		return finishNode(result, pos);
	}
	return tagName;
}

Node* Parser::parseJsxAttributes() {
	int pos = nodePos();
	return finishNode(factory.newJsxAttributes(
			                  parseList(PCJsxAttributes,
			                            &Parser::parseJsxAttribute)),
	                  pos);
}

Node* Parser::parseJsxAttribute() {
	if (token == Kind::OpenBraceToken) {
		return parseJsxSpreadAttribute();
	}
	int pos = nodePos();
	return finishNode(
		factory.newJsxAttribute(parseJsxAttributeName(),
		                        parseJsxAttributeValue()),
		pos);
}

Node* Parser::parseJsxSpreadAttribute() {
	int pos = nodePos();
	parseExpected(Kind::OpenBraceToken);
	parseExpected(Kind::DotDotDotToken);
	Node* expression = parseExpression();
	parseExpected(Kind::CloseBraceToken);
	return finishNode(factory.newJsxSpreadAttribute(expression), pos);
}

Node* Parser::parseJsxAttributeName() {
	int pos = nodePos();
	scanJsxIdentifier();
	Node* attrName = parseIdentifierNameErrorOnUnicodeEscapeSequence();
	if (parseOptional(Kind::ColonToken)) {
		scanJsxIdentifier();
		return finishNode(
			factory.newJsxNamespacedName(
				attrName,
				parseIdentifierNameErrorOnUnicodeEscapeSequence()),
			pos);
	}
	return attrName;
}

Node* Parser::parseJsxAttributeValue() {
	if (token == Kind::EqualsToken) {
		if (scanJsxAttributeValue() == Kind::StringLiteral) {
			return parseLiteralExpression();
		}
		if (token == Kind::OpenBraceToken) {
			return parseJsxExpression(true);
		}
		if (token == Kind::LessThanToken) {
			// An attribute value must be a single JsxAttributeValue, so
			// don't allow the sibling-element recovery to wrap it in a
			// synthetic binary expression.
			return parseJsxElementOrSelfClosingElementOrFragment(
				true, -1, nullptr, true);
		}
		parseErrorAtCurrentToken(X_or_JSX_element_expected);
	}
	return nullptr;
}

Node* Parser::parseJsxClosingFragment(bool inExpressionContext) {
	int pos = nodePos();
	parseExpected(Kind::LessThanSlashToken);
	if (parseExpectedWithDiagnostic(
	        Kind::GreaterThanToken,
	        Expected_corresponding_closing_tag_for_JSX_fragment, false)) {
		// manually advance the scanner in order to look for jsx text
		// inside jsx
		if (inExpressionContext) {
			nextToken();
		} else {
			scanJsxText();
		}
	}
	return finishNode(factory.newJsxClosingFragment(), pos);
}

Node* Parser::parseSimpleUnaryExpression() {
	switch (token) {
	case Kind::PlusToken:
	case Kind::MinusToken:
	case Kind::TildeToken:
	case Kind::ExclamationToken:
		return parsePrefixUnaryExpression();
	case Kind::DeleteKeyword:
		return parseDeleteExpression();
	case Kind::TypeOfKeyword:
		return parseTypeOfExpression();
	case Kind::VoidKeyword:
		return parseVoidExpression();
	case Kind::LessThanToken:
		// Just like in parseUpdateExpression, we need to avoid parsing
		// type assertions when in JSX and we see an expression like
		// "+ <foo> bar".
		if (languageVariant == LanguageVariant::JSX) {
			return parseJsxElementOrSelfClosingElementOrFragment(
				true, -1, nullptr, true);
		}
		// This is modified UnaryExpression grammar in TypeScript
		//  UnaryExpression (modified):
		//      < type > UnaryExpression
		return parseTypeAssertion();
	case Kind::AwaitKeyword:
		if (isAwaitExpression()) {
			return parseAwaitExpression();
		}
		[[fallthrough]];
	default:
		return parseUpdateExpression();
	}
}

Node* Parser::parsePrefixUnaryExpression() {
	int pos = nodePos();
	Kind operator_ = token;
	nextToken();
	return finishNode(factory.newPrefixUnaryExpression(
			                  operator_, parseSimpleUnaryExpression()),
	                  pos);
}

Node* Parser::parseDeleteExpression() {
	int pos = nodePos();
	nextToken();
	return finishNode(
		factory.newDeleteExpression(parseSimpleUnaryExpression()), pos);
}

Node* Parser::parseTypeOfExpression() {
	int pos = nodePos();
	nextToken();
	return finishNode(
		factory.newTypeOfExpression(parseSimpleUnaryExpression()), pos);
}

Node* Parser::parseVoidExpression() {
	int pos = nodePos();
	nextToken();
	return finishNode(
		factory.newVoidExpression(parseSimpleUnaryExpression()), pos);
}

bool Parser::isAwaitExpression() {
	if (token == Kind::AwaitKeyword) {
		if (inAwaitContext()) {
			return true;
		}
		// here we are using similar heuristics as 'isYieldExpression'
		return lookAhead(
			&Parser::nextTokenIsIdentifierOrKeywordOrLiteralOnSameLine);
	}
	return false;
}

Node* Parser::parseAwaitExpression() {
	int pos = nodePos();
	nextToken();
	return finishNode(
		factory.newAwaitExpression(parseSimpleUnaryExpression()), pos);
}

Node* Parser::parseTypeAssertion() {
	// debug.Assert(p.languageVariant != LanguageVariantJSX)
	int pos = nodePos();
	parseExpected(Kind::LessThanToken);
	Node* typeNode = parseType();
	parseExpected(Kind::GreaterThanToken);
	Node* expression = parseSimpleUnaryExpression();
	return finishNode(factory.newTypeAssertion(typeNode, expression), pos);
}

Node* Parser::parseLeftHandSideExpressionOrHigher() {
	// Original Ecma:
	// LeftHandSideExpression: See 11.2
	//      NewExpression
	//      CallExpression
	//
	// Our simplification:
	//
	// LeftHandSideExpression: See 11.2
	//      MemberExpression
	//      CallExpression
	//
	// See comment in parseMemberExpressionOrHigher on how we replaced
	// NewExpression with MemberExpression to make our lives easier.
	//
	// CallExpression:
	//      MemberExpression Arguments
	//      CallExpression Arguments
	//      CallExpression[Expression]
	//      CallExpression.IdentifierName
	//      import (AssignmentExpression)
	//      super Arguments
	//      super.IdentifierName
	//
	// Because of the recursion in these calls, we need to bottom out first.
	// There are three bottom out states we can run into: 1) We see 'super'
	// which must start either of the last two CallExpression productions.
	// 2) We see 'import' which must start import call. 3) we have a
	// MemberExpression which either completes the
	// LeftHandSideExpression, or starts the beginning of the first four
	// CallExpression productions.
	int pos = nodePos();
	Node* expression;
	if (token == Kind::ImportKeyword) {
		if (lookAhead(&Parser::nextTokenIsOpenParenOrLessThan)) {
			// We don't want to eagerly consume all import keyword as
			// import call expression so we look ahead to find "("
			// For example:
			//      var foo3 = require("subfolder
			//      import * as foo1 from "module-from-node
			// We want this import to be a statement rather than import call
			// expression
			sourceFlags |= NodeFlagsPossiblyContainsDynamicImport;
			expression = parseKeywordExpression();
		} else if (lookAhead(&Parser::nextTokenIsDot)) {
			// This is an 'import.*' metaproperty (i.e. 'import.meta')
			nextToken();  // advance past the 'import'
			nextToken();  // advance past the dot
			expression = finishNode(
				factory.newMetaProperty(Kind::ImportKeyword,
				                        parseIdentifierName()),
				pos);
			if (expression->text() == "defer") {
				if (token == Kind::OpenParenToken ||
				    token == Kind::LessThanToken) {
					sourceFlags |=
						NodeFlagsPossiblyContainsDynamicImport;
				}
			} else {
				sourceFlags |= NodeFlagsPossiblyContainsImportMeta;
			}
		} else {
			expression = parseMemberExpressionOrHigher();
		}
	} else if (token == Kind::SuperKeyword) {
		expression = parseSuperExpression();
	} else {
		expression = parseMemberExpressionOrHigher();
	}
	// Now, we *may* be complete. However, we might have consumed the start
	// of a CallExpression or OptionalExpression. As such, we need to
	// consume the rest of it here to be complete.
	return parseCallExpressionRest(pos, expression);
}

bool Parser::nextTokenIsDot() {
	return nextToken() == Kind::DotToken;
}

Node* Parser::parseSuperExpression() {
	int pos = nodePos();
	Node* expression = parseKeywordExpression();
	if (token == Kind::LessThanToken) {
		int startPos = nodePos();
		NodeList* typeArguments = tryParseTypeArgumentsInExpression();
		if (typeArguments != nullptr) {
			parseErrorAt(startPos, nodePos(), X_super_may_not_use_type_arguments);
			if (!isTemplateStartOfTaggedTemplate()) {
				expression = finishNode(
					factory.newExpressionWithTypeArguments(
						expression, typeArguments),
					pos);
			}
		}
	}
	if (token == Kind::OpenParenToken || token == Kind::DotToken ||
	    token == Kind::OpenBracketToken) {
		return expression;
	}
	// If we have seen "super" it must be followed by '(' or '.'.
	// If it wasn't then just try to parse out a '.' and report an error.
	parseErrorAtCurrentToken(
		X_super_must_be_followed_by_an_argument_list_or_member_access);
	// private names will never work with `super` (`super.#foo`), but that's
	// a semantic error, not syntactic
	return finishNode(
		factory.newPropertyAccessExpression(
			expression, nullptr,
			parseRightSideOfDot(true, true, true), NodeFlagsNone),
		pos);
}

bool Parser::isTemplateStartOfTaggedTemplate() {
	return token == Kind::NoSubstitutionTemplateLiteral ||
	       token == Kind::TemplateHead;
}

NodeList* Parser::tryParseTypeArgumentsInExpression() {
	// TypeArguments must not be parsed in JavaScript files to avoid
	// ambiguity with binary operators. Check the cheap preconditions before
	// saving the parser state: unless the current token is `<` (or `<<`,
	// which reScanLessThanToken would split), there is nothing to
	// speculatively parse and the mark/rewind would be a no-op.
	if ((contextFlags & NodeFlagsJavaScriptFile) != 0 ||
	    (token != Kind::LessThanToken &&
	     token != Kind::LessThanLessThanToken)) {
		return nullptr;
	}
	ParserState state = mark();
	if (reScanLessThanToken() == Kind::LessThanToken) {
		nextToken();
		NodeList* typeArguments = parseDelimitedList(PCTypeArguments,
		                                             &Parser::parseType);
		// If it doesn't have the closing `>` then it's definitely not an
		// type argument list.
		if (reScanGreaterThanToken() == Kind::GreaterThanToken) {
			nextToken();
			// We successfully parsed a type argument list. The next token
			// determines whether we want to treat it as such. If the type
			// argument list is followed by `(` or a template literal, as in
			// `f<number>(42)`, we favor the type argument interpretation
			// even though JavaScript would view it as a relational
			// expression.
			if (canFollowTypeArgumentsInExpression()) {
				return typeArguments;
			}
		}
	}
	rewind(state);
	return nullptr;
}

bool Parser::canFollowTypeArgumentsInExpression() {
	switch (token) {
	// These tokens can follow a type argument list in a call expression:
	// foo<x>(
	// foo<T> `...`
	// foo<T> `...${100}...`
	case Kind::OpenParenToken:
	case Kind::NoSubstitutionTemplateLiteral:
	case Kind::TemplateHead:
		return true;
	// A type argument list followed by `<` never makes sense, and a type
	// argument list followed by `>` is ambiguous with a (re-scanned) `>>`
	// operator, so we disqualify both. Also, in this context, `+` and `-`
	// are unary operators, not binary operators.
	case Kind::LessThanToken:
	case Kind::GreaterThanToken:
	case Kind::PlusToken:
	case Kind::MinusToken:
		return false;
	default:
		break;
	}
	// We favor the type argument list interpretation when it is immediately
	// followed by a line break, a binary operator, or something that can't
	// start an expression.
	return hasPrecedingLineBreak() || isBinaryOperator() ||
	       !isStartOfExpression();
}

Node* Parser::parseMemberExpressionOrHigher() {
	// Note: to make our lives simpler, we decompose the NewExpression
	// productions and place ObjectCreationExpression and FunctionExpression
	// into PrimaryExpression. See the comment in
	// parseLeftHandSideExpressionOrHigher.
	int pos = nodePos();
	Node* expression = parsePrimaryExpression();
	return parseMemberExpressionRest(pos, expression, true);
}

Node* Parser::parseMemberExpressionRest(int pos, Node* expression,
                                        bool allowOptionalChain) {
	for (;;) {
		Node* questionDotToken = nullptr;
		bool isPropertyAccess = false;
		if (allowOptionalChain &&
		    isStartOfOptionalPropertyOrElementAccessChain()) {
			questionDotToken =
				parseExpectedToken(Kind::QuestionDotToken);
			isPropertyAccess = tokenIsIdentifierOrKeyword(token);
		} else {
			isPropertyAccess = parseOptional(Kind::DotToken);
		}
		if (isPropertyAccess) {
			expression = parsePropertyAccessExpressionRest(
				pos, expression, questionDotToken);
			continue;
		}
		// when in the [Decorator] context, we do not parse ElementAccess as
		// it could be part of a ComputedPropertyName
		if ((questionDotToken != nullptr || !inDecoratorContext()) &&
		    parseOptional(Kind::OpenBracketToken)) {
			expression = parseElementAccessExpressionRest(
				pos, expression, questionDotToken);
			continue;
		}
		if (isTemplateStartOfTaggedTemplate()) {
			// Absorb type arguments into TemplateExpression when preceding
			// expression is ExpressionWithTypeArguments
			if (questionDotToken == nullptr &&
			    isExpressionWithTypeArguments(expression)) {
				auto* original =
					expression->as<ExpressionWithTypeArguments>();
				expression = parseTaggedTemplateRest(
					pos, original->Expression, questionDotToken,
					original->TypeArguments);
				unparseExpressionWithTypeArguments(
					original->Expression, original->TypeArguments,
					expression);
			} else {
				expression = parseTaggedTemplateRest(pos, expression,
				                                     questionDotToken,
				                                     nullptr);
			}
			continue;
		}
		if (questionDotToken == nullptr) {
			if (token == Kind::ExclamationToken &&
			    !hasPrecedingLineBreak()) {
				nextToken();
				expression = checkJSSyntax(finishNode(
					factory.newNonNullExpression(expression, NodeFlagsNone),
					pos));
				continue;
			}
			NodeList* typeArguments =
				tryParseTypeArgumentsInExpression();
			if (typeArguments != nullptr) {
				expression = finishNode(
					factory.newExpressionWithTypeArguments(
						expression, typeArguments),
					pos);
				continue;
			}
		}
		return expression;
	}
}

bool Parser::isStartOfOptionalPropertyOrElementAccessChain() {
	return token == Kind::QuestionDotToken &&
	       lookAhead(
	           &Parser::nextTokenIsIdentifierOrKeywordOrOpenBracketOrTemplate);
}

bool Parser::nextTokenIsIdentifierOrKeywordOrOpenBracketOrTemplate() {
	nextToken();
	return tokenIsIdentifierOrKeyword(token) ||
	       token == Kind::OpenBracketToken ||
	       isTemplateStartOfTaggedTemplate();
}

Node* Parser::parsePropertyAccessExpressionRest(int pos, Node* expression,
                                                Node* questionDotToken) {
	Node* name = parseRightSideOfDot(true, true, true);
	bool isOptionalChain =
		questionDotToken != nullptr || tryReparseOptionalChain(expression);
	Node* propertyAccess = factory.newPropertyAccessExpression(
		expression, questionDotToken, name,
		isOptionalChain ? NodeFlagsOptionalChain : NodeFlagsNone);
	if (isOptionalChain && isPrivateIdentifier(name)) {
		parseErrorAtRange(
			skipRangeTrivia(name->loc),
			An_optional_chain_cannot_contain_private_identifiers);
	}
	if (isExpressionWithTypeArguments(expression)) {
		NodeList* typeArguments = expression->typeArgumentList();
		if (typeArguments != nullptr) {
			TextRange loc{typeArguments->pos() - 1,
			              skipTrivia(sourceText, typeArguments->end()) + 1};
			parseErrorAtRange(
				loc,
				An_instantiation_expression_cannot_be_followed_by_a_property_access);
		}
	}
	return finishNode(propertyAccess, pos);
}

bool Parser::tryReparseOptionalChain(Node* node) {
	if ((node->flags & NodeFlagsOptionalChain) != 0) {
		return true;
	}
	// check for an optional chain in a non-null expression
	if (isNonNullExpression(node)) {
		Node* expr = node->expression();
		while (isNonNullExpression(expr) &&
		       (expr->flags & NodeFlagsOptionalChain) == 0) {
			expr = expr->expression();
		}
		if ((expr->flags & NodeFlagsOptionalChain) != 0) {
			// this is part of an optional chain. Walk down from `node` to
			// `expression` and set the flag.
			while (isNonNullExpression(node)) {
				node->flags |= NodeFlagsOptionalChain;
				node = node->expression();
			}
			return true;
		}
	}
	return false;
}

Node* Parser::parseElementAccessExpressionRest(int pos, Node* expression,
                                               Node* questionDotToken) {
	Node* argumentExpression = createMissingIdentifier();
	if (token == Kind::CloseBracketToken) {
		parseErrorAt(nodePos(), nodePos(),
		             An_element_access_expression_should_take_an_argument);
	} else {
		argumentExpression = parseExpressionAllowIn();
	}
	parseExpected(Kind::CloseBracketToken);
	bool isOptionalChain =
		questionDotToken != nullptr || tryReparseOptionalChain(expression);
	return finishNode(
		factory.newElementAccessExpression(
			expression, questionDotToken, argumentExpression,
			isOptionalChain ? NodeFlagsOptionalChain : NodeFlagsNone),
		pos);
}

Node* Parser::parseCallExpressionRest(int pos, Node* expression) {
	for (;;) {
		expression = parseMemberExpressionRest(pos, expression, true);
		NodeList* typeArguments = nullptr;
		Node* questionDotToken = parseOptionalToken(Kind::QuestionDotToken);
		if (questionDotToken != nullptr) {
			typeArguments = tryParseTypeArgumentsInExpression();
			if (isTemplateStartOfTaggedTemplate()) {
				expression = parseTaggedTemplateRest(
					pos, expression, questionDotToken, typeArguments);
				continue;
			}
		}
		if (typeArguments != nullptr || token == Kind::OpenParenToken) {
			// Absorb type arguments into CallExpression when preceding
			// expression is ExpressionWithTypeArguments
			if (questionDotToken == nullptr &&
			    expression->kind == Kind::ExpressionWithTypeArguments) {
				typeArguments = expression->typeArgumentList();
				expression =
					expression->as<ExpressionWithTypeArguments>()
						->Expression;
			}
			Node* inner = expression;
			NodeList* argumentList = parseArgumentList();
			bool isOptionalChain =
				questionDotToken != nullptr ||
				tryReparseOptionalChain(expression);
			expression = checkJSSyntax(finishNode(
				factory.newCallExpression(
					expression, questionDotToken, typeArguments,
					argumentList,
					isOptionalChain ? NodeFlagsOptionalChain
					                : NodeFlagsNone),
				pos));
			unparseExpressionWithTypeArguments(inner, typeArguments,
			                                   expression);
			continue;
		}
		if (questionDotToken != nullptr) {
			// We parsed `?.` but then failed to parse anything, so report a
			// missing identifier here.
			parseErrorAtCurrentToken(Identifier_expected);
			Node* name = createMissingIdentifier();
			expression = finishNode(
				factory.newPropertyAccessExpression(
					expression, questionDotToken, name,
					NodeFlagsOptionalChain),
				pos);
		}
		break;
	}
	return expression;
}

NodeList* Parser::parseArgumentList() {
	parseExpected(Kind::OpenParenToken);
	NodeList* result = parseDelimitedList(PCArgumentExpressions,
	                                      &Parser::parseArgumentExpression);
	parseExpected(Kind::CloseParenToken);
	return result;
}

Node* Parser::parseArgumentExpression() {
	return doInContext(NodeFlagsDisallowInContext |
	                       NodeFlagsDecoratorContext,
	                   false,
	                   &Parser::parseArgumentOrArrayLiteralElement);
}

Node* Parser::parseArgumentOrArrayLiteralElement() {
	switch (token) {
	case Kind::DotDotDotToken:
		return parseSpreadElement();
	case Kind::CommaToken:
		return finishNode(factory.newOmittedExpression(), nodePos());
	default:
		break;
	}
	return parseAssignmentExpressionOrHigher();
}

Node* Parser::parseSpreadElement() {
	int pos = nodePos();
	parseExpected(Kind::DotDotDotToken);
	Node* expression = parseAssignmentExpressionOrHigher();
	return finishNode(factory.newSpreadElement(expression), pos);
}

Node* Parser::parseTaggedTemplateRest(int pos, Node* tag,
                                      Node* questionDotToken,
                                      NodeList* typeArguments) {
	Node* template_;
	if (token == Kind::NoSubstitutionTemplateLiteral) {
		reScanTemplateToken(true);
		template_ = parseLiteralExpression();
	} else {
		template_ = parseTemplateExpression(true);
	}
	bool isOptionalChain = questionDotToken != nullptr ||
	                       (tag->flags & NodeFlagsOptionalChain) != 0;
	return checkJSSyntax(finishNode(
		factory.newTaggedTemplateExpression(
			tag, questionDotToken, typeArguments, template_,
			isOptionalChain ? NodeFlagsOptionalChain : NodeFlagsNone),
		pos));
}

Node* Parser::parseTemplateExpression(bool isTaggedTemplate) {
	int pos = nodePos();
	return finishNode(factory.newTemplateExpression(
			                  parseTemplateHead(isTaggedTemplate),
			                  parseTemplateSpans(isTaggedTemplate)),
	                  pos);
}

NodeList* Parser::parseTemplateSpans(bool isTaggedTemplate) {
	int pos = nodePos();
	std::vector<Node*> list;
	for (;;) {
		Node* span = parseTemplateSpan(isTaggedTemplate);
		list.push_back(span);
		if (span->as<TemplateSpan>()->Literal->kind !=
		    Kind::TemplateMiddle) {
			break;
		}
	}
	return newNodeList(TextRange{pos, nodePos()}, list);
}

Node* Parser::parseTemplateSpan(bool isTaggedTemplate) {
	int pos = nodePos();
	Node* expression = parseExpressionAllowIn();
	Node* literal = parseLiteralOfTemplateSpan(isTaggedTemplate);
	return finishNode(factory.newTemplateSpan(expression, literal), pos);
}

Node* Parser::parsePrimaryExpression() {
	switch (token) {
	case Kind::NoSubstitutionTemplateLiteral:
		if ((scanner->tokenFlags() & TokenFlagsIsInvalid) != 0) {
			reScanTemplateToken(false);
		}
		[[fallthrough]];
	case Kind::NumericLiteral:
	case Kind::BigIntLiteral:
	case Kind::StringLiteral:
		return parseLiteralExpression();
	case Kind::ThisKeyword:
	case Kind::SuperKeyword:
	case Kind::NullKeyword:
	case Kind::TrueKeyword:
	case Kind::FalseKeyword:
		return parseKeywordExpression();
	case Kind::OpenParenToken:
		return parseParenthesizedExpression();
	case Kind::OpenBracketToken:
		return parseArrayLiteralExpression();
	case Kind::OpenBraceToken:
		return parseObjectLiteralExpression();
	case Kind::AsyncKeyword:
		// Async arrow functions are parsed earlier in
		// parseAssignmentExpressionOrHigher. If we encounter `async [no
		// LineTerminator here] function` then this is an async function;
		// otherwise, its an identifier.
		if (!lookAhead(
		        &Parser::nextTokenIsFunctionKeywordOnSameLine)) {
			break;
		}
		return parseFunctionExpression();
	case Kind::AtToken:
		return parseDecoratedExpression();
	case Kind::ClassKeyword:
		return parseClassExpression();
	case Kind::FunctionKeyword:
		return parseFunctionExpression();
	case Kind::NewKeyword:
		return parseNewExpressionOrNewDotTarget();
	case Kind::SlashToken:
	case Kind::SlashEqualsToken:
		if (reScanSlashToken() == Kind::RegularExpressionLiteral) {
			return parseLiteralExpression();
		}
		break;
	case Kind::TemplateHead:
		return parseTemplateExpression(false);
	case Kind::PrivateIdentifier:
		return parsePrivateIdentifier();
	default:
		break;
	}
	return parseIdentifierWithDiagnostic(Expression_expected, nullptr);
}

Node* Parser::parseParenthesizedExpression() {
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	parseExpected(Kind::OpenParenToken);
	Node* expression = parseExpressionAllowIn();
	parseExpected(Kind::CloseParenToken);
	Node* result =
		finishNode(factory.newParenthesizedExpression(expression), pos);
	withJSDoc(result, jsdoc);
	return result;
}

Node* Parser::parseArrayLiteralExpression() {
	int pos = nodePos();
	int openBracketPosition = scanner->tokenStart();
	bool openBracketParsed = parseExpected(Kind::OpenBracketToken);
	bool multiLine = hasPrecedingLineBreak();
	NodeList* elements =
		parseDelimitedList(PCArrayLiteralMembers,
	                       &Parser::parseArgumentOrArrayLiteralElement);
	parseExpectedMatchingBrackets(Kind::OpenBracketToken,
	                              Kind::CloseBracketToken, openBracketParsed,
	                              openBracketPosition);
	return finishNode(
		factory.newArrayLiteralExpression(elements, multiLine), pos);
}

Node* Parser::parseObjectLiteralExpression() {
	int pos = nodePos();
	int openBracePosition = scanner->tokenStart();
	bool openBraceParsed = parseExpected(Kind::OpenBraceToken);
	bool multiLine = hasPrecedingLineBreak();
	NodeList* properties = parseDelimitedList(PCObjectLiteralMembers,
	                                          &Parser::parseObjectLiteralElement);
	parseExpectedMatchingBrackets(Kind::OpenBraceToken,
	                              Kind::CloseBraceToken, openBraceParsed,
	                              openBracePosition);
	return finishNode(
		factory.newObjectLiteralExpression(properties, multiLine), pos);
}

Node* Parser::parseObjectLiteralElement() {
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	if (parseOptional(Kind::DotDotDotToken)) {
		Node* expression = parseAssignmentExpressionOrHigher();
		Node* result =
			finishNode(factory.newSpreadAssignment(expression), pos);
		withJSDoc(result, jsdoc);
		return result;
	}
	ModifierList* modifiers = parseModifiersEx(true, false, false);
	if (parseContextualModifier(Kind::GetKeyword)) {
		return parseAccessorDeclaration(pos, jsdoc, modifiers,
		                                Kind::GetAccessor, ParseFlagsNone);
	}
	if (parseContextualModifier(Kind::SetKeyword)) {
		return parseAccessorDeclaration(pos, jsdoc, modifiers,
		                                Kind::SetAccessor, ParseFlagsNone);
	}
	Node* asteriskToken = parseOptionalToken(Kind::AsteriskToken);
	bool tokenIsIdentifier = isIdentifier();
	Node* name = parsePropertyName();
	// Disallowing of optional property assignments and definite assignment
	// assertion happens in the grammar checker.
	Node* postfixToken = parseOptionalToken(Kind::QuestionToken);
	// Decorators, Modifiers, questionToken, and exclamationToken are not
	// supported by property assignments and are reported in the grammar
	// checker
	if (postfixToken == nullptr) {
		postfixToken = parseOptionalToken(Kind::ExclamationToken);
	}
	if (asteriskToken != nullptr || token == Kind::OpenParenToken ||
	    token == Kind::LessThanToken) {
		return parseMethodDeclaration(pos, jsdoc, modifiers, asteriskToken,
		                              name, postfixToken, nullptr);
	}
	// check if it is short-hand property assignment or normal property
	// assignment
	// NOTE: if token is EqualsToken it is interpreted as
	// CoverInitializedName production
	// CoverInitializedName[Yield] :
	//     IdentifierReference[?Yield] Initializer[In, ?Yield]
	// this is necessary because ObjectLiteral productions are also used to
	// cover grammar for ObjectAssignmentPattern
	Node* node;
	bool isShorthandPropertyAssignment =
		tokenIsIdentifier && token != Kind::ColonToken;
	if (isShorthandPropertyAssignment) {
		Node* equalsToken = parseOptionalToken(Kind::EqualsToken);
		Node* initializer = nullptr;
		if (equalsToken != nullptr) {
			initializer = doInContext(
				NodeFlagsDisallowInContext, false,
				&Parser::parseAssignmentExpressionOrHigher);
		}
		node = factory.newShorthandPropertyAssignment(
			modifiers, name, postfixToken, nullptr, equalsToken,
			initializer);
	} else {
		parseExpected(Kind::ColonToken);
		Node* initializer = doInContext(
			NodeFlagsDisallowInContext, false,
			&Parser::parseAssignmentExpressionOrHigher);
		node = factory.newPropertyAssignment(modifiers, name, postfixToken,
		                                     nullptr, initializer);
	}
	finishNode(node, pos);
	withJSDoc(node, jsdoc);
	return node;
}

Node* Parser::parseFunctionExpression() {
	// GeneratorExpression:
	//      function* BindingIdentifier [Yield][opt](FormalParameters[Yield])
	//      { GeneratorBody }
	//
	// FunctionExpression:
	//      function BindingIdentifier[opt](FormalParameters){ FunctionBody }
	NodeFlags saveContextFlags = contextFlags;
	setContextFlags(NodeFlagsDecoratorContext, false);
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	ModifierList* modifiers = parseModifiers();
	parseExpected(Kind::FunctionKeyword);
	Node* asteriskToken = parseOptionalToken(Kind::AsteriskToken);
	bool isGenerator = asteriskToken != nullptr;
	bool isAsync = modifierListHasAsync(modifiers);
	ParseFlags signatureFlags =
		(isGenerator ? ParseFlagsYield : ParseFlagsNone) |
		(isAsync ? ParseFlagsAwait : ParseFlagsNone);
	Node* name;
	if (isGenerator && isAsync) {
		name = doInContext(
			NodeFlagsYieldContext | NodeFlagsAwaitContext, true,
			&Parser::parseOptionalBindingIdentifier);
	} else if (isGenerator) {
		name = doInContext(NodeFlagsYieldContext, true,
		                   &Parser::parseOptionalBindingIdentifier);
	} else if (isAsync) {
		name = doInContext(NodeFlagsAwaitContext, true,
		                   &Parser::parseOptionalBindingIdentifier);
	} else {
		name = parseOptionalBindingIdentifier();
	}
	NodeList* typeParameters = parseTypeParameters();
	NodeList* parameters = parseParameters(signatureFlags);
	Node* returnType = parseReturnType(Kind::ColonToken, false);
	Node* body = parseFunctionBlock(signatureFlags, nullptr);
	contextFlags = saveContextFlags;
	Node* result = factory.newFunctionExpression(
		modifiers, asteriskToken, name, typeParameters, parameters,
		returnType, nullptr, body);
	finishNode(result, pos);
	withJSDoc(result, jsdoc);
	checkJSSyntax(result);
	return result;
}

Node* Parser::parseOptionalBindingIdentifier() {
	if (isBindingIdentifier()) {
		return parseBindingIdentifier();
	}
	return nullptr;
}

Node* Parser::parseDecoratedExpression() {
	int pos = nodePos();
	JSDocScannerInfo jsdoc = jsdocScannerInfo();
	ModifierList* modifiers = parseModifiersEx(true, false, false);
	if (token == Kind::ClassKeyword) {
		return parseClassDeclarationOrExpression(pos, jsdoc, modifiers,
		                                         Kind::ClassExpression);
	}
	parseErrorAt(nodePos(), nodePos(), Expression_expected);
	return finishNode(factory.newMissingDeclaration(modifiers), pos);
}

void Parser::unparseExpressionWithTypeArguments(Node* expression,
                                                NodeList* typeArguments,
                                                Node* result) {
	// force overwrite the `.Parent` of the expression and type arguments to
	// erase the fact that they may have originally been parsed as an
	// ExpressionWithTypeArguments and be parented to such
	if (expression != nullptr) {
		expression->parent = result;
	}
	if (typeArguments != nullptr) {
		for (Node* a : typeArguments->nodes) {
			a->parent = result;
		}
	}
}

Node* Parser::parseNewExpressionOrNewDotTarget() {
	int pos = nodePos();
	parseExpected(Kind::NewKeyword);
	if (parseOptional(Kind::DotToken)) {
		Node* name = parseIdentifierName();
		return finishNode(factory.newMetaProperty(Kind::NewKeyword, name),
		                  pos);
	}
	int expressionPos = nodePos();
	Node* expression = parseMemberExpressionRest(
		expressionPos, parsePrimaryExpression(), false);
	NodeList* typeArguments = nullptr;
	// Absorb type arguments into NewExpression when preceding expression is
	// ExpressionWithTypeArguments
	if (expression->kind == Kind::ExpressionWithTypeArguments) {
		typeArguments = expression->typeArgumentList();
		expression =
			expression->as<ExpressionWithTypeArguments>()->Expression;
	}
	if (token == Kind::QuestionDotToken) {
		parseErrorAtCurrentToken(
			Invalid_optional_chain_from_new_expression_Did_you_mean_to_call_0,
			{std::string(getTextOfNodeFromSourceText(sourceText, expression,
			                                         false))});
	}
	NodeList* argumentList = nullptr;
	if (token == Kind::OpenParenToken) {
		argumentList = parseArgumentList();
	}
	Node* result = checkJSSyntax(
		finishNode(factory.newNewExpression(expression, typeArguments,
		                                    argumentList),
		           pos));
	unparseExpressionWithTypeArguments(expression, typeArguments, result);
	return result;
}

Node* Parser::parseKeywordExpression() {
	int pos = nodePos();
	Node* result = factory.newKeywordExpression(token);
	nextToken();
	return finishNode(result, pos);
}

Node* Parser::parseLiteralExpression() {
	int pos = nodePos();
	std::string_view text = scanner->tokenValue();
	uint32_t tokenFlags = scanner->tokenFlags();
	Node* result;
	switch (token) {
	case Kind::StringLiteral:
		result = factory.newStringLiteral(std::string(text), tokenFlags);
		break;
	case Kind::NumericLiteral:
		result = factory.newNumericLiteral(std::string(text), tokenFlags);
		break;
	case Kind::BigIntLiteral:
		result = factory.newBigIntLiteral(std::string(text), tokenFlags);
		break;
	case Kind::RegularExpressionLiteral:
		result =
			factory.newRegularExpressionLiteral(std::string(text),
			                                    tokenFlags);
		break;
	case Kind::NoSubstitutionTemplateLiteral:
		result = factory.newNoSubstitutionTemplateLiteral(std::string(text),
		                                                  tokenFlags);
		break;
	default:
		TSC_UNREACHABLE("Unhandled case in parseLiteralExpression");
	}
	nextToken();
	return finishNode(result, pos);
}

Node* Parser::parseIdentifierNameErrorOnUnicodeEscapeSequence() {
	if (scanner->hasUnicodeEscape() || scanner->hasExtendedUnicodeEscape()) {
		parseErrorAtCurrentToken(Unicode_escape_sequence_cannot_appear_here);
	}
	return createIdentifier(tokenIsIdentifierOrKeyword(token));
}

Node* Parser::parseBindingIdentifier() {
	return parseBindingIdentifierWithDiagnostic(nullptr);
}

Node* Parser::parseBindingIdentifierWithDiagnostic(
	const DiagnosticMessage* privateIdentifierDiagnosticMessage) {
	bool saveHasAwaitIdentifier = statementHasAwaitIdentifier;
	Node* id = createIdentifierWithDiagnostic(
		isBindingIdentifier(), nullptr,
		privateIdentifierDiagnosticMessage);
	statementHasAwaitIdentifier = saveHasAwaitIdentifier;
	return id;
}

Node* Parser::parseIdentifierName() {
	return parseIdentifierNameWithDiagnostic(nullptr);
}

Node* Parser::parseIdentifierNameWithDiagnostic(
	const DiagnosticMessage* diagnosticMessage) {
	return createIdentifierWithDiagnostic(
		tokenIsIdentifierOrKeyword(token), diagnosticMessage, nullptr);
}

Node* Parser::parseIdentifier() {
	return parseIdentifierWithDiagnostic(nullptr, nullptr);
}

Node* Parser::parseIdentifierWithDiagnostic(
	const DiagnosticMessage* diagnosticMessage,
	const DiagnosticMessage* privateIdentifierDiagnosticMessage) {
	return createIdentifierWithDiagnostic(isIdentifier(), diagnosticMessage,
	                                      privateIdentifierDiagnosticMessage);
}

Node* Parser::createIdentifier(bool isIdentifier) {
	return createIdentifierWithDiagnostic(isIdentifier, nullptr, nullptr);
}

Node* Parser::createIdentifierWithDiagnostic(
	bool isIdentifier, const DiagnosticMessage* diagnosticMessage,
	const DiagnosticMessage* privateIdentifierDiagnosticMessage) {
	if (isIdentifier) {
		int pos;
		if (scanner->hasPrecedingJSDocLeadingAsterisks()) {
			pos = scanner->tokenStart();
		} else {
			pos = nodePos();
		}
		std::string text(scanner->tokenValue());
		nextTokenWithoutCheck();
		return finishNode(newIdentifier(std::move(text)), pos);
	}
	if (token == Kind::PrivateIdentifier) {
		if (privateIdentifierDiagnosticMessage != nullptr) {
			parseErrorAtCurrentToken(privateIdentifierDiagnosticMessage);
		} else {
			parseErrorAtCurrentToken(
				Private_identifiers_are_not_allowed_outside_class_bodies);
		}
		return createIdentifier(true);
	}
	// Only for end of file because the error gets reported incorrectly on
	// embedded script tags.
	bool reportAtCurrentPosition = token == Kind::EndOfFile;
	if (diagnosticMessage != nullptr) {
		if (reportAtCurrentPosition) {
			int pos = scanner->tokenFullStart();
			parseErrorAt(pos, pos, diagnosticMessage);
		} else {
			parseErrorAtCurrentToken(diagnosticMessage);
		}
	} else if (isReservedWord(token)) {
		if (reportAtCurrentPosition) {
			int pos = scanner->tokenFullStart();
			parseErrorAt(pos, pos,
			             Identifier_expected_0_is_a_reserved_word_that_cannot_be_used_here,
			             {std::string(scanner->tokenText())});
		} else {
			parseErrorAtCurrentToken(
				Identifier_expected_0_is_a_reserved_word_that_cannot_be_used_here,
				{std::string(scanner->tokenText())});
		}
	} else {
		if (reportAtCurrentPosition) {
			int pos = scanner->tokenFullStart();
			parseErrorAt(pos, pos, Identifier_expected);
		} else {
			parseErrorAtCurrentToken(Identifier_expected);
		}
	}
	return createMissingIdentifier();
}

NodeList* Parser::newNodeList(TextRange loc, std::vector<Node*> nodes) {
	NodeList* list = factory.newNodeList(std::move(nodes));
	list->loc = loc;
	return list;
}

ModifierList* Parser::newModifierList(TextRange loc,
                                      std::vector<Node*> nodes) {
	ModifierList* list = factory.newModifierList(std::move(nodes));
	list->loc = loc;
	return list;
}

Node* Parser::finishNode(Node* node, int pos) {
	return finishNodeWithEnd(node, pos, nodePos());
}

Node* Parser::finishNodeWithEnd(Node* node, int pos, int end) {
	node->loc = TextRange{pos, end};
	node->flags |= contextFlags;
	if (hasParseError) {
		node->flags |= NodeFlagsThisNodeHasError;
		hasParseError = false;
	}
	overrideParentInImmediateChildren(node);
	return node;
}

void Parser::overrideParentInImmediateChildren(Node* node) {
	currentParent = node;
	node->forEachChild(setParentFromContext);
	currentParent = nullptr;
}

bool Parser::nextTokenIsSlash() {
	return nextToken() == Kind::SlashToken;
}

bool Parser::scanTypeMemberStart() {
	// Return true if we have the start of a signature member
	if (token == Kind::OpenParenToken || token == Kind::LessThanToken ||
	    token == Kind::GetKeyword || token == Kind::SetKeyword) {
		return true;
	}
	bool idToken = false;
	// Eat up all modifiers, but hold on to the last one in case it is
	// actually an identifier
	while (isModifierKind(token)) {
		idToken = true;
		nextToken();
	}
	// Index signatures and computed property names are type members
	if (token == Kind::OpenBracketToken) {
		return true;
	}
	// Try to get the first property-like token following all modifiers
	if (isLiteralPropertyName()) {
		idToken = true;
		nextToken();
	}
	// If we were able to get any potential identifier, check that it is
	// the start of a member declaration
	if (idToken) {
		return token == Kind::OpenParenToken ||
		       token == Kind::LessThanToken ||
		       token == Kind::QuestionToken || token == Kind::ColonToken ||
		       token == Kind::CommaToken || canParseSemicolon();
	}
	return false;
}

bool Parser::scanClassMemberStart() {
	Kind idToken = Kind::Unknown;
	if (token == Kind::AtToken) {
		return true;
	}
	// Eat up all modifiers, but hold on to the last one in case it is
	// actually an identifier.
	while (isModifierKind(token)) {
		idToken = token;
		// If the idToken is a class modifier (protected, private, public,
		// and static), it is certain that we are starting to parse class
		// member. This allows better error recovery
		// Example:
		//      public foo() ...     // true
		//      public @dec blah ... // true; we will then report an error
		//      later
		//      export public ...    // true; we will then report an error
		//      later
		if (isClassMemberModifier(idToken)) {
			return true;
		}
		nextToken();
	}
	if (token == Kind::AsteriskToken) {
		return true;
	}
	// Try to get the first property-like token following all modifiers.
	// This can either be an identifier or the 'get' or 'set' keywords.
	if (isLiteralPropertyName()) {
		idToken = token;
		nextToken();
	}
	// Index signatures and computed properties are class members; we can
	// parse.
	if (token == Kind::OpenBracketToken) {
		return true;
	}
	// If we were able to get any potential identifier...
	if (idToken != Kind::Unknown) {
		// If we have a non-keyword identifier, or if we have an accessor,
		// then it's safe to parse.
		if (!isKeyword(idToken) || idToken == Kind::SetKeyword ||
		    idToken == Kind::GetKeyword) {
			return true;
		}
		// If it *is* a keyword, but not an accessor, check a little farther
		// along to see if it should actually be parsed as a class member.
		switch (token) {
		case Kind::OpenParenToken:   // Method declaration
		case Kind::LessThanToken:    // Generic Method declaration
		case Kind::ExclamationToken:  // Non-null assertion on property name
		case Kind::ColonToken:       // Type Annotation for declaration
		case Kind::EqualsToken:      // Initializer for declaration
		case Kind::QuestionToken:    // Not valid, but permitted so that it
		                             // gets caught later on.
			return true;
		default:
			break;
		}
		// Covers
		//  - Semicolons     (declaration termination)
		//  - Closing braces (end-of-class, must be declaration)
		//  - End-of-files   (not valid, but permitted so that it gets caught
		//    later on)
		//  - Line-breaks    (enabling *automatic semicolon insertion*)
		return canParseSemicolon();
	}
	return false;
}

bool Parser::canParseSemicolon() {
	// If there's a real semicolon, then we can always parse it out.
	// We can parse out an optional semicolon in ASI cases in the following
	// cases.
	return token == Kind::SemicolonToken || token == Kind::CloseBraceToken ||
	       token == Kind::EndOfFile || hasPrecedingLineBreak();
}

bool Parser::tryParseSemicolon() {
	if (!canParseSemicolon()) {
		return false;
	}
	if (token == Kind::SemicolonToken) {
		// consume the semicolon if it was explicitly provided.
		nextToken();
	}
	return true;
}

bool Parser::parseSemicolon() {
	return tryParseSemicolon() || parseExpected(Kind::SemicolonToken);
}

bool Parser::isLiteralPropertyName() {
	return tokenIsIdentifierOrKeyword(token) ||
	       token == Kind::StringLiteral || token == Kind::NumericLiteral ||
	       token == Kind::BigIntLiteral;
}

bool Parser::isStartOfStatement() {
	switch (token) {
	// 'catch' and 'finally' do not actually indicate that the code is part
	// of a statement, however, we say they are here so that we may
	// gracefully parse them and error later.
	case Kind::AtToken:
	case Kind::SemicolonToken:
	case Kind::OpenBraceToken:
	case Kind::VarKeyword:
	case Kind::LetKeyword:
	case Kind::UsingKeyword:
	case Kind::FunctionKeyword:
	case Kind::ClassKeyword:
	case Kind::EnumKeyword:
	case Kind::IfKeyword:
	case Kind::DoKeyword:
	case Kind::WhileKeyword:
	case Kind::ForKeyword:
	case Kind::ContinueKeyword:
	case Kind::BreakKeyword:
	case Kind::ReturnKeyword:
	case Kind::WithKeyword:
	case Kind::SwitchKeyword:
	case Kind::ThrowKeyword:
	case Kind::TryKeyword:
	case Kind::DebuggerKeyword:
	case Kind::CatchKeyword:
	case Kind::FinallyKeyword:
		return true;
	case Kind::ImportKeyword:
		return isStartOfDeclaration() ||
		       isNextTokenOpenParenOrLessThanOrDot();
	case Kind::ConstKeyword:
	case Kind::ExportKeyword:
		return isStartOfDeclaration();
	case Kind::AsyncKeyword:
	case Kind::DeclareKeyword:
	case Kind::InterfaceKeyword:
	case Kind::ModuleKeyword:
	case Kind::NamespaceKeyword:
	case Kind::TypeKeyword:
	case Kind::GlobalKeyword:
	case Kind::DeferKeyword:
		// When these don't start a declaration, they're an identifier in an
		// expression statement
		return true;
	case Kind::AccessorKeyword:
	case Kind::PublicKeyword:
	case Kind::PrivateKeyword:
	case Kind::ProtectedKeyword:
	case Kind::StaticKeyword:
	case Kind::ReadonlyKeyword:
		// When these don't start a declaration, they may be the start of a
		// class member if an identifier immediately follows. Otherwise
		// they're an identifier in an expression statement.
		return isStartOfDeclaration() ||
		       !lookAhead(
		           &Parser::nextTokenIsIdentifierOrKeywordOnSameLine);
	default:
		return isStartOfExpression();
	}
}

bool Parser::isStartOfDeclaration() {
	return lookAhead(&Parser::scanStartOfDeclaration);
}

bool Parser::scanStartOfDeclaration() {
	for (;;) {
		switch (token) {
		case Kind::VarKeyword:
		case Kind::LetKeyword:
		case Kind::ConstKeyword:
		case Kind::FunctionKeyword:
		case Kind::ClassKeyword:
		case Kind::EnumKeyword:
			return true;
		case Kind::UsingKeyword:
			return isUsingDeclaration();
		case Kind::AwaitKeyword:
			return isAwaitUsingDeclaration();
		// 'declare', 'module', 'namespace', 'interface'* and 'type' are all
		// legal JavaScript identifiers; however, an identifier cannot be
		// followed by another identifier on the same line. This is what we
		// count on to parse out the respective declarations. For instance,
		// we exploit this to say that
		//
		//    namespace n
		//
		// can be none other than the beginning of a namespace declaration,
		// but need to respect that JavaScript sees
		//
		//    namespace
		//    n
		//
		// as the identifier 'namespace' on one line followed by the
		// identifier 'n' on another. We need to look one token ahead to see
		// if it permissible to try parsing a declaration.
		//
		// *Note*: 'interface' is actually a strict mode reserved word. So
		// while
		//
		//   "use strict"
		//   interface
		//   I {}
		//
		// could be legal, it would add complexity for very little gain.
		case Kind::InterfaceKeyword:
		case Kind::TypeKeyword:
		case Kind::DeferKeyword:
			return nextTokenIsIdentifierOnSameLine();
		case Kind::ModuleKeyword:
		case Kind::NamespaceKeyword:
			return nextTokenIsIdentifierOrStringLiteralOnSameLine();
		case Kind::AbstractKeyword:
		case Kind::AccessorKeyword:
		case Kind::AsyncKeyword:
		case Kind::DeclareKeyword:
		case Kind::PrivateKeyword:
		case Kind::ProtectedKeyword:
		case Kind::PublicKeyword:
		case Kind::ReadonlyKeyword: {
			Kind previousToken = token;
			nextToken();
			// ASI takes effect for this modifier.
			if (hasPrecedingLineBreak()) {
				return false;
			}
			if (previousToken == Kind::DeclareKeyword &&
			    token == Kind::TypeKeyword) {
				// If we see 'declare type', then commit to parsing a type
				// alias. parseTypeAliasDeclaration will report
				// Line_break_not_permitted_here if needed.
				return true;
			}
			continue;
		}
		case Kind::GlobalKeyword:
			nextToken();
			return token == Kind::OpenBraceToken ||
			       token == Kind::Identifier ||
			       token == Kind::ExportKeyword;
		case Kind::ImportKeyword:
			nextToken();
			return token == Kind::DeferKeyword ||
			       token == Kind::StringLiteral ||
			       token == Kind::AsteriskToken ||
			       token == Kind::OpenBraceToken ||
			       tokenIsIdentifierOrKeyword(token);
		case Kind::ExportKeyword:
			nextToken();
			if (token == Kind::EqualsToken ||
			    token == Kind::AsteriskToken ||
			    token == Kind::OpenBraceToken ||
			    token == Kind::DefaultKeyword ||
			    token == Kind::AsKeyword || token == Kind::AtToken) {
				return true;
			}
			if (token == Kind::TypeKeyword) {
				nextToken();
				return token == Kind::AsteriskToken ||
				       token == Kind::OpenBraceToken ||
				       (isIdentifier() && !hasPrecedingLineBreak());
			}
			continue;
		case Kind::StaticKeyword:
			nextToken();
			continue;
		default:
			break;
		}
		return false;
	}
}

bool Parser::isStartOfExpression() {
	if (isStartOfLeftHandSideExpression()) {
		return true;
	}
	switch (token) {
	case Kind::PlusToken:
	case Kind::MinusToken:
	case Kind::TildeToken:
	case Kind::ExclamationToken:
	case Kind::DeleteKeyword:
	case Kind::TypeOfKeyword:
	case Kind::VoidKeyword:
	case Kind::PlusPlusToken:
	case Kind::MinusMinusToken:
	case Kind::LessThanToken:
	case Kind::AwaitKeyword:
	case Kind::YieldKeyword:
	case Kind::PrivateIdentifier:
	case Kind::AtToken:
		// Yield/await always starts an expression. Either it is an
		// identifier (in which case it is definitely an expression). Or
		// it's a keyword (either because we're in a generator or async
		// function, or in strict mode (or both)) and it started a yield or
		// await expression.
		return true;
	default:
		break;
	}
	// Error tolerance. If we see the start of some binary operator, we
	// consider that the start of an expression. That way we'll parse out a
	// missing identifier, give a good message about an identifier being
	// missing, and then consume the rest of the binary expression.
	if (isBinaryOperator()) {
		return true;
	}
	return isIdentifier();
}

bool Parser::isStartOfLeftHandSideExpression() {
	switch (token) {
	case Kind::ThisKeyword:
	case Kind::SuperKeyword:
	case Kind::NullKeyword:
	case Kind::TrueKeyword:
	case Kind::FalseKeyword:
	case Kind::NumericLiteral:
	case Kind::BigIntLiteral:
	case Kind::StringLiteral:
	case Kind::NoSubstitutionTemplateLiteral:
	case Kind::TemplateHead:
	case Kind::OpenParenToken:
	case Kind::OpenBracketToken:
	case Kind::OpenBraceToken:
	case Kind::FunctionKeyword:
	case Kind::ClassKeyword:
	case Kind::NewKeyword:
	case Kind::SlashToken:
	case Kind::SlashEqualsToken:
	case Kind::Identifier:
		return true;
	case Kind::ImportKeyword:
		return isNextTokenOpenParenOrLessThanOrDot();
	default:
		break;
	}
	return isIdentifier();
}

bool Parser::isStartOfType(bool inStartOfParameter) {
	switch (token) {
	case Kind::AnyKeyword:
	case Kind::UnknownKeyword:
	case Kind::StringKeyword:
	case Kind::NumberKeyword:
	case Kind::BigIntKeyword:
	case Kind::BooleanKeyword:
	case Kind::ReadonlyKeyword:
	case Kind::SymbolKeyword:
	case Kind::UniqueKeyword:
	case Kind::VoidKeyword:
	case Kind::UndefinedKeyword:
	case Kind::NullKeyword:
	case Kind::ThisKeyword:
	case Kind::TypeOfKeyword:
	case Kind::NeverKeyword:
	case Kind::OpenBraceToken:
	case Kind::OpenBracketToken:
	case Kind::LessThanToken:
	case Kind::BarToken:
	case Kind::AmpersandToken:
	case Kind::NewKeyword:
	case Kind::StringLiteral:
	case Kind::NumericLiteral:
	case Kind::BigIntLiteral:
	case Kind::TrueKeyword:
	case Kind::FalseKeyword:
	case Kind::ObjectKeyword:
	case Kind::AsteriskToken:
	case Kind::QuestionToken:
	case Kind::ExclamationToken:
	case Kind::DotDotDotToken:
	case Kind::InferKeyword:
	case Kind::ImportKeyword:
	case Kind::AssertsKeyword:
	case Kind::NoSubstitutionTemplateLiteral:
	case Kind::TemplateHead:
		return true;
	case Kind::FunctionKeyword:
		return !inStartOfParameter;
	case Kind::MinusToken:
		return !inStartOfParameter &&
		       lookAhead(&Parser::nextTokenIsNumericOrBigIntLiteral);
	case Kind::OpenParenToken:
		// Only consider '(' the start of a type if followed by ')', '...',
		// an identifier, a modifier, or something that starts a type. We
		// don't want to consider things like '(1)' a type.
		return !inStartOfParameter &&
		       lookAhead(&Parser::nextIsParenthesizedOrFunctionType);
	default:
		break;
	}
	return isIdentifier();
}

bool Parser::nextTokenIsNumericOrBigIntLiteral() {
	nextToken();
	return token == Kind::NumericLiteral || token == Kind::BigIntLiteral;
}

bool Parser::nextIsParenthesizedOrFunctionType() {
	nextToken();
	return token == Kind::CloseParenToken || isStartOfParameter(false) ||
	       isStartOfType(false);
}

bool Parser::isStartOfParameter(bool isJSDocParameter) {
	return token == Kind::DotDotDotToken ||
	       isBindingIdentifierOrPrivateIdentifierOrPattern() ||
	       isModifierKind(token) || token == Kind::AtToken ||
	       isStartOfType(!isJSDocParameter);
}

bool Parser::isBindingIdentifierOrPrivateIdentifierOrPattern() {
	return token == Kind::OpenBraceToken || token == Kind::OpenBracketToken ||
	       token == Kind::PrivateIdentifier || isBindingIdentifier();
}

bool Parser::isNextTokenOpenParenOrLessThanOrDot() {
	return lookAhead(&Parser::nextTokenIsOpenParenOrLessThanOrDot);
}

bool Parser::nextTokenIsOpenParenOrLessThanOrDot() {
	switch (nextToken()) {
	case Kind::OpenParenToken:
	case Kind::LessThanToken:
	case Kind::DotToken:
		return true;
	default:
		break;
	}
	return false;
}

bool Parser::nextTokenIsIdentifierOnSameLine() {
	nextToken();
	return isIdentifier() && !hasPrecedingLineBreak();
}

bool Parser::nextTokenIsIdentifierOrStringLiteralOnSameLine() {
	nextToken();
	return (isIdentifier() || token == Kind::StringLiteral) &&
	       !hasPrecedingLineBreak();
}

// Ignore strict mode flag because we will report an error in type checker
// instead.
bool Parser::isIdentifier() {
	if (token == Kind::Identifier) {
		return true;
	}
	// If we have a 'yield' keyword, and we're in the [yield] context, then
	// 'yield' is considered a keyword and is not an identifier.
	// If we have a 'await' keyword, and we're in the [Await] context, then
	// 'await' is considered a keyword and is not an identifier.
	if ((token == Kind::YieldKeyword && inYieldContext()) ||
	    (token == Kind::AwaitKeyword && inAwaitContext())) {
		return false;
	}
	return token > KindLastReservedWord;
}

bool Parser::isBindingIdentifier() {
	// `let await`/`let yield` in [Yield] or [Await] are allowed here and
	// disallowed in the binder.
	return token == Kind::Identifier || token > KindLastReservedWord;
}

bool Parser::isImportAttributeName() {
	return tokenIsIdentifierOrKeyword(token) || token == Kind::StringLiteral;
}

bool Parser::isBinaryOperator() {
	if (inDisallowInContext() && token == Kind::InKeyword) {
		return false;
	}
	return getBinaryOperatorPrecedence(token) != OperatorPrecedenceInvalid;
}

bool Parser::isValidHeritageClauseObjectLiteral() {
	return lookAhead(&Parser::nextIsValidHeritageClauseObjectLiteral);
}

bool Parser::nextIsValidHeritageClauseObjectLiteral() {
	if (nextToken() == Kind::CloseBraceToken) {
		// if we see "extends {}" then only treat the {} as what we're
		// extending (and not the class body) if we have:
		//
		//      extends {} {
		//      extends {},
		//      extends {} extends
		//      extends {} implements
		Kind next = nextToken();
		return next == Kind::CommaToken || next == Kind::OpenBraceToken ||
		       next == Kind::ExtendsKeyword ||
		       next == Kind::ImplementsKeyword;
	}
	return true;
}

bool Parser::isHeritageClause() {
	return token == Kind::ExtendsKeyword || token == Kind::ImplementsKeyword;
}

bool Parser::isHeritageClauseExtendsOrImplementsKeyword() {
	return isHeritageClause() && lookAhead(&Parser::nextIsStartOfExpression);
}

bool Parser::nextIsStartOfExpression() {
	nextToken();
	return isStartOfExpression();
}

bool Parser::isUsingDeclaration() {
	// 'using' always starts a lexical declaration if followed by an
	// identifier. We also eagerly parse |ObjectBindingPattern| so that we
	// can report a grammar error during check. We don't parse out
	// |ArrayBindingPattern| since it potentially conflicts with element
	// access (i.e., `using[x]`).
	return lookAhead([](Parser* p) -> bool {
		return p->nextTokenIsBindingIdentifierOrStartOfDestructuringOnSameLine(
			false);
	});
}

bool Parser::nextTokenIsEqualsOrSemicolonOrColonToken() {
	nextToken();
	return token == Kind::EqualsToken || token == Kind::SemicolonToken ||
	       token == Kind::ColonToken;
}

bool Parser::nextTokenIsBindingIdentifierOrStartOfDestructuringOnSameLine(
	bool disallowOf) {
	nextToken();
	if (disallowOf && token == Kind::OfKeyword) {
		return lookAhead(&Parser::nextTokenIsEqualsOrSemicolonOrColonToken);
	}
	return (isBindingIdentifier() || token == Kind::OpenBraceToken) &&
	       !hasPrecedingLineBreak();
}

bool Parser::nextTokenIsBindingIdentifierOrStartOfDestructuringOnSameLineDisallowOf() {
	return nextTokenIsBindingIdentifierOrStartOfDestructuringOnSameLine(true);
}

bool Parser::isAwaitUsingDeclaration() {
	return lookAhead(
		&Parser::nextIsUsingKeywordThenBindingIdentifierOrStartOfObjectDestructuringOnSameLine);
}

bool Parser::nextIsUsingKeywordThenBindingIdentifierOrStartOfObjectDestructuringOnSameLine() {
	return nextToken() == Kind::UsingKeyword &&
	       nextTokenIsBindingIdentifierOrStartOfDestructuringOnSameLine(false);
}

bool Parser::nextTokenIsTokenStringLiteral() {
	return nextToken() == Kind::StringLiteral;
}

void Parser::setContextFlags(NodeFlags flags, bool value) {
	if (value) {
		contextFlags |= flags;
	} else {
		contextFlags &= ~flags;
	}
}

bool Parser::inYieldContext() {
	return (contextFlags & NodeFlagsYieldContext) != 0;
}

bool Parser::inDisallowInContext() {
	return (contextFlags & NodeFlagsDisallowInContext) != 0;
}

bool Parser::inDisallowConditionalTypesContext() {
	return (contextFlags & NodeFlagsDisallowConditionalTypesContext) != 0;
}

bool Parser::inDecoratorContext() {
	return (contextFlags & NodeFlagsDecoratorContext) != 0;
}

bool Parser::inAwaitContext() {
	return (contextFlags & NodeFlagsAwaitContext) != 0;
}

bool isReservedWord(Kind token) {
	return KindFirstReservedWord <= token && token <= KindLastReservedWord;
}

std::vector<Diagnostic*> attachFileToDiagnostics(
	std::vector<Diagnostic*> diagnostics, SourceFile* file) {
	for (Diagnostic* d : diagnostics) {
		d->SetFile(file);
		for (Diagnostic* r : d->relatedInformation) {
			r->SetFile(file);
		}
	}
	return diagnostics;
}

// ---------------------------------------------------------------------------
// Comment pragma extraction (parser.go:getCommentPragmas..extractQuotedString)
// ---------------------------------------------------------------------------

static bool match(std::string_view text, int pos, std::string_view s) {
	return text.substr(pos).starts_with(s);
}

static int skipBlanks(std::string_view text, int pos) {
	while (pos < (int)text.size() &&
	       (text[pos] == ' ' || text[pos] == '\t')) {
		pos++;
	}
	return pos;
}

static int skipNonBlanks(std::string_view text, int pos) {
	while (pos < (int)text.size() && text[pos] != ' ' &&
	       text[pos] != '\t' && text[pos] != '\r' && text[pos] != '\n') {
		pos++;
	}
	return pos;
}

static int skipTo(std::string_view text, int pos, std::string_view s) {
	if (pos >= (int)text.size()) {
		return -1;
	}
	size_t i = text.find(s, pos);
	if (i == std::string_view::npos) {
		return -1;
	}
	return pos + (int)i;
}

static int lineEndPos(std::string_view text, int pos) {
	while (pos < (int)text.size()) {
		uint32_t ch;
		int size = 1;
		uint8_t b0 = (uint8_t)text[pos];
		if (b0 < 0x80) {
			ch = b0;
		} else if ((b0 & 0xE0) == 0xC0 && pos + 1 < (int)text.size()) {
			ch = ((b0 & 0x1F) << 6) | ((uint8_t)text[pos + 1] & 0x3F);
			size = 2;
		} else if ((b0 & 0xF0) == 0xE0 && pos + 2 < (int)text.size()) {
			ch = ((b0 & 0x0F) << 12) | (((uint8_t)text[pos + 1] & 0x3F) << 6) |
			     ((uint8_t)text[pos + 2] & 0x3F);
			size = 3;
		} else if ((b0 & 0xF8) == 0xF0 && pos + 3 < (int)text.size()) {
			ch = ((b0 & 0x07) << 18) | (((uint8_t)text[pos + 1] & 0x3F) << 12) |
			     (((uint8_t)text[pos + 2] & 0x3F) << 6) |
			     ((uint8_t)text[pos + 3] & 0x3F);
			size = 4;
		} else {
			ch = 0xFFFD;
		}
		if (isLineBreak(ch)) {
			return pos;
		}
		pos += size;
	}
	return (int)text.size();
}

static std::string extractName(std::string_view text, int pos) {
	int start = pos;
	while (pos < (int)text.size() &&
	       ((text[pos] >= 'A' && text[pos] <= 'Z') ||
	        (text[pos] >= 'a' && text[pos] <= 'z') || text[pos] == '-')) {
		pos++;
	}
	std::string s(text.substr(start, pos - start));
	for (char& c : s) {
		if (c >= 'A' && c <= 'Z') {
			c += 32;
		}
	}
	return s;
}

static bool extractQuotedString(std::string_view text, int pos,
                                std::string& out) {
	if (pos == (int)text.size()) {
		return false;
	}
	char quote = text[pos];
	if (quote != '\'' && quote != '"') {
		return false;
	}
	pos++;
	int start = pos;
	while (pos < (int)text.size() && text[pos] != quote) {
		pos++;
	}
	if (pos == (int)text.size()) {
		return false;
	}
	out = std::string(text.substr(start, pos - start));
	return true;
}

static std::vector<Pragma> extractPragmas(CommentRange commentRange,
                                          std::string_view text) {
	if (commentRange.kind == Kind::SingleLineCommentTrivia) {
		int pos = 2;
		bool tripleSlash = match(text, pos, "/");
		if (tripleSlash) {
			pos++;
		}
		pos = skipBlanks(text, pos);
		if (tripleSlash && match(text, pos, "<")) {
			std::string tagName = extractName(text, pos + 1);
			if (tagName != "reference") {
				return {};
			}
			pos += 10;
			std::unordered_map<std::string, PragmaArgument> args;
			for (;;) {
				pos = skipBlanks(text, pos);
				if (match(text, pos, "/>")) {
					break;
				}
				std::string argName = extractName(text, pos);
				if (argName.empty()) {
					break;
				}
				pos = skipBlanks(text, pos + (int)argName.size());
				if (!match(text, pos, "=")) {
					break;
				}
				pos = skipBlanks(text, pos + 1);
				std::string value;
				if (!extractQuotedString(text, pos, value)) {
					break;
				}
				args[argName] = PragmaArgument{
					TextRange{commentRange.pos() + pos + 1,
					          commentRange.pos() + pos + 1 +
					              (int)value.size()},
					argName, value};
				pos += (int)value.size() + 2;
			}
			Pragma pr;
			static_cast<CommentRange&>(pr) = commentRange;
			pr.Name = "reference";
			pr.Args = std::move(args);
			return {pr};
		}
		if (match(text, pos, "@")) {
			pos++;
			std::string pragmaName = extractName(text, pos);
			if (!(pragmaName == "ts-check" ||
			      pragmaName == "ts-nocheck")) {
				return {};
			}
			Pragma pr;
			static_cast<CommentRange&>(pr) = commentRange;
			pr.Name = pragmaName;
			return {pr};
		}
	}
	if (commentRange.kind == Kind::MultiLineCommentTrivia) {
		if (text.ends_with("*/")) {
			text.remove_suffix(2);
		}
		int pos = 2;
		std::vector<Pragma> pragmas;
		for (;;) {
			pos = skipTo(text, pos, "@");
			if (pos < 0) {
				break;
			}
			// Mirrors the /@(\S+)(\s+(?:\S.*)?)?$/gm pragma regex used by
			// TypeScript: the '@' must be immediately followed by a
			// non-whitespace pragma name, and the remainder of the line is
			// consumed as that pragma's arguments. As a consequence, only
			// the first '@'-token on a line is considered, so an unrelated
			// '@token' earlier on the line (e.g. an email address) prevents
			// a later '@jsx' on the same line from being treated as a
			// pragma.
			int namePos = pos + 1;
			int nameEnd = skipNonBlanks(text, namePos);
			if (nameEnd == namePos) {
				pos++;
				continue;
			}
			int lineEnd = lineEndPos(text, pos);
			std::string pragmaName(text.substr(namePos, nameEnd - namePos));
			for (char& c : pragmaName) {
				if (c >= 'A' && c <= 'Z') {
					c += 32;
				}
			}
			if (pragmaName == "jsx" || pragmaName == "jsxfrag" ||
			    pragmaName == "jsximportsource" ||
			    pragmaName == "jsxruntime") {
				int start = skipBlanks(text, nameEnd);
				int argEnd = skipNonBlanks(text, start);
				if (argEnd != start) {
					Pragma pr;
					static_cast<CommentRange&>(pr) = commentRange;
					pr.Name = pragmaName;
					pr.Args["factory"] = PragmaArgument{
						TextRange{commentRange.pos() + start,
						          commentRange.pos() + argEnd},
						"factory",
						std::string(
							text.substr(start, argEnd - start))};
					pragmas.push_back(std::move(pr));
				}
			}
			pos = lineEnd;
		}
		return pragmas;
	}
	return {};
}

std::vector<Pragma> getCommentPragmas(NodeFactory* f,
                                    std::string_view sourceText) {
	std::vector<Pragma> pragmas;
	getLeadingCommentRanges(
		sourceText, 0, [&](CommentRange commentRange) {
			std::string_view comment =
				sourceText.substr(commentRange.pos(),
				                  commentRange.end() - commentRange.pos());
			auto extracted = extractPragmas(commentRange, comment);
			pragmas.insert(pragmas.end(), extracted.begin(),
			               extracted.end());
			return true;
		});
	return pragmas;
}

void Parser::processPragmasIntoFields(SourceFile* context) {
	context->CheckJsDirective = nullptr;
	context->ReferencedFiles.clear();
	context->TypeReferenceDirectives.clear();
	context->LibReferenceDirectives.clear();
	for (const Pragma& pragma : context->Pragmas) {
		if (pragma.Name == "reference") {
			auto find = [&](const char* key) -> const PragmaArgument* {
				auto it = pragma.Args.find(key);
				return it == pragma.Args.end() ? nullptr : &it->second;
			};
			const PragmaArgument* types = find("types");
			const PragmaArgument* lib = find("lib");
			const PragmaArgument* path = find("path");
			const PragmaArgument* resolutionMode = find("resolution-mode");
			const PragmaArgument* preserve = find("preserve");
			const PragmaArgument* noDefaultLib = find("no-default-lib");
			if (noDefaultLib != nullptr &&
			    noDefaultLib->Value == "true") {
				// Ignored.
			} else if (types != nullptr) {
				ResolutionMode parsed = ResolutionMode::None;
				if (resolutionMode != nullptr) {
					parsed = parseResolutionMode(
						resolutionMode->Value, resolutionMode->pos(),
						resolutionMode->end());
				}
				auto* fr = factory.newData<FileReference>();
				fr->pos_ = types->pos(); fr->end_ = types->end();
				fr->FileName = types->Value;
				fr->ResolutionMode = parsed;
				fr->Preserve = preserve != nullptr &&
				               preserve->Value == "true";
				context->TypeReferenceDirectives.push_back(fr);
			} else if (lib != nullptr) {
				auto* fr = factory.newData<FileReference>();
				fr->pos_ = lib->pos(); fr->end_ = lib->end();
				fr->FileName = lib->Value;
				fr->Preserve = preserve != nullptr &&
				               preserve->Value == "true";
				context->LibReferenceDirectives.push_back(fr);
			} else if (path != nullptr) {
				auto* fr = factory.newData<FileReference>();
				fr->pos_ = path->pos(); fr->end_ = path->end();
				fr->FileName = path->Value;
				fr->Preserve = preserve != nullptr &&
				               preserve->Value == "true";
				context->ReferencedFiles.push_back(fr);
			} else {
				parseErrorAtRange(pragma,
				                  Invalid_reference_directive_syntax);
			}
		} else if (pragma.Name == "ts-check" ||
		           pragma.Name == "ts-nocheck") {
			// _last_ of either nocheck or check in a file is the "winner"
			if (context->CheckJsDirective == nullptr ||
			    pragma.pos() > context->CheckJsDirective->Range.pos()) {
				auto* d = factory.newData<CheckJsDirective>();
				d->Enabled = pragma.Name == "ts-check";
				d->Range = static_cast<const CommentRange&>(pragma);
				context->CheckJsDirective = d;
			}
		} else if (pragma.Name == "jsx" || pragma.Name == "jsxfrag" ||
		           pragma.Name == "jsximportsource" ||
		           pragma.Name == "jsxruntime") {
			// Nothing to do here
		} else {
			TSC_UNREACHABLE("Unhandled pragma kind");
		}
	}
}

ResolutionMode Parser::parseResolutionMode(std::string_view mode, int pos, int end) {
	if (mode == "import") {
		return ResolutionMode::ESM;
	}
	if (mode == "require") {
		return ResolutionMode::CommonJS;
	}
	parseErrorAt(pos, end,
	             X_resolution_mode_should_be_either_require_or_import);
	return ResolutionMode::None;
}

void Parser::jsErrorAtRange(TextRange loc, const DiagnosticMessage* message,
                            const std::vector<std::string>& args) {
	jsDiagnostics.push_back(newDetachedDiagnostic(
		TextRange{skipTrivia(sourceText, loc.pos()), loc.end()}, message,
		args));
}

void Parser::checkJSDecoratorSyntax(Node* node) {
	std::vector<Node*> modifiers = node->modifierNodes();
	if (modifiers.empty()) {
		return;
	}

	if (canHaveIllegalDecorators(node)) {
		for (Node* modifier : modifiers) {
			if (isDecorator(modifier)) {
				jsErrorAtRange(modifier->loc,
				               Decorators_are_not_valid_here);
				break;
			}
		}
	} else if (canHaveDecorators(node)) {
		auto decoratorIt =
			std::find_if(modifiers.begin(), modifiers.end(), isDecorator);
		if (decoratorIt != modifiers.end()) {
			int decoratorIndex = (int)(decoratorIt - modifiers.begin());
			if (isClassDeclaration(node)) {
				auto exportIt = std::find_if(
					modifiers.begin(), modifiers.end(), isExportModifier);
				if (exportIt != modifiers.end()) {
					int exportIndex =
						(int)(exportIt - modifiers.begin());
					auto defaultIt = std::find_if(
						modifiers.begin(), modifiers.end(),
						[](Node* m) {
							return m->kind == Kind::DefaultKeyword;
						});
					int defaultIndex = defaultIt == modifiers.end()
					                       ? -1
					                       : (int)(defaultIt -
					                               modifiers.begin());
					if (decoratorIndex > exportIndex &&
					    defaultIndex >= 0 &&
					    decoratorIndex < defaultIndex) {
						// Decorator between `export` and `default`
						jsErrorAtRange(
							modifiers[decoratorIndex]->loc,
							Decorators_are_not_valid_here);
					} else if (decoratorIndex < exportIndex) {
						// Find a trailing decorator after the export
						// keyword
						int trailingDecoratorIndex = -1;
						for (int i = exportIndex;
						     i < (int)modifiers.size(); i++) {
							if (isDecorator(modifiers[i])) {
								trailingDecoratorIndex = i;
								break;
							}
						}
						if (trailingDecoratorIndex >= 0) {
							Diagnostic* diag =
								newDetachedDiagnostic(
									TextRange{skipTrivia(
										          sourceText,
										          modifiers
										              [trailingDecoratorIndex]
										                  ->loc.pos()),
									          modifiers
									              [trailingDecoratorIndex]
									                  ->loc.end()},
									Decorators_may_not_appear_after_export_or_export_default_if_they_also_appear_before_export);
							diag->messageChain.push_back(
								newDetachedDiagnostic(
									TextRange{skipTrivia(
										          sourceText,
										          modifiers[decoratorIndex]
										                  ->loc.pos()),
									          modifiers[decoratorIndex]
									              ->loc.end()},
									Decorator_used_before_export_here));
							jsDiagnostics.push_back(diag);
						}
					}
				}
			}
		}
	}
}

Node* Parser::checkJSSyntax(Node* node) {
	if ((node->flags & NodeFlagsJavaScriptFile) == 0 ||
	    (node->flags & (NodeFlagsJSDoc | NodeFlagsReparsed)) != 0) {
		return node;
	}
	switch (node->kind) {
	case Kind::Parameter:
	case Kind::PropertyDeclaration:
	case Kind::MethodDeclaration: {
		if (Node* token_ = node->questionToken();
		    token_ != nullptr &&
		    (token_->flags & NodeFlagsReparsed) == 0 &&
		    isQuestionToken(token_)) {
			jsErrorAtRange(
				token_->loc,
				The_0_modifier_can_only_be_used_in_TypeScript_files,
				{"?"});
		}
		[[fallthrough]];
	}
	case Kind::MethodSignature:
	case Kind::Constructor:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::FunctionExpression:
	case Kind::FunctionDeclaration:
	case Kind::ArrowFunction:
	case Kind::VariableDeclaration:
	case Kind::IndexSignature:
		if (isFunctionLike(node) && node->body() == nullptr) {
			jsErrorAtRange(
				node->loc,
				Signature_declarations_can_only_be_used_in_TypeScript_files);
		} else if (Node* t = node->type();
		           t != nullptr && (t->flags & NodeFlagsReparsed) == 0) {
			jsErrorAtRange(
				t->loc,
				Type_annotations_can_only_be_used_in_TypeScript_files);
		}
		break;
	case Kind::ImportDeclaration:
		if (Node* clause = node->importClause();
		    clause != nullptr && clause->isTypeOnly()) {
			jsErrorAtRange(
				node->loc,
				X_0_declarations_can_only_be_used_in_TypeScript_files,
				{"import type"});
		}
		break;
	case Kind::ExportDeclaration:
		if (node->isTypeOnly()) {
			jsErrorAtRange(
				node->loc,
				X_0_declarations_can_only_be_used_in_TypeScript_files,
				{"export type"});
		}
		break;
	case Kind::ImportSpecifier:
		if (node->isTypeOnly()) {
			jsErrorAtRange(
				node->loc,
				X_0_declarations_can_only_be_used_in_TypeScript_files,
				{"import...type"});
		}
		break;
	case Kind::ExportSpecifier:
		if (node->isTypeOnly()) {
			jsErrorAtRange(
				node->loc,
				X_0_declarations_can_only_be_used_in_TypeScript_files,
				{"export...type"});
		}
		break;
	case Kind::ImportEqualsDeclaration:
		jsErrorAtRange(node->loc,
		               X_import_can_only_be_used_in_TypeScript_files);
		break;
	case Kind::ExportAssignment:
		if (node->as<ExportAssignment>()->IsExportEquals) {
			jsErrorAtRange(node->loc,
			               X_export_can_only_be_used_in_TypeScript_files);
		}
		break;
	case Kind::HeritageClause:
		if (node->as<HeritageClause>()->Token == Kind::ImplementsKeyword) {
			jsErrorAtRange(
				node->loc,
				X_implements_clauses_can_only_be_used_in_TypeScript_files);
		}
		break;
	case Kind::InterfaceDeclaration:
		jsErrorAtRange(
			node->name()->loc,
			X_0_declarations_can_only_be_used_in_TypeScript_files,
			{"interface"});
		break;
	case Kind::ModuleDeclaration:
		jsErrorAtRange(
			node->name()->loc,
			X_0_declarations_can_only_be_used_in_TypeScript_files,
			{std::string(tokenToString(
				node->as<ModuleDeclaration>()->Keyword))});
		break;
	case Kind::TypeAliasDeclaration:
		jsErrorAtRange(
			node->name()->loc,
			Type_aliases_can_only_be_used_in_TypeScript_files);
		break;
	case Kind::EnumDeclaration:
		jsErrorAtRange(
			node->name()->loc,
			X_0_declarations_can_only_be_used_in_TypeScript_files,
			{"enum"});
		break;
	case Kind::NonNullExpression:
		jsErrorAtRange(node->loc,
		               Non_null_assertions_can_only_be_used_in_TypeScript_files);
		break;
	case Kind::AsExpression:
		jsErrorAtRange(
			node->type()->loc,
			Type_assertion_expressions_can_only_be_used_in_TypeScript_files);
		break;
	case Kind::SatisfiesExpression:
		jsErrorAtRange(
			node->type()->loc,
			Type_satisfaction_expressions_can_only_be_used_in_TypeScript_files);
		break;
	default:
		break;
	}
	// Check decorator placement in JS files
	checkJSDecoratorSyntax(node);
	// Check absence of type parameters, type arguments and non-JavaScript
	// modifiers
	switch (node->kind) {
	case Kind::ClassDeclaration:
	case Kind::ClassExpression:
	case Kind::MethodDeclaration:
	case Kind::Constructor:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::FunctionExpression:
	case Kind::FunctionDeclaration:
	case Kind::ArrowFunction: {
		if (NodeList* list = node->typeParameterList();
		    list != nullptr &&
		    std::any_of(list->nodes.begin(), list->nodes.end(),
		                [](Node* n) {
			                return (n->flags & NodeFlagsReparsed) == 0;
		                })) {
			jsErrorAtRange(
				list->loc,
				Type_parameter_declarations_can_only_be_used_in_TypeScript_files);
		}
		[[fallthrough]];
	}
	case Kind::VariableStatement:
	case Kind::PropertyDeclaration:
		for (Node* modifier : node->modifierNodes()) {
			if ((modifier->flags & NodeFlagsReparsed) == 0 &&
			    modifier->kind != Kind::Decorator &&
			    (modifierToFlag(modifier->kind) &
			     ModifierFlagsJavaScript) == 0) {
				jsErrorAtRange(
					modifier->loc,
					The_0_modifier_can_only_be_used_in_TypeScript_files,
					{std::string(tokenToString(modifier->kind))});
			}
		}
		break;
	case Kind::Parameter: {
		std::vector<Node*> mods = node->modifierNodes();
		if (std::any_of(mods.begin(), mods.end(), isModifier)) {
			jsErrorAtRange(
				node->modifiers()->loc,
				Parameter_modifiers_can_only_be_used_in_TypeScript_files);
		}
		break;
	}
	case Kind::CallExpression:
	case Kind::NewExpression:
	case Kind::ExpressionWithTypeArguments:
	case Kind::JsxSelfClosingElement:
	case Kind::JsxOpeningElement:
	case Kind::TaggedTemplateExpression:
		if (NodeList* list = node->typeArgumentList();
		    list != nullptr &&
		    std::any_of(list->nodes.begin(), list->nodes.end(),
		                [](Node* n) {
			                return (n->flags & NodeFlagsReparsed) == 0;
		                })) {
			jsErrorAtRange(
				list->loc,
				Type_arguments_can_only_be_used_in_TypeScript_files);
		}
		break;
	default:
		break;
	}
	return node;
}
// utilities.go
bool isKeywordOrPunctuation(Kind token) {
	return (token >= KindFirstKeyword && token <= KindLastKeyword) ||
	       (token >= KindFirstPunctuation && token <= KindLastPunctuation);
}

bool isJSDocLikeText(std::string_view text) {
	return text.size() >= 4 && text[1] == '*' && text[2] == '*' &&
	       text[3] != '/';
}

}  // namespace tsc
