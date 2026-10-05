// checker_walk.cpp — checkSourceFile walker + expression-check entry points.
// Ports checker.go:2237-2622 (walker + deferred checks + JSDoc comment pass),
// checker.go:7509-7576 (type-of-expression quick path), checker.go:7656-8090
// (contextual entry, expression cache, const-enum access, generic-instantiation
// fast path, checkExpressionWorker dispatch), and the diagnostics tail
// checker.go:14185-14219 + utilities.go:1716 (checkNotCanceled).
//
// Dep-stub convention (this file only): callees owned by other slices/files are
// defined at the bottom under "// === dep stubs ===" with TSC_UNREACHABLE bodies
// and an owner tag; each is deleted from here when the owner's real definition
// lands. Free functions owned by other files are given static definitions here —
// internal linkage, so they cannot collide with the owner's definition at merge.

#include "internal/checker/checker.h"
#include "internal/checker/mapper.h"
#include "internal/scanner/scanner.h"
#include "internal/jsnum/jsnum.h"
#include <algorithm>
#include <cstdlib>

namespace tsc::checker {

// ---------------------------------------------------------------------------
// Small free helpers (owned by this slice or marked for the owner)
// ---------------------------------------------------------------------------

// utilities.go:1128
static bool isCallChain(Node* node) {
	return isCallExpression(node) && (node->flags & NodeFlagsOptionalChain);
}

// utilities.go:1150 — owner: utilities slice (also defined here statically; internal linkage, no conflict)
static bool isInRightSideOfImportOrExportAssignment(Node* node) {
	while (node->parent->kind == Kind::QualifiedName) {
		node = node->parent;
	}
	return (node->parent->kind == Kind::ImportEqualsDeclaration &&
			node->parent->as<ImportEqualsDeclaration>()->ModuleReference == node) ||
		(node->parent->kind == Kind::ExportAssignment && node->parent->expression() == node);
}

// checker.go:28130 — owner: typeops slice (static here; internal linkage, no conflict)
static bool isConstEnumSymbol(Symbol* symbol) {
	return symbol->flags & SymbolFlagsConstEnum;
}

static bool isConstEnumObjectType(Type* t) {
	return (t->objectFlags & ObjectFlagsAnonymous) && t->symbol != nullptr && isConstEnumSymbol(t->symbol);
}

// utilities.go:1915 — owner: ast utilities (static; internal linkage, no conflict)
static bool isInstanceOfExpression(Node* node) {
	return isBinaryExpression(node) && node->as<BinaryExpression>()->OperatorToken->kind == Kind::InstanceOfKeyword;
}

// utilities.go:4065-4097 — owner: ast utilities (static; internal linkage, no conflict)
static Node* getJSDocRoot(Node* node) {
	return findAncestor(node->parent, [](Node* n) { return n->kind == Kind::JSDoc; });
}

static Node* getJSDocHost(Node* node) {
	Node* jsDoc = getJSDocRoot(node);
	if (jsDoc == nullptr) {
		return nullptr;
	}
	return jsDoc->parent;
}

static Node* getHostSignatureFromJSDoc(Node* node) {
	Node* host = getJSDocHost(node);
	if (host == nullptr) {
		return nullptr;
	}
	// !!! Strada's getEffectiveJSDocHost applies JS assignment pattern transforms (getSourceOfAssignment, getSourceOfDefaultedAssignment, etc.) not yet ported
	if (isPropertySignatureDeclaration(host) && host->type() != nullptr && isFunctionLikeKind(host->type()->kind)) {
		return host->type();
	}
	if (isFunctionLikeKind(host->kind)) {
		return host;
	}
	return nullptr;
}

// ast utilities: literal kinds (IsLiteralKind/IsLiteralExpression — ast_generated.go:9818, utilities.go:319)
static bool isLiteralKind(Kind kind) {
	return kind >= Kind::NumericLiteral && kind <= Kind::NoSubstitutionTemplateLiteral;
}

static bool isLiteralExpression(Node* node) {
	return isLiteralKind(node->kind);
}

static bool hasInferenceCandidates(InferenceInfo* info) {
	return !info->candidates.empty() || !info->contraCandidates.empty();
}

// inference.go:1626 — owner: inference slice (static; internal linkage, no conflict)
static InferenceInfo* newInferenceInfo(Type* typeParameter) {
	auto* info = new InferenceInfo();
	info->typeParameter = typeParameter;
	info->priority = InferencePriorityMaxValue;
	info->topLevel = true;
	info->impliedArity = -1;
	return info;
}

static bool hasTypeParameterByName(const std::vector<Type*>& typeParameters, const std::string& name) {
	for (Type* tp : typeParameters) {
		if (tp->symbol->name == name) {
			return true;
		}
	}
	return false;
}

static std::string getUniqueTypeParameterName(const std::vector<Type*>& typeParameters, std::string baseName) {
	while (baseName.size() > 1 && baseName.back() >= '0' && baseName.back() <= '9') {
		baseName.pop_back();
	}
	int index = 1;
	for (;;) {
		std::string augmentedName = baseName + std::to_string(index);
		if (!hasTypeParameterByName(typeParameters, augmentedName)) {
			return augmentedName;
		}
		index++;
	}
}


// ---------------------------------------------------------------------------
// Diagnostics tail — checker.go:14185-14219 + utilities.go:1716-1720
// ---------------------------------------------------------------------------

std::vector<Diagnostic*> Checker::GetDiagnostics(SourceFile* sourceFile) {
	return getDiagnostics(sourceFile, &diagnostics);
}

std::vector<Diagnostic*> Checker::GetSuggestionDiagnostics(SourceFile* sourceFile) {
	return getDiagnostics(sourceFile, &suggestionDiagnostics);
}

std::vector<Diagnostic*> Checker::getDiagnostics(SourceFile* sourceFile, DiagnosticsCollection* collection) {
	checkNotCanceled();
	bool checkUnused = tristateIsTrue(compilerOptions->NoUnusedLocals) || tristateIsTrue(compilerOptions->NoUnusedParameters) ||
		collection == &suggestionDiagnostics;
	checkSourceFile(sourceFile, checkUnused);
	if (wasCanceled) {
		return {};
	}
	return collection->GetDiagnosticsForFile(sourceFile);
}

std::vector<Diagnostic*> Checker::GetGlobalDiagnostics() {
	checkNotCanceled();
	produceDeferredDiagnostics();
	return diagnostics.GetGlobalDiagnostics();
}

void Checker::addDeferredDiagnostic(std::function<void()> callback) {
	deferredDiagnosticCallbacks.push_back(std::move(callback));
}

void Checker::produceDeferredDiagnostics() {
	for (auto& cb : deferredDiagnosticCallbacks) {
		cb();
	}
	deferredDiagnosticCallbacks.clear();
}

// utilities.go:1716
void Checker::checkNotCanceled() {
	if (wasCanceled) {
		TSC_UNREACHABLE("Checker was previously cancelled");
	}
}

// ---------------------------------------------------------------------------
// Check walker — checker.go:2237-2621
// ---------------------------------------------------------------------------

void Checker::checkSourceFile(SourceFile* sourceFile, bool checkUnused) {
	// Go sets c.ctx = ctx and clears it at the end; the C++ port has no
	// cancellation context (single-shot CLI), so isCanceled() reads wasCanceled.
	SourceFileLinks* links = sourceFileLinks.Get(sourceFile);
	if (!links->typeChecked) {
		// TRACING: defer tr.Push(tracing.PhaseCheck, "checkSourceFile", {"path": sourceFile.FileName()}, true)
		checkGrammarSourceFile(sourceFile);
		renamedBindingElementsInTypes.clear();
		checkSourceElements(sourceFile->Statements->nodes);
		checkDeferredNodes(sourceFile);
		if (isExternalOrCommonJSModule(sourceFile)) {
			checkExternalModuleExports(sourceFile);
			registerForUnusedIdentifiersCheck(sourceFile);
		}
		if (!sourceFile->IsDeclarationFile && !isCanceled()) {
			checkUnusedRenamedBindingElements();
		}
		produceDeferredDiagnostics();
		reportedUnreachableNodes.Clear();
		links->typeChecked = true;
	}
	if (checkUnused && !links->unusedChecked) {
		// The unused identifiers check relies on a full type check having first been performed
		if (!sourceFile->IsDeclarationFile && !isCanceled()) {
			checkUnusedIdentifiers(links->identifierCheckNodes);
		}
		links->unusedChecked = true;
	}
	if (isCanceled()) {
		wasCanceled = true;
	}
}

void Checker::checkSourceElements(const std::vector<Node*>& nodes) {
	for (Node* node : nodes) {
		if (isCanceled()) {
			break;
		}
		checkSourceElement(node);
	}
}

bool Checker::checkSourceElement(Node* node) {
	if (node != nullptr) {
		Node* saveCurrentNode = currentNode;
		bool saveWithinUnreachableCode = withinUnreachableCode;
		currentNode = node;
		instantiationCount = 0;
		checkSourceElementWorker(node);
		currentNode = saveCurrentNode;
		withinUnreachableCode = saveWithinUnreachableCode;
	}
	return false;
}

void Checker::checkSourceElementWorker(Node* node) {
	for (Node* jsdoc : node->eagerJSDoc()) {
		checkJSDocComments(jsdoc);
		if (NodeList* tags = jsdoc->as<JSDoc>()->Tags; tags != nullptr) {
			for (Node* tag : tags->nodes) {
				checkJSDocComments(tag);
			}
		}
	}

	if (!withinUnreachableCode && compilerOptions->AllowUnreachableCode != Tristate::True) {
		if (checkSourceElementUnreachable(node)) {
			withinUnreachableCode = true;
		}
	}

	switch (node->kind) {
	case Kind::TypeParameter:
		checkTypeParameter(node);
		break;
	case Kind::Parameter:
		checkParameter(node);
		break;
	case Kind::PropertyDeclaration:
		checkPropertyDeclaration(node);
		break;
	case Kind::PropertySignature:
		checkPropertySignature(node);
		break;
	case Kind::ConstructorType:
	case Kind::FunctionType:
	case Kind::CallSignature:
	case Kind::ConstructSignature:
	case Kind::IndexSignature:
		checkSignatureDeclaration(node);
		break;
	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
		checkMethodDeclaration(node);
		break;
	case Kind::ClassStaticBlockDeclaration:
		checkClassStaticBlockDeclaration(node);
		break;
	case Kind::Constructor:
		checkConstructorDeclaration(node);
		break;
	case Kind::GetAccessor:
	case Kind::SetAccessor:
		checkAccessorDeclaration(node);
		break;
	case Kind::TypeReference:
		checkTypeReferenceNode(node);
		break;
	case Kind::TypePredicate:
		checkTypePredicate(node);
		break;
	case Kind::TypeQuery:
		checkTypeQuery(node);
		break;
	case Kind::TypeLiteral:
		checkTypeLiteral(node);
		break;
	case Kind::ArrayType:
		checkArrayType(node);
		break;
	case Kind::TupleType:
		checkTupleType(node);
		break;
	case Kind::UnionType:
	case Kind::IntersectionType:
		checkUnionOrIntersectionType(node);
		break;
	case Kind::ParenthesizedType:
	case Kind::OptionalType:
	case Kind::RestType:
		node->forEachChild([this](Node* child) -> bool {
			checkSourceElement(child);
			return false;
		});
		break;
	case Kind::ThisType:
		checkThisType(node);
		break;
	case Kind::TypeOperator:
		checkTypeOperator(node);
		break;
	case Kind::ConditionalType:
		checkConditionalType(node);
		break;
	case Kind::InferType:
		checkInferType(node);
		break;
	case Kind::TemplateLiteralType:
		checkTemplateLiteralType(node);
		break;
	case Kind::ImportType:
		checkImportType(node);
		break;
	case Kind::NamedTupleMember:
		checkNamedTupleMember(node);
		break;
	case Kind::IndexedAccessType:
		checkIndexedAccessType(node);
		break;
	case Kind::MappedType:
		checkMappedType(node);
		break;
	case Kind::FunctionDeclaration:
		checkFunctionDeclaration(node);
		break;
	case Kind::Block:
	case Kind::ModuleBlock:
		checkBlock(node);
		break;
	case Kind::VariableStatement:
		checkVariableStatement(node);
		break;
	case Kind::ExpressionStatement:
		checkExpressionStatement(node);
		break;
	case Kind::IfStatement:
		checkIfStatement(node);
		break;
	case Kind::DoStatement:
		checkDoStatement(node);
		break;
	case Kind::WhileStatement:
		checkWhileStatement(node);
		break;
	case Kind::ForStatement:
		checkForStatement(node);
		break;
	case Kind::ForInStatement:
		checkForInStatement(node);
		break;
	case Kind::ForOfStatement:
		checkForOfStatement(node);
		break;
	case Kind::ContinueStatement:
	case Kind::BreakStatement:
		checkBreakOrContinueStatement(node);
		break;
	case Kind::ReturnStatement:
		checkReturnStatement(node);
		break;
	case Kind::WithStatement:
		checkWithStatement(node);
		break;
	case Kind::SwitchStatement:
		checkSwitchStatement(node);
		break;
	case Kind::LabeledStatement:
		checkLabeledStatement(node);
		break;
	case Kind::ThrowStatement:
		checkThrowStatement(node);
		break;
	case Kind::TryStatement:
		checkTryStatement(node);
		break;
	case Kind::VariableDeclaration:
		checkVariableDeclaration(node);
		break;
	case Kind::BindingElement:
		checkBindingElement(node);
		break;
	case Kind::ClassDeclaration:
		checkClassDeclaration(node);
		break;
	case Kind::InterfaceDeclaration:
		checkInterfaceDeclaration(node);
		break;
	case Kind::TypeAliasDeclaration:
	case Kind::JSTypeAliasDeclaration:
		checkTypeAliasDeclaration(node);
		break;
	case Kind::EnumDeclaration:
		checkEnumDeclaration(node);
		break;
	case Kind::EnumMember:
		checkEnumMember(node);
		break;
	case Kind::ModuleDeclaration:
		checkModuleDeclaration(node);
		break;
	case Kind::ImportDeclaration:
	case Kind::JSImportDeclaration:
		checkImportDeclaration(node);
		break;
	case Kind::ImportEqualsDeclaration:
		checkImportEqualsDeclaration(node);
		break;
	case Kind::ExportDeclaration:
		checkExportDeclaration(node);
		break;
	case Kind::ExportAssignment:
		checkExportAssignment(node);
		break;
	case Kind::EmptyStatement:
		checkGrammarStatementInAmbientContext(node);
		break;
	case Kind::DebuggerStatement:
		checkGrammarStatementInAmbientContext(node);
		break;
	case Kind::MissingDeclaration:
		checkMissingDeclaration(node);
		break;
	case Kind::JSDocNonNullableType:
	case Kind::JSDocNullableType:
	case Kind::JSDocAllType:
	case Kind::JSDocTypeLiteral:
		checkJSDocType(node);
		break;
	}
}

bool Checker::checkSourceElementUnreachable(Node* node) {
	if (!isPotentiallyExecutableNode(node)) {
		return false;
	}

	if (reportedUnreachableNodes.Has(node)) {
		return true;
	}

	if (!isSourceElementUnreachable(node)) {
		return false;
	}

	reportedUnreachableNodes.Add(node);

	SourceFile* sourceFile = getSourceFileOfNode(node);

	Node* startNode = node;
	Node* endNode = node;

	Node* parent = node->parent;
	if (parent != nullptr && parent->canHaveStatements()) {
		std::vector<Node*> statements = parent->statements();
		auto it = std::find(statements.begin(), statements.end(), node);
		if (it != statements.end()) {
			size_t offset = static_cast<size_t>(it - statements.begin());
			// Scan backwards to find the first unreachable unreported node;
			// this may happen when producing region diagnostics where not all nodes
			// will have been visited.
			// TODO: enable this code once we support region diagnostics again.
			size_t first = offset;
			// for i := offset - 1; i >= 0; i-- {
			// 	prevNode := statements[i]
			// 	if !ast.IsPotentiallyExecutableNode(prevNode) || c.reportedUnreachableNodes.Has(prevNode) || !c.isSourceElementUnreachable(prevNode) {
			// 		break
			// 	}
			// 	firstUnreachableIndex = i
			// 	c.reportedUnreachableNodes.Add(prevNode)
			// }

			size_t last = offset;
			for (size_t i = offset + 1; i < statements.size(); i++) {
				Node* nextNode = statements[i];
				if (!isPotentiallyExecutableNode(nextNode) || !isSourceElementUnreachable(nextNode)) {
					break;
				}
				last = i;
				reportedUnreachableNodes.Add(nextNode);
			}

			startNode = statements[first];
			endNode = statements[last];
		}
	}

	TextPos start = getTokenPosOfNode(startNode, sourceFile, false /*includeJSDoc*/);

	Diagnostic* diagnostic = newDiagnostic(sourceFile, TextRange{start, endNode->end()}, Unreachable_code_detected);
	addErrorOrSuggestion(compilerOptions->AllowUnreachableCode == Tristate::False, diagnostic);

	return true;
}

bool Checker::isSourceElementUnreachable(Node* node) {
	// Precondition: ast.IsPotentiallyExecutableNode is true
	if (node->flags & NodeFlagsUnreachable) {
		// The binder has determined that this code is unreachable.
		// Ignore const enums unless preserveConstEnums is set.
		switch (node->kind) {
		case Kind::EnumDeclaration:
			return !isEnumConst(node) || compilerOptions->ShouldPreserveConstEnums();
		case Kind::ModuleDeclaration:
			return isInstantiatedModule(node, compilerOptions->ShouldPreserveConstEnums());
		default:
			return true;
		}
	} else if (FlowNode* flowNode = *node->flowNodeData().flowNode; flowNode != nullptr) {
		// For code the binder doesn't know is unreachable, use control flow / types.
		return !isReachableFlowNode(flowNode);
	}
	return false;
}

// Function and class expression bodies are checked after all statements in the enclosing body. This is
// to ensure constructs like the following are permitted:
//
//	const foo = function () {
//	   const s = foo();
//	   return "hello";
//	}
//
// Here, performing a full type check of the body of the function expression whilst in the process of
// determining the type of foo would cause foo to be given type any because of the recursive reference.
// Delaying the type check of the body ensures foo has been assigned a type.
void Checker::checkNodeDeferred(Node* node) {
	SourceFile* enclosingFile = getSourceFileOfNode(node);
	SourceFileLinks* links = sourceFileLinks.Get(enclosingFile);
	if (!links->typeChecked) {
		if (links->deferredNodesSet.insert(node).second) {
			links->deferredNodes.push_back(node);
		}
	}
}

void Checker::checkDeferredNodes(SourceFile* context) {
	SourceFileLinks* links = sourceFileLinks.Get(context);
	for (Node* node : links->deferredNodes) {
		if (isCanceled()) {
			break;
		}
		checkDeferredNode(node);
	}
	links->deferredNodes.clear();
	links->deferredNodesSet.clear();
}

void Checker::checkDeferredNode(Node* node) {
	// TRACING: defer tr.Push(tracing.PhaseCheck, "checkDeferredNode", {"kind": node.Kind, "pos": node.Pos(), "end": node.End(), "path": ast.GetSourceFileOfNode(node).FileName()}, false)
	Node* saveCurrentNode = currentNode;
	currentNode = node;
	instantiationCount = 0;
	switch (node->kind) {
	case Kind::CallExpression:
	case Kind::NewExpression:
	case Kind::TaggedTemplateExpression:
	case Kind::Decorator:
	case Kind::JsxOpeningElement:
		// These node kinds are deferred checked when overload resolution fails. To save on work,
		// we ensure the arguments are checked just once in a deferred way.
		resolveUntypedCall(node);
		break;
	case Kind::FunctionExpression:
	case Kind::ArrowFunction:
	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
		checkFunctionExpressionOrObjectLiteralMethodDeferred(node);
		break;
	case Kind::GetAccessor:
	case Kind::SetAccessor:
		checkAccessorDeclaration(node);
		break;
	case Kind::ClassExpression:
		checkClassExpressionDeferred(node);
		break;
	case Kind::TypeParameter:
		checkTypeParameterDeferred(node);
		break;
	case Kind::JsxSelfClosingElement:
		checkJsxSelfClosingElementDeferred(node);
		break;
	case Kind::JsxElement:
		checkJsxElementDeferred(node);
		break;
	case Kind::TypeAssertionExpression:
	case Kind::AsExpression:
		checkAssertionDeferred(node);
		break;
	case Kind::VoidExpression:
		checkExpression(node->expression());
		break;
	case Kind::BinaryExpression:
		if (isInstanceOfExpression(node)) {
			resolveUntypedCall(node);
		}
		break;
	case Kind::ObjectLiteralExpression:
	case Kind::JsxAttributes:
		checkContextualDeprecations(node);
		break;
	}
	currentNode = saveCurrentNode;
}

void Checker::checkJSDocComments(Node* node) {
	for (Node* comment : node->comments()) {
		checkJSDocComment(comment);
	}
}

void Checker::checkJSDocComment(Node* node) {
	// This performs minimal checking of JSDoc nodes to ensure that @link references to entities are recorded
	// for purposes of checking unused identifiers.
	switch (node->kind) {
	case Kind::JSDocLink:
	case Kind::JSDocLinkCode:
	case Kind::JSDocLinkPlain:
		resolveJSDocMemberName(node->name());
	}
}

Symbol* Checker::resolveJSDocMemberName(Node* name) {
	if (name != nullptr && isEntityName(name)) {
		SymbolFlags meaning = SymbolFlagsType | SymbolFlagsNamespace | SymbolFlagsValue;
		if (Symbol* symbol = resolveEntityName(name, meaning, true /*ignoreErrors*/, true /*dontResolveAlias*/,
											   getHostSignatureFromJSDoc(name));
			symbol != nullptr) {
			return symbol;
		}
		if (isQualifiedName(name)) {
			if (Symbol* symbol = resolveJSDocMemberName(name->as<QualifiedName>()->Left); symbol != nullptr) {
				Type* t = nullptr;
				if (symbol->flags & SymbolFlagsValue) {
					Symbol* proto = getPropertyOfType(getTypeOfSymbol(symbol), "prototype");
					if (proto != nullptr) {
						t = getTypeOfSymbol(proto);
					}
				}
				if (t == nullptr) {
					t = getDeclaredTypeOfSymbol(symbol);
				}
				return getPropertyOfType(t, name->as<QualifiedName>()->Right->text());
			}
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// Type-of-expression quick path — checker.go:7509-7576
// ---------------------------------------------------------------------------

Type* Checker::getTypeOfExpression(Node* node) {
	// Don't bother caching types that require no flow analysis and are quick to compute.
	Type* quickType = getQuickTypeOfExpression(node);
	if (quickType != nullptr) {
		return quickType;
	}
	// If a type has been cached for the node, return it.
	if (Type* cachedType = flowTypeCache[node]; cachedType != nullptr) {
		return cachedType;
	}
	int startInvocationCount = flowInvocationCount;
	Type* t = checkExpressionEx(node, CheckModeTypeOnly);
	// If control flow analysis was required to determine the type, it is worth caching.
	if (flowInvocationCount != startInvocationCount) {
		flowTypeCache[node] = t;
	}
	return t;
}

// Returns the type of an expression. Unlike checkExpression, this function is simply concerned
// with computing the type and may not fully check all contained sub-expressions for errors.
Type* Checker::getQuickTypeOfExpression(Node* node) {
	Node* expr = skipParentheses(node);
	if (isAwaitExpression(expr)) {
		Type* t = getQuickTypeOfExpression(expr->expression());
		if (t != nullptr) {
			return getAwaitedType(t);
		}
		return nullptr;
	}
	// Optimize for the common case of a call to a function with a single non-generic call
	// signature where we can just fetch the return type without checking the arguments.
	if (isCallExpression(expr) && expr->expression()->kind != Kind::SuperKeyword &&
		!isRequireCall(expr, true /*requireStringLiteralLikeArgument*/) &&
		!isSymbolOrSymbolForCall(expr) && !isImportCall(expr)) {
		if (isCallChain(expr)) {
			return getReturnTypeOfSingleNonGenericSignatureOfCallChain(expr);
		}
		return getReturnTypeOfSingleNonGenericSignature(checkNonNullExpression(expr->expression()),
														SignatureKind::Call);
	}
	if (isNewExpression(expr)) {
		return getReturnTypeOfSingleNonGenericSignature(checkNonNullExpression(expr->expression()),
														SignatureKind::Construct);
	}
	if (isAssertionExpression(expr) && !isConstTypeReference(expr->type())) {
		return getTypeFromTypeNode(expr->type());
	}
	if (isLiteralExpression(node) || isBooleanLiteral(node)) {
		return checkExpression(node);
	}
	return nullptr;
}

Type* Checker::getReturnTypeOfSingleNonGenericSignature(Type* funcType, SignatureKind kind) {
	Signature* signature = getSingleSignature(funcType, kind, true /*allowMembers*/);
	if (signature != nullptr && signature->typeParameters.empty()) {
		return getReturnTypeOfSignature(signature);
	}
	return nullptr;
}

Type* Checker::getReturnTypeOfSingleNonGenericSignatureOfCallChain(Node* expr) {
	Type* funcType = checkExpression(expr->expression());
	Type* nonOptionalType = getOptionalExpressionType(funcType, expr->expression());
	Type* returnType = getReturnTypeOfSingleNonGenericSignature(funcType, SignatureKind::Call);
	if (returnType != nullptr) {
		return propagateOptionalTypeMarker(returnType, expr, nonOptionalType != funcType);
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// Contextual entry + expression cache + dispatch — checker.go:7656-8008
// ---------------------------------------------------------------------------

Type* Checker::checkExpressionWithContextualType(Node* node, Type* contextualType,
												 InferenceContext* inferenceContext,
												 CheckMode checkMode) {
	Node* contextNode = getContextNode(node);
	pushContextualType(contextNode, contextualType, false /*isCache*/);
	pushInferenceContext(contextNode, inferenceContext);
	Type* t = checkExpressionEx(node,
								checkMode | CheckModeContextual |
									(inferenceContext != nullptr ? CheckModeInferential : CheckModeNormal));
	// In CheckMode.Inferential we collect intra-expression inference sites to process before fixing any type
	// parameters. This information is no longer needed after the call to checkExpression.
	if (inferenceContext != nullptr) {
		inferenceContext->intraExpressionInferenceSites.clear();
	}
	// We strip literal freshness when an appropriate contextual type is present such that contextually typed
	// literals always preserve their literal types (otherwise they might widen during type inference). An alternative
	// here would be to not mark contextually typed literals as fresh in the first place.
	if (maybeTypeOfKind(t, TypeFlagsLiteral) &&
		isLiteralOfContextualType(t, instantiateContextualType(contextualType, node, ContextFlagsNone))) {
		t = getRegularTypeOfLiteralType(t);
	}
	popInferenceContext();
	popContextualType();
	return t;
}

Node* Checker::getContextNode(Node* node) {
	if (isJsxAttributes(node) && !isJsxSelfClosingElement(node->parent)) {
		// Needs to be the root JsxElement, so it encompasses the attributes _and_ the children (which are essentially part of the attributes)
		return node->parent->parent;
	}
	return node;
}

Type* Checker::checkExpressionCachedEx(Node* node, CheckMode checkMode) {
	if (checkMode != CheckModeNormal) {
		return checkExpressionEx(node, checkMode);
	}
	TypeNodeLinks* links = typeNodeLinks.Get(node);
	if (links->resolvedType == nullptr) {
		// When computing a type that we're going to cache, we need to ignore any ongoing control flow
		// analysis because variables may have transient types in indeterminable states. Moving flowLoopStart
		// to the top of the stack ensures all transient types are computed from a known point.
		auto saveFlowLoopStack = std::move(flowLoopStack);
		auto saveFlowTypeCache = std::move(flowTypeCache);
		flowLoopStack.clear();
		flowTypeCache.clear();
		links->resolvedType = checkExpressionEx(node, checkMode);
		flowTypeCache = std::move(saveFlowTypeCache);
		flowLoopStack = std::move(saveFlowLoopStack);
	}
	return links->resolvedType;
}

// Returns the type of an expression. Unlike checkExpression, this function is simply concerned
// with computing the type and may not fully check all contained sub-expressions for errors.
// It is intended for uses where you know there is no contextual type,
// and requesting the contextual type might cause a circularity or other bad behaviour.
// It sets the contextual type of the node to any before calling getTypeOfExpression.
Type* Checker::getContextFreeTypeOfExpression(Node* node) {
	if (Type* cached = contextFreeTypes[node]; cached != nullptr) {
		return cached;
	}
	pushContextualType(node, anyType, false /*isCache*/);
	Type* t = checkExpressionEx(node, CheckModeSkipContextSensitive);
	contextFreeTypes[node] = t;
	popContextualType();
	return t;
}

Type* Checker::checkExpressionEx(Node* node, CheckMode checkMode) {
	// TRACING: defer tr.Push(tracing.PhaseCheck, "checkExpression", {"kind": node.Kind, "pos": node.Pos(), "end": node.End(), "path": ast.GetSourceFileOfNode(node).FileName()}, false)
	Node* saveCurrentNode = currentNode;
	currentNode = node;
	instantiationCount = 0;
	Type* uninstantiatedType = checkExpressionWorker(node, checkMode);
	Type* t = instantiateTypeWithSingleGenericCallSignature(node, uninstantiatedType, checkMode);
	if (isConstEnumObjectType(t)) {
		checkConstEnumAccess(node, t);
	}
	currentNode = saveCurrentNode;
	return t;
}

void Checker::checkConstEnumAccess(Node* node, Type* t) {
	// enum object type for const enums are only permitted in:
	// - 'left' in property access
	// - 'object' in indexed access
	// - target in rhs of import statement
	bool ok = isPropertyAccessExpression(node->parent) && node->parent->expression() == node ||
		isElementAccessExpression(node->parent) && node->parent->expression() == node ||
		((isIdentifier(node) || isQualifiedName(node)) && isInRightSideOfImportOrExportAssignment(node) ||
		 isTypeQueryNode(node->parent) && node->parent->as<TypeQueryNode>()->ExprName == node) ||
		isExportSpecifier(node->parent); // We allow reexporting const enums
	if (!ok) {
		error(node, X_const_enums_can_only_be_used_in_property_or_index_access_expressions_or_the_right_hand_side_of_an_import_declaration_or_export_assignment_or_type_query);
	}
	// --verbatimModuleSyntax only gets checked here when the enum usage does not
	// resolve to an import, because imports of ambient const enums get checked
	// separately in `checkAliasSymbol`.
	if ((tristateIsTrue(compilerOptions->IsolatedModules) ||
		 tristateIsTrue(compilerOptions->VerbatimModuleSyntax)) && ok &&
		resolveName(node, getFirstIdentifier(node)->text(), SymbolFlagsAlias, nullptr, false, true) == nullptr) {
		TSC_ASSERT(t->symbol->flags & SymbolFlagsConstEnum, "isolated const-enum check assumes const-enum symbol");
		Node* constEnumDeclaration = t->symbol->valueDeclaration;
		auto* redirect = program->GetProjectReferenceFromOutputDts(getSourceFileOfNode(constEnumDeclaration)->Path());
		if (constEnumDeclaration->flags & NodeFlagsAmbient &&
			!isValidTypeOnlyAliasUseSite(node) &&
			(redirect == nullptr ||
			 !redirect->resolved->CompilerOptions()->ShouldPreserveConstEnums())) {
			error(node, Cannot_access_ambient_const_enums_when_0_is_enabled, {getIsolatedModulesLikeFlagName()});
		}
	}
}

Type* Checker::instantiateTypeWithSingleGenericCallSignature(Node* node, Type* t, CheckMode checkMode) {
	if ((checkMode & (CheckModeInferential | CheckModeSkipGenericFunctions)) == CheckModeNormal) {
		return t;
	}
	Signature* callSignature = getSingleSignature(t, SignatureKind::Call, true /*allowMembers*/);
	Signature* constructSignature = getSingleSignature(t, SignatureKind::Construct, true /*allowMembers*/);
	Signature* signature = callSignature != nullptr ? callSignature : constructSignature;
	if (signature == nullptr || signature->typeParameters.empty()) {
		return t;
	}
	Type* contextualType = getApparentTypeOfContextualType(node, ContextFlagsNoConstraints);
	if (contextualType == nullptr) {
		return t;
	}
	Signature* contextualSignature = getSingleSignature(
		GetNonNullableType(contextualType),
		callSignature != nullptr ? SignatureKind::Call : SignatureKind::Construct,
		false /*allowMembers*/);
	if (contextualSignature == nullptr || !contextualSignature->typeParameters.empty()) {
		return t;
	}
	if (checkMode & CheckModeSkipGenericFunctions) {
		skippedGenericFunction(node, checkMode);
		return anyFunctionType;
	}
	InferenceContext* context = getInferenceContext(node);
	// We have an expression that is an argument of a generic function for which we are performing
	// type argument inference. The expression is of a function type with a single generic call
	// signature and a contextual function type with a single non-generic call signature. Now check
	// if the outer function returns a function type with a single non-generic call signature and
	// if some of the outer function type parameters have no inferences so far. If so, we can
	// potentially add inferred type parameters to the outer function return type.
	Signature* returnSignature = nullptr;
	if (context->signature != nullptr) {
		Type* returnType = getReturnTypeOfSignature(context->signature);
		if (returnType != nullptr) {
			returnSignature = getSingleCallOrConstructSignature(returnType);
		}
	}
	if (returnSignature != nullptr && returnSignature->typeParameters.empty() &&
		!std::all_of(context->inferences.begin(), context->inferences.end(),
					 [](InferenceInfo* info) { return hasInferenceCandidates(info); })) {
		// Instantiate the signature with its own type parameters as type arguments, possibly
		// renaming the type parameters to ensure they have unique names.
		std::vector<Type*> uniqueTypeParameters = getUniqueTypeParameters(context, signature->typeParameters);
		Signature* instantiatedSignature =
			getSignatureInstantiationWithoutFillingInTypeArguments(signature, uniqueTypeParameters);
		// Infer from the parameters of the instantiated signature to the parameters of the
		// contextual signature starting with an empty set of inference candidates.
		std::vector<InferenceInfo*> inferences;
		inferences.reserve(context->inferences.size());
		for (InferenceInfo* info : context->inferences) {
			inferences.push_back(newInferenceInfo(info->typeParameter));
		}
		applyToParameterTypes(instantiatedSignature, contextualSignature, [this, &inferences](Type* source, Type* target) {
			inferTypes(inferences, source, target, InferencePriorityNone, true /*contravariant*/);
		});
		if (std::any_of(inferences.begin(), inferences.end(),
						[](InferenceInfo* info) { return hasInferenceCandidates(info); })) {
			// We have inference candidates, indicating that one or more type parameters are referenced
			// in the parameter types of the contextual signature. Now also infer from the return type.
			applyToReturnTypes(instantiatedSignature, contextualSignature, [this, &inferences](Type* source, Type* target) {
				inferTypes(inferences, source, target, InferencePriorityNone, false);
			});
			// If the type parameters for which we produced candidates do not have any inferences yet,
			// we adopt the new inference candidates and add the type parameters of the expression type
			// to the set of inferred type parameters for the outer function return type.
			if (!hasOverlappingInferences(context->inferences, inferences)) {
				mergeInferences(context->inferences, inferences);
				context->inferredTypeParameters.insert(context->inferredTypeParameters.end(),
													   uniqueTypeParameters.begin(), uniqueTypeParameters.end());
				return getOrCreateTypeFromSignature(instantiatedSignature);
			}
		}
	}
	// TODO: The signature may reference any outer inference contexts, but we map pop off and then apply new inference contexts,
	// and thus get different inferred types. That this is cached on the *first* such attempt is not currently an issue, since expression
	// types *also* get cached on the first pass. If we ever properly speculate, though, the cached "isolatedSignatureType" signature
	// field absolutely needs to be included in the list of speculative caches.
	return getOrCreateTypeFromSignature(instantiateSignatureInContextOf(signature, contextualSignature, context, nullptr));
}

std::vector<Type*> Checker::getOuterInferenceTypeParameters() {
	std::vector<Type*> result;
	for (auto& info : inferenceContextInfos) {
		InferenceContext* context = info.context;
		if (context != nullptr) {
			for (InferenceInfo* inference : context->inferences) {
				result.push_back(inference->typeParameter);
			}
		}
	}
	return result;
}

std::vector<Type*> Checker::getUniqueTypeParameters(InferenceContext* context,
												  const std::vector<Type*>& typeParameters) {
	std::vector<Type*> oldTypeParameters;
	std::vector<Type*> newTypeParameters;
	std::vector<Type*> result;
	result.reserve(typeParameters.size());
	for (Type* tp : typeParameters) {
		const std::string& name = tp->symbol->name;
		if (hasTypeParameterByName(context->inferredTypeParameters, name) ||
			hasTypeParameterByName(result, name)) {
			std::vector<Type*> combined = context->inferredTypeParameters;
			combined.insert(combined.end(), result.begin(), result.end());
			std::string newName = getUniqueTypeParameterName(combined, name);
			Symbol* symbol = newSymbol(SymbolFlagsTypeParameter, newName);
			Type* newTypeParameter = this->newTypeParameter(symbol);
			newTypeParameter->AsTypeParameter()->target = tp;
			oldTypeParameters.push_back(tp);
			newTypeParameters.push_back(newTypeParameter);
			result.push_back(newTypeParameter);
		} else {
			result.push_back(tp);
		}
	}
	if (!newTypeParameters.empty()) {
		TypeMapper* mapper = newTypeMapper(oldTypeParameters, newTypeParameters);
		for (Type* tp : newTypeParameters) {
			tp->AsTypeParameter()->mapper = mapper;
		}
	}
	return result;
}

Type* Checker::checkExpressionWorker(Node* node, CheckMode checkMode) {
	switch (node->kind) {
	case Kind::Identifier:
		return checkIdentifier(node, checkMode);
	case Kind::PrivateIdentifier:
		return checkPrivateIdentifierExpression(node);
	case Kind::ThisKeyword:
		return checkThisExpression(node);
	case Kind::SuperKeyword:
		return checkSuperExpression(node);
	case Kind::NullKeyword:
		return nullWideningType;
	case Kind::StringLiteral:
	case Kind::NoSubstitutionTemplateLiteral:
		if (isSkipDirectInferenceNode(node)) {
			return blockedStringType;
		}
		return getFreshTypeOfLiteralType(getStringLiteralType(node->text()));
	case Kind::NumericLiteral:
		checkGrammarNumericLiteral(node->as<NumericLiteral>());
		return getFreshTypeOfLiteralType(getNumberLiteralType(numberFromString(node->text())));
	case Kind::BigIntLiteral:
		checkGrammarBigIntLiteral(node->as<BigIntLiteral>());
		return getFreshTypeOfLiteralType(
			getBigIntLiteralType(PseudoBigInt::create(parsePseudoBigInt(node->text()), false /*negative*/)));
	case Kind::TrueKeyword:
		return trueType;
	case Kind::FalseKeyword:
		return falseType;
	case Kind::TemplateExpression:
		return checkTemplateExpression(node);
	case Kind::RegularExpressionLiteral:
		return checkRegularExpressionLiteral(node);
	case Kind::ArrayLiteralExpression:
		return checkArrayLiteral(node, checkMode);
	case Kind::ObjectLiteralExpression:
		return checkObjectLiteral(node, checkMode);
	case Kind::PropertyAccessExpression:
		return checkPropertyAccessExpression(node, checkMode, false /*writeOnly*/);
	case Kind::QualifiedName:
		return checkQualifiedName(node, checkMode);
	case Kind::ElementAccessExpression:
		return checkIndexedAccess(node, checkMode);
	case Kind::CallExpression:
		if (isImportCall(node)) {
			return checkImportCallExpression(node);
		}
		return checkCallExpression(node, checkMode);
	case Kind::NewExpression:
		return checkCallExpression(node, checkMode);
	case Kind::TaggedTemplateExpression:
		return checkTaggedTemplateExpression(node);
	case Kind::ParenthesizedExpression:
		return checkParenthesizedExpression(node, checkMode);
	case Kind::ClassExpression:
		return checkClassExpression(node);
	case Kind::FunctionExpression:
	case Kind::ArrowFunction:
		return checkFunctionExpressionOrObjectLiteralMethod(node, checkMode);
	case Kind::TypeAssertionExpression:
	case Kind::AsExpression:
		return checkAssertion(node, checkMode);
	case Kind::TypeOfExpression:
		return checkTypeOfExpression(node);
	case Kind::NonNullExpression:
		return checkNonNullAssertion(node);
	case Kind::ExpressionWithTypeArguments:
		return checkExpressionWithTypeArguments(node);
	case Kind::SatisfiesExpression:
		return checkSatisfiesExpression(node);
	case Kind::MetaProperty:
		return checkMetaProperty(node);
	case Kind::DeleteExpression:
		return checkDeleteExpression(node);
	case Kind::VoidExpression:
		return checkVoidExpression(node);
	case Kind::AwaitExpression:
		return checkAwaitExpression(node);
	case Kind::PrefixUnaryExpression:
		return checkPrefixUnaryExpression(node);
	case Kind::PostfixUnaryExpression:
		return checkPostfixUnaryExpression(node);
	case Kind::BinaryExpression:
		return checkBinaryExpression(node, checkMode);
	case Kind::ConditionalExpression:
		return checkConditionalExpression(node, checkMode);
	case Kind::SpreadElement:
		return checkSpreadExpression(node, checkMode);
	case Kind::OmittedExpression:
		return undefinedWideningType;
	case Kind::YieldExpression:
		return checkYieldExpression(node);
	case Kind::SyntheticExpression:
		return checkSyntheticExpression(node);
	case Kind::JsxExpression:
		return checkJsxExpression(node, checkMode);
	case Kind::JsxElement:
		return checkJsxElement(node, checkMode);
	case Kind::JsxSelfClosingElement:
		return checkJsxSelfClosingElement(node, checkMode);
	case Kind::JsxFragment:
		return checkJsxFragment(node);
	case Kind::JsxAttributes:
		return checkJsxAttributes(node, checkMode);
	case Kind::JsxOpeningElement:
		TSC_UNREACHABLE("Should never directly check a JsxOpeningElement");
	}
	return errorType;
}

Type* Checker::checkPrivateIdentifierExpression(Node* node) {
	checkGrammarPrivateIdentifierExpression(node->as<PrivateIdentifier>());
	Symbol* symbol = getSymbolForPrivateIdentifierExpression(node);
	if (symbol != nullptr) {
		markPropertyAsReferenced(symbol, nullptr /*nodeForCheckWriteOnly*/, false /*isSelfTypeAccess*/);
	}
	return anyType;
}

// (getSymbolForPrivateIdentifierExpression moved to the grammarchecks slice's
// file — identical ports; theirs is the surviving definition.)

// checker.go:10217
void Checker::skippedGenericFunction(Node* node, CheckMode checkMode) {
	if (checkMode & CheckModeInferential) {
		// We have skipped a generic function during inferential typing. Obtain the inference context and
		// indicate this has occurred such that we know a second pass of inference is be needed.
		InferenceContext* context = getInferenceContext(node);
		context->flags |= InferenceFlagsSkippedGenericFunction;
	}
}

// ---------------------------------------------------------------------------
// === dep stubs — owned by other slices; deleted from here when the owner's
// real definition lands. Never called successfully until then.
// ---------------------------------------------------------------------------

// (grammar dep-stubs removed — grammarchecks slice landed the real definitions.)

// owner: flow slice (flow.go)

// owner: contextual slice










// owner: contextual slice
// (deduped: getAwaitedType defined in cpp/internal/checker/checker_contextual.cpp)

// (deduped: getOptionalExpressionType defined in cpp/internal/checker/checker_contextual.cpp)

// (deduped: propagateOptionalTypeMarker defined in cpp/internal/checker/checker_contextual.cpp)

// (deduped: getApparentTypeOfContextualType defined in cpp/internal/checker/checker_contextual.cpp)

// (deduped: pushContextualType defined in cpp/internal/checker/checker_contextual.cpp)

// (deduped: popContextualType defined in cpp/internal/checker/checker_contextual.cpp)

// (deduped: pushInferenceContext defined in cpp/internal/checker/checker_contextual.cpp)

// (deduped: popInferenceContext defined in cpp/internal/checker/checker_contextual.cpp)

// (deduped: getInferenceContext defined in cpp/internal/checker/checker_contextual.cpp)

// (deduped: instantiateContextualType defined in cpp/internal/checker/checker_contextual.cpp)


// owner: typenodes slice

// owner: decltypes slice (checker.go:16720-19097)
// (deduped: GetNonNullableType defined in cpp/internal/checker/checker_decltypes.cpp)

// widen slice landed in checker_widen.cpp — its stubs here were removed.


// owner: signatures slice

// owner: decltypes slice

// maybeTypeOfKind / markPropertyAsReferenced — ported in checker_typeops.cpp
// (typeops slice).

// owner: inference slice (inference.go)
bool Checker::isSkipDirectInferenceNode(Node* node) { TSC_UNREACHABLE("isSkipDirectInferenceNode — inference slice"); }
bool Checker::hasOverlappingInferences(std::vector<InferenceInfo*>& a, std::vector<InferenceInfo*>& b) { TSC_UNREACHABLE("hasOverlappingInferences — inference slice"); }
void Checker::mergeInferences(std::vector<InferenceInfo*>& target, const std::vector<InferenceInfo*>& source) { TSC_UNREACHABLE("mergeInferences — inference slice"); }
// `newInferenceInfo` / `hasInferenceCandidates` are free fns (inference.go:1626,1651):
// defined statically above until the inference slice lands its own copies.

// owner: expression-check slices (wave-3)
Type* Checker::checkIdentifier(Node* node, CheckMode checkMode) { TSC_UNREACHABLE("checkIdentifier — expressions slice"); }
Type* Checker::checkThisExpression(Node* node) { TSC_UNREACHABLE("checkThisExpression — expressions slice"); }
Type* Checker::checkSuperExpression(Node* node) { TSC_UNREACHABLE("checkSuperExpression — expressions slice"); }
Type* Checker::checkTemplateExpression(Node* node) { TSC_UNREACHABLE("checkTemplateExpression — expressions slice"); }
Type* Checker::checkRegularExpressionLiteral(Node* node) { TSC_UNREACHABLE("checkRegularExpressionLiteral — expressions slice"); }
Type* Checker::checkArrayLiteral(Node* node, CheckMode checkMode) { TSC_UNREACHABLE("checkArrayLiteral — expressions slice"); }
// (deduped: checkObjectLiteral defined in cpp/internal/checker/checker_expressions_c.cpp)
Type* Checker::checkPropertyAccessExpression(Node* node, CheckMode checkMode, bool writeOnly) { TSC_UNREACHABLE("checkPropertyAccessExpression — expressions slice"); }
Type* Checker::checkQualifiedName(Node* node, CheckMode checkMode) { TSC_UNREACHABLE("checkQualifiedName — expressions slice"); }
Type* Checker::checkIndexedAccess(Node* node, CheckMode checkMode) { TSC_UNREACHABLE("checkIndexedAccess — expressions slice"); }
Type* Checker::checkCallExpression(Node* node, CheckMode checkMode) { TSC_UNREACHABLE("checkCallExpression — expressions slice"); }
Type* Checker::checkImportCallExpression(Node* node) { TSC_UNREACHABLE("checkImportCallExpression — expressions slice"); }
Type* Checker::checkTaggedTemplateExpression(Node* node) { TSC_UNREACHABLE("checkTaggedTemplateExpression — expressions slice"); }
Type* Checker::checkParenthesizedExpression(Node* node, CheckMode checkMode) { TSC_UNREACHABLE("checkParenthesizedExpression — expressions slice"); }
Type* Checker::checkClassExpression(Node* node) { TSC_UNREACHABLE("checkClassExpression — expressions slice"); }
Type* Checker::checkFunctionExpressionOrObjectLiteralMethod(Node* node, CheckMode checkMode) { TSC_UNREACHABLE("checkFunctionExpressionOrObjectLiteralMethod — expressions slice"); }
// (deduped: checkAssertion defined in cpp/internal/checker/checker_expressions_c.cpp)
Type* Checker::checkTypeOfExpression(Node* node) { TSC_UNREACHABLE("checkTypeOfExpression — expressions slice"); }
Type* Checker::checkNonNullAssertion(Node* node) { TSC_UNREACHABLE("checkNonNullAssertion — expressions slice"); }
Type* Checker::checkSatisfiesExpression(Node* node) { TSC_UNREACHABLE("checkSatisfiesExpression — expressions slice"); }
Type* Checker::checkMetaProperty(Node* node) { TSC_UNREACHABLE("checkMetaProperty — expressions slice"); }
Type* Checker::checkDeleteExpression(Node* node) { TSC_UNREACHABLE("checkDeleteExpression — expressions slice"); }
Type* Checker::checkVoidExpression(Node* node) { TSC_UNREACHABLE("checkVoidExpression — expressions slice"); }
Type* Checker::checkAwaitExpression(Node* node) { TSC_UNREACHABLE("checkAwaitExpression — expressions slice"); }
Type* Checker::checkPrefixUnaryExpression(Node* node) { TSC_UNREACHABLE("checkPrefixUnaryExpression — expressions slice"); }
Type* Checker::checkPostfixUnaryExpression(Node* node) { TSC_UNREACHABLE("checkPostfixUnaryExpression — expressions slice"); }
// (deduped: checkBinaryExpression defined in cpp/internal/checker/checker_expressions_c.cpp)
Type* Checker::checkConditionalExpression(Node* node, CheckMode checkMode) { TSC_UNREACHABLE("checkConditionalExpression — expressions slice"); }
Type* Checker::checkSpreadExpression(Node* node, CheckMode checkMode) { TSC_UNREACHABLE("checkSpreadExpression — expressions slice"); }
Type* Checker::checkYieldExpression(Node* node) { TSC_UNREACHABLE("checkYieldExpression — expressions slice"); }
Type* Checker::checkSyntheticExpression(Node* node) { TSC_UNREACHABLE("checkSyntheticExpression — expressions slice"); }
Type* Checker::checkJsxExpression(Node* node, CheckMode checkMode) { TSC_UNREACHABLE("checkJsxExpression — jsx slice"); }
Type* Checker::checkJsxElement(Node* node, CheckMode checkMode) { TSC_UNREACHABLE("checkJsxElement — jsx slice"); }
Type* Checker::checkJsxSelfClosingElement(Node* node, CheckMode checkMode) { TSC_UNREACHABLE("checkJsxSelfClosingElement — jsx slice"); }
Type* Checker::checkJsxFragment(Node* node) { TSC_UNREACHABLE("checkJsxFragment — jsx slice"); }
Type* Checker::checkJsxAttributes(Node* node, CheckMode checkMode) { TSC_UNREACHABLE("checkJsxAttributes — jsx slice"); }
Type* Checker::checkNonNullExpression(Node* node) { TSC_UNREACHABLE("checkNonNullExpression — expressions slice"); }
bool Checker::isSymbolOrSymbolForCall(Node* node) { TSC_UNREACHABLE("isSymbolOrSymbolForCall — expressions slice"); }

// owner: deferred-check callees (wave-3)
Signature* Checker::resolveUntypedCall(Node* node) { TSC_UNREACHABLE("resolveUntypedCall — call-resolution slice"); }
void Checker::checkFunctionExpressionOrObjectLiteralMethodDeferred(Node* node) { TSC_UNREACHABLE("checkFunctionExpressionOrObjectLiteralMethodDeferred — expressions slice"); }
void Checker::checkClassExpressionDeferred(Node* node) { TSC_UNREACHABLE("checkClassExpressionDeferred — expressions slice"); }
void Checker::checkJsxSelfClosingElementDeferred(Node* node) { TSC_UNREACHABLE("checkJsxSelfClosingElementDeferred — jsx slice"); }
void Checker::checkJsxElementDeferred(Node* node) { TSC_UNREACHABLE("checkJsxElementDeferred — jsx slice"); }
// (deduped: checkAssertionDeferred defined in cpp/internal/checker/checker_expressions_c.cpp)
// (deduped: checkContextualDeprecations defined in cpp/internal/checker/checker_expressions_c.cpp)

// owner: walker per-node checks (wave-3)
void Checker::checkVariableStatement(Node* node) { TSC_UNREACHABLE("checkVariableStatement — declchecks slice"); }
void Checker::checkExpressionStatement(Node* node) { TSC_UNREACHABLE("checkExpressionStatement — stmtchecks slice"); }
void Checker::checkVariableDeclaration(Node* node) { TSC_UNREACHABLE("checkVariableDeclaration — declchecks slice"); }
void Checker::checkInterfaceDeclaration(Node* node) { TSC_UNREACHABLE("checkInterfaceDeclaration — declchecks slice"); }
void Checker::checkTypeAliasDeclaration(Node* node) { TSC_UNREACHABLE("checkTypeAliasDeclaration — declchecks slice"); }
void Checker::checkEnumDeclaration(Node* node) { TSC_UNREACHABLE("checkEnumDeclaration — declchecks slice"); }
void Checker::checkEnumMember(Node* node) { TSC_UNREACHABLE("checkEnumMember — declchecks slice"); }
void Checker::checkModuleDeclaration(Node* node) { TSC_UNREACHABLE("checkModuleDeclaration — modulechecks slice"); }
void Checker::checkImportDeclaration(Node* node) { TSC_UNREACHABLE("checkImportDeclaration — modulechecks slice"); }
void Checker::checkImportEqualsDeclaration(Node* node) { TSC_UNREACHABLE("checkImportEqualsDeclaration — modulechecks slice"); }
void Checker::checkExportDeclaration(Node* node) { TSC_UNREACHABLE("checkExportDeclaration — modulechecks slice"); }
void Checker::checkExportAssignment(Node* node) { TSC_UNREACHABLE("checkExportAssignment — modulechecks slice"); }
void Checker::checkMissingDeclaration(Node* node) { TSC_UNREACHABLE("checkMissingDeclaration — declchecks slice"); }

// owner: unused/export bookkeeping (wave-3)
void Checker::checkUnusedIdentifiers(const std::vector<Node*>& potentiallyUnusedIdentifiers) { TSC_UNREACHABLE("checkUnusedIdentifiers — unusedcheck slice"); }
void Checker::checkUnusedRenamedBindingElements() { TSC_UNREACHABLE("checkUnusedRenamedBindingElements — unusedcheck slice"); }
void Checker::checkExternalModuleExports(Node* moduleNode) { TSC_UNREACHABLE("checkExternalModuleExports — modulechecks slice"); }
void Checker::registerForUnusedIdentifiersCheck(Node* node) { TSC_UNREACHABLE("registerForUnusedIdentifiersCheck — unusedcheck slice"); }

// (TypeToString dep-stub removed — tracer slice landed the real definition.)

// owner: module/resolved-symbol helpers
std::string Checker::getIsolatedModulesLikeFlagName() { TSC_UNREACHABLE("getIsolatedModulesLikeFlagName — modulechecks slice"); }

} // namespace tsc::checker
