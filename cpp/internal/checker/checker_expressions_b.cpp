// checker_expressions_b.cpp — expressions-b slice of the checker.
// Ports checker.go:10492-12389 — contextual call signatures, contextual
// parameter assignment, declaration-name collision checks, typeof/nonnull/
// satisfies/meta-property/delete/void/await/unary/conditional/spread/yield/
// synthetic/identifier expression checks, property-access checking (private
// identifiers, nonexistent-property diagnostics, accessibility), and
// this-expression typing.
//
// Functions in this range already ported in checker.cpp are NOT duplicated:
// isInAmbientOrTypeNode (checker.cpp:3740), getEntityNameForExtendingInterface
// (:4944), checkAndReportErrorForExtendingInterface (:4966),
// getSuggestedLibForNonExistentProperty (:5310). Checker::getThisContainer
// (checker.go:12390, just past the range) is already real at checker.cpp:5837.
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
#include <cstring>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace tsc::checker {

// ast/utilities.go:2998 — IsCallLikeExpression (def at file end)
bool isCallLikeExpression(Node* node);


// Free fns defined (non-static) in other checker TUs — forward declarations.
Diagnostic* NewDiagnosticChainForNode(Diagnostic* chain, Node* node,
                                      const DiagnosticMessage* message,
                                      const std::vector<std::string>& args);
bool everyContainedType(Type* t, const std::function<bool(Type*)>& f);
// checker_decltypes.cpp — utilities.go:757-797
ModifierFlags getDeclarationModifierFlagsFromSymbol(Symbol* s);
ModifierFlags getDeclarationModifierFlagsFromSymbolEx(Symbol* s, bool isWrite);
// checker_members.cpp — ast.GetClassLikeDeclarationOfSymbol
Node* getClassLikeDeclarationOfSymbol(Symbol* symbol);

namespace {

// Forward decls for helpers defined later in this TU.
bool isCompoundLikeAssignment(Node* assignment);
bool isShiftOperatorOrHigher(Kind kind);
bool hasCommonDomTypeName(Type* t);
Node* getThisParameterFromNodeContext(Node* node);

// binder.go:374 — GetSymbolNameForPrivateIdentifier (checker_contextual.cpp
// defines this inside an anonymous namespace, so this TU needs its own copy).
std::string getSymbolNameForPrivateIdentifier(Symbol* containingClassSymbol,
                                              const std::string& description) {
	return std::string(1, kInternalSymbolNamePrefix) + "#" +
	       std::to_string(static_cast<uint64_t>(getSymbolId(containingClassSymbol))) +
	       "@" + description;
}

// checker/utilities.go:298 — isOptionalDeclaration
static bool isOptionalDeclaration(Node* declaration) {
	return hasQuestionToken(declaration);
}

// checker/utilities.go:272 — hasDotDotDotToken
static bool hasDotDotDotToken(Node* node) {
	switch (node->kind) {
	case Kind::Parameter:
		return node->as<ParameterDeclaration>()->DotDotDotToken != nullptr;
	case Kind::BindingElement:
		return node->as<BindingElement>()->DotDotDotToken != nullptr;
	case Kind::NamedTupleMember:
		return node->as<NamedTupleMember>()->DotDotDotToken != nullptr;
	case Kind::JsxExpression:
		return node->as<JsxExpression>()->DotDotDotToken != nullptr;
	}
	return false;
}

// utilities.go:89 — getAssignmentTargetKind
AssignmentKind getAssignmentTargetKind(Node* node) {
	Node* target = getAssignmentTarget(node);
	if (target == nullptr) {
		return AssignmentKindNone;
	}
	switch (target->kind) {
	case Kind::BinaryExpression: {
		Kind binaryOperator = target->as<BinaryExpression>()->OperatorToken->kind;
		if (binaryOperator == Kind::EqualsToken ||
			isLogicalOrCoalescingAssignmentOperator(binaryOperator)) {
			return AssignmentKindDefinite;
		}
		return AssignmentKindCompound;
	}
	case Kind::PrefixUnaryExpression:
	case Kind::PostfixUnaryExpression:
		return AssignmentKindCompound;
	case Kind::ForInStatement:
	case Kind::ForOfStatement:
		return AssignmentKindDefinite;
	}
	TSC_UNREACHABLE("Unhandled case in getAssignmentTargetKind");
}

// utilities.go:117 — isInCompoundLikeAssignment
bool isInCompoundLikeAssignment(Node* node) {
	Node* target = getAssignmentTarget(node);
	return target != nullptr &&
	       isAssignmentExpression(target, true /*excludeCompoundAssignment*/) &&
	       isCompoundLikeAssignment(target);
}

// utilities.go:122 — isCompoundLikeAssignment
bool isCompoundLikeAssignment(Node* assignment) {
	Node* right = skipParentheses(assignment->as<BinaryExpression>()->Right);
	return right->kind == Kind::BinaryExpression &&
	       isShiftOperatorOrHigher(right->as<BinaryExpression>()->OperatorToken->kind);
}

// ast_generated.go:9966 — IsShiftOperatorOrHigher
bool isShiftOperatorOrHigher(Kind kind) {
	switch (kind) {
	case Kind::AsteriskAsteriskToken:
	case Kind::AsteriskToken:
	case Kind::SlashToken:
	case Kind::PercentToken:
	case Kind::PlusToken:
	case Kind::MinusToken:
	case Kind::LessThanLessThanToken:
	case Kind::GreaterThanGreaterThanToken:
	case Kind::GreaterThanGreaterThanGreaterThanToken:
		return true;
	}
	return false;
}

// utilities.go:158 — isDeleteTarget
bool isDeleteTarget(Node* node) {
	if (!isAccessExpression(node)) {
		return false;
	}
	node = walkUpParenthesizedExpressions(node->parent);
	return node != nullptr && node->kind == Kind::DeleteExpression;
}

// utilities.go:190 — isInTypeQuery
bool isInTypeQuery(Node* node) {
	// TypeScript 1.0 spec (April 2014): 3.6.3
	// A type query consists of the keyword typeof followed by an expression. The
	// expression is restricted to a single identifier or a sequence of identifiers
	// separated by periods
	return findAncestorOrQuit(node, [](Node* n) -> FindAncestorResult {
		switch (n->kind) {
		case Kind::TypeQuery:
			return FindAncestorResult::True;
		case Kind::Identifier:
		case Kind::QualifiedName:
			return FindAncestorResult::False;
		default:
			return FindAncestorResult::Quit;
		}
	}) != nullptr;
}

// utilities.go:281 — IsTypeAny
// (deduped: local replica of isTypeAny removed)

// ast/utilities.go:3032 — IsThisInTypeQuery
// (deduped: local replica of isThisInTypeQuery removed)

// checker.go:29036 — shouldMarkIdentifierAliasReferenced
bool shouldMarkIdentifierAliasReferenced(Node* node /*Identifier*/) {
	Node* parent = node->parent;
	if (parent != nullptr) {
		// A property access expression LHS? checkPropertyAccessExpression will handle that.
		if (isPropertyAccessExpression(parent) && parent->expression() == node) {
			return false;
		}
		// Next two check for an identifier inside a type only export.
		if (isExportSpecifier(parent) && parent->isTypeOnly()) {
			return false;
		}
		if (parent->parent != nullptr) {
			Node* greatGrandparent = parent->parent->parent;
			if (greatGrandparent != nullptr && isExportDeclaration(greatGrandparent) &&
				greatGrandparent->isTypeOnly()) {
				return false;
			}
		}
	}
	return true;
}

// checker.cpp (isThisProperty) — utilities.go:998
bool isThisProperty(Node* node) {
	return (isPropertyAccessExpression(node) || isElementAccessExpression(node)) &&
	       node->expression()->kind == Kind::ThisKeyword;
}

// emitresolver.go:693 — isConstEnumSymbol
bool isConstEnumSymbol(Symbol* s) {
	return (s->flags & SymbolFlagsConstEnum) != 0;
}

// utilities.go (isConstEnumObjectType)
bool isConstEnumObjectType(Type* t) {
	return (t->objectFlags & ObjectFlagsAnonymous) != 0 && t->symbol != nullptr &&
	       isConstEnumSymbol(t->symbol);
}

// ast/utilities.go:2639 — GetDeclarationContainer
Node* getDeclarationContainer(Node* node) {
	return findAncestor(getRootDeclaration(node), [](Node* n) -> bool {
		switch (n->kind) {
		case Kind::VariableDeclaration:
		case Kind::VariableDeclarationList:
		case Kind::ImportSpecifier:
		case Kind::NamedImports:
		case Kind::NamespaceImport:
		case Kind::ImportClause:
			return false;
		default:
			return true;
		}
	})->parent;
}

// utilities.go:1060 — isClassInstanceProperty
bool isClassInstanceProperty(Node* node) {
	if (isInJSFile(node) && isExpandoPropertyDeclaration(node)) {
		Node* left = node->as<BinaryExpression>()->Left;
		return (!isBindableStaticAccessExpression(left, false /*excludeThisKeyword*/) ||
				!isPrototypeAccess(left->expression())) &&
			!isBindableStaticNameExpression(left, true /*excludeThisKeyword*/);
	}
	return node->parent != nullptr && isClassLike(node->parent) &&
	       isPropertyDeclaration(node) && !hasAccessorModifier(node);
}

// utilities.go:1069 — isThisInitializedObjectBindingExpression
bool isThisInitializedObjectBindingExpression(Node* node) {
	return node != nullptr &&
	       (isShorthandPropertyAssignment(node) || isPropertyAssignment(node)) &&
	       isBinaryExpression(node->parent->parent) &&
	       node->parent->parent->as<BinaryExpression>()->OperatorToken->kind ==
	           Kind::EqualsToken &&
	       node->parent->parent->as<BinaryExpression>()->Right->kind ==
	           Kind::ThisKeyword;
}

// utilities.go:1075 — isThisInitializedDeclaration
bool isThisInitializedDeclaration(Node* node) {
	return node != nullptr && isVariableDeclaration(node) &&
	       node->initializer() != nullptr &&
	       node->initializer()->kind == Kind::ThisKeyword;
}

// utilities.go:1124 — getBindingElementPropertyName
Node* getBindingElementPropertyName(Node* node) {
	return node->propertyNameOrName();
}

// utilities.go:1163 — getContainingObjectLiteral
// (deduped: local replica of getContainingObjectLiteral removed)

// utilities.go:1203 — expressionResultIsUnused
bool expressionResultIsUnused(Node* node) {
	for (;;) {
		Node* parent = node->parent;
		// walk up parenthesized expressions, but keep a pointer to the top-most parenthesized expression
		if (isParenthesizedExpression(parent)) {
			node = parent;
			continue;
		}
		// result is unused in an expression statement, `void` expression, or the initializer or incrementer of a `for` loop
		if (isExpressionStatement(parent) || isVoidExpression(parent) ||
		    (isForStatement(parent) &&
		     (parent->initializer() == node ||
		      parent->as<ForStatement>()->Incrementor == node))) {
			return true;
		}
		if (isBinaryExpression(parent) &&
		    parent->as<BinaryExpression>()->OperatorToken->kind == Kind::CommaToken) {
			// left side of comma is always unused
			if (node == parent->as<BinaryExpression>()->Left) {
				return true;
			}
			// right side of comma is unused if parent is unused
			node = parent;
			continue;
		}
		return false;
	}
}

// compileroptions.go / ModuleKind String — program.cpp (anonymous namespace
// there, so this TU needs its own copy)
std::string moduleKindString(ModuleKind i) {
	static const char* name0 =
	    "NoneCommonJSAMDUMDSystemES2015ES2020ES2022";
	static const uint8_t index0[] = {0, 4, 12, 15, 18, 24, 30, 36, 42};
	static const char* name1 = "ESNextNode16Node18Node20";
	static const uint8_t index1[] = {0, 6, 12, 18, 24};
	static const char* name2 = "NodeNextPreserve";
	static const uint8_t index2[] = {0, 8, 16};
	int v = static_cast<int>(i);
	if (0 <= v && v <= 7)
		return std::string(name0 + index0[v],
		                   static_cast<size_t>(index0[v + 1] - index0[v]));
	if (99 <= v && v <= 102) {
		v -= 99;
		return std::string(name1 + index1[v],
		                   static_cast<size_t>(index1[v + 1] - index1[v]));
	}
	if (199 <= v && v <= 200) {
		v -= 199;
		return std::string(name2 + index2[v],
		                   static_cast<size_t>(index2[v + 1] - index2[v]));
	}
	return "ModuleKind(" + std::to_string(v) + ")";
}

// utilities.go:1037 — getContainingClassExcludingClassDecorators
Node* getContainingClassExcludingClassDecorators(Node* node) {
	Node* decorator = findAncestorOrQuit(node->parent, [](Node* n) -> FindAncestorResult {
		if (isClassLike(n)) {
			return FindAncestorResult::Quit;
		}
		if (isDecorator(n)) {
			return FindAncestorResult::True;
		}
		return FindAncestorResult::False;
	});
	if (decorator != nullptr && isClassLike(decorator->parent)) {
		return getContainingClass(decorator->parent);
	}
	if (decorator != nullptr) {
		return getContainingClass(decorator);
	}
	return getContainingClass(node);
}

// ast/utilities.go:2209 — GetNewTargetContainer
Node* getNewTargetContainer(Node* node) {
	Node* container = getThisContainer(node, false /*includeArrowFunctions*/,
	                                   false /*includeClassComputedPropertyName*/);
	if (container != nullptr) {
		switch (container->kind) {
		case Kind::Constructor:
		case Kind::FunctionDeclaration:
		case Kind::FunctionExpression:
			return container;
		default:
			break;
		}
	}
	return nullptr;
}


// ast/utilities.go:4058 — IsJSDocNameReferenceContext
// (deduped: local replica of isJSDocNameReferenceContext removed)

// ast/utilities.go:4548 — GetReparsedNodeForNode
// (deduped: local replica of compareNodePositions removed)

Node* findCloneInNode(Node* node, Node* original) {
	for (;;) {
		if (node->kind == original->kind &&
		    node->loc.pos() == original->loc.pos() &&
		    node->loc.end() == original->loc.end()) {
			return node;
		}
		bool foundContainingChild = node->forEachChild([&](Node* n) -> bool {
			if (n->loc.pos() <= original->loc.pos() &&
			    original->loc.end() <= n->loc.end()) {
				node = n;
				return true;
			}
			return false;
		});
		if (!foundContainingChild) {
			return nullptr;
		}
	}
}

// (deduped: local replica of getReparsedNodeForNode removed)

// checker.go:17358 — signatureHasRestParameter
bool signatureHasRestParameter(Signature* sig) {
	return (sig->flags & SignatureFlagsHasRestParameter) != 0;
}

// StructuredType.CallSignatures() / ConstructSignatures() — views over
// signatures split at callSignatureCount (types.go:949).
std::vector<Signature*> callSignaturesOf(const StructuredType* t) {
	return std::vector<Signature*>(t->signatures.begin(),
		t->signatures.begin() + t->callSignatureCount);
}
std::vector<Signature*> constructSignaturesOf(const StructuredType* t) {
	return std::vector<Signature*>(t->signatures.begin() + t->callSignatureCount,
		t->signatures.end());
}

// core.Filter / core.SameMap / core.Same — file-local templates.
template <class T, class F>
std::vector<T> filterVec(const std::vector<T>& v, F&& f) {
	std::vector<T> result;
	for (const T& x : v) {
		if (f(x)) {
			result.push_back(x);
		}
	}
	return result;
}

template <class T, class F>
std::vector<T> sameMap(const std::vector<T>& v, F&& f) {
	std::vector<T> result;
	result.reserve(v.size());
	bool same = true;
	for (const T& x : v) {
		T y = f(x);
		if (y != x) {
			same = false;
		}
		result.push_back(y);
	}
	return same ? v : result;
}

template <class T>
bool sameVec(const std::vector<T>& a, const std::vector<T>& b) {
	return a == b;
}

}  // namespace

