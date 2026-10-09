// Port of tsc/internal/pseudochecker (type.go + checker.go + lookup.go).
// pseudochecker is a limited "checker" that returns pseudo-"types" of
// expressions - mostly those which trivially have type nodes.
//
// TODO: Late binding/symbol merging? (checker.go)
// In strada, `expressionToTypeNode` used many `resolver` methods whose net
// effect was just calling `Checker.GetMergedSymbol` on a symbol when dealing
// with accessors. Right now those just use Node.Symbol, which will fail to
// pair up late-bound symbols. In theory, this is actually fine, since ID can't
// possibly know if `set [q1()](a){}` and `get [q2()](): T {}` are connected
// without performing real type checking, regardless, so it shouldn't matter.
// If anything, it might be OK to add a "dumb" late binder that can merge
// multiple `[a.b.c]: T` together, but not anything else.

#include "internal/pseudochecker/pseudochecker.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <vector>

namespace tsc::pseudochecker {

using namespace tsc;

// ---------------------------------------------------------------------------
// File-local ast helpers not yet ported to cpp/internal/ast.
// ---------------------------------------------------------------------------

namespace {

// ast/utilities.go — IsPrimitiveLiteralValue
bool isPrimitiveLiteralValue(Node* node, bool includeBigInt) {
	switch (node->kind) {
	case Kind::TrueKeyword:
	case Kind::FalseKeyword:
	case Kind::NumericLiteral:
	case Kind::StringLiteral:
	case Kind::NoSubstitutionTemplateLiteral:
		return true;
	case Kind::BigIntLiteral:
		return includeBigInt;
	case Kind::PrefixUnaryExpression: {
		auto* p = node->as<PrefixUnaryExpression>();
		if (p->Operator == Kind::MinusToken) {
			return isNumericLiteral(p->Operand) ||
			       (includeBigInt && isBigIntLiteral(p->Operand));
		}
		if (p->Operator == Kind::PlusToken) {
			return isNumericLiteral(p->Operand);
		}
		return false;
	}
	default:
		return false;
	}
}

// ast/utilities.go — IsVariableParameterOrProperty
bool isVariableParameterOrProperty(Node* node) {
	switch (node->kind) {
	case Kind::VariableDeclaration:
	case Kind::Parameter:
	case Kind::PropertySignature:
	case Kind::PropertyDeclaration:
		return true;
	default:
		return false;
	}
}

// ast/utilities.go — ForEachReturnStatement
bool forEachReturnStatement(Node* body,
                            const std::function<bool(Node*)>& visitor) {
	std::function<bool(Node*)> traverse = [&](Node* node) -> bool {
		switch (node->kind) {
		case Kind::ReturnStatement:
			return visitor(node);
		case Kind::CaseBlock:
		case Kind::Block:
		case Kind::IfStatement:
		case Kind::DoStatement:
		case Kind::WhileStatement:
		case Kind::ForStatement:
		case Kind::ForInStatement:
		case Kind::ForOfStatement:
		case Kind::WithStatement:
		case Kind::SwitchStatement:
		case Kind::CaseClause:
		case Kind::DefaultClause:
		case Kind::LabeledStatement:
		case Kind::TryStatement:
		case Kind::CatchClause:
			return node->forEachChild(
				[&](Node* child) -> bool { return traverse(child); });
		}
		return false;
	};
	return traverse(body);
}

// lookup.go:470 — isConstContextPropagatingKind
bool isConstContextPropagatingKind(Kind kind) {
	switch (kind) {
	case Kind::ArrayLiteralExpression:
	case Kind::ObjectLiteralExpression:
	case Kind::ParenthesizedExpression:
	case Kind::SpreadElement:
	case Kind::PropertyAssignment:
	case Kind::ShorthandPropertyAssignment:
	case Kind::TemplateSpan:
	case Kind::PrefixUnaryExpression:
		return true;
	}
	return false;
}

// lookup.go:196 — isValueSignatureDeclaration
bool isValueSignatureDeclaration(Node* node) {
	return isFunctionExpression(node) || isArrowFunction(node) ||
	       isMethodDeclaration(node) || isAccessor(node) ||
	       isFunctionDeclaration(node) || isConstructorDeclaration(node);
}

// lookup.go:715 — isContextuallyTyped
bool isContextuallyTyped(Node* node) {
	return findAncestor(node->parent, [](Node* n) {
		// Functions calls or parent type annotations (but not the return type
		// of a function expression) may impact the inferred type and local
		// inference is unreliable
		if (isCallExpression(n)) {
			return true;
		}
		if (isSatisfiesExpression(n)) {
			return true;
		}
		if ((isVariableParameterOrProperty(n) || isAssertionExpression(n)) &&
		    n->type() != nullptr && !isConstAssertion(n)) {
			return true;
		}
		return isJsxElement(n) || isJsxExpression(n);
	}) != nullptr;
}

// lookup.go:546 — isUndefinedPseudoType
bool isUndefinedPseudoType(PseudoType* t) {
	return t->kind == PseudoTypeKind::Undefined ||
	       (t->kind == PseudoTypeKind::MaybeConstLocation &&
	        isUndefinedPseudoType(t->AsPseudoTypeMaybeConstLocation()->constType));
}

// lookup.go:550 — typeNodeCouldReferToUndefined
bool typeNodeCouldReferToUndefined(Node* node) {
	while (node->kind == Kind::ParenthesizedType) {
		node = node->as<ParenthesizedTypeNode>()->Type;
	}
	switch (node->kind) {
	// these types require symbolic/type resolution to know if they definitely
	// do or do not refer to `undefined`, so might (or definitely do)
	case Kind::TypeReference:
	case Kind::IndexedAccessType:
	case Kind::TypeQuery:
	case Kind::OptionalType:
	case Kind::RestType:
	case Kind::ImportType:
		return true;
	case Kind::IntersectionType:
		// TODO: why is this not `core.Every`? strada treated unions and
		// intersections the same, but logically every intersection member
		// needs to contain a possible `undefined` for the result type to
		// contain `undefined`. Likely a bug persisting from strada.
		return std::any_of(
			node->as<IntersectionTypeNode>()->Types->nodes.begin(),
			node->as<IntersectionTypeNode>()->Types->nodes.end(),
			[](Node* n) { return typeNodeCouldReferToUndefined(n); });
	case Kind::UnionType:
		return std::any_of(
			node->as<UnionTypeNode>()->Types->nodes.begin(),
			node->as<UnionTypeNode>()->Types->nodes.end(),
			[](Node* n) { return typeNodeCouldReferToUndefined(n); });
	case Kind::ConditionalType: // suspect - should be treated as a union of
	                            // both branches instead, likely a bug
	                            // persisted from strada
		return true;
	case Kind::TypeOperator: // suspect - always refers to a subset of
	                         // `string | number | symbol` for `keyof` or
	                         // `symbol` for `unique`
		return true;
	case Kind::TypePredicate: // suspect - always refers to `never` or
	                          // `boolean`, depending on kind - considered
	                          // possibly-`undefined` referencing for strada
	                          // compat
		return true;
	case Kind::UndefinedKeyword:
		return true;
	default: // all other keywords, literal types, function-y types,
	         // array/tuple types, type literals, template types, this types
		return false;
	}
}

// lookup.go:619 — addUndefinedIfDefinitelyRequired
PseudoType* addUndefinedIfDefinitelyRequired(PseudoType* expr) {
	// If `expr` doesn't already contain `| undefined` or a direct/inferred
	// type that may contain `undefined`, add `| undefined`
	// in Strada, this reached into the checker to see if `undefined` was
	// necessary, using `isRequiredOptionalParameter` from the emit resolver,
	// but that's not required on top of the syntactic checks to get the same
	// behavior. (If we get the type wrong, it'll mismatch later and be
	// discarded for an inference error since corsa actually validates that
	// pseudotypes semantically match the inferred type the checker produces)
	if (couldAlreadyReferToUndefinedType(expr)) {
		return expr; // will just error later, more like than not, unless the
		             // `undefined` is explicit in the pseudo
	}
	// Explicitly add an `| undefined`
	return newPseudoTypeUnion({expr, PseudoTypeUndefined});
}

// lookup.go:597 — isOptionalInitializedOrRestParameter
bool isOptionalInitializedOrRestParameter(Node* node) {
	auto* p = node->as<ParameterDeclaration>();
	return p->DotDotDotToken != nullptr || p->Initializer != nullptr ||
	       p->QuestionToken != nullptr;
}

// lastRequiredParamIndex returns the index just past the last required
// parameter in the list. A parameter is "required" if it has no question
// token, no initializer, and no rest token.
int lastRequiredParamIndex(const std::vector<Node*>& params) {
	for (size_t i = params.size(); i-- > 0;) {
		if (!isOptionalInitializedOrRestParameter(params[i])) {
			return static_cast<int>(i) + 1;
		}
	}
	return 0;
}

} // namespace

// ---------------------------------------------------------------------------
// type.go
// ---------------------------------------------------------------------------

PseudoType* PseudoTypeUndefined = new PseudoType{PseudoTypeKind::Undefined};
PseudoType* PseudoTypeNull = new PseudoType{PseudoTypeKind::Null};
PseudoType* PseudoTypeAny = new PseudoType{PseudoTypeKind::Any};
PseudoType* PseudoTypeString = new PseudoType{PseudoTypeKind::String};
PseudoType* PseudoTypeNumber = new PseudoType{PseudoTypeKind::Number};
PseudoType* PseudoTypeBigInt = new PseudoType{PseudoTypeKind::BigInt};
PseudoType* PseudoTypeBoolean = new PseudoType{PseudoTypeKind::Boolean};
PseudoType* PseudoTypeFalse = new PseudoType{PseudoTypeKind::False};
PseudoType* PseudoTypeTrue = new PseudoType{PseudoTypeKind::True};

PseudoType* newPseudoTypeDirect(Node* typeNode) {
	auto* t = new PseudoType();
	t->kind = PseudoTypeKind::Direct;
	t->direct = new PseudoType::PseudoTypeDirect{typeNode};
	return t;
}

PseudoType* newPseudoTypeInferred(Node* expr, bool isSignatureReturn) {
	auto* t = new PseudoType();
	t->kind = PseudoTypeKind::Inferred;
	t->inferred = new PseudoType::PseudoTypeInferred{expr, {}, isSignatureReturn};
	return t;
}

PseudoType* newPseudoTypeInferredWithErrors(Node* expr, bool isSignatureReturn,
                                            std::vector<Node*> errorNodes) {
	auto* t = new PseudoType();
	t->kind = PseudoTypeKind::Inferred;
	t->inferred = new PseudoType::PseudoTypeInferred{
		expr, std::move(errorNodes), isSignatureReturn};
	return t;
}

PseudoType* newPseudoTypeNoResult(Node* decl) {
	auto* t = new PseudoType();
	t->kind = PseudoTypeKind::NoResult;
	t->noResult = new PseudoType::PseudoTypeNoResult{decl};
	return t;
}

PseudoType* newPseudoTypeMaybeConstLocation(Node* loc, PseudoType* ct,
                                            PseudoType* reg) {
	auto* t = new PseudoType();
	t->kind = PseudoTypeKind::MaybeConstLocation;
	t->maybeConstLocation =
		new PseudoType::PseudoTypeMaybeConstLocation{loc, ct, reg};
	return t;
}

PseudoType* newPseudoTypeUnion(std::vector<PseudoType*> types) {
	auto* t = new PseudoType();
	t->kind = PseudoTypeKind::Union;
	t->union_ = new PseudoType::PseudoTypeUnion{std::move(types)};
	return t;
}

PseudoType* newPseudoTypeSingleCallSignature(
	Node* signature, std::vector<PseudoParameter*> parameters,
	std::vector<Node*> typeParameters, PseudoType* returnType) {
	auto* t = new PseudoType();
	t->kind = PseudoTypeKind::SingleCallSignature;
	t->singleCallSignature = new PseudoType::PseudoTypeSingleCallSignature{
		signature, std::move(parameters), std::move(typeParameters),
		returnType};
	return t;
}

PseudoType* newPseudoTypeTuple(std::vector<PseudoType*> elements) {
	auto* t = new PseudoType();
	t->kind = PseudoTypeKind::Tuple;
	t->tuple = new PseudoType::PseudoTypeTuple{std::move(elements)};
	return t;
}

PseudoType* newPseudoTypeObjectLiteral(
	std::vector<PseudoObjectElement*> elements) {
	auto* t = new PseudoType();
	t->kind = PseudoTypeKind::ObjectLiteral;
	t->objectLiteral =
		new PseudoType::PseudoTypeObjectLiteral{std::move(elements)};
	return t;
}

PseudoType* newPseudoTypeStringLiteral(Node* node) {
	auto* t = new PseudoType();
	t->kind = PseudoTypeKind::StringLiteral;
	t->literal = new PseudoType::PseudoTypeLiteral{node};
	return t;
}

PseudoType* newPseudoTypeNumericLiteral(Node* node) {
	auto* t = new PseudoType();
	t->kind = PseudoTypeKind::NumericLiteral;
	t->literal = new PseudoType::PseudoTypeLiteral{node};
	return t;
}

PseudoType* newPseudoTypeBigIntLiteral(Node* node) {
	auto* t = new PseudoType();
	t->kind = PseudoTypeKind::BigIntLiteral;
	t->literal = new PseudoType::PseudoTypeLiteral{node};
	return t;
}

PseudoParameter* newPseudoParameter(bool isRest, Node* name, bool isOptional,
                                    PseudoType* t) {
	return new PseudoParameter{isRest, name, isOptional, t};
}

// type.go:227 — PseudoObjectElement.Signature
Node* PseudoObjectElement::Signature() const {
	switch (kind) {
	case PseudoObjectElementKind::Method:
		return method->signature;
	case PseudoObjectElementKind::SetAccessor:
		return setAccessor->signature;
	case PseudoObjectElementKind::GetAccessor:
		return getAccessor->signature;
	default:
		return nullptr;
	}
}

PseudoObjectElement* newPseudoObjectMethod(
	Node* signature, Node* name, bool optional,
	std::vector<Node*> typeParameters,
	std::vector<PseudoParameter*> parameters, PseudoType* returnType) {
	auto* e = new PseudoObjectElement();
	e->name = name;
	e->optional = optional;
	e->kind = PseudoObjectElementKind::Method;
	e->method = new PseudoObjectElement::PseudoObjectMethod{
		signature, std::move(typeParameters), std::move(parameters),
		returnType};
	return e;
}

PseudoObjectElement* newPseudoPropertyAssignment(bool readonly, Node* name,
                                                 bool optional, PseudoType* t) {
	auto* e = new PseudoObjectElement();
	e->name = name;
	e->optional = optional;
	e->kind = PseudoObjectElementKind::PropertyAssignment;
	e->propertyAssignment =
		new PseudoObjectElement::PseudoPropertyAssignment{readonly, t};
	return e;
}

PseudoObjectElement* newPseudoSetAccessor(Node* signature, Node* name,
                                          bool optional, PseudoParameter* p) {
	auto* e = new PseudoObjectElement();
	e->name = name;
	e->optional = optional;
	e->kind = PseudoObjectElementKind::SetAccessor;
	e->setAccessor = new PseudoObjectElement::PseudoSetAccessor{signature, p};
	return e;
}

PseudoObjectElement* newPseudoGetAccessor(Node* signature, Node* name,
                                          bool optional, PseudoType* t) {
	auto* e = new PseudoObjectElement();
	e->name = name;
	e->optional = optional;
	e->kind = PseudoObjectElementKind::GetAccessor;
	e->getAccessor = new PseudoObjectElement::PseudoGetAccessor{signature, t};
	return e;
}

// ---------------------------------------------------------------------------
// checker.go:19 — NewPseudoChecker
// ---------------------------------------------------------------------------

PseudoChecker* newPseudoChecker(bool strictNullChecks,
                                bool exactOptionalPropertyTypes) {
	return new PseudoChecker{strictNullChecks, exactOptionalPropertyTypes};
}

// ---------------------------------------------------------------------------
// lookup.go
// ---------------------------------------------------------------------------

// lookup.go:11 — GetReturnTypeOfSignature
PseudoType* PseudoChecker::getReturnTypeOfSignature(Node* signatureNode) {
	switch (signatureNode->kind) {
	case Kind::GetAccessor:
		return getTypeOfAccessor(signatureNode);
	case Kind::MethodDeclaration:
	case Kind::FunctionDeclaration:
	case Kind::Constructor:
	case Kind::MethodSignature:
	case Kind::CallSignature:
	case Kind::ConstructSignature:
	case Kind::SetAccessor:
	case Kind::IndexSignature:
	case Kind::FunctionType:
	case Kind::ConstructorType:
	case Kind::FunctionExpression:
	case Kind::ArrowFunction:
	case Kind::JSDocSignature:
		return createReturnFromSignature(signatureNode);
	default:
		TSC_UNREACHABLE(
			"Node needs to be an inferrable node"); // debug.FailBadSyntaxKind
		return nullptr;
	}
}

// lookup.go:26 — GetTypeOfAccessor
PseudoType* PseudoChecker::getTypeOfAccessor(Node* accessor) {
	return typeFromAccessor(accessor);
}

// lookup.go:30 — GetTypeOfExpression
PseudoType* PseudoChecker::getTypeOfExpression(Node* node) {
	return typeFromExpression(node);
}

// lookup.go:34 — GetTypeOfDeclaration
PseudoType* PseudoChecker::getTypeOfDeclaration(Node* node) {
	switch (node->kind) {
	case Kind::Parameter:
		return typeFromParameter(node);
	case Kind::VariableDeclaration:
		return typeFromVariable(node);
	case Kind::PropertySignature:
	case Kind::PropertyDeclaration:
	case Kind::JSDocPropertyTag:
		return typeFromProperty(node);
	case Kind::BindingElement:
		return newPseudoTypeNoResult(node);
	case Kind::ExportAssignment:
		return typeFromExpression(node->as<ExportAssignment>()->Expression);
	case Kind::PropertyAccessExpression:
	case Kind::ElementAccessExpression:
	case Kind::BinaryExpression:
		return typeFromExpandoProperty(node);
	case Kind::PropertyAssignment:
	case Kind::ShorthandPropertyAssignment:
		return typeFromPropertyAssignment(node);
	case Kind::CallExpression:
		switch (getAssignmentDeclarationKind(node)) {
		// TODO: How much of the checker's getTypeFromPropertyDescriptor is
		// worth trying to emulate over ASTs?
		case JSDeclarationKind::ObjectDefinePropertyValue: {
			// !!!
		}
		case JSDeclarationKind::ObjectDefinePropertyExports: {
			// !!!
		}
		default:
			break;
		}
		return newPseudoTypeNoResult(node);
	default:
		TSC_UNREACHABLE(
			"node needs to be an inferrable node"); // debug.FailBadSyntaxKind
		return nullptr;
	}
}

// lookup.go:69 — typeFromPropertyAssignment
PseudoType* PseudoChecker::typeFromPropertyAssignment(Node* node) {
	Node* annotation = node->type();
	if (annotation != nullptr) {
		return newPseudoTypeDirect(annotation);
	}
	if (node->kind == Kind::PropertyAssignment) {
		Node* init = node->initializer();
		if (init != nullptr) {
			PseudoType* expr = typeFromExpression(init);
			if (expr != nullptr &&
			    (expr->kind != PseudoTypeKind::Inferred ||
			     !expr->AsPseudoTypeInferred()->errorNodes.empty())) {
				return expr;
			}
			// fallback to NoResult if PseudoTypeKindInferred without error
			// nodes
		}
	}
	return newPseudoTypeNoResult(node);
}

// lookup.go:88 — typeFromExpandoProperty
// This is _not_ redundant with the reparser; see how
// expandoFunctionSymbolProperty.ts and similar behaves
PseudoType* PseudoChecker::typeFromExpandoProperty(Node* node) {
	Node* declaredType = node->type();
	if (declaredType != nullptr) {
		return newPseudoTypeDirect(declaredType);
	}
	// While `node` is an expression, as an expando, it should also always be a
	// declaration with a `.Symbol()` which requires declaration fallback
	// handling
	return newPseudoTypeNoResult(node);
}

// lookup.go:98 — typeFromProperty
PseudoType* PseudoChecker::typeFromProperty(Node* node) {
	Node* t = node->type();
	if (t != nullptr) {
		return newPseudoTypeDirect(t);
	}
	if (isPropertyDeclaration(node)) {
		Node* init = node->initializer();
		if (init != nullptr && !isContextuallyTyped(node)) {
			// explicit fail on readonly template literals to allow for
			// literal freshness in the future
			if (hasModifier(node, ModifierFlagsReadonly) &&
			    isTemplateExpression(init)) {
				return newPseudoTypeNoResult(node);
			}
			PseudoType* expr = typeFromExpression(init);
			if (expr != nullptr &&
			    (expr->kind != PseudoTypeKind::Inferred ||
			     !expr->AsPseudoTypeInferred()->errorNodes.empty())) {
				if (expr->kind != PseudoTypeKind::Direct &&
				    node->as<PropertyDeclaration>()->PostfixToken != nullptr &&
				    node->as<PropertyDeclaration>()->PostfixToken->kind ==
				        Kind::QuestionToken) {
					// type comes from the initializer expression on a
					// property with a `?` - add `| undefined` to the type
					return addUndefinedIfDefinitelyRequired(expr);
				}
				return expr;
			}
			// fallback to NoResult if PseudoTypeKindInferred without error
			// nodes
		}
	}
	return newPseudoTypeNoResult(node);
}

// lookup.go:124 — typeFromVariable
PseudoType* PseudoChecker::typeFromVariable(Node* declaration) {
	auto* d = declaration->as<VariableDeclaration>();
	if (d->Type != nullptr) {
		return newPseudoTypeDirect(d->Type);
	}
	Node* init = d->Initializer;
	if (init != nullptr && d->Symbol != nullptr &&
	    (d->Symbol->data->declarations.size() == 1 ||
	     static_cast<int>(std::count_if(
		     d->Symbol->data->declarations.begin(), d->Symbol->data->declarations.end(),
		     [](Node* n) { return isVariableDeclaration(n); })) == 1)) {
		if (!isContextuallyTyped(declaration)) { // TODO: also should bail on
			// expando declarations; reuse syntactic expando check used in
			// declaration emit
			// TODO: Strada forces an inference fallback on `const` variables
			// with template expression initializers, to leave space for
			// template literal freshness in the future
			if (isVarConst(declaration) && isTemplateExpression(init)) {
				return newPseudoTypeNoResult(declaration);
			}
			PseudoType* expr = typeFromExpression(init);
			if (expr != nullptr &&
			    (expr->kind != PseudoTypeKind::Inferred ||
			     !expr->AsPseudoTypeInferred()->errorNodes.empty())) {
				return expr;
			}
			// fallback to NoResult if PseudoTypeKindInferred without error
			// nodes
		}
	}
	return newPseudoTypeNoResult(declaration);
}

// lookup.go:146 — typeFromAccessor
PseudoType* PseudoChecker::typeFromAccessor(Node* accessor) {
	AllAccessorDeclarations accessorDeclarations =
		getAllAccessorDeclarationsForDeclaration(
			accessor, (*accessor->declarationData().symbol)->data->declarations);
	Node* accessorType = getTypeAnnotationFromAllAccessorDeclarations(
		accessor, accessorDeclarations);
	if (accessorType != nullptr && !isTypePredicateNode(accessorType)) {
		return newPseudoTypeDirect(accessorType);
	}
	if (accessorDeclarations.getAccessor != nullptr) {
		PseudoType* res =
			createReturnFromSignature(accessorDeclarations.getAccessor);
		if (res->kind == PseudoTypeKind::Inferred &&
		    res->AsPseudoTypeInferred()->errorNodes.empty()) {
			std::vector<Node*> errorNodes{accessorDeclarations.getAccessor};
			if (accessorDeclarations.setAccessor != nullptr) {
				errorNodes.push_back(accessorDeclarations.setAccessor);
			}
			res = newPseudoTypeInferredWithErrors(
				res->AsPseudoTypeInferred()->expression,
				res->AsPseudoTypeInferred()->isSignatureReturn,
				errorNodes); // Move error up to the accessor
		}
		return res;
	}
	return newPseudoTypeNoResult(accessor);
}

// lookup.go:166 — getTypeAnnotationFromAllAccessorDeclarations
Node* PseudoChecker::getTypeAnnotationFromAllAccessorDeclarations(
	Node* node, const AllAccessorDeclarations& accessors) {
	Node* accessorType = getTypeAnnotationFromAccessor(node);
	if (accessorType == nullptr && node != accessors.firstAccessor) {
		accessorType = getTypeAnnotationFromAccessor(accessors.firstAccessor);
	}
	if (accessorType == nullptr && accessors.secondAccessor != nullptr &&
	    node != accessors.secondAccessor) {
		accessorType = getTypeAnnotationFromAccessor(accessors.secondAccessor);
	}
	return accessorType;
}

// lookup.go:177 — getTypeAnnotationFromAccessor
Node* PseudoChecker::getTypeAnnotationFromAccessor(Node* node) {
	if (node == nullptr) {
		return nullptr;
	}
	// !!! TODO: support ripping return type off of .FullSignature
	if (node->kind == Kind::GetAccessor) {
		return node->as<GetAccessorDeclaration>()->Type;
	}
	auto* set = node->as<SetAccessorDeclaration>();
	if (set->Parameters == nullptr || set->Parameters->nodes.empty()) {
		return nullptr;
	}
	Node* p = set->Parameters->nodes[0];
	if (!isParameterDeclaration(p)) {
		return nullptr;
	}
	return p->as<ParameterDeclaration>()->Type;
}

// lookup.go:201 — createReturnFromSignature
// does not return `nil`, returns a `NoResult` pseudotype instead
PseudoType* PseudoChecker::createReturnFromSignature(Node* fn) {
	if (isFunctionLike(fn)) {
		auto d = fn->functionLikeData();
		// !!! TODO: support ripping return type off of .FullSignature
		Node* r = d.type != nullptr ? *d.type : nullptr;
		if (r != nullptr) {
			return newPseudoTypeDirect(r);
		}
	}
	if (isValueSignatureDeclaration(fn)) {
		return typeFromSingleReturnExpression(fn);
	}
	return newPseudoTypeNoResult(fn);
}

// lookup.go:216 — typeFromSingleReturnExpression
PseudoType* PseudoChecker::typeFromSingleReturnExpression(Node* fn) {
	Node* candidateExpr = nullptr;
	if (fn != nullptr && !nodeIsMissing(fn->body())) {
		FunctionFlags flags = getFunctionFlags(fn);
		if ((flags & FunctionFlagsAsyncGenerator) != 0) {
			return newPseudoTypeInferred(fn, true);
		}

		Node* body = fn->body();
		if (isBlock(body)) {
			forEachReturnStatement(body, [&](Node* stmt) -> bool {
				if (stmt->parent != body) { // Why bail on nested return
					                        // statements?
					candidateExpr = nullptr;
					return true;
				}
				if (candidateExpr == nullptr) {
					candidateExpr =
						stmt->as<ReturnStatement>()->Expression;
				} else {
					candidateExpr = nullptr;
					return true;
				}
				return false;
			});
		} else {
			candidateExpr = body;
		}
	}
	if (candidateExpr != nullptr) {
		if (isContextuallyTyped(candidateExpr)) {
			Node* t = nullptr;
			if (candidateExpr->kind == Kind::TypeAssertionExpression) {
				t = candidateExpr->as<TypeAssertion>()->Type;
			} else if (candidateExpr->kind == Kind::AsExpression) {
				t = candidateExpr->as<AsExpression>()->Type;
			}
			if (t != nullptr && !isConstTypeReference(t)) {
				return newPseudoTypeDirect(t);
			}
		} else {
			return typeFromExpression(candidateExpr);
		}
	}
	return newPseudoTypeInferred(fn, true);
}

// lookup.go:262 — typeFromExpression
// This is basically `checkExpression` for pseudotypes
PseudoType* PseudoChecker::typeFromExpression(Node* node) {
	switch (node->kind) {
	case Kind::OmittedExpression:
		return PseudoTypeUndefined;
	case Kind::ParenthesizedExpression:
		// assertions transformed on reparse, just unwrap
		return typeFromExpression(
			node->as<ParenthesizedExpression>()->Expression);
	case Kind::Identifier:
		// !!! TODO: in strada, this uses symbol information to ensure `node`
		// refers to the global `undefined` symbol instead
		// we should probably import `resolveName` and use it here to check for
		// the same; but we have to setup some barebones pseudoglobals for
		// that to work!
		if (node->as<Identifier>()->Text == "undefined") {
			return PseudoTypeUndefined;
		}
		break;
	case Kind::NullKeyword:
		return PseudoTypeNull;
	case Kind::ArrowFunction:
	case Kind::FunctionExpression:
		return typeFromFunctionLikeExpression(node);
	case Kind::TypeAssertionExpression:
		return typeFromTypeAssertion(node->as<TypeAssertion>()->Expression,
		                             node->as<TypeAssertion>()->Type);
	case Kind::AsExpression:
		return typeFromTypeAssertion(node->as<AsExpression>()->Expression,
		                             node->as<AsExpression>()->Type);
	case Kind::PrefixUnaryExpression:
		if (isPrimitiveLiteralValue(node, true)) {
			return typeFromPrimitiveLiteralPrefix(node);
		}
		break;
	case Kind::ArrayLiteralExpression:
		return typeFromArrayLiteral(node);
	case Kind::ObjectLiteralExpression:
		return typeFromObjectLiteral(node);
	case Kind::ClassExpression:
		return newPseudoTypeInferredWithErrors(
			node, false, {node}); // No possible annotation/directly mappable
			                      // syntax
	case Kind::TemplateExpression:
		// templateLitWithHoles as const, not supported
		if (isInConstContext(node)) {
			return newPseudoTypeInferred(node, false);
		}
		return newPseudoTypeMaybeConstLocation(
			node, newPseudoTypeInferred(node, false), PseudoTypeString);
	case Kind::NumericLiteral:
		return newPseudoTypeMaybeConstLocation(
			node, newPseudoTypeNumericLiteral(node), PseudoTypeNumber);
	case Kind::NoSubstitutionTemplateLiteral:
		return newPseudoTypeMaybeConstLocation(
			node, newPseudoTypeStringLiteral(node), PseudoTypeString);
	case Kind::StringLiteral:
		return newPseudoTypeMaybeConstLocation(
			node, newPseudoTypeStringLiteral(node), PseudoTypeString);
	case Kind::BigIntLiteral:
		return newPseudoTypeMaybeConstLocation(
			node, newPseudoTypeBigIntLiteral(node), PseudoTypeBigInt);
	case Kind::TrueKeyword:
		return newPseudoTypeMaybeConstLocation(node, PseudoTypeTrue,
		                                       PseudoTypeBoolean);
	case Kind::FalseKeyword:
		return newPseudoTypeMaybeConstLocation(node, PseudoTypeFalse,
		                                       PseudoTypeBoolean);
	default:
		break;
	}
	return newPseudoTypeInferred(node, false);
}

// lookup.go:315 — typeFromObjectLiteral
PseudoType* PseudoChecker::typeFromObjectLiteral(Node* node) {
	if (std::optional<std::vector<Node*>> errorNodes =
	        canGetTypeFromObjectLiteral(node)) {
		return newPseudoTypeInferredWithErrors(node, false, *errorNodes);
	}
	// we are in a const context producing an object literal type, there are no
	// shorthand or spread assignments
	if (node->as<ObjectLiteralExpression>()->Properties == nullptr ||
	    node->as<ObjectLiteralExpression>()->Properties->nodes.empty()) {
		return newPseudoTypeObjectLiteral({});
	}
	std::vector<PseudoObjectElement*> results;
	for (Node* e : node->as<ObjectLiteralExpression>()->Properties->nodes) {
		switch (e->kind) {
		case Kind::MethodDeclaration: {
			bool optional = e->as<MethodDeclaration>()->PostfixToken != nullptr &&
			                e->as<MethodDeclaration>()->PostfixToken->kind ==
			                    Kind::QuestionToken;
			if (e->functionLikeData().fullSignature != nullptr &&
			    *e->functionLikeData().fullSignature != nullptr) {
				results.push_back(newPseudoPropertyAssignment(
					false, e->name(), optional,
					newPseudoTypeDirect(
						*e->functionLikeData().fullSignature)));
			} else {
				results.push_back(newPseudoObjectMethod(
					e, e->name(), optional,
					cloneTypeParameters(
						e->as<MethodDeclaration>()->TypeParameters),
					cloneParameters(e->parameterList()),
					createReturnFromSignature(e)));
			}
			break;
		}
		case Kind::PropertyAssignment:
			results.push_back(newPseudoPropertyAssignment(
				false, e->name(),
				e->as<PropertyAssignment>()->PostfixToken != nullptr &&
				    e->as<PropertyAssignment>()->PostfixToken->kind ==
				        Kind::QuestionToken,
				typeFromExpression(e->initializer())));
			break;
		case Kind::SetAccessor:
		case Kind::GetAccessor: {
			PseudoObjectElement* member = getAccessorMember(e, e->name());
			if (member != nullptr) {
				results.push_back(member);
			}
			break;
		}
		default:
			break;
		}
	}
	return newPseudoTypeObjectLiteral(results);
}

// lookup.go:363 — getAccessorMember
// roughly analogous to typeFromObjectLiteralAccessor in strada
PseudoObjectElement* PseudoChecker::getAccessorMember(Node* accessor,
                                                    Node* name) {
	AllAccessorDeclarations allAccessors =
		getAllAccessorDeclarationsForDeclaration(
			accessor, accessor->symbol()->data->declarations); // TODO: node
	// preservation for late-bound accessor pairs?

	// TODO: handle pseudo-annotations from get accessor return positions?
	if (allAccessors.getAccessor != nullptr &&
	    allAccessors.getAccessor->type() != nullptr &&
	    allAccessors.setAccessor != nullptr &&
	    !allAccessors.setAccessor->as<SetAccessorDeclaration>()
	         ->Parameters->nodes.empty() &&
	    allAccessors.setAccessor->as<SetAccessorDeclaration>()
	            ->Parameters->nodes[0]
	            ->as<ParameterDeclaration>()
	            ->Type != nullptr) {
		// We have possible types for both accessors, we can't know if they are
		// the same type so we keep both accessors

		if (isGetAccessorDeclaration(accessor)) {
			return newPseudoGetAccessor(accessor, name, false,
			                            typeFromAccessor(accessor));
		} else {
			return newPseudoSetAccessor(
				accessor, name, false,
				cloneParameters(
					accessor->as<SetAccessorDeclaration>()->Parameters)[0]);
		}
	}

	if (accessor == allAccessors.firstAccessor) {
		// only one annotated accessor; output a property - `readonly` for a
		// single `get` accessor

		PseudoType* accessorType = typeFromAccessor(accessor);
		bool readonly = isGetAccessorDeclaration(accessor) &&
		                allAccessors.secondAccessor == nullptr;
		return newPseudoPropertyAssignment(readonly, name, false, accessorType);
	}
	return nullptr;
}

// lookup.go:406 — canGetTypeFromObjectLiteral checks whether an object literal
// can be typed by the pseudochecker. Returns nil if the object can be typed,
// or a slice of error nodes (shorthand/spread properties, non-literal
// computed names) that prevent typing.
std::optional<std::vector<Node*>> PseudoChecker::canGetTypeFromObjectLiteral(
	Node* node) {
	if (node->as<ObjectLiteralExpression>()->Properties == nullptr ||
	    node->as<ObjectLiteralExpression>()->Properties->nodes.empty()) {
		return std::nullopt; // empty object, ok
	}
	std::vector<Node*> errorNodes;
	for (Node* e : node->as<ObjectLiteralExpression>()->Properties->nodes) {
		if ((e->flags & NodeFlagsThisNodeHasError) != 0) {
			errorNodes.push_back(e);
			continue;
		}
		if (e->kind == Kind::ShorthandPropertyAssignment ||
		    e->kind == Kind::SpreadAssignment) {
			errorNodes.push_back(e);
			continue;
		}
		if ((e->name()->flags & NodeFlagsThisNodeHasError) != 0) {
			errorNodes.push_back(e->name());
			continue;
		}
		if (e->name()->kind == Kind::PrivateIdentifier) {
			errorNodes.push_back(e);
			continue;
		}
		if (e->name()->kind == Kind::ComputedPropertyName) {
			Node* expression = e->name()->expression();
			if (!isPrimitiveLiteralValue(expression, false)) {
				errorNodes.push_back(e->name());
			}
		}
	}
	// Go returns a nil slice when no error nodes were found — an engaged
	// empty vector would be misread as "cannot type" by callers.
	if (errorNodes.empty()) {
		return std::nullopt;
	}
	return errorNodes;
}

// lookup.go:438 — typeFromArrayLiteral
PseudoType* PseudoChecker::typeFromArrayLiteral(Node* node) {
	if (std::optional<std::vector<Node*>> errorNodes =
	        canGetTypeFromArrayLiteral(node)) {
		return newPseudoTypeInferredWithErrors(node, false, *errorNodes);
	}
	if (isInConstContext(node) && isContextuallyTyped(node)) {
		return newPseudoTypeInferred(node, false); // expr in an as const cast
		                                          // with a contextual type has
		                                          // variable readonly state,
		                                          // bail
	}
	// we are in a const context producing a tuple type, there are no spread
	// elements
	std::vector<PseudoType*> results;
	for (Node* e : node->as<ArrayLiteralExpression>()->Elements->nodes) {
		results.push_back(typeFromExpression(e));
	}
	return newPseudoTypeTuple(results);
}

// lookup.go:457 — canGetTypeFromArrayLiteral checks whether an array literal
// can be typed by the pseudochecker. Returns nil if the array can be typed,
// or a slice of error nodes that prevent typing.
// For non-const arrays, the error node is the array expression itself.
// For const arrays with spreads, the error node is the spread element.
std::optional<std::vector<Node*>> PseudoChecker::canGetTypeFromArrayLiteral(
	Node* node) {
	if (!isInConstContext(node)) {
		return std::vector<Node*>{node};
	}
	for (Node* e : node->as<ArrayLiteralExpression>()->Elements->nodes) {
		if (e->kind == Kind::SpreadElement) {
			return std::vector<Node*>{e};
		}
	}
	return std::nullopt;
}

// lookup.go:482 — IsInConstContext traverses up the parent chain to determine
// if the node is within a const context without needing any persistent
// traversal scope tracking (which could be unreliable in the presence of
// `typeof` queries anyway!)
bool isInConstContext(Node* node) {
	// An expression is in a const context if an ancestor is a const type
	// maybeAssertion expression
	Node* maybeAssertion = findAncestor(node->parent, [](Node* n) -> bool {
		// stop traversing at assertions or anything not an array/object
		// literal, since only those create or transfer const-ness
		return isAssertionExpression(n) ||
		       !isConstContextPropagatingKind(n->kind);
	});
	return isConstAssertion(maybeAssertion);
}

// lookup.go:494 — typeFromPrimitiveLiteralPrefix
PseudoType* PseudoChecker::typeFromPrimitiveLiteralPrefix(Node* node) {
	auto* n = node->as<PrefixUnaryExpression>();
	Node* expr = node;
	if (n->Operator == Kind::PlusToken) {
		expr = n->Operand;
	}
	Node* inner = n->Operand;
	if (inner->kind == Kind::BigIntLiteral) {
		return newPseudoTypeMaybeConstLocation(
			node, newPseudoTypeBigIntLiteral(expr), PseudoTypeBigInt);
	}
	if (inner->kind == Kind::NumericLiteral) {
		return newPseudoTypeMaybeConstLocation(
			node, newPseudoTypeNumericLiteral(expr), PseudoTypeNumber);
	}
	TSC_UNREACHABLE("failBadSyntaxKind"); // debug.FailBadSyntaxKind(inner)
	return nullptr;
}

// lookup.go:510 — typeFromTypeAssertion
PseudoType* PseudoChecker::typeFromTypeAssertion(Node* expression,
                                                 Node* typeNode) {
	if (isConstTypeReference(typeNode)) {
		return typeFromExpression(expression);
	}
	return newPseudoTypeDirect(typeNode);
}

// lookup.go:517 — typeFromFunctionLikeExpression
PseudoType* PseudoChecker::typeFromFunctionLikeExpression(Node* node) {
	auto d = node->functionLikeData();
	if (d.fullSignature != nullptr && *d.fullSignature != nullptr) {
		return newPseudoTypeDirect(*d.fullSignature);
	}
	PseudoType* returnType = createReturnFromSignature(node);
	std::vector<Node*> typeParameters = cloneTypeParameters(*d.typeParameters);
	std::vector<PseudoParameter*> parameters = cloneParameters(*d.parameters);
	return newPseudoTypeSingleCallSignature(node, parameters, typeParameters,
	                                        returnType);
}

// lookup.go:532 — cloneTypeParameters
std::vector<Node*> PseudoChecker::cloneTypeParameters(NodeList* nodes) {
	if (nodes == nullptr || nodes->nodes.empty()) {
		return {};
	}
	return nodes->nodes;
}

// lookup.go:578 — CouldAlreadyReferToUndefinedType
// see this as the inverse of `canAddUndefined` in `expressionToTypeNode` in
// strada
bool couldAlreadyReferToUndefinedType(PseudoType* t) {
	if (t->kind == PseudoTypeKind::NoResult ||
	    t->kind == PseudoTypeKind::Inferred || isUndefinedPseudoType(t)) {
		return true;
	}
	if (t->kind == PseudoTypeKind::MaybeConstLocation) {
		auto* mc = t->AsPseudoTypeMaybeConstLocation();
		return couldAlreadyReferToUndefinedType(
			mc->regularType); // if we're even asking this question, it's not a
		                      // `const` location
	}
	if (t->kind == PseudoTypeKind::Direct) {
		// inspect the direct type node
		Node* node = t->AsPseudoTypeDirect()->typeNode;
		return typeNodeCouldReferToUndefined(node);
	}
	if (t->kind == PseudoTypeKind::Union) {
		return std::any_of(t->AsPseudoTypeUnion()->types.begin(),
		                   t->AsPseudoTypeUnion()->types.end(),
		                   [](PseudoType* u) {
			                   return couldAlreadyReferToUndefinedType(u);
		                   });
	}
	return false;
}

// lookup.go:631 — typeFromParameter
PseudoType* PseudoChecker::typeFromParameter(Node* node) {
	Node* parent = node->parent;
	if (parent->kind == Kind::SetAccessor) {
		return getTypeOfAccessor(parent);
	}
	// Fast path: no initializer means we never need parameter position info.
	auto* p = node->as<ParameterDeclaration>();
	if (p->Initializer == nullptr) {
		if (p->Type != nullptr) {
			return newPseudoTypeDirect(p->Type);
		}
		return newPseudoTypeNoResult(node);
	}
	std::vector<Node*> params = parent->parameters();
	auto it = std::find(params.begin(), params.end(), node);
	int selfIdx = it == params.end()
	                  ? -1
	                  : static_cast<int>(std::distance(params.begin(), it));
	int lastRequired = lastRequiredParamIndex(params);
	return typeFromParameterWorker(node, selfIdx, lastRequired);
}

// lookup.go:649 — typeFromParameterWorker
PseudoType* PseudoChecker::typeFromParameterWorker(Node* node, int selfIdx,
                                                   int lastRequired) {
	Node* parent = node->parent;
	if (parent->kind == Kind::SetAccessor) {
		return getTypeOfAccessor(parent);
	}
	bool hasRequiredAfter = selfIdx < lastRequired - 1;
	auto* p = node->as<ParameterDeclaration>();
	if (p->Type != nullptr) {
		PseudoType* result = newPseudoTypeDirect(p->Type);
		// When the parameter has an initializer and strict null checks are
		// enabled, check if `| undefined` needs to be added because there are
		// required parameters after this one. This mirrors the checker's
		// getTypeOfParameter which adds optionality for initialized
		// parameters.
		if (strictNullChecks && p->Initializer != nullptr &&
		    hasRequiredAfter) {
			return addUndefinedIfDefinitelyRequired(result);
		}
		return result;
	}
	if (p->Initializer != nullptr && isIdentifier(node->name()) &&
	    !isContextuallyTyped(node)) {
		PseudoType* expr = typeFromExpression(p->Initializer);
		if (expr != nullptr && expr->kind == PseudoTypeKind::Inferred &&
		    expr->AsPseudoTypeInferred()->errorNodes.empty()) {
			expr = newPseudoTypeInferredWithErrors(
				expr->AsPseudoTypeInferred()->expression, false,
				{node}); // Move error up to the parameter
		}
		if (!strictNullChecks) {
			return expr;
		}
		if (!hasRequiredAfter) {
			return expr;
		}
		// if there is a non-optional parameter after this one, a
		// `| undefined` will need to explicitly be emitted on this parameter,
		// if it's not already there
		return addUndefinedIfDefinitelyRequired(expr);
	}
	// TODO: In strada, the ID checker doesn't infer a parameter type from
	// binding pattern names, but the real checker _does_!
	// This means ID won't let you write, say, `({elem}) => false` without an
	// annotation, even though it's trivially of type
	// `(p0: {elem: any}) => boolean` and error-free under `noImplicitAny:
	// false`! That limitation is retained here.
	return newPseudoTypeNoResult(node);
}

// lookup.go:687 — cloneParameters
std::vector<PseudoParameter*> PseudoChecker::cloneParameters(
	NodeList* nodes) {
	if (nodes == nullptr || nodes->nodes.empty()) {
		return {};
	}
	int lastRequired = lastRequiredParamIndex(nodes->nodes);
	std::vector<PseudoParameter*> result;
	int i = 0;
	for (Node* e : nodes->nodes) {
		auto* p = e->as<ParameterDeclaration>();
		bool optional = p->QuestionToken != nullptr;
		if (!optional && p->Initializer != nullptr) {
			// A parameter with an initializer is optional only if all
			// subsequent parameters are also optional/have initializers/are
			// rest parameters. This matches the checker's isOptionalParameter
			// semantics.
			optional = i >= lastRequired - 1;
		}
		result.push_back(newPseudoParameter(
			p->DotDotDotToken != nullptr, e->name(), optional,
			typeFromParameterWorker(p, i, lastRequired)));
		i++;
	}
	return result;
}

} // namespace tsc::pseudochecker
