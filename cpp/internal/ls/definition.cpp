// === slice: ls-coreC ===
// definition.cpp — definition.go: go-to-definition and go-to-type-definition.
#include "internal/astnav/tokens.h"
#include "internal/ls/ls.h"

namespace tsc::ls {

namespace {

// core.NewTextRange
TextRange newTextRange(int pos, int end) {
	return TextRange{static_cast<TextPos>(pos), static_cast<TextPos>(end)};
}

// scope-bound `defer` — Go `defer done()`.
struct Deferred {
	std::function<void()> f;
	~Deferred() { if (f) f(); }
};

// --- file-local replicas of ast/utilities.go helpers ---
// (isJumpStatementTarget / isRightSideOfPropertyAccess live in
// utilities.cpp)

// getContextNode — findallreferences.go:286. Declared in ls.h (defined in
// findallreferences.cpp at tsc::ls scope).

// ast/utilities.go:3764 — GetInvokedExpression
::tsc::Node* getInvokedExpression(::tsc::Node* node) {
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

// ast/utilities.go:587 — IsObjectLiteralElement
bool isObjectLiteralElement(::tsc::Node* node) {
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

// definition.go:380 — fwd decl lives in ls.h (defined below, used
// earlier).

// ast/utilities.go:2998 — IsCallLikeExpression
bool isCallLikeExpression(::tsc::Node* node) {
	switch (node->kind) {
	case Kind::JsxOpeningElement:
	case Kind::JsxSelfClosingElement:
	case Kind::JsxOpeningFragment:
	case Kind::CallExpression:
	case Kind::NewExpression:
	case Kind::TaggedTemplateExpression:
	case Kind::Decorator:
		return true;
	case Kind::BinaryExpression:
		return node->as<BinaryExpression>()->OperatorToken->kind ==
			   Kind::InstanceOfKeyword;
	}
	return false;
}

// core.OrElse
template <class T>
T orElse(T v, T fallback) {
	return v != nullptr ? v : fallback;
}

template <class T>
std::vector<T> concatVec(const std::vector<T>& a, const std::vector<T>& b) {
	std::vector<T> r = a;
	r.insert(r.end(), b.begin(), b.end());
	return r;
}

template <class T>
T firstOrNil(const std::vector<T>& v) {
	return v.empty() ? nullptr : v.front();
}

template <class T, class F>
std::vector<T> filterTo(const std::vector<T>& v, F f) {
	std::vector<T> r;
	for (auto& x : v)
		if (f(x)) r.push_back(x);
	return r;
}

template <class T, class F>
bool someOf(const std::vector<T>& v, F f) {
	for (auto& x : v)
		if (f(x)) return true;
	return false;
}

template <class T>
void appendIfUnique(std::vector<T>& v, T x) {
	if (std::find(v.begin(), v.end(), x) == v.end()) v.push_back(x);
}

template <class T, class F>
auto mapVec(const std::vector<T>& v, F f) -> std::vector<decltype(f(v[0]))> {
	using U = decltype(f(v[0]));
	std::vector<U> result;
	result.reserve(v.size());
	for (const T& x : v) result.push_back(f(x));
	return result;
}

// definition.go:208 — fileRange
struct fileRange {
	SourceFile* file = nullptr;
	// Go field name `fileRange` collides with the struct name in C++.
	TextRange range_{};
	bool operator==(const fileRange&) const = default;
};

} // namespace
} // namespace tsc::ls

namespace std {
template <> struct hash<tsc::ls::fileRange> {
	size_t operator()(const tsc::ls::fileRange& fr) const {
		return std::hash<tsc::SourceFile*>{}(fr.file) ^
			   std::hash<tsc::TextPos>{}(fr.range_.pos()) * 131 ^
			   std::hash<tsc::TextPos>{}(fr.range_.end()) * 8191;
	}
};
} // namespace std

namespace tsc::ls {

// definition.go:162 — combineDefinitionResponses
lsp::lsproto::DefinitionResponse combineDefinitionResponses(
	const std::vector<lsp::lsproto::DefinitionResponse>& results, bool links) {
	std::vector<lsp::lsproto::Location> locations;
	std::vector<std::shared_ptr<lsp::lsproto::LocationLink>>
	    definitionLinks;
	collections::Set<lsp::lsproto::Location> seen;
	for (auto& result : results) {
		if (result.DefinitionLinks != nullptr) {
			for (auto& link : **result.DefinitionLinks) {
				lsp::lsproto::Location location;
				location.Uri = link->TargetUri;
				location.Range = link->TargetSelectionRange;
				if (seen.AddIfAbsent(location)) {
					definitionLinks.push_back(link);
					locations.push_back(location);
				}
			}
		}
		if (result.Location != nullptr && seen.AddIfAbsent(*result.Location)) {
			locations.push_back(*result.Location);
			auto link = std::make_shared<lsp::lsproto::LocationLink>();
			link->TargetUri = result.Location->Uri;
			link->TargetRange = result.Location->Range;
			link->TargetSelectionRange = result.Location->Range;
			definitionLinks.push_back(link);
		}
		if (result.Locations != nullptr) {
			for (auto& location : **result.Locations) {
				if (seen.AddIfAbsent(location)) {
					locations.push_back(location);
					auto link = std::make_shared<lsp::lsproto::LocationLink>();
					link->TargetUri = location.Uri;
					link->TargetRange = location.Range;
					link->TargetSelectionRange = location.Range;
					definitionLinks.push_back(link);
				}
			}
		}
	}
	lsp::lsproto::LocationOrLocationsOrDefinitionLinksOrNull res;
	if (links) {
		res.DefinitionLinks = std::make_shared<lsp::lsproto::Slice<
		    std::shared_ptr<lsp::lsproto::LocationLink>>>(
		    std::move(definitionLinks));
	} else {
		res.Locations = std::make_shared<lsp::lsproto::Slice<
		    lsp::lsproto::Location>>(std::move(locations));
	}
	return res;
}

// definition.go:195 — getDeclarationNameForKeyword
::tsc::Node* getDeclarationNameForKeyword(::tsc::Node* node) {
	if (node->kind >= KindFirstKeyword && node->kind <= KindLastKeyword) {
		if (isVariableDeclarationList(node->parent)) {
			if (::tsc::Node* decl = firstOrNil(
					node->parent->as<VariableDeclarationList>()->Declarations->nodes);
				decl != nullptr && decl->name() != nullptr) {
				return decl->name();
			}
		} else if (node->parent->declarationData().symbol != nullptr &&
				   node->parent->name() != nullptr &&
				   node->pos() < node->parent->name()->pos()) {
			return node->parent->name();
		}
	}
	return node;
}

// definition.go:281 — lspRangeContains
bool lspRangeContains(lsp::lsproto::Range outer, lsp::lsproto::Range inner) {
	return lsp::lsproto::ComparePositions(outer.Start, inner.Start) <= 0 &&
		   lsp::lsproto::ComparePositions(inner.End, outer.End) <= 0;
}

// definition.go:286 — createLocationsFromLinks
lsp::lsproto::DefinitionResponse createLocationsFromLinks(
	std::vector<std::shared_ptr<lsp::lsproto::LocationLink>>& links) {
	auto locations = mapVec(links,
	                        [](const std::shared_ptr<lsp::lsproto::LocationLink>&
	                               link) {
		lsp::lsproto::Location loc;
		loc.Uri = link->TargetUri;
		loc.Range = link->TargetSelectionRange;
		return loc;
	});
	lsp::lsproto::LocationOrLocationsOrDefinitionLinksOrNull res;
	res.Locations = std::make_shared<lsp::lsproto::Slice<
	    lsp::lsproto::Location>>(std::move(locations));
	return res;
}

// definition.go:306 — getDeclarationsFromLocation
std::vector<::tsc::Node*> getDeclarationsFromLocation(checker::Checker* c,
													::tsc::Node* node) {
	if (isIdentifier(node) && isShorthandPropertyAssignment(node->parent)) {
		// Because name in short-hand property assignment has two different meanings: property name and property value,
		// using go-to-definition at such position should go to the variable declaration of the property value rather than
		// go to the declaration of the property name (in this case stay at the same position). However, if go-to-definition
		// is performed at the location of property access, we would like to go to definition of the property in the short-hand
		// assignment. This case and others are handled by the following code.
		// and the contextual type's property declarations
		Symbol* shorthandSymbol = c->GetResolvedSymbol(node);
		std::vector<::tsc::Node*> declarations;
		if (shorthandSymbol != nullptr) {
			declarations = shorthandSymbol->data->declarations;
		}
		std::vector<::tsc::Node*> contextualDeclarations =
			getDeclarationsFromObjectLiteralElement(c, node);
		return concatVec(declarations, contextualDeclarations);
	}

	if (isPropertyName(node) && isBindingElement(node->parent) &&
		isObjectBindingPattern(node->parent->parent)) {
		// If the node is the name of a BindingElement within an ObjectBindingPattern instead of just returning the
		// declaration of the symbol (which is itself), we should try to get to the original type of the
		// ObjectBindingPattern and return the property declaration for the referenced property.
		// For example:
		//      import('./foo').then(({ bar }) => undefined); => should navigate to the declaration in file "./foo"
		//
		//      function bar<T>(onfulfilled: (value: T) => void) { }
		//      interface Test { prop1: number }
		//      bar<Test>(({ prop1 }) => {});  => should navigate to prop1 in Test
		BindingElement* bindingEl = node->parent->as<BindingElement>();
		if (bindingEl->DotDotDotToken == nullptr &&
			node == orElse(bindingEl->PropertyName, node->parent->name())) {
			std::string name;
			if (tryGetTextOfPropertyName(node, name)) {
				checker::Type* t = c->GetTypeAtLocation(node->parent->parent);
				std::vector<checker::Type*> types{t};
				if (t->IsUnion()) {
					types = t->types();
				}
				std::vector<::tsc::Node*> result;
				for (auto* unionType : types) {
					if (Symbol* prop = c->GetPropertyOfType(unionType, name);
						prop != nullptr) {
						result.insert(result.end(), prop->data->declarations.begin(),
									  prop->data->declarations.end());
					}
				}
				return result;
			}
		}
	}

	node = getDeclarationNameForKeyword(node);
	if (Symbol* symbol = c->GetSymbolAtLocation(node); symbol != nullptr) {
		if ((symbol->flags & SymbolFlagsClass) != 0 &&
			(symbol->flags & (SymbolFlagsFunction | SymbolFlagsVariable)) == 0 &&
			node->kind == Kind::ConstructorKeyword) {
			if (Symbol* constructor =
					getSymbolFromTable(symbol->data->members, InternalSymbolNameConstructor);
				constructor != nullptr) {
				symbol = constructor;
			}
		}
		if ((symbol->flags & SymbolFlagsAlias) != 0) {
			if (auto [resolved, ok] = c->ResolveAlias(symbol); ok) {
				symbol = resolved;
			}
		}
		std::vector<::tsc::Node*> objectLiteralElementDeclarations =
			getDeclarationsFromObjectLiteralElement(c, node);
		if (!objectLiteralElementDeclarations.empty()) {
			return objectLiteralElementDeclarations;
		}
		if (!symbol->data->declarations.empty()) {
			return symbol->data->declarations;
		}
	}
	if (std::vector<::tsc::Node*> indexInfos = c->GetIndexSignaturesAtLocation(node);
		!indexInfos.empty()) {
		return indexInfos;
	}
	return {};
}

// getDeclarationsFromObjectLiteralElement returns declarations from the contextual type
// of an object literal element, if available.
// definition.go:380
std::vector<::tsc::Node*> getDeclarationsFromObjectLiteralElement(checker::Checker* c,
																::tsc::Node* node) {
	::tsc::Node* element = getContainingObjectLiteralElement(node);
	if (element == nullptr) {
		return {};
	}

	checker::Type* contextualType =
		c->GetContextualType(element->parent, checker::ContextFlagsNone);
	if (contextualType == nullptr) {
		return {};
	}

	std::vector<Symbol*> properties =
		c->GetPropertySymbolsFromContextualType(element, contextualType,
											  false /*unionSymbolOk*/);
	if (someOf(properties, [&](Symbol* p) {
			return p->data->valueDeclaration != nullptr &&
				   isObjectLiteralExpression(p->data->valueDeclaration->parent) &&
				   isObjectLiteralElement(p->data->valueDeclaration) &&
				   p->data->valueDeclaration->name() == node;
		})) {
		if (checker::Type* withoutNodeInferencesType = c->GetContextualType(
				element->parent, checker::ContextFlagsIgnoreNodeInferences);
			withoutNodeInferencesType != nullptr) {
			if (auto withoutNodeInferencesProperties =
					c->GetPropertySymbolsFromContextualType(
						element, withoutNodeInferencesType, false /*unionSymbolOk*/);
				!withoutNodeInferencesProperties.empty()) {
				properties = withoutNodeInferencesProperties;
			}
		}
	}

	std::vector<::tsc::Node*> result;
	for (auto* prop : properties) {
		result.insert(result.end(), prop->data->declarations.begin(),
					  prop->data->declarations.end());
	}
	return result;
}

// Returns a CallLikeExpression where `node` is the target being invoked.
// definition.go:410
::tsc::Node* getAncestorCallLikeExpression(::tsc::Node* node) {
	::tsc::Node* target = findAncestor(node, [](::tsc::Node* n) {
		return !isRightSideOfPropertyAccess(n);
	});
	::tsc::Node* callLike = target->parent;
	if (callLike != nullptr && isCallLikeExpression(callLike) &&
		getInvokedExpression(callLike) == target) {
		return callLike;
	}
	return nullptr;
}

// definition.go:421 — tryGetSignatureDeclaration
::tsc::Node* tryGetSignatureDeclaration(checker::Checker* typeChecker,
										::tsc::Node* node) {
	checker::Signature* signature = nullptr;
	::tsc::Node* callLike = getAncestorCallLikeExpression(node);
	if (callLike != nullptr) {
		signature = typeChecker->GetResolvedSignature(callLike);
	}
	// Don't go to a function type, go to the value having that type.
	::tsc::Node* declaration = nullptr;
	if (signature != nullptr && signature->declaration != nullptr) {
		declaration = signature->declaration;
		if (isFunctionLike(declaration) &&
			!isFunctionTypeNode(declaration)) {
			return declaration;
		}
	}
	return nullptr;
}

// definition.go:438 — isJsxConstructorLike
bool isJsxConstructorLike(::tsc::Node* node) {
	return isConstructorDeclaration(node) || isConstructorTypeNode(node) ||
		   isCallSignatureDeclaration(node) ||
		   isConstructSignatureDeclaration(node);
}

// definition.go:450 — symbolMatchesSignature
bool symbolMatchesSignature(Symbol* symbol, ::tsc::Node* calledDeclaration) {
	if (symbol == nullptr || calledDeclaration == nullptr) {
		return false;
	}
	Symbol* calledSymbol = calledDeclaration->symbol();
	if (symbol == calledSymbol ||
		(calledSymbol != nullptr && symbol == calledSymbol->data->parent)) {
		return true;
	}
	::tsc::Node* parent = calledDeclaration->parent;
	return parent != nullptr &&
		   (isAssignmentExpression(parent, false /*excludeCompoundAssignment*/) ||
			(!isCallLikeExpression(parent) && canHaveSymbol(parent) &&
			 symbol == parent->symbol()));
}

// definition.go:463 — getSymbolForOverriddenMember
Symbol* getSymbolForOverriddenMember(checker::Checker* typeChecker,
									 ::tsc::Node* node) {
	::tsc::Node* classElement = findAncestor(node, &isClassElement);
	if (classElement == nullptr || classElement->name() == nullptr) {
		return nullptr;
	}
	::tsc::Node* baseDeclaration =
		findAncestor(classElement, &isClassLike);
	if (baseDeclaration == nullptr) {
		return nullptr;
	}
	::tsc::Node* baseTypeNode =
		getClassExtendsHeritageElement(baseDeclaration);
	if (baseTypeNode == nullptr) {
		return nullptr;
	}
	::tsc::Node* expression =
		skipParentheses(baseTypeNode->expression());
	Symbol* base = nullptr;
	if (isClassExpression(expression)) {
		base = expression->symbol();
	} else {
		base = typeChecker->GetSymbolAtLocation(expression);
	}
	if (base == nullptr) {
		return nullptr;
	}
	std::string name = getTextOfPropertyName(classElement->name());
	if (hasStaticModifier(classElement)) {
		return typeChecker->GetPropertyOfType(typeChecker->GetTypeOfSymbol(base),
											  name);
	}
	return typeChecker->GetPropertyOfType(
		typeChecker->GetDeclaredTypeOfSymbol(base), name);
}

// definition.go:493 — getTypeOfSymbolAtLocation
checker::Type* getTypeOfSymbolAtLocation(checker::Checker* c, Symbol* symbol,
										 ::tsc::Node* node) {
	checker::Type* t = c->GetTypeOfSymbolAtLocation(symbol, node);
	// If the type is just a function's inferred type, go-to-type should go to the return type instead since
	// go-to-definition takes you to the function anyway.
	if (t->symbol == symbol ||
		(t->symbol != nullptr && symbol->data->valueDeclaration != nullptr &&
		 isVariableDeclaration(symbol->data->valueDeclaration) &&
		 symbol->data->valueDeclaration->initializer() ==
			 t->symbol->data->valueDeclaration)) {
		auto sigs = c->GetCallSignatures(t);
		if (sigs.size() == 1) {
			return c->GetReturnTypeOfSignature(sigs[0]);
		}
	}
	return t;
}

// definition.go:506 — getDeclarationsFromType
std::vector<::tsc::Node*> getDeclarationsFromType(checker::Type* t) {
	std::vector<::tsc::Node*> result;
	for (auto* u : t->Distributed()) {
		if (u->symbol != nullptr) {
			for (auto* decl : u->symbol->data->declarations) {
				appendIfUnique(result, decl);
			}
		}
	}
	return result;
}

// ============================================================================
// definition.go — ProvideDefinition / provideDefinitionWorker /
// provideDefinitionAtPosition
// ============================================================================
// definition.go:19
lsp::lsproto::DefinitionResponse LanguageService::ProvideDefinition(
	gostd::Context ctx, lsp::lsproto::DocumentUri documentURI,
	lsp::lsproto::Position position) {
	if (UserPreferences().PreferGoToSourceDefinition) {
		return ProvideSourceDefinition(ctx, documentURI, position);
	}
	return provideDefinitionWorker(ctx, documentURI, position);
}

// definition.go:30
lsp::lsproto::DefinitionResponse LanguageService::provideDefinitionWorker(
	gostd::Context ctx, lsp::lsproto::DocumentUri documentURI,
	lsp::lsproto::Position position) {
	auto caps = lsp::lsproto::getClientCapabilities(ctx);
	bool clientSupportsLink = caps->TextDocument.Definition.LinkSupport;

	auto [program, file] = getProgramAndFile(documentURI);
	auto positions = converters->FromLSPPositionForSourceFile(file, position,
															spanmap::FeatureDefinition);
	std::vector<lsp::lsproto::DefinitionResponse> results;
	results.reserve(positions.size());
	for (auto& mapped : positions) {
		if (mapped.Fidelity.IsSingleSegment()) {
			results.push_back(provideDefinitionAtPosition(
				ctx, program, mapped.Script, mapped.Position, clientSupportsLink));
		}
	}
	return combineDefinitionResponses(results, clientSupportsLink);
}

// definition.go:49
lsp::lsproto::DefinitionResponse LanguageService::provideDefinitionAtPosition(
	gostd::Context ctx, compiler::SimpleProgram* program, SourceFile* file,
	TextPos textPos, bool clientSupportsLink) {
	int pos = int(textPos);
	::tsc::Node* node = astnav::getTouchingPropertyName(file, pos);
	refInfo* reference = getReferenceAtPosition(file, pos, program);

	if (node->kind == Kind::SourceFile) {
		return lsp::lsproto::LocationOrLocationsOrDefinitionLinksOrNull{};
	}

	auto [originSelectionRange, _f1] = createLspRangeFromNode(node, file);
	if (reference != nullptr && reference->file != nullptr) {
		return createDefinitionLocations(originSelectionRange, clientSupportsLink,
										 {}, reference, spanmap::FeatureDefinition);
	}

	auto [c, done] = program->GetTypeCheckerForFileExclusive(file);
	Deferred _done{done};

	if (node->kind == Kind::OverrideKeyword) {
		if (Symbol* sym = getSymbolForOverriddenMember(c, node); sym != nullptr) {
			return createDefinitionLocations(originSelectionRange,
											 clientSupportsLink, sym->data->declarations,
											 nullptr /*reference*/,
											 spanmap::FeatureDefinition);
		}
	}

	if (isJumpStatementTarget(node)) {
		if (::tsc::Node* label = getTargetLabel(node->parent, node->text());
			label != nullptr) {
			return createDefinitionLocations(originSelectionRange,
											 clientSupportsLink, {label},
											 nullptr /*reference*/,
											 spanmap::FeatureDefinition);
		}
	}

	if (node->kind == Kind::CaseKeyword ||
		(node->kind == Kind::DefaultKeyword && isDefaultClause(node->parent))) {
		if (::tsc::Node* stmt = findAncestor(node->parent, &isSwitchStatement);
			stmt != nullptr) {
			SourceFile* file = getSourceFileOfNode(stmt);
			return createLocationFromFileAndRange(
				file, tsc::getRangeOfTokenAtPosition(file, int(stmt->pos())),
				spanmap::FeatureDefinition);
		}
	}

	if (node->kind == Kind::ReturnKeyword || node->kind == Kind::YieldKeyword ||
		node->kind == Kind::AwaitKeyword) {
		if (::tsc::Node* fn = findAncestor(node, &isFunctionLikeDeclaration);
			fn != nullptr) {
			return createDefinitionLocations(originSelectionRange,
											 clientSupportsLink, {fn},
											 nullptr /*reference*/,
											 spanmap::FeatureDefinition);
		}
	}

	std::vector<::tsc::Node*> declarations = getDeclarationsFromLocation(c, node);
	::tsc::Node* calledDeclaration = tryGetSignatureDeclaration(c, node);
	if (calledDeclaration != nullptr &&
		!(isJsxOpeningLikeElement(node->parent) &&
		  isJsxConstructorLike(calledDeclaration))) {
		Symbol* symbol = c->GetSymbolAtLocation(getDeclarationNameForKeyword(node));
		if (symbol != nullptr &&
			someOf(c->GetRootSymbols(symbol), [&](Symbol* rootSymbol) {
				return symbolMatchesSignature(rootSymbol, calledDeclaration);
			})) {
			if (!isConstructorDeclaration(calledDeclaration)) {
				declarations = {};
			} else {
				declarations = filterTo(declarations, [&](::tsc::Node* node) {
					return node != calledDeclaration &&
						   (isClassDeclaration(node) ||
							isClassExpression(node));
				});
			}
		} else {
			declarations = filterTo(declarations, [&](::tsc::Node* node) {
				return node != calledDeclaration;
			});
		}
		declarations.push_back(calledDeclaration);
	}
	return createDefinitionLocations(originSelectionRange, clientSupportsLink,
									 declarations, reference,
									 spanmap::FeatureDefinition);
}

// ============================================================================
// definition.go — ProvideTypeDefinition / provideTypeDefinitionAtPosition
// ============================================================================
// definition.go:113
lsp::lsproto::TypeDefinitionResponse LanguageService::ProvideTypeDefinition(
	gostd::Context ctx, lsp::lsproto::DocumentUri documentURI,
	lsp::lsproto::Position position) {
	auto caps = lsp::lsproto::getClientCapabilities(ctx);
	bool clientSupportsLink = caps->TextDocument.TypeDefinition.LinkSupport;

	auto [program, file] = getProgramAndFile(documentURI);
	auto positions = converters->FromLSPPositionForSourceFile(file, position,
															spanmap::FeatureTypeDefinition);
	std::vector<lsp::lsproto::TypeDefinitionResponse> results;
	results.reserve(positions.size());
	for (auto& mapped : positions) {
		if (mapped.Fidelity.IsSingleSegment()) {
			results.push_back(provideTypeDefinitionAtPosition(
				ctx, program, mapped.Script, mapped.Position, clientSupportsLink));
		}
	}
	return combineDefinitionResponses(results, clientSupportsLink);
}

// definition.go:132
lsp::lsproto::TypeDefinitionResponse LanguageService::provideTypeDefinitionAtPosition(
	gostd::Context ctx, compiler::SimpleProgram* program, SourceFile* file,
	TextPos textPos, bool clientSupportsLink) {
	int pos = int(textPos);
	::tsc::Node* node = astnav::getTouchingPropertyName(file, pos);
	if (node->kind == Kind::SourceFile) {
		return lsp::lsproto::LocationOrLocationsOrDefinitionLinksOrNull{};
	}
	auto [originSelectionRange, _f1] = createLspRangeFromNode(node, file);

	auto [c, done] = program->GetTypeCheckerForFileExclusive(file);
	Deferred _done{done};

	node = getDeclarationNameForKeyword(node);

	if (Symbol* symbol = c->GetSymbolAtLocation(node); symbol != nullptr) {
		checker::Type* symbolType = getTypeOfSymbolAtLocation(c, symbol, node);
		std::vector<::tsc::Node*> declarations = getDeclarationsFromType(symbolType);
		if (checker::Type* typeArgument =
				c->GetFirstTypeArgumentFromKnownType(symbolType);
			typeArgument != nullptr) {
			declarations =
				concatVec(getDeclarationsFromType(typeArgument), declarations);
		}
		if (!declarations.empty()) {
			return createDefinitionLocations(originSelectionRange,
											 clientSupportsLink, declarations,
											 nullptr /*reference*/,
											 spanmap::FeatureTypeDefinition);
		}
		if ((symbol->flags & SymbolFlagsValue) == 0 &&
			(symbol->flags & SymbolFlagsType) != 0) {
			return createDefinitionLocations(originSelectionRange,
											 clientSupportsLink,
											 symbol->data->declarations,
											 nullptr /*reference*/,
											 spanmap::FeatureTypeDefinition);
		}
	}

	return lsp::lsproto::LocationOrLocationsOrDefinitionLinksOrNull{};
}

// ============================================================================
// definition.go — createDefinitionLocations / createLocationFromFileAndRange
// ============================================================================
// definition.go:213
lsp::lsproto::DefinitionResponse LanguageService::createDefinitionLocations(
	lsp::lsproto::Range originSelectionRange, bool clientSupportsLink,
	std::vector<::tsc::Node*> declarations, refInfo* reference,
	spanmap::Feature feature) {
	std::vector<std::shared_ptr<lsp::lsproto::LocationLink>> locations;
	collections::Set<fileRange> locationRanges;

	if (reference != nullptr) {
		// definition.go:224 — Go zero-initializes targetRange here.
		lsp::lsproto::Range targetRange{};
		auto link = std::make_shared<lsp::lsproto::LocationLink>();
		link->OriginSelectionRange =
			std::make_shared<lsp::lsproto::Range>(originSelectionRange);
		link->TargetUri =
			lsconv::FileNameToDocumentURI(reference->fileName);
		link->TargetRange = targetRange;
		link->TargetSelectionRange = targetRange;
		locations.push_back(link);
	}

	for (auto* decl : declarations) {
		SourceFile* file = getSourceFileOfNode(decl);
		::tsc::Node* name = orElse(getNameOfDeclaration(decl), decl);
		TextRange nameRange;
		if (name->kind == Kind::EmptyStatement) {
			nameRange = newTextRange(int(name->pos()), int(name->pos()));
		} else {
			nameRange = createRangeFromNode(name, file);
		}
		if (locationRanges.AddIfAbsent(fileRange{file, nameRange})) {
			::tsc::Node* contextNode = orElse(getContextNode(decl), decl);
			TextRange* contextRange =
				orElse(toContextRange(&nameRange, file, contextNode), &nameRange);
			if (!nameRange.containedBy(*contextRange)) {
				TextRange enclosingRange = newTextRange(
					std::min(nameRange.pos(), contextRange->pos()),
					std::max(nameRange.end(), contextRange->end()));
				*contextRange = enclosingRange;
			}
			auto [targetSelectionLoc, selectionFidelity] =
				sourceFileRangeToLSPLocationForFeature(file, nameRange, feature);
			if (!selectionFidelity.IsSingleSegment()) {
				continue;
			}
			auto [targetLoc, contextFidelity] =
				sourceFileRangeToLSPLocation(file, *contextRange);
			if (contextFidelity.IsNone() ||
				targetLoc.Uri != targetSelectionLoc.Uri ||
				!lspRangeContains(targetLoc.Range, targetSelectionLoc.Range)) {
				targetLoc = targetSelectionLoc;
			}
			auto link = std::make_shared<lsp::lsproto::LocationLink>();
			link->OriginSelectionRange = std::make_shared<lsp::lsproto::Range>(
			    originSelectionRange);
			link->TargetSelectionRange = targetSelectionLoc.Range;
			link->TargetUri = targetLoc.Uri;
			link->TargetRange = targetLoc.Range;
			locations.push_back(link);
		}
	}

	lsp::lsproto::LocationOrLocationsOrDefinitionLinksOrNull res;
	if (clientSupportsLink) {
		res.DefinitionLinks = std::make_shared<lsp::lsproto::Slice<
		    std::shared_ptr<lsp::lsproto::LocationLink>>>(
		    std::move(locations));
		return res;
	}
	return createLocationsFromLinks(locations);
}

// definition.go:296
lsp::lsproto::DefinitionResponse LanguageService::createLocationFromFileAndRange(
	SourceFile* file, TextRange textRange, spanmap::Feature feature) {
	auto [mappedLocation, fidelity] =
		sourceFileRangeToLSPLocationForFeature(file, textRange, feature);
	if (fidelity.IsNone()) {
		mappedLocation.Range = lsp::lsproto::Range{};
	}
	lsp::lsproto::LocationOrLocationsOrDefinitionLinksOrNull res;
	res.Location =
		std::make_shared<lsp::lsproto::Location>(mappedLocation);
	return res;
}

} // namespace tsc::ls