// ===========================================================================
// checker.go:10492-12389
// ===========================================================================

Signature* Checker::createUnionSignature(
    Signature* sig, const std::vector<Signature*>& unionSignatures) {
	Signature* result = cloneSignature(sig);
	CompositeSignature* composite = new CompositeSignature();
	composite->isUnion = true;
	composite->signatures = unionSignatures;
	result->composite = composite;
	result->target = nullptr;
	result->mapper = nullptr;
	return result;
}

// If the given type is an object or union type with a single signature, and if that signature has at
// least as many parameters as the given function, return the signature. Otherwise return undefined.
Signature* Checker::getContextualCallSignature(Type* t, Node* node) {
	std::vector<Signature*> signatures =
	    getSignaturesOfType(t, SignatureKind::Call);
	std::vector<Signature*> applicableByArity =
	    filterVec(signatures, [&](Signature* s) { return !isAritySmaller(s, node); });
	if (applicableByArity.size() == 1) {
		return applicableByArity[0];
	}
	return getIntersectedSignatures(applicableByArity);
}

Signature* Checker::getIntersectedSignatures(
    const std::vector<Signature*>& signatures) {
	if (!noImplicitAny) {
		return nullptr;
	}
	Signature* combined = nullptr;
	for (Signature* sig : signatures) {
		if (combined == sig || combined == nullptr) {
			combined = sig;
		} else if (compareTypeParametersIdentical(combined->typeParameters,
		                                          sig->typeParameters)) {
			combined = combineUnionOrIntersectionMemberSignatures(
			    combined, sig, false /*isUnion*/);
		} else {
			return nullptr;
		}
	}
	return combined;
}

/** If the contextual signature has fewer parameters than the function expression, do not use it */
bool Checker::isAritySmaller(Signature* signature, Node* target) {
	std::vector<Node*> parameters = target->parameters();
	int targetParameterCount = 0;
	while (targetParameterCount < static_cast<int>(parameters.size())) {
		Node* param = parameters[targetParameterCount];
		if (param->initializer() != nullptr || param->questionToken() != nullptr ||
		    hasDotDotDotToken(param)) {
			break;
		}
		targetParameterCount++;
	}
	if (!parameters.empty() && isThisParameter(parameters[0])) {
		targetParameterCount--;
	}
	return !hasEffectiveRestParameter(signature) &&
	       getParameterCount(signature) < targetParameterCount;
}

void Checker::assignContextualParameterTypes(Signature* sig, Signature* context) {
	if (!context->typeParameters.empty()) {
		if (!sig->typeParameters.empty()) {
			// This signature has already has a contextual inference performed and cached on it
			return;
		}
		sig->typeParameters = context->typeParameters;
	}
	if (context->thisParameter != nullptr) {
		Symbol* parameter = sig->thisParameter;
		if (parameter == nullptr || (parameter->valueDeclaration != nullptr &&
		                             parameter->valueDeclaration->type() == nullptr)) {
			if (parameter == nullptr) {
				sig->thisParameter = createSymbolWithType(context->thisParameter,
				                                          nullptr /*type*/);
			}
			assignParameterType(sig->thisParameter,
			                  getTypeOfSymbol(context->thisParameter));
		}
	}
	int length = static_cast<int>(sig->parameters.size()) -
	             (signatureHasRestParameter(sig) ? 1 : 0);
	for (int i = 0; i < length; i++) {
		Symbol* parameter = sig->parameters[i];
		Node* declaration = parameter->valueDeclaration;
		if (declaration->type() == nullptr) {
			Type* t = tryGetTypeAtPosition(context, i);
			if (t != nullptr && declaration->initializer() != nullptr) {
				Type* initializerType =
				    checkDeclarationInitializer(declaration, CheckModeNormal, nullptr);
				if (!isTypeAssignableTo(initializerType, t)) {
					initializerType =
					    widenTypeInferredFromInitializer(declaration, initializerType);
					if (isTypeAssignableTo(t, initializerType)) {
						t = initializerType;
					}
				}
			}
			assignParameterType(parameter, t);
		}
	}
	if (signatureHasRestParameter(sig)) {
		// parameter might be a transient symbol generated by use of `arguments` in the function body.
		Symbol* parameter = sig->parameters.back();
		if ((parameter->valueDeclaration != nullptr &&
		     parameter->valueDeclaration->type() == nullptr) ||
		    (parameter->valueDeclaration == nullptr &&
		     (parameter->checkFlags & CheckFlagsDeferredType) != 0)) {
			Type* contextualParameterType =
			    getRestTypeAtPosition(context, length, false);
			assignParameterType(parameter, contextualParameterType);
		}
	}
}

void Checker::assignNonContextualParameterTypes(Signature* signature) {
	if (signature->thisParameter != nullptr) {
		assignParameterType(signature->thisParameter, nullptr);
	}
	for (Symbol* parameter : signature->parameters) {
		assignParameterType(parameter, nullptr);
	}
}

void Checker::assignParameterType(Symbol* parameter, Type* contextualType) {
	ValueSymbolLinks* links = valueSymbolLinks.Get(parameter);
	if (links->resolvedType != nullptr &&
		!staleForCheckFile(links->resolvedTypeCheckFile)) {
		return;
	}
	links->resolvedType = nullptr;
	links->resolvedTypeCheckFile = checkFileTag();
	Node* declaration = parameter->valueDeclaration;
	Type* t = contextualType;
	if (t == nullptr) {
		if (declaration != nullptr) {
			t = getWidenedTypeForVariableLikeDeclaration(declaration,
			                                           true /*reportErrors*/);
		} else {
			t = getTypeOfSymbol(parameter);
		}
	}
	links->resolvedType =
	    addOptionalityEx(t, false,
	                     declaration != nullptr && declaration->initializer() == nullptr &&
	                         isOptionalDeclaration(declaration));
	if (declaration != nullptr && !isIdentifier(declaration->name())) {
		// if inference didn't come up with anything but unknown, fall back to the binding pattern if present.
		if (links->resolvedType == unknownType) {
			links->resolvedType =
			    getTypeFromBindingPattern(declaration->name(), false, false);
		}
		assignBindingElementTypes(declaration->name(), links->resolvedType);
	}
}

// When contextual typing assigns a type to a parameter that contains a binding pattern, we also need to push
// the destructured type into the contained binding elements.
void Checker::assignBindingElementTypes(Node* pattern, Type* parentType) {
	for (Node* element : pattern->elements()) {
		Node* name = element->name();
		if (name != nullptr) {
			Type* t = getBindingElementTypeFromParentType(element, parentType,
			                                            false /*noTupleBoundsCheck*/);
			if (isIdentifier(name)) {
				auto* elLinks = valueSymbolLinks.Get(getSymbolOfDeclaration(element));
				elLinks->resolvedType = t;
				elLinks->resolvedTypeCheckFile = checkFileTag();
			} else {
				assignBindingElementTypes(name, t);
			}
		}
	}
}

void Checker::checkCollisionsForDeclarationName(Node* node, Node* name) {
	if (name == nullptr) {
		return;
	}
	checkCollisionWithRequireExportsInGeneratedCode(node, name);
	checkCollisionWithGlobalObjectInGeneratedCode(node, name);
	checkCollisionWithGlobalPromiseInGeneratedCode(node, name);
	recordPotentialCollisionWithWeakMapSetInGeneratedCode(node, name);
	recordPotentialCollisionWithReflectInGeneratedCode(node, name);
	if (isClassLike(node)) {
		checkTypeNameIsReserved(name, Class_name_cannot_be_0);
		if ((node->flags & NodeFlagsAmbient) == 0) {
			checkClassNameCollisionWithObject(name);
		}
	} else if (isEnumDeclaration(node)) {
		checkTypeNameIsReserved(name, Enum_name_cannot_be_0);
	}
}

void Checker::checkCollisionWithRequireExportsInGeneratedCode(Node* node,
                                                              Node* name) {
	// No need to check for require or exports for ES6 modules and later
	if (program->GetEmitModuleFormatOfFile(getSourceFileOfNode(node)) >=
	    ModuleKind::ES2015) {
		return;
	}
	if (name == nullptr ||
	    (!needCollisionCheckForIdentifier(node, name, "require") &&
	     !needCollisionCheckForIdentifier(node, name, "exports"))) {
		return;
	}
	// Uninstantiated modules shouldnt do this check
	if (isModuleDeclaration(node) &&
	    getModuleInstanceState(node) != ModuleInstanceState::Instantiated) {
		return;
	}
	// In case of variable declaration, node.parent is variable statement so look at the variable statement's parent
	Node* parent = getDeclarationContainer(node);
	if (isSourceFile(parent) && isExternalOrCommonJSModule(parent->as<SourceFile>())) {
		// If the declaration happens to be in external module, report error that require and exports are reserved keywords
		errorSkippedOnNoEmit(
		    name,
		    Duplicate_identifier_0_Compiler_reserves_name_1_in_top_level_scope_of_a_module,
		    {declarationNameToString(name), declarationNameToString(name)});
	}
}

void Checker::checkCollisionWithGlobalObjectInGeneratedCode(Node* node,
                                                            Node* name) {
	if (name == nullptr || isClassLike(node) ||
	    !needCollisionCheckForIdentifier(node, name, "Object")) {
		return;
	}
	// Uninstantiated modules shouldn't do this check
	if (isModuleDeclaration(node) &&
	    getModuleInstanceState(node) != ModuleInstanceState::Instantiated) {
		return;
	}
	// In case of variable declaration, node.parent is variable statement so look at the variable statement's parent
	Node* parent = getDeclarationContainer(node);
	if (isSourceFile(parent) && isExternalOrCommonJSModule(parent->as<SourceFile>()) &&
	    program->GetEmitModuleFormatOfFile(parent->as<SourceFile>()) ==
	        ModuleKind::CommonJS) {
		// If the declaration happens to be in external module, report error that Object is a reserved identifier.
		errorSkippedOnNoEmit(
		    name,
		    Duplicate_identifier_0_Compiler_reserves_name_1_in_top_level_scope_of_a_module,
		    {declarationNameToString(name), declarationNameToString(name)});
	}
}

bool Checker::needCollisionCheckForIdentifier(Node* node, Node* identifier,
                                              std::string_view name) {
	std::string scratch;
	if (identifier != nullptr && identifier->textView(scratch) != name) {
		return false;
	}
	switch (node->kind) {
	case Kind::PropertyDeclaration:
	case Kind::PropertySignature:
	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::PropertyAssignment:
		// it is ok to have member named '_super', '_this', `Promise`, etc. - member access is always qualified
		return false;
	default:
		break;
	}
	if ((node->flags & NodeFlagsAmbient) != 0) {
		// ambient context - no codegen impact
		return false;
	}
	if (isImportClause(node) || isImportEqualsDeclaration(node) ||
	    isImportSpecifier(node)) {
		// type-only imports do not require collision checks against runtime values.
		if (isTypeOnlyImportOrExportDeclaration(node)) {
			return false;
		}
	}
	Node* root = getRootDeclaration(node);
	if (isParameterDeclaration(root) && nodeIsMissing(root->parent->body())) {
		// just an overload - no codegen impact
		return false;
	}
	return true;
}

void Checker::setNodeLinksForPrivateIdentifierScope(Node* node) {
	if (Node* name = node->name(); isPrivateIdentifier(name)) {
		if (languageVersion <
		        LanguageFeatureMinimumTarget.PrivateNamesAndClassStaticBlocks ||
		    languageVersion <
		        LanguageFeatureMinimumTarget.ClassAndClassElementDecorators ||
		    !compilerOptions->GetUseDefineForClassFields()) {
			for (Node* lexicalScope = getEnclosingBlockScopeContainer(node);
			     lexicalScope != nullptr;
			     lexicalScope = getEnclosingBlockScopeContainer(lexicalScope)) {
				nodeLinks.Get(lexicalScope)->flags |=
				    NodeCheckFlagsContainsClassWithPrivateIdentifiers;
			}
		}
	}
}

void Checker::recordPotentialCollisionWithWeakMapSetInGeneratedCode(Node* node,
                                                                    Node* name) {
	if (languageVersion <= ScriptTarget::ES2021 &&
	    (needCollisionCheckForIdentifier(node, name, "WeakMap") ||
	     needCollisionCheckForIdentifier(node, name, "WeakSet"))) {
		addDeferredDiagnostic([this, node]() { checkWeakMapSetCollision(node); });
	}
}

void Checker::checkWeakMapSetCollision(Node* node) {
	Node* enclosingBlockScope = getEnclosingBlockScopeContainer(node);
	if ((nodeLinks.Get(enclosingBlockScope)->flags &
	     NodeCheckFlagsContainsClassWithPrivateIdentifiers) != 0) {
		Node* name = node->name();
		if (name != nullptr && isIdentifier(name)) {
			errorSkippedOnNoEmit(
			    node,
			    Compiler_reserves_name_0_when_emitting_private_identifier_downlevel,
			    {name->text()});
		}
	}
}

void Checker::checkCollisionWithGlobalPromiseInGeneratedCode(Node* node,
                                                             Node* name) {
	if (name == nullptr || languageVersion >= ScriptTarget::ES2017 ||
	    !needCollisionCheckForIdentifier(node, name, "Promise")) {
		return;
	}
	// Uninstantiated modules shouldn't do this check
	if (isModuleDeclaration(node) &&
	    getModuleInstanceState(node) != ModuleInstanceState::Instantiated) {
		return;
	}
	// In case of variable declaration, node.parent is variable statement so look at the variable statement's parent
	Node* parent = getDeclarationContainer(node);
	if (isSourceFile(parent) && isExternalOrCommonJSModule(parent->as<SourceFile>()) &&
	    (parent->flags & NodeFlagsHasAsyncFunctions) != 0) {
		// If the declaration happens to be in external module, report error that Promise is a reserved identifier.
		errorSkippedOnNoEmit(
		    name,
		    Duplicate_identifier_0_Compiler_reserves_name_1_in_top_level_scope_of_a_module_containing_async_functions,
		    {declarationNameToString(name), declarationNameToString(name)});
	}
}

