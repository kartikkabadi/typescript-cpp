// Port of tsc/internal/pseudochecker (type.go + checker.go + lookup.go).
// `PseudoType`s are skeletons of types - partially interpreted expressions and
// type nodes composed to represent how you *should* construct a type out of
// them. They can be trivially mapped into actual types by a real `Checker`, or
// into a tree of `Node`s directly, without needing to make any intermediate
// types, by a `NodeBuilder`. Unlike checker `Type`s, these are never
// normalized, and multiple pseudo-types may refer to the same underlying
// `Type`.
#pragma once

#include <optional>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/ast/visitor.h"

namespace tsc::pseudochecker {


// type.go: PseudoTypeKind
enum class PseudoTypeKind : int16_t {
	Direct = 0,
	Inferred,
	NoResult,
	MaybeConstLocation,
	Union,
	Undefined,
	Null,
	Any,
	String,
	Number,
	BigInt,
	Boolean,
	False,
	True,
	SingleCallSignature,
	Tuple,
	ObjectLiteral,
	StringLiteral,
	NumericLiteral,
	BigIntLiteral,
};

struct PseudoType;
struct PseudoParameter;
struct PseudoObjectElement;

struct PseudoType {
	PseudoTypeKind kind = PseudoTypeKind::Any;
	// The variant payload. Exactly one of the following is non-null, by kind:
	//   Direct          -> direct
	//   Inferred        -> inferred
	//   NoResult        -> noResult
	//   MaybeConstLocation -> maybeConstLocation
	//   Union           -> union
	//   SingleCallSignature -> singleCallSignature
	//   Tuple           -> tuple
	//   ObjectLiteral   -> objectLiteral
	//   StringLiteral | NumericLiteral | BigIntLiteral -> literal
	struct PseudoTypeDirect {
		Node* typeNode = nullptr;
	};
	struct PseudoTypeInferred {
		Node* expression = nullptr;
		std::vector<Node*> errorNodes;
		bool isSignatureReturn = false;
	};
	struct PseudoTypeNoResult {
		Node* declaration = nullptr;
	};
	struct PseudoTypeMaybeConstLocation {
		Node* node = nullptr;
		PseudoType* constType = nullptr;
		PseudoType* regularType = nullptr;
	};
	struct PseudoTypeUnion {
		std::vector<PseudoType*> types;
	};
	struct PseudoTypeSingleCallSignature {
		Node* signature = nullptr;
		std::vector<PseudoParameter*> parameters;
		std::vector<Node*> typeParameters;
		PseudoType* returnType = nullptr;
	};
	struct PseudoTypeTuple {
		std::vector<PseudoType*> elements;
	};
	struct PseudoTypeObjectLiteral {
		std::vector<PseudoObjectElement*> elements;
	};
	struct PseudoTypeLiteral {
		Node* node = nullptr;
	};

	// Non-owning pointers into a per-allocation payload; only one is set.
	PseudoTypeDirect* direct = nullptr;
	PseudoTypeInferred* inferred = nullptr;
	PseudoTypeNoResult* noResult = nullptr;
	PseudoTypeMaybeConstLocation* maybeConstLocation = nullptr;
	PseudoTypeUnion* union_ = nullptr;
	PseudoTypeSingleCallSignature* singleCallSignature = nullptr;
	PseudoTypeTuple* tuple = nullptr;
	PseudoTypeObjectLiteral* objectLiteral = nullptr;
	PseudoTypeLiteral* literal = nullptr;

