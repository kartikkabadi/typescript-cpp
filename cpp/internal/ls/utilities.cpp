// Port of tsc/internal/ls/utilities.go (1411 Go lines) — ls-coreA slice.
// See PORTING.md for conventions.
#include "internal/ls/ls.h"

#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "internal/astnav/tokens.h"
#include "internal/collections/collections.h"
#include "internal/compiler/program.h"
#include "internal/json/json.h"
#include "internal/jsnum/jsnum.h"
#include "internal/scanner/scanner.h"
#include "internal/spanmap/spanmap.h"
#include "internal/stringutil/stringutil.h"
#include "internal/tspath/tspath.h"

namespace tsc::ls {

namespace {

// ---------------------------------------------------------------------------
// File-local replicas of helpers owned by other packages/slices (per
// PORTING.md). Collapses when the owning slice lands a shared version.
// ---------------------------------------------------------------------------

// ast/utilities.go:3077 — ast.IsVariableLike.
bool isVariableLike(Node* node) {
	switch (node->kind) {
	case Kind::BindingElement:
	case Kind::EnumMember:
	case Kind::Parameter:
	case Kind::PropertyAssignment:
	case Kind::PropertyDeclaration:
	case Kind::PropertySignature:
	case Kind::ShorthandPropertyAssignment:
	case Kind::VariableDeclaration:
		return true;
	}
	return false;
}

// ast/utilities.go:3086 — ast.HasInitializer.
bool hasInitializer(Node* node) {
	switch (node->kind) {
	case Kind::VariableDeclaration:
	case Kind::Parameter:
	case Kind::BindingElement:
	case Kind::PropertyDeclaration:
	case Kind::PropertyAssignment:
	case Kind::EnumMember:
	case Kind::ForStatement:
	case Kind::ForInStatement:
	case Kind::ForOfStatement:
	case Kind::JsxAttribute:
		return node->initializer() != nullptr;
	default:
		return false;
	}
}

// ast/utilities.go:2336 — ast.IsBreakOrContinueStatement.
bool isBreakOrContinueStatement(Node* node) {
	return nodeKindIs(node, Kind::BreakStatement, Kind::ContinueStatement);
}

// ast/utilities.go:3274 — ast.IsStringTextContainingNode.
bool isStringTextContainingNode(Node* node) {
	return node->kind == Kind::StringLiteral ||
	       isTemplateLiteralKind(node->kind);
}

// ast/utilities.go:1746 — ast.GetImplementsHeritageClauseElements.
std::vector<Node*> getImplementsHeritageClauseElements(Node* node) {
	return getHeritageElements(node, Kind::ImplementsKeyword);
}

// ast/utilities.go:2510 — ast.IsInternalModuleImportEqualsDeclaration.
bool isInternalModuleImportEqualsDeclaration(Node* node) {
	return isImportEqualsDeclaration(node) &&
	       node->as<ImportEqualsDeclaration>()->ModuleReference->kind !=
	           Kind::ExternalModuleReference;
}

// ast/utilities.go:587 — ast.IsObjectLiteralElement (file-local copy;
// same replica exists in checker_relater.cpp).
bool isObjectLiteralElement(Node* node) {
	switch (node->kind) {
	case Kind::PropertyAssignment:
	case Kind::ShorthandPropertyAssignment:
	case Kind::SpreadAssignment:
	case Kind::MethodDeclaration:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
		return true;
	default:
		return false;
	}
}

// stringutil/util.go:222 — stringutil.StripQuotes.
std::string stripQuotes(const std::string& name) {
	if (name.size() < 2) {
		return name;
	}
	int w1 = 0, w2 = 0;
	char32_t firstChar = decodeUtf8Rune(name, &w1);
	char32_t lastChar = decodeLastUtf8Rune(name, &w2);
	if (firstChar == lastChar &&
	    (firstChar == '\'' || firstChar == '"' || firstChar == '`')) {
		return name.substr(1, name.size() - 2);
	}
	return name;
}

// core/core.go — generic slice helpers (per-file convention).

template <class R, class F>
auto findRange(R&& v, F f) {
	for (auto& x : v) {
		if (f(x)) {
			return x;
		}
	}
	return std::decay_t<std::ranges::range_value_t<R>>{};
}

template <class R, class T>
bool containsRange(R&& v, const T& x) {
	return std::find(v.begin(), v.end(), x) != v.end();
}

// core/core.go:117 — core.MapNonNil.
template <class T, class F>
std::vector<Symbol*> mapNonNil(const std::vector<T>& v, F f) {
	std::vector<Symbol*> out;
	for (auto& x : v) {
		if (Symbol* r = f(x)) {
			out.push_back(r);
		}
	}
	return out;
}

// utilities.go:26 — quoteReplacer: strings.NewReplacer("'", `\'`, `\"`, `"`).
// Single left-to-right pass; `'` wins over `\"` when both could start (they
// never overlap — the second pattern starts with '\\').
std::string quoteReplacerReplace(std::string_view s) {
	std::string out;
	for (size_t i = 0; i < s.size();) {
		if (s[i] == '\'') {
			out += "\\'";
			i++;
		} else if (s.substr(i, 2) == "\\\"") {
			out += '"';
			i += 2;
		} else {
			out += s[i++];
		}
	}
	return out;
}

// ast/ast.go:2935 — ast.createToken (unexported). `kind` should be a token
// kind.
Node* createToken(Kind kind, SourceFile* file, int pos, int end,
                  TokenFlags flags) {
	if (file->tokenFactory == nullptr) {
		file->tokenFactory = new NodeFactory(NodeFactoryHooks{});
	}
	std::string text = file->text.substr(pos, end - pos);
	switch (kind) {
	case Kind::NumericLiteral:
		return file->tokenFactory->newNumericLiteral(text, flags);
	case Kind::BigIntLiteral:
		return file->tokenFactory->newBigIntLiteral(text, flags);
	case Kind::StringLiteral:
		return file->tokenFactory->newStringLiteral(text, flags);
	case Kind::JsxText:
	case Kind::JsxTextAllWhiteSpaces:
		return file->tokenFactory->newJsxText(
		    text, kind == Kind::JsxTextAllWhiteSpaces);
	case Kind::RegularExpressionLiteral:
		return file->tokenFactory->newRegularExpressionLiteral(text, flags);
	case Kind::NoSubstitutionTemplateLiteral:
		return file->tokenFactory->newNoSubstitutionTemplateLiteral(text,
		                                                            flags);
	case Kind::TemplateHead:
		return file->tokenFactory->newTemplateHead(text, "" /*rawText*/,
		                                           flags);
	case Kind::TemplateMiddle:
		return file->tokenFactory->newTemplateMiddle(text, "" /*rawText*/,
		                                             flags);
	case Kind::TemplateTail:
		return file->tokenFactory->newTemplateTail(text, "" /*rawText*/,
		                                           flags);
	case Kind::Identifier:
		return file->tokenFactory->newIdentifier(text);
	case Kind::PrivateIdentifier:
		return file->tokenFactory->newPrivateIdentifier(text);
	default: // Punctuation and keywords
		return file->tokenFactory->newToken(kind);
	}
}

// ast/ast.go:2904 — SourceFile.GetOrCreateToken. Gets a token from the file's
// token cache, or creates it if it does not already exist. This function
// should NOT be used for creating synthetic tokens that are not in the file
// in the first place.
Node* getOrCreateToken(SourceFile* file, Kind kind, int pos, int end,
                       Node* parent, TokenFlags flags) {
	std::lock_guard<std::mutex> lock(file->tokenCacheMu);
	TextRange loc{pos, end};
	TokenCacheKey key{parent, loc};
	if (auto it = file->tokenCache.find(key); it != file->tokenCache.end()) {
		Node* token = it->second;
		if (token->kind != kind) {
			std::string msg = "Token cache mismatch: " +
			                  std::string(kindToString(token->kind)) +
			                  " != " + std::string(kindToString(kind));
			TSC_UNREACHABLE(msg.c_str());
		}
		return token;
	}
	if ((parent->flags & NodeFlagsReparsed) != 0) {
		std::string msg =
		    "Cannot create token from reparsed node of kind " +
		    std::string(kindToString(parent->kind));
		TSC_UNREACHABLE(msg.c_str());
	}
	Node* token = createToken(kind, file, pos, end, flags);
	token->loc = loc;
	token->parent = parent;
	file->tokenCache[key] = token;
	return token;
}

} // namespace

// ---------------------------------------------------------------------------

// utilities.go:28 IsInString.
bool IsInString(SourceFile* sourceFile, int position, Node* previousToken) {
	if (previousToken != nullptr &&
	    isStringTextContainingNode(previousToken)) {
		int start =
		    astnav::getStartOfNode(previousToken, sourceFile,
		                           false /*includeJSDoc*/);
		int end = previousToken->end();

		// To be "in" one of these literals, the position has to be:
		//   1. entirely within the token text.
		//   2. at the end position of an unterminated token.
		//   3. at the end of a regular expression (due to trailing flags like
		//      '/foo/g').
		if (start < position && position < end) {
			return true;
		}

		if (position == end) {
			return isUnterminatedLiteral(previousToken);
		}
	}
	return false;
}

// utilities.go:48 isModuleSpecifierLike.
bool isModuleSpecifierLike(Node* node) {
	if (!isStringLiteralLike(node)) {
		return false;
	}

	if (isRequireCall(node->parent,
	                  false /*requireStringLiteralLikeArgument*/) ||
	    isImportCall(node->parent)) {
		return node->parent->arguments()[0] == node;
	}

	return node->parent->kind == Kind::ExternalModuleReference ||
	       node->parent->kind == Kind::ImportDeclaration ||
	       node->parent->kind == Kind::JSImportDeclaration;
}

// utilities.go:62 getNonModuleSymbolOfMergedModuleSymbol.
Symbol* getNonModuleSymbolOfMergedModuleSymbol(Symbol* symbol) {
	if (symbol->data->declarations.empty() ||
	    (symbol->flags &
	     (SymbolFlagsModule | SymbolFlagsTransient)) == 0) {
		return nullptr;
	}

	if (Node* decl = findRange(symbol->data->declarations, [](Node* d) {
		    return !isSourceFile(d) && !isModuleDeclaration(d);
	    });
	    decl != nullptr) {
		return decl->symbol();
	}
	return nullptr;
}

// utilities.go:73 getLocalSymbolForExportSpecifier.
Symbol* getLocalSymbolForExportSpecifier(Node* referenceLocation,
                                         Symbol* referenceSymbol,
                                         ExportSpecifier* exportSpecifier,
                                         checker::Checker* ch) {
	if (isExportSpecifierAlias(referenceLocation, exportSpecifier)) {
		if (Symbol* symbol = ch->GetExportSpecifierLocalTargetSymbol(
		        exportSpecifier->asNode());
		    symbol != nullptr) {
			return symbol;
		}
	}
	return referenceSymbol;
}

// utilities.go:82 isExportSpecifierAlias.
bool isExportSpecifierAlias(Node* referenceLocation,
                            ExportSpecifier* exportSpecifier) {
	TSC_ASSERT(exportSpecifier->PropertyName == referenceLocation ||
	                  exportSpecifier->name == referenceLocation,
	              "referenceLocation is not export specifier name or property "
	              "name");
	Node* propertyName = exportSpecifier->PropertyName;
	if (propertyName != nullptr) {
		// Given `export { foo as bar } [from "someModule"]`: It's an alias at
		// `foo`, but at `bar` it's a new symbol.
		return propertyName == referenceLocation;
	}
	// `export { foo } from "foo"` is a re-export.
	// `export { foo };` is not a re-export, it creates an alias for the local
	// variable `foo`.
	return exportSpecifier->parent->parent->moduleSpecifier() == nullptr;
}

// utilities.go:95 isInComment.
CommentRange* isInComment(SourceFile* file, int position,
                          Node* tokenAtPosition) {
	return getRangeOfEnclosingComment(
	    file, position, astnav::findPrecedingToken(file, position),
	    tokenAtPosition);
}

// utilities.go:99 positionBelongsToNode.
bool positionBelongsToNode(Node* candidate, int position, SourceFile* file) {
	return lsutil::PositionBelongsToNode(candidate, position, file);
}

// utilities.go:109 getPossibleTypeArgumentsInfo — get info for an expression
// like `f <` that may be the start of type arguments.
PossibleTypeArgumentInfo* getPossibleTypeArgumentsInfo(Node* tokenIn,
                                                       SourceFile* sourceFile) {
	// This is a rare case, but one that saves on a _lot_ of work if true - if
	// the source file has _no_ `<` character, then there obviously can't be
	// any type arguments - no expensive brace-matching backwards scanning
	// required
	if (sourceFile->Text().find('<') == std::string_view::npos) {
		return nullptr;
	}

	Node* token = tokenIn;
	// This function determines if the node could be a type argument position
	// When editing, it is common to have an incomplete type argument list
	// (e.g. missing ">"), so the tree can have any shape depending on the
	// tokens before the current node. Instead, scanning for an identifier
	// followed by a "<" before current node will typically give us better
	// results than inspecting the tree. Note that we also balance out the
	// already provided type arguments, arrays, object literals while doing
	// so.
	int remainingLessThanTokens = 0;
	int nTypeArguments = 0;
	while (token != nullptr) {
		switch (token->kind) {
		case Kind::LessThanToken: {
			// Found the beginning of the generic argument expression
			token = astnav::findPrecedingToken(sourceFile, token->pos());
			if (token != nullptr && token->kind == Kind::QuestionDotToken) {
				token =
				    astnav::findPrecedingToken(sourceFile, token->pos());
			}
			if (token == nullptr || !isIdentifier(token)) {
				return nullptr;
			}
			if (remainingLessThanTokens == 0) {
				if (isDeclarationName(token)) {
					return nullptr;
				}
				PossibleTypeArgumentInfo* info =
				    new PossibleTypeArgumentInfo();
				info->called = token;
				info->nTypeArguments = nTypeArguments;
				return info;
			}
			remainingLessThanTokens--;
			break;
		}
		case Kind::GreaterThanGreaterThanGreaterThanToken:
			remainingLessThanTokens += 3;
			break;
		case Kind::GreaterThanGreaterThanToken:
			remainingLessThanTokens += 2;
			break;
		case Kind::GreaterThanToken:
			remainingLessThanTokens++;
			break;
		case Kind::CloseBraceToken:
			// This can be object type, skip until we find the matching open
			// brace token
			token = findPrecedingMatchingToken(token, Kind::OpenBraceToken,
			                                   sourceFile);
			if (token == nullptr) {
				return nullptr;
			}
			break;
		case Kind::CloseParenToken:
			// This can be object type, skip until we find the matching open
			// brace token
			token = findPrecedingMatchingToken(token, Kind::OpenParenToken,
			                                   sourceFile);
			if (token == nullptr) {
				return nullptr;
			}
			break;
		case Kind::CloseBracketToken:
			// This can be object type, skip until we find the matching open
			// brace token
			token = findPrecedingMatchingToken(token, Kind::OpenBracketToken,
			                                   sourceFile);
			if (token == nullptr) {
				return nullptr;
			}
			break;
		case Kind::CommaToken:
			// Valid tokens in a type name. Skip.
			nTypeArguments++;
			break;
		case Kind::EqualsGreaterThanToken:
		case Kind::Identifier:
		case Kind::StringLiteral:
		case Kind::NumericLiteral:
		case Kind::BigIntLiteral:
		case Kind::TrueKeyword:
		case Kind::FalseKeyword:
		case Kind::TypeOfKeyword:
		case Kind::ExtendsKeyword:
		case Kind::KeyOfKeyword:
		case Kind::DotToken:
		case Kind::BarToken:
		case Kind::QuestionToken:
		case Kind::ColonToken:
			// do nothing
			break;
		default:
			if (!isTypeNode(token)) {
				// Invalid token in type
				return nullptr;
			}
		}
		token = astnav::findPrecedingToken(sourceFile, token->pos());
	}
	return nullptr;
}

// utilities.go:191 isNameOfModuleDeclaration.
bool isNameOfModuleDeclaration(Node* node) {
	if (node->parent->kind != Kind::ModuleDeclaration) {
		return false;
	}
	return node->parent->name() == node;
}

// utilities.go:198 isExpressionOfExternalModuleImportEqualsDeclaration.
bool isExpressionOfExternalModuleImportEqualsDeclaration(Node* node) {
	return isExternalModuleImportEqualsDeclaration(node->parent->parent) &&
	       getExternalModuleImportEqualsDeclarationExpression(
	           node->parent->parent) == node;
}

// utilities.go:202 isNamespaceReference.
bool isNamespaceReference(Node* node) {
	return isQualifiedNameNamespaceReference(node) ||
	       isPropertyAccessNamespaceReference(node);
}

// utilities.go:206 isQualifiedNameNamespaceReference.
bool isQualifiedNameNamespaceReference(Node* node) {
	Node* root = node;
	bool isLastClause = true;
	if (root->parent->kind == Kind::QualifiedName) {
		while (root->parent != nullptr &&
		       root->parent->kind == Kind::QualifiedName) {
			root = root->parent;
		}

		isLastClause = root->as<QualifiedName>()->Right == node;
	}

	return root->parent->kind == Kind::TypeReference && !isLastClause;
}

// utilities.go:220 isPropertyAccessNamespaceReference.
bool isPropertyAccessNamespaceReference(Node* node) {
	Node* root = node;
	bool isLastClause = true;
	if (root->parent->kind == Kind::PropertyAccessExpression) {
		while (root->parent != nullptr &&
		       root->parent->kind == Kind::PropertyAccessExpression) {
			root = root->parent;
		}

		isLastClause = root->name() == node;
	}

	if (!isLastClause &&
	    root->parent->kind == Kind::ExpressionWithTypeArguments &&
	    root->parent->parent->kind == Kind::HeritageClause) {
		Node* decl = root->parent->parent->parent;
		return (decl->kind == Kind::ClassDeclaration &&
		        root->parent->parent->as<HeritageClause>()->Token ==
		            Kind::ImplementsKeyword) ||
		       (decl->kind == Kind::InterfaceDeclaration &&
		        root->parent->parent->as<HeritageClause>()->Token ==
		            Kind::ExtendsKeyword);
	}

	return false;
}

// utilities.go:240 isThis.
bool isThis(Node* node) {
	switch (node->kind) {
	case Kind::ThisKeyword:
		// case Kind::ThisType: TODO: GH#9267
		return true;
	case Kind::Identifier:
		// 'this' as a parameter
		return node->text() == "this" &&
		       node->parent->kind == Kind::Parameter;
	default:
		return false;
	}
}

// utilities.go:253 isTypeReference.
bool isTypeReference(Node* node) {
	if (isRightSideOfQualifiedNameOrPropertyAccess(node)) {
		node = node->parent;
	}

	switch (node->kind) {
	case Kind::ThisKeyword:
		return !isExpressionNode(node);
	case Kind::ThisType:
		return true;
	default:
		break;
	}

	switch (node->parent->kind) {
	case Kind::TypeReference:
		return true;
	case Kind::ImportType:
		return !node->parent->as<ImportTypeNode>()->IsTypeOf;
	case Kind::ExpressionWithTypeArguments:
		return isPartOfTypeNode(node->parent);
	default:
		break;
	}

	return false;
}

// utilities.go:277 isInRightSideOfInternalImportEqualsDeclaration.
bool isInRightSideOfInternalImportEqualsDeclaration(Node* node) {
	if (node->parent == nullptr) {
		return false;
	}
	while (node->parent->kind == Kind::QualifiedName) {
		node = node->parent;
	}

	return isInternalModuleImportEqualsDeclaration(node->parent) &&
	       node->parent->as<ImportEqualsDeclaration>()->ModuleReference ==
	           node;
}

// utilities.go:288 createLspRangeFromNode.
std::pair<lsproto::Range, spanmap::Fidelity>
LanguageService::createLspRangeFromNode(Node* node, SourceFile* file) {
	return createLspRangeFromBounds(
	    getTokenPosOfNode(node, file, false /*includeJSDoc*/),
	    node->end(), file);
}

// utilities.go:292 createLspRangeFromNodeForFeature.
std::pair<lsproto::Range, spanmap::Fidelity>
LanguageService::createLspRangeFromNodeForFeature(Node* node,
                                                  SourceFile* file,
                                                  spanmap::Feature feature) {
	return converters->ToLSPRangeForFeature(file,
	                                        createRangeFromNode(node, file),
	                                        feature);
}

// utilities.go:296 createRangeFromNode.
TextRange createRangeFromNode(Node* node, SourceFile* file) {
	return TextRange{
	    getTokenPosOfNode(node, file, false /*includeJSDoc*/),
	    node->end()};
}

// utilities.go:300 createLspRangeFromBounds.
std::pair<lsproto::Range, spanmap::Fidelity>
LanguageService::createLspRangeFromBounds(int start, int end,
                                          SourceFile* file) {
	return converters->ToLSPRange(file, TextRange{start, end});
}

// utilities.go:308 createLspPosition.
std::pair<lsproto::Position, spanmap::Fidelity>
LanguageService::createLspPosition(int position, SourceFile* file) {
	return converters->ToLSPPosition(file, TextPos(position));
}

// utilities.go:312 quote.
std::string quote(SourceFile* file,
                  const lsutil::UserPreferences& preferences,
                  const std::string& text) {
	// Editors can pass in undefined or empty string - we want to infer the
	// preference in those cases.
	lsutil::QuotePreference quotePreference =
	    lsutil::GetQuotePreference(file, preferences);
	// core.StringifyJson(text, "" /*prefix*/, "" /*indent*/)
	auto [quoted, _] = json::marshalIndent(text, "", "");
	if (quotePreference == lsutil::QuotePreferenceSingle) {
		quoted = "'" + quoteReplacerReplace(stripQuotes(quoted)) + "'";
	}
	return quoted;
}

// utilities.go:322 typeKeywords.
static const collections::Set<Kind> typeKeywords = [] {
	collections::Set<Kind> s;
	for (Kind k :
	     {Kind::AnyKeyword, Kind::AssertsKeyword, Kind::BigIntKeyword,
	      Kind::BooleanKeyword, Kind::FalseKeyword, Kind::InferKeyword,
	      Kind::KeyOfKeyword, Kind::NeverKeyword, Kind::NullKeyword,
	      Kind::NumberKeyword, Kind::ObjectKeyword, Kind::ReadonlyKeyword,
	      Kind::StringKeyword, Kind::SymbolKeyword, Kind::TypeOfKeyword,
	      Kind::TrueKeyword, Kind::VoidKeyword, Kind::UndefinedKeyword,
	      Kind::UniqueKeyword, Kind::UnknownKeyword}) {
		s.Add(k);
	}
	return s;
}();

// utilities.go:345 isTypeKeyword.
bool isTypeKeyword(Kind kind) {
	return typeKeywords.Has(kind);
}

// utilities.go:349 isSeparator.
bool isSeparator(Node* node, Node* candidate) {
	return candidate != nullptr && node->parent != nullptr &&
	       (candidate->kind == Kind::CommaToken ||
	        (candidate->kind == Kind::SemicolonToken &&
	         node->parent->kind == Kind::ObjectLiteralExpression));
}

// utilities.go:353 isLiteralNameOfPropertyDeclarationOrIndexAccess.
bool isLiteralNameOfPropertyDeclarationOrIndexAccess(Node* node) {
	// utilities
	switch (node->parent->kind) {
	case Kind::PropertyDeclaration:
	case Kind::PropertySignature:
	case Kind::PropertyAssignment:
	case Kind::EnumMember:
	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::ModuleDeclaration:
		return getNameOfDeclaration(node->parent) == node;
	case Kind::ElementAccessExpression:
		return node->parent->as<ElementAccessExpression>()
		           ->ArgumentExpression == node;
	case Kind::ComputedPropertyName:
		return true;
	case Kind::LiteralType:
		return node->parent->parent->kind == Kind::IndexedAccessType;
	default:
		return false;
	}
}

// utilities.go:377 isObjectBindingElementWithoutPropertyName.
bool isObjectBindingElementWithoutPropertyName(Node* bindingElement) {
	return bindingElement->kind == Kind::BindingElement &&
	       bindingElement->parent->kind == Kind::ObjectBindingPattern &&
	       bindingElement->name()->kind == Kind::Identifier &&
	       bindingElement->propertyName() == nullptr;
}

// utilities.go:384 isRightSideOfPropertyAccess.
bool isRightSideOfPropertyAccess(Node* node) {
	return node->parent != nullptr &&
	       node->parent->kind == Kind::PropertyAccessExpression &&
	       node->parent->name() == node;
}

// utilities.go:388 isStaticSymbol.
bool isStaticSymbol(Symbol* symbol) {
	if (symbol->data->valueDeclaration == nullptr) {
		return false;
	}
	ModifierFlags modifierFlags =
	    symbol->data->valueDeclaration->modifierFlags();
	return (modifierFlags & ModifierFlagsStatic) != 0;
}

// utilities.go:396 isImplementation.
bool isImplementation(Node* node) {
	if ((node->flags & NodeFlagsAmbient) != 0) {
		return !(node->kind == Kind::InterfaceDeclaration ||
		         node->kind == Kind::TypeAliasDeclaration);
	}
	if (isVariableLike(node)) {
		return hasInitializer(node);
	}
	if (isFunctionLikeDeclaration(node)) {
		return node->body() != nullptr;
	}
	return isClassLike(node) || isModuleOrEnumDeclaration(node);
}

// utilities.go:409 isImplementationExpression.
bool isImplementationExpression(Node* node) {
	switch (node->kind) {
	case Kind::ParenthesizedExpression:
		return isImplementationExpression(node->expression());
	case Kind::ArrowFunction:
	case Kind::FunctionExpression:
	case Kind::ObjectLiteralExpression:
	case Kind::ClassExpression:
	case Kind::ArrayLiteralExpression:
		return true;
	default:
		return false;
	}
}

// utilities.go:420 isReadonlyTypeOperator.
bool isReadonlyTypeOperator(Node* node) {
	return node->kind == Kind::ReadonlyKeyword &&
	       node->parent->kind == Kind::TypeOperator &&
	       node->parent->as<TypeOperatorNode>()->Operator ==
	           Kind::ReadonlyKeyword;
}

// utilities.go:424 isJumpStatementTarget.
bool isJumpStatementTarget(Node* node) {
	return node->kind == Kind::Identifier &&
	       isBreakOrContinueStatement(node->parent) &&
	       node->parent->label() == node;
}

// utilities.go:428 isLabelOfLabeledStatement.
bool isLabelOfLabeledStatement(Node* node) {
	return node->kind == Kind::Identifier &&
	       node->parent->kind == Kind::LabeledStatement &&
	       node->parent->label() == node;
}

// utilities.go:432 findReferenceInPosition.
FileReference* findReferenceInPosition(
    const std::vector<FileReference*>& refs, int pos) {
	return findRange(refs, [&](FileReference* ref) {
		return ref->containsInclusive(pos);
	});
}

// utilities.go:436 getContainingNodeIfInHeritageClause.
Node* getContainingNodeIfInHeritageClause(Node* node) {
	if (node->kind == Kind::Identifier ||
	    node->kind == Kind::QualifiedName ||
	    node->kind == Kind::PropertyAccessExpression) {
		return getContainingNodeIfInHeritageClause(node->parent);
	}
	if ((node->kind == Kind::ExpressionWithTypeArguments ||
	     node->kind == Kind::TypeReference) &&
	    isHeritageClause(node->parent) &&
	    (isClassLike(node->parent->parent) ||
	     node->parent->parent->kind == Kind::InterfaceDeclaration)) {
		return node->parent->parent;
	}
	return nullptr;
}

// utilities.go:448 getContainerNode.
Node* getContainerNode(Node* node) {
	for (Node* parent = node->parent; parent != nullptr;
	     parent = parent->parent) {
		switch (parent->kind) {
		case Kind::SourceFile:
		case Kind::MethodDeclaration:
		case Kind::MethodSignature:
		case Kind::FunctionDeclaration:
		case Kind::FunctionExpression:
		case Kind::GetAccessor:
		case Kind::SetAccessor:
		case Kind::ClassDeclaration:
		case Kind::InterfaceDeclaration:
		case Kind::EnumDeclaration:
		case Kind::ModuleDeclaration:
			return parent;
		default:
			break;
		}
	}
	return nullptr;
}

// utilities.go:459 getAdjustedLocation.
Node* getAdjustedLocation(Node* node, bool forRename,
                          SourceFile* sourceFile) {
	// todo: check if this function needs to be changed for jsdoc updates

	Node* parent = node->parent;
	// /**/<modifier> [|name|] ...
	// /**/<modifier> <class|interface|type|enum|module|namespace|function|get|set> [|name|] ...
	// /**/<class|interface|type|enum|module|namespace|function|get|set> [|name|] ...
	// /**/import [|name|] = ...
	//
	// NOTE: If the node is a modifier, we don't adjust its location if it is
	// the `default` modifier as that is handled specially by
	// `getSymbolAtLocation`.
	auto isModifier = [&](Node* node) -> bool {
		if (::tsc::isModifier(node) &&
		    (forRename || node->kind != Kind::DefaultKeyword)) {
			return canHaveModifiers(parent) &&
			       containsRange(parent->modifierNodes(), node);
		}
		switch (node->kind) {
		case Kind::ClassKeyword:
			return isClassDeclaration(parent) || isClassExpression(node);
		case Kind::FunctionKeyword:
			return isFunctionDeclaration(parent) ||
			       isFunctionExpression(node);
		case Kind::InterfaceKeyword:
			return isInterfaceDeclaration(parent);
		case Kind::EnumKeyword:
			return isEnumDeclaration(parent);
		case Kind::TypeKeyword:
			return isTypeAliasDeclaration(parent);
		case Kind::NamespaceKeyword:
		case Kind::ModuleKeyword:
			return isModuleDeclaration(parent);
		case Kind::ImportKeyword:
			return isImportEqualsDeclaration(parent);
		case Kind::GetKeyword:
			return isGetAccessorDeclaration(parent);
		case Kind::SetKeyword:
			return isSetAccessorDeclaration(parent);
		default:
			break;
		}
		return false;
	};
	if (isModifier(node)) {
		if (sourceFile == nullptr) {
			sourceFile = getSourceFileOfNode(node);
		}
		if (Node* location =
		        getAdjustedLocationForDeclaration(parent, forRename,
		                                          sourceFile);
		    location != nullptr) {
			return location;
		}
	}

	// /**/<var|let|const> [|name|] ...
	if ((node->kind == Kind::VarKeyword || node->kind == Kind::ConstKeyword ||
	     node->kind == Kind::LetKeyword) &&
	    isVariableDeclarationList(parent) &&
	    parent->as<VariableDeclarationList>()->Declarations->nodes.size() ==
	        1) {
		VariableDeclaration* declaration =
		    parent->as<VariableDeclarationList>()
		        ->Declarations->nodes[0]
		        ->as<VariableDeclaration>();
		if (isIdentifier(declaration->name)) {
			return declaration->name;
		}
	}

	if (node->kind == Kind::TypeKeyword) {
		// import /**/type [|name|] from ...;
		// import /**/type { [|name|] } from ...;
		// import /**/type { propertyName as [|name|] } from ...;
		// import /**/type ... from "[|module|]";
		if (isImportClause(parent) && parent->isTypeOnly()) {
			if (Node* location = getAdjustedLocationForImportDeclaration(
			        parent->parent->as<ImportDeclaration>(), forRename);
			    location != nullptr) {
				return location;
			}
		}
		// export /**/type { [|name|] } from ...;
		// export /**/type { propertyName as [|name|] } from ...;
		// export /**/type * from "[|module|]";
		// export /**/type * as ... from "[|module|]";
		if (isExportDeclaration(parent) && parent->isTypeOnly()) {
			if (Node* location = getAdjustedLocationForExportDeclaration(
			        parent->as<ExportDeclaration>(), forRename);
			    location != nullptr) {
				return location;
			}
		}
	}

	// import { propertyName /**/as [|name|] } ...
	// import * /**/as [|name|] ...
	// export { propertyName /**/as [|name|] } ...
	// export * /**/as [|name|] ...
	if (node->kind == Kind::AsKeyword) {
		if ((parent->kind == Kind::ImportSpecifier &&
		     parent->propertyName() != nullptr) ||
		    (parent->kind == Kind::ExportSpecifier &&
		     parent->propertyName() != nullptr) ||
		    parent->kind == Kind::NamespaceImport ||
		    parent->kind == Kind::NamespaceExport) {
			return parent->name();
		}
		if (parent->kind == Kind::ExportDeclaration) {
			if (Node* exportClause =
			        parent->as<ExportDeclaration>()->ExportClause;
			    exportClause != nullptr &&
			    exportClause->kind == Kind::NamespaceExport) {
				return exportClause->name();
			}
		}
	}

	// /**/import [|name|] from ...;
	// /**/import { [|name|] } from ...;
	// /**/import { propertyName as [|name|] } from ...;
	// /**/import ... from "[|module|]";
	// /**/import "[|module|]";
	if (node->kind == Kind::ImportKeyword &&
	    parent->kind == Kind::ImportDeclaration) {
		if (Node* location = getAdjustedLocationForImportDeclaration(
		        parent->as<ImportDeclaration>(), forRename);
		    location != nullptr) {
			return location;
		}
	}

	if (node->kind == Kind::ExportKeyword) {
		// /**/export { [|name|] } ...;
		// /**/export { propertyName as [|name|] } ...;
		// /**/export * from "[|module|]";
		// /**/export * as ... from "[|module|]";
		if (parent->kind == Kind::ExportDeclaration) {
			if (Node* location = getAdjustedLocationForExportDeclaration(
			        parent->as<ExportDeclaration>(), forRename);
			    location != nullptr) {
				return location;
			}
		}
		// NOTE: We don't adjust the location of the `default` keyword as
		// that is handled specially by `getSymbolAtLocation`.
		// /**/export default [|name|];
		// /**/export = [|name|];
		if (parent->kind == Kind::ExportAssignment) {
			return skipOuterExpressions(parent->expression(), OEKAll);
		}
	}
	// import name = /**/require("[|module|]");
	if (node->kind == Kind::RequireKeyword &&
	    parent->kind == Kind::ExternalModuleReference) {
		return parent->expression();
	}
	// import ... /**/from "[|module|]";
	// export ... /**/from "[|module|]";
	if (node->kind == Kind::FromKeyword) {
		if ((parent->kind == Kind::ImportDeclaration ||
		     parent->kind == Kind::ExportDeclaration) &&
		    parent->moduleSpecifier() != nullptr) {
			return parent->moduleSpecifier();
		}
	}
	// class ... /**/extends [|name|] ...
	// class ... /**/implements [|name|] ...
	// class ... /**/implements name1, name2 ...
	// interface ... /**/extends [|name|] ...
	// interface ... /**/extends name1, name2 ...
	if ((node->kind == Kind::ExtendsKeyword ||
	     node->kind == Kind::ImplementsKeyword) &&
	    parent->kind == Kind::HeritageClause &&
	    parent->as<HeritageClause>()->Token == node->kind) {
		auto getAdjustedLocationForHeritageClause =
		    [](HeritageClause* node) -> Node* {
			// /**/extends [|name|]
			// /**/implements [|name|]
			if (node->Types->nodes.size() == 1) {
				return getHeritageClauseElementName(node->Types->nodes[0]);
			}

			// fall through `getAdjustedLocation`
			//    /**/extends name1, name2 ...
			//    /**/implements name1, name2 ...
			return nullptr;
		};

		if (Node* location = getAdjustedLocationForHeritageClause(
		        parent->as<HeritageClause>());
		    location != nullptr) {
			return location;
		}
	}
	if (node->kind == Kind::ExtendsKeyword) {
		// ... <T /**/extends [|U|]> ...
		if (parent->kind == Kind::TypeParameter) {
			if (Node* constraint =
			        parent->as<TypeParameterDeclaration>()->Constraint;
			    constraint != nullptr &&
			    constraint->kind == Kind::TypeReference) {
				return constraint->as<TypeReferenceNode>()->TypeName;
			}
		}
		// ... T /**/extends [|U|] ? ...
		if (parent->kind == Kind::ConditionalType) {
			if (Node* extendsType =
			        parent->as<ConditionalTypeNode>()->ExtendsType;
			    extendsType != nullptr &&
			    extendsType->kind == Kind::TypeReference) {
				return extendsType->as<TypeReferenceNode>()->TypeName;
			}
		}
	}
	// ... T extends /**/infer [|U|] ? ...
	if (node->kind == Kind::InferKeyword &&
	    parent->kind == Kind::InferType) {
		return parent->as<InferTypeNode>()->TypeParameter->name();
	}
	// { [ [|K|] /**/in keyof T]: ... }
	if (node->kind == Kind::InKeyword &&
	    parent->kind == Kind::TypeParameter &&
	    parent->parent->kind == Kind::MappedType) {
		return parent->name();
	}
	// /**/keyof [|T|]
	if (node->kind == Kind::KeyOfKeyword &&
	    parent->kind == Kind::TypeOperator &&
	    parent->as<TypeOperatorNode>()->Operator == Kind::KeyOfKeyword) {
		if (Node* parentType = parent->type();
		    parentType != nullptr &&
		    parentType->kind == Kind::TypeReference) {
			return parentType->as<TypeReferenceNode>()->TypeName;
		}
	}
	// /**/readonly [|name|][]
	if (node->kind == Kind::ReadonlyKeyword &&
	    parent->kind == Kind::TypeOperator &&
	    parent->as<TypeOperatorNode>()->Operator == Kind::ReadonlyKeyword) {
		if (Node* parentType = parent->type();
		    parentType != nullptr &&
		    parentType->kind == Kind::ArrayType &&
		    parentType->as<ArrayTypeNode>()->ElementType->kind ==
		        Kind::TypeReference) {
			return parentType->as<ArrayTypeNode>()
			    ->ElementType->as<TypeReferenceNode>()
			    ->TypeName;
		}
	}

	if (!forRename) {
		// /**/new [|name|]
		// /**/void [|name|]
		// /**/void obj.[|name|]
		// /**/typeof [|name|]
		// /**/typeof obj.[|name|]
		// /**/await [|name|]
		// /**/await obj.[|name|]
		// /**/yield [|name|]
		// /**/yield obj.[|name|]
		// /**/delete obj.[|name|]
		if ((node->kind == Kind::NewKeyword &&
		     parent->kind == Kind::NewExpression) ||
		    (node->kind == Kind::VoidKeyword &&
		     parent->kind == Kind::VoidExpression) ||
		    (node->kind == Kind::TypeOfKeyword &&
		     parent->kind == Kind::TypeOfExpression) ||
		    (node->kind == Kind::AwaitKeyword &&
		     parent->kind == Kind::AwaitExpression) ||
		    (node->kind == Kind::YieldKeyword &&
		     parent->kind == Kind::YieldExpression) ||
		    (node->kind == Kind::DeleteKeyword &&
		     parent->kind == Kind::DeleteExpression)) {
			if (Node* expr = parent->expression(); expr != nullptr) {
				return skipOuterExpressions(expr, OEKAll);
			}
		}

		// left /**/in [|name|]
		// left /**/instanceof [|name|]
		if ((node->kind == Kind::InKeyword ||
		     node->kind == Kind::InstanceOfKeyword) &&
		    parent->kind == Kind::BinaryExpression &&
		    parent->as<BinaryExpression>()->OperatorToken == node) {
			return skipOuterExpressions(
			    parent->as<BinaryExpression>()->Right, OEKAll);
		}

		// left /**/as [|name|]
		if (node->kind == Kind::AsKeyword &&
		    parent->kind == Kind::AsExpression) {
			if (Node* asExprType = parent->type();
			    asExprType != nullptr &&
			    asExprType->kind == Kind::TypeReference) {
				return asExprType->as<TypeReferenceNode>()->TypeName;
			}
		}

		// for (... /**/in [|name|])
		// for (... /**/of [|name|])
		if ((node->kind == Kind::InKeyword &&
		     parent->kind == Kind::ForInStatement) ||
		    (node->kind == Kind::OfKeyword &&
		     parent->kind == Kind::ForOfStatement)) {
			return skipOuterExpressions(parent->expression(), OEKAll);
		}
	}

	return node;
}

// utilities.go:696 getAdjustedLocationForDeclaration.
Node* getAdjustedLocationForDeclaration(Node* node, bool forRename,
                                        SourceFile* sourceFile) {
	if (node->name() != nullptr) {
		return node->name();
	}
	if (forRename) {
		return nullptr;
	}
	switch (node->kind) {
	case Kind::ClassDeclaration:
	case Kind::FunctionDeclaration:
		// for class and function declarations, use the `default` modifier
		// when the declaration is unnamed.
		return findRange(node->modifierNodes(), [&](Node* /*it*/) {
			// Go: closure mistakenly checks the outer `node.Kind` — kept
			// verbatim for parity.
			return node->kind == Kind::DefaultKeyword;
		});
	case Kind::ClassExpression:
		// for class expressions, use the `class` keyword when the class is
		// unnamed
		return astnav::findChildOfKind(node, Kind::ClassKeyword, sourceFile);
	case Kind::FunctionExpression:
		// for function expressions, use the `function` keyword when the
		// function is unnamed
		return astnav::findChildOfKind(node, Kind::FunctionKeyword,
		                               sourceFile);
	case Kind::Constructor:
		return node;
	default:
		break;
	}
	return nullptr;
}

// utilities.go:720 getAdjustedLocationForImportDeclaration.
Node* getAdjustedLocationForImportDeclaration(ImportDeclaration* node,
                                              bool forRename) {
	if (node->ImportClause != nullptr) {
		if (Node* name = node->ImportClause->name(); name != nullptr) {
			if (node->ImportClause->as<ImportClause>()->NamedBindings !=
			    nullptr) {
				// do not adjust if we have both a name and named bindings
				return nullptr;
			}
			// /**/import [|name|] from ...;
			// import /**/type [|name|] from ...;
			return node->ImportClause->name();
		}

		// /**/import { [|name|] } from ...;
		// /**/import { propertyName as [|name|] } from ...;
		// /**/import * as [|name|] from ...;
		// import /**/type { [|name|] } from ...;
		// import /**/type { propertyName as [|name|] } from ...;
		// import /**/type * as [|name|] from ...;
		if (Node* namedBindings =
		        node->ImportClause->as<ImportClause>()->NamedBindings;
		    namedBindings != nullptr) {
			switch (namedBindings->kind) {
			case Kind::NamedImports: {
				// do nothing if there is more than one binding
				std::vector<Node*> elements = namedBindings->elements();
				if (elements.size() != 1) {
					return nullptr;
				}
				return elements[0]->name();
			}
			case Kind::NamespaceImport:
				return namedBindings->name();
			default:
				break;
			}
		}
	}
	if (!forRename) {
		// /**/import "[|module|]";
		// /**/import ... from "[|module|]";
		// import /**/type ... from "[|module|]";
		return node->ModuleSpecifier;
	}
	return nullptr;
}

// utilities.go:763 getAdjustedLocationForExportDeclaration.
Node* getAdjustedLocationForExportDeclaration(ExportDeclaration* node,
                                              bool forRename) {
	if (node->ExportClause != nullptr) {
		// /**/export { [|name|] } ...
		// /**/export { propertyName as [|name|] } ...
		// /**/export * as [|name|] ...
		// export /**/type { [|name|] } from ...
		// export /**/type { propertyName as [|name|] } from ...
		// export /**/type * as [|name|] ...
		switch (node->ExportClause->kind) {
		case Kind::NamedExports: {
			// do nothing if there is more than one binding
			std::vector<Node*> elements = node->ExportClause->elements();
			if (elements.size() != 1) {
				return nullptr;
			}
			return elements[0]->name();
		}
		case Kind::NamespaceExport:
			return node->ExportClause->name();
		default:
			break;
		}
	}
	if (!forRename) {
		// /**/export * from "[|module|]";
		// export /**/type * from "[|module|]";
		return node->ModuleSpecifier;
	}
	return nullptr;
}

// utilities.go:791 symbolFlagsHaveMeaning.
bool symbolFlagsHaveMeaning(SymbolFlags flags, SemanticMeaning meaning) {
	if (meaning == SemanticMeaningAll) {
		return true;
	}
	if ((meaning & SemanticMeaningValue) != 0) {
		return (flags & SymbolFlagsValue) != 0;
	}
	if ((meaning & SemanticMeaningType) != 0) {
		return (flags & SymbolFlagsType) != 0;
	}
	if ((meaning & SemanticMeaningNamespace) != 0) {
		return (flags & SymbolFlagsNamespace) != 0;
	}
	return false;
}

// utilities.go:807 getMeaningFromLocation.
SemanticMeaning getMeaningFromLocation(Node* node) {
	// todo: check if this function needs to be changed for jsdoc updates
	node = getAdjustedLocation(getReparsedNodeForNode(node),
	                           false /*forRename*/, nullptr);
	Node* parent = node->parent;
	if (isSourceFile(node)) {
		return SemanticMeaningValue;
	}
	if (nodeKindIs(parent,
	               {Kind::ExportAssignment, Kind::ExportSpecifier,
	                Kind::ExternalModuleReference, Kind::ImportSpecifier,
	                Kind::ImportClause}) ||
	    (parent->kind == Kind::ImportEqualsDeclaration &&
	     node == parent->name())) {
		return SemanticMeaningAll;
	}
	if (isInRightSideOfInternalImportEqualsDeclaration(node)) {
		//     import a = |b|; // Namespace
		//     import a = |b.c|; // Value, type, namespace
		//     import a = |b.c|.d; // Namespace
		Node* name = node;
		if (node->kind != Kind::QualifiedName) {
			name = (parent->kind == Kind::QualifiedName &&
			        parent->as<QualifiedName>()->Right == node)
			           ? parent
			           : nullptr;
		}
		if (name != nullptr &&
		    name->parent->kind == Kind::ImportEqualsDeclaration) {
			return SemanticMeaningAll;
		}
		return SemanticMeaningNamespace;
	}
	if (isDeclarationName(node)) {
		return getMeaningFromDeclaration(parent);
	}
	if (isEntityName(node) && isJSDocNameReferenceContext(node)) {
		return SemanticMeaningAll;
	}
	if (isTypeReference(node)) {
		return SemanticMeaningType;
	}
	if (isNamespaceReference(node)) {
		return SemanticMeaningNamespace;
	}
	if (isTypeParameterDeclaration(parent)) {
		return SemanticMeaningType;
	}
	if (isLiteralTypeNode(parent)) {
		// This might be T["name"], which is actually referencing a property
		// and not a type. So allow both meanings.
		return SemanticMeaningType | SemanticMeaningValue;
	}
	return SemanticMeaningValue;
}

// utilities.go:846 getMeaningFromDeclaration.
SemanticMeaning getMeaningFromDeclaration(Node* node) {
	switch (node->kind) {
	case Kind::VariableDeclaration:
	case Kind::Parameter:
	case Kind::BindingElement:
	case Kind::PropertyDeclaration:
	case Kind::PropertySignature:
	case Kind::PropertyAssignment:
	case Kind::ShorthandPropertyAssignment:
	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
	case Kind::Constructor:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
	case Kind::ArrowFunction:
	case Kind::CatchClause:
	case Kind::JsxAttribute:
		return SemanticMeaningValue;

	case Kind::TypeParameter:
	case Kind::InterfaceDeclaration:
	case Kind::TypeAliasDeclaration:
	case Kind::JSTypeAliasDeclaration:
	case Kind::TypeLiteral:
		return SemanticMeaningType;

	case Kind::EnumMember:
	case Kind::ClassDeclaration:
		return SemanticMeaningValue | SemanticMeaningType;

	case Kind::ModuleDeclaration:
		if (isAmbientModule(node)) {
			return SemanticMeaningNamespace | SemanticMeaningValue;
		} else if (getModuleInstanceState(node) ==
		           ModuleInstanceState::Instantiated) {
			return SemanticMeaningNamespace | SemanticMeaningValue;
		} else {
			return SemanticMeaningNamespace;
		}

	case Kind::EnumDeclaration:
	case Kind::NamedImports:
	case Kind::ImportSpecifier:
	case Kind::ImportEqualsDeclaration:
	case Kind::ImportDeclaration:
	case Kind::JSImportDeclaration:
	case Kind::ExportAssignment:
	case Kind::ExportDeclaration:
		return SemanticMeaningAll;

	// An external module can be a Value
	case Kind::SourceFile:
		return SemanticMeaningNamespace | SemanticMeaningValue;
	default:
		break;
	}

	return SemanticMeaningAll;
}

// utilities.go:881 getIntersectingMeaningFromDeclarations.
SemanticMeaning getIntersectingMeaningFromDeclarations(
    Node* node, Symbol* symbol, SemanticMeaning defaultMeaning) {
	if (node == nullptr) {
		return defaultMeaning;
	}

	SemanticMeaning meaning = getMeaningFromLocation(node);
	const std::vector<Node*>& declarations = symbol->data->declarations;
	if (declarations.empty()) {
		return meaning;
	}

	SemanticMeaning lastIterationMeaning = meaning;

	// !!! TODO check if the port is correct and the for loop is needed
	auto iteration = [&](SemanticMeaning m) -> SemanticMeaning {
		for (Node* declaration : declarations) {
			SemanticMeaning declarationMeaning =
			    getMeaningFromDeclaration(declaration);

			if ((declarationMeaning & m) != 0) {
				m |= declarationMeaning;
			}
		}
		return m;
	};
	meaning = iteration(meaning);

	while (meaning != lastIterationMeaning) {
		// The result is order-sensitive, for instance if initialMeaning ==
		// Namespace, and declarations = [class, instantiated module] we need
		// to consider both as the initialMeaning intersects with the module
		// in the namespace space, and the module intersects with the class in
		// the value space. To achieve that we will keep iterating until the
		// result stabilizes.

		// Remember the last meaning
		lastIterationMeaning = meaning;
		meaning = iteration(meaning);
	}

	return meaning;
}

// utilities.go:922 getAllSuperTypeNodes — returns the node in an `extends` or
// `implements` clause of a class or interface.
std::vector<Node*> getAllSuperTypeNodes(Node* node) {
	if (isInterfaceDeclaration(node)) {
		return getHeritageElements(node, Kind::ExtendsKeyword);
	}
	if (isClassLike(node)) {
		std::vector<Node*> result;
		// core.SingleElementSlice(ast.GetClassExtendsHeritageElement(node))
		if (Node* e = getClassExtendsHeritageElement(node)) {
			result.push_back(e);
		}
		std::vector<Node*> impls = getImplementsHeritageClauseElements(node);
		result.insert(result.end(), impls.begin(), impls.end());
		return result;
	}
	return {};
}

// utilities.go:935 getParentSymbolsOfPropertyAccess.
std::vector<Symbol*> getParentSymbolsOfPropertyAccess(Node* location,
                                                    Symbol* symbol,
                                                    checker::Checker* ch) {
	if (!isRightSideOfPropertyAccess(location)) {
		return {};
	}
	checker::Type* lhsType =
	    ch->GetTypeAtLocation(location->parent->expression());
	if (lhsType == nullptr) {
		return {};
	}
	std::vector<checker::Type*> possibleSymbols;
	if ((lhsType->flags & checker::TypeFlagsUnionOrIntersection) != 0) {
		possibleSymbols = lhsType->types();
	} else if (lhsType->symbol != symbol->data->parent) {
		possibleSymbols = {lhsType};
	}
	return mapNonNil(possibleSymbols, [](checker::Type* t) -> Symbol* {
		if (t->symbol != nullptr &&
		    (t->symbol->flags &
		     (SymbolFlagsClass | SymbolFlagsInterface)) != 0) {
			return t->symbol;
		}
		return nullptr;
	});
}

// utilities.go:963 getPropertySymbolsFromBaseTypes — find symbol of the
// given property-name and add the symbol to the given result array.
//   - symbol: a symbol to start searching for the given propertyName
//   - propertyName: a name of property to search for
//   - cb: a cache of symbol from previous iterations of calling this
//     function to prevent infinite revisiting of the same symbol.
//     The value of previousIterationSymbol is undefined when the function is
//     first called.
Symbol* getPropertySymbolsFromBaseTypes(
    Symbol* symbol, const std::string& propertyName, checker::Checker* c,
    const std::function<Symbol*(Symbol*)>& cb) {
	collections::Set<Symbol*> seen;
	std::function<Symbol*(Symbol*)> recur = [&](Symbol* symbol) -> Symbol* {
		// Use `addToSeen` to ensure we don't infinitely recurse in this
		// situation:
		//      interface C extends C {
		//          /*findRef*/propName: string;
		//      }
		if ((symbol->flags &
		     (SymbolFlagsClass | SymbolFlagsInterface)) == 0 ||
		    !seen.AddIfAbsent(symbol)) {
			return nullptr;
		}
		for (Node* declaration : symbol->data->declarations) {
			for (Node* typeReference :
			     getAllSuperTypeNodes(declaration)) {
				checker::Type* propertyType =
				    c->GetTypeAtLocation(typeReference);
				if (propertyType != nullptr &&
				    propertyType->symbol != nullptr) {
					// Visit the typeReference as well to see if it directly
					// or indirectly uses that property
					if (Symbol* propertySymbol = c->GetPropertyOfType(
					        propertyType, propertyName);
					    propertySymbol != nullptr) {
						for (Symbol* rootSymbol :
						     c->GetRootSymbols(propertySymbol)) {
							if (Symbol* result = cb(rootSymbol);
							    result != nullptr) {
								return result;
							}
						}
					}
					if (Symbol* result = recur(propertyType->symbol);
					    result != nullptr) {
						return result;
					}
				}
			}
		}
		return nullptr;
	};
	return recur(symbol);
}

// utilities.go:996 getPropertySymbolFromBindingElement.
Symbol* getPropertySymbolFromBindingElement(checker::Checker* c,
                                            Node* bindingElement) {
	if (checker::Type* typeOfPattern =
	        c->GetTypeAtLocation(bindingElement->parent);
	    typeOfPattern != nullptr) {
		return c->GetPropertyOfType(typeOfPattern,
		                            bindingElement->name()->text());
	}
	return nullptr;
}

// utilities.go:1003
// getPropertySymbolOfObjectBindingPatternWithoutPropertyName.
Symbol* getPropertySymbolOfObjectBindingPatternWithoutPropertyName(
    Symbol* symbol, checker::Checker* c) {
	Node* bindingElement =
	    getDeclarationOfKind(symbol, Kind::BindingElement);
	if (bindingElement != nullptr &&
	    isObjectBindingElementWithoutPropertyName(bindingElement)) {
		return getPropertySymbolFromBindingElement(c, bindingElement);
	}
	return nullptr;
}

// utilities.go:1011 getTargetLabel.
Node* getTargetLabel(Node* referenceNode, const std::string& labelName) {
	// todo: rewrite as `ast.FindAncestor`
	while (referenceNode != nullptr) {
		if (referenceNode->kind == Kind::LabeledStatement &&
		    referenceNode->label()->text() == labelName) {
			return referenceNode->label();
		}
		referenceNode = referenceNode->parent;
	}
	return nullptr;
}

// utilities.go:1022 skipConstraint.
checker::Type* skipConstraint(checker::Type* t, checker::Checker* typeChecker) {
	if (t->IsTypeParameter()) {
		checker::Type* c = typeChecker->GetBaseConstraintOfType(t);
		if (c != nullptr) {
			return c;
		}
	}
	return t;
}

// ---------------------------------------------------------------------------
// utilities.go:1032 caseClauseTrackerState.
namespace {

struct caseClauseTrackerState : caseClauseTracker {
	collections::Set<std::string> existingStrings;
	collections::Set<Number> existingNumbers;
	collections::Set<PseudoBigInt> existingBigInts;

