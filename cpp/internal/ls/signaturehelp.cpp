// signaturehelp.cpp — port of tsc/internal/ls/signaturehelp.go (1439 lines).
// Signature help provider: finds the call/new/type-argument/template
// invocation enclosing the caret and produces lsproto.SignatureHelp.

#include "internal/ls/ls.h"

#include <algorithm>

#include "internal/ast/ast.h"
#include "internal/astnav/tokens.h"
#include "internal/checker/checker.h"
#include "internal/compiler/program.h"
#include "internal/core/text.h"
#include "internal/debug/debug.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/nodebuilder/types.h"
#include "internal/printer/emitcontext.h"
#include "internal/printer/printer.h"
#include "internal/scanner/scanner.h"
#include "internal/spanmap/spanmap.h"
#include "internal/stringutil/stringutil.h"

namespace tsc::ls {

// signaturehelp.go:24 — SignatureHelpTriggerCharacters /
// SignatureHelpRetriggerCharacters are advertised both in the static server
// capabilities and in the dynamic content-mapper registration, so they live
// here to keep those two declarations in sync.
const std::vector<std::string> SignatureHelpTriggerCharacters{"(", ",", "<"};
const std::vector<std::string> SignatureHelpRetriggerCharacters{")"};

// doneGuard — RAII for the `done` release callback returned by
// GetTypeCheckerForFileExclusive (Go `defer done()`).
struct doneGuard {
	std::function<void()> done;
	~doneGuard() {
		if (done) {
			done();
		}
	}
};

// signatureHelpTriggerReasonKind — local enum of GetSignatureHelpItems
// (signaturehelp.go:88). Emulates VS Code's toTsTriggerReason.
using signatureHelpTriggerReasonKind = int32_t;
inline constexpr signatureHelpTriggerReasonKind
	signatureHelpTriggerReasonKindNone = 0; // was undefined
inline constexpr signatureHelpTriggerReasonKind
	signatureHelpTriggerReasonKindInvoked = 1; // was "invoked"
inline constexpr signatureHelpTriggerReasonKind
	signatureHelpTriggerReasonKindCharacterTyped = 2; // was "characterTyped"
inline constexpr signatureHelpTriggerReasonKind
	signatureHelpTriggerReasonKindRetriggered = 3; // was "retrigger"

// signatureHelpNodeBuilderFlags — signaturehelp.go:667.
inline constexpr nodebuilder::Flags signatureHelpNodeBuilderFlags =
    nodebuilder::FlagsOmitParameterModifiers | nodebuilder::FlagsIgnoreErrors |
    nodebuilder::FlagsUseAliasDefinedOutsideCurrentScope;

// === file-local replicas — helpers owned by ast/core that this repo's ast
// slice did not port ===
namespace {

// findIndex — core.go:237.
template <class T, class F>
int findIndex(const std::vector<T>& slice, F f) {
	for (size_t i = 0; i < slice.size(); i++) {
		if (f(slice[i])) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

// getInvokedExpression — utilities.go:3764.
Node* getInvokedExpression(Node* node) {
	switch (node->kind) {
	case Kind::TaggedTemplateExpression:
		return node->as<TaggedTemplateExpression>()->Tag;
	case Kind::JsxOpeningElement:
	case Kind::JsxSelfClosingElement:
		return node->tagName();
	case Kind::BinaryExpression:
		return node->as<BinaryExpression>()->Right;
	case Kind::JsxOpeningFragment:
		return node;
	default:
		return node->expression();
	}
}

// indexOfNode — utilities.go:3783 (BinarySearchFunc on CompareNodePositions).
int indexOfNode(const std::vector<Node*>& nodes, Node* node) {
	auto it = std::lower_bound(
		nodes.begin(), nodes.end(), node, [](Node* a, Node* b) {
			return compareTextRanges(a->loc, b->loc) < 0;
		});
	if (it != nodes.end() &&
	    compareTextRanges((*it)->loc, node->loc) == 0) {
		return static_cast<int>(it - nodes.begin());
	}
	return -1;
}

// isTemplateLiteralToken — ast/utilities.go:3282.
bool isTemplateLiteralToken(Node* node) {
	return isTemplateLiteralKind(node->kind);
}

// createToken — ast.go:2935 replica (astnav's copy has internal linkage).
// `kind` should be a token kind.
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

// getOrCreateToken — ast.go:2904 replica (SourceFile.GetOrCreateToken).
// Gets a token from the file's token cache, or creates it if it does not
// already exist. This function should NOT be used for creating synthetic
// tokens that are not in the file in the first place.
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
		std::string msg = "Cannot create token from reparsed node of kind " +
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

// === forward decls for functions defined later in this file (Go package
// order is preserved; earlier callees referenced before their body) ===
Node* getEnclosingDeclarationFromInvocation(invocation* invocation_);
Node* getExpressionFromInvocation(argumentListInfo* argumentInfo);
Node* getAdjustedNode(Node* node);
contextualSignatureLocationInfo* getContextualSignatureLocationInfo(
    Node* node, SourceFile* sourceFile, checker::Checker* c);
argumentOrParameterListInfo* getArgumentOrParameterListInfo(
    Node* node, SourceFile* sourceFile, checker::Checker* c);
argumentOrParameterListAndIndex* getArgumentOrParameterListAndIndex(
    Node* node, SourceFile* sourceFile, checker::Checker* c);
NodeList* getChildListThatStartsWithOpenerToken(Node* parent,
                                                Node* openerToken);
int getArgumentIndex(Node* node, NodeList* arguments, SourceFile* sourceFile,
                     checker::Checker* c);
int getArgumentCount(Node* node, NodeList* arguments, SourceFile* sourceFile,
                     checker::Checker* c);
int getSpreadElementCount(Node* node, checker::Checker* c);
std::vector<Node*> getTokenFromNodeList(NodeList* nodeList,
                                      Node* nodeListParent,
                                      SourceFile* sourceFile);
argumentListInfo* getArgumentListInfoForTemplate(Node* tagExpression,
                                                 int argumentIndex,
                                                 SourceFile* sourceFile);
TextRange getApplicableRangeForTaggedTemplate(Node* taggedTemplate,
                                              SourceFile* sourceFile);
argumentListInfo* tryGetParameterInfo(Node* startingToken,
                                      SourceFile* sourceFile,
                                      checker::Checker* c);
bool isSyntacticOwner(Node* startingToken, Node* node,
                      SourceFile* sourceFile);
bool containsPrecedingToken(Node* startingToken, SourceFile* sourceFile,
                            Node* container);
argumentListInfo* getImmediatelyContainingArgumentInfo(
    Node* node, int position, SourceFile* sourceFile, checker::Checker* c);
CandidateOrTypeInfo* getCandidateOrTypeInfo(argumentListInfo* info,
                                          checker::Checker* c,
                                          SourceFile* sourceFile,
                                          Node* startingToken,
                                          bool onlyUseSyntacticOwners);
Symbol* chooseBetterSymbol(Symbol* s);
std::shared_ptr<lsproto::SignatureHelp> createTypeHelpItems(
    const gostd::Context& ctx, Symbol* symbol, argumentListInfo* argumentInfo,
    SourceFile* sourceFile, checker::Checker* c);
signatureInformation getTypeHelpItem(
    Symbol* symbol, const std::vector<checker::Type*>& typeParameter,
    Node* enclosingDeclaration, SourceFile* sourceFile, checker::Checker* c);
int getArgumentIndexForTemplatePiece(int spanIndex, Node* node, int position,
                                     SourceFile* sourceFile);
argumentListInfo* getContainingArgumentInfo(
    Node* node, SourceFile* sourceFile, checker::Checker* checker_,
    bool isManuallyInvoked, int position);
argumentListInfo* getImmediatelyContainingArgumentOrContextualParameterInfo(
    Node* node, int position, SourceFile* sourceFile,
    checker::Checker* checker_);
int getArgumentIndexOrCount(const std::vector<Node*>& arguments, Node* node,
                            checker::Checker* c);
TextRange getApplicableSpanForArguments(NodeList* argumentList, Node* node,
                                        SourceFile* sourceFile);
int ensureMinimumSpanSize(int start, int end);
BinaryExpression* getHighestBinary(BinaryExpression* b);
int countBinaryExpressionParameters(BinaryExpression* b);
displayPartsWriter* returnTypeToDisplayParts(checker::Signature* candidateSignature,
                                             checker::Checker* c,
                                             Node* enclosingDeclaration,
                                             SourceFile* sourceFile,
                                             bool vsCapability);
signatureHelpParameter createSignatureHelpParameterForTypeParameter(
    checker::Type* t, SourceFile* sourceFile, Node* enclosingDeclaration,
    checker::Checker* c, printer::Printer* p);

// === ProvideSignatureHelp — signaturehelp.go:49 ===
std::pair<lsproto::SignatureHelpOrNull, gostd::Error>
LanguageService::ProvideSignatureHelp(
    const gostd::Context& ctx, lsproto::DocumentUri documentURI,
    lsproto::Position position, lsproto::SignatureHelpContext* context) {
	auto [program, sourceFile] = getProgramAndFile(documentURI);
	auto positions = converters->FromLSPPositionForSourceFile(
	    sourceFile, position, spanmap::FeatureSignatureHelp);
	for (auto& projection : positions) {
		if (!projection.Fidelity.IsSingleSegment()) {
			continue;
		}
		auto items = GetSignatureHelpItems(
		    ctx, projection.Position, program, projection.Script, context);
		if (items != nullptr) {
			return {lsproto::SignatureHelpOrNull{items}, nullptr};
		}
	}
	return {lsproto::SignatureHelpOrNull{}, nullptr};
}

// === GetSignatureHelpItems — signaturehelp.go:75 ===
std::shared_ptr<lsproto::SignatureHelp>
LanguageService::GetSignatureHelpItems(
    const gostd::Context& ctx, int position,
    compiler::SimpleProgram* program, SourceFile* sourceFile,
    lsproto::SignatureHelpContext* context) {
	auto [typeChecker, done] = program->GetTypeCheckerForFileExclusive(sourceFile);
	doneGuard dg{std::move(done)};

	// Decide whether to show signature help
	Node* startingToken = astnav::findPrecedingToken(sourceFile, position);
	if (startingToken == nullptr) {
		// We are at the beginning of the file
		return nullptr;
	}

	// Emulate VS Code's toTsTriggerReason.
	auto triggerReasonKind = signatureHelpTriggerReasonKindNone;
	if (context != nullptr) {
		switch (context->TriggerKind) {
		case lsproto::SignatureHelpTriggerKindTriggerCharacter:
			if (context->TriggerCharacter.has_value()) {
				if (context->IsRetrigger) {
					triggerReasonKind = signatureHelpTriggerReasonKindRetriggered;
				} else {
					triggerReasonKind = signatureHelpTriggerReasonKindCharacterTyped;
				}
			} else {
				triggerReasonKind = signatureHelpTriggerReasonKindInvoked;
			}
			break;
		case lsproto::SignatureHelpTriggerKindContentChange:
			if (context->IsRetrigger) {
				triggerReasonKind = signatureHelpTriggerReasonKindRetriggered;
			} else {
				triggerReasonKind = signatureHelpTriggerReasonKindCharacterTyped;
			}
			break;
		case lsproto::SignatureHelpTriggerKindInvoked:
			triggerReasonKind = signatureHelpTriggerReasonKindInvoked;
			break;
		default:
			triggerReasonKind = signatureHelpTriggerReasonKindInvoked;
		}
	}

	// Only need to be careful if the user typed a character and signature help
	// wasn't showing.
	bool onlyUseSyntacticOwners =
	    triggerReasonKind == signatureHelpTriggerReasonKindCharacterTyped;

	// Bail out quickly in the middle of a string or comment, don't provide
	// signature help unless the user explicitly requested it.
	if (onlyUseSyntacticOwners &&
	    (IsInString(sourceFile, position, startingToken) ||
	     isInComment(sourceFile, position, startingToken) != nullptr)) {
		return nullptr;
	}

	bool isManuallyInvoked =
	    triggerReasonKind == signatureHelpTriggerReasonKindInvoked;
	argumentListInfo* argumentInfo = getContainingArgumentInfo(
	    startingToken, sourceFile, typeChecker, isManuallyInvoked, position);
	if (argumentInfo == nullptr) {
		return nullptr;
	}

	if (gostd::ctxErr(ctx) != nullptr) {
		return nullptr;
	}

	// Extra syntactic and semantic filtering of signature help
	CandidateOrTypeInfo* candidateInfo = getCandidateOrTypeInfo(
	    argumentInfo, typeChecker, sourceFile, startingToken,
	    onlyUseSyntacticOwners);

	if (gostd::ctxErr(ctx) != nullptr) {
		return nullptr;
	}

	if (candidateInfo == nullptr) {
		// For JS files, try a fallback that searches all source files for
		// declarations with matching names that have call signatures. This is a
		// heuristic for untyped JS code.
		if (isSourceFileJS(sourceFile)) {
			return createJSSignatureHelpItems(ctx, argumentInfo, program,
			                                  typeChecker);
		}
		return nullptr;
	}

	if (candidateInfo->candidateInfo != nullptr) {
		return createSignatureHelpItems(
		    ctx, candidateInfo->candidateInfo->candidates,
		    candidateInfo->candidateInfo->resolvedSignature, argumentInfo,
		    sourceFile, typeChecker, onlyUseSyntacticOwners);
	}
	return createTypeHelpItems(ctx, candidateInfo->typeInfo, argumentInfo,
	                           sourceFile, typeChecker);
}

// === createTypeHelpItems — signaturehelp.go:169 ===
std::shared_ptr<lsproto::SignatureHelp> createTypeHelpItems(
    const gostd::Context& ctx, Symbol* symbol, argumentListInfo* argumentInfo,
    SourceFile* sourceFile, checker::Checker* c) {
	std::vector<checker::Type*> typeParameters =
	    c->GetLocalTypeParametersOfClassOrInterfaceOrTypeAlias(symbol);
	if (typeParameters.empty()) {
		return nullptr;
	}
	// NOTE: Go compares `typeParameters == nil` — a non-nil empty slice would
	// still reach here; treat empty the same (the label path emits no angle
	// brackets for an empty list either way).
	signatureInformation item = getTypeHelpItem(
	    symbol, typeParameters,
	    getEnclosingDeclarationFromInvocation(argumentInfo->invocation_),
	    sourceFile, c);

	// Check client capabilities for activeParameter handling
	auto caps = lsproto::getClientCapabilities(ctx);
	auto& sigInfoCaps = caps->TextDocument.SignatureHelp.SignatureInformation;
	bool supportsPerSignatureActiveParam = sigInfoCaps.ActiveParameterSupport;

	// Converting signatureHelpParameter to *lsproto.ParameterInformation
	std::vector<std::shared_ptr<lsproto::ParameterInformation>> parameters;
	parameters.reserve(item.Parameters.size());
	for (auto& param : item.Parameters) {
		parameters.push_back(
		    std::shared_ptr<lsproto::ParameterInformation>(param.parameterInfo));
	}

	auto sigInfo = std::shared_ptr<lsproto::SignatureInformation>(
	    new lsproto::SignatureInformation{
	        /*Label*/ item.Label,
	        /*Documentation*/ nullptr,
	        /*Parameters*/ std::make_shared<lsproto::Slice<
	            std::shared_ptr<lsproto::ParameterInformation>>>(
	            std::move(parameters))});


	// If client supports per-signature activeParameter, set it on
	// SignatureInformation
	if (supportsPerSignatureActiveParam && !item.Parameters.empty()) {
		sigInfo->ActiveParameter =
		    std::shared_ptr<lsproto::UintegerOrNull>(
		        new lsproto::UintegerOrNull{
		            std::make_shared<uint32_t>(argumentInfo->argumentIndex)});
	}

	auto help = std::shared_ptr<lsproto::SignatureHelp>(
	    new lsproto::SignatureHelp{
	        /*Signatures*/ lsproto::Slice<
	            std::shared_ptr<lsproto::SignatureInformation>>(
	            std::vector<std::shared_ptr<lsproto::SignatureInformation>>{
	                sigInfo}),
	        /*ActiveSignature*/ 0u});


	// If client doesn't support per-signature activeParameter, set it on the
	// top-level SignatureHelp
	if (!supportsPerSignatureActiveParam && !item.Parameters.empty()) {
		help->ActiveParameter =
		    std::shared_ptr<lsproto::UintegerOrNull>(
		        new lsproto::UintegerOrNull{
		            std::make_shared<uint32_t>(argumentInfo->argumentIndex)});
	}

	return help;
}

// === getTypeHelpItem — signaturehelp.go:211 ===
signatureInformation getTypeHelpItem(
    Symbol* symbol, const std::vector<checker::Type*>& typeParameter,
    Node* enclosingDeclaration, SourceFile* sourceFile, checker::Checker* c) {
	printer::Printer* p = printer::NewPrinter(
	    printer::PrinterOptions{.NewLine = NewLineKind::LineFeed},
	    printer::PrintHandlers{}, nullptr);

	std::vector<signatureHelpParameter> parameters;
	parameters.reserve(typeParameter.size());
	for (auto* typeParam : typeParameter) {
		parameters.push_back(createSignatureHelpParameterForTypeParameter(
		    typeParam, sourceFile, enclosingDeclaration, c, p));
	}

	// Creating display label
	std::string displayParts;
	displayParts += c->SymbolToString(symbol);
	if (!parameters.empty()) {
		displayParts += tokenToString(Kind::LessThanToken);
		for (size_t i = 0; i < parameters.size(); i++) {
			if (i > 0) {
				displayParts += ", ";
			}
			displayParts += *parameters[i].parameterInfo->Label.String;
		}
		displayParts += tokenToString(Kind::GreaterThanToken);
	}

	return signatureInformation{
	    /*Label*/ displayParts,
	    /*Documentation*/ nullptr,
	    /*Parameters*/ parameters,
	    /*IsVariadic*/ false,
	};
}

// === createJSSignatureHelpItems — signaturehelp.go:244 ===
std::shared_ptr<lsproto::SignatureHelp>
LanguageService::createJSSignatureHelpItems(
    const gostd::Context& ctx, argumentListInfo* argumentInfo,
    compiler::SimpleProgram* program, checker::Checker* c) {
	if (argumentInfo->invocation_->contextualInvocation != nullptr) {
		return nullptr;
	}
	// See if we can find some symbol with the call expression name that has
	// call signatures.
	Node* expression = getExpressionFromInvocation(argumentInfo);
	if (!isPropertyAccessExpression(expression)) {
		return nullptr;
	}
	std::string name =
	    expression->as<PropertyAccessExpression>()->name->text();
	if (name.empty()) {
		return nullptr;
	}

	for (auto* sf : program->GetSourceFiles()) {
		auto result =
		    findSignatureHelpFromNamedDeclarations(ctx, sf, name,
		                                           argumentInfo, c);
		if (result != nullptr) {
			return result;
		}
	}
	return nullptr;
}

// === findSignatureHelpFromNamedDeclarations — signaturehelp.go:267 ===
std::shared_ptr<lsproto::SignatureHelp>
LanguageService::findSignatureHelpFromNamedDeclarations(
    const gostd::Context& ctx, SourceFile* sourceFile,
    const std::string& name, argumentListInfo* argumentInfo,
    checker::Checker* c) {
	std::shared_ptr<lsproto::SignatureHelp> result;
	std::function<bool(Node*)> visit;
	visit = [&](Node* node) -> bool {
		if (result != nullptr) {
			return true;
		}
		if (getDeclarationName(node) == name) {
			if (Symbol* symbol = node->symbol(); symbol != nullptr) {
				if (checker::Type* t =
				        c->GetTypeOfSymbolAtLocation(symbol, node);
				    t != nullptr) {
					auto callSignatures = c->GetCallSignatures(t);
					if (!callSignatures.empty()) {
						result = createSignatureHelpItems(
						    ctx, callSignatures, callSignatures[0],
						    argumentInfo, sourceFile, c,
						    true /*useFullPrefix*/);
						if (result != nullptr) {
							return true;
						}
					}
				}
			}
		}
		node->forEachChild(
		    [&](Node* child) -> bool { return visit(child); });
		return result != nullptr;
	};
	visit(sourceFile->asNode());
	return result;
}

// === createSignatureHelpItems — signaturehelp.go:295 ===
std::shared_ptr<lsproto::SignatureHelp>
LanguageService::createSignatureHelpItems(
    const gostd::Context& ctx,
    const std::vector<checker::Signature*>& candidates,
    checker::Signature* resolvedSignature, argumentListInfo* argumentInfo,
    SourceFile* sourceFile, checker::Checker* c, bool useFullPrefix) {
	auto caps = lsproto::getClientCapabilities(ctx);
	lsproto::MarkupKind docFormat = lsproto::PreferredMarkupKind(
	    caps->TextDocument.SignatureHelp.SignatureInformation
	        .DocumentationFormat);
	bool vsCapability = caps->VSSupportsVisualStudioExtensions;

	Node* enclosingDeclaration =
	    getEnclosingDeclarationFromInvocation(argumentInfo->invocation_);
	if (enclosingDeclaration == nullptr) {
		return nullptr;
	}
	Symbol* callTargetSymbol = nullptr;
	if (argumentInfo->invocation_->contextualInvocation != nullptr) {
		callTargetSymbol =
		    argumentInfo->invocation_->contextualInvocation->symbol;
	} else {
		callTargetSymbol =
		    c->GetSymbolAtLocation(getExpressionFromInvocation(argumentInfo));
		if (callTargetSymbol == nullptr && useFullPrefix &&
		    resolvedSignature->declaration != nullptr) {
			callTargetSymbol = resolvedSignature->declaration->symbol();
		}
	}

	std::string callTargetDisplayParts;
	// A contextual signature for an anonymous inline function type (e.g. a
	// callback argument) has a synthetic symbol whose name is an internal
	// marker such as "\xFEtype". There is no meaningful name to show, so
	// render the signature with no prefix (as we already do when there is no
	// call target symbol) rather than leaking the internal name.
	if (callTargetSymbol != nullptr &&
	    !callTargetSymbol->name.starts_with(kInternalSymbolNamePrefix)) {
		if (useFullPrefix) {
			callTargetDisplayParts += c->SymbolToStringEx(
			    callTargetSymbol, sourceFile->asNode(), SymbolFlagsNone,
			    checker::SymbolFormatFlagsUseAliasDefinedOutsideCurrentScope);
		} else {
			callTargetDisplayParts += c->SymbolToString(callTargetSymbol);
		}
	}
	std::vector<std::vector<signatureInformation>> items;
	items.reserve(candidates.size());
	for (auto* candidateSignature : candidates) {
		items.push_back(getSignatureHelpItem(
		    candidateSignature, argumentInfo->isTypeParameterList,
		    callTargetDisplayParts, callTargetSymbol, enclosingDeclaration,
		    sourceFile, c, docFormat, vsCapability));
	}

	int selectedItemIndex = 0;
	int itemSeen = 0;
	for (size_t i = 0; i < items.size(); i++) {
		auto& item = items[i];
		if (candidates[i] == resolvedSignature) {
			selectedItemIndex = itemSeen;
			if (item.size() > 1) {
				int count = 0;
				for (auto& j : item) {
					if (j.IsVariadic ||
					    static_cast<int>(j.Parameters.size()) >=
					        argumentInfo->argumentCount) {
						selectedItemIndex = itemSeen + count;
						break;
					}
					count++;
				}
			}
		}
		itemSeen += static_cast<int>(item.size());
	}

	debug::assert(selectedItemIndex != -1);
	std::vector<signatureInformation> flattenedSignatures;
	for (auto& item : items) {
		flattenedSignatures.insert(flattenedSignatures.end(), item.begin(),
		                           item.end());
	}
	if (flattenedSignatures.empty()) {
		return nullptr;
	}

	// Check client capabilities for activeParameter handling
	auto& sigInfoCaps = caps->TextDocument.SignatureHelp.SignatureInformation;
	bool supportsPerSignatureActiveParam = sigInfoCaps.ActiveParameterSupport;
	bool supportsNullActiveParam = sigInfoCaps.NoActiveParameterSupport;

	// Converting []signatureInformation to []*lsproto.SignatureInformation
	std::vector<std::shared_ptr<lsproto::SignatureInformation>>
	    signatureInformation;
	signatureInformation.reserve(flattenedSignatures.size());
	for (auto& item : flattenedSignatures) {
		std::vector<std::shared_ptr<lsproto::ParameterInformation>> parameters;
		parameters.reserve(item.Parameters.size());
		for (auto& param : item.Parameters) {
			parameters.push_back(std::shared_ptr<lsproto::ParameterInformation>(
			    param.parameterInfo));
		}
		std::shared_ptr<lsproto::StringOrMarkupContent> documentation;
		if (item.Documentation != nullptr) {
			documentation = std::make_shared<lsproto::StringOrMarkupContent>();
			documentation->MarkupContent =
			    std::shared_ptr<lsproto::MarkupContent>(
			        new lsproto::MarkupContent{
			            /*Kind*/ docFormat,
			            /*Value*/ *item.Documentation});
		}
		auto sigInfo = std::shared_ptr<lsproto::SignatureInformation>(
		    new lsproto::SignatureInformation{
		        /*Label*/ item.Label,
		        /*Documentation*/ documentation,
		        /*Parameters*/ std::make_shared<lsproto::Slice<
		            std::shared_ptr<lsproto::ParameterInformation>>>(
		            std::move(parameters))});


		// Set VS-specific colorized label if we have classified runs
		if (item.ColorizedRuns && !item.ColorizedRuns->empty()) {
			sigInfo->VSColorizedLabel =
			    std::shared_ptr<lsproto::VSClassifiedTextElement>(
			        new lsproto::VSClassifiedTextElement{
			            /*Runs*/ item.ColorizedRuns});
		}

		// If client supports per-signature activeParameter, set it on each
		// SignatureInformation
		if (supportsPerSignatureActiveParam) {
			sigInfo->ActiveParameter = computeActiveParameter(
			    item, argumentInfo->argumentIndex, supportsNullActiveParam);
		}

		signatureInformation.push_back(sigInfo);
	}

	auto help = std::shared_ptr<lsproto::SignatureHelp>(
	    new lsproto::SignatureHelp{
	        /*Signatures*/ lsproto::Slice<
	            std::shared_ptr<lsproto::SignatureInformation>>(
	            std::move(signatureInformation)),
	        /*ActiveSignature*/ static_cast<uint32_t>(selectedItemIndex)});


	// If client doesn't support per-signature activeParameter, set it on the
	// top-level SignatureHelp
	if (!supportsPerSignatureActiveParam) {
		auto& activeSignature = flattenedSignatures[selectedItemIndex];
		help->ActiveParameter =
		    computeActiveParameter(activeSignature,
		                           argumentInfo->argumentIndex,
		                           supportsNullActiveParam);
	}

	return help;
}

// === computeActiveParameter — signaturehelp.go:419 ===
// Calculates the active parameter index for a signature, handling variadic
// signatures and null support appropriately.
std::shared_ptr<lsproto::UintegerOrNull>
LanguageService::computeActiveParameter(
    signatureInformation sig, int argumentIndex, bool supportsNull) {
	int paramCount = static_cast<int>(sig.Parameters.size());
	if (paramCount == 0) {
		// No parameters, return nil (omit the field)
		return nullptr;
	}

	uint32_t activeParam = static_cast<uint32_t>(argumentIndex);

	if (sig.IsVariadic) {
		int firstRest = findIndex(
		    sig.Parameters,
		    [](const signatureHelpParameter& p) { return p.isRest; });
		if (-1 < firstRest && firstRest < paramCount - 1) {
			// Middle rest parameter - we can't accurately highlight, so
			// indicate "no active parameter"
			if (supportsNull) {
				return std::make_shared<lsproto::UintegerOrNull>(); // null means "no parameter is active"
			}
			// Client doesn't support null, use out-of-range index (defaults to
			// 0 per LSP spec)
			return std::shared_ptr<lsproto::UintegerOrNull>(
			    new lsproto::UintegerOrNull{
			        std::make_shared<uint32_t>(paramCount)});
		}
		// Clamp to last parameter for trailing rest parameters
		if (activeParam > static_cast<uint32_t>(paramCount - 1)) {
			activeParam = static_cast<uint32_t>(paramCount - 1);
		}
	}

	return std::shared_ptr<lsproto::UintegerOrNull>(
	    new lsproto::UintegerOrNull{
	        std::make_shared<uint32_t>(activeParam)});
}

// === getSignatureHelpItem — signaturehelp.go:449 ===
std::vector<signatureInformation> LanguageService::getSignatureHelpItem(
    checker::Signature* candidate, bool isTypeParameterList,
    const std::string& callTargetSymbol, Symbol* callTargetSym,
    Node* enclosingDeclaration, SourceFile* sourceFile, checker::Checker* c,
    lsproto::MarkupKind docFormat, bool vsCapability) {
	std::vector<signatureHelpItemInfo*> infos;
	if (isTypeParameterList) {
		infos = itemInfoForTypeParameters(candidate, c, enclosingDeclaration,
		                                  sourceFile, docFormat, vsCapability);
	} else {
		infos = itemInfoForParameters(candidate, c, enclosingDeclaration,
		                              sourceFile, docFormat, vsCapability);
	}

	displayPartsWriter* suffixDpw = returnTypeToDisplayParts(
	    candidate, c, enclosingDeclaration, sourceFile, vsCapability);

	// Generate documentation from the signature's declaration
	std::string* documentation = nullptr;
	if (Node* declaration = candidate->declaration; declaration != nullptr) {
		std::string doc = getDocumentationFromDeclaration(
		    documentationLocationMapper(spanmap::FeatureSignatureHelp), c,
		    nullptr, declaration, nullptr, docFormat, true /*commentOnly*/);
		if (!doc.empty()) {
			documentation = new std::string(doc);
		}
	}

	std::vector<signatureInformation> result;
	result.reserve(infos.size());
	for (auto* info : infos) {
		displayPartsWriter* labelDpw = newDisplayPartsWriter(vsCapability);
		if (!callTargetSymbol.empty()) {
			labelDpw->WriteSymbol(callTargetSymbol, callTargetSym);
		}
		labelDpw->WriteFrom(info->writer);
		labelDpw->WriteFrom(suffixDpw);

		result.push_back(signatureInformation{
		    /*Label*/ labelDpw->String(),
		    /*Documentation*/ documentation,
		    /*Parameters*/ info->parameters,
		    /*IsVariadic*/ info->isVariadic,
		    /*ColorizedRuns*/ labelDpw->GetRuns(),
		});
	}
	return result;
}

// === returnTypeToDisplayParts — signaturehelp.go:488 ===
displayPartsWriter* returnTypeToDisplayParts(
    checker::Signature* candidateSignature, checker::Checker* c,
    Node* enclosingDeclaration, SourceFile* sourceFile, bool vsCapability) {
	displayPartsWriter* dpw = newDisplayPartsWriter(vsCapability);

	// Add ": " prefix
	dpw->WritePunctuation(": ");

	checker::TypePredicate* predicate =
	    c->GetTypePredicateOfSignature(candidateSignature);
	if (predicate != nullptr) {
		dpw->Write(c->TypePredicateToString(predicate));
	} else {
		checker::Type* returnType =
		    c->GetReturnTypeOfSignature(candidateSignature);
		Node* typeNode = c->TypeToTypeNode(
		    returnType, enclosingDeclaration, signatureHelpNodeBuilderFlags,
		    nullptr);
		if (typeNode != nullptr) {
			printer::Printer* p = printer::NewPrinter(
			    printer::PrinterOptions{.NewLine = NewLineKind::LineFeed},
			    printer::PrintHandlers{}, printer::NewEmitContext());
			// Use a temporary writer for p.Write since the printer calls
			// Clear() on its writer
			displayPartsWriter* tempDpw = newDisplayPartsWriter(vsCapability);
			p->Write(typeNode, sourceFile, tempDpw, nullptr);
			dpw->WriteFrom(tempDpw);
		} else {
			dpw->Write(c->TypeToString(returnType));
		}
	}
	return dpw;
}

// === itemInfoForTypeParameters — signaturehelp.go:513 ===
std::vector<signatureHelpItemInfo*>
LanguageService::itemInfoForTypeParameters(
    checker::Signature* candidateSignature, checker::Checker* c,
    Node* enclosingDeclaration, SourceFile* sourceFile,
    lsproto::MarkupKind docFormat, bool vsCapability) {
	printer::EmitContext* emitContext = printer::NewEmitContext();
	printer::Printer* p = printer::NewPrinter(
	    printer::PrinterOptions{.NewLine = NewLineKind::LineFeed},
	    printer::PrintHandlers{}, emitContext);

	std::vector<checker::Type*> typeParameters;
	if (candidateSignature->target != nullptr) {
		typeParameters = candidateSignature->target->typeParameters;
	} else {
		typeParameters = candidateSignature->typeParameters;
	}
	std::vector<signatureHelpParameter> signatureHelpTypeParameters;
	signatureHelpTypeParameters.reserve(typeParameters.size());
	for (auto* typeParameter : typeParameters) {
		signatureHelpTypeParameters.push_back(
		    createSignatureHelpParameterForTypeParameter(
		        typeParameter, sourceFile, enclosingDeclaration, c, p));
	}

	std::vector<signatureHelpParameter> thisParameter;
	if (candidateSignature->thisParameter != nullptr) {
		thisParameter.push_back(createSignatureHelpParameterForParameter(
		    candidateSignature->thisParameter, enclosingDeclaration, p,
		    sourceFile, c, docFormat));
	}

	// Creating type parameter display label
	displayPartsWriter* dpw = newDisplayPartsWriter(vsCapability);

	std::string_view lessThanToken =
	    tokenToString(Kind::LessThanToken);
	dpw->WritePunctuation(std::string(lessThanToken));
	for (size_t i = 0; i < signatureHelpTypeParameters.size(); i++) {
		if (i > 0) {
			dpw->WritePunctuation(", ");
		}
		std::string label =
		    *signatureHelpTypeParameters[i].parameterInfo->Label.String;
		dpw->WriteClassified(
		    label, lsproto::ClassificationTypeNameTypeParameterName);
	}
	std::string_view greaterThanToken =
	    tokenToString(Kind::GreaterThanToken);
	dpw->WritePunctuation(std::string(greaterThanToken));

	// Creating display label for parameters like, (a: string, b: number)
	auto lists = c->GetExpandedParameters(candidateSignature, false);
	if (!lists.empty()) {
		std::string_view openParen =
		    tokenToString(Kind::OpenParenToken);
		dpw->WritePunctuation(std::string(openParen));
	}

	std::vector<signatureHelpItemInfo*> result;
	result.reserve(lists.size());
	for (auto& parameterList : lists) {
		displayPartsWriter* paramDpw = newDisplayPartsWriter(vsCapability);
		paramDpw->WriteFrom(dpw);

		std::vector<signatureHelpParameter> parameters = thisParameter;
		for (size_t j = 0; j < parameterList.size(); j++) {
			Symbol* param = parameterList[j];
			Node* paramNode = checker::NewNodeBuilder(c, emitContext)
			                      ->symbolToParameterDeclaration(
			                          param, enclosingDeclaration,
			                          signatureHelpNodeBuilderFlags,
			                          nodebuilder::InternalFlagsNone,
			                          nullptr);

			if (j > 0) {
				paramDpw->WritePunctuation(", ");
			}
			// Use a temporary writer for p.Write since the printer calls
			// Clear() on its writer
			displayPartsWriter* tempDpw = newDisplayPartsWriter(vsCapability);
			p->Write(paramNode, sourceFile, tempDpw, nullptr);
			std::string paramLabel = tempDpw->String();
			paramDpw->WriteFrom(tempDpw);

			signatureHelpParameter parameter =
			    createSignatureHelpParameterFromLabel(param, paramLabel, c,
			                                          docFormat);
			parameters.push_back(parameter);
		}
		std::string_view closeParen =
		    tokenToString(Kind::CloseParenToken);
		paramDpw->WritePunctuation(std::string(closeParen));

		result.push_back(new signatureHelpItemInfo{
		    /*isVariadic*/ false,
		    /*parameters*/ signatureHelpTypeParameters,
		    /*writer*/ paramDpw,
		});
	}
	return result;
}

// === itemInfoForParameters — signaturehelp.go:588 ===
std::vector<signatureHelpItemInfo*> LanguageService::itemInfoForParameters(
    checker::Signature* candidateSignature, checker::Checker* c,
    Node* enclosingDeclaratipn, SourceFile* sourceFile,
    lsproto::MarkupKind docFormat, bool vsCapability) {
	printer::EmitContext* emitContext = printer::NewEmitContext();
	printer::Printer* p = printer::NewPrinter(
	    printer::PrinterOptions{.NewLine = NewLineKind::LineFeed},
	    printer::PrintHandlers{}, emitContext);

	auto signatureTypeParameters = candidateSignature->typeParameters;
	std::vector<signatureHelpParameter> signatureHelpTypeParameters;
	signatureHelpTypeParameters.reserve(signatureTypeParameters.size());
	if (!signatureTypeParameters.empty()) {
		for (auto* typeParameter : signatureTypeParameters) {
			signatureHelpTypeParameters.push_back(
			    createSignatureHelpParameterForTypeParameter(
			        typeParameter, sourceFile, enclosingDeclaratipn, c, p));
		}
	}

	// Creating display label for type parameters like, <T, U>
	displayPartsWriter* dpw = newDisplayPartsWriter(vsCapability);

	if (!signatureHelpTypeParameters.empty()) {
		std::string_view lessThanToken =
		    tokenToString(Kind::LessThanToken);
		dpw->WritePunctuation(std::string(lessThanToken));
		for (size_t i = 0; i < signatureHelpTypeParameters.size(); i++) {
			if (i > 0) {
				dpw->WritePunctuation(", ");
			}
			std::string label =
			    *signatureHelpTypeParameters[i].parameterInfo->Label.String;
			dpw->WriteClassified(
			    label, lsproto::ClassificationTypeNameTypeParameterName);
		}
		std::string_view greaterThanToken =
		    tokenToString(Kind::GreaterThanToken);
		dpw->WritePunctuation(std::string(greaterThanToken));
	}

	// Creating display parts for parameters. For example, (a: string, b:
	// number)
	auto lists = c->GetExpandedParameters(candidateSignature, false);
	if (!lists.empty()) {
		std::string_view openParen =
		    tokenToString(Kind::OpenParenToken);
		dpw->WritePunctuation(std::string(openParen));
	}

	auto isVariadic = [&](const std::vector<Symbol*>& parameterList) -> bool {
		if (!c->HasEffectiveRestParameter(candidateSignature)) {
			return false;
		}
		if (lists.size() == 1) {
			return true;
		}
		return !parameterList.empty() &&
		       parameterList.back() != nullptr &&
		       (parameterList.back()->checkFlags &
		        CheckFlagsRestParameter) != 0;
	};

	std::vector<signatureHelpItemInfo*> result;
	result.reserve(lists.size());
	for (auto& parameterList : lists) {
		std::vector<signatureHelpParameter> parameters(parameterList.size());
		displayPartsWriter* paramDpw = newDisplayPartsWriter(vsCapability);
		paramDpw->WriteFrom(dpw);

		for (size_t j = 0; j < parameterList.size(); j++) {
			Symbol* param = parameterList[j];
			Node* paramNode = checker::NewNodeBuilder(c, emitContext)
			                      ->symbolToParameterDeclaration(
			                          param, enclosingDeclaratipn,
			                          signatureHelpNodeBuilderFlags,
			                          nodebuilder::InternalFlagsNone,
			                          nullptr);

			if (j > 0) {
				paramDpw->WritePunctuation(", ");
			}
			// Use a temporary writer for p.Write since the printer calls
			// Clear() on its writer
			displayPartsWriter* tempDpw = newDisplayPartsWriter(vsCapability);
			p->Write(paramNode, sourceFile, tempDpw, nullptr);
			std::string paramLabel = tempDpw->String();
			paramDpw->WriteFrom(tempDpw);

			signatureHelpParameter parameter =
			    createSignatureHelpParameterFromLabel(param, paramLabel, c,
			                                          docFormat);
			parameters[j] = parameter;
		}
		std::string_view closeParen =
		    tokenToString(Kind::CloseParenToken);
		paramDpw->WritePunctuation(std::string(closeParen));

		result.push_back(new signatureHelpItemInfo{
		    /*isVariadic*/ isVariadic(parameterList),
		    /*parameters*/ parameters,
		    /*writer*/ paramDpw,
		});
	}
	return result;
}

// === createSignatureHelpParameterFromLabel — signaturehelp.go:669 ===
// Creates a signatureHelpParameter from a pre-computed label string.
signatureHelpParameter LanguageService::createSignatureHelpParameterFromLabel(
    Symbol* parameter, const std::string& label, checker::Checker* c,
    lsproto::MarkupKind docFormat) {
	bool isOptional =
	    (parameter->checkFlags & CheckFlagsOptionalParameter) != 0;
	bool isRest = (parameter->checkFlags & CheckFlagsRestParameter) != 0;
	std::shared_ptr<lsproto::StringOrMarkupContent> documentation;
	if (parameter->valueDeclaration != nullptr) {
		std::string doc = getDocumentationFromDeclaration(
		    documentationLocationMapper(spanmap::FeatureSignatureHelp), c,
		    nullptr, parameter->valueDeclaration, nullptr, docFormat,
		    true /*commentOnly*/);
		if (!doc.empty()) {
			documentation = std::make_shared<lsproto::StringOrMarkupContent>();
			documentation->MarkupContent =
			    std::shared_ptr<lsproto::MarkupContent>(
			        new lsproto::MarkupContent{
			            /*Kind*/ docFormat,
			            /*Value*/ doc});
		}
	}
	return signatureHelpParameter{
	    /*parameterInfo*/ new lsproto::ParameterInformation{
	        /*Label*/ lsproto::StringOrTuple{
	            std::make_shared<std::string>(label), nullptr},
	        /*Documentation*/ documentation,
	    },
	    /*isRest*/ isRest,
	    /*isOptional*/ isOptional,
	};
}

// === createSignatureHelpParameterForParameter — signaturehelp.go:694 ===
signatureHelpParameter
LanguageService::createSignatureHelpParameterForParameter(
    Symbol* parameter, Node* enclosingDeclaration, printer::Printer* p,
    SourceFile* sourceFile, checker::Checker* c,
    lsproto::MarkupKind docFormat) {
	std::string display = p->Emit(
	    checker::NewNodeBuilder(c, printer::NewEmitContext())
	        ->symbolToParameterDeclaration(
	            parameter, enclosingDeclaration,
	            signatureHelpNodeBuilderFlags,
	            nodebuilder::InternalFlagsNone, nullptr),
	    sourceFile);
	return createSignatureHelpParameterFromLabel(parameter, display, c,
	                                             docFormat);
}

// === createSignatureHelpParameterForTypeParameter — signaturehelp.go:699 ===
signatureHelpParameter createSignatureHelpParameterForTypeParameter(
    checker::Type* t, SourceFile* sourceFile, Node* enclosingDeclaration,
    checker::Checker* c, printer::Printer* p) {
	std::string display = p->Emit(
	    checker::NewNodeBuilder(c, printer::NewEmitContext())
	        ->TypeParameterToDeclaration(
	            t, enclosingDeclaration, signatureHelpNodeBuilderFlags,
	            nodebuilder::InternalFlagsNone, nullptr),
	    sourceFile);
	return signatureHelpParameter{
	    /*parameterInfo*/ new lsproto::ParameterInformation{
	        /*Label*/ lsproto::StringOrTuple{
	            std::make_shared<std::string>(display), nullptr},
	    },
	    /*isRest*/ false,
	    /*isOptional*/ false,
	};
}

// === getEnclosingDeclarationFromInvocation — signaturehelp.go:740 ===
Node* getEnclosingDeclarationFromInvocation(invocation* invocation_) {
	if (invocation_->callInvocation != nullptr) {
		return invocation_->callInvocation->node;
	} else if (invocation_->typeArgsInvocation != nullptr) {
		return invocation_->typeArgsInvocation->called;
	} else {
		return invocation_->contextualInvocation->node;
	}
}

// === getExpressionFromInvocation — signaturehelp.go:750 ===
Node* getExpressionFromInvocation(argumentListInfo* argumentInfo) {
	if (argumentInfo->invocation_->callInvocation != nullptr) {
		return getInvokedExpression(
		    argumentInfo->invocation_->callInvocation->node);
	}
	return argumentInfo->invocation_->typeArgsInvocation->called;
}

// === getCandidateOrTypeInfo — signaturehelp.go:767 ===
CandidateOrTypeInfo* getCandidateOrTypeInfo(
    argumentListInfo* info, checker::Checker* c, SourceFile* sourceFile,
    Node* startingToken, bool onlyUseSyntacticOwners) {
	if (info->invocation_->callInvocation != nullptr) {
		if (onlyUseSyntacticOwners &&
		    !isSyntacticOwner(startingToken,
		                      info->invocation_->callInvocation->node,
		                      sourceFile)) {
			return nullptr;
		}

		auto [resolvedSignature, candidates] =
		    checker::GetResolvedSignatureForSignatureHelp(
		        info->invocation_->callInvocation->node, info->argumentCount,
		        c);
		if (candidates.empty()) {
			return nullptr;
		}

		auto* ci = new CandidateOrTypeInfo{};
		ci->candidateInfo = new candidateInfo{
		    /*candidates*/ candidates,
		    /*resolvedSignature*/ resolvedSignature,
		};
		return ci;
	}
	if (info->invocation_->typeArgsInvocation != nullptr) {
		Node* called = info->invocation_->typeArgsInvocation->called;
		Node* container = called;
		if (isIdentifier(called)) {
			container = called->parent;
		}

		if (onlyUseSyntacticOwners &&
		    !containsPrecedingToken(startingToken, sourceFile, container)) {
			return nullptr;
		}

		auto candidates = getPossibleGenericSignatures(
		    called, info->argumentCount, c);
		if (!candidates.empty()) {
			auto* ci = new CandidateOrTypeInfo{};
			ci->candidateInfo = new candidateInfo{
			    /*candidates*/ candidates,
			    /*resolvedSignature*/ candidates[0],
			};
			return ci;
		}

		if (Symbol* symbol = c->GetSymbolAtLocation(called);
		    symbol != nullptr) {
			auto* ci = new CandidateOrTypeInfo{};
			ci->typeInfo = symbol;
			return ci;
		}

		// This can happen in the case of an unresolved symbol.
		return nullptr;
	}

	if (info->invocation_->contextualInvocation != nullptr) {
		auto* ci = new CandidateOrTypeInfo{};
		ci->candidateInfo = new candidateInfo{
		    /*candidates*/ {info->invocation_->contextualInvocation->signature},
		    /*resolvedSignature*/ info->invocation_->contextualInvocation
		        ->signature,
		};
		return ci;
	}
	debug::assertNever(info->invocation_);
	return nullptr;
}

// === isSyntacticOwner — signaturehelp.go:828 ===
bool isSyntacticOwner(Node* startingToken, Node* node,
                      SourceFile* sourceFile) {
	if (!isCallOrNewExpression(node)) {
		return false;
	}
	std::vector<Node*> invocationChildren =
	    getChildrenFromNonJSDocNode(node, sourceFile);
	switch (startingToken->kind) {
	case Kind::OpenParenToken:
	case Kind::CommaToken:
		return std::find(invocationChildren.begin(),
		                 invocationChildren.end(),
		                 startingToken) != invocationChildren.end();
	case Kind::LessThanToken:
		return containsPrecedingToken(startingToken, sourceFile,
		                              node->expression());
	default:
		return false;
	}
}

// === containsPrecedingToken — signaturehelp.go:843 ===
bool containsPrecedingToken(Node* startingToken, SourceFile* sourceFile,
                            Node* container) {
	TextPos pos = startingToken->pos();
	// There's a possibility that `startingToken.parent` contains only
	// `startingToken` and missing nodes, none of which are valid to be
	// returned by `findPrecedingToken`. In that case, the preceding token we
	// want is actually higher up the tree—almost definitely the next parent,
	// but theoretically the situation with missing nodes might be happening
	// on multiple nested levels.
	Node* currentParent = startingToken->parent;
	while (currentParent != nullptr) {
		Node* precedingToken = astnav::findPrecedingTokenEx(
		    sourceFile, pos, currentParent, true /*excludeJSDoc*/);
		if (precedingToken != nullptr) {
			// RangeContainsRange — utilities.go:1105.
			return precedingToken->loc.containedBy(container->loc);
		}
		currentParent = currentParent->parent;
	}
	return false;
}

// === getContainingArgumentInfo — signaturehelp.go:861 ===
std::shared_ptr<lsproto::SignatureHelp> createTypeHelpItems(
    const gostd::Context& ctx, Symbol* symbol, argumentListInfo* argumentInfo,
    SourceFile* sourceFile, checker::Checker* c);
signatureInformation getTypeHelpItem(
    Symbol* symbol, const std::vector<checker::Type*>& typeParameter,
    Node* enclosingDeclaration, SourceFile* sourceFile, checker::Checker* c);
int getArgumentIndexForTemplatePiece(int spanIndex, Node* node, int position,
                                     SourceFile* sourceFile);
argumentListInfo* getContainingArgumentInfo(
    Node* node, SourceFile* sourceFile, checker::Checker* checker_,
    bool isManuallyInvoked, int position) {
	argumentListInfo* firstArgumentInfo = nullptr;
	for (Node* n = node; !isSourceFile(n) &&
	     (isManuallyInvoked || !isBlock(n));
	     n = n->parent) {
		// If the node is not a subspan of its parent, this is a big problem.
		// There have been crashes that might be caused by this violation.
		debug::assert(
		    n->loc.containedBy(n->parent->loc));
		argumentListInfo* argumentInfo =
		    getImmediatelyContainingArgumentOrContextualParameterInfo(
		        n, position, sourceFile, checker_);
		if (argumentInfo != nullptr) {
			// For contextual invocations (e.g., arrow functions with contextual
			// types), always return immediately without checking the
			// position. This ensures that when inside a callback's parameter
			// list, we show the callback's signature, not the outer call's
			// signature.
			if (argumentInfo->invocation_->contextualInvocation != nullptr) {
				return argumentInfo;
			}

			// Remember the first (innermost) argument info we find
			if (firstArgumentInfo == nullptr) {
				firstArgumentInfo = argumentInfo;
			}

			// If the position is at the end boundary of an argument list,
			// keep the innermost call. This covers cases like foo(bar("x"|))
			// where the cursor is still inside the inner invocation, just
			// before its closing paren.
			if (argumentInfo->argumentsSpan.end() == position) {
				return argumentInfo;
			}

			// If any call's span contains the position, return it.
			// We walk from inner to outer, so this naturally prefers the
			// innermost call when multiple calls contain the position.
			if (argumentInfo->argumentsSpan.contains(position)) {
				return argumentInfo;
			}
		}
	}

	// No call's span contains the position. Fall back to the innermost call
	// we found. This covers boundary positions that are still syntactically
	// associated with that invocation, such as being at the end of the
	// argument list or on the close paren.
	return firstArgumentInfo;
}

// === getImmediatelyContainingArgumentOrContextualParameterInfo —
// signaturehelp.go:904 ===
argumentListInfo* getImmediatelyContainingArgumentOrContextualParameterInfo(
    Node* node, int position, SourceFile* sourceFile,
    checker::Checker* checker_) {
	argumentListInfo* result =
	    tryGetParameterInfo(node, sourceFile, checker_);
	if (result == nullptr) {
		return getImmediatelyContainingArgumentInfo(node, position,
		                                          sourceFile, checker_);
	}
	return result;
}

// === getImmediatelyContainingArgumentInfo — signaturehelp.go:923 ===
// Returns relevant information for the argument list and the current argument
// if we are in the argument of an invocation; returns undefined otherwise.
argumentListInfo* getImmediatelyContainingArgumentInfo(
    Node* node, int position, SourceFile* sourceFile, checker::Checker* c) {
	Node* parent = node->parent;
	if (isCallOrNewExpression(parent)) {
		// There are 3 cases to handle:
		//   1. The token introduces a list, and should begin a signature help
		//      session
		//   2. The token is either not associated with a list, or ends a list,
		//      so the session should end
		//   3. The token is buried inside a list, and should give signature
		//      help
		//
		// The following are examples of each:
		//
		//    Case 1:
		//          foo<#T, U>(#a, b)    -> The token introduces a list, and
		//          should begin a signature help session
		//    Case 2:
		//          fo#o<T, U>#(a, b)#   -> The token is either not associated
		//          with a list, or ends a list, so the session should end
		//    Case 3:
		//          foo<T#, U#>(a#, #b#) -> The token is buried inside a list,
		//          and should give signature help
		// Find out if 'node' is an argument, a type argument, or neither
		argumentOrParameterListInfo* info =
		    getArgumentOrParameterListInfo(node, sourceFile, c);
		if (info == nullptr) {
			return nullptr;
		}
		NodeList* list = info->list;
		int argumentIndex = info->argumentIndex;
		int argumentCount = info->argumentCount;
		TextRange argumentsSpan = info->argumentsSpan;
		bool isTypeParameterList = false;
		NodeList* parentTypeArgumentList = parent->typeArgumentList();
		if (parentTypeArgumentList != nullptr) {
			if (parentTypeArgumentList->pos() == list->pos()) {
				isTypeParameterList = true;
			}
		}
		return new argumentListInfo{
		    /*isTypeParameterList*/ isTypeParameterList,
		    /*invocation*/ new invocation{
		        new callInvocation{parent}, nullptr, nullptr},
		    /*argumentsSpan*/ argumentsSpan,
		    /*argumentIndex*/ argumentIndex,
		    /*argumentCount*/ argumentCount,
		};
	} else if (isNoSubstitutionTemplateLiteral(node) &&
	           isTaggedTemplateExpression(parent)) {
		// Check if we're actually inside the template;
		// otherwise we'll fall out and return undefined.
		if (isInsideTemplateLiteral(node, position, sourceFile)) {
			return getArgumentListInfoForTemplate(
			    parent->as<TaggedTemplateExpression>(), 0, sourceFile);
		}
		return nullptr;
	} else if (isTemplateHead(node) && parent->parent != nullptr &&
	           parent->parent->kind == Kind::TaggedTemplateExpression) {
		TemplateExpression* templateExpression =
		    parent->as<TemplateExpression>();
		TaggedTemplateExpression* tagExpression =
		    templateExpression->parent->as<TaggedTemplateExpression>();

		int argumentIndex = 1;
		if (isInsideTemplateLiteral(node, position, sourceFile)) {
			argumentIndex = 0;
		}
		return getArgumentListInfoForTemplate(tagExpression, argumentIndex,
		                                      sourceFile);
	} else if (isTemplateSpan(parent) && parent->parent != nullptr &&
	           parent->parent->parent != nullptr &&
	           isTaggedTemplateExpression(parent->parent->parent)) {
		Node* templateSpan = parent;
		Node* tagExpression = parent->parent->parent;

		// If we're just after a template tail, don't show signature help.
		if (isTemplateTail(node) &&
		    !isInsideTemplateLiteral(node, position, sourceFile)) {
			return nullptr;
		}

		int spanIndex = indexOfNode(
		    templateSpan->parent->as<TemplateExpression>()
		        ->TemplateSpans->nodes,
		    templateSpan);
		int argumentIndex = getArgumentIndexForTemplatePiece(
		    spanIndex, node, position, sourceFile);

		return getArgumentListInfoForTemplate(
		    tagExpression->as<TaggedTemplateExpression>(), argumentIndex,
		    sourceFile);
	} else if (isJsxOpeningLikeElement(parent)) {
		// Provide a signature help for JSX opening element or JSX
		// self-closing element.
		// This is not guarantee that JSX tag-name is resolved into stateless
		// function component. (that is done in "getSignatureHelpItems")
		// i.e
		//      export function MainButton(props: ButtonProps, context: any):
		//      JSX.Element { ... }
		//      <MainButton /*signatureHelp*/
		int attributeSpanStart = parent->attributes()->loc.pos();
		int attributeSpanEnd =
		    skipTrivia(sourceFile->Text(), parent->attributes()->end());
		return new argumentListInfo{
		    /*isTypeParameterList*/ false,
		    /*invocation*/ new invocation{
		        new callInvocation{parent}, nullptr, nullptr},
		    /*argumentsSpan*/
		    TextRange{attributeSpanStart,
		              attributeSpanEnd - attributeSpanStart},
		    /*argumentIndex*/ 0,
		    /*argumentCount*/ 1,
		};
	} else {
		PossibleTypeArgumentInfo* typeArgInfo =
		    getPossibleTypeArgumentsInfo(node, sourceFile);
		if (typeArgInfo != nullptr) {
			Node* called = typeArgInfo->called;
			int nTypeArguments = typeArgInfo->nTypeArguments;
			auto* invoc = new typeArgsInvocation{called->as<Identifier>()};
			TextRange argumentRange = {called->loc.pos(), node->end()};
			return new argumentListInfo{
			    /*isTypeParameterList*/ true,
			    /*invocation*/ new invocation{
			        nullptr, invoc, nullptr},
			    /*argumentsSpan*/ argumentRange,
			    /*argumentIndex*/ nTypeArguments,
			    /*argumentCount*/ nTypeArguments + 1,
			};
		}
	}
	return nullptr;
}

// === getArgumentIndexForTemplatePiece — signaturehelp.go:1029 ===
// spanIndex is either the index for a given template span.
// This does not give appropriate results for a NoSubstitutionTemplateLiteral
int getArgumentIndexForTemplatePiece(int spanIndex, Node* node, int position,
                                     SourceFile* sourceFile) {
	// Because the TemplateStringsArray is the first argument, we have to
	// offset each substitution expression by 1.
	// There are three cases we can encounter:
	//      1. We are precisely in the template literal (argIndex = 0).
	//      2. We are in or to the right of the substitution expression
	//         (argIndex = spanIndex + 1).
	//      3. We are directly to the right of the template literal, but
	//         because we look for the token on the left,
	//          not enough to put us in the substitution expression; we should
	//          consider ourselves part of the
	//          *next* span's expression by offsetting the index (argIndex =
	//          (spanIndex + 1) + 1).
	//
	// Example: f  `# abcd $#{#  1 + 1#  }# efghi ${ #"#hello"#  }  #  `
	//              ^       ^ ^       ^   ^          ^ ^      ^     ^
	// Case:        1       1 3       2   1          3 2      2     1
	debug::assert(position >= node->loc.pos());
	if (isTemplateLiteralToken(node)) {
		if (isInsideTemplateLiteral(node, position, sourceFile)) {
			return 0;
		}
		return spanIndex + 2;
	}
	return spanIndex + 1;
}

// === getAdjustedNode — signaturehelp.go:1051 ===
Node* getAdjustedNode(Node* node) {
	switch (node->kind) {
	case Kind::OpenParenToken:
	case Kind::CommaToken:
		return node;
	default:
		return findAncestor(node->parent, [](Node* n) -> bool {
			if (isParameterDeclaration(n)) {
				return true;
			} else if (isBindingElement(n) || isObjectBindingPattern(n) ||
			           isArrayBindingPattern(n)) {
				return false;
			}
			return false;
		});
	}
}

// === getSpreadElementCount — signaturehelp.go:1074 ===
int getSpreadElementCount(Node* node, checker::Checker* c) {
	checker::Type* spreadType =
	    c->GetTypeAtLocation(node->as<SpreadElement>()->Expression);
	if (checker::IsTupleType(spreadType)) {
		checker::TupleType* tupleType = spreadType->Target()->AsTupleType();
		if (tupleType == nullptr) {
			return 0;
		}
		// ElementFlags() — the flags of each element info (types.go:1092).
		std::vector<checker::ElementFlags> elementFlags;
		elementFlags.reserve(tupleType->elementInfos.size());
		for (auto& ei : tupleType->elementInfos) {
			elementFlags.push_back(ei.flags);
		}
		int fixedLength = tupleType->fixedLength;
		if (fixedLength == 0) {
			return 0;
		}

		int firstOptionalIndex = findIndex(
		    elementFlags, [](checker::ElementFlags f) {
			    return (f & checker::ElementFlagsRequired) == 0;
		    });
		if (firstOptionalIndex < 0) {
			return fixedLength;
		}
		return firstOptionalIndex;
	}
	return 0;
}

// === getArgumentIndex / getArgumentCount / getArgumentIndexOrCount —
// signaturehelp.go:1098,1102,1106 ===
int getArgumentIndex(Node* node, NodeList* arguments, SourceFile* sourceFile,
                     checker::Checker* c) {
	return getArgumentIndexOrCount(
	    getTokenFromNodeList(arguments, node->parent, sourceFile), node, c);
}

int getArgumentCount(Node* node, NodeList* arguments, SourceFile* sourceFile,
                     checker::Checker* c) {
	return getArgumentIndexOrCount(
	    getTokenFromNodeList(arguments, node->parent, sourceFile), nullptr, c);
}

int getArgumentIndexOrCount(const std::vector<Node*>& arguments, Node* node,
                            checker::Checker* c) {
	int argumentIndex = 0;
	bool skipComma = false;
	for (Node* arg : arguments) {
		if (node != nullptr && arg == node) {
			if (!skipComma && arg->kind == Kind::CommaToken) {
				argumentIndex++;
			}
			return argumentIndex;
		}
		if (isSpreadElement(arg)) {
			argumentIndex += getSpreadElementCount(arg, c);
			skipComma = true;
			continue;
		}
		if (arg->kind != Kind::CommaToken) {
			argumentIndex++;
			skipComma = true;
			continue;
		}
		if (skipComma) {
			skipComma = false;
			continue;
		}
		argumentIndex++;
	}
	if (node != nullptr) {
		return argumentIndex;
	}
	// The argument count for a list is normally the number of non-comma
	// children it has.
	// For example, if you have "Foo(a,b)" then there will be three children of
	// the arg
	// list 'a' '<comma>' 'b'. So, in this case the arg count will be 2.
	// However, there
	// is a small subtlety. If you have "Foo(a,)", then the child list will
	// just have
	// 'a' '<comma>'. So, in the case where the last child is a comma, we
	// increase the
	// arg count by one to compensate.
	int argumentCount = argumentIndex;
	if (!arguments.empty() &&
	    arguments.back()->kind == Kind::CommaToken) {
		argumentCount = argumentIndex + 1;
	}
	return argumentCount;
}

// === getArgumentOrParameterListInfo — signaturehelp.go:1155 ===
argumentOrParameterListInfo* getArgumentOrParameterListInfo(
    Node* node, SourceFile* sourceFile, checker::Checker* c) {
	argumentOrParameterListAndIndex* info =
	    getArgumentOrParameterListAndIndex(node, sourceFile, c);
	if (info == nullptr) {
		return nullptr;
	}
	NodeList* list = info->list;
	int argumentIndex = info->argumentIndex;
	int argumentCount = getArgumentCount(node, list, sourceFile, c);
	TextRange argumentsSpan =
	    getApplicableSpanForArguments(list, node, sourceFile);
	return new argumentOrParameterListInfo{
	    /*list*/ list,
	    /*argumentIndex*/ argumentIndex,
	    /*argumentCount*/ argumentCount,
	    /*argumentsSpan*/ argumentsSpan,
	};
}

// === getApplicableSpanForArguments — signaturehelp.go:1172 ===
TextRange getApplicableSpanForArguments(NodeList* argumentList, Node* node,
                                        SourceFile* sourceFile) {
	// We use full start and skip trivia on the end because we want to include
	// trivia on
	// both sides. For example,
	//
	//    foo(   /*comment */     a, b, c      /*comment*/     )
	//        |                                               |
	//
	// The applicable span is from the first bar to the second bar (inclusive,
	// but not including parentheses).
	if (argumentList == nullptr && node != nullptr) {
		// If the user has just opened a list, and there are no arguments.
		// For example, foo(    )
		//                  |  |
		// The span should include positions inside the parentheses.
		int spanStart = node->end();
		int spanEnd = skipTrivia(sourceFile->Text(), node->end());
		spanEnd = ensureMinimumSpanSize(spanStart, spanEnd);
		return TextRange{spanStart, spanEnd};
	}
	int applicableSpanStart = argumentList->pos();
	int applicableSpanEnd =
	    skipTrivia(sourceFile->Text(), argumentList->end());

	// If the argument list is empty (Pos == End), extend the span to include
	// at least
	// one position. This handles foo(|) where the cursor is right after the
	// opening paren.
	applicableSpanEnd =
	    ensureMinimumSpanSize(applicableSpanStart, applicableSpanEnd);

	return TextRange{applicableSpanStart, applicableSpanEnd};
}

// === ensureMinimumSpanSize — signaturehelp.go:1204 ===
// Ensures that a span includes at least one position.
// TextRange.Contains uses a half-open interval, so an empty span would not
// contain
// the cursor immediately after typing an opening paren in a call like
// foo(bar(|)).
int ensureMinimumSpanSize(int start, int end) {
	if (end <= start) {
		return start + 1;
	}
	return end;
}

// === getArgumentOrParameterListAndIndex — signaturehelp.go:1216 ===
argumentOrParameterListAndIndex* getArgumentOrParameterListAndIndex(
    Node* node, SourceFile* sourceFile, checker::Checker* c) {
	if (node->kind == Kind::LessThanToken ||
	    node->kind == Kind::OpenParenToken) {
		// Find the list that starts right *after* the < or ( token.
		// If the user has just opened a list, consider this item 0.
		NodeList* list =
		    getChildListThatStartsWithOpenerToken(node->parent, node);
		return new argumentOrParameterListAndIndex{
		    /*list*/ list,
		    /*argumentIndex*/ 0,
		};
	} else {
		// findListItemInfo can return undefined if we are not in parent's
		// argument list
		// or type argument list. This includes cases where the cursor is:
		//   - To the right of the closing parenthesis, non-substitution
		//     template, or template tail.
		//   - Between the type arguments and the arguments (greater than
		//     token)
		//   - On the target of the call (parent.func)
		//   - On the 'new' keyword in a 'new' expression
		NodeList* list = findContainingList(node, sourceFile);
		if (list == nullptr) {
			return nullptr;
		}
		return new argumentOrParameterListAndIndex{
		    /*list*/ list,
		    // Find the index of the argument that contains the node.
		    /*argumentIndex*/ getArgumentIndex(node, list, sourceFile, c),
		};
	}
}

// === getChildListThatStartsWithOpenerToken — signaturehelp.go:1244 ===
NodeList* getChildListThatStartsWithOpenerToken(Node* parent,
                                                Node* openerToken) {
	if (isCallExpression(parent)) {
		CallExpression* parentCallExpression = parent->as<CallExpression>();
		if (openerToken->kind == Kind::LessThanToken) {
			return parentCallExpression->TypeArguments;
		}
		return parentCallExpression->Arguments;
	} else if (isNewExpression(parent)) {
		NewExpression* parentNewExpression = parent->as<NewExpression>();
		if (openerToken->kind == Kind::LessThanToken) {
			return parentNewExpression->TypeArguments;
		}
		return parentNewExpression->Arguments;
	}
	return nullptr;
}

// === tryGetParameterInfo — signaturehelp.go:1261 ===
argumentListInfo* tryGetParameterInfo(Node* startingToken,
                                      SourceFile* sourceFile,
                                      checker::Checker* c) {
	Node* node = getAdjustedNode(startingToken);
	if (node == nullptr) {
		return nullptr;
	}
	contextualSignatureLocationInfo* info =
	    getContextualSignatureLocationInfo(node, sourceFile, c);
	if (info == nullptr) {
		return nullptr;
	}

	// for optional function condition
	checker::Type* nonNullableContextualType =
	    c->GetNonNullableType(info->contextualType);
	if (nonNullableContextualType == nullptr) {
		return nullptr;
	}

	Symbol* symbol = nonNullableContextualType->symbol;
	if (symbol == nullptr) {
		return nullptr;
	}

	auto signatures = c->GetSignaturesOfType(
	    nonNullableContextualType, checker::SignatureKind::Call);
	if (signatures.empty()) {
		return nullptr;
	}
	checker::Signature* signature = signatures.back();

	auto* contextualInvocation_ = new contextualInvocation{
	    /*signature*/ signature,
	    /*node*/ startingToken,
	    /*symbol*/ chooseBetterSymbol(symbol),
	};
	return new argumentListInfo{
	    /*isTypeParameterList*/ false,
	    /*invocation*/ new invocation{
	        nullptr, nullptr, contextualInvocation_},
	    /*argumentsSpan*/ info->argumentsSpan,
	    /*argumentIndex*/ info->argumentIndex,
	    /*argumentCount*/ info->argumentCount,
	};
}

// === chooseBetterSymbol — signaturehelp.go:1302 ===
Symbol* chooseBetterSymbol(Symbol* s) {
	if (s->name == InternalSymbolNameType) {
		for (auto* d : s->declarations) {
			if (isFunctionTypeNode(d) && canHaveSymbol(d->parent)) {
				return d->parent->symbol();
			}
		}
	}
	return s;
}

// === getContextualSignatureLocationInfo — signaturehelp.go:1313 ===
contextualSignatureLocationInfo* getContextualSignatureLocationInfo(
    Node* node, SourceFile* sourceFile, checker::Checker* c) {
	Node* parent = node->parent;
	switch (parent->kind) {
	case Kind::ParenthesizedExpression:
	case Kind::MethodDeclaration:
	case Kind::FunctionExpression:
	case Kind::ArrowFunction: {
		argumentOrParameterListInfo* info =
		    getArgumentOrParameterListInfo(node, sourceFile, c);
		if (info == nullptr) {
			return nullptr;
		}
		int argumentIndex = info->argumentIndex;
		int argumentCount = info->argumentCount;
		TextRange argumentsSpan = info->argumentsSpan;

		checker::Type* contextualType = nullptr;
		if (isMethodDeclaration(parent)) {
			contextualType =
			    c->GetContextualTypeForObjectLiteralElement(
			        parent, checker::ContextFlagsNone);
		} else {
			contextualType =
			    c->GetContextualType(parent, checker::ContextFlagsNone);
		}
		if (contextualType != nullptr) {
			return new contextualSignatureLocationInfo{
			    /*contextualType*/ contextualType,
			    /*argumentIndex*/ argumentIndex,
			    /*argumentCount*/ argumentCount,
			    /*argumentsSpan*/ argumentsSpan,
			};
		}
		return nullptr;
	}
	case Kind::BinaryExpression: {
		BinaryExpression* highestBinary =
		    getHighestBinary(parent->as<BinaryExpression>());
		checker::Type* contextualType = c->GetContextualType(
		    highestBinary->asNode(), checker::ContextFlagsNone);
		int argumentIndex = 0;
		if (node->kind != Kind::OpenParenToken) {
			argumentIndex =
			    countBinaryExpressionParameters(
			        parent->as<BinaryExpression>()) -
			    1;
			int argumentCount =
			    countBinaryExpressionParameters(highestBinary);
			if (contextualType != nullptr) {
				return new contextualSignatureLocationInfo{
				    /*contextualType*/ contextualType,
				    /*argumentIndex*/ argumentIndex,
				    /*argumentCount*/ argumentCount,
				    /*argumentsSpan*/
				    TextRange{parent->pos(), parent->end()},
				};
			}
			return nullptr;
		}
		break;
	}
	}
	return nullptr;
}

// === getHighestBinary — signaturehelp.go:1361 ===
BinaryExpression* getHighestBinary(BinaryExpression* b) {
	if (isBinaryExpression(b->parent)) {
		return getHighestBinary(b->parent->as<BinaryExpression>());
	}
	return b;
}

// === countBinaryExpressionParameters — signaturehelp.go:1368 ===
int countBinaryExpressionParameters(BinaryExpression* b) {
	if (isBinaryExpression(b->Left)) {
		return countBinaryExpressionParameters(b->Left->as<BinaryExpression>()) +
		       1;
	}
	return 2;
}

// === getTokenFromNodeList — signaturehelp.go:1375 ===
std::vector<Node*> getTokenFromNodeList(NodeList* nodeList,
                                      Node* nodeListParent,
                                      SourceFile* sourceFile) {
	if (nodeList == nullptr || nodeListParent == nullptr) {
		return {};
	}
	int left = nodeList->pos();
	size_t nodeListIndex = 0;
	std::vector<Node*> tokens;
	while (left < nodeList->end()) {
		if (nodeList->nodes.size() > nodeListIndex &&
		    left == nodeList->nodes[nodeListIndex]->pos()) {
			tokens.push_back(nodeList->nodes[nodeListIndex]);
			left = nodeList->nodes[nodeListIndex]->end();
			nodeListIndex++;
		} else {
			Scanner scanner;
			getScannerForSourceFile(scanner, sourceFile, left);
			Kind token = scanner.token();
			int tokenFullStart = scanner.tokenFullStart();
			int tokenEnd = scanner.tokenEnd();
			tokens.push_back(getOrCreateToken(
			    sourceFile, token, tokenFullStart, tokenEnd, nodeListParent,
			    scanner.tokenFlags()));
			left = tokenEnd;
		}
	}
	return tokens;
}

// === getArgumentListInfoForTemplate — signaturehelp.go:1399 ===
argumentListInfo* getArgumentListInfoForTemplate(Node* tagExpression,
                                                 int argumentIndex,
                                                 SourceFile* sourceFile) {
	// argumentCount is either 1 or (numSpans + 1) to account for the
	// template strings array argument.
	int argumentCount = 1;
	TaggedTemplateExpression* tagged =
	    tagExpression->as<TaggedTemplateExpression>();
	if (!isNoSubstitutionTemplateLiteral(tagged->Template)) {
		argumentCount = static_cast<int>(
		    tagged->Template->as<TemplateExpression>()
		        ->TemplateSpans->nodes.size()) +
		    1;
	}
	if (argumentIndex != 0) {
		debug::assert(argumentIndex < argumentCount);
	}
	return new argumentListInfo{
	    /*isTypeParameterList*/ false,
	    /*invocation*/ new invocation{
	        new callInvocation{tagged->asNode()}, nullptr, nullptr},
	    /*argumentsSpan*/
	    getApplicableRangeForTaggedTemplate(tagged, sourceFile),
	    /*argumentIndex*/ argumentIndex,
	    /*argumentCount*/ argumentCount,
	};
}

// === getApplicableRangeForTaggedTemplate — signaturehelp.go:1417 ===
TextRange getApplicableRangeForTaggedTemplate(Node* taggedTemplate,
                                              SourceFile* sourceFile) {
	Node* template_ = taggedTemplate->as<TaggedTemplateExpression>()->Template;
	int applicableSpanStart =
	    getTokenPosOfNode(template_, sourceFile, false);
	int applicableSpanEnd = template_->end();

	// We need to adjust the end position for the case where the template
	// does not have a tail.
	// Otherwise, we will not show signature help past the expression.
	// For example,
	//
	//      ` ${ 1 + 1 foo(10)
	//       |       |
	// This is because a Missing node has no width. However, what we actually
	// want is to include trivia
	// leading up to the next token in case the user is about to type in a
	// TemplateMiddle or TemplateTail.
	if (template_->kind == Kind::TemplateExpression) {
		NodeList* templateSpans =
		    template_->as<TemplateExpression>()->TemplateSpans;
		Node* lastSpan = templateSpans->nodes.back();
		if (lastSpan->as<TemplateSpan>()->Literal->end() -
		        lastSpan->as<TemplateSpan>()->Literal->pos() ==
		    0) {
			applicableSpanEnd =
			    skipTrivia(sourceFile->Text(), applicableSpanEnd);
		}
	}

	return TextRange{applicableSpanStart,
	                 applicableSpanEnd - applicableSpanStart};
}

// === dep stubs — removed when owner slice lands ===

// utilities.go — ls-coreA
bool IsInString(SourceFile* sourceFile, int position, Node* previousToken) {
	TSC_UNREACHABLE("IsInString — owned by ls-coreA");
}
CommentRange* isInComment(SourceFile* file, int position,
                          Node* tokenAtPosition) {
	TSC_UNREACHABLE("isInComment — owned by ls-coreA");
}
std::vector<Node*> getChildrenFromNonJSDocNode(Node* node,
                                             SourceFile* sourceFile) {
	TSC_UNREACHABLE("getChildrenFromNonJSDocNode — owned by ls-coreA");
}
PossibleTypeArgumentInfo* getPossibleTypeArgumentsInfo(
    Node* tokenIn, SourceFile* sourceFile) {
	TSC_UNREACHABLE("getPossibleTypeArgumentsInfo — owned by ls-coreA");
}
std::vector<checker::Signature*> getPossibleGenericSignatures(
    Node* called, int typeArgumentCount, checker::Checker* c) {
	TSC_UNREACHABLE("getPossibleGenericSignatures — owned by ls-coreA");
}
bool isNoSubstitutionTemplateLiteral(Node* node) {
	TSC_UNREACHABLE("isNoSubstitutionTemplateLiteral — owned by ls-coreA");
}
bool isTaggedTemplateExpression(Node* node) {
	TSC_UNREACHABLE("isTaggedTemplateExpression — owned by ls-coreA");
}
bool isInsideTemplateLiteral(Node* node, int position,
                             SourceFile* sourceFile) {
	TSC_UNREACHABLE("isInsideTemplateLiteral — owned by ls-coreA");
}
bool isTemplateHead(Node* node) {
	TSC_UNREACHABLE("isTemplateHead — owned by ls-coreA");
}
bool isTemplateTail(Node* node) {
	TSC_UNREACHABLE("isTemplateTail — owned by ls-coreA");
}
NodeList* findContainingList(Node* node, SourceFile* file) {
	TSC_UNREACHABLE("findContainingList — owned by ls-coreA");
}
// hover.go — ls-coreC
std::string getDocumentationFromDeclaration(
    documentationLocationMapper getMappedLocation, checker::Checker* c,
    Symbol* symbol, Node* declaration, Node* location,
    lsproto::MarkupKind contentFormat, bool commentOnly) {
	TSC_UNREACHABLE("getDocumentationFromDeclaration — owned by ls-coreC");
}
documentationLocationMapper LanguageService::documentationLocationMapper(
    spanmap::Feature feature) {
	TSC_UNREACHABLE(
	    "LanguageService::documentationLocationMapper — owned by ls-coreC");
}

} // namespace tsc::ls