	PseudoTypeDirect* AsPseudoTypeDirect() { return direct; }
	PseudoTypeInferred* AsPseudoTypeInferred() { return inferred; }
	PseudoTypeNoResult* AsPseudoTypeNoResult() { return noResult; }
	PseudoTypeMaybeConstLocation* AsPseudoTypeMaybeConstLocation() {
		return maybeConstLocation;
	}
	PseudoTypeUnion* AsPseudoTypeUnion() { return union_; }
	PseudoTypeSingleCallSignature* AsPseudoTypeSingleCallSignature() {
		return singleCallSignature;
	}
	PseudoTypeTuple* AsPseudoTypeTuple() { return tuple; }
	PseudoTypeObjectLiteral* AsPseudoTypeObjectLiteral() {
		return objectLiteral;
	}
	PseudoTypeLiteral* AsPseudoTypeLiteral() { return literal; }
};

// Singleton pseudotypes (type.go:71-79).
extern PseudoType* PseudoTypeUndefined;
extern PseudoType* PseudoTypeNull;
extern PseudoType* PseudoTypeAny;
extern PseudoType* PseudoTypeString;
extern PseudoType* PseudoTypeNumber;
extern PseudoType* PseudoTypeBigInt;
extern PseudoType* PseudoTypeBoolean;
extern PseudoType* PseudoTypeFalse;
extern PseudoType* PseudoTypeTrue;

// type.go constructors
PseudoType* newPseudoTypeDirect(Node* typeNode);
PseudoType* newPseudoTypeInferred(Node* expr, bool isSignatureReturn);
PseudoType* newPseudoTypeInferredWithErrors(Node* expr, bool isSignatureReturn,
                                            std::vector<Node*> errorNodes);
PseudoType* newPseudoTypeNoResult(Node* decl);
PseudoType* newPseudoTypeMaybeConstLocation(Node* loc, PseudoType* ct,
                                            PseudoType* reg);
PseudoType* newPseudoTypeUnion(std::vector<PseudoType*> types);
PseudoType* newPseudoTypeSingleCallSignature(
	Node* signature, std::vector<PseudoParameter*> parameters,
	std::vector<Node*> typeParameters, PseudoType* returnType);
PseudoType* newPseudoTypeTuple(std::vector<PseudoType*> elements);
PseudoType* newPseudoTypeObjectLiteral(
	std::vector<PseudoObjectElement*> elements);
PseudoType* newPseudoTypeStringLiteral(Node* node);
PseudoType* newPseudoTypeNumericLiteral(Node* node);
PseudoType* newPseudoTypeBigIntLiteral(Node* node);

// type.go: PseudoParameter
struct PseudoParameter {
	bool rest = false;
	Node* name = nullptr;
	bool optional = false;
	PseudoType* type = nullptr;
};

PseudoParameter* newPseudoParameter(bool isRest, Node* name, bool isOptional,
                                    PseudoType* t);

// type.go: PseudoObjectElementKind
enum class PseudoObjectElementKind : int8_t {
	Method = 0,
	PropertyAssignment,
	SetAccessor,
	GetAccessor,
};

struct PseudoObjectElement {
	Node* name = nullptr;
	bool optional = false;
	PseudoObjectElementKind kind = PseudoObjectElementKind::PropertyAssignment;
	// Variant payloads — exactly one non-null per kind:
	//   Method -> method, PropertyAssignment -> propertyAssignment,
	//   SetAccessor -> setAccessor, GetAccessor -> getAccessor
	struct PseudoObjectMethod {
		Node* signature = nullptr;
		std::vector<Node*> typeParameters;
		std::vector<PseudoParameter*> parameters;
		PseudoType* returnType = nullptr;
	};
	struct PseudoPropertyAssignment {
		bool readonly = false;
		PseudoType* type = nullptr;
	};
	struct PseudoSetAccessor {
		Node* signature = nullptr;
		PseudoParameter* parameter = nullptr;
	};
	struct PseudoGetAccessor {
		Node* signature = nullptr;
		PseudoType* type = nullptr;
	};

	PseudoObjectMethod* method = nullptr;
	PseudoPropertyAssignment* propertyAssignment = nullptr;
	PseudoSetAccessor* setAccessor = nullptr;
	PseudoGetAccessor* getAccessor = nullptr;

	// type.go:227 — Signature
	Node* Signature() const;
};

PseudoObjectElement* newPseudoObjectMethod(
	Node* signature, Node* name, bool optional,
	std::vector<Node*> typeParameters,
	std::vector<PseudoParameter*> parameters, PseudoType* returnType);
PseudoObjectElement* newPseudoPropertyAssignment(bool readonly, Node* name,
                                                 bool optional, PseudoType* t);
PseudoObjectElement* newPseudoSetAccessor(Node* signature, Node* name,
                                          bool optional, PseudoParameter* p);
PseudoObjectElement* newPseudoGetAccessor(Node* signature, Node* name,
                                          bool optional, PseudoType* t);

// checker.go: PseudoChecker
struct PseudoChecker {
	bool strictNullChecks = false;
	bool exactOptionalPropertyTypes = false;

	// lookup.go
	PseudoType* getReturnTypeOfSignature(Node* signatureNode);
	PseudoType* getTypeOfAccessor(Node* accessor);
	PseudoType* getTypeOfExpression(Node* node);
	PseudoType* getTypeOfDeclaration(Node* node);

	PseudoType* typeFromPropertyAssignment(Node* node);
	PseudoType* typeFromExpandoProperty(Node* node);
	PseudoType* typeFromProperty(Node* node);
	PseudoType* typeFromVariable(Node* declaration);
	PseudoType* typeFromAccessor(Node* accessor);
	Node* getTypeAnnotationFromAllAccessorDeclarations(
		Node* node, const AllAccessorDeclarations& accessors);
	Node* getTypeAnnotationFromAccessor(Node* node);
	PseudoType* createReturnFromSignature(Node* fn);
	PseudoType* typeFromSingleReturnExpression(Node* fn);
	PseudoType* typeFromExpression(Node* node);
	PseudoType* typeFromObjectLiteral(Node* node);
	PseudoObjectElement* getAccessorMember(Node* accessor, Node* name);
	// Go returns nil-able slices; nullopt models nil.
	std::optional<std::vector<Node*>> canGetTypeFromObjectLiteral(Node* node);
	PseudoType* typeFromArrayLiteral(Node* node);
	std::optional<std::vector<Node*>> canGetTypeFromArrayLiteral(Node* node);
	PseudoType* typeFromPrimitiveLiteralPrefix(Node* node);
	PseudoType* typeFromTypeAssertion(Node* expression, Node* typeNode);
	PseudoType* typeFromFunctionLikeExpression(Node* node);
	std::vector<Node*> cloneTypeParameters(NodeList* nodes);
	PseudoType* typeFromParameter(Node* node);
	PseudoType* typeFromParameterWorker(Node* node, int selfIdx,
	                                    int lastRequired);
	std::vector<PseudoParameter*> cloneParameters(NodeList* nodes);
};

PseudoChecker* newPseudoChecker(bool strictNullChecks,
                                bool exactOptionalPropertyTypes);

// lookup.go free functions
bool isInConstContext(Node* node);
bool couldAlreadyReferToUndefinedType(PseudoType* t);

} // namespace tsc::pseudochecker
