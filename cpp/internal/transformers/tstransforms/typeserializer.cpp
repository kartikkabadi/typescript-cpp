// Port of tsc/internal/transformers/tstransforms/typeserializer.go.
#include "internal/transformers/tstransforms/tstransforms.h"

#include "internal/checker/checker.h" // checker::EmitResolver

namespace tsc::transformers::tstransforms {
namespace {

// RAII helper for the Go `defer s.setContext(oldCtx)` pattern.
struct serializerContextGuard {
	metadataSerializer* s;
	metadataSerializerContext old;
	serializerContextGuard(metadataSerializer* s, metadataSerializerContext ctx)
		: s(s), old(s->c) {
		s->c = ctx;
	}
	~serializerContextGuard() { s->c = old; }
};

// ast/utilities.go:4493 — GetRestParameterElementType (static; internal
// linkage, no conflict).
Node* getRestParameterElementType(Node* node) {
	if (node == nullptr) {
		return node;
	}
	if (node->kind == Kind::ArrayType) {
		return node->as<ArrayTypeNode>()->ElementType;
	}
	if (node->kind == Kind::TypeReference &&
		node->as<TypeReferenceNode>()->TypeArguments != nullptr) {
		auto& nodes = node->as<TypeReferenceNode>()->TypeArguments->nodes;
		return nodes.empty() ? nullptr : nodes[0];
	}
	return nullptr;
}

} // namespace

// GetSetAccessorValueParameter — typeserializer.go:55
Node* GetSetAccessorValueParameter(SetAccessorDeclaration* node) {
	if (node != nullptr && !node->Parameters->nodes.empty()) {
		if (node->Parameters->nodes.size() >= 2 &&
			isThisParameter(node->Parameters->nodes[0])) {
			return node->Parameters->nodes[1];
		}
		return node->Parameters->nodes[0];
	}
	return nullptr;
}

namespace {

// getSetAccessorTypeAnnotationNode — typeserializer.go:70. Get the type
// annotation for the value parameter. (@internal in Go)
Node* getSetAccessorTypeAnnotationNode(SetAccessorDeclaration* node) {
	Node* p = GetSetAccessorValueParameter(node);
	if (p != nullptr && p->type() != nullptr) {
		return p->type();
	}
	return nullptr;
}

// getAccessorTypeNode — typeserializer.go:78
Node* getAccessorTypeNode(Node* node, Node* container) {
	AllAccessorDeclarations accessors =
		getAllAccessorDeclarations(container->members(), node);
	if (accessors.setAccessor != nullptr) {
		return getSetAccessorTypeAnnotationNode(
			accessors.setAccessor->as<SetAccessorDeclaration>());
	}
	if (accessors.getAccessor != nullptr) {
		return accessors.getAccessor->as<GetAccessorDeclaration>()->Type;
	}
	return nullptr;
}

// getParametersOfDecoratedDeclaration — typeserializer.go:137
NodeList* getParametersOfDecoratedDeclaration(Node* node, Node* container) {
	if (container != nullptr && node->kind == Kind::GetAccessor) {
		AllAccessorDeclarations acc =
			getAllAccessorDeclarations(container->members(), node);
		if (acc.setAccessor != nullptr) {
			return acc.setAccessor->as<SetAccessorDeclaration>()->Parameters;
		}
	}
	return node->parameterList();
}

} // namespace

// newMetadataSerializer — typeserializer.go:26
metadataSerializer* newMetadataSerializer(
	checker::EmitResolver* resolver, printer::NodeFactory* f,
	printer::EmitContext* ec, ScriptTarget languageVersion,
	bool strictNullChecks) {
	auto* s = new metadataSerializer;
	s->resolver = resolver;
	s->languageVersion = languageVersion;
	s->f = f;
	s->ec = ec;
	s->strictNullChecks = strictNullChecks;
	return s;
}

// SerializeTypeOfNode — typeserializer.go:34
Node* metadataSerializer::SerializeTypeOfNode(metadataSerializerContext ctx,
											  Node* node, Node* container) {
	serializerContextGuard guard(this, ctx);
	return serializeTypeOfNode(node, container);
}

// SerializeParameterTypesOfNode — typeserializer.go:41
Node* metadataSerializer::SerializeParameterTypesOfNode(
	metadataSerializerContext ctx, Node* node, Node* container) {
	serializerContextGuard guard(this, ctx);
	return serializeParameterTypesOfNode(node, container);
}

// SerializeReturnTypeOfNode — typeserializer.go:48
Node* metadataSerializer::SerializeReturnTypeOfNode(
	metadataSerializerContext ctx, Node* node) {
	serializerContextGuard guard(this, ctx);
	return serializeReturnTypeOfNode(node);
}

// serializeTypeOfNode — typeserializer.go:93. Serializes the type of a node
// for use with decorator type metadata.
Node* metadataSerializer::serializeTypeOfNode(Node* node, Node* container) {
	switch (node->kind) {
	case Kind::PropertyDeclaration:
	case Kind::Parameter:
		return serializeTypeNode(node->type());
	case Kind::GetAccessor:
	case Kind::SetAccessor:
		return serializeTypeNode(getAccessorTypeNode(node, container));
	case Kind::ClassDeclaration:
	case Kind::ClassExpression:
	case Kind::MethodDeclaration:
		return f->newIdentifier("Function");
	default:
		return f->newVoidZeroExpression();
	}
}

// serializeParameterTypesOfNode — typeserializer.go:110. Serializes the type
// of a node for use with decorator type metadata.
Node* metadataSerializer::serializeParameterTypesOfNode(Node* node,
														Node* container) {
	Node* valueDeclaration = nullptr;
	if (isClassLike(node)) {
		valueDeclaration = getFirstConstructorWithBody(node);
	} else if (isFunctionLike(node) && nodeIsPresent(node->body())) {
		valueDeclaration = node;
	}

	if (valueDeclaration == nullptr) {
		return f->newArrayLiteralExpression(f->newNodeList({}), false);
	}

	std::vector<Node*> expressions;
	NodeList* parameters =
		getParametersOfDecoratedDeclaration(valueDeclaration, container);
	for (size_t i = 0; i < parameters->nodes.size(); i++) {
		Node* parameter = parameters->nodes[i];
		if (i == 0 && isIdentifier(parameter->name()) &&
			parameter->name()->text() == "this") {
			continue;
		}
		if (parameter->as<ParameterDeclaration>()->DotDotDotToken != nullptr) {
			expressions.push_back(serializeTypeNode(
				getRestParameterElementType(parameter->type())));
		} else {
			expressions.push_back(serializeTypeOfNode(parameter, container));
		}
	}
	return f->newArrayLiteralExpression(f->newNodeList(expressions), false);
}

// serializeReturnTypeOfNode — typeserializer.go:151. Serializes the return
// type of a node for use with decorator type metadata.
Node* metadataSerializer::serializeReturnTypeOfNode(Node* node) {
	if (isFunctionLike(node) && node->type() != nullptr) {
		return serializeTypeNode(node->type());
	} else if (isAsyncFunction(node)) {
		return f->newIdentifier("Promise");
	}
	return f->newVoidZeroExpression();
}

// serializeTypeNode — typeserializer.go:178. Serializes a type node for use
// with decorator type metadata.
//
// Types are serialized in the following fashion:
// - Void types point to "undefined" (e.g. "void 0")
// - Function and Constructor types point to the global "Function" constructor.
// - Interface types with a call or construct signature types point to the
//   global "Function" constructor.
// - Array and Tuple types point to the global "Array" constructor.
// - Type predicates and booleans point to the global "Boolean" constructor.
// - String literal types and strings point to the global "String"
//   constructor.
// - Enum and number types point to the global "Number" constructor.
// - Symbol types point to the global "Symbol" constructor.
// - Type references to classes (or class-like variables) point to the
//   constructor for the class.
// - Anything else points to the global "Object" constructor.
Node* metadataSerializer::serializeTypeNode(Node* node) {
	if (node == nullptr) {
		return f->newIdentifier("Object");
	}

	node = skipTypeParentheses(node);

	switch (node->kind) {
	case Kind::VoidKeyword:
	case Kind::UndefinedKeyword:
	case Kind::NeverKeyword:
		return f->newVoidZeroExpression();
	case Kind::FunctionType:
	case Kind::ConstructorType:
		return f->newIdentifier("Function");
	case Kind::ArrayType:
	case Kind::TupleType:
		return f->newIdentifier("Array");
	case Kind::TypePredicate:
		if (node->as<TypePredicateNode>()->AssertsModifier != nullptr) {
			return f->newVoidZeroExpression();
		}
		return f->newIdentifier("Boolean");
	case Kind::BooleanKeyword:
		return f->newIdentifier("Boolean");
	case Kind::TemplateLiteralType:
	case Kind::StringKeyword:
		return f->newIdentifier("String");
	case Kind::ObjectKeyword:
		return f->newIdentifier("Object");
	case Kind::LiteralType:
		return serializeLiteralOfLiteralTypeNode(
			node->as<LiteralTypeNode>()->Literal);
	case Kind::NumberKeyword:
		return f->newIdentifier("Number");
	case Kind::BigIntKeyword:
		return serializeBigIntConstructor();
	case Kind::SymbolKeyword:
		return f->newIdentifier("Symbol");
	case Kind::TypeReference:
		return serializeTypeReferenceNode(node->as<TypeReferenceNode>());
	case Kind::IntersectionType:
		return serializeUnionOrIntersectionConstituents(
			node->as<IntersectionTypeNode>()->Types->nodes, true);
	case Kind::UnionType:
		return serializeUnionOrIntersectionConstituents(
			node->as<UnionTypeNode>()->Types->nodes, false);
	case Kind::ConditionalType: {
		bool oldState = c.serializingConditionalTypeBranch;
		c.serializingConditionalTypeBranch = true;
		Node* result = serializeUnionOrIntersectionConstituents(
			{node->as<ConditionalTypeNode>()->TrueType,
			 node->as<ConditionalTypeNode>()->FalseType},
			false);
		c.serializingConditionalTypeBranch = oldState;
		return result;
	}
	case Kind::TypeOperator:
		if (node->as<TypeOperatorNode>()->Operator == Kind::ReadonlyKeyword) {
			return serializeTypeNode(node->type());
		}
		// TODO: why is `unique symbol` not handled as `Symbol`? This falls
		// back to `Object`
		break;
	case Kind::TypeQuery:
	case Kind::IndexedAccessType:
	case Kind::MappedType:
	case Kind::TypeLiteral:
	case Kind::AnyKeyword:
	case Kind::UnknownKeyword:
	case Kind::ThisType:
	case Kind::ImportType:
		// These types fall back to Object.
		break;

	// handle JSDoc types from an invalid parse
	case Kind::JSDocAllType:
	case Kind::JSDocVariadicType:
		// no meaningful serialization for these invalid-parse JSDoc types
		break;
	case Kind::JSDocNullableType:
	case Kind::JSDocNonNullableType:
	case Kind::JSDocOptionalType:
		return serializeTypeNode(node->type());
	default:
		TSC_UNREACHABLE("failBadSyntaxKind");
	}
	return f->newIdentifier("Object");
}

// serializeUnionOrIntersectionConstituents — typeserializer.go:242
Node* metadataSerializer::serializeUnionOrIntersectionConstituents(
	const std::vector<Node*>& types, bool isIntersection) {
	// Note when updating logic here also update
	// `getEntityNameForDecoratorMetadata` in checker.ts so that aliases can be
	// marked as referenced
	Node* serializedType = nullptr;
	for (Node* typeNode : types) {
		typeNode = skipTypeParentheses(typeNode);
		if (typeNode->kind == Kind::NeverKeyword) {
			if (isIntersection) {
				return f->newVoidZeroExpression(); // Reduce to `never` in an
												   // intersection
			}
			continue; // Elide `never` in a union
		}

		if (typeNode->kind == Kind::UnknownKeyword) {
			if (!isIntersection) {
				return f->newIdentifier(
					"Object"); // Reduce to `unknown` in a union
			}
			continue; // Elide `unknown` in an intersection
		}

		if (typeNode->kind == Kind::AnyKeyword) {
			return f->newIdentifier(
				"Object"); // Reduce to `any` in a union or intersection
		}

		if (!strictNullChecks &&
			((isLiteralTypeNode(typeNode) &&
			  typeNode->as<LiteralTypeNode>()->Literal->kind ==
				  Kind::NullKeyword) ||
			 typeNode->kind == Kind::UndefinedKeyword)) {
			continue; // Elide null and undefined from unions for metadata,
					  // just like what we did prior to the implementation of
					  // strict null checks
		}

		Node* serializedConstituent = serializeTypeNode(typeNode);
		if (isIdentifier(serializedConstituent) &&
			serializedConstituent->as<Identifier>()->Text == "Object") {
			// One of the individual is global object, return immediately
			return serializedConstituent;
		}

		// If there exists union that is not `void 0` expression, check if the
		// the common type is identifier. anything more complex and we will
		// just default to Object
		if (serializedType != nullptr) {
			// Different types
			if (!equateSerializedTypeNodes(serializedType,
										   serializedConstituent)) {
				return f->newIdentifier("Object");
			}
		} else {
			// Initialize the union type
			serializedType = serializedConstituent;
		}
	}

	// If we were able to find common type, use it
	if (serializedType != nullptr) {
		return serializedType;
	}
	return f->newVoidZeroExpression(); // Fallback is only hit if all union
									 // constituents are
									 // null/undefined/never
}

// serializeLiteralOfLiteralTypeNode — typeserializer.go:295
Node* metadataSerializer::serializeLiteralOfLiteralTypeNode(Node* node) {
	switch (node->kind) {
	case Kind::StringLiteral:
	case Kind::NoSubstitutionTemplateLiteral:
		return f->newIdentifier("String");
	case Kind::PrefixUnaryExpression: {
		Node* operand = node->as<PrefixUnaryExpression>()->Operand;
		switch (operand->kind) {
		case Kind::NumericLiteral:
		case Kind::BigIntLiteral:
			return serializeLiteralOfLiteralTypeNode(operand);
		default:
			TSC_UNREACHABLE("failBadSyntaxKind");
		}
		break;
	}
	case Kind::NumericLiteral:
		return f->newIdentifier("Number");
	case Kind::BigIntLiteral:
		return serializeBigIntConstructor();
	case Kind::TrueKeyword:
	case Kind::FalseKeyword:
		return f->newIdentifier("Boolean");
	case Kind::NullKeyword:
		return f->newVoidZeroExpression();
	default:
		TSC_UNREACHABLE("failBadSyntaxKind");
	}
	return nullptr;
}

// serializeTypeReferenceNode — typeserializer.go:326. Serializes a
// TypeReferenceNode to an appropriate JS constructor value for use with
// decorator type metadata.
Node* metadataSerializer::serializeTypeReferenceNode(TypeReferenceNode* node) {
	Node* serialScope = c.currentNameScope;
	if (serialScope == nullptr) {
		serialScope = c.currentLexicalScope;
	}
	printer::TypeReferenceSerializationKind kind =
		resolver->GetTypeReferenceSerializationKind(
			ec->parseNode(node->TypeName), ec->parseNode(serialScope));
	switch (kind) {
	case printer::TypeReferenceSerializationKind::Unknown: {
		// From conditional type type reference that cannot be resolved is
		// Similar to any or unknown
		if (c.serializingConditionalTypeBranch) {
			return f->newIdentifier("Object");
		}

		Node* serialized =
			serializeEntityNameAsExpressionFallback(node->TypeName);
		Node* temp = f->newTempVariable();
		ec->addVariableDeclaration(temp);
		return f->newConditionalExpression(
			f->newTypeCheck(f->newAssignmentExpression(temp, serialized),
							"function"),
			f->newToken(Kind::QuestionToken), temp,
			f->newToken(Kind::ColonToken), f->newIdentifier("Object"));
	}

	case printer::TypeReferenceSerializationKind::
		TypeWithConstructSignatureAndValue:
		return serializeEntityNameAsExpression(node->TypeName);

	case printer::TypeReferenceSerializationKind::VoidNullableOrNeverType:
		return f->newVoidZeroExpression();

	case printer::TypeReferenceSerializationKind::BigIntLikeType:
		return serializeBigIntConstructor();

	case printer::TypeReferenceSerializationKind::BooleanType:
		return f->newIdentifier("Boolean");

	case printer::TypeReferenceSerializationKind::NumberLikeType:
		return f->newIdentifier("Number");

	case printer::TypeReferenceSerializationKind::StringLikeType:
		return f->newIdentifier("String");

	case printer::TypeReferenceSerializationKind::ArrayLikeType:
		return f->newIdentifier("Array");

	case printer::TypeReferenceSerializationKind::ESSymbolType:
		return f->newIdentifier("Symbol");

	case printer::TypeReferenceSerializationKind::TypeWithCallSignature:
		return f->newIdentifier("Function");

	case printer::TypeReferenceSerializationKind::Promise:
		return f->newIdentifier("Promise");

	case printer::TypeReferenceSerializationKind::ObjectType:
		return f->newIdentifier("Object");
	default:
		TSC_UNREACHABLE("unknown type reference serialization kind");
	}
}

// serializeBigIntConstructor — typeserializer.go:388
Node* metadataSerializer::serializeBigIntConstructor() {
	if (languageVersion >= ScriptTarget::ES2020) {
		return f->newIdentifier("BigInt");
	}
	return f->newConditionalExpression(
		f->newTypeCheck(f->newIdentifier("BigInt"), "function"),
		f->newToken(Kind::QuestionToken), f->newIdentifier("BigInt"),
		f->newToken(Kind::ColonToken), f->newIdentifier("Object"));
}

// serializeEntityNameAsExpression — typeserializer.go:405. Serializes an
// entity name as an expression for decorator type metadata.
Node* metadataSerializer::serializeEntityNameAsExpression(Node* node) {
	switch (node->kind) {
	case Kind::Identifier: {
		// Create a clone of the name with a new parent, and treat it as if it
		// were a source tree node for the purposes of the checker.
		Node* name = node->clone(*f);
		name->loc = node->loc;
		ec->unsetOriginal(name); // make this identifier emulate a parse node,
								 // making it behave correctly when inspected
								 // by the module transforms
		name->parent = ec->parseNode(
			c.currentLexicalScope); // ensure the parent is set to a parse
									// tree node.
		return name;
	}
	case Kind::QualifiedName:
		return serializeQualifiedNameAsExpression(node->as<QualifiedName>());
	}
	return nullptr;
}

// serializeQualifiedNameAsExpression — typeserializer.go:425. Serializes a
// qualified name as an expression for decorator type metadata.
Node* metadataSerializer::serializeQualifiedNameAsExpression(
	QualifiedName* node) {
	return f->newPropertyAccessExpression(
		serializeEntityNameAsExpression(node->Left), nullptr, node->Right,
		NodeFlagsNone);
}

// serializeEntityNameAsExpressionFallback — typeserializer.go:433. Serializes
// an entity name which may not exist at runtime, but whose access shouldn't
// throw.
Node* metadataSerializer::serializeEntityNameAsExpressionFallback(Node* node) {
	if (node->kind == Kind::Identifier) {
		// A -> typeof A !== "undefined" && A
		Node* copied = serializeEntityNameAsExpression(node);
		return createCheckedValue(copied, copied);
	}
	if (node->as<QualifiedName>()->Left->kind == Kind::Identifier) {
		// A.B -> typeof A !== "undefined" && A.B
		return createCheckedValue(
			serializeEntityNameAsExpression(node->as<QualifiedName>()->Left),
			serializeEntityNameAsExpression(node));
	}
	// A.B.C -> typeof A !== "undefined" && (_a = A.B) !== void 0 && _a.C
	Node* left =
		serializeEntityNameAsExpressionFallback(node->as<QualifiedName>()->Left);
	Node* temp = f->newTempVariable();
	ec->addVariableDeclaration(temp);
	return f->newLogicalANDExpression(
		f->newLogicalANDExpression(
			left->as<BinaryExpression>()->Left,
			f->newStrictInequalityExpression(
				f->newAssignmentExpression(
					temp, left->as<BinaryExpression>()->Right),
				f->newVoidZeroExpression())),
		f->newPropertyAccessExpression(temp, nullptr,
									   node->as<QualifiedName>()->Right,
									   NodeFlagsNone));
}

// createCheckedValue — typeserializer.go:467. Produces an expression that
// results in `right` if `left` is not undefined at runtime:
//
//	typeof left !== "undefined" && right
//
// We use `typeof L !== "undefined"` (rather than `L !== undefined`) since `L`
// may not be declared. It's acceptable for this expression to result in
// `false` at runtime, as the result is intended to be further checked by any
// containing expression.
Node* metadataSerializer::createCheckedValue(Node* left, Node* right) {
	return f->newLogicalANDExpression(
		f->newStrictInequalityExpression(
			f->newTypeOfExpression(left),
			f->newStringLiteral("undefined", TokenFlagsNone)),
		right);
}

// equateSerializedTypeNodes — typeserializer.go:474
bool metadataSerializer::equateSerializedTypeNodes(Node* left, Node* right) {
	// temp vars used in fallback
	if (isGeneratedIdentifier(ec, left)) {
		return isGeneratedIdentifier(ec, right);
	}
	// entity names
	if (isIdentifier(left)) {
		return isIdentifier(right) && left->text() == right->text();
	}
	if (isPropertyAccessExpression(left)) {
		return isPropertyAccessExpression(right) &&
			   equateSerializedTypeNodes(left->expression(),
										 right->expression()) &&
			   equateSerializedTypeNodes(left->name(), right->name());
	}
	// `void 0`
	if (isVoidExpression(left)) {
		return isVoidExpression(right) &&
			   isNumericLiteral(left->expression()) &&
			   isNumericLiteral(right->expression()) &&
			   left->expression()->text() == "0" &&
			   right->expression()->text() == "0";
	}
	// `"undefined"` or `"function"` in `typeof` checks
	if (isStringLiteral(left)) {
		return isStringLiteral(right) && left->text() == right->text();
	}
	// used in `typeof` checks for fallback
	if (isTypeOfExpression(left)) {
		return isTypeOfExpression(right) &&
			   equateSerializedTypeNodes(left->expression(),
										 right->expression());
	}
	// parens in `typeof` checks with temps
	if (isParenthesizedExpression(left)) {
		return isParenthesizedExpression(right) &&
			   equateSerializedTypeNodes(left->expression(),
										 right->expression());
	}
	// conditionals used in fallback
	if (isConditionalExpression(left)) {
		return isConditionalExpression(right) &&
			   equateSerializedTypeNodes(
				   left->as<ConditionalExpression>()->Condition,
				   right->as<ConditionalExpression>()->Condition) &&
			   equateSerializedTypeNodes(
				   left->as<ConditionalExpression>()->WhenTrue,
				   right->as<ConditionalExpression>()->WhenTrue) &&
			   equateSerializedTypeNodes(
				   left->as<ConditionalExpression>()->WhenFalse,
				   right->as<ConditionalExpression>()->WhenFalse);
	}
	// logical binary and assignments used in fallback
	if (isBinaryExpression(left)) {
		return isBinaryExpression(right) &&
			   left->as<BinaryExpression>()->OperatorToken->kind ==
				   right->as<BinaryExpression>()->OperatorToken->kind &&
			   equateSerializedTypeNodes(left->as<BinaryExpression>()->Left,
										 right->as<BinaryExpression>()->Left) &&
			   equateSerializedTypeNodes(
				   left->as<BinaryExpression>()->Right,
				   right->as<BinaryExpression>()->Right);
	}
	return false;
}

} // namespace tsc::transformers::tstransforms