void Checker::recordPotentialCollisionWithReflectInGeneratedCode(Node* node,
                                                                 Node* name) {
	if (name != nullptr && languageVersion <= ScriptTarget::ES2021 &&
	    needCollisionCheckForIdentifier(node, name, "Reflect")) {
		addDeferredDiagnostic([this, node]() { checkReflectCollision(node); });
	}
}

void Checker::checkReflectCollision(Node* node) {
	bool hasCollision = false;
	if (isClassExpression(node)) {
		// ClassExpression names don't contribute to their containers, but do matter for any of their block-scoped members.
		for (Node* member : node->members()) {
			if ((nodeLinks.Get(member)->flags &
			     NodeCheckFlagsContainsSuperPropertyInStaticInitializer) != 0) {
				hasCollision = true;
				break;
			}
		}
	} else if (isFunctionExpression(node)) {
		// FunctionExpression names don't contribute to their containers, but do matter for their contents
		if ((nodeLinks.Get(node)->flags &
		     NodeCheckFlagsContainsSuperPropertyInStaticInitializer) != 0) {
			hasCollision = true;
		}
	} else {
		Node* container = getEnclosingBlockScopeContainer(node);
		if (container != nullptr &&
		    (nodeLinks.Get(container)->flags &
		     NodeCheckFlagsContainsSuperPropertyInStaticInitializer) != 0) {
			hasCollision = true;
		}
	}
	if (hasCollision) {
		Node* name = node->name();
		if (name != nullptr && isIdentifier(name)) {
			errorSkippedOnNoEmit(
			    node,
			    Duplicate_identifier_0_Compiler_reserves_name_1_when_emitting_super_references_in_static_initializers,
			    {declarationNameToString(name), "Reflect"});
		}
	}
}

void Checker::checkClassNameCollisionWithObject(Node* name) {
	if (name->text() == "Object" &&
	    program->GetEmitModuleFormatOfFile(getSourceFileOfNode(name)) <
	        ModuleKind::ES2015) {
		error(name,
		      Class_name_cannot_be_Object_when_targeting_ES5_and_above_with_module_0,
		      moduleKindString(moduleKind));
	}
}

Type* Checker::checkTypeOfExpression(Node* node) {
	checkExpression(node->expression());
	return typeofType;
}

Type* Checker::checkNonNullAssertion(Node* node) {
	if ((node->flags & NodeFlagsOptionalChain) != 0) {
		// checkNonNullChain checks the same operand expression (node.Expression()),
		// so the child is still visited on this branch.
		return checkNonNullChain(node); //nolint:customlint // checkNonNullChain checks the operand
	}
	return GetNonNullableType(checkExpression(node->expression()));
}

Type* Checker::checkNonNullChain(Node* node) {
	Type* leftType = checkExpression(node->expression());
	Type* nonOptionalType = getOptionalExpressionType(leftType, node->expression());
	return propagateOptionalTypeMarker(GetNonNullableType(nonOptionalType), node,
	                                   nonOptionalType != leftType);
}

Type* Checker::checkExpressionWithTypeArguments(Node* node) {
	checkGrammarExpressionWithTypeArguments(node);
	checkSourceElements(node->typeArguments());
	if (isExpressionWithTypeArguments(node)) {
		Node* parent = walkUpParenthesizedExpressions(node->parent);
		if (isBinaryExpression(parent) &&
		    parent->as<BinaryExpression>()->OperatorToken->kind ==
		        Kind::InstanceOfKeyword &&
		    isNodeDescendantOf(node, parent->as<BinaryExpression>()->Right)) {
			error(node,
			      The_right_hand_side_of_an_instanceof_expression_must_not_be_an_instantiation_expression);
		}
	}
	Type* exprType;
	if (isExpressionWithTypeArguments(node)) {
		exprType = checkExpression(node->expression());
	} else {
		Node* exprName = node->as<TypeQueryNode>()->ExprName;
		if (isThisIdentifier(exprName)) {
			exprType = checkThisExpression(node->as<TypeQueryNode>()->ExprName);
		} else {
			exprType = checkExpression(node->as<TypeQueryNode>()->ExprName);
		}
	}
	return getInstantiationExpressionType(exprType, node);
}

Type* Checker::getInstantiationExpressionType(Type* exprType, Node* node) {
	NodeList* typeArguments = node->typeArgumentList();
	if (exprType == silentNeverType || isErrorType(exprType) ||
	    typeArguments == nullptr) {
		return exprType;
	}
	InstantiationExpressionKey key{getNodeId(node), exprType->id};
	auto cachedIt = instantiationExpressionTypes.find(key);
	if (cachedIt != instantiationExpressionTypes.end()) {
		return cachedIt->second;
	}
	bool hasSomeApplicableSignature = false;
	Type* nonApplicableType = nullptr;
	auto getInstantiatedSignatures =
	    [&](const std::vector<Signature*>& signatures) -> std::vector<Signature*> {
		std::vector<Signature*> applicableSignatures =
		    filterVec(signatures, [&](Signature* sig) {
			    return !sig->typeParameters.empty() &&
			           hasCorrectTypeArgumentArity(sig, typeArguments->nodes);
		    });
		return sameMap(applicableSignatures, [&](Signature* sig) -> Signature* {
			std::vector<Type*> typeArgumentTypes =
			    checkTypeArguments(sig, typeArguments->nodes,
			                       true /*reportErrors*/, nullptr);
			if (!typeArgumentTypes.empty()) {
				return getSignatureInstantiation(sig, typeArgumentTypes,
				                                 isInJSFile(sig->declaration), {});
			}
			return sig;
		});
	};
	std::function<Type*(Type*)> getInstantiatedType;
	getInstantiatedType = [&](Type* t) -> Type* {
		bool hasSignatures = false;
		bool hasApplicableSignature = false;
		std::function<Type*(Type*)> getInstantiatedTypePart;
		getInstantiatedTypePart = [&](Type* t) -> Type* {
			if ((t->flags & TypeFlagsObject) != 0) {
				StructuredType* resolved = resolveStructuredTypeMembers(t);
				std::vector<Signature*> callSignatures =
				    getInstantiatedSignatures(callSignaturesOf(resolved));
				std::vector<Signature*> constructSignatures =
				    getInstantiatedSignatures(constructSignaturesOf(resolved));
				hasSignatures = hasSignatures || !callSignaturesOf(resolved).empty() ||
				                !constructSignaturesOf(resolved).empty();
				hasApplicableSignature = hasApplicableSignature ||
				                         !callSignatures.empty() ||
				                         !constructSignatures.empty();
				if (!sameVec(callSignatures, callSignaturesOf(resolved)) ||
				    !sameVec(constructSignatures, constructSignaturesOf(resolved))) {
					Symbol* symbol = newSymbol(
					    SymbolFlagsNone, InternalSymbolNameInstantiationExpression);
					TSC_ASSERT(t->symbol != nullptr,
					           "Instantiation expression source type must have a symbol");
					symbol->declarations = t->symbol->declarations;
					Type* result = newObjectType(
					    ObjectFlagsAnonymous | ObjectFlagsInstantiationExpressionType,
					    symbol);
					setStructuredTypeMembers(result, resolved->members,
					                         callSignatures, constructSignatures,
					                         resolved->indexInfos);
					result->AsInstantiationExpressionType()->node = node;
					return result;
				}
			} else if ((t->flags & TypeFlagsInstantiableNonPrimitive) != 0) {
				Type* constraint = getBaseConstraintOfType(t);
				if (constraint != nullptr) {
					Type* instantiated = getInstantiatedTypePart(constraint);
					if (instantiated != constraint) {
						return instantiated;
					}
				}
			} else if ((t->flags & TypeFlagsUnion) != 0) {
				return mapType(t, getInstantiatedType);
			} else if ((t->flags & TypeFlagsIntersection) != 0) {
				return getIntersectionType(
				    sameMap(t->AsIntersectionType()->types, getInstantiatedTypePart));
			}
			return t;
		};
		Type* result = getInstantiatedTypePart(t);
		hasSomeApplicableSignature =
		    hasSomeApplicableSignature || hasApplicableSignature;
		if (hasSignatures && !hasApplicableSignature) {
			if (nonApplicableType == nullptr) {
				nonApplicableType = t;
			}
		}
		return result;
	};
	Type* result = getInstantiatedType(exprType);
	instantiationExpressionTypes[key] = result;
	Type* errType;
	if (hasSomeApplicableSignature) {
		errType = nonApplicableType;
	} else {
		errType = exprType;
	}
	if (errType != nullptr) {
		SourceFile* sourceFile = getSourceFileOfNode(node);
		TextRange loc{static_cast<TextPos>(
		                  skipTrivia(sourceFile->text, typeArguments->pos())),
		              typeArguments->end()};
		addDiagnostic(newDiagnostic(
		    sourceFile, loc,
		    Type_0_has_no_signatures_for_which_the_type_argument_list_is_applicable,
		    {TypeToString(errType)}));
	}
	return result;
}

Type* Checker::checkSatisfiesExpression(Node* node) {
	Node* typeNode = node->type();
	checkSourceElement(typeNode);
	Type* exprType = checkExpression(node->expression());
	Type* targetType = getTypeFromTypeNode(typeNode);
	if (isErrorType(targetType)) {
		return targetType;
	}
	checkTypeAssignableToAndOptionallyElaborate(
	    exprType, targetType, node, node->expression(),
	    Type_0_does_not_satisfy_the_expected_type_1, nullptr);
	return exprType;
}

Type* Checker::checkMetaProperty(Node* node) {
	checkGrammarMetaProperty(node->as<MetaProperty>());
	switch (node->as<MetaProperty>()->KeywordToken) {
	case Kind::NewKeyword:
		return checkNewTargetMetaProperty(node);
	case Kind::ImportKeyword:
		if (node->name()->text() == "defer") {
			TSC_ASSERT(!isCallExpression(node->parent) ||
			               node->parent->expression() != node,
			           "Trying to get the type of `import.defer` in `import.defer(...)`");
			return errorType;
		}
		return checkImportMetaProperty(node);
	default:
		break;
	}
	TSC_UNREACHABLE("Unhandled case in checkMetaProperty");
}

Type* Checker::checkNewTargetMetaProperty(Node* node) {
	Node* container = getNewTargetContainer(node);
	if (container == nullptr) {
		error(node,
		      Meta_property_0_is_only_allowed_in_the_body_of_a_function_declaration_function_expression_or_constructor,
		      "new.target");
		return errorType;
	}
	if (isConstructorDeclaration(container)) {
		Symbol* symbol = getSymbolOfDeclaration(container->parent);
		return getTypeOfSymbol(symbol);
	}
	Symbol* symbol = getSymbolOfDeclaration(container);
	return getTypeOfSymbol(symbol);
}

Type* Checker::checkImportMetaProperty(Node* node) {
	if (ModuleKind::Node16 <= moduleKind && moduleKind <= ModuleKind::NodeNext) {
		SourceFile* sourceFile = getSourceFileOfNode(node);
		// Go reads program.GetSourceFileMetaData(file.Path()).ImpliedNodeFormat;
		// the C++ Program surface exposes the same value via GetImpliedNodeFormatForEmit.
		if (program->GetImpliedNodeFormatForEmit(sourceFile) !=
		    ModuleKind::ESNext) {
			error(node,
			      The_import_meta_meta_property_is_not_allowed_in_files_which_will_build_into_CommonJS_output);
		}
	} else if (moduleKind < ModuleKind::ES2020 &&
	           moduleKind != ModuleKind::System) {
		error(node,
		      The_import_meta_meta_property_is_only_allowed_when_the_module_option_is_es2020_es2022_esnext_system_node16_node18_node20_or_nodenext);
	}
	SourceFile* file = getSourceFileOfNode(node);
	TSC_ASSERT((file->flags & NodeFlagsPossiblyContainsImportMeta) != 0,
	           "Containing file is missing import meta node flag.");
	if (node->name()->text() == "meta") {
		return getGlobalImportMetaType();
	}
	return errorType;
}

Type* Checker::checkMetaPropertyKeyword(Node* node) {
	// !!! This is effectively a helper for GetSymbolAtLocation and GetTypeAtLocation
	return errorType;
}

Type* Checker::checkDeleteExpression(Node* node) {
	checkExpression(node->expression());
	Node* expr = skipParentheses(node->expression());
	if (!isAccessExpression(expr)) {
		error(expr, The_operand_of_a_delete_operator_must_be_a_property_reference);
		return booleanType;
	}
	if (isPropertyAccessExpression(expr) && isPrivateIdentifier(expr->name())) {
		error(expr,
		      The_operand_of_a_delete_operator_cannot_be_a_private_identifier);
	}
	Symbol* symbol =
	    getExportSymbolOfValueSymbolIfExported(getResolvedSymbolOrNil(expr));
	if (symbol != nullptr) {
		if (isReadonlySymbol(symbol)) {
			error(expr,
			      The_operand_of_a_delete_operator_cannot_be_a_read_only_property);
		} else {
			checkDeleteExpressionMustBeOptional(expr, symbol);
		}
	}
	return booleanType;
}

void Checker::checkDeleteExpressionMustBeOptional(Node* expr, Symbol* symbol) {
	Type* t = getTypeOfSymbol(symbol);
	if (strictNullChecks &&
	    (t->flags & (TypeFlagsAnyOrUnknown | TypeFlagsNever)) == 0) {
		bool isOptional;
		if (exactOptionalPropertyTypes) {
			isOptional = (symbol->flags & SymbolFlagsOptional) != 0;
		} else {
			isOptional = hasTypeFacts(t, TypeFactsIsUndefined);
		}
		if (!isOptional) {
			error(expr, The_operand_of_a_delete_operator_must_be_optional);
		}
	}
}

Type* Checker::checkVoidExpression(Node* node) {
	checkNodeDeferred(node);
	return undefinedWideningType;
}

Type* Checker::checkAwaitExpression(Node* node) {
	checkGrammarAwaitOrAwaitUsing(node);
	Type* operandType = checkExpression(node->expression());
	Type* awaitedType = checkAwaitedType(
	    operandType, true /*withAlias*/, node,
	    Type_of_await_operand_must_either_be_a_valid_promise_or_must_not_contain_a_callable_then_member);
	if (awaitedType == operandType && !isErrorType(awaitedType) &&
	    (operandType->flags & TypeFlagsAnyOrUnknown) == 0) {
		addErrorOrSuggestion(
		    false,
		    NewDiagnosticForNode(
		        node, X_await_has_no_effect_on_the_type_of_this_expression, {}));
	}
	return awaitedType;
}

