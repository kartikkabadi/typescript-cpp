// Port of tsc/internal/checker/nodecopy.go — the AST node-reuse machinery used
// by the node builder (NodeBuilderImpl): reuseNode/reuseName/reuseTypeNode,
// the recoveryBoundary / wrappingTracker machinery, and
// getExistingNodeTreeVisitor. Also ports SymbolTrackerImpl from
// symboltracker.go, the small free helpers the file depends on, and declares
// stubs for dependencies owned by not-yet-ported slices.

#include "internal/checker/checker.h"
#include "internal/checker/mapper.h"

#include "internal/jsnum/jsnum.h"
#include "internal/scanner/scanner.h"

#include <string>
#include <tuple>
#include <vector>

namespace tsc::checker {

// --- checkNotCanceled — utilities.go:1716 ----------------------------------

void Checker::checkNotCanceled() {
	if (wasCanceled) {
		TSC_UNREACHABLE("Checker was previously cancelled");
	}
}

// --- getSignatureFromDeclaration — ported with the signatures slice ---------

// --- IsSymbolAccessible — ported with the symbolaccessibility slice ---------

printer::SymbolAccessibilityResult Checker::isSymbolAccessible(
	Symbol* symbol, Node* enclosingDeclaration, SymbolFlags meaning,
	bool shouldComputeAliasesToMakeVisible) {
	TSC_UNREACHABLE("isSymbolAccessible — ported with the symbolaccessibility slice");
}

// --- IsExternalModuleSymbol — utilities.go:1708 -----------------------------

bool isExternalModuleSymbol(Symbol* moduleSymbol) {
	return moduleSymbol->isExternalModule();
}

// --- classifyPropertyName — nodebuilderimpl.go:2456 -------------------------

propertyNameNodeKind classifyPropertyName(const std::string& name,
                                        bool stringNamed, bool isMethod) {
	if (isMethod && name == "new") {
		return propertyNameNodeKind::StringLiteral;
	}
	if (isIdentifierText(name, LanguageVariant::Standard)) {
		return propertyNameNodeKind::Identifier;
	}
	return (!stringNamed && isNumericLiteralName(name) &&
	        numberFromString(name).v >= 0)
	           ? propertyNameNodeKind::NumericLiteral
	           : propertyNameNodeKind::StringLiteral;
}

// --- getMeaningOfEntityNameReference — emitresolver.go:311 ------------------

SymbolFlags getMeaningOfEntityNameReference(Node* entityName) {
	// get symbol of the first identifier of the entityName
	if (entityName->parent->kind == Kind::TypeQuery ||
		(entityName->parent->kind == Kind::ExpressionWithTypeArguments &&
		 !isPartOfTypeNode(entityName->parent)) ||
		entityName->parent->kind == Kind::ComputedPropertyName ||
		(entityName->parent->kind == Kind::TypePredicate &&
		 entityName->parent->as<TypePredicateNode>()->ParameterName ==
			 entityName) ||
		entityName->parent->kind == Kind::BinaryExpression) {
		// Typeof value
		return SymbolFlagsValue | SymbolFlagsExportValue;
	}
	if (entityName->kind == Kind::QualifiedName ||
		entityName->kind == Kind::PropertyAccessExpression ||
		entityName->parent->kind == Kind::ImportEqualsDeclaration ||
		(entityName->parent->kind == Kind::QualifiedName &&
		 entityName->parent->as<QualifiedName>()->Left == entityName) ||
		(entityName->parent->kind == Kind::PropertyAccessExpression &&
		 entityName->parent->expression() == entityName) ||
		(entityName->parent->kind == Kind::ElementAccessExpression &&
		 entityName->parent->expression() == entityName)) {
		// Left identifier from type reference or TypeAlias
		// Entity name of the import declaration
		return SymbolFlagsNamespace;
	}
	// Type Reference or TypeAlias entity = Identifier
	return SymbolFlagsType;
}

// --- setTextRange — nodebuilderimpl.go:1434 ---------------------------------

Node* NodeBuilderImpl::setTextRange(Node* range_, Node* location) {
	if (range_ == nullptr) {
		return range_;
	}
	if (!nodeIsSynthesized(range_) ||
		!(range_->flags & NodeFlagsSynthesized) ||
		ctx->enclosingFile == nullptr ||
		ctx->enclosingFile != getSourceFileOfNode(e->mostOriginal(range_))) {
		Node* original = range_;
		range_ = range_->clone(
			*f); // if `range` is synthesized or originates in another file, copy it so it definitely has synthetic positions
		range_->loc = TextRange{-1, -1};
		if (auto it = idToSymbol.find(original); it != idToSymbol.end()) {
			idToSymbol[range_] = it->second;
		}
	}
	if (range_ == location || location == nullptr) {
		return range_;
	}
	// Don't overwrite the original node if `range` has an `original` node that
	// points either directly or indirectly to `location`
	Node* original = e->original(range_);
	while (original != nullptr && original != location) {
		original = e->original(original);
	}
	if (original == nullptr) {
		e->setOriginalEx(range_, location, true);
	}

	// only set positions if range comes from the same file since copying text
	// across files isn't supported by the emitter
	if (ctx->enclosingFile != nullptr &&
		ctx->enclosingFile == getSourceFileOfNode(e->mostOriginal(location))) {
		range_->loc = location->loc;
		return range_;
	} else {
		range_->loc = TextRange{-1, -1};
	}
	return range_;
}

// --- newIdentifier — nodebuilderimpl.go:3639 --------------------------------

Node* NodeBuilderImpl::newIdentifier(const std::string& text, Symbol* symbol) {
	Node* id = f->newIdentifier(text);
	if (symbol != nullptr) {
		idToSymbol[id] = symbol;
	}
	return id;
}

// --- Former dep stubs now ported for real in checker_nodebuilder.cpp -------
// getTypeFromTypeNode, typeToTypeNode, checkTypeExpandability, enterNewScope,
// serializeTypeName, canReuseExistingJSTypeNode,
// tryGetResolvedSymbolFromTypeNode, lookupSymbolChain,
// getSpecifierForModuleSymbol, typeParameterToName — dedup'd here to avoid
// duplicate definitions.

// --- reuseNode — nodecopy.go:11 ---------------------------------------------

Node* NodeBuilderImpl::reuseNode(Node* node) {
	if (node == nullptr) {
		return node;
	}

	return tryReuseExistingNodeHelper(node);
}

// --- tryJSTypeNodeToTypeNode — nodecopy.go:20 -------------------------------

Node* NodeBuilderImpl::tryJSTypeNodeToTypeNode(Node* node) {
	return reuseNode(node);
}

// --- reuseName — nodecopy.go:24 ---------------------------------------------

Node* NodeBuilderImpl::reuseName(Node* node, bool isMethod) {
	Node* res = reuseNode(node);
	if (res == nullptr) {
		return res;
	}

	std::string text;
	if (!tryGetTextOfPropertyName(res, text)) {
		return res;
	}

	propertyNameNodeKind kind =
		classifyPropertyName(text, isStringLiteral(res), isMethod);
	if (isIdentifier(res) && kind == propertyNameNodeKind::Identifier) {
		return res;
	}
	if (isStringLiteral(res) && kind == propertyNameNodeKind::StringLiteral) {
		return res;
	}

	Node* renamed = nullptr;
	switch (kind) {
	case propertyNameNodeKind::Identifier:
		renamed = newIdentifier(text, nullptr);
		break;
	case propertyNameNodeKind::StringLiteral:
		renamed = f->newStringLiteral(text, TokenFlagsNone);
		break;
	default:
		return res;
	}
	e->setOriginal(renamed, res);
	return setTextRange(renamed, res);
}

// --- reuseTypeNode — nodecopy.go:55 -----------------------------------------

Node* NodeBuilderImpl::reuseTypeNode(Node* node) {
	if (node == nullptr) {
		return node;
	}
	Node* r = reuseNode(node);
	if (r != nullptr) {
		// After successful reuse during hover, probe the reused AST for expandable
		// type references so canIncreaseExpansionDepth is set even though
		// typeToTypeNode (and shouldExpandType) were never called.
		if (ctx->maxExpansionDepth >= 0 && !ctx->canIncreaseExpansionDepth) {
			walkNodeForExpandability(node);
		}
		return r;
	}
	ctx->tracker->ReportInferenceFallback(node);
	Type* t = getTypeFromTypeNode(node, false);
	return typeToTypeNode(t);
}

// walkNodeForExpandability walks a reused AST node tree, calling
// checkTypeExpandability on each type reference, type predicate, or import
// type node. Short-circuits once canIncreaseExpansionDepth is set.
// — nodecopy.go:75
void NodeBuilderImpl::walkNodeForExpandability(Node* node) {
	if (ctx->canIncreaseExpansionDepth || node == nullptr) {
		return;
	}
	// Check these explicitly so we look into type arguments wehther or not
	// they are in the tree or not.
	if (isTypeReferenceNode(node) || isExpressionWithTypeArguments(node) ||
		isTypePredicateNode(node) || isImportTypeNode(node)) {
		Type* t = getTypeFromTypeNode(node, false);
		if (t != nullptr) {
			checkTypeExpandability(t);
			if (ctx->canIncreaseExpansionDepth) {
				return;
			}
		}
	}
	node->forEachChild([this](Node* child) -> bool {
		walkNodeForExpandability(child);
		return ctx->canIncreaseExpansionDepth;
	});
}

// --- recoveryBoundary — nodecopy.go:96 --------------------------------------

void recoveryBoundary::markError(std::function<void()> f) {
	hadError = true;
	if (f) {
		deferredReports.push_back(std::move(f));
	}
}

// --- startRecoveryScope — nodecopy.go:115 -----------------------------------

originalRecoveryScopeState recoveryBoundary::startRecoveryScope() {
	int trackedSymbolsTop = static_cast<int>(ctx->trackedSymbols.size());
	int unreportedErrorsTop = static_cast<int>(deferredReports.size());
	return originalRecoveryScopeState{trackedSymbolsTop, unreportedErrorsTop,
	                                  hadError};
}

// --- endRecoveryScope — nodecopy.go:121 -------------------------------------

void recoveryBoundary::endRecoveryScope(originalRecoveryScopeState state) {
	hadError = state.hadError;
	ctx->trackedSymbols.resize(state.trackedSymbolsTop);
	deferredReports.resize(state.unreportedErrorsTop);
}

// --- wrappingTracker — nodecopy.go:132 --------------------------------------

void wrappingTracker::PopErrorFallbackNode() {
	wrapped->PopErrorFallbackNode();
}

void wrappingTracker::PushErrorFallbackNode(Node* node) {
	wrapped->PushErrorFallbackNode(node);
}

void wrappingTracker::ReportCyclicStructureError() {
	bound->markError([this]() { wrapped->ReportCyclicStructureError(); });
}

void wrappingTracker::ReportInaccessibleThisError() {
	bound->markError([this]() { wrapped->ReportInaccessibleThisError(); });
}

void wrappingTracker::ReportInaccessibleUniqueSymbolError() {
	bound->markError([this]() { wrapped->ReportInaccessibleUniqueSymbolError(); });
}

void wrappingTracker::ReportInferenceFallback(Node* node) {
	wrapped->ReportInferenceFallback(node); // Should this also be deferred?
}

void wrappingTracker::ReportLikelyUnsafeImportRequiredError(
	const std::string& specifier, const std::string& symbolName) {
	bound->markError([this, specifier, symbolName]() {
		wrapped->ReportLikelyUnsafeImportRequiredError(specifier, symbolName);
	});
}

void wrappingTracker::ReportNonSerializableProperty(
	const std::string& propertyName) {
	bound->markError([this, propertyName]() {
		wrapped->ReportNonSerializableProperty(propertyName);
	});
}

void wrappingTracker::ReportNonlocalAugmentation(SourceFile* containingFile,
                                                 Symbol* parentSymbol,
                                                 Symbol* augmentingSymbol) {
	wrapped->ReportNonlocalAugmentation(containingFile, parentSymbol,
	                                    augmentingSymbol); // Should this also be deferred?
}

void wrappingTracker::ReportPrivateInBaseOfClassExpression(
	const std::string& propertyName) {
	bound->markError([this, propertyName]() {
		wrapped->ReportPrivateInBaseOfClassExpression(propertyName);
	});
}

void wrappingTracker::ReportTruncationError() {
	wrapped->ReportTruncationError(); // Should this also be deferred?
}

bool wrappingTracker::TrackSymbol(Symbol* symbol, Node* enclosingDeclaration,
                                  SymbolFlags meaning) {
	bound->trackedSymbols.push_back(
		new TrackedSymbolArgs{symbol, enclosingDeclaration, meaning});
	return false;
}

// --- newWrappingTracker — nodecopy.go:196 -----------------------------------

wrappingTracker* newWrappingTracker(nodebuilder::SymbolTracker* inner,
                                    recoveryBoundary* bound) {
	auto* w = new wrappingTracker();
	w->wrapped = inner;
	w->bound = bound;
	return w;
}

// --- SymbolTrackerImpl — symboltracker.go:9 ---------------------------------

SymbolTrackerImpl* newSymbolTrackerImpl(NodeBuilderContext* context,
                                        nodebuilder::SymbolTracker* tracker) {
	if (tracker != nullptr) {
		for (;;) {
			auto* t = dynamic_cast<SymbolTrackerImpl*>(tracker);
			if (t == nullptr) {
				break;
			}
			tracker = t->inner;
		}
	}

	auto* impl = new SymbolTrackerImpl();
	impl->context = context;
	impl->inner = tracker;
	impl->DisableTrackSymbol = false;
	return impl;
}

bool SymbolTrackerImpl::TrackSymbol(Symbol* symbol, Node* enclosingDeclaration,
                                    SymbolFlags meaning) {
	if (!DisableTrackSymbol) {
		if (inner != nullptr &&
			inner->TrackSymbol(symbol, enclosingDeclaration, meaning)) {
			onDiagnosticReported();
			return true;
		}
		// Skip recording type parameters as they dont contribute to late painted statements
		if (!(symbol->flags & SymbolFlagsTypeParameter)) {
			context->trackedSymbols.push_back(
				new TrackedSymbolArgs{symbol, enclosingDeclaration, meaning});
		}
	}
	return false;
}

void SymbolTrackerImpl::ReportInaccessibleThisError() {
	onDiagnosticReported();
	if (inner == nullptr) {
		return;
	}
	inner->ReportInaccessibleThisError();
}

void SymbolTrackerImpl::ReportPrivateInBaseOfClassExpression(
	const std::string& propertyName) {
	onDiagnosticReported();
	if (inner == nullptr) {
		return;
	}
	inner->ReportPrivateInBaseOfClassExpression(propertyName);
}

void SymbolTrackerImpl::ReportInaccessibleUniqueSymbolError() {
	onDiagnosticReported();
	if (inner == nullptr) {
		return;
	}
	inner->ReportInaccessibleUniqueSymbolError();
}

void SymbolTrackerImpl::ReportCyclicStructureError() {
	onDiagnosticReported();
	if (inner == nullptr) {
		return;
	}
	inner->ReportCyclicStructureError();
}

void SymbolTrackerImpl::ReportLikelyUnsafeImportRequiredError(
	const std::string& specifier, const std::string& symbolName) {
	onDiagnosticReported();
	if (inner == nullptr) {
		return;
	}
	inner->ReportLikelyUnsafeImportRequiredError(specifier, symbolName);
}

void SymbolTrackerImpl::ReportTruncationError() {
	onDiagnosticReported();
	if (inner == nullptr) {
		return;
	}
	inner->ReportTruncationError();
}

void SymbolTrackerImpl::ReportNonlocalAugmentation(SourceFile* containingFile,
                                                   Symbol* parentSymbol,
                                                   Symbol* augmentingSymbol) {
	onDiagnosticReported();
	if (inner == nullptr) {
		return;
	}
	inner->ReportNonlocalAugmentation(containingFile, parentSymbol,
	                                  augmentingSymbol);
}

void SymbolTrackerImpl::ReportNonSerializableProperty(
	const std::string& propertyName) {
	onDiagnosticReported();
	if (inner == nullptr) {
		return;
	}
	inner->ReportNonSerializableProperty(propertyName);
}

void SymbolTrackerImpl::ReportInferenceFallback(Node* node) {
	if (inner == nullptr) {
		return;
	}
	inner->ReportInferenceFallback(node);
}

void SymbolTrackerImpl::PushErrorFallbackNode(Node* node) {
	if (inner == nullptr) {
		return;
	}
	inner->PushErrorFallbackNode(node);
}

void SymbolTrackerImpl::PopErrorFallbackNode() {
	if (inner == nullptr) {
		return;
	}
	inner->PopErrorFallbackNode();
}

// --- createRecoveryBoundary — nodecopy.go:203 --------------------------------

recoveryBoundary* NodeBuilderImpl::createRecoveryBoundary() {
	ch->checkNotCanceled();
	auto* bound = new recoveryBoundary();
	bound->ctx = ctx;
	bound->oldTracker = ctx->tracker;
	bound->oldTrackedSymbols = ctx->trackedSymbols;
	bound->oldEncounteredError = ctx->encounteredError;
	bound->oldApproximateLength = ctx->approximateLength;
	nodebuilder::SymbolTracker* newTracker =
		newSymbolTrackerImpl(ctx, newWrappingTracker(ctx->tracker, bound));
	ctx->tracker = newTracker;
	ctx->trackedSymbols.clear();
	return bound;
}

// --- finalizeBoundary — nodecopy.go:212 --------------------------------------

bool NodeBuilderImpl::finalizeBoundary(recoveryBoundary* bound) {
	ctx->tracker = bound->oldTracker;
	ctx->trackedSymbols = bound->oldTrackedSymbols;
	ctx->encounteredError = bound->oldEncounteredError;
	ctx->approximateLength = bound->oldApproximateLength;

	for (auto& f : bound->deferredReports) {
		f();
	}
	if (bound->hadError) {
		return false;
	}
	for (auto* a : bound->trackedSymbols) {
		ctx->tracker->TrackSymbol(a->symbol, a->enclosingDeclaration, a->meaning);
	}
	return true;
}

// --- tryReuseExistingNodeHelper — nodecopy.go:229 ----------------------------

Node* NodeBuilderImpl::tryReuseExistingNodeHelper(Node* existing) {
	recoveryBoundary* bound = createRecoveryBoundary();
	Node* transformed = nullptr;
	NodeVisitor* v = getExistingNodeTreeVisitor(this,
	                                            bound); // !!! TODO: Cache visitor and just reset bound+host builder? We try this for a *lot* of nodes.
	transformed = v->visitNode(existing);
	if (!finalizeBoundary(bound)) {
		return nullptr;
	}
	ctx->approximateLength += existing->loc.end() - existing->loc.pos();
	return transformed;
}

// --- getModuleSpecifierOverride — nodecopy.go:241 ----------------------------

std::string NodeBuilderImpl::getModuleSpecifierOverride(Node* parent,
                                                      Node* lit) {
	if (ctx->enclosingFile != getSourceFileOfNode(lit)) {
		ResolutionMode mode = ResolutionModeNone;
		if (parent->as<ImportTypeNode>()->Attributes != nullptr) {
			mode = getResolutionModeOverride(
					   parent->as<ImportTypeNode>()->Attributes->as<ImportAttributes>(),
					   nullptr)
					   .first;
		}
		std::string name = lit->text();
		std::string originalName = name;
		Symbol* nodeSymbol = tryGetResolvedSymbolFromTypeNode(parent);
		SymbolFlags meaning = SymbolFlagsType;
		if (parent->as<ImportTypeNode>()->IsTypeOf) {
			meaning = SymbolFlagsValue;
		}
		Symbol* parentSymbol = nullptr;
		if (nodeSymbol != nullptr &&
			ch->isSymbolAccessible(nodeSymbol, ctx->enclosingDeclaration,
			                       meaning, false)
					.Accessibility == printer::SymbolAccessibility::Accessible) {
			parentSymbol = lookupSymbolChain(nodeSymbol, meaning, true)[0];
		}
		if (parentSymbol != nullptr && isExternalModuleSymbol(parentSymbol)) {
			name = getSpecifierForModuleSymbol(parentSymbol, mode).specifier;
		} else {
			SourceFile* targetFile =
				ch->getExternalModuleFileFromDeclaration(parent);
			if (targetFile != nullptr) {
				name = getSpecifierForModuleSymbol(targetFile->Symbol, mode)
						   .specifier;
			}
		}
		if (!name.empty() && name.find("/node_modules/") != std::string::npos) {
			ctx->encounteredError = true;
			ctx->tracker->ReportLikelyUnsafeImportRequiredError(name, "");
		}
		if (name != originalName) {
			return name;
		}
	}
	return "";
}

// --- rewriteModuleSpecifier — nodecopy.go:272 --------------------------------

Node* NodeBuilderImpl::rewriteModuleSpecifier(Node* parent, Node* lit) {
	std::string newName = getModuleSpecifierOverride(parent, lit);
	if (newName.empty()) {
		return lit;
	}
	Node* res = f->newStringLiteral(newName, TokenFlagsNone);
	e->setOriginal(res, lit);
	return res;
}

// --- getEnclosingDeclarationIgnoringFakeScope — nodecopy.go:282 --------------

Node* NodeBuilderImpl::getEnclosingDeclarationIgnoringFakeScope() {
	Node* enc = ctx->enclosingDeclaration;
	while (enc != nullptr &&
		   links.Get(enc)->fakeScopeForSignatureDeclaration) {
		enc = enc->parent;
	}
	return enc;
}

// --- getExistingNodeTreeVisitor — nodecopy.go:290 ----------------------------

// !!! TODO: wrap all these closures into methods on an object so we can
// guarantee we reuse the same memory on each invocation by reusing/resetting
// the object instead of re-closing-over all of these each time we need a
// visitor. In theory the compiler could handle this, but in practice closure
// inlining hasn't been reliable
NodeVisitor* getExistingNodeTreeVisitor(NodeBuilderImpl* b,
                                      recoveryBoundary* bound) {
	NodeVisitor* visitor = nullptr;
	// note: also handles renaming type parameters renamed within the current context
	auto attachSymbolToLeftmostIdentifier =
		[&](Node* leftmost, Node* node, Symbol* sym) -> Node* {
		NodeVisitor* vis = nullptr;
		std::function<Node*(Node*)> visitorFunc = [&](Node* node) -> Node* {
			if (node == leftmost) {
				Type* type_ = nullptr;
				Node* name = nullptr;
				if (sym != nullptr) {
					type_ = b->ch->getDeclaredTypeOfSymbol(sym);
					if (sym->flags & SymbolFlagsTypeParameter) {
						name = b->typeParameterToName(type_)->asNode();
					}
				}
				if (name == nullptr) {
					name = b->newIdentifier(node->text(), sym);
				}
				name = b->setTextRange(name, node);
				b->e->addEmitFlags(name, printer::EFNoAsciiEscaping);
				return name;
			}
			return b->setTextRange(node->visitEachChild(*vis), node);
		};
		vis = newNodeVisitor(visitorFunc, b->f, NodeVisitorHooks{});
		return visitorFunc(node);
	};
	auto trackExistingEntityName =
		[&](Node* node, Node* overrideEnclosing)
		-> std::tuple<bool, Node*, Symbol*> {
		Node* enclosingDeclaration = b->ctx->enclosingDeclaration;
		if (overrideEnclosing != nullptr) {
			enclosingDeclaration = overrideEnclosing;
		}
		bool introducesError = false;
		Node* leftmost = getFirstIdentifier(node);
		if (isInJSFile(node) &&
			(isExportsIdentifier(leftmost) ||
			 isModuleExportsAccessExpression(leftmost->parent) ||
			 (isQualifiedName(leftmost->parent) &&
			  isModuleIdentifier(leftmost->parent->as<QualifiedName>()->Left) &&
			  isExportsIdentifier(
				  leftmost->parent->as<QualifiedName>()->Right)))) {
			introducesError = true;
			return {introducesError,
			        b->setTextRange(deepCloneNode(*b->f, node), node), nullptr};
		}
		SymbolFlags meaning = getMeaningOfEntityNameReference(node);
		Symbol* sym = nullptr;
		if (isThisIdentifier(leftmost)) {
			// `this` isn't a bindable identifier - skip resolution, find a relevant `this` symbol directly and avoid exhaustive scope traversal
			sym = b->ch->getSymbolOfDeclaration(
				b->ch->getThisContainer(leftmost, false, false));
			if (b->ch->isSymbolAccessible(sym, leftmost, meaning, false)
					.Accessibility != printer::SymbolAccessibility::Accessible) {
				introducesError = true;
				b->ctx->tracker->ReportInaccessibleThisError();
			}
			return {introducesError,
			        attachSymbolToLeftmostIdentifier(leftmost, node, sym),
			        nullptr};
		}
		sym = b->ch->resolveEntityName(leftmost, meaning, true, true, nullptr);
		if (b->ctx->enclosingDeclaration != nullptr &&
			!(sym != nullptr && (sym->flags & SymbolFlagsTypeParameter) != 0)) {
			sym = b->ch->getExportSymbolOfValueSymbolIfExported(sym);
			// Some declarations may be transplanted to a new location.
			// When this happens we need to make sure that the name has the same meaning at both locations
			// We also check for the unknownSymbol because when we create a fake scope some parameters may actually not be usable
			// either because they are the expanded rest parameter,
			// or because they are the newly added parameters from the tuple, which might have different meanings in the original context
			Symbol* symAtLocation = b->ch->resolveEntityName(
				leftmost, meaning, true, true, b->ctx->enclosingDeclaration);
			if (
				// Check for unusable parameters symbols
				symAtLocation == b->ch->unknownSymbol ||
				// If the symbol is not found, but was not found in the original scope either we probably have an error, don't reuse the node
				(symAtLocation == nullptr && sym != nullptr) ||
				// If the symbol is found both in declaration scope and in current scope then it should point to the same reference
				(symAtLocation != nullptr && sym != nullptr &&
				 b->ch->getSymbolIfSameReference(
					 b->ch->getExportSymbolOfValueSymbolIfExported(
						 symAtLocation),
					 sym) == nullptr)) {
				// In isolated declaration we will not do rest parameter expansion so there is no need to report on these.
				if (symAtLocation != b->ch->unknownSymbol) {
					b->ctx->tracker->ReportInferenceFallback(node);
				}
				introducesError = true;
				return {introducesError,
				        b->setTextRange(deepCloneNode(*b->f, node), node), sym};
			} else {
				sym = symAtLocation;
			}
		}

		if (sym != nullptr) {
			// If a parameter is resolvable in the current context it is also visible, so no need to go to symbol accesibility
			if ((sym->flags & SymbolFlagsFunctionScopedVariable) != 0 &&
				sym->valueDeclaration != nullptr) {
				if (isPartOfParameterDeclaration(sym->valueDeclaration) ||
					isJSDocParameterTag(sym->valueDeclaration)) {
					return {introducesError,
					        attachSymbolToLeftmostIdentifier(leftmost, node,
					                                         sym),
					        nullptr};
				}
			}
			if ((sym->flags & SymbolFlagsTypeParameter) ==
					0 /* Type parameters are visible in the current context if they are are resolvable */ &&
				!isDeclarationName(node) &&
				b->ch->isSymbolAccessible(sym, enclosingDeclaration, meaning,
				                          false)
						.Accessibility !=
					printer::SymbolAccessibility::Accessible) {
				b->ctx->tracker->ReportInferenceFallback(node);
				introducesError = true;
			} else {
				b->ctx->tracker->TrackSymbol(sym, enclosingDeclaration,
				                             meaning);
			}
			return {introducesError,
			        attachSymbolToLeftmostIdentifier(leftmost, node, sym),
			        nullptr};
		}
		return {introducesError,
		        b->setTextRange(deepCloneNode(*b->f, node), node), nullptr};
	};
	std::function<Node*(Node*)> tryVisitSimpleTypeNode;
	auto tryVisitIndexedAccess = [&](Node* node) -> Node* {
		Node* resultObjectType = tryVisitSimpleTypeNode(
			node->as<IndexedAccessTypeNode>()->ObjectType);
		if (resultObjectType == nullptr) {
			return nullptr;
		}
		return b->setTextRange(
			b->f->updateIndexedAccessTypeNode(
				node->as<IndexedAccessTypeNode>(), resultObjectType,
				visitor->visitNode(
					node->as<IndexedAccessTypeNode>()->IndexType)),
			node);
	};
	auto tryVisitKeyOf = [&](Node* node) -> Node* {
		TypeOperatorNode* to = node->as<TypeOperatorNode>();
		Node* t = tryVisitSimpleTypeNode(to->Type);
		if (t == nullptr) {
			return nullptr;
		}
		return b->setTextRange(
			b->f->updateTypeOperatorNode(to, to->Operator, t), node);
	};
	auto tryVisitTypeQuery = [&](Node* node) -> Node* {
		auto [introducesError, exprName, _1] =
			trackExistingEntityName(node->as<TypeQueryNode>()->ExprName,
			                        nullptr);
		if (!introducesError) {
			return b->setTextRange(
				b->f->updateTypeQueryNode(
					node->as<TypeQueryNode>(), exprName,
					visitor->visitNodes(
						node->as<TypeQueryNode>()->TypeArguments)),
				node);
		}

		Node* serializedName = b->serializeTypeName(
			node->as<TypeQueryNode>()->ExprName, true,
			visitor->visitNodes(node->as<TypeQueryNode>()->TypeArguments));
		if (serializedName != nullptr) {
			return b->setTextRange(serializedName,
			                       node->as<TypeQueryNode>()->ExprName);
		}
		return nullptr;
	};
	auto tryVisitTypeReference = [&](Node* node) -> Node* {
		if (isConstTypeReference(node)) {
			return nullptr;
		}
		Symbol* s = b->tryGetResolvedSymbolFromTypeNode(node);
		if (s == nullptr) {
			return nullptr; // ???
		}
		if ((s->flags & SymbolFlagsTypeParameter) != 0) {
			Type* declaredType = b->ch->getDeclaredTypeOfSymbol(s);
			if (b->ctx->mapper != nullptr &&
				getMappedType(declaredType, b->ctx->mapper) != declaredType) {
				return nullptr; // refers to type parameter remapped by context (TODO improvement: just return the remapped param name?)
			}
		}
		if (!b->canReuseExistingJSTypeNode(
				node, b->getTypeFromTypeNode(node, false))) {
			// fallback to serialization for jsdoc types that have insufficient or incomplete type args, or are remapped by the checker in only jsdoc contexts
			// TODO: remappings like `promise` -> `Promise<any>` are static, we *could* statically remap the nodes, too. But that only matters for `isolatedDeclarations`
			// in JS, should we enable that.
			return nullptr;
		}
		auto [introducesError, newName, _2] = trackExistingEntityName(
			node->as<TypeReferenceNode>()->TypeName, nullptr);
		if (!introducesError) {
			NodeList* typeArguments = visitor->visitNodes(
				node->as<TypeReferenceNode>()->TypeArguments);
			return b->setTextRange(
				b->f->updateTypeReferenceNode(node->as<TypeReferenceNode>(),
			                                  newName, typeArguments),
				node);
		} else {
			Node* serializedName = b->serializeTypeName(
				node->as<TypeReferenceNode>()->TypeName, false,
				visitor->visitNodes(
					node->as<TypeReferenceNode>()->TypeArguments));
			if (serializedName != nullptr) {
				return b->setTextRange(
					serializedName, node->as<TypeReferenceNode>()->TypeName);
			}
			return nullptr;
		}
	};
	tryVisitSimpleTypeNode = [&](Node* node) -> Node* {
		Node* innerNode = skipParentheses(node);
		switch (innerNode->kind) {
		case Kind::TypeReference:
			return tryVisitTypeReference(innerNode);
		case Kind::TypeQuery:
			return tryVisitTypeQuery(innerNode);
		case Kind::IndexedAccessType:
			return tryVisitIndexedAccess(innerNode);
		case Kind::TypeOperator:
			if (innerNode->as<TypeOperatorNode>()->Operator ==
				Kind::KeyOfKeyword) {
				return tryVisitKeyOf(innerNode);
			}
			break;
		default:
			break;
		}
		return visitor->visitNode(node);
	};
	auto visitExistingNodeTreeSymbolsWorker = [&](Node* node) -> Node* {
		NodeFactory* factory = b->f;
		// !!! TODO: the reparser *should* make all the jsdoc remapping logic here redundant,
		// assuming we only ever try to preserve reparsed nodes and never walk back to the jsdoc "originals"
		// accidentally.
		// Still, what can be ported of the logic is here, just in case.
		// Begin JSDoc handling
		if (node->kind == Kind::JSDocTypeExpression) {
			// Unwrap JSDocTypeExpressions
			return visitor->visitNode(node->as<JSDocTypeExpression>()->Type);
		}
		// !!! TODO: We don't _actually_ support jsdoc namepath types, emit `any` instead; verify we handle as gracefully as strada
		if (node->kind == Kind::JSDocAllType /* || node.Kind == ast.JSDocNamepathType */) {
			return factory->newKeywordTypeNode(Kind::AnyKeyword);
		}
		// !!! TODO: verify JSDocUnknwonType is hopefully just parsed into `unknown` upfront; the kind no longer exists
		// if node.Kind == ast.KindJSDocUnknownType {
		// 	return factory.NewKeywordTypeNode(ast.KindUnknownKeyword)
		// }
		if (node->kind == Kind::JSDocNullableType) {
			std::vector<Node*> unionMembers{
				visitor->visitNode(node->as<JSDocNullableType>()->Type),
				factory->newLiteralTypeNode(
					factory->newKeywordExpression(Kind::NullKeyword)),
			};
			return factory->newUnionTypeNode(
				factory->newNodeList(unionMembers));
		}
		if (node->kind == Kind::JSDocOptionalType) {
			std::vector<Node*> unionMembers{
				visitor->visitNode(node->as<JSDocOptionalType>()->Type),
				factory->newKeywordTypeNode(Kind::UndefinedKeyword),
			};
			return factory->newUnionTypeNode(
				factory->newNodeList(unionMembers));
		}
		if (node->kind == Kind::JSDocNonNullableType) {
			// Unwrap
			return visitor->visitNode(node->as<JSDocNonNullableType>()->Type);
		}
		if (node->kind ==
			Kind::JSDocVariadicType) { // !!! TODO: verify this matches how jsdoc variadics are actually handled now?
			return factory->newArrayTypeNode(
				visitor->visitNode(node->as<JSDocVariadicType>()->Type));
		}
		if (node->kind == Kind::JSDocTypeLiteral) {
			std::vector<Node*> members;
			for (Node* t : node->as<JSDocTypeLiteral>()->JSDocPropertyTags) {
				if (t->kind != Kind::JSDocPropertyTag &&
					t->kind != Kind::JSDocParameterTag) {
					continue;
				}
				Node* n = t->name();
				Node* targetName = nullptr;
				if (isIdentifier(n)) {
					targetName = n;
				} else {
					targetName = n->as<QualifiedName>()
									 ->Right; // !!! TODO: without typesystem backup, doing this cast unguarded seems really suspect, even though it is what strada does
				}
				Node* name = visitor->visitNode(targetName);
				bool shouldBeOptional =
					t->as<JSDocParameterOrPropertyTag>()->IsBracketed ||
					(t->typeExpression() != nullptr &&
					 t->typeExpression()->kind == Kind::JSDocOptionalType);
				Node* question = nullptr;
				if (shouldBeOptional) {
					question = factory->newToken(Kind::QuestionToken);
				}
				Node* ty = visitor->visitNode(
					t->typeExpression()); // !!! TODO: alternate lookup locations for the type? serialize on demand if it doesn't serialze? strada does something funky here.

				members.push_back(factory->newPropertySignatureDeclaration(
					nullptr, name, question, ty, nullptr));
			}
			return factory->newTypeLiteralNode(factory->newNodeList(members));
		}
		// if (ast.IsExpressionWithTypeArguments(node) || ast.IsTypeReferenceNode(node)) && ast.IsJSDocIndexSignature(node) { /// !!! TODO: JSDocIndexSignature handling hasn't been ported - readd if it's readded
		// 	args := node.TypeArguments()
		// 	if len(args) != 2 {
		// 		return factory.NewKeywordTypeNode(ast.KindAnyKeyword) // shouldn't be flagged as a jsdoc index signature in the first place
		// 	}
		// 	return factory.NewTypeLiteralNode(factory.NewNodeList([]*ast.Node{
		// 		factory.NewIndexSignatureDeclaration(nil, factory.NewNodeList([]*ast.Node{
		// 			factory.NewParameterDeclaration(nil, nil, factory.NewIdentifier("x"), nil, visitor.VisitNode(args[0]), nil),
		// 		}), visitor.VisitNode(args[1])),
		// 	}))
		// }
		// if node.Kind == ast.KindJSDocFunctionType {} // !!! no longer exists
		// End JSDoc handling

		if (isTypeReferenceNode(node) &&
			isIdentifier(node->as<TypeReferenceNode>()->TypeName) &&
			node->as<TypeReferenceNode>()->TypeName->as<Identifier>()->Text ==
				"") {
			Node* replacement = factory->newKeywordTypeNode(Kind::AnyKeyword);
			b->e->setOriginal(replacement, node);
			return replacement;
		}
		if (isThisTypeNode(node)) {
			// TODO: strada never marks `this` type nodes as an error - it calls `canReuseTypeNode` on it, but that function always returns `true` for `this`
			// type nodes, which in turn fails to verify that the `this` context is the same between the source and target locations. The conservative thing is to
			// _never_ copy a `this`. We could improve this, but strada is *definitely* wrong and overbroad here. (note that we're inling uses of `canReuseTypeNode`
			// in corsa because of the unfurled host structure meaning we don't need to defer to a host object for functionality it needs)
			// bound.markError(nil) // conservative approach
			return node;
		}
		if (isTypeParameterDeclaration(node)) {
			auto [_3, newName, _4] =
				trackExistingEntityName(node->name(), nullptr);
			return factory->updateTypeParameterDeclaration(
				node->as<TypeParameterDeclaration>(),
				visitor->visitModifiers(node->modifiers()), newName,
				visitor->visitNode(node->as<TypeParameterDeclaration>()->Constraint),
				visitor->visitNode(node->as<TypeParameterDeclaration>()->Expression),
				visitor->visitNode(node->as<TypeParameterDeclaration>()->DefaultType));
		}
		if (isIndexedAccessTypeNode(node)) {
			Node* result = tryVisitIndexedAccess(node);
			if (result != nullptr) {
				return result;
			}
			bound->markError(nullptr);
			return node;
		}
		if (isTypeReferenceNode(node)) {
			Node* result = tryVisitTypeReference(node);
			if (result != nullptr) {
				return result;
			}
			bound->markError(nullptr);
			return node;
		}
		if (isTypeQueryNode(node)) {
			Node* result = tryVisitTypeQuery(node);
			if (result != nullptr) {
				return result;
			}
			bound->markError(nullptr);
			return node;
		}
		if (isTypeOperatorNode(node)) {
			if (node->as<TypeOperatorNode>()->Operator == Kind::UniqueKeyword &&
				node->as<TypeOperatorNode>()->Type->kind ==
					Kind::SymbolKeyword) {
				Node* nonFakeEnclosing =
					b->getEnclosingDeclarationIgnoringFakeScope();
				Node* sameScope = findAncestor(node, [&](Node* a) -> bool {
					return a == nonFakeEnclosing;
				});
				if (sameScope == nullptr) {
					bound->markError(nullptr);
					return node;
				}
			} else if (node->as<TypeOperatorNode>()->Operator ==
					   Kind::KeyOfKeyword) {
				Node* result = tryVisitKeyOf(node);
				if (result != nullptr) {
					return result;
				}
				bound->markError(nullptr);
				return node;
			}
		}
		if (isLiteralImportTypeNode(node)) {
			// assert keyword in imported attributes is deprecated, so we don't reuse types that contain it
			// Ex: import("pkg", { assert: {} }
			if (node->as<ImportTypeNode>()->Attributes != nullptr &&
				node->as<ImportTypeNode>()
						->Attributes->as<ImportAttributes>()
						->Token == Kind::AssertKeyword) {
				bound->markError(nullptr);
				return node;
			}
			Type* t = b->getTypeFromTypeNode(node, true);
			if (t == nullptr) {
				bound->markError(nullptr);
				return node;
			}
			if (isInJSFile(node)) {
				// !!! TODO: invalidate node reuse if js fallback logic used in type param list/typeof lookup (but isn't this logic gone?)
				// s := b.ch.symbolNodeLinks.Get(node).resolvedSymbol
			}
			Node* originalSpec =
				node->as<ImportTypeNode>()->Argument->as<LiteralTypeNode>()
					->Literal;
			Node* specifier = b->rewriteModuleSpecifier(node, originalSpec);
			if (originalSpec == specifier) {
				specifier = visitor->visitNode(
					specifier); // visit node if not replaced
			}
			Node* arg = node->as<ImportTypeNode>()->Argument;
			if (specifier != originalSpec) {
				arg = factory->newLiteralTypeNode(specifier);
			}
			return factory->updateImportTypeNode(
				node->as<ImportTypeNode>(),
				node->as<ImportTypeNode>()->IsTypeOf, arg,
				visitor->visitNode(node->as<ImportTypeNode>()->Attributes),
				visitor->visitNode(node->as<ImportTypeNode>()->Qualifier),
				visitor->visitNodes(node->as<ImportTypeNode>()->TypeArguments));
		}
		if (node->name() != nullptr &&
			node->name()->kind == Kind::ComputedPropertyName &&
			!b->ch->hasLateBindableName(node)) {
			if (!hasDynamicName(node)) {
				// !!! TODO: This matches strada, but rather than recursing, this should probably fall down to later cases.
				// Take a `["field"]` property declaration - it still needs a `: any` appended to it
				return visitor->visitEachChild(node);
			}
			// !!! TODO: this condition matches strada, but it just seems wrong? Or at the very least extraordinarily approximate, and doesn't flag a builder error...
			bool shouldRemoveDeclaration = !((
				(b->ctx->internalFlags &
				 nodebuilder::InternalFlagsAllowUnresolvedNames) != 0 &&
				isEntityNameExpression(
					node->name()->as<ComputedPropertyName>()->Expression) &&
				(b->ch->checkComputedPropertyName(node->name())->flags &
				 TypeFlagsAny) != 0));
			if (shouldRemoveDeclaration) {
				return nullptr;
			}
		}
		if ((isFunctionLike(node) && node->type() == nullptr) ||
			(isPropertyDeclaration(node) && node->type() == nullptr &&
			 node->initializer() == nullptr) ||
			(isPropertySignatureDeclaration(node) && node->type() == nullptr &&
			 node->initializer() == nullptr) ||
			(isParameterDeclaration(node) && node->type() == nullptr &&
			 node->initializer() == nullptr)) {
			Node* visited = visitor->visitEachChild(node);
			if (visited == node) {
				visited = b->setTextRange(node->clone(*factory), node);
			}
			node = visited;
			Node* newType = factory->newKeywordTypeNode(Kind::AnyKeyword);
			switch (node->kind) {
			case Kind::PropertyDeclaration:
				return factory->updatePropertyDeclaration(
					node->as<PropertyDeclaration>(), node->modifiers(),
					node->name(), node->postfixToken(), newType, nullptr);
			case Kind::PropertySignature:
				return factory->updatePropertySignatureDeclaration(
					node->as<PropertySignatureDeclaration>(), node->modifiers(),
					node->name(), node->postfixToken(), newType, nullptr);
			case Kind::Parameter:
				return factory->updateParameterDeclaration(
					node->as<ParameterDeclaration>(), nullptr,
					node->as<ParameterDeclaration>()->DotDotDotToken,
					node->name(),
					node->as<ParameterDeclaration>()->QuestionToken, newType,
					nullptr);
			case Kind::MethodSignature:
				return factory->updateMethodSignatureDeclaration(
					node->as<MethodSignatureDeclaration>(), node->modifiers(),
					node->name(),
					node->as<MethodSignatureDeclaration>()->PostfixToken,
					node->as<MethodSignatureDeclaration>()->TypeParameters,
					node->as<MethodSignatureDeclaration>()->Parameters,
					newType);
			case Kind::CallSignature:
				return factory->updateCallSignatureDeclaration(
					node->as<CallSignatureDeclaration>(),
					node->as<CallSignatureDeclaration>()->TypeParameters,
					node->as<CallSignatureDeclaration>()->Parameters, newType);
			case Kind::JSDocSignature:
				return factory->updateJSDocSignature(
					node->as<JSDocSignature>(),
					node->as<JSDocSignature>()->TypeParameters,
					node->as<JSDocSignature>()->Parameters, newType);
			case Kind::ConstructSignature:
				return factory->updateConstructSignatureDeclaration(
					node->as<ConstructSignatureDeclaration>(),
					node->as<ConstructSignatureDeclaration>()->TypeParameters,
					node->as<ConstructSignatureDeclaration>()->Parameters,
					newType);
			case Kind::IndexSignature:
				return factory->updateIndexSignatureDeclaration(
					node->as<IndexSignatureDeclaration>(), node->modifiers(),
					node->as<IndexSignatureDeclaration>()->Parameters,
					newType);
			case Kind::FunctionType:
				return factory->updateFunctionTypeNode(
					node->as<FunctionTypeNode>(),
					node->as<FunctionTypeNode>()->TypeParameters,
					node->as<FunctionTypeNode>()->Parameters, newType);
			case Kind::ConstructorType:
				return factory->updateConstructorTypeNode(
					node->as<ConstructorTypeNode>(), node->modifiers(),
					node->as<ConstructorTypeNode>()->TypeParameters,
					node->as<ConstructorTypeNode>()->Parameters, newType);
			default:
				break;
			}
		}
		if (isComputedPropertyName(node) &&
			isEntityNameExpression(
				node->as<ComputedPropertyName>()->Expression)) {
			auto [introducesError, result, _5] = trackExistingEntityName(
				node->as<ComputedPropertyName>()->Expression, nullptr);
			if (!introducesError) {
				return factory->updateComputedPropertyName(
					node->as<ComputedPropertyName>(), result);
			} else {
				// !!! TODO: rewriting computed names based on evaluator/typecheck results?
				// strada's behavior seems hard to justify vs marking an error and moving on
				bound->markError(nullptr);
				return visitor->visitEachChild(node);
			}
		}
		if (isTypePredicateNode(node)) {
			Node* parameterName = nullptr;
			if (isIdentifier(node->as<TypePredicateNode>()->ParameterName)) {
				auto [introducesError, result, _6] = trackExistingEntityName(
					node->as<TypePredicateNode>()->ParameterName, nullptr);
				// Should not usually happen the only case is when a type predicate comes from a JSDoc type annotation with it's own parameter symbol definition.
				// /** @type {(v: unknown) => v is undefined} */
				// const isUndef = v => v === undefined;
				if (introducesError) {
					bound->markError(nullptr);
				}
				parameterName = result;
			} else {
				parameterName =
					node->as<TypePredicateNode>()->ParameterName->clone(
						*factory);
			}
			return factory->updateTypePredicateNode(
				node->as<TypePredicateNode>(),
				visitor->visitNode(
					node->as<TypePredicateNode>()->AssertsModifier),
				parameterName,
				visitor->visitNode(node->as<TypePredicateNode>()->Type));
		}
		if (isConditionalTypeNode(node)) {
			Node* checkType = visitor->visitNode(
				node->as<ConditionalTypeNode>()->CheckType);
			std::function<void()> dispose =
				b->enterNewScope(node, {}, b->ch->getInferTypeParameters(node),
				                 {}, nullptr);
			Node* extendsType = visitor->visitNode(
				node->as<ConditionalTypeNode>()->ExtendsType);
			Node* trueType =
				visitor->visitNode(node->as<ConditionalTypeNode>()->TrueType);
			dispose();
			Node* falseType =
				visitor->visitNode(node->as<ConditionalTypeNode>()->FalseType);
			return factory->updateConditionalTypeNode(
				node->as<ConditionalTypeNode>(), checkType, extendsType,
				trueType, falseType);
		}

		// style applications
		if (isTupleTypeNode(node) ||
			((b->ctx->flags & nodebuilder::FlagsMultilineObjectLiterals) == 0 &&
			 isTypeLiteralNode(node)) ||
			isMappedTypeNode(node)) {
			// make tuples/types/mappedtypes single line
			Node* res = visitor->visitEachChild(node);
			if (res == node) {
				res = res->clone(*factory);
				res = b->setTextRange(res, node);
			}
			b->e->addEmitFlags(res, printer::EFSingleLine);
			return res;
		}

		if (isStringLiteralLike(node)) {
			// Preserve the original characters of the literal (e.g. emojis) in declaration emit
			// rather than escaping them as ASCII Unicode escapes. Mirrors TypeScript's behavior
			// for synthesized string literal types in the node builder (checker.ts:6853).
			Node* c = node->clone(*b->f);
			if (isStringLiteral(node) &&
				(b->ctx->flags &
				 nodebuilder::FlagsUseSingleQuotesForStringLiteralType) != 0 &&
				!(node->as<StringLiteral>()->TokenFlags &
				  TokenFlagsSingleQuote)) {
				// set single quote on string literals
				c->as<StringLiteral>()->TokenFlags ^= TokenFlagsSingleQuote;
			}
			b->e->addEmitFlags(c, printer::EFNoAsciiEscaping);
			return c;
		}

		return visitor->visitEachChild(node);
	};
	bool nonLocalNode = true;
	NodeVisitorHooks hooks;
	hooks.visitNodes = [&](NodeList* nodes, NodeVisitor* v) -> NodeList* {
		NodeList* res = v->visitNodes(nodes);
		if (nonLocalNode && res != nullptr) {
			// Remove position data from node lists originating in other files
			if (res == nodes) {
				res = nodes->clone(*b->f);
			}
			res->loc = TextRange{-1, -1};
		}
		return res;
	};
	hooks.visitNode = [&](Node* node, NodeVisitor* v) -> Node* {
		// Capture if the current node is in the current file so node lists knoww if they can keep positions or not
		bool oldNonLocalNode = nonLocalNode;
		nonLocalNode =
			b->ctx->enclosingFile == nullptr ||
			b->ctx->enclosingFile !=
				getSourceFileOfNode(b->e->mostOriginal(node));
		Node* res = v->visitNode(node);
		nonLocalNode = oldNonLocalNode;
		return res;
	};
	visitor = newNodeVisitor(
		[&](Node* node) -> Node* {
			// If there was an error in a sibling node bail early, the result will be discarded anyway
			if (bound->hadError) {
				return node;
			}
			originalRecoveryScopeState recover_ = bound->startRecoveryScope();
			bool introducesNewScope =
				isFunctionLike(node) || isMappedTypeNode(node);
			std::function<void()> exit;
			if (introducesNewScope) {
				std::vector<Symbol*> params;
				std::vector<Type*> typeParams;
				if (isFunctionLike(node)) {
					Signature* sig = b->ch->getSignatureFromDeclaration(node);
					params = sig->parameters;
					typeParams = sig->typeParameters;
				} else if (isConditionalTypeNode(node)) { // !!! TODO: impossible in combination with the scope start check???
					typeParams = b->ch->getInferTypeParameters(node);
				} else if (isMappedTypeNode(node)) {
					typeParams = {b->ch->getDeclaredTypeOfTypeParameter(
						b->ch->getSymbolOfDeclaration(
							node->as<MappedTypeNode>()->TypeParameter))};
				}
				exit = b->enterNewScope(node, params, typeParams, {}, nullptr);
			}
			Node* result = visitExistingNodeTreeSymbolsWorker(node);
			if (exit) {
				exit();
			}

			if (result == node && !nodeIsSynthesized(node)) {
				result = deepCloneNode(*b->f, node); // always clone a new node
			}

			// We want to clone the subtree, so when we mark it up with __pos and __end in quickfixes,
			//  we don't get odd behavior because of reused nodes. We also need to clone to _remove_
			//  the position information if the node comes from a different file than the one the node builder
			//  is set to build for (even though we are reusing the node structure, the position information
			//  would make the printer print invalid spans for literals and identifiers, and the formatter would
			//  choke on the mismatched positonal spans between a parent and an injected child from another file).
			result = b->setTextRange(result, node);

			if (bound->hadError) {
				if (isTypeNode(node) && !isTypePredicateNode(node)) {
					bound->endRecoveryScope(recover_);
					// TODO: this fallback matches strada behavior, but it lacks any verification that the type from `node` actually matches
					// the type we'd expect at this traversal position within the parent type.
					Type* t = b->getTypeFromTypeNode(node, false);
					return b->typeToTypeNode(t);
				}
				return b->setTextRange(node->clone(*b->f), node);
			}

			return result;
		},
		b->f, hooks);
	return visitor;
}

}  // namespace tsc::checker