	// utilities.go:1049 addValue — trackerAddValue = string | jsnum.Number.
	void addValue(const checker::LiteralValue& value) override {
		if (const std::string* v = std::get_if<std::string>(&value)) {
			existingStrings.Add(*v);
		} else if (const Number* v = std::get_if<Number>(&value)) {
			existingNumbers.Add(*v);
		} else {
			TSC_UNREACHABLE("Unsupported type in addValue");
		}
	}

	// utilities.go:1060 hasValue — trackerHasValue =
	// string | jsnum.Number | jsnum.PseudoBigInt.
	bool hasValue(const checker::LiteralValue& value) override {
		if (const std::string* v = std::get_if<std::string>(&value)) {
			return existingStrings.Has(*v);
		}
		if (const Number* v = std::get_if<Number>(&value)) {
			return existingNumbers.Has(*v);
		}
		if (const PseudoBigInt* v = std::get_if<PseudoBigInt>(&value)) {
			return existingBigInts.Has(*v);
		}
		TSC_UNREACHABLE("Unsupported type in hasValue");
		return false;
	}
};

} // namespace

// utilities.go:1073 newCaseClauseTracker.
caseClauseTracker* newCaseClauseTracker(checker::Checker* typeChecker,
                                        const std::vector<Node*>& clauses) {
	caseClauseTrackerState* c = new caseClauseTrackerState();
	for (Node* clause : clauses) {
		if (!isDefaultClause(clause)) {
			Node* expression = skipParentheses(clause->expression());
			if (isLiteralExpression(expression)) {
				switch (expression->kind) {
				case Kind::NoSubstitutionTemplateLiteral:
				case Kind::StringLiteral:
					c->existingStrings.Add(expression->text());
					break;
				case Kind::NumericLiteral:
					c->existingNumbers.Add(
					    numberFromString(expression->text()));
					break;
				case Kind::BigIntLiteral:
					c->existingBigInts.Add(
					    parseValidBigInt(expression->text()));
					break;
				default:
					break;
				}
			} else {
				Symbol* symbol =
				    typeChecker->GetSymbolAtLocation(clause->expression());
				if (symbol != nullptr &&
				    symbol->data->valueDeclaration != nullptr &&
				    isEnumMember(symbol->data->valueDeclaration)) {
					checker::LiteralValue enumValue = typeChecker->GetConstantValue(
					    symbol->data->valueDeclaration);
					if (!std::holds_alternative<std::monostate>(
					        enumValue)) {
						c->addValue(enumValue);
					}
				}
			}
		}
	}
	return c;
}

// utilities.go:1105 RangeContainsRange.
bool RangeContainsRange(const TextRange& r1, const TextRange& r2) {
	return startEndContainsRange(r1.pos(), r1.end(), r2);
}

// utilities.go:1109 startEndContainsRange.
bool startEndContainsRange(int start, int end, const TextRange& textRange) {
	return start <= textRange.pos() && end >= textRange.end();
}

// utilities.go:1113 getPossibleGenericSignatures.
std::vector<checker::Signature*> getPossibleGenericSignatures(
    Node* called, int typeArgumentCount, checker::Checker* c) {
	checker::Type* typeAtLocation = c->GetTypeAtLocation(called);
	if (isOptionalChain(called->parent)) {
		typeAtLocation = removeOptionality(
		    typeAtLocation, isOptionalChainRoot(called->parent),
		    true /*isOptionalChain*/, c);
	}
	std::vector<checker::Signature*> signatures;
	if (isNewExpression(called->parent)) {
		signatures =
		    c->GetSignaturesOfType(typeAtLocation,
		                           checker::SignatureKind::Construct);
	} else {
		signatures =
		    c->GetSignaturesOfType(typeAtLocation, checker::SignatureKind::Call);
	}
	std::vector<checker::Signature*> result;
	for (checker::Signature* s : signatures) {
		if (!s->typeParameters.empty() &&
		    static_cast<int>(s->typeParameters.size()) >=
		        typeArgumentCount) {
			result.push_back(s);
		}
	}
	return result;
}

// utilities.go:1129 removeOptionality.
checker::Type* removeOptionality(checker::Type* t, bool isOptionalExpression,
                                 bool isOptionalChain, checker::Checker* c) {
	if (isOptionalExpression) {
		return c->GetNonNullableType(t);
	} else if (isOptionalChain) {
		return c->GetNonOptionalType(t);
	}
	return t;
}

// utilities.go:1138 isNoSubstitutionTemplateLiteral.
bool isNoSubstitutionTemplateLiteral(Node* node) {
	return node->kind == Kind::NoSubstitutionTemplateLiteral;
}

// utilities.go:1142 isTaggedTemplateExpression.
bool isTaggedTemplateExpression(Node* node) {
	return node->kind == Kind::TaggedTemplateExpression;
}

// utilities.go:1146 isInsideTemplateLiteral.
bool isInsideTemplateLiteral(Node* node, int position,
                             SourceFile* sourceFile) {
	return isTemplateLiteralKind(node->kind) &&
	       ((getTokenPosOfNode(node, sourceFile, false) < position &&
	         position < node->end()) ||
	        (isUnterminatedLiteral(node) && position == node->end()));
}

// Pseudo-literals
// utilities.go:1151 isTemplateHead.
bool isTemplateHead(Node* node) {
	return node->kind == Kind::TemplateHead;
}

// utilities.go:1155 isTemplateTail.
bool isTemplateTail(Node* node) {
	return node->kind == Kind::TemplateTail;
}

// utilities.go:1159 findPrecedingMatchingToken.
Node* findPrecedingMatchingToken(Node* token, Kind matchingTokenKind,
                                 SourceFile* sourceFile) {
	std::string_view closeTokenText = tokenToString(token->kind);
	std::string_view matchingTokenText =
	    tokenToString(matchingTokenKind);
	// Text-scan based fast path - can be bamboozled by comments and other
	// trivia, but often provides a good, fast approximation without too much
	// extra work in the cases where it fails.
	std::string_view text = sourceFile->Text();
	size_t bestGuessIndex = text.rfind(matchingTokenText);
	if (bestGuessIndex == std::string_view::npos) {
		return nullptr; // if the token text doesn't appear in the file,
		                // there can't be a match - super fast bail
	}
	// we can only use the textual result directly if we didn't have to count
	// any close tokens within the range
	size_t closeIndex = text.rfind(closeTokenText);
	if (closeIndex == std::string_view::npos || closeIndex < bestGuessIndex) {
		Node* nodeAtGuess = astnav::findPrecedingToken(
		    sourceFile, static_cast<int>(bestGuessIndex) + 1);
		if (nodeAtGuess != nullptr &&
		    nodeAtGuess->kind == matchingTokenKind) {
			return nodeAtGuess;
		}
	}
	Kind tokenKind = token->kind;
	int remainingMatchingTokens = 0;
	for (;;) {
		Node* preceding =
		    astnav::findPrecedingToken(sourceFile, token->pos());
		if (preceding == nullptr) {
			return nullptr;
		}
		token = preceding;
		if (token->kind == matchingTokenKind) {
			if (remainingMatchingTokens == 0) {
				return token;
			}
			remainingMatchingTokens--;
		} else if (token->kind == tokenKind) {
			remainingMatchingTokens++;
		}
	}
}

// utilities.go:1195 findContainingList.
NodeList* findContainingList(Node* node, SourceFile* file) {
	// The node might be a list element (nonsynthetic) or a comma (synthetic).
	// Either way, it will be parented by the container of the SyntaxList, not
	// the SyntaxList itself.
	NodeList* list = nullptr;
	auto visitNode = [](Node* n, NodeVisitor* /*visitor*/) -> Node* {
		return n;
	};
	auto visitNodes = [&](NodeList* nodes, NodeVisitor* /*visitor*/)
	    -> NodeList* {
		if (nodes != nullptr && RangeContainsRange(nodes->loc, node->loc)) {
			list = nodes;
		}
		return nodes;
	};
	astnav::visitEachChildAndJSDoc(node->parent, file, visitNode,
	                               visitNodes);
	return list;
}

// utilities.go:1212 getLeadingCommentRangesOfNode.
std::vector<CommentRange> getLeadingCommentRangesOfNode(Node* node,
                                                        SourceFile* file) {
	if (node->kind == Kind::JsxText) {
		return {};
	}
	std::vector<CommentRange> out;
	getLeadingCommentRanges(
	    file->Text(), node->pos(), [&](const CommentRange& r) {
		    out.push_back(r);
		    return true;
	    });
	return out;
}

// utilities.go:1220 getChildrenFromNonJSDocNode — equivalent to Strada's
// `node.getChildren()` for non-JSDoc nodes.
std::vector<Node*> getChildrenFromNonJSDocNode(Node* node,
                                               SourceFile* sourceFile) {
	std::vector<Node*> childNodes;
	node->forEachChild([&](Node* child) {
		childNodes.push_back(child);
		return false;
	});

	// If the node has no children, don't scan for tokens.
	// This prevents creating tokens for leaf nodes' own text.
	if (childNodes.empty()) {
		return {};
	}

	std::vector<Node*> children;
	int pos = node->pos();
	Scanner scanner;
	for (Node* child : childNodes) {
		getScannerForSourceFile(scanner, sourceFile, pos);
		while (pos < child->pos()) {
			Kind token = scanner.token();
			int tokenFullStart = scanner.tokenFullStart();
			int tokenEnd = scanner.tokenEnd();
			children.push_back(getOrCreateToken(sourceFile, token,
			                                  tokenFullStart, tokenEnd,
			                                  node, scanner.tokenFlags()));
			pos = tokenEnd;
			scanner.scan();
		}
		children.push_back(child);
		pos = child->end();
	}
	getScannerForSourceFile(scanner, sourceFile, pos);
	while (pos < node->end()) {
		Kind token = scanner.token();
		int tokenFullStart = scanner.tokenFullStart();
		int tokenEnd = scanner.tokenEnd();
		children.push_back(getOrCreateToken(sourceFile, token,
		                                    tokenFullStart, tokenEnd, node,
		                                    scanner.tokenFlags()));
		pos = tokenEnd;
		scanner.scan();
	}
	return children;
}

// utilities.go:1261 getContainingObjectLiteralElement — returns the
// containing object literal property declaration given a possible name
// node, e.g. "a" in x = { "a": 1 }.
Node* getContainingObjectLiteralElement(Node* node) {
	Node* element = getContainingObjectLiteralElementWorker(node);
	if (element != nullptr &&
	    (isObjectLiteralExpression(element->parent) ||
	     isJsxAttributes(element->parent))) {
		return element;
	}
	return nullptr;
}

// utilities.go:1269 getContainingObjectLiteralElementWorker.
Node* getContainingObjectLiteralElementWorker(Node* node) {
	switch (node->kind) {
	case Kind::StringLiteral:
	case Kind::NoSubstitutionTemplateLiteral:
	case Kind::NumericLiteral:
		if (node->parent->kind == Kind::ComputedPropertyName) {
			if (isObjectLiteralOrJsxElement(node->parent->parent)) {
				return node->parent->parent;
			}
			return nullptr;
		}
		[[fallthrough]];
	case Kind::Identifier:
	case Kind::JsxNamespacedName:
		if (isObjectLiteralOrJsxElement(node->parent) &&
		    (node->parent->parent->kind == Kind::ObjectLiteralExpression ||
		     node->parent->parent->kind == Kind::JsxAttributes) &&
		    node->parent->name() == node) {
			return node->parent;
		}
		break;
	default:
		break;
	}
	return nullptr;
}

// utilities.go:1287 isObjectLiteralOrJsxElement.
bool isObjectLiteralOrJsxElement(Node* node) {
	return isObjectLiteralElement(node) || isJsxAttribute(node) ||
	       isJsxSpreadAttribute(node);
}

// utilities.go:1292 nodeSeenTracker — returns a function that returns true
// if the given node has not been seen.
std::function<bool(Node*)> nodeSeenTracker() {
	auto seen = std::make_shared<collections::Set<Node*>>();
	return [seen](Node* node) -> bool { return seen->AddIfAbsent(node); };
}

// utilities.go:1300 toContextRange — FindAllReferences.toContextSpan.
TextRange* toContextRange(TextRange* textRange, SourceFile* contextFile,
                          Node* context) {
	if (context == nullptr) {
		return textRange;
	}
	// !!! isContextWithStartAndEndNode
	TextRange contextRange =
	    getRangeOfNode(context, contextFile, nullptr /*endNode*/);
	if (contextRange.pos() != textRange->pos() ||
	    contextRange.end() != textRange->end()) {
		return new TextRange(contextRange);
	}
	return nullptr;
}

// utilities.go:1312 getReferenceAtPosition.
refInfo* getReferenceAtPosition(SourceFile* sourceFile, int position,
                                compiler::SimpleProgram* program) {
	if (FileReference* referencePath =
	        findReferenceInPosition(sourceFile->ReferencedFiles, position);
	    referencePath != nullptr) {
		if (SourceFile* file =
		        program->GetSourceFileFromReference(sourceFile,
		                                          referencePath);
		    file != nullptr) {
			return new refInfo{file, file->FileName(), referencePath,
			                   false};
		}
		return nullptr;
	}

	if (FileReference* typeReferenceDirective = findReferenceInPosition(
	        sourceFile->TypeReferenceDirectives, position);
	    typeReferenceDirective != nullptr) {
		if (module::ResolvedTypeReferenceDirective* reference =
		        program
		            ->GetResolvedTypeReferenceDirectiveFromTypeReferenceDirective(
		                typeReferenceDirective, sourceFile);
		    reference != nullptr) {
			if (SourceFile* file =
			        program->GetSourceFile(reference->ResolvedFileName);
			    file != nullptr) {
				return new refInfo{file, file->FileName(),
				                   typeReferenceDirective, false};
			}
		}
		return nullptr;
	}

	if (FileReference* libReferenceDirective = findReferenceInPosition(
	        sourceFile->LibReferenceDirectives, position);
	    libReferenceDirective != nullptr) {
		if (SourceFile* file =
		        program->GetLibFileFromReference(libReferenceDirective);
		    file != nullptr) {
			return new refInfo{file, file->FileName(),
			                   libReferenceDirective, false};
		}
		return nullptr;
	}

	if (sourceFile->imports.empty() &&
	    sourceFile->ModuleAugmentations.empty()) {
		return nullptr;
	}

	Node* node = astnav::getTouchingToken(sourceFile, position);
	if (!isModuleSpecifierLike(node) ||
	    !tspath::isExternalModuleNameRelative(node->text())) {
		return nullptr;
	}

	if (module::ResolvedModule* resolution =
	        program->GetResolvedModuleFromModuleSpecifier(sourceFile, node);
	    resolution != nullptr) {
		const std::string& verifiedFileName = resolution->ResolvedFileName;
		std::string fileName = resolution->ResolvedFileName;
		if (fileName.empty()) {
			fileName = tspath::resolvePath(
			    tspath::getDirectoryPath(sourceFile->FileName()),
			    {node->text()});
		}
		refInfo* r = new refInfo();
		r->file = program->GetSourceFile(fileName);
		r->fileName = fileName;
		r->reference = nullptr;
		r->unverified = !verifiedFileName.empty();
		return r;
	}

	return nullptr;
}

// utilities.go:1362 getContextualTypeFromParent.
checker::Type* getContextualTypeFromParent(
    Node* node, checker::Checker* typeChecker,
    checker::ContextFlags contextFlags) {
	Node* parent = walkUpParenthesizedExpressions(node->parent);
	switch (parent->kind) {
	case Kind::NewExpression:
		return typeChecker->GetContextualType(parent, contextFlags);
	case Kind::BinaryExpression:
		if (isEqualityOperatorKind(
		        parent->as<BinaryExpression>()->OperatorToken->kind)) {
			return typeChecker->GetTypeAtLocation(
			    node == parent->as<BinaryExpression>()->Right
			        ? parent->as<BinaryExpression>()->Left
			        : parent->as<BinaryExpression>()->Right);
		}
		return typeChecker->GetContextualType(node, contextFlags);
	case Kind::CaseClause:
		return getSwitchedType(parent, typeChecker);
	default:
		return typeChecker->GetContextualType(node, contextFlags);
	}
}

// utilities.go:1381 getContextualTypeFromParentOrAncestorTypeNode.
checker::Type* getContextualTypeFromParentOrAncestorTypeNode(
    Node* node, checker::Checker* typeChecker) {
	if ((node->flags & NodeFlagsJSDoc) != 0 &&
	    (node->flags & NodeFlagsJavaScriptFile) == 0) {
		return nullptr;
	}

	checker::Type* contextualType =
	    getContextualTypeFromParent(node, typeChecker, checker::ContextFlagsNone);
	if (contextualType != nullptr) {
		return contextualType;
	}

	if (Node* ancestorTypeNode = getAncestorTypeNode(node);
	    ancestorTypeNode != nullptr) {
		return typeChecker->GetTypeAtLocation(ancestorTypeNode);
	}

	return nullptr;
}

// utilities.go:1398 getAncestorTypeNode.
Node* getAncestorTypeNode(Node* node) {
	Node* lastTypeNode = nullptr;
	findAncestor(node, [&](Node* n) -> bool {
		if (isTypeNode(n)) {
			lastTypeNode = n;
		}
		return !isQualifiedName(n->parent) && !isTypeNode(n->parent) &&
		       !isTypeElement(n->parent);
	});
	return lastTypeNode;
}

// utilities.go:1409 isSourceFileWithGlobalExports.
bool isSourceFileWithGlobalExports(Node* node) {
	return node != nullptr && isSourceFile(node) &&
	       node->as<SourceFile>()->GlobalExports.size() != 0;
}

// (deduped: getRangeOfEnclosingComment defined in cpp/internal/ls/format.cpp)
// (deduped: getRangeOfNode defined in cpp/internal/ls/findallreferences.cpp)

} // namespace tsc::ls