Type* Checker::checkPrefixUnaryExpression(Node* node) {
	PrefixUnaryExpression* expr = node->as<PrefixUnaryExpression>();
	Type* operandType = checkExpression(expr->Operand);
	if (operandType == silentNeverType) {
		return silentNeverType;
	}
	switch (expr->Operand->kind) {
	case Kind::NumericLiteral:
		switch (expr->Operator) {
		case Kind::MinusToken:
			return getFreshTypeOfLiteralType(
			    getNumberLiteralType(Number{-numberFromString(expr->Operand->text()).v}));
		case Kind::PlusToken:
			return getFreshTypeOfLiteralType(
			    getNumberLiteralType(numberFromString(expr->Operand->text())));
		default:
			break;
		}
		break;
	case Kind::BigIntLiteral:
		if (expr->Operator == Kind::MinusToken) {
			return getFreshTypeOfLiteralType(getBigIntLiteralType(
			    PseudoBigInt::create(parsePseudoBigInt(expr->Operand->text()),
			                         true /*negative*/)));
		}
		break;
	default:
		break;
	}
	switch (expr->Operator) {
	case Kind::PlusToken:
	case Kind::MinusToken:
	case Kind::TildeToken: {
		checkNonNullType(operandType, expr->Operand);
		if (maybeTypeOfKindConsideringBaseConstraint(operandType,
		                                             TypeFlagsESSymbolLike)) {
			error(expr->Operand, The_0_operator_cannot_be_applied_to_type_symbol,
			      std::string(tokenToString(expr->Operator)));
		}
		if (expr->Operator == Kind::PlusToken) {
			if (maybeTypeOfKindConsideringBaseConstraint(operandType,
			                                             TypeFlagsBigIntLike)) {
				error(expr->Operand, Operator_0_cannot_be_applied_to_type_1,
				      {std::string(tokenToString(expr->Operator)),
				       TypeToString(getBaseTypeOfLiteralType(operandType))});
			}
			return numberType;
		}
		return getUnaryResultType(operandType);
	}
	case Kind::ExclamationToken: {
		checkTruthinessOfType(operandType, expr->Operand);
		TypeFacts facts =
		    getTypeFacts(operandType, TypeFactsTruthy | TypeFactsFalsy);
		if (facts == TypeFactsTruthy) {
			return falseType;
		} else if (facts == TypeFactsFalsy) {
			return trueType;
		} else {
			return booleanType;
		}
	}
	case Kind::PlusPlusToken:
	case Kind::MinusMinusToken: {
		bool ok = checkArithmeticOperandType(
		    expr->Operand, checkNonNullType(operandType, expr->Operand),
		    An_arithmetic_operand_must_be_of_type_any_number_bigint_or_an_enum_type,
		    false);
		if (ok) {
			// run check only if former checks succeeded to avoid reporting cascading errors
			checkReferenceExpression(
			    expr->Operand,
			    The_operand_of_an_increment_or_decrement_operator_must_be_a_variable_or_a_property_access,
			    The_operand_of_an_increment_or_decrement_operator_may_not_be_an_optional_property_access);
		}
		return getUnaryResultType(operandType);
	}
	default:
		break;
	}
	return errorType;
}

Type* Checker::checkPostfixUnaryExpression(Node* node) {
	PostfixUnaryExpression* expr = node->as<PostfixUnaryExpression>();
	Type* operandType = checkExpression(expr->Operand);
	if (operandType == silentNeverType) {
		return silentNeverType;
	}
	bool ok = checkArithmeticOperandType(
	    expr->Operand, checkNonNullType(operandType, expr->Operand),
	    An_arithmetic_operand_must_be_of_type_any_number_bigint_or_an_enum_type,
	    false);
	if (ok) {
		// run check only if former checks succeeded to avoid reporting cascading errors
		checkReferenceExpression(
		    expr->Operand,
		    The_operand_of_an_increment_or_decrement_operator_must_be_a_variable_or_a_property_access,
		    The_operand_of_an_increment_or_decrement_operator_may_not_be_an_optional_property_access);
	}
	return getUnaryResultType(operandType);
}

Type* Checker::getUnaryResultType(Type* operandType) {
	if (maybeTypeOfKind(operandType, TypeFlagsBigIntLike)) {
		if (isTypeAssignableToKind(operandType, TypeFlagsAnyOrUnknown) ||
		    maybeTypeOfKind(operandType, TypeFlagsNumberLike)) {
			return numberOrBigIntType;
		}
		return bigintType;
	}
	// If it's not a bigint type, implicit coercion will result in a number
	return numberType;
}

Type* Checker::checkConditionalExpression(Node* node, CheckMode checkMode) {
	ConditionalExpression* cond = node->as<ConditionalExpression>();
	Type* t = checkTruthinessExpression(cond->Condition, checkMode);
	checkTestingKnownTruthyCallableOrAwaitableOrEnumMemberType(
	    cond->Condition, t, cond->WhenTrue);
	Type* type1 = checkExpressionEx(cond->WhenTrue, checkMode);
	Type* type2 = checkExpressionEx(cond->WhenFalse, checkMode);
	return getUnionTypeEx({type1, type2}, UnionReductionSubtype, nullptr, nullptr);
}

Type* Checker::checkTruthinessExpression(Node* node, CheckMode checkMode) {
	return checkTruthinessOfType(checkExpressionEx(node, checkMode), node);
}

Type* Checker::checkSpreadExpression(Node* node, CheckMode checkMode) {
	Type* arrayOrIterableType = checkExpressionEx(node->expression(), checkMode);
	return checkIteratedTypeOrElementType(IterationUseSpread,
	                                      arrayOrIterableType, undefinedType,
	                                      node->expression());
}

Type* Checker::checkYieldExpression(Node* node) {
	checkGrammarYieldExpression(node);
	// Always check the operand so its identifiers are resolved even when the yield is
	// outside a generator, keeping diagnostics stable regardless of traversal order.
	Type* yieldExpressionType;
	if (node->expression() != nullptr) {
		yieldExpressionType = checkExpression(node->expression());
	} else {
		yieldExpressionType = undefinedWideningType;
	}
	Node* fn = getContainingFunction(node);
	if (fn == nullptr) {
		return anyType;
	}
	FunctionFlags functionFlags = getFunctionFlags(fn);
	if ((functionFlags & FunctionFlagsGenerator) == 0) {
		// If the user's code is syntactically correct, the func should always have a star. After all, we are in a yield context.
		return anyType;
	}
	bool isAsync = (functionFlags & FunctionFlagsAsync) != 0;
	if (node->as<YieldExpression>()->AsteriskToken != nullptr) {
		// Async generator functions prior to ES2018 require the __await, __asyncDelegator,
		// and __asyncValues helpers
		if (isAsync &&
		    languageVersion < LanguageFeatureMinimumTarget.AsyncGenerators) {
			checkExternalEmitHelpers(node,
			                         ExternalEmitHelpersAsyncDelegatorIncludes);
		}
	}
	// There is no point in doing an assignability check if the function
	// has no explicit return type because the return type is directly computed
	// from the yield expressions.
	Type* returnType = getReturnTypeFromAnnotation(fn);
	if (returnType != nullptr && (returnType->flags & TypeFlagsUnion) != 0) {
		returnType = filterType(returnType, [&](Type* t) {
			return checkGeneratorInstantiationAssignabilityToReturnType(
			    t, functionFlags, nullptr /*errorNode*/);
		});
	}
	IterationTypes iterationTypes{};
	if (returnType != nullptr) {
		iterationTypes = getIterationTypesOfGeneratorFunctionReturnType(
		    returnType, isAsync);
	}
	Type* signatureYieldType = iterationTypes.yieldType != nullptr
	                               ? iterationTypes.yieldType
	                               : anyType;
	Type* signatureNextType = iterationTypes.nextType != nullptr
	                              ? iterationTypes.nextType
	                              : anyType;
	Type* yieldedType = getYieldedTypeOfYieldExpression(
	    node, yieldExpressionType, signatureNextType, isAsync);
	if (returnType != nullptr && yieldedType != nullptr) {
		checkTypeAssignableToAndOptionallyElaborate(
		    yieldedType, signatureYieldType,
		    node->expression() != nullptr ? node->expression() : node,
		    node->expression(), nullptr, nullptr);
	}
	if (node->as<YieldExpression>()->AsteriskToken != nullptr) {
		IterationUse use =
		    isAsync ? IterationUseAsyncYieldStar : IterationUseYieldStar;
		Type* t = getIterationTypeOfIterable(use, IterationTypeKind::Return,
		                                     yieldExpressionType,
		                                     node->expression());
		return t != nullptr ? t : anyType;
	}
	if (returnType != nullptr) {
		Type* t = getIterationTypeOfGeneratorFunctionReturnType(
		    IterationTypeKind::Next, returnType, isAsync);
		return t != nullptr ? t : anyType;
	}
	Type* t = getContextualIterationType(IterationTypeKind::Next, fn);
	if (t == nullptr) {
		t = anyType;
		if (noImplicitAny && !expressionResultIsUnused(node)) {
			Type* contextualType = getContextualType(node, ContextFlagsNone);
			if (contextualType == nullptr || isTypeAny(contextualType)) {
				error(node,
				      X_yield_expression_implicitly_results_in_an_any_type_because_its_containing_generator_lacks_a_return_type_annotation);
			}
		}
	}
	return t;
}

Type* Checker::getYieldedTypeOfYieldExpression(Node* node,
                                               Type* expressionType,
                                               Type* sentType, bool isAsync) {
	Node* errorNode =
	    node->expression() != nullptr ? node->expression() : node;
	bool isYieldStar = node->as<YieldExpression>()->AsteriskToken != nullptr;
	// A `yield*` expression effectively yields everything that its operand yields
	Type* yieldedType = expressionType;
	if (isYieldStar) {
		yieldedType = checkIteratedTypeOrElementType(
		    isAsync ? IterationUseAsyncYieldStar : IterationUseYieldStar,
		    expressionType, sentType, errorNode);
	}
	if (!isAsync) {
		return yieldedType;
	}
	return getAwaitedTypeEx(yieldedType, errorNode,
	                        isYieldStar
	                            ? Type_of_iterated_elements_of_a_yield_Asterisk_operand_must_either_be_a_valid_promise_or_must_not_contain_a_callable_then_member
	                            : Type_of_yield_operand_in_an_async_generator_must_either_be_a_valid_promise_or_must_not_contain_a_callable_then_member);
}

Type* Checker::checkSyntheticExpression(Node* node) {
	Type* t =
	    static_cast<Type*>(node->as<SyntheticExpression>()->Type);
	if (node->as<SyntheticExpression>()->IsSpread) {
		return getIndexedAccessType(t, numberType);
	}
	return t;
}

Type* Checker::checkIdentifier(Node* node, CheckMode checkMode) {
	if (isThisInTypeQuery(node)) {
		return checkThisExpression(node);
	}
	Symbol* symbol = getResolvedSymbol(node);
	if (symbol == unknownSymbol) {
		return errorType;
	}
	if (symbol == argumentsSymbol) {
		if (isInPropertyInitializerOrClassStaticBlock(node,
		                                            true /*ignoreArrowFunctions*/)) {
			error(node,
			      X_arguments_cannot_be_referenced_in_property_initializers_or_class_static_initialization_blocks);
			return errorType;
		}
		return getTypeOfSymbol(symbol);
	}
	if (shouldMarkIdentifierAliasReferenced(node)) {
		markLinkedReferences(node, ReferenceHint::Identifier,
		                     nullptr /*propSymbol*/, nullptr /*parentType*/);
	}
	Symbol* localOrExportSymbol = getExportSymbolOfValueSymbolIfExported(symbol);
	Symbol* targetSymbol =
	    resolveAliasWithDeprecationCheck(localOrExportSymbol, node);
	if (!targetSymbol->declarations.empty() && isDeprecatedSymbol(targetSymbol) &&
	    isUncalledFunctionReference(node, targetSymbol)) {
		addDeprecatedSuggestion(node, targetSymbol->declarations, node->text());
	}
	Node* declaration = localOrExportSymbol->valueDeclaration;
	Node* immediateDeclaration = declaration;
	// If the identifier is declared in a binding pattern for which we're currently computing the implied type and the
	// reference occurs with the same binding pattern, return the non-inferrable any type. This for example occurs in
	// 'const [a, b = a + 1] = [2]' when we're computing the contextual type for the array literal '[2]'.
	if (declaration != nullptr &&
	    declaration->kind == Kind::BindingElement &&
	    std::find(contextualBindingPatterns.begin(),
	              contextualBindingPatterns.end(),
	              declaration->parent) != contextualBindingPatterns.end() &&
	    findAncestor(node, [&](Node* parent) { return parent == declaration->parent; }) !=
	        nullptr) {
		return nonInferrableAnyType;
	}
	Type* t = getNarrowedTypeOfSymbol(localOrExportSymbol, node);
	AssignmentKind assignmentKind = getAssignmentTargetKind(node);
	if (assignmentKind != AssignmentKindNone) {
		if ((localOrExportSymbol->flags & SymbolFlagsVariable) == 0 &&
		    !(isInJSFile(node) && (localOrExportSymbol->flags &
		                           SymbolFlagsValueModule) != 0)) {
			const DiagnosticMessage* assignmentError;
			if ((localOrExportSymbol->flags & SymbolFlagsEnum) != 0) {
				assignmentError = Cannot_assign_to_0_because_it_is_an_enum;
			} else if ((localOrExportSymbol->flags & SymbolFlagsClass) != 0) {
				assignmentError = Cannot_assign_to_0_because_it_is_a_class;
			} else if ((localOrExportSymbol->flags & SymbolFlagsModule) != 0) {
				assignmentError = Cannot_assign_to_0_because_it_is_a_namespace;
			} else if ((localOrExportSymbol->flags & SymbolFlagsFunction) != 0) {
				assignmentError = Cannot_assign_to_0_because_it_is_a_function;
			} else if ((localOrExportSymbol->flags & SymbolFlagsAlias) != 0) {
				assignmentError = Cannot_assign_to_0_because_it_is_an_import;
			} else {
				assignmentError = Cannot_assign_to_0_because_it_is_not_a_variable;
			}
			error(node, assignmentError, symbolToString(symbol));
			return errorType;
		}
		if (isReadonlySymbol(localOrExportSymbol)) {
			if ((localOrExportSymbol->flags & SymbolFlagsVariable) != 0) {
				error(node, Cannot_assign_to_0_because_it_is_a_constant,
				      symbolToString(symbol));
			} else {
				error(node,
				      Cannot_assign_to_0_because_it_is_a_read_only_property,
				      symbolToString(symbol));
			}
			return errorType;
		}
	}
	bool isAlias = (localOrExportSymbol->flags & SymbolFlagsAlias) != 0;
	// We only narrow variables and parameters occurring in a non-assignment position. For all other
	// entities we simply return the declared type.
	if ((localOrExportSymbol->flags & SymbolFlagsVariable) != 0) {
		if (assignmentKind == AssignmentKindDefinite) {
			if (isInCompoundLikeAssignment(node)) {
				return getBaseTypeOfLiteralType(t);
			}
			return t;
		}
	} else if (isAlias) {
		declaration = getDeclarationOfAliasSymbol(symbol);
	} else {
		return t;
	}
	if (declaration == nullptr) {
		return t;
	}
	t = getNarrowableTypeForReference(t, node, checkMode);
	// The declaration container is the innermost function that encloses the declaration of the variable
	// or parameter. The flow container is the innermost function starting with which we analyze the control
	// flow graph to determine the control flow based type.
	bool isParameter =
	    getRootDeclaration(declaration)->kind == Kind::Parameter;
	Node* declarationContainer = getControlFlowContainer(declaration);
	Node* flowContainer = getControlFlowContainer(node);
	bool isOuterVariable = flowContainer != declarationContainer;
	bool isSpreadDestructuringAssignmentTarget =
	    node->parent != nullptr && node->parent->parent != nullptr &&
	    isSpreadAssignment(node->parent) &&
	    isDestructuringAssignmentTarget(node->parent->parent);
	bool isModuleExports =
	    (symbol->flags & SymbolFlagsModuleExports) != 0;
	bool typeIsAutomatic = t == autoType || t == autoArrayType;
	bool isAutomaticTypeInNonNull =
	    typeIsAutomatic && node->parent->kind == Kind::NonNullExpression;
	// When the control flow originates in a function expression, arrow function, method, or accessor, and
	// we are referencing a closed-over const variable or parameter or mutable local variable past its last
	// assignment, we extend the origin of the control flow analysis to include the immediately enclosing
	// control flow container.
	while (flowContainer != declarationContainer &&
	       (isFunctionExpressionOrArrowFunction(flowContainer) ||
	        isObjectLiteralOrClassExpressionMethodOrAccessor(flowContainer)) &&
	       ((isConstantVariable(localOrExportSymbol) && t != autoArrayType) ||
	        (isParameterOrMutableLocalVariable(localOrExportSymbol) &&
	         isPastLastAssignment(localOrExportSymbol, node)))) {
		flowContainer = getControlFlowContainer(flowContainer);
	}
	// We only look for uninitialized variables in strict null checking mode, and only when we can analyze the
	// entire control flow graph from the variable's declaration (i.e. when the flow container and
	// declaration container are the same).
	bool isNeverInitialized =
	    immediateDeclaration != nullptr &&
	    isVariableDeclaration(immediateDeclaration) &&
	    !isForInOrOfStatement(immediateDeclaration->parent->parent) &&
	    immediateDeclaration->initializer() == nullptr &&
	    immediateDeclaration->as<VariableDeclaration>()->ExclamationToken ==
	        nullptr &&
	    isMutableLocalVariableDeclaration(immediateDeclaration) &&
	    !isSymbolAssignedDefinitely(symbol);
	bool assumeInitialized =
	    isParameter || isAlias || (isOuterVariable && !isNeverInitialized) ||
	    isSpreadDestructuringAssignmentTarget || isModuleExports ||
	    isSameScopedBindingElement(node, declaration) ||
	    (t != autoType && t != autoArrayType &&
	     (!strictNullChecks ||
	      (t->flags & (TypeFlagsAnyOrUnknown | TypeFlagsVoid)) != 0 ||
	      isInTypeQuery(node) || isInAmbientOrTypeNode(node) ||
	      node->parent->kind == Kind::ExportSpecifier)) ||
	    isNonNullExpression(node->parent) ||
	    (isVariableDeclaration(declaration) &&
	     declaration->as<VariableDeclaration>()->ExclamationToken != nullptr) ||
	    (declaration->flags & NodeFlagsAmbient) != 0;
	Type* initialType;
	if (isAutomaticTypeInNonNull) {
		initialType = undefinedType;
	} else if (assumeInitialized && isParameter) {
		initialType = removeOptionalityFromDeclaredType(t, declaration);
	} else if (assumeInitialized) {
		initialType = t;
	} else if (typeIsAutomatic) {
		initialType = undefinedType;
	} else {
		initialType = getOptionalType(t, false /*isProperty*/);
	}
	Type* flowType;
	if (isAutomaticTypeInNonNull) {
		flowType = GetNonNullableType(
		    getFlowTypeOfReferenceEx(node, t, initialType, flowContainer, nullptr));
	} else {
		flowType =
		    getFlowTypeOfReferenceEx(node, t, initialType, flowContainer, nullptr);
	}
	// A variable is considered uninitialized when it is possible to analyze the entire control flow graph
	// from declaration to use, and when the variable's declared type doesn't include undefined but the
	// control flow based type does include undefined.
	if (!isEvolvingArrayOperationTarget(node) &&
	    (t == autoType || t == autoArrayType)) {
		if (flowType == autoType || flowType == autoArrayType) {
			if (noImplicitAny) {
				error(getNameOfDeclaration(declaration),
				      Variable_0_implicitly_has_type_1_in_some_locations_where_its_type_cannot_be_determined,
				      {symbolToString(symbol), TypeToString(flowType)});
				error(node, Variable_0_implicitly_has_an_1_type,
				      {symbolToString(symbol), TypeToString(flowType)});
			}
			return convertAutoToAny(flowType);
		}
	} else if (!assumeInitialized && !containsUndefinedType(t) &&
	           containsUndefinedType(flowType)) {
		error(node, Variable_0_is_used_before_being_assigned,
		      symbolToString(symbol));
		// Return the declared type to reduce follow-on errors
		return t;
	}
	if (assignmentKind != AssignmentKindNone) {
		// Identifier is target of a compound assignment
		return getBaseTypeOfLiteralType(flowType);
	}
	return flowType;
}

bool Checker::isSameScopedBindingElement(Node* node, Node* declaration) {
	if (isBindingElement(declaration)) {
		Node* bindingElement = findAncestor(node, isBindingElement);
		return bindingElement != nullptr &&
		       getRootDeclaration(bindingElement) ==
		           getRootDeclaration(declaration);
	}
	return false;
}

// Remove undefined from the annotated type of a parameter when there is an initializer (that doesn't include undefined)
Type* Checker::removeOptionalityFromDeclaredType(Type* declaredType,
                                                 Node* declaration) {
	bool removeUndefined =
	    strictNullChecks && isParameterDeclaration(declaration) &&
	    declaration->initializer() != nullptr &&
	    hasTypeFacts(declaredType, TypeFactsIsUndefined) &&
	    !parameterInitializerContainsUndefined(declaration);
	if (removeUndefined) {
		return getTypeWithFacts(declaredType, TypeFactsNEUndefined);
	}
	return declaredType;
}

bool Checker::parameterInitializerContainsUndefined(Node* declaration) {
	NodeLinks* links = nodeLinks.Get(declaration);
	if ((links->flags & NodeCheckFlagsInitializerIsUndefinedComputed) == 0) {
		if (!pushTypeResolution(declaration,
		                        TypeSystemPropertyName::InitializerIsUndefined)) {
			reportCircularityError(declaration->symbol());
			return true;
		}
		bool containsUndefined =
		    hasTypeFacts(checkDeclarationInitializer(declaration, CheckModeNormal,
		                                             nullptr),
		                 TypeFactsIsUndefined);
		if (!popTypeResolution()) {
			reportCircularityError(declaration->symbol());
			return true;
		}
		if ((links->flags & NodeCheckFlagsInitializerIsUndefinedComputed) == 0) {
			links->flags |= NodeCheckFlagsInitializerIsUndefinedComputed |
			                (containsUndefined
			                     ? NodeCheckFlagsInitializerIsUndefined
			                     : NodeCheckFlagsNone);
		}
	}
	return (links->flags & NodeCheckFlagsInitializerIsUndefined) != 0;
}

// isInAmbientOrTypeNode — already real in checker.cpp (checker.go:11436).

Type* Checker::checkPropertyAccessExpression(Node* node, CheckMode checkMode,
                                             bool writeOnly) {
	if ((node->flags & NodeFlagsOptionalChain) != 0) {
		return checkPropertyAccessChain(node, checkMode);
	}
	Node* expr = node->expression();
	return checkPropertyAccessExpressionOrQualifiedName(
	    node, expr, checkNonNullExpression(expr),
	    node->as<PropertyAccessExpression>()->name, checkMode, writeOnly);
}

Type* Checker::checkPropertyAccessChain(Node* node, CheckMode checkMode) {
	Type* leftType = checkExpression(node->expression());
	Type* nonOptionalType =
	    getOptionalExpressionType(leftType, node->expression());
	return propagateOptionalTypeMarker(
	    checkPropertyAccessExpressionOrQualifiedName(
	        node, node->expression(),
	        checkNonNullType(nonOptionalType, node->expression()),
	        node->name(), checkMode, false),
	    node, nonOptionalType != leftType);
}

Type* Checker::checkPropertyAccessExpressionOrQualifiedName(
    Node* node, Node* left, Type* leftType, Node* right, CheckMode checkMode,
    bool writeOnly) {
	Symbol* parentSymbol = getResolvedSymbolOrNil(left);
	AssignmentKind assignmentKind = getAssignmentTargetKind(node);
	Type* widenedType = leftType;
	if (assignmentKind != AssignmentKindNone || isMethodAccessForCall(node)) {
		widenedType = getWidenedType(leftType);
	}
	Type* apparentType = getApparentType(widenedType);
	bool isAnyLike = isTypeAny(apparentType) || apparentType == silentNeverType;
	Symbol* prop = nullptr;
	if (isPrivateIdentifier(right)) {
		if (languageVersion <
		        LanguageFeatureMinimumTarget.PrivateNamesAndClassStaticBlocks ||
		    languageVersion <
		        LanguageFeatureMinimumTarget.ClassAndClassElementDecorators ||
		    !compilerOptions->GetUseDefineForClassFields()) {
			if (assignmentKind != AssignmentKindNone) {
				checkExternalEmitHelpers(node,
				                         ExternalEmitHelpersClassPrivateFieldSet);
			}
			if (assignmentKind != AssignmentKindDefinite) {
				checkExternalEmitHelpers(node,
				                         ExternalEmitHelpersClassPrivateFieldGet);
			}
		}
		Symbol* lexicallyScopedSymbol =
		    lookupSymbolForPrivateIdentifierDeclaration(right->text(), right);
		if (assignmentKind != AssignmentKindNone &&
		    lexicallyScopedSymbol != nullptr &&
		    lexicallyScopedSymbol->valueDeclaration != nullptr &&
		    isMethodDeclaration(lexicallyScopedSymbol->valueDeclaration)) {
			grammarErrorOnNode(
			    right,
			    Cannot_assign_to_private_method_0_Private_methods_are_not_writable,
			    {right->text()});
		}
		if (isAnyLike) {
			if (lexicallyScopedSymbol != nullptr) {
				if (isErrorType(apparentType)) {
					return errorType;
				}
				return apparentType;
			}
			if (getContainingClassExcludingClassDecorators(right) == nullptr) {
				grammarErrorOnNode(
				    right, Private_identifiers_are_not_allowed_outside_class_bodies);
				return anyType;
			}
		}
		if (lexicallyScopedSymbol != nullptr) {
			prop = getPrivateIdentifierPropertyOfType(leftType,
			                                        lexicallyScopedSymbol);
		}
		if (prop == nullptr) {
			// Check for private-identifier-specific shadowing and lexical-scoping errors.
			if (checkPrivateIdentifierPropertyAccess(leftType, right,
			                                         lexicallyScopedSymbol)) {
				return errorType;
			}
			Node* containingClass =
			    getContainingClassExcludingClassDecorators(right);
			if (containingClass != nullptr &&
			    isPlainJSFile(getSourceFileOfNode(containingClass),
			                  compilerOptions->CheckJs)) {
				grammarErrorOnNode(
				    right, Private_field_0_must_be_declared_in_an_enclosing_class,
				    {right->text()});
			}
		} else {
			bool isSetonlyAccessor =
			    (prop->flags & SymbolFlagsSetAccessor) != 0 &&
			    (prop->flags & SymbolFlagsGetAccessor) == 0;
			if (isSetonlyAccessor && assignmentKind != AssignmentKindDefinite) {
				error(node, Private_accessor_was_defined_without_a_getter);
			}
		}
	} else {
		if (isAnyLike) {
			if (isIdentifier(left) && parentSymbol != nullptr) {
				markLinkedReferences(node, ReferenceHint::Property,
				                     nullptr /*propSymbol*/, leftType);
			}
			if (isErrorType(apparentType)) {
				return errorType;
			}
			return apparentType;
		}
		prop = getPropertyOfTypeEx(
		    apparentType, right->text(),
		    isConstEnumObjectType(apparentType) /*skipObjectFunctionPropertyAugment*/,
		    node->kind == Kind::QualifiedName /*includeTypeOnlyMembers*/);
	}
	markLinkedReferences(node, ReferenceHint::Property, prop, leftType);
	Type* propType = nullptr;
	if (prop == nullptr) {
		IndexInfo* indexInfo = nullptr;
		if (!isPrivateIdentifier(right) &&
		    (assignmentKind == AssignmentKindNone ||
		     !isGenericObjectType(leftType) || isThisTypeParameter(leftType))) {
			indexInfo =
			    getApplicableIndexInfoForName(apparentType, right->text());
		}
		if (indexInfo == nullptr) {
			bool isUncheckedJS =
			    isUncheckedJSSuggestion(node, leftType->symbol,
			                            true /*excludeClasses*/);
			if (!isUncheckedJS && isJSLiteralType(leftType)) {
				return anyType;
			}
			if (leftType->symbol == globalThisSymbol) {
				Symbol* globalSymbol = nullptr;
				auto it = globalThisSymbol->exports.find(right->text());
				if (it != globalThisSymbol->exports.end()) {
					globalSymbol = it->second;
				}
				if (globalSymbol != nullptr &&
				    (globalSymbol->flags & SymbolFlagsBlockScoped) != 0) {
					error(right, Property_0_does_not_exist_on_type_1,
					      {right->text(), TypeToString(leftType)});
				} else if (noImplicitAny) {
					error(right,
					      Element_implicitly_has_an_any_type_because_type_0_has_no_index_signature,
					      TypeToString(leftType));
				}
				return anyType;
			}
			if (!right->text().empty() &&
			    !checkAndReportErrorForExtendingInterface(node)) {
				// Go captures the locals by value; [&] would dangle once
				// this frame returns before the deferred diagnostic runs.
				addDeferredDiagnostic([right, leftType, apparentType, isUncheckedJS, this]() {
					// must be deferred because reporting this error can cause us to materialize the containing type completely (to print it), leading to erroneous circularity errors
					reportNonexistentProperty(
					    right,
					    isThisTypeParameter(leftType) ? apparentType : leftType,
					    isUncheckedJS);
				});
			}
			return errorType;
		}
		if (indexInfo->isReadonly &&
		    (isAssignmentTarget(node) || isDeleteTarget(node))) {
			error(node, Index_signature_in_type_0_only_permits_reading,
			      TypeToString(apparentType));
		}
		propType = indexInfo->valueType;
		if (compilerOptions->NoUncheckedIndexedAccess == Tristate::True &&
		    getAssignmentTargetKind(node) != AssignmentKindDefinite) {
			propType = getUnionType({propType, missingType});
		}
		if (compilerOptions->NoPropertyAccessFromIndexSignature ==
		        Tristate::True &&
		    isPropertyAccessExpression(node)) {
			error(right,
			      Property_0_comes_from_an_index_signature_so_it_must_be_accessed_with_0,
			      right->text());
		}
		if (indexInfo->declaration != nullptr &&
		    IsDeprecatedDeclaration(indexInfo->declaration)) {
			addDeprecatedSuggestion(right, {indexInfo->declaration},
			                        right->text());
		}
	} else {
		Symbol* targetPropSymbol = resolveAliasWithDeprecationCheck(prop, right);
		if (isDeprecatedSymbol(targetPropSymbol) &&
		    isUncalledFunctionReference(node, targetPropSymbol) &&
		    targetPropSymbol->declarations.size() != 0) {
			addDeprecatedSuggestion(right, targetPropSymbol->declarations,
			                        right->text());
		}
		checkPropertyNotUsedBeforeDeclaration(prop, node, right);
		markPropertyAsReferenced(prop, node,
		                         isSelfTypeAccess(left, parentSymbol));
		auto* propLinks = symbolNodeLinks.Get(node);
		propLinks->resolvedSymbol = prop;
		propLinks->resolvedSymbolCheckFile = checkFileTag();
		checkPropertyAccessibility(node, left->kind == Kind::SuperKeyword,
		                           isWriteAccess(node), apparentType, prop);
		if (isAssignmentToReadonlyEntity(node, prop, assignmentKind)) {
			error(right, Cannot_assign_to_0_because_it_is_a_read_only_property,
			      right->text());
			return errorType;
		}
		if (isThisPropertyAccessInConstructor(node, prop)) {
			propType = autoType;
		} else if (writeOnly || isWriteOnlyAccess(node)) {
			propType = getWriteTypeOfSymbol(prop);
		} else {
			propType = getTypeOfSymbol(prop);
		}
	}
	return getFlowTypeOfAccessExpression(node, prop, propType, right, checkMode);
}

Type* Checker::getFlowTypeOfAccessExpression(Node* node, Symbol* prop,
                                             Type* propType, Node* errorNode,
                                             CheckMode checkMode) {
	// Only compute control flow type if this is a property access expression that isn't an
	// assignment target, and the referenced property was declared as a variable, property,
	// accessor, or optional method.
	AssignmentKind assignmentKind = getAssignmentTargetKind(node);
	if (assignmentKind == AssignmentKindDefinite) {
		return removeMissingType(
		    propType,
		    prop != nullptr && (prop->flags & SymbolFlagsOptional) != 0);
	}
	if (prop != nullptr &&
	    (prop->flags & (SymbolFlagsVariable | SymbolFlagsProperty |
	                    SymbolFlagsAccessor)) == 0 &&
	    !((prop->flags & SymbolFlagsMethod) != 0 &&
	      (propType->flags & TypeFlagsUnion) != 0)) {
		return propType;
	}
	if (propType == autoType) {
		return getFlowTypeOfProperty(node, prop);
	}
	propType = getNarrowableTypeForReference(propType, node, checkMode);
	// If strict null checks and strict property initialization checks are enabled, if we have
	// a this.xxx property access, if the property is an instance property without an initializer,
	// and if we are in a constructor of the same class as the property declaration, assume that
	// the property is uninitialized at the top of the control flow.
	bool assumeUninitialized = false;
	if (strictNullChecks && prop != nullptr) {
		Node* declaration = prop->valueDeclaration;
		if (declaration != nullptr) {
			if (strictPropertyInitialization && isAccessExpression(node) &&
			    node->expression()->kind == Kind::ThisKeyword &&
			    isPropertyWithoutInitializer(declaration) &&
			    !isStatic(declaration)) {
				Node* flowContainer = getControlFlowContainer(node);
				if (isConstructorDeclaration(flowContainer) &&
				    flowContainer->parent == declaration->parent &&
				    (declaration->flags & NodeFlagsAmbient) == 0) {
					assumeUninitialized = true;
				}
			} else if (isBinaryExpression(declaration) &&
			           isPropertyAccessExpression(
			               declaration->as<BinaryExpression>()->Left) &&
			           getControlFlowContainer(node) ==
			               getControlFlowContainer(declaration)) {
				assumeUninitialized = true;
			}
		}
	}
	Type* flowType = getFlowTypeOfReferenceEx(
	    node, propType,
	    addOptionalityEx(propType, false /*isProperty*/, assumeUninitialized),
	    nullptr, nullptr);
	if (assumeUninitialized && !containsUndefinedType(propType) &&
	    containsUndefinedType(flowType)) {
		error(errorNode, Property_0_is_used_before_being_assigned,
		      symbolToString(prop));
		// Return the declared type to reduce follow-on errors
		return propType;
	}
	if (assignmentKind != AssignmentKindNone) {
		return getBaseTypeOfLiteralType(flowType);
	}
	return flowType;
}

Node* Checker::getControlFlowContainer(Node* node) {
	return findAncestor(node->parent, [](Node* node) -> bool {
		return (isFunctionLike(node) &&
		        getImmediatelyInvokedFunctionExpression(node) == nullptr) ||
		       isModuleBlock(node) || isSourceFile(node) ||
		       isPropertyDeclaration(node);
	});
}

// (deduped: getFlowTypeOfProperty defined in the owning slice file)

// Return the inherited type of the given property or undefined if property doesn't exist in a base class.
Type* Checker::getTypeOfPropertyInBaseClass(Symbol* property) {
	Type* classType = getDeclaringClass(property);
	if (classType != nullptr) {
		std::vector<Type*> baseClassTypes = getBaseTypes(classType);
		if (!baseClassTypes.empty()) {
			return getTypeOfPropertyOfType(baseClassTypes[0], property->name);
		}
	}
	return nullptr;
}

bool Checker::isMethodAccessForCall(Node* node) {
	while (isParenthesizedExpression(node->parent)) {
		node = node->parent;
	}
	return isCallOrNewExpression(node->parent) &&
	       node->parent->expression() == node;
}

// Lookup the private identifier lexically.
Symbol* Checker::lookupSymbolForPrivateIdentifierDeclaration(
    std::string_view propName, Node* location) {
	for (Node* containingClass =
	         getContainingClassExcludingClassDecorators(location);
	     containingClass != nullptr;
	     containingClass = getContainingClass(containingClass)) {
		Symbol* symbol = containingClass->symbol();
		std::string name =
		    getSymbolNameForPrivateIdentifier(symbol, std::string(propName));
		auto it = symbol->members.find(name);
		if (it != symbol->members.end()) {
			return it->second;
		}
		auto it2 = symbol->exports.find(name);
		if (it2 != symbol->exports.end()) {
			return it2->second;
		}
	}
	return nullptr;
}

Symbol* Checker::getPrivateIdentifierPropertyOfType(
    Type* leftType, Symbol* lexicallyScopedIdentifier) {
	return getPropertyOfType(leftType, lexicallyScopedIdentifier->name);
}

bool Checker::checkPrivateIdentifierPropertyAccess(
    Type* leftType, Node* right, Symbol* lexicallyScopedIdentifier) {
	// Either the identifier could not be looked up in the lexical scope OR the lexically scoped identifier did not exist on the type.
	// Find a private identifier with the same description on the type.
	std::vector<Symbol*> properties = getPropertiesOfType(leftType);
	Symbol* propertyOnType = nullptr;
	for (Symbol* symbol : properties) {
		Node* decl = symbol->valueDeclaration;
		if (decl != nullptr && decl->name() != nullptr &&
		    isPrivateIdentifier(decl->name()) &&
		    decl->name()->text() == right->text()) {
			propertyOnType = symbol;
			break;
		}
	}
	std::string diagName = declarationNameToString(right);
	if (propertyOnType != nullptr) {
		Node* typeValueDecl = propertyOnType->valueDeclaration;
		Node* typeClass = getContainingClass(typeValueDecl);
		// We found a private identifier property with the same description.
		// Either:
		// - There is a lexically scoped private identifier AND it shadows the one we found on the type.
		// - It is an attempt to access the private identifier outside of the class.
		if (lexicallyScopedIdentifier != nullptr &&
		    lexicallyScopedIdentifier->valueDeclaration != nullptr) {
			Node* lexicalValueDecl = lexicallyScopedIdentifier->valueDeclaration;
			Node* lexicalClass = getContainingClass(lexicalValueDecl);
			if (findAncestor(lexicalClass,
			                 [&](Node* n) { return typeClass == n; }) != nullptr) {
				Diagnostic* diagnostic = error(
				    right,
				    The_property_0_cannot_be_accessed_on_type_1_within_this_class_because_it_is_shadowed_by_another_private_identifier_with_the_same_spelling,
				    {diagName, TypeToString(leftType)});
				diagnostic->AddRelatedInfo(NewDiagnosticForNode(
				    lexicalValueDecl,
				    The_shadowing_declaration_of_0_is_defined_here, {diagName}));
				diagnostic->AddRelatedInfo(NewDiagnosticForNode(
				    typeValueDecl,
				    The_declaration_of_0_that_you_probably_intended_to_use_is_defined_here,
				    {diagName}));
				return true;
			}
		}
		error(right,
		      Property_0_is_not_accessible_outside_class_1_because_it_has_a_private_identifier,
		      {diagName, SymbolToString(typeClass->symbol())});
		return true;
	}
	return false;
}

void Checker::reportNonexistentProperty(Node* propNode, Type* containingType,
                                        bool isUncheckedJS) {
	NonExistentPropertyKey key{propNode, containingType, isUncheckedJS};
	if (nonExistentProperties.find(key) != nonExistentProperties.end()) {
		return;
	}
	nonExistentProperties.insert(key);
	NodeLinks* links = nodeLinks.Get(propNode);
	if ((links->flags & NodeCheckFlagsTypeChecked) != 0) {
		return; // error already made/in progress
	}
	links->flags |= NodeCheckFlagsTypeChecked;
	if (isJSDocNameReferenceContext(propNode)) {
		return;
	}
	Diagnostic* diagnostic = nullptr;
	// One text() copy per call (was per-lookup): the name is needed as a real
	// string for the const string& property-index signatures.
	const std::string propText = propNode->text();
	if (!isPrivateIdentifier(propNode) &&
	    (containingType->flags & TypeFlagsUnion) != 0 &&
	    (containingType->flags & TypeFlagsPrimitive) == 0) {
		for (Type* subtype : containingType->types()) {
			if (getPropertyOfType(subtype, propText) == nullptr &&
			    getApplicableIndexInfoForName(subtype, propText) ==
			        nullptr) {
				diagnostic = NewDiagnosticChainForNode(
				    diagnostic, propNode, Property_0_does_not_exist_on_type_1,
				    {declarationNameToString(propNode),
				     TypeToString(subtype)});
				break;
			}
		}
	}
	if (typeHasStaticProperty(propText, containingType)) {
		std::string propName = declarationNameToString(propNode);
		std::string typeName = TypeToString(containingType);
		diagnostic = NewDiagnosticChainForNode(
		    diagnostic, propNode,
		    Property_0_does_not_exist_on_type_1_Did_you_mean_to_access_the_static_member_2_instead,
		    {propName, typeName, typeName + "." + propName});
	} else {
		Type* promisedType = GetPromisedTypeOfPromise(containingType);
		if (promisedType != nullptr &&
		    getPropertyOfType(promisedType, propText) != nullptr) {
			diagnostic = NewDiagnosticChainForNode(
			    diagnostic, propNode, Property_0_does_not_exist_on_type_1,
			    {declarationNameToString(propNode),
			     TypeToString(containingType)});
			diagnostic->AddRelatedInfo(
			    NewDiagnosticForNode(propNode, Did_you_forget_to_use_await, {}));
		} else {
			std::string missingProperty = declarationNameToString(propNode);
			std::string container = TypeToString(containingType);
			std::string libSuggestion =
			    getSuggestedLibForNonExistentProperty(missingProperty,
			                                          containingType);
			if (!libSuggestion.empty()) {
				diagnostic = NewDiagnosticChainForNode(
				    diagnostic, propNode,
				    Property_0_does_not_exist_on_type_1_Do_you_need_to_change_your_target_library_Try_changing_the_lib_compiler_option_to_2_or_later,
				    {missingProperty, container, libSuggestion});
			} else {
				Symbol* suggestion = getSuggestedSymbolForNonexistentProperty(
				    propNode, containingType);
				if (suggestion != nullptr) {
					std::string suggestedName = symbolName(suggestion);
					const DiagnosticMessage* message =
					    isUncheckedJS
					        ? Property_0_may_not_exist_on_type_1_Did_you_mean_2
					        : Property_0_does_not_exist_on_type_1_Did_you_mean_2;
					diagnostic = NewDiagnosticChainForNode(
					    diagnostic, propNode, message,
					    {missingProperty, container, suggestedName});
					if (suggestion->valueDeclaration != nullptr) {
						diagnostic->AddRelatedInfo(NewDiagnosticForNode(
						    suggestion->valueDeclaration, X_0_is_declared_here,
						    {suggestedName}));
					}
				} else {
					diagnostic = elaborateNeverIntersection(diagnostic, propNode,
					                                        containingType);
					const DiagnosticMessage* message;
					if (containerSeemsToBeEmptyDomElement(containingType)) {
						message =
						    Property_0_does_not_exist_on_type_1_Try_changing_the_lib_compiler_option_to_include_dom;
					} else {
						message = Property_0_does_not_exist_on_type_1;
					}
					diagnostic = NewDiagnosticChainForNode(
					    diagnostic, propNode, message,
					    {missingProperty, container});
				}
			}
		}
	}
	addErrorOrSuggestion(
	    !isUncheckedJS ||
	        diagnostic->Code() !=
	            Property_0_may_not_exist_on_type_1_Did_you_mean_2->code,
	    diagnostic);
}

// getSuggestedLibForNonExistentProperty — already real in checker.cpp (checker.go:11806).

Symbol* Checker::getSuggestedSymbolForNonexistentProperty(Node* name,
                                                        Type* containingType) {
	std::vector<Symbol*> props = getPropertiesOfType(containingType);
	Node* parent = name->parent;
	if (isPropertyAccessExpression(parent)) {
		props = filterVec(props, [&](Symbol* prop) {
			return isValidPropertyAccessForCompletions(parent, containingType,
			                                         prop);
		});
	}
	return getSpellingSuggestionForName(name->text(), props, SymbolFlagsValue);
}

// Checks if an existing property access is valid for completions purposes.
// @param node a property access-like node where we want to check if we can access a property.
// This node does not need to be an access of the property we are checking.
// e.g. in completions, this node will often be an incomplete property access node, as in `foo.`.
// Besides providing a location (i.e. scope) used to check property accessibility, we use this node for
// computing whether this is a `super` property access.
// @param type the type whose property we are checking.
// @param property the accessed property's symbol.
bool Checker::isValidPropertyAccessForCompletions(Node* node, Type* t,
                                                  Symbol* property) {
	return isPropertyAccessible(
	    node,
	    isPropertyAccessExpression(node) &&
	        node->expression()->kind == Kind::SuperKeyword,
	    false /*isWrite*/, t, property);
	// Previously we validated the 'this' type of methods but this adversely affected performance. See #31377 for more context.
}

// Checks if a property can be accessed in a location.
// The location is given by the `node` parameter.
// The node does not need to be a property access.
// @param node location where to check property accessibility
// @param isSuper whether to consider this a `super` property access, e.g. `super.foo`.
// @param isWrite whether this is a write access, e.g. `++foo.x`.
// @param containingType type where the property comes from.
// @param property property symbol.
bool Checker::isPropertyAccessible(Node* node, bool isSuper, bool isWrite,
                                   Type* containingType, Symbol* property) {
	// Short-circuiting for improved performance.
	if (isTypeAny(containingType)) {
		return true;
	}
	// A #private property access in an optional chain is an error dealt with by the parser.
	// The checker does not check for it, so we need to do our own check here.
	if (property->valueDeclaration != nullptr &&
	    isPrivateIdentifierClassElementDeclaration(property->valueDeclaration)) {
		Node* declClass = getContainingClass(property->valueDeclaration);
		return !isOptionalChain(node) && isNodeDescendantOf(node, declClass);
	}
	return checkPropertyAccessibilityAtLocation(node, isSuper, isWrite,
	                                            containingType, property,
	                                            nullptr);
}

bool Checker::containerSeemsToBeEmptyDomElement(Type* containingType) {
	return std::find(compilerOptions->Lib.begin(), compilerOptions->Lib.end(),
	                 "lib.dom.d.ts") == compilerOptions->Lib.end() &&
	       everyContainedType(containingType, hasCommonDomTypeName) &&
	       isEmptyObjectType(containingType);
}

namespace {

bool hasCommonDomTypeName(Type* t) {
	if (t->symbol == nullptr) {
		return false;
	}
	const std::string& name = t->symbol->name;
	return name == "EventTarget" || name == "Node" || name == "Element" ||
	       (name.starts_with("HTML") && name.ends_with("Element"));
}

}  // namespace

// checkAndReportErrorForExtendingInterface / getEntityNameForExtendingInterface —
// already real in checker.cpp (checker.go:11865, 11875).

bool Checker::isUncalledFunctionReference(Node* node, Symbol* symbol) {
	if ((symbol->flags & (SymbolFlagsFunction | SymbolFlagsMethod)) != 0) {
		Node* parent = findAncestor(
		    node->parent, [](Node* n) { return !isAccessExpression(n); });
		if (parent == nullptr) {
			parent = node->parent;
		}
		if (isCallLikeExpression(parent)) {
			return isCallOrNewExpression(parent) && isIdentifier(node) &&
			       hasMatchingArgument(parent, node);
		}
		return std::all_of(
		    symbol->declarations.begin(), symbol->declarations.end(),
		    [&](Node* d) {
			    return !isFunctionLike(d) || IsDeprecatedDeclaration(d);
		    });
	}
	return true;
}

void Checker::checkPropertyNotUsedBeforeDeclaration(Symbol* prop, Node* node,
                                                    Node* right) {
	Node* valueDeclaration = prop->valueDeclaration;
	if (valueDeclaration == nullptr ||
	    getSourceFileOfNode(node)->IsDeclarationFile) {
		return;
	}
	Diagnostic* diagnostic = nullptr;
	std::string declarationName = right->text();
	if (isInPropertyInitializerOrClassStaticBlock(node,
	                                            false /*ignoreArrowFunctions*/) &&
	    !isOptionalPropertyDeclaration(valueDeclaration) &&
	    !(isAccessExpression(node) && isAccessExpression(node->expression())) &&
	    !isBlockScopedNameDeclaredBeforeUse(valueDeclaration, right) &&
	    !(isMethodDeclaration(valueDeclaration) &&
	      (getCombinedModifierFlagsCached(valueDeclaration) &
	       ModifierFlagsStatic) != 0) &&
	    (compilerOptions->GetUseDefineForClassFields() ||
	     !isPropertyDeclaredInAncestorClass(prop))) {
		diagnostic = error(right,
		                   Property_0_is_used_before_its_initialization,
		                   declarationName);
	} else if (isClassDeclaration(valueDeclaration) &&
	           !isTypeReferenceNode(node->parent) &&
	           (valueDeclaration->flags & NodeFlagsAmbient) == 0 &&
	           !isBlockScopedNameDeclaredBeforeUse(valueDeclaration, right)) {
		diagnostic =
		    error(right, Class_0_used_before_its_declaration, declarationName);
	}
	if (diagnostic != nullptr) {
		diagnostic->AddRelatedInfo(NewDiagnosticForNode(
		    valueDeclaration, X_0_is_declared_here, {declarationName}));
	}
}

bool Checker::isOptionalPropertyDeclaration(Node* node) {
	return isPropertyDeclaration(node) && !hasAccessorModifier(node) &&
	       isQuestionToken(node->postfixToken());
}

bool Checker::isPropertyDeclaredInAncestorClass(Symbol* prop) {
	if ((prop->parent->flags & SymbolFlagsClass) != 0) {
		std::vector<Type*> baseTypes =
		    getBaseTypes(getDeclaredTypeOfSymbol(prop->parent));
		if (!baseTypes.empty()) {
			Symbol* superProperty =
			    getPropertyOfType(baseTypes[0], prop->name);
			return superProperty != nullptr &&
			       superProperty->valueDeclaration != nullptr;
		}
	}
	return false;
}

/**
 * Check whether the requested property access is valid.
 * Returns true if node is a valid property access, and false otherwise.
 * @param node The node to be checked.
 * @param isSuper True if the access is from `super.`.
 * @param type The type of the object whose property is being accessed. (Not the type of the property.)
 * @param prop The symbol for the property being accessed.
 */
bool Checker::checkPropertyAccessibility(Node* node, bool isSuper,
                                         bool writing, Type* t,
                                         Symbol* prop) {
	return checkPropertyAccessibilityEx(node, isSuper, writing, t, prop,
	                                    true /*reportError*/);
}

bool Checker::checkPropertyAccessibilityEx(Node* node, bool isSuper,
                                           bool writing, Type* t, Symbol* prop,
                                           bool reportError) {
	Node* errorNode = nullptr;
	if (reportError) {
		switch (node->kind) {
		case Kind::PropertyAccessExpression:
			errorNode = node->as<PropertyAccessExpression>()->name;
			break;
		case Kind::QualifiedName:
			errorNode = node->as<QualifiedName>()->Right;
			break;
		case Kind::ImportType:
			errorNode = node;
			break;
		case Kind::BindingElement:
			errorNode = getBindingElementPropertyName(node);
			break;
		default:
			errorNode = node->name();
		}
	}
	return checkPropertyAccessibilityAtLocation(node, isSuper, writing, t, prop,
	                                            errorNode);
}

/**
 * Check whether the requested property can be accessed at the requested location.
 * Returns true if node is a valid property access, and false otherwise.
 * @param location The location node where we want to check if the property is accessible.
 * @param isSuper True if the access is from `super.`.
 * @param writing True if this is a write property access, false if it is a read property access.
 * @param containingType The type of the object whose property is being accessed. (Not the type of the property.)
 * @param prop The symbol for the property being accessed.
 * @param errorNode The node where we should report an invalid property access error, or undefined if we should not report errors.
 */
bool Checker::checkPropertyAccessibilityAtLocation(
    Node* location, bool isSuper, bool writing, Type* containingType,
    Symbol* prop, Node* errorNode) {
	ModifierFlags flags = getDeclarationModifierFlagsFromSymbolEx(prop, writing);
	if (isSuper) {
		// TS 1.0 spec (April 2014): 4.8.2
		// - In a constructor, instance member function, instance member accessor, or
		//   instance member variable initializer where this references a derived class instance,
		//   a super property access is permitted and must specify a public instance member function of the base class.
		// - In a static member function or static member accessor
		//   where this references the constructor function object of a derived class,
		//   a super property access is permitted and must specify a public static member function of the base class.
		if ((flags & ModifierFlagsAbstract) != 0) {
			// A method cannot be accessed in a super property access if the method is abstract.
			// This error could mask a private property access error. But, a member
			// cannot simultaneously be private and abstract, so this will trigger an
			// additional error elsewhere.
			if (errorNode != nullptr) {
				error(errorNode,
				      Abstract_method_0_in_class_1_cannot_be_accessed_via_super_expression,
				      {symbolToString(prop),
				       TypeToString(getDeclaringClass(prop))});
			}
			return false;
		}
		// A class field cannot be accessed via super.* from a derived class.
		// This is true for both [[Set]] (old) and [[Define]] (ES spec) semantics.
		if ((flags & ModifierFlagsStatic) == 0 &&
		    std::any_of(prop->declarations.begin(), prop->declarations.end(),
		                isClassInstanceProperty)) {
			if (errorNode != nullptr) {
				error(errorNode,
				      Class_field_0_defined_by_the_parent_class_is_not_accessible_in_the_child_class_via_super,
				      symbolToString(prop));
			}
			return false;
		}
	}
	// Referencing abstract properties within their own constructors is not allowed
	if ((flags & ModifierFlagsAbstract) != 0 &&
	    symbolHasNonMethodDeclaration(prop) &&
	    (isThisProperty(location) ||
	     isThisInitializedObjectBindingExpression(location) ||
	     (isObjectBindingPattern(location->parent) &&
	      isThisInitializedDeclaration(location->parent->parent)))) {
		Symbol* parentSymbol = getParentOfSymbol(prop);
		if (parentSymbol != nullptr &&
		    (parentSymbol->flags & SymbolFlagsClass) != 0 &&
		    isNodeUsedDuringClassInitialization(location)) {
			if (errorNode != nullptr) {
				error(errorNode,
				      Abstract_property_0_in_class_1_cannot_be_accessed_in_the_constructor,
				      {symbolToString(prop), symbolToString(parentSymbol)});
			}
			return false;
		}
	}
	// Public properties are otherwise accessible.
	if ((flags & ModifierFlagsNonPublicAccessibilityModifier) == 0) {
		return true;
	}
	// Property is known to be private or protected at this point
	// Private property is accessible if the property is within the declaring class
	if ((flags & ModifierFlagsPrivate) != 0) {
		Node* declaringClassDeclaration = nullptr;
		if (Symbol* parent = getParentOfSymbol(prop); parent != nullptr) {
			declaringClassDeclaration = getClassLikeDeclarationOfSymbol(parent);
		}
		if (declaringClassDeclaration == nullptr ||
		    !isNodeWithinClass(location, declaringClassDeclaration)) {
			if (errorNode != nullptr) {
				Type* klass = getDeclaringClass(prop) != nullptr
				                  ? getDeclaringClass(prop)
				                  : containingType;
				error(errorNode,
				      Property_0_is_private_and_only_accessible_within_class_1,
				      {symbolToString(prop), TypeToString(klass)});
			}
			return false;
		}
		return true;
	}
	// Property is known to be protected at this point
	// All protected properties of a supertype are accessible in a super access
	if (isSuper) {
		return true;
	}
	// Find the first enclosing class that has the declaring classes of the protected constituents
	// of the property as base classes
	Type* enclosingClass = nullptr;
	Node* container = getContainingClass(location);
	while (container != nullptr) {
		Type* klass =
		    getDeclaredTypeOfSymbol(getSymbolOfDeclaration(container));
		if (isClassDerivedFromDeclaringClasses(klass, prop, writing)) {
			enclosingClass = klass;
			break;
		}
		container = getContainingClass(container);
	}
	// A protected property is accessible if the property is within the declaring class or classes derived from it
	if (enclosingClass == nullptr) {
		// allow PropertyAccessibility if context is in function with this parameter
		// static member access is disallowed
		Type* klass = getEnclosingClassFromThisParameter(location);
		if (klass != nullptr &&
		    isClassDerivedFromDeclaringClasses(klass, prop, writing)) {
			enclosingClass = klass;
		}
		if ((flags & ModifierFlagsStatic) != 0 || enclosingClass == nullptr) {
			if (errorNode != nullptr) {
				Type* klass = getDeclaringClass(prop) != nullptr
				                  ? getDeclaringClass(prop)
				                  : containingType;
				error(errorNode,
				      Property_0_is_protected_and_only_accessible_within_class_1_and_its_subclasses,
				      {symbolToString(prop), TypeToString(klass)});
			}
			return false;
		}
	}
	// No further restrictions for static properties
	if ((flags & ModifierFlagsStatic) != 0) {
		return true;
	}
	if ((containingType->flags & TypeFlagsTypeParameter) != 0) {
		// get the original type -- represented as the type constraint of the 'this' type
		if (containingType->AsTypeParameter()->isThisType) {
			containingType = getConstraintOfTypeParameter(containingType);
		} else {
			containingType = getBaseConstraintOfType(containingType);
		}
	}
	if (containingType == nullptr ||
	    !hasBaseType(containingType, enclosingClass)) {
		if (errorNode != nullptr && containingType != nullptr) {
			error(errorNode,
			      Property_0_is_protected_and_only_accessible_through_an_instance_of_class_1_This_is_an_instance_of_class_2,
			      {symbolToString(prop), TypeToString(enclosingClass),
			       TypeToString(containingType)});
		}
		return false;
	}
	return true;
}

bool Checker::symbolHasNonMethodDeclaration(Symbol* symbol) {
	return forEachProperty(symbol, [](Symbol* prop) {
		return (prop->flags & SymbolFlagsMethod) == 0;
	});
}

// Invoke the callback for each underlying property symbol of the given symbol and return the first
// value that isn't undefined.
bool Checker::forEachProperty(
    Symbol* prop, const std::function<bool(Symbol*)>& callback) {
	if ((prop->checkFlags & CheckFlagsSynthetic) == 0) {
		return callback(prop);
	}
	for (Type* t : valueSymbolLinks.Get(prop)->containingType->types()) {
		Symbol* p = getPropertyOfType(t, prop->name);
		if (p != nullptr && forEachProperty(p, callback)) {
			return true;
		}
	}
	return false;
}

// Return the declaring class type of a property or undefined if property not declared in class
Type* Checker::getDeclaringClass(Symbol* prop) {
	if (prop->parent != nullptr &&
	    (prop->parent->flags & SymbolFlagsClass) != 0) {
		return getDeclaredTypeOfSymbol(getParentOfSymbol(prop));
	}
	return nullptr;
}

// Return true if source property is a valid override of protected parts of target property.
bool Checker::isValidOverrideOf(Symbol* sourceProp, Symbol* targetProp) {
	return !forEachProperty(targetProp, [&](Symbol* tp) {
		if ((getDeclarationModifierFlagsFromSymbol(tp) &
		     ModifierFlagsProtected) != 0) {
			return !isPropertyInClassDerivedFrom(sourceProp,
			                                     getDeclaringClass(tp));
		}
		return false;
	});
}

// Return true if some underlying source property is declared in a class that derives
// from the given base class.
bool Checker::isPropertyInClassDerivedFrom(Symbol* prop, Type* baseClass) {
	return forEachProperty(prop, [&](Symbol* sp) {
		Type* sourceClass = getDeclaringClass(sp);
		if (sourceClass != nullptr) {
			return hasBaseType(sourceClass, baseClass);
		}
		return false;
	});
}

bool Checker::isNodeUsedDuringClassInitialization(Node* node) {
	return findAncestorOrQuit(node, [](Node* element) -> FindAncestorResult {
		if ((isConstructorDeclaration(element) &&
		     nodeIsPresent(element->body())) ||
		    isPropertyDeclaration(element)) {
			return FindAncestorResult::True;
		} else if (isClassLike(element) ||
		           isFunctionLikeDeclaration(element)) {
			return FindAncestorResult::Quit;
		}
		return FindAncestorResult::False;
	}) != nullptr;
}

// (deduped: isNodeWithinClass defined in owning slice file)

bool Checker::forEachEnclosingClass(
    Node* node, const std::function<bool(Node*)>& callback) {
	Node* containingClass = getContainingClass(node);
	while (containingClass != nullptr) {
		bool result = callback(containingClass);
		if (result) {
			return true;
		}
		containingClass = getContainingClass(containingClass);
	}
	return false;
}

// Return true if the given class derives from each of the declaring classes of the protected
// constituents of the given property.
bool Checker::isClassDerivedFromDeclaringClasses(Type* checkClass,
                                                 Symbol* prop, bool writing) {
	return !forEachProperty(prop, [&](Symbol* p) {
		if ((getDeclarationModifierFlagsFromSymbolEx(p, writing) &
		     ModifierFlagsProtected) != 0) {
			return !hasBaseType(checkClass, getDeclaringClass(p));
		}
		return false;
	});
}

Type* Checker::getEnclosingClassFromThisParameter(Node* node) {
	// 'this' type for a node comes from, in priority order...
	// 1. The type of a syntactic 'this' parameter in the enclosing function scope
	Node* thisParameter = getThisParameterFromNodeContext(node);
	Type* thisType = nullptr;
	if (thisParameter != nullptr && thisParameter->type() != nullptr) {
		thisType = getTypeFromTypeNode(thisParameter->type());
	}
	if (thisType != nullptr) {
		// 2. The constraint of a type parameter used for an explicit 'this' parameter
		if ((thisType->flags & TypeFlagsTypeParameter) != 0) {
			thisType = getConstraintOfTypeParameter(thisType);
		}
	} else {
		// 3. The 'this' parameter of a contextual type
		Node* thisContainer =
		    tsc::getThisContainer(node, false /*includeArrowFunctions*/,
		                          false /*includeClassComputedPropertyName*/);
		if (thisContainer != nullptr && isFunctionLike(thisContainer)) {
			thisType = getContextualThisParameterType(thisContainer);
		}
	}
	if (thisType != nullptr &&
	    (thisType->objectFlags &
	     (ObjectFlagsClassOrInterface | ObjectFlagsReference)) != 0) {
		return getTargetType(thisType);
	}
	return nullptr;
}

namespace {

// checker.go:12191 — getThisParameterFromNodeContext (free fn)
Node* getThisParameterFromNodeContext(Node* node) {
	Node* thisContainer =
	    getThisContainer(node, false /*includeArrowFunctions*/,
	                     false /*includeClassComputedPropertyName*/);
	if (thisContainer != nullptr && isFunctionLike(thisContainer)) {
		return getThisParameter(thisContainer);
	}
	return nullptr;
}

}  // namespace

Type* Checker::getContextualThisParameterType(Node* fn) {
	if (isArrowFunction(fn)) {
		return nullptr;
	}
	if (isContextSensitiveFunctionOrObjectLiteralMethod(fn)) {
		Signature* contextualSignature = getContextualSignature(fn);
		if (contextualSignature != nullptr) {
			Symbol* thisParameter = contextualSignature->thisParameter;
			if (thisParameter != nullptr) {
				return getTypeOfSymbol(thisParameter);
			}
		}
	}
	bool inJs = isInJSFile(fn);
	if (noImplicitThis || inJs) {
		Node* containingLiteral = getContainingObjectLiteral(fn);
		if (containingLiteral != nullptr) {
			// We have an object literal method. Check if the containing object literal has a contextual type
			// that includes a ThisType<T>. If so, T is the contextual type for 'this'. We continue looking in
			// any directly enclosing object literals.
			Type* contextualType = getApparentTypeOfContextualType(
			    containingLiteral, ContextFlagsNone);
			Type* thisType = getThisTypeOfObjectLiteralFromContextualType(
			    containingLiteral, contextualType);
			if (thisType != nullptr) {
				return instantiateType(
				    thisType,
				    getMapperFromContext(getInferenceContext(containingLiteral)));
			}
			// There was no contextual ThisType<T> for the containing object literal, so the contextual type
			// for 'this' is the non-null form of the contextual type for the containing object literal or
			// the type of the object literal itself.
			if (contextualType != nullptr) {
				thisType = GetNonNullableType(contextualType);
			} else {
				thisType = checkExpressionCached(containingLiteral);
			}
			return getWidenedType(thisType);
		}
		// In an assignment of the form 'obj.xxx = function(...)' or 'obj[xxx] = function(...)', the
		// contextual type for 'this' is 'obj'.
		Node* parent = walkUpParenthesizedExpressions(fn->parent);
		if (isAssignmentExpression(parent, false)) {
			Node* target = parent->as<BinaryExpression>()->Left;
			if (isAccessExpression(target)) {
				Node* expression = target->expression();
				// Don't contextually type `this` as `exports` in `exports.Point = function(x, y) { this.x = x; this.y = y; }`
				if (inJs && isIdentifier(expression)) {
					SourceFile* sourceFile = getSourceFileOfNode(parent);
					if (sourceFile->CommonJSModuleIndicator != nullptr &&
					    (getResolvedSymbol(expression)->flags &
					     SymbolFlagsModuleExports) != 0) {
						return nullptr;
					}
				}
				return getWidenedType(checkExpressionCached(expression));
			}
		}
	}
	return nullptr;
}

Type* Checker::checkThisExpression(Node* node) {
	// Stop at the first arrow function so that we can
	// tell whether 'this' needs to be captured.
	Node* container =
	    tsc::getThisContainer(node, true /*includeArrowFunctions*/,
	                          true /*includeClassComputedPropertyName*/);
	bool capturedByArrowFunction = false;
	bool thisInComputedPropertyName = false;
	if (isConstructorDeclaration(container)) {
		checkThisBeforeSuper(
		    node, container,
		    X_super_must_be_called_before_accessing_this_in_the_constructor_of_a_derived_class);
	}
	for (;;) {
		// Now skip arrow functions to get the "real" owner of 'this'.
		if (isArrowFunction(container)) {
			container = tsc::getThisContainer(
			    container, false /*includeArrowFunctions*/,
			    !thisInComputedPropertyName);
			capturedByArrowFunction = true;
		}
		if (isComputedPropertyName(container)) {
			container = tsc::getThisContainer(
			    container, !capturedByArrowFunction,
			    false /*includeClassComputedPropertyName*/);
			thisInComputedPropertyName = true;
			continue;
		}
		break;
	}
	checkThisInStaticClassFieldInitializerInDecoratedClass(node, container);
	if (thisInComputedPropertyName) {
		error(node, X_this_cannot_be_referenced_in_a_computed_property_name);
	} else {
		switch (container->kind) {
		case Kind::ModuleDeclaration:
			error(node,
			      X_this_cannot_be_referenced_in_a_module_or_namespace_body);
			// do not return here so in case if lexical this is captured - it will be reflected in flags on NodeLinks
			break;
		case Kind::EnumDeclaration:
			error(node, X_this_cannot_be_referenced_in_current_location);
			// do not return here so in case if lexical this is captured - it will be reflected in flags on NodeLinks
			break;
		default:
			break;
		}
	}
	Type* t = tryGetThisTypeAtEx(node, true /*includeGlobalThis*/, container);
	if (noImplicitThis) {
		Type* globalThisType = getTypeOfSymbol(globalThisSymbol);
		if (t == globalThisType && capturedByArrowFunction) {
			error(node,
			      The_containing_arrow_function_captures_the_global_value_of_this);
		} else if (t == nullptr) {
			// With noImplicitThis, functions may not reference 'this' if it has type 'any'
			Diagnostic* diag = error(
			    node,
			    X_this_implicitly_has_type_any_because_it_does_not_have_a_type_annotation);
			if (!isSourceFile(container)) {
				Type* outsideThis = tryGetThisTypeAt(container);
				if (outsideThis != nullptr &&
				    outsideThis != globalThisType) {
					diag->AddRelatedInfo(NewDiagnosticForNode(
					    container,
					    An_outer_value_of_this_is_shadowed_by_this_container, {}));
				}
			}
		}
	}
	if (t == nullptr) {
		return anyType;
	}
	return t;
}

Type* Checker::tryGetThisTypeAt(Node* node) {
	return tryGetThisTypeAtEx(node, true /*includeGlobalThis*/,
	                          nullptr /*container*/);
}

Type* Checker::TryGetThisTypeAtEx(Node* node, bool includeGlobalThis,
                                  Node* container) {
	Node* reparsed = getReparsedNodeForNode(node);
	if ((reparsed->flags & NodeFlagsJSDoc) != 0 &&
	    (reparsed->flags & NodeFlagsReparsed) == 0) {
		return nullptr; // Binder doesn't process non-reparsed JSDoc nodes
	}
	return tryGetThisTypeAtEx(reparsed, includeGlobalThis,
	                          getReparsedNodeForNode(container));
}

Type* Checker::tryGetThisTypeAtEx(Node* node, bool includeGlobalThis,
                                  Node* container) {
	if (container == nullptr) {
		container = this->getThisContainer(node,
		                                   false /*includeArrowFunctions*/,
		                                   false /*includeClassComputedPropertyName*/);
	}
	if (isFunctionLike(container) &&
	    (!isInParameterInitializerBeforeContainingFunction(node) ||
	     getThisParameter(container) != nullptr)) {
		Signature* sig = getSignatureOfFullSignatureType(container);
		if (sig == nullptr) {
			sig = getSignatureFromDeclaration(container);
		}
		Type* thisType = getThisTypeOfSignature(sig);
		// Note: a parameter initializer should refer to class-this unless function-this is explicitly annotated.
		// If this is a function in a JS file, it might be a class method.
		if (thisType == nullptr) {
			thisType = getContextualThisParameterType(container);
		}
		if (thisType != nullptr) {
			return getFlowTypeOfReference(node, thisType);
		}
	}
	if (container->parent != nullptr && isClassLike(container->parent)) {
		Symbol* symbol = getSymbolOfDeclaration(container->parent);
		Type* t;
		if (isStatic(container)) {
			t = getTypeOfSymbol(symbol);
		} else {
			t = getDeclaredTypeOfSymbol(symbol)->AsInterfaceType()->thisType;
		}
		return getFlowTypeOfReference(node, t);
	}
	if (isSourceFile(container)) {
		// look up in the source file's locals or exports
		if (container->as<SourceFile>()->ExternalModuleIndicator != nullptr) {
			// TODO: Maybe issue a better error than 'object is possibly undefined'
			return undefinedType;
		}
		if (includeGlobalThis) {
			return getTypeOfSymbol(globalThisSymbol);
		}
	}
	return nullptr;
}

// === dep stubs — removed when owner slice lands ===

// (deduped: checkArithmeticOperandType defined in the owning slice file)

// (deduped: checkNonNullType defined in the owning slice file)

// (deduped: checkThisBeforeSuper defined in the owning slice file)

// (deduped: checkThisInStaticClassFieldInitializerInDecoratedClass defined in the owning slice file)

// (deduped: checkTruthinessOfType defined in the owning slice file)

// (deduped: checkTypeArguments defined in owning slice file)

// (deduped: getIterationTypeOfIterable defined in owning slice file)

// (deduped: getMapperFromContext defined in the owning slice file)

// (deduped: getNarrowedTypeOfSymbol defined in the owning slice file)

// (deduped: getThisTypeOfObjectLiteralFromContextualType defined in the owning slice file)

// (deduped: hasCorrectTypeArgumentArity defined in owning slice file)

// (deduped: hasMatchingArgument defined in the owning slice file)

// (deduped: isDestructuringAssignmentTarget defined in the owning slice file)

// (deduped: isEvolvingArrayOperationTarget defined in the owning slice file)

// (deduped: isInPropertyInitializerOrClassStaticBlock defined in the owning slice file)

// (deduped: isPastLastAssignment defined in the owning slice file)

// (deduped: isSymbolAssignedDefinitely defined in the owning slice file)

// (deduped: resolveAliasWithDeprecationCheck defined in owning slice file)

// (deduped: SymbolToString defined in cpp/internal/checker/checker_printer.cpp)

// checker.go:11644 getFlowTypeOfProperty
Type* Checker::getFlowTypeOfProperty(Node* reference, Symbol* prop) {
	Type* initialType = undefinedType;
	if (prop != nullptr && prop->valueDeclaration != nullptr &&
	    (!isAutoTypedProperty(prop) ||
	     prop->valueDeclaration->modifierFlags() & ModifierFlagsAmbient)) {
		if (Type* baseType = getTypeOfPropertyInBaseClass(prop);
		    baseType != nullptr) {
			initialType = baseType;
		}
	}
	return getFlowTypeOfReferenceEx(reference, autoType, initialType, nullptr,
	                                nullptr);
}

// ast/utilities.go:2998 — IsCallLikeExpression
bool isCallLikeExpression(Node* node) {
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
	default:
		return false;
	}
}

}  // namespace tsc::checker
