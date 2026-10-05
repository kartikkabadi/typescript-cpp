// Port of tsc/internal/checker/checker.go:2622-3830 — the per-declaration and
// per-type-node check functions the checkSourceElement walker dispatches to:
// JSDoc types, type parameters, parameters, signatures, class members,
// constructors/accessors, type references/predicates, the small type-node
// checks (literal/array/tuple/union/intersection/this/operator/conditional/
// infer/template/import/named-tuple/indexed-access/mapped), function
// declarations, the overload-agreement machinery, and code-path analysis.
//
// Callees owned by slices that have not landed are declared in
// `// === slice: declchecks ===` in checker.h and stubbed once at the bottom
// of this file with `TSC_UNREACHABLE("<name> — <slice> dep")`; each stub is
// deleted here when its owner's real definition merges.

#include <algorithm>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/binder/binder.h"
#include "internal/checker/checker.h"
#include "internal/checker/mapper.h"
#include "internal/checker/types.h"
#include "internal/scanner/scanner.h"
#include "internal/tracing/tracing.h"

namespace tsc::checker {

// ---------------------------------------------------------------------------
// File-local helpers — faithful ports of free functions whose home files have
// not been ported yet (checker/utilities.go and free helpers inside
// checker.go). When the owning slice lands, these stay file-local and the
// owner's copy is either also file-local or exported from its home TU; either
// way there is no symbol collision.
// ---------------------------------------------------------------------------

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

// checker/utilities.go:1136 — isSuperCall
static bool isSuperCall(Node* n) {
	return isCallExpression(n) && n->expression()->kind == Kind::SuperKeyword;
}

// checker.go:2952 — isInstancePropertyWithInitializerOrPrivateIdentifierProperty
static bool isInstancePropertyWithInitializerOrPrivateIdentifierProperty(Node* n) {
	return isPrivateIdentifierClassElementDeclaration(n) ||
	       (isPropertyDeclaration(n) && !isStatic(n) && n->initializer() != nullptr);
}

// checker.go:2956 — superCallIsRootLevelInConstructor
static bool superCallIsRootLevelInConstructor(Node* superCall, Node* body) {
	Node* superCallParent = walkUpParenthesizedExpressions(superCall->parent);
	return isExpressionStatement(superCallParent) && superCallParent->parent == body;
}

// checker.go:2961 — nodeImmediatelyReferencesSuperOrThis
static bool nodeImmediatelyReferencesSuperOrThis(Node* node) {
	switch (node->kind) {
	case Kind::SuperKeyword:
	case Kind::ThisKeyword:
		return true;
	case Kind::ArrowFunction:
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
	case Kind::PropertyDeclaration:
		return false;
	case Kind::Block:
		switch (node->parent->kind) {
		case Kind::Constructor:
		case Kind::MethodDeclaration:
		case Kind::GetAccessor:
		case Kind::SetAccessor:
			return false;
		default:
			break;
		}
		break;
	default:
		break;
	}
	bool found = false;
	node->forEachChild([&found](Node* child) -> bool {
		if (nodeImmediatelyReferencesSuperOrThis(child)) {
			found = true;
			return true;
		}
		return false;
	});
	return found;
}

// checker/utilities.go:1298 — getEnclosingContainer
static Node* getEnclosingContainer(Node* node) {
	return findAncestor(node->parent, [](Node* n) -> bool {
		return getContainerFlags(n) & ContainerFlagsIsContainer;
	});
}

// checker/utilities.go:1304 — getDeclarationsOfKind
static std::vector<Node*> getDeclarationsOfKind(Symbol* symbol, Kind kind) {
	std::vector<Node*> result;
	for (Node* d : symbol->declarations) {
		if (d->kind == kind) {
			result.push_back(d);
		}
	}
	return result;
}

// checker/utilities.go:342 — isPrivateWithinAmbient
static bool isPrivateWithinAmbient(Node* node) {
	return (hasModifier(node, ModifierFlagsPrivate) ||
	        isPrivateIdentifierClassElementDeclaration(node)) &&
	       (node->flags & NodeFlagsAmbient) != 0;
}

// checker.go:17358 — signatureHasRestParameter (signatures-slice range; free fn)
static bool signatureHasRestParameter(Signature* sig) {
	return (sig->flags & SignatureFlagsHasRestParameter) != 0;
}

// core.ElementOrNil
template <class T>
static T* elementOrNil(const std::vector<T*>& v, size_t i) {
	return i < v.size() ? v[i] : nullptr;
}

// checker.go:23946 — isTupleType (typenodes range; free fn)
static bool isTupleType(Type* t) {
	return (t->objectFlags & ObjectFlagsReference) != 0 &&
	       (t->Target()->objectFlags & ObjectFlagsTuple) != 0;
}

// types.go:794 — Type.TargetTupleType
static TupleType* targetTupleType(Type* t) {
	return t->AsTypeReference()->target->AsTupleType();
}

// ---------------------------------------------------------------------------
// checker.go:2622 — checkJSDocType
// ---------------------------------------------------------------------------

void Checker::checkJSDocType(Node* node) {
	checkJSDocTypeIsInJsFile(node);
	node->forEachChild([this](Node* child) -> bool {
		checkSourceElement(child);
		return false;
	});
}

// checker.go:2627
void Checker::checkJSDocTypeIsInJsFile(Node* node) {
	if (!isInJSFile(node)) {
		if (isJSDocNonNullableType(node) || isJSDocNullableType(node)) {
			const char* token = isJSDocNonNullableType(node) ? "!" : "?";
			bool postfix = node->pos() == node->type()->pos();
			const DiagnosticMessage* message =
			    postfix
			        ? X_0_at_the_end_of_a_type_is_not_valid_TypeScript_syntax_Did_you_mean_to_write_1
			        : X_0_at_the_start_of_a_type_is_not_valid_TypeScript_syntax_Did_you_mean_to_write_1;
			Type* t = getTypeFromTypeNode(node->type());
			if (isJSDocNullableType(node) && t != neverType && t != voidType) {
				t = getNullableType(t, postfix ? TypeFlagsUndefined : TypeFlagsNullable);
			}
			grammarErrorOnNode(node, message, {token, TypeToString(t)});
		} else {
			grammarErrorOnNode(node, JSDoc_types_can_only_be_used_inside_documentation_comments);
		}
	}
}

// checker.go:2646
void Checker::checkTypeParameter(Node* node) {
	// Grammar Checking
	checkGrammarModifiers(node);
	auto* tpNode = node->as<TypeParameterDeclaration>();
	if (Node* expr = tpNode->Expression; expr != nullptr) {
		grammarErrorOnFirstToken(expr, Type_expected);
	}
	checkSourceElement(tpNode->Constraint);
	checkSourceElement(tpNode->DefaultType);
	Type* typeParameter = getDeclaredTypeOfTypeParameter(getSymbolOfDeclaration(node));
	// Resolve base constraint to reveal circularity errors
	getBaseConstraintOfType(typeParameter);
	if (getResolvedTypeParameterDefault(typeParameter) == circularConstraintType) {
		error(tpNode->DefaultType, Type_parameter_0_has_a_circular_default,
		      {TypeToString(typeParameter)});
	}
	Type* constraintType = getConstraintOfTypeParameter(typeParameter);
	Type* defaultType = getDefaultFromTypeParameter(typeParameter);
	if (constraintType != nullptr && defaultType != nullptr) {
		checkTypeAssignableTo(
		    defaultType,
		    getTypeWithThisArgument(
		        instantiateType(constraintType,
		                        newSimpleTypeMapper(typeParameter, defaultType)),
		        defaultType, false),
		    tpNode->DefaultType, Type_0_does_not_satisfy_the_constraint_1);
	}
	checkTypeNameIsReserved(node->name(), Type_parameter_name_cannot_be_0);
	checkNodeDeferred(node);
}

// checker.go:2670
void Checker::checkTypeParameterDeferred(Node* node) {
	if (isInterfaceDeclaration(node->parent) || isClassLike(node->parent) ||
	    isTypeOrJSTypeAliasDeclaration(node->parent)) {
		Type* typeParameter = getDeclaredTypeOfTypeParameter(getSymbolOfDeclaration(node));
		ModifierFlags modifiers =
		    getTypeParameterModifiers(typeParameter) & (ModifierFlagsIn | ModifierFlagsOut);
		if (modifiers != 0) {
			Symbol* symbol = getSymbolOfDeclaration(node->parent);
			if (isTypeOrJSTypeAliasDeclaration(node->parent) &&
			    (getDeclaredTypeOfSymbol(symbol)->objectFlags &
			     (ObjectFlagsAnonymous | ObjectFlagsMapped)) == 0) {
				error(node,
				      Variance_annotations_are_only_supported_in_type_aliases_for_object_function_constructor_and_mapped_types);
			} else if (modifiers == ModifierFlagsIn || modifiers == ModifierFlagsOut) {
				std::function<void()> pop;
				if (Tracer* tr = tracer; tr != nullptr) {
					pop = tr->Push(tracing::PhaseCheckTypes, "checkTypeParameterDeferred",
					               {{"parent", getDeclaredTypeOfSymbol(symbol)->id},
					                {"id", typeParameter->id}},
					               false);
				}
				Type* source = createMarkerType(
				    symbol, typeParameter,
				    modifiers == ModifierFlagsOut ? markerSubTypeForCheck
				                                  : markerSuperTypeForCheck);
				Type* target = createMarkerType(
				    symbol, typeParameter,
				    modifiers == ModifierFlagsOut ? markerSuperTypeForCheck
				                                  : markerSubTypeForCheck);
				Type* saveVarianceTypeParameter = typeParameter;
				varianceTypeParameter = typeParameter;
				checkTypeAssignableTo(
				    source, target, node,
				    Type_0_is_not_assignable_to_type_1_as_implied_by_variance_annotation);
				varianceTypeParameter = saveVarianceTypeParameter;
				if (pop) {
					pop();
				}
			}
		}
	}
}

// checker.go:2693
bool Checker::shouldCheckErasableSyntax(Node* node) {
	return tristateIsTrue(compilerOptions->ErasableSyntaxOnly) && !isInJSFile(node);
}

// checker.go:2697
void Checker::checkParameter(Node* node) {
	// Grammar checking
	// It is a SyntaxError if the Identifier "eval" or the Identifier "arguments" occurs as the
	// Identifier in a PropertySetParameterList of a PropertyAssignment that is contained in strict code
	// or if its FunctionBody is strict code(11.1.5).
	checkGrammarModifiers(node);
	checkVariableLikeDeclaration(node);
	Node* fn = getContainingFunction(node);
	std::string paramName;
	if (node->name() != nullptr && isIdentifier(node->name())) {
		paramName = node->name()->text();
	}
	if (hasSyntacticModifier(node, ModifierFlagsParameterPropertyModifier)) {
		if (shouldCheckErasableSyntax(node)) {
			error(node, This_syntax_is_not_allowed_when_erasableSyntaxOnly_is_enabled);
		}
		if (!(isConstructorDeclaration(fn) && nodeIsPresent(fn->body()))) {
			error(node, A_parameter_property_is_only_allowed_in_a_constructor_implementation);
		}
		if (isConstructorDeclaration(fn) && paramName == "constructor") {
			error(node->name(), X_constructor_cannot_be_used_as_a_parameter_property_name);
		}
	}
	if (node->initializer() == nullptr && isOptionalDeclaration(node) &&
	    isBindingPattern(node->name()) && fn->body() != nullptr) {
		error(node, A_binding_pattern_parameter_cannot_be_optional_in_an_implementation_signature);
	}
	if (paramName == "this" || paramName == "new") {
		auto params = fn->parameters();
		auto it = std::find(params.begin(), params.end(), node);
		if (it == params.end() || std::distance(params.begin(), it) != 0) {
			error(node, A_0_parameter_must_be_the_first_parameter, {paramName});
		}
		if (isConstructorDeclaration(fn) || isConstructSignatureDeclaration(fn) ||
		    isConstructorTypeNode(fn)) {
			error(node, A_constructor_cannot_have_a_this_parameter);
		}
		if (isArrowFunction(fn)) {
			error(node, An_arrow_function_cannot_have_a_this_parameter);
		}
		if (isAccessor(fn)) {
			error(node, X_get_and_set_accessors_cannot_declare_this_parameters);
		}
	}
	// Only check rest parameter type if it's not a binding pattern. Since binding patterns are
	// not allowed in a rest parameter, we already have an error from checkGrammarParameterList.
	if (hasDotDotDotToken(node) && !isBindingPattern(node->name()) &&
	    !isTypeAssignableTo(getReducedType(getTypeOfSymbol(node->symbol())),
	                        anyReadonlyArrayType)) {
		error(node, A_rest_parameter_must_be_of_an_array_type);
	}
}

// checker.go:2744
void Checker::checkPropertyDeclaration(Node* node) {
	// Grammar checking
	if (!checkGrammarModifiers(node) && !checkGrammarProperty(node)) {
		checkGrammarComputedPropertyName(node->name());
	}
	checkVariableLikeDeclaration(node);
	setNodeLinksForPrivateIdentifierScope(node);
	// property signatures already report "initializer not allowed in ambient context" elsewhere
	if (hasSyntacticModifier(node, ModifierFlagsAbstract) && isPropertyDeclaration(node)) {
		if (node->initializer() != nullptr) {
			error(node,
			      Property_0_cannot_have_an_initializer_because_it_is_marked_abstract,
			      {declarationNameToString(node->name())});
		}
	}
}

// checker.go:2759
void Checker::checkPropertySignature(Node* node) {
	if (isPrivateIdentifier(node->as<PropertySignatureDeclaration>()->name)) {
		error(node, Private_identifiers_are_not_allowed_outside_class_bodies);
	}
	checkPropertyDeclaration(node);
}

// checker.go:2766
void Checker::checkSignatureDeclaration(Node* node) {
	// Grammar checking
	switch (node->kind) {
	case Kind::IndexSignature:
		checkGrammarIndexSignature(node->as<IndexSignatureDeclaration>());
		break;
	case Kind::FunctionType:
	case Kind::FunctionDeclaration:
	case Kind::ConstructorType:
	case Kind::CallSignature:
	case Kind::Constructor:
	case Kind::ConstructSignature:
		checkGrammarFunctionLikeDeclaration(node);
		break;
	default:
		break;
	}
	FunctionFlags functionFlags = getFunctionFlags(node);
	if ((functionFlags & FunctionFlagsInvalid) == 0) {
		// Async generators prior to ES2018 require the __await and __asyncGenerator helpers
		if ((functionFlags & FunctionFlagsAsyncGenerator) == FunctionFlagsAsyncGenerator &&
		    languageVersion < LanguageFeatureMinimumTarget.AsyncGenerators) {
			checkExternalEmitHelpers(node, ExternalEmitHelpersAsyncGeneratorIncludes);
		}
		if ((functionFlags & FunctionFlagsAsyncGenerator) == FunctionFlagsAsync &&
		    languageVersion < LanguageFeatureMinimumTarget.AsyncFunctions) {
			checkExternalEmitHelpers(node, ExternalEmitHelpersAwaiter);
		}
	}
	checkTypeParameters(node->typeParameters());
	checkUnmatchedJSDocParameters(node);
	checkSourceElements(node->parameters());
	Node* returnTypeNode = node->type();
	if (returnTypeNode != nullptr) {
		checkSourceElement(returnTypeNode);
	}
	if (noImplicitAny && returnTypeNode == nullptr) {
		switch (node->kind) {
		case Kind::ConstructSignature:
			error(node,
			      Construct_signature_which_lacks_return_type_annotation_implicitly_has_an_any_return_type);
			break;
		case Kind::CallSignature:
			error(node,
			      Call_signature_which_lacks_return_type_annotation_implicitly_has_an_any_return_type);
			break;
		default:
			break;
		}
	}
	if (returnTypeNode != nullptr) {
		if ((functionFlags & (FunctionFlagsInvalid | FunctionFlagsGenerator)) ==
		    FunctionFlagsGenerator) {
			Type* returnType = getTypeFromTypeNode(returnTypeNode);
			if (returnType == voidType) {
				error(returnTypeNode, A_generator_cannot_have_a_void_type_annotation);
			} else {
				checkGeneratorInstantiationAssignabilityToReturnType(returnType, functionFlags,
				                                                   returnTypeNode);
			}
		} else if ((functionFlags & FunctionFlagsAsyncGenerator) == FunctionFlagsAsync) {
			checkAsyncFunctionReturnType(node, returnTypeNode);
		}
	}
	if (!isIndexSignatureDeclaration(node)) {
		registerForUnusedIdentifiersCheck(node);
	}
}

// Checks the return type of an async function to ensure it is a compatible
// Promise implementation.
//
// checker.go:2825
void Checker::checkAsyncFunctionReturnType(Node* node, Node* returnTypeNode) {
	Type* returnType = getTypeFromTypeNode(returnTypeNode);
	if (isErrorType(returnType)) {
		return;
	}
	Type* globalPromiseType = getGlobalPromiseTypeChecked();
	if (globalPromiseType != emptyGenericType &&
	    !isReferenceToType(returnType, globalPromiseType)) {
		// The promise type was not a valid type reference to the global promise type, so we
		// report an error and return the unknown type.
		error(returnTypeNode,
		      The_return_type_of_an_async_function_or_method_must_be_the_global_Promise_T_type_Did_you_mean_to_write_Promise_0,
		      {TypeToString(getAwaitedTypeNoAlias(returnType) != nullptr
		                        ? getAwaitedTypeNoAlias(returnType)
		                        : voidType)});
		return;
	}
	checkAwaitedType(returnType, false /*withAlias*/, node,
	                 The_return_type_of_an_async_function_must_either_be_a_valid_promise_or_must_not_contain_a_callable_then_member);
}

// checker.go:2840
void Checker::checkMethodDeclaration(Node* node) {
	// Grammar checking
	if (!checkGrammarMethod(node)) {
		checkGrammarComputedPropertyName(node->name());
		if (isMethodDeclaration(node) &&
		    node->as<MethodDeclaration>()->AsteriskToken != nullptr &&
		    isIdentifier(node->name()) && node->name()->text() == "constructor") {
			error(node->name(), Class_constructor_may_not_be_a_generator);
		}
	}
	// Grammar checking for modifiers is done inside the function checkGrammarFunctionLikeDeclaration
	checkFunctionOrMethodDeclaration(node);
	// method signatures already report "implementation not allowed in ambient context" elsewhere
	if (hasSyntacticModifier(node, ModifierFlagsAbstract) && isMethodDeclaration(node) &&
	    node->body() != nullptr) {
		error(node, Method_0_cannot_have_an_implementation_because_it_is_marked_abstract,
		      {declarationNameToString(node->name())});
	}
	// Private named methods are only allowed in class declarations
	if (isPrivateIdentifier(node->name()) && getContainingClass(node) == nullptr) {
		error(node, Private_identifiers_are_not_allowed_outside_class_bodies);
	}
	setNodeLinksForPrivateIdentifierScope(node);
}

// checker.go:2861
void Checker::checkClassStaticBlockDeclaration(Node* node) {
	// Grammar checking
	checkGrammarModifiers(node);
	node->forEachChild([this](Node* child) -> bool {
		checkSourceElement(child);
		return false;
	});
	SymbolTable* locals = node->locals();
	if (locals != nullptr && !locals->empty()) {
		registerForUnusedIdentifiersCheck(node);
	}
}

// checker.go:2870
void Checker::checkConstructorDeclaration(Node* node) {
	// Grammar check on signature of constructor and modifier of the constructor is done in checkSignatureDeclaration function.
	checkSignatureDeclaration(node);
	// Grammar check for checking only related to constructorDeclaration
	auto* ctor = node->as<ConstructorDeclaration>();
	if (!checkGrammarConstructorTypeParameters(ctor)) {
		checkGrammarConstructorTypeAnnotation(ctor);
	}
	checkSourceElement(node->body());
	Symbol* symbol = getSymbolOfDeclaration(node);
	checkFunctionOrConstructorSymbol(symbol);
	// exit early in the case of signature - super checks are not relevant to them
	if (nodeIsMissing(node->body())) {
		return;
	}
	// TS 1.0 spec (April 2014): 8.3.2
	// Constructors of classes with no extends clause may not contain super calls, whereas
	// constructors of derived classes must contain at least one super call somewhere in their function body.
	Node* containingClassDecl = node->parent;
	if (getClassExtendsHeritageElement(containingClassDecl) == nullptr) {
		return;
	}
	bool classExtendsNull = classDeclarationExtendsNull(containingClassDecl);
	Node* superCall = findFirstSuperCall(node->body());
	if (superCall != nullptr) {
		if (classExtendsNull) {
			error(superCall,
			      A_constructor_cannot_contain_a_super_call_when_its_class_extends_null);
		}
		// A super call must be root-level in a constructor if both of the following are true:
		// - The containing class is a derived class.
		// - The constructor declares parameter properties
		//   or the containing class declares instance member variables with initializers.
		bool hasInstanceMemberWithInitializer = false;
		for (Node* m : node->parent->members()) {
			if (isInstancePropertyWithInitializerOrPrivateIdentifierProperty(m)) {
				hasInstanceMemberWithInitializer = true;
				break;
			}
		}
		bool hasParameterProperty = false;
		for (Node* p : node->parameters()) {
			if (hasSyntacticModifier(p, ModifierFlagsParameterPropertyModifier)) {
				hasParameterProperty = true;
				break;
			}
		}
		bool superCallShouldBeRootLevel =
		    !emitStandardClassFields && (hasInstanceMemberWithInitializer || hasParameterProperty);
		if (superCallShouldBeRootLevel) {
			// Until we have better flow analysis, it is an error to place the super call within any kind of block or conditional
			// See GH #8277
			if (!superCallIsRootLevelInConstructor(superCall, node->body())) {
				error(superCall,
				      A_super_call_must_be_a_root_level_statement_within_a_constructor_of_a_derived_class_that_contains_initialized_properties_parameter_properties_or_private_identifiers);
			} else {
				Node* superCallStatement = nullptr;
				for (Node* statement : node->body()->statements()) {
					if (isExpressionStatement(statement) &&
					    isSuperCall(skipOuterExpressions(statement->expression(), OEKAll))) {
						superCallStatement = statement;
						break;
					}
					if (nodeImmediatelyReferencesSuperOrThis(statement)) {
						break;
					}
				}
				// Until we have better flow analysis, it is an error to place the super call within any kind of block or conditional
				// See GH #8277
				if (superCallStatement == nullptr) {
					error(node,
					      A_super_call_must_be_the_first_statement_in_the_constructor_to_refer_to_super_or_this_when_a_derived_class_contains_initialized_properties_parameter_properties_or_private_identifiers);
				}
			}
		}
	} else if (!classExtendsNull) {
		error(node, Constructors_for_derived_classes_must_contain_a_super_call);
	}
}

// checker.go:2935
Node* Checker::findFirstSuperCall(Node* node) {
	Node* superCall = nullptr;
	std::function<bool(Node*)> visit = [&](Node* n) -> bool {
		if (isSuperCall(n)) {
			superCall = n;
			return true;
		}
		if (isFunctionLike(n)) {
			return false;
		}
		bool found = false;
		n->forEachChild([&](Node* child) -> bool {
			if (visit(child)) {
				found = true;
				return true;
			}
			return false;
		});
		return found;
	};
	visit(node);
	return superCall;
}

// checker.go:2976
void Checker::checkAccessorDeclaration(Node* node) {
	// Grammar checking accessors
	if (!checkGrammarFunctionLikeDeclaration(node) && !checkGrammarAccessor(node)) {
		checkGrammarComputedPropertyName(node->name());
	}
	Node* name = node->name();
	if (isIdentifier(name) && name->text() == "constructor" && isClassLike(node->parent)) {
		error(node->name(), Class_constructor_may_not_be_an_accessor);
	}
	checkDecorators(node);
	checkSignatureDeclaration(node);
	if (isGetAccessorDeclaration(node)) {
		if ((node->flags & NodeFlagsAmbient) == 0 && nodeIsPresent(node->body()) &&
		    (node->flags & NodeFlagsHasImplicitReturn) != 0) {
			if ((node->flags & NodeFlagsHasExplicitReturn) == 0) {
				error(name, A_get_accessor_must_return_a_value);
			}
		}
	}
	// Do not use hasDynamicName here, because that returns false for well known symbols.
	// We want to perform checkComputedPropertyName for all computed properties, including
	// well known symbols.
	if (isComputedPropertyName(name)) {
		checkComputedPropertyName(name);
	}
	if (hasBindableName(node)) {
		// TypeScript 1.0 spec (April 2014): 8.4.3
		// Accessors for the same member name must specify the same accessibility.
		Symbol* symbol = getSymbolOfDeclaration(node);
		Node* getter = getDeclarationOfKind(symbol, Kind::GetAccessor);
		Node* setter = getDeclarationOfKind(symbol, Kind::SetAccessor);
		if (getter != nullptr && setter != nullptr &&
		    (nodeLinks.Get(getter)->flags & NodeCheckFlagsTypeChecked) == 0) {
			nodeLinks.Get(getter)->flags |= NodeCheckFlagsTypeChecked;
			ModifierFlags getterFlags = getter->modifierFlags();
			ModifierFlags setterFlags = setter->modifierFlags();
			if ((getterFlags & ModifierFlagsAbstract) != (setterFlags & ModifierFlagsAbstract)) {
				error(getter->name(), Accessors_must_both_be_abstract_or_non_abstract);
				error(setter->name(), Accessors_must_both_be_abstract_or_non_abstract);
			}
			if (((getterFlags & ModifierFlagsProtected) != 0 &&
			     (setterFlags & (ModifierFlagsProtected | ModifierFlagsPrivate)) == 0) ||
			    ((getterFlags & ModifierFlagsPrivate) != 0 &&
			     (setterFlags & ModifierFlagsPrivate) == 0)) {
				error(getter->name(), A_get_accessor_must_be_at_least_as_accessible_as_the_setter);
				error(setter->name(), A_get_accessor_must_be_at_least_as_accessible_as_the_setter);
			}
		}
	}
	Type* returnType = getTypeOfAccessors(getSymbolOfDeclaration(node));
	if (node->kind == Kind::GetAccessor) {
		checkAllCodePathsInNonVoidFunctionReturnOrThrow(node, returnType);
	}
	checkSourceElement(node->body());
	setNodeLinksForPrivateIdentifierScope(node);
}

// checker.go:3028
void Checker::checkTypeReferenceNode(Node* node) {
	checkGrammarTypeArguments(node, node->typeArgumentList());
	if (isTypeReferenceNode(node) && (node->flags & NodeFlagsJSDoc) == 0) {
		auto* data = node->as<TypeReferenceNode>();
		if (data->TypeArguments != nullptr && data->TypeName->end() != data->TypeArguments->pos()) {
			// If there was a token between the type name and the type arguments, check if it was a DotToken
			SourceFile* sourceFile = getSourceFileOfNode(node);
			if (scanTokenAtPosition(sourceFile, data->TypeName->end()) == Kind::DotToken) {
				grammarErrorAtPos(node,
				                  skipTrivia(sourceFile->text, data->TypeName->end()), 1,
				                  JSDoc_types_can_only_be_used_inside_documentation_comments);
			}
		}
	}
	checkSourceElements(node->typeArguments());
	if (!(isConstTypeReference(node) && isAssertionExpression(node->parent))) {
		checkTypeReferenceOrImport(node);
	}
}

// checker.go:3046
void Checker::checkTypeReferenceOrImport(Node* node) {
	Type* t = getTypeFromTypeNode(node);
	if (!isErrorType(t)) {
		if (!node->typeArguments().empty()) {
			std::vector<Type*> typeParameters =
			    getTypeParametersForTypeReferenceOrImport(node);
			if (!typeParameters.empty()) {
				checkTypeArgumentConstraints(node, typeParameters);
			}
		}
		Symbol* symbol = getResolvedSymbolOrNil(node);
		if (symbol != nullptr) {
			bool anyDeprecated = false;
			for (Node* d : symbol->declarations) {
				if (isTypeDeclaration(d) && IsDeprecatedDeclaration(d)) {
					anyDeprecated = true;
					break;
				}
			}
			if (anyDeprecated) {
				addDeprecatedSuggestion(getDeprecatedSuggestionNode(node), symbol->declarations,
				                        symbol->name);
			}
		}
	}
}

// checker.go:3064
bool Checker::checkTypeArgumentConstraints(Node* node, std::vector<Type*> typeParameters) {
	std::vector<Type*> typeArguments;
	TypeMapper* mapper = nullptr;
	bool result = true;
	size_t i = 0;
	for (Type* typeParameter : typeParameters) {
		Type* constraint = getConstraintOfTypeParameter(typeParameter);
		if (constraint != nullptr) {
			if (typeArguments.empty()) {
				typeArguments = getEffectiveTypeArguments(node, typeParameters);
				mapper = newTypeMapper(typeParameters, typeArguments);
			}
			Node* errorNode =
			    elementOrNil(node->typeArguments(), i);
			result = result && checkTypeAssignableTo(typeArguments[i],
			                                         instantiateType(constraint, mapper),
			                                         errorNode,
			                                         Type_0_does_not_satisfy_the_constraint_1);
		}
		i++;
	}
	return result;
}

// checker.go:3081
Node* Checker::getDeprecatedSuggestionNode(Node* node) {
	node = skipParentheses(node);
	switch (node->kind) {
	case Kind::CallExpression:
	case Kind::Decorator:
	case Kind::NewExpression:
		return getDeprecatedSuggestionNode(node->expression());
	case Kind::TaggedTemplateExpression:
		return getDeprecatedSuggestionNode(node->as<TaggedTemplateExpression>()->Tag);
	case Kind::JsxOpeningElement:
	case Kind::JsxSelfClosingElement:
		return getDeprecatedSuggestionNode(node->tagName());
	case Kind::ElementAccessExpression:
		return node->as<ElementAccessExpression>()->ArgumentExpression;
	case Kind::PropertyAccessExpression:
		return node->name();
	case Kind::TypeReference: {
		Node* typeName = node->as<TypeReferenceNode>()->TypeName;
		if (isQualifiedName(typeName)) {
			return typeName->as<QualifiedName>()->Right;
		}
		break;
	}
	default:
		break;
	}
	return node;
}

// checker.go:3103
void Checker::checkTypePredicate(Node* node) {
	// Always check the predicate's type so nested type errors are reported even when the
	// predicate is in an invalid position, keeping diagnostics stable.
	checkSourceElement(node->type());
	Node* parent = getTypePredicateParent(node);
	if (parent == nullptr) {
		// The parent must not be valid.
		error(node,
		      A_type_predicate_is_only_allowed_in_return_type_position_for_functions_and_methods);
		return;
	}
	Signature* signature = getSignatureFromDeclaration(parent);
	TypePredicate* typePredicate = getTypePredicateOfSignature(signature);
	if (typePredicate == nullptr) {
		return;
	}
	Node* parameterName = node->as<TypePredicateNode>()->ParameterName;
	if (typePredicate->kind != TypePredicateKind::This &&
	    typePredicate->kind != TypePredicateKind::AssertsThis) {
		if (typePredicate->parameterIndex >= 0) {
			if (signatureHasRestParameter(signature) &&
			    typePredicate->parameterIndex ==
			        static_cast<int32_t>(signature->parameters.size()) - 1) {
				error(parameterName, A_type_predicate_cannot_reference_a_rest_parameter);
			} else {
				if (typePredicate->t != nullptr) {
					std::vector<Diagnostic*> diags;
					if (!checkTypeAssignableToEx(
					        typePredicate->t,
					        getTypeOfSymbol(
					            signature->parameters[typePredicate->parameterIndex]),
					        node->type(), nullptr /*headMessage*/, &diags)) {
						addDiagnostic(newDiagnosticChain(
						    diags[0],
						    A_type_predicate_s_type_must_be_assignable_to_its_parameter_s_type));
					}
				}
			}
		} else if (parameterName != nullptr) {
			bool hasReportedError = false;
			for (Node* param : parent->parameters()) {
				Node* paramName = param->name();
				if (isBindingPattern(paramName) &&
				    checkIfTypePredicateVariableIsDeclaredInBindingPattern(
				        paramName, parameterName, typePredicate->parameterName)) {
					hasReportedError = true;
					break;
				}
			}
			if (!hasReportedError) {
				error(parameterName, Cannot_find_parameter_0, {typePredicate->parameterName});
			}
		}
	}
}

// checker.go:3147
Node* Checker::getTypePredicateParent(Node* node) {
	Node* parent = node->parent;
	switch (parent->kind) {
	case Kind::ArrowFunction:
	case Kind::CallSignature:
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
	case Kind::FunctionType:
	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
		if (node == parent->type()) {
			return parent;
		}
		break;
	default:
		break;
	}
	return nullptr;
}

// checker.go:3159
bool Checker::checkIfTypePredicateVariableIsDeclaredInBindingPattern(
    Node* pattern, Node* predicateVariableNode, const std::string& predicateVariableName) {
	for (Node* element : pattern->elements()) {
		Node* name = element->name();
		if (name == nullptr) {
			continue;
		}
		if (isIdentifier(name) && name->text() == predicateVariableName) {
			error(predicateVariableNode,
			      A_type_predicate_cannot_reference_element_0_in_a_binding_pattern,
			      {predicateVariableName});
			return true;
		}
		if (isArrayBindingPattern(name) || isObjectBindingPattern(name)) {
			if (checkIfTypePredicateVariableIsDeclaredInBindingPattern(
			        name, predicateVariableNode, predicateVariableName)) {
				return true;
			}
		}
	}
	return false;
}

// checker.go:3178
void Checker::checkTypeQuery(Node* node) {
	getTypeFromTypeQueryNode(node);
}

// checker.go:3182
void Checker::checkTypeLiteral(Node* node) {
	checkSourceElements(node->members());
	Type* t = getTypeFromTypeLiteralOrFunctionOrConstructorTypeNode(node);
	checkIndexConstraints(t, t->symbol, false /*isStaticIndex*/);
	checkTypeForDuplicateIndexSignatures(node);
	checkObjectTypeForDuplicateDeclarations(node, false /*checkPrivateNames*/);
}

// checker.go:3190
void Checker::checkObjectTypeForDuplicateDeclarations(Node* node, bool checkPrivateNames) {
	std::unordered_map<std::string, int> instanceNames;
	std::unordered_map<std::string, int> staticNames;
	std::unordered_map<std::string, int> privateNames;
	bool nodeInAmbientContext = (node->flags & NodeFlagsAmbient) != 0;
	auto checkPropertyOrAccessor = [&](Symbol* symbol, int kind, bool isStaticMember) {
		if (symbol->declarations.size() > 1) {
			std::unordered_map<std::string, int>* names;
			if (isStaticMember) {
				names = &staticNames;
			} else {
				names = &instanceNames;
			}
			int state = 0;
			auto it = names->find(symbol->name);
			if (it != names->end()) {
				state = it->second;
			}
			if (state == 0) {
				// On first occurrence just record the kind
				(*names)[symbol->name] = kind;
			} else if (state == 1 || (state == 2 && kind != 2)) {
				// Error on second property or combination of property and accessor
				reportDuplicateMemberErrors(node, symbol->name, true, isStaticMember,
				                            Duplicate_identifier_0);
				// Record that errors have been reported
				(*names)[symbol->name] = 3;
			}
		}
	};
	for (Node* member : node->members()) {
		if (isConstructorDeclaration(member)) {
			for (Node* param : member->parameters()) {
				if (isParameterPropertyDeclaration(param, member) &&
				    !isBindingPattern(param->name())) {
					checkPropertyOrAccessor(getSymbolOfDeclaration(param), 1, false /*isStatic*/);
				}
			}
		} else {
			Symbol* symbol = getSymbolOfDeclaration(member);
			bool isStatic = hasStaticModifier(member);
			// In non-ambient contexts, check that static members are not named 'prototype'.
			if (!nodeInAmbientContext && isStatic && symbol != nullptr &&
			    symbol->name == "prototype") {
				error(member->name(),
				      Static_property_0_conflicts_with_built_in_property_Function_0_of_constructor_function_1,
				      {symbol->name, symbolToString(getSymbolOfDeclaration(node))});
			}
			// Check that this object type declaration doesn't contain multiple declarations of the same property,
			// or accessor and property declarations with the same name.
			if ((isPropertyDeclaration(member) && !hasAccessorModifier(member)) ||
			    isPropertySignatureDeclaration(member)) {
				checkPropertyOrAccessor(symbol, 1, isStatic);
			} else if (isAccessor(member) ||
			           (isPropertyDeclaration(member) && hasAccessorModifier(member))) {
				checkPropertyOrAccessor(symbol, 2, isStatic);
			}
			// Check that each private identifier is used only for instance members or only for static members. It is an
			// error for an instance and a static member to have the same private identifier.
			if (checkPrivateNames && member->name() != nullptr &&
			    isPrivateIdentifier(member->name())) {
				int flags = 0;
				auto it = privateNames.find(symbol->name);
				if (it != privateNames.end()) {
					flags = it->second;
				}
				if (flags != 3) {
					flags |= tsc::isStatic(member) ? 2 : 1;
					privateNames[symbol->name] = flags;
					if (flags == 3) {
						reportDuplicateMemberErrors(
						    node, symbol->name, false, false,
						    Duplicate_identifier_0_Static_and_instance_elements_cannot_share_the_same_private_name);
					}
				}
			}
		}
	}
}

// checker.go:3261
void Checker::reportDuplicateMemberErrors(Node* node, const std::string& name,
                                          bool checkStatic, bool isStaticMember,
                                          const DiagnosticMessage* message) {
	for (Node* member : node->members()) {
		if (isConstructorDeclaration(member)) {
			for (Node* param : member->parameters()) {
				if (isParameterPropertyDeclaration(param, member) &&
				    !isBindingPattern(param->name())) {
					if (Symbol* symbol = getSymbolOfDeclaration(param);
					    symbol->name == name) {
						error(param->name(), message, {symbolToString(symbol)});
					}
				}
			}
		} else if (Symbol* symbol = getSymbolOfDeclaration(member);
		           symbol != nullptr && symbol->name == name &&
		           (!checkStatic || isStaticMember == isStatic(member))) {
			error(member->name(), message, {symbolToString(symbol)});
		}
	}
}

// checker.go:3277
void Checker::checkArrayType(Node* node) {
	checkSourceElement(node->as<ArrayTypeNode>()->ElementType);
}

// checker.go:3281
void Checker::checkTupleType(Node* node) {
	bool seenOptionalElement = false;
	bool seenRestElement = false;
	std::vector<Node*> elements = node->elements();
	for (Node* e : elements) {
		ElementFlags flags = getTupleElementFlags(e);
		if ((flags & ElementFlagsVariadic) != 0) {
			Type* t = getTypeFromTypeNode(e->type());
			if (!isArrayLikeType(t)) {
				error(e, A_rest_element_type_must_be_an_array_type);
				break;
			}
			if (isArrayType(t) ||
			    (isTupleType(t) &&
			     (targetTupleType(t)->combinedFlags & ElementFlagsRest) != 0)) {
				flags |= ElementFlagsRest;
			}
		}
		if ((flags & ElementFlagsRest) != 0) {
			if (seenRestElement) {
				grammarErrorOnNode(e, A_rest_element_cannot_follow_another_rest_element);
				break;
			}
			seenRestElement = true;
		} else if ((flags & ElementFlagsOptional) != 0) {
			if (seenRestElement) {
				grammarErrorOnNode(e, An_optional_element_cannot_follow_a_rest_element);
				break;
			}
			seenOptionalElement = true;
		} else if ((flags & ElementFlagsRequired) != 0 && seenOptionalElement) {
			grammarErrorOnNode(e, A_required_element_cannot_follow_an_optional_element);
			break;
		}
	}
	checkSourceElements(elements);
	getTypeFromTypeNode(node);
}

// checker.go:3318
void Checker::checkUnionOrIntersectionType(Node* node) {
	node->forEachChild([this](Node* child) -> bool {
		checkSourceElement(child);
		return false;
	});
	getTypeFromTypeNode(node);
}

// checker.go:3323
void Checker::checkThisType(Node* node) {
	getTypeFromThisTypeNode(node);
}

// checker.go:3327
void Checker::checkTypeOperator(Node* node) {
	checkGrammarTypeOperatorNode(node->as<TypeOperatorNode>());
	checkSourceElement(node->type());
}

// checker.go:3332
void Checker::checkConditionalType(Node* node) {
	node->forEachChild([this](Node* child) -> bool {
		checkSourceElement(child);
		return false;
	});
}

// checker.go:3336
void Checker::checkInferType(Node* node) {
	if (findAncestor(node, [](Node* n) -> bool {
		    return n->parent != nullptr && n->parent->kind == Kind::ConditionalType &&
		           n->parent->as<ConditionalTypeNode>()->ExtendsType == n;
	    }) == nullptr) {
		grammarErrorOnNode(
		    node,
		    X_infer_declarations_are_only_permitted_in_the_extends_clause_of_a_conditional_type);
	}
	Node* typeParameterDeclarationNode = node->as<InferTypeNode>()->TypeParameter;
	checkSourceElement(typeParameterDeclarationNode);
	Symbol* symbol = getSymbolOfDeclaration(typeParameterDeclarationNode);
	if (symbol->declarations.size() > 1) {
		DeclaredTypeLinks* links = declaredTypeLinks.Get(symbol);
		if (!links->typeParametersChecked) {
			links->typeParametersChecked = true;
			Type* typeParameter = getDeclaredTypeOfTypeParameter(symbol);
			std::vector<Node*> declarations =
			    getDeclarationsOfKind(symbol, Kind::TypeParameter);
			if (!areTypeParametersIdentical(
			        declarations, {typeParameter},
			        [](Node* decl) -> std::vector<Node*> { return {decl}; })) {
				// Report an error on every conflicting declaration.
				std::string name = symbolToString(symbol);
				for (Node* declaration : declarations) {
					error(declaration->name(),
					      All_declarations_of_0_must_have_identical_constraints, {name});
				}
			}
		}
	}
	registerForUnusedIdentifiersCheck(node);
}

// checker.go:3363
void Checker::checkTemplateLiteralType(Node* node) {
	for (Node* span : node->as<TemplateLiteralTypeNode>()->TemplateSpans->nodes) {
		checkSourceElement(span->type());
		Type* t = getTypeFromTypeNode(span->type());
		checkTypeAssignableTo(t, templateConstraintType, span->type(), nullptr);
	}
	getTypeFromTypeNode(node);
}

// checker.go:3372
void Checker::checkImportType(Node* node) {
	auto* importTypeNode = node->as<ImportTypeNode>();
	checkSourceElement(importTypeNode->Argument);
	if (importTypeNode->Attributes != nullptr) {
		checkGrammarImportAttributeValues(importTypeNode->Attributes->as<ImportAttributes>());
		getResolutionModeOverride(importTypeNode->Attributes, true /*reportErrors*/);
	}
	checkTypeReferenceOrImport(node);
	checkImportAttributes(node);
}

// checker.go:3383
ResolutionMode Checker::getResolutionModeOverride(Node* node, bool reportErrors) {
	std::function<bool(Node*, const DiagnosticMessage*)> grammarErrorOnNode;
	if (reportErrors) {
		grammarErrorOnNode = [this](Node* n, const DiagnosticMessage* message) -> bool {
			return this->grammarErrorOnNode(n, message);
		};
	}
	return tsc::getResolutionModeOverride(node, grammarErrorOnNode).first;
}

// checker.go:3392
void Checker::checkNamedTupleMember(Node* node) {
	auto* tupleMember = node->as<NamedTupleMember>();
	if (tupleMember->DotDotDotToken != nullptr && tupleMember->QuestionToken != nullptr) {
		grammarErrorOnNode(node, A_tuple_member_cannot_be_both_optional_and_rest);
	}
	if (tupleMember->Type->kind == Kind::OptionalType) {
		grammarErrorOnNode(
		    tupleMember->Type,
		    A_labeled_tuple_element_is_declared_as_optional_with_a_question_mark_after_the_name_and_before_the_colon_rather_than_after_the_type);
	}
	if (tupleMember->Type->kind == Kind::RestType) {
		grammarErrorOnNode(
		    tupleMember->Type,
		    A_labeled_tuple_element_is_declared_as_rest_with_a_before_the_name_rather_than_before_the_type);
	}
	checkSourceElement(node->type());
	getTypeFromTypeNode(node);
}

// checker.go:3407
void Checker::checkIndexedAccessType(Node* node) {
	node->forEachChild([this](Node* child) -> bool {
		checkSourceElement(child);
		return false;
	});
	checkIndexedAccessIndexType(getTypeFromIndexedAccessTypeNode(node), node);
}

// checker.go:3412
void Checker::checkMappedType(Node* node) {
	auto* mappedTypeNode = node->as<MappedTypeNode>();
	checkGrammarMappedType(mappedTypeNode);
	checkSourceElement(mappedTypeNode->TypeParameter);
	checkSourceElement(mappedTypeNode->NameType);
	checkSourceElement(mappedTypeNode->Type);
	if (mappedTypeNode->Type == nullptr) {
		reportImplicitAny(node, anyType, WideningKind::Normal);
	}
	Type* t = getTypeFromMappedTypeNode(node);
	Type* nameType = getNameTypeFromMappedType(t);
	if (nameType != nullptr) {
		checkTypeAssignableTo(nameType, stringNumberSymbolType, mappedTypeNode->NameType,
		                      nullptr);
	} else {
		Type* constraintType = getConstraintTypeFromMappedType(t);
		checkTypeAssignableTo(
		    constraintType, stringNumberSymbolType,
		    mappedTypeNode->TypeParameter->as<TypeParameterDeclaration>()->Constraint,
		    nullptr);
	}
}

// checker.go:3431
void Checker::checkFunctionDeclaration(Node* node) {
	checkFunctionOrMethodDeclaration(node);
	checkGrammarForGenerator(node);
	checkCollisionsForDeclarationName(node, node->name());
}

// checker.go:3437
void Checker::checkFunctionOrMethodDeclaration(Node* node) {
	checkDecorators(node);
	checkSignatureDeclaration(node);
	FunctionFlags functionFlags = getFunctionFlags(node);
	// Do not use hasDynamicName here, because that returns false for well known symbols.
	// We want to perform checkComputedPropertyName for all computed properties, including
	// well known symbols.
	if (node->name() != nullptr && isComputedPropertyName(node->name())) {
		// This check will account for methods in class/interface declarations,
		// as well as accessors in classes/object literals
		checkComputedPropertyName(node->name());
	}
	if (hasBindableName(node)) {
		// first we want to check the local symbol that contain this declaration
		// - if node.localSymbol !== undefined - this is current declaration is exported and localSymbol points to the local symbol
		// - if node.localSymbol === undefined - this node is non-exported so we can just pick the result of getSymbolOfNode
		Symbol* symbol = getSymbolOfDeclaration(node);
		Symbol* localSymbol =
		    node->localSymbol() != nullptr ? node->localSymbol() : symbol;
		// Since the javascript won't do semantic analysis like typescript, ignore javascript function
		// declarations so that redeclaring a function in a JS file is not reported as a duplicate.
		if ((node->flags & NodeFlagsJavaScriptFile) == 0) {
			checkFunctionOrConstructorSymbol(localSymbol);
		}
		if (symbol->parent != nullptr) {
			// run check on export symbol to check that modifiers agree across all exported declarations
			checkFunctionOrConstructorSymbol(symbol);
		}
	}
	Node* body = node->body();
	checkSourceElement(body);
	checkAllCodePathsInNonVoidFunctionReturnOrThrow(node, getReturnTypeFromAnnotation(node));
	auto funcData = node->functionLikeData();
	if (funcData.fullSignature != nullptr && *funcData.fullSignature != nullptr) {
		checkSourceElement(*funcData.fullSignature);
		if (getContextualCallSignature(
		        getTypeFromTypeNode(*funcData.fullSignature), node) == nullptr) {
			error(*funcData.fullSignature,
			      A_JSDoc_type_tag_on_a_function_must_have_a_signature_with_the_correct_number_of_arguments);
		}
	}
	if (node->type() == nullptr) {
		// Report an implicit any error if there is no body, no explicit return type, and node is not a private method
		// in an ambient context
		if (nodeIsMissing(body) && !isPrivateWithinAmbient(node)) {
			reportImplicitAny(node, anyType, WideningKind::Normal);
		}
		if ((functionFlags & FunctionFlagsGenerator) != 0 && nodeIsPresent(body)) {
			// A generator with a body and no type annotation can still cause errors. It can error if the
			// yielded values have no common supertype, or it can give an implicit any error if it has no
			// yielded values. The only way to trigger these errors is to try checking its return type.
			getReturnTypeOfSignature(getSignatureFromDeclaration(node));
		}
	}
}

// checker.go:3489
void Checker::checkFunctionOrConstructorSymbol(Symbol* symbol) {
	// Only check the symbol once
	ValueSymbolLinks* links = valueSymbolLinks.Get(symbol);
	if (!links->functionOrConstructorChecked) {
		links->functionOrConstructorChecked = true;
		checkFunctionOrConstructorSymbolWorker(symbol);
	}
}

// checker.go:3497
void Checker::checkFunctionOrConstructorSymbolWorker(Symbol* symbol) {
	ModifierFlags flagsToCheck = ModifierFlagsExport | ModifierFlagsAmbient |
	                             ModifierFlagsPrivate | ModifierFlagsProtected |
	                             ModifierFlagsAbstract;
	ModifierFlags someNodeFlags = ModifierFlagsNone;
	ModifierFlags allNodeFlags = flagsToCheck;
	bool someHaveQuestionToken = false;
	bool allHaveQuestionToken = true;
	bool hasOverloads = false;
	Node* bodyDeclaration = nullptr;
	Node* lastSeenNonAmbientDeclaration = nullptr;
	Node* previousDeclaration = nullptr;
	std::vector<Node*> declarations = symbol->declarations;
	bool isConstructor = (symbol->flags & SymbolFlagsConstructor) != 0;
	bool duplicateFunctionDeclaration = false;
	bool multipleConstructorImplementation = false;
	bool hasNonAmbientClass = false;
	std::vector<Node*> functionDeclarations;
	auto getCanonicalOverload = [](const std::vector<Node*>& overloads,
	                               Node* implementation) -> Node* {
		// Consider the canonical set of flags to be the flags of the bodyDeclaration or the first declaration
		// Error on all deviations from this canonical set of flags
		// The caveat is that if some overloads are defined in lib.d.ts, we don't want to
		// report the errors on those. To achieve this, we will say that the implementation is
		// the canonical signature only if it is in the same container as the first overload
		bool implementationSharesContainerWithFirstOverload =
		    implementation != nullptr && implementation->parent == overloads[0]->parent;
		if (implementationSharesContainerWithFirstOverload) {
			return implementation;
		}
		return overloads[0];
	};
	auto checkFlagAgreementBetweenOverloads =
	    [&](const std::vector<Node*>& overloads, Node* implementation,
	        ModifierFlags flagsToCheck, ModifierFlags someOverloadFlags,
	        ModifierFlags allOverloadFlags) {
		// Error if some overloads have a flag that is not shared by all overloads. To find the
		// deviations, we XOR someOverloadFlags with allOverloadFlags
		ModifierFlags someButNotAllOverloadFlags = someOverloadFlags ^ allOverloadFlags;
		if (someButNotAllOverloadFlags != 0) {
			ModifierFlags canonicalFlags = getEffectiveDeclarationFlags(
			    getCanonicalOverload(overloads, implementation), flagsToCheck);
			std::map<SourceFile*, std::vector<Node*>> groups;
			for (Node* overload : overloads) {
				SourceFile* sourceFile = getSourceFileOfNode(overload);
				groups[sourceFile].push_back(overload);
			}
			for (auto& [sourceFile, overloadsInFile] : groups) {
				ModifierFlags canonicalFlagsForFile = getEffectiveDeclarationFlags(
				    getCanonicalOverload(overloadsInFile, implementation), flagsToCheck);
				for (Node* overload : overloadsInFile) {
					ModifierFlags deviation =
					    getEffectiveDeclarationFlags(overload, flagsToCheck) ^ canonicalFlags;
					ModifierFlags deviationInFile =
					    getEffectiveDeclarationFlags(overload, flagsToCheck) ^
					    canonicalFlagsForFile;
					if ((deviationInFile & ModifierFlagsExport) != 0) {
						// Overloads in different files need not all have export modifiers. This is ok:
						//   // lib.d.ts
						//   declare function foo(s: number): string;
						//   declare function foo(s: string): number;
						//   export { foo };
						//
						//   // app.ts
						//   declare module "lib" {
						//     export function foo(s: boolean): boolean;
						//   }
						error(getNameOfDeclaration(overload),
						      Overload_signatures_must_all_be_exported_or_non_exported);
					} else if ((deviationInFile & ModifierFlagsAmbient) != 0) {
						// Though rare, a module augmentation (necessarily ambient) is allowed to add overloads
						// to a non-ambient function in an implementation file.
						error(getNameOfDeclaration(overload),
						      Overload_signatures_must_all_be_ambient_or_non_ambient);
					} else if ((deviation &
					            (ModifierFlagsPrivate | ModifierFlagsProtected)) != 0) {
						Node* errNode = getNameOfDeclaration(overload) != nullptr
						                    ? getNameOfDeclaration(overload)
						                    : overload;
						error(errNode,
						      Overload_signatures_must_all_be_public_private_or_protected);
					} else if ((deviation & ModifierFlagsAbstract) != 0) {
						error(getNameOfDeclaration(overload),
						      Overload_signatures_must_all_be_abstract_or_non_abstract);
					}
				}
			}
		}
	};
	auto checkQuestionTokenAgreementBetweenOverloads =
	    [&](const std::vector<Node*>& overloads, Node* implementation,
	        bool someHaveQuestionToken, bool allHaveQuestionToken) {
		if (someHaveQuestionToken != allHaveQuestionToken) {
			bool canonicalHasQuestionToken =
			    isOptionalDeclaration(getCanonicalOverload(overloads, implementation));
			for (Node* o : overloads) {
				if (isOptionalDeclaration(o) != canonicalHasQuestionToken) {
					error(getNameOfDeclaration(o),
					      Overload_signatures_must_all_be_optional_or_required);
				}
			}
		}
	};
	auto reportImplementationExpectedError = [&](Node* node) {
		Node* name = node->name();
		if (name != nullptr && nodeIsMissing(name)) {
			return;
		}
		bool seen = false;
		Node* subsequentNode = nullptr;
		node->parent->forEachChild([&](Node* child) -> bool {
			if (seen) {
				subsequentNode = child;
				return true;
			}
			seen = child == node;
			return false;
		});
		// We may be here because of some extra nodes between overloads that could not be parsed into a valid node.
		// In this case the subsequent node is not really consecutive (.pos !== node.end), and we must ignore it here.
		if (subsequentNode != nullptr && subsequentNode->pos() == node->end()) {
			if (subsequentNode->kind == node->kind) {
				Node* subsequentName = subsequentNode->name();
				Node* errorNode =
				    subsequentName != nullptr ? subsequentName : subsequentNode;
				if (name != nullptr && subsequentName != nullptr &&
				    ((isPrivateIdentifier(name) && isPrivateIdentifier(subsequentName) &&
				      name->text() == subsequentName->text()) ||
				     (isComputedPropertyName(name) &&
				      isComputedPropertyName(subsequentName) &&
				      isTypeIdenticalTo(checkComputedPropertyName(name),
				                        checkComputedPropertyName(subsequentName))) ||
				     (isPropertyNameLiteral(name) &&
				      isPropertyNameLiteral(subsequentName) &&
				      name->text() == subsequentName->text()))) {
					bool reportError =
					    (isMethodDeclaration(node) ||
					     isMethodSignatureDeclaration(node)) &&
					    (isStatic(node) != isStatic(subsequentNode));
					// we can get here in two cases
					// 1. mixed static and instance class members
					// 2. something with the same name was defined before the set of overloads that prevents them from merging
					// here we'll report error only for the first case since for second we should already report error in binder
					if (reportError) {
						const DiagnosticMessage* diagnostic =
						    isStatic(node) ? Function_overload_must_be_static
						                   : Function_overload_must_not_be_static;
						error(errorNode, diagnostic);
					}
					return;
				}
				if (nodeIsPresent(subsequentNode->body())) {
					error(errorNode, Function_implementation_name_must_be_0,
					      {declarationNameToString(name)});
					return;
				}
			}
		}
		Node* errorNode = name != nullptr ? name : node;
		if (isConstructor) {
			error(errorNode, Constructor_implementation_is_missing);
		} else {
			// Report different errors regarding non-consecutive blocks of declarations depending on whether
			// the node in question is abstract.
			if (hasSyntacticModifier(node, ModifierFlagsAbstract)) {
				error(errorNode,
				      All_declarations_of_an_abstract_method_must_be_consecutive);
			} else {
				error(errorNode,
				      Function_implementation_is_missing_or_not_immediately_following_the_declaration);
			}
		}
	};
	for (Node* node : declarations) {
		bool inAmbientContext = (node->flags & NodeFlagsAmbient) != 0;
		bool inAmbientContextOrInterface =
		    inAmbientContext ||
		    (node->parent != nullptr && (isInterfaceDeclaration(node->parent) ||
		                                 isTypeLiteralNode(node->parent)));
		if (inAmbientContextOrInterface) {
			// check if declarations are consecutive only if they are non-ambient
			// 1. ambient declarations can be interleaved
			// i.e. this is legal
			//     declare function foo();
			//     declare function bar();
			//     declare function foo();
			// 2. mixing ambient and non-ambient declarations is a separate error that will be reported - do not want to report an extra one
			previousDeclaration = nullptr;
		}
		if (isClassLike(node) && !inAmbientContext) {
			hasNonAmbientClass = true;
		}
		if (isFunctionDeclaration(node) || isMethodDeclaration(node) ||
		    isMethodSignatureDeclaration(node) || isConstructorDeclaration(node)) {
			functionDeclarations.push_back(node);
			ModifierFlags currentNodeFlags =
			    getEffectiveDeclarationFlags(node, flagsToCheck);
			someNodeFlags |= currentNodeFlags;
			allNodeFlags &= currentNodeFlags;
			someHaveQuestionToken =
			    someHaveQuestionToken || isOptionalDeclaration(node);
			allHaveQuestionToken =
			    allHaveQuestionToken && isOptionalDeclaration(node);
			bool bodyIsPresent = nodeIsPresent(node->body());
			if (bodyIsPresent && bodyDeclaration != nullptr) {
				if (isConstructor) {
					multipleConstructorImplementation = true;
				} else {
					duplicateFunctionDeclaration = true;
				}
			} else if (previousDeclaration != nullptr &&
			           previousDeclaration->parent == node->parent &&
			           previousDeclaration->end() != node->pos() &&
			           (previousDeclaration->flags & NodeFlagsReparsed) == 0) {
				reportImplementationExpectedError(previousDeclaration);
			}
			if (bodyIsPresent) {
				if (bodyDeclaration == nullptr) {
					bodyDeclaration = node;
				}
			} else {
				hasOverloads = true;
			}
			previousDeclaration = node;
			if (!inAmbientContextOrInterface) {
				lastSeenNonAmbientDeclaration = node;
			}
		}
	}
	if (multipleConstructorImplementation) {
		for (Node* declaration : functionDeclarations) {
			error(declaration, Multiple_constructor_implementations_are_not_allowed);
		}
	}
	if (duplicateFunctionDeclaration) {
		for (Node* declaration : functionDeclarations) {
			Node* errNode = getNameOfDeclaration(declaration) != nullptr
			                    ? getNameOfDeclaration(declaration)
			                    : declaration;
			error(errNode, Duplicate_function_implementation);
		}
	}
	if (hasNonAmbientClass && !isConstructor &&
	    (symbol->flags & SymbolFlagsFunction) != 0 && !declarations.empty()) {
		std::vector<Diagnostic*> relatedDiagnostics;
		for (Node* declaration : declarations) {
			if (isClassDeclaration(declaration)) {
				relatedDiagnostics.push_back(createDiagnosticForNode(
				    declaration, Consider_adding_a_declare_modifier_to_this_class));
			}
		}
		for (Node* declaration : declarations) {
			const DiagnosticMessage* diagnostic = nullptr;
			switch (declaration->kind) {
			case Kind::ClassDeclaration:
				diagnostic = Class_declaration_cannot_implement_overload_list_for_0;
				break;
			case Kind::FunctionDeclaration:
				diagnostic = Function_with_bodies_can_only_merge_with_classes_that_are_ambient;
				break;
			default:
				break;
			}
			if (diagnostic != nullptr) {
				Node* errNode = getNameOfDeclaration(declaration) != nullptr
				                    ? getNameOfDeclaration(declaration)
				                    : declaration;
				error(errNode, diagnostic, {symbol->name})
				    ->SetRelatedInfo(relatedDiagnostics);
			}
		}
	}
	// Abstract methods can't have an implementation -- in particular, they don't need one.
	if (lastSeenNonAmbientDeclaration != nullptr &&
	    lastSeenNonAmbientDeclaration->body() == nullptr &&
	    !hasSyntacticModifier(lastSeenNonAmbientDeclaration, ModifierFlagsAbstract) &&
	    !isOptionalDeclaration(lastSeenNonAmbientDeclaration)) {
		reportImplementationExpectedError(lastSeenNonAmbientDeclaration);
	}
	if (hasOverloads) {
		checkFlagAgreementBetweenOverloads(declarations, bodyDeclaration, flagsToCheck,
		                                   someNodeFlags, allNodeFlags);
		checkQuestionTokenAgreementBetweenOverloads(declarations, bodyDeclaration,
		                                            someHaveQuestionToken,
		                                            allHaveQuestionToken);
		if (bodyDeclaration != nullptr) {
			std::vector<Signature*> signatures = getSignaturesOfSymbol(symbol);
			Signature* bodySignature = getSignatureFromDeclaration(bodyDeclaration);
			for (Signature* signature : signatures) {
				if (!isImplementationCompatibleWithOverload(bodySignature, signature)) {
					Node* errorNode = signature->declaration;
					error(errorNode,
					      This_overload_signature_is_not_compatible_with_its_implementation_signature)
					    ->AddRelatedInfo(createDiagnosticForNode(
					        bodyDeclaration,
					        The_implementation_signature_is_declared_here));
					break;
				}
			}
		}
	}
}

// checker.go:3729
ModifierFlags Checker::getEffectiveDeclarationFlags(Node* n, ModifierFlags flagsToCheck) {
	ModifierFlags flags = getCombinedModifierFlagsCached(n);
	// children of classes (even ambient classes) should not be marked as ambient or export
	// because those flags have no useful semantics there.
	if (!isInterfaceDeclaration(n->parent) && !isClassDeclaration(n->parent) &&
	    !isClassExpression(n->parent) && (n->flags & NodeFlagsAmbient) != 0) {
		Node* container = getEnclosingContainer(n);
		if (container != nullptr && (container->flags & NodeFlagsExportContext) != 0 &&
		    (flags & ModifierFlagsAmbient) == 0 &&
		    !(isModuleBlock(n->parent) && isGlobalScopeAugmentation(n->parent->parent))) {
			// It is nested in an ambient export context, which means it is automatically exported
			flags |= ModifierFlagsExport;
		}
		flags |= ModifierFlagsAmbient;
	}
	return flags & flagsToCheck;
}

// checker.go:3744
bool Checker::isImplementationCompatibleWithOverload(Signature* implementation,
                                                     Signature* overload) {
	Signature* erasedSource = getErasedSignature(implementation);
	Signature* erasedTarget = getErasedSignature(overload);
	// First see if the return types are compatible in either direction.
	Type* sourceReturnType = getReturnTypeOfSignature(erasedSource);
	Type* targetReturnType = getReturnTypeOfSignature(erasedTarget);
	if (targetReturnType == voidType ||
	    isTypeRelatedTo(targetReturnType, sourceReturnType, assignableRelation) ||
	    isTypeRelatedTo(sourceReturnType, targetReturnType, assignableRelation)) {
		return isSignatureAssignableTo(erasedSource, erasedTarget,
		                               true /*ignoreReturnTypes*/);
	}
	return false;
}

// checker.go:3756
void Checker::checkAllCodePathsInNonVoidFunctionReturnOrThrow(Node* fn, Type* returnType) {
	FunctionFlags functionFlags = getFunctionFlags(fn);
	Type* t = nullptr;
	if (returnType != nullptr) {
		t = unwrapReturnType(returnType, functionFlags);
	}
	// Functions with an explicitly specified return type that includes `void` or is exactly `any` or `undefined` don't
	// need any return statements.
	if (t != nullptr && (maybeTypeOfKind(t, TypeFlagsVoid) ||
	                     (t->flags & (TypeFlagsAny | TypeFlagsUndefined)) != 0)) {
		return;
	}
	// If all we have is a function signature, or an arrow function with an expression body, then there is nothing to check.
	// also if HasImplicitReturn flag is not set this means that all codepaths in function body end with return or throw
	if (isMethodSignatureDeclaration(fn) || nodeIsMissing(fn->body()) ||
	    !isBlock(fn->body()) || !functionHasImplicitReturn(fn)) {
		return;
	}
	bool hasExplicitReturn = (fn->flags & NodeFlagsHasExplicitReturn) != 0;
	Node* errorNode = fn->type();
	if (errorNode == nullptr) {
		auto data = fn->functionLikeData();
		if (data.fullSignature != nullptr && *data.fullSignature != nullptr) {
			errorNode = *data.fullSignature;
		}
	}
	if (errorNode == nullptr) {
		errorNode = fn;
	}
	if (t != nullptr && (t->flags & TypeFlagsNever) != 0) {
		error(errorNode, A_function_returning_never_cannot_have_a_reachable_end_point);
	} else if (t != nullptr && !hasExplicitReturn) {
		// minimal check: function has syntactic return type annotation and no explicit return statements in the body
		// this function does not conform to the specification.
		error(errorNode,
		      A_function_whose_declared_type_is_neither_undefined_void_nor_any_must_return_a_value);
	} else if (t != nullptr && strictNullChecks &&
	           !isTypeAssignableTo(undefinedType, t)) {
		error(errorNode,
		      Function_lacks_ending_return_statement_and_return_type_does_not_include_undefined);
	} else if (compilerOptions->NoImplicitReturns == Tristate::True) {
		if (t == nullptr) {
			// If return type annotation is omitted check if function has any explicit return statements.
			// If it does not have any - its inferred return type is void - don't do any checks.
			// Otherwise get inferred return type from function body and report error only if it is not void / anytype
			if (!hasExplicitReturn) {
				return;
			}
			Type* inferredReturnType =
			    getReturnTypeOfSignature(getSignatureFromDeclaration(fn));
			if (isUnwrappedReturnTypeUndefinedVoidOrAny(fn, inferredReturnType)) {
				return;
			}
		}
		error(errorNode, Not_all_code_paths_return_a_value);
	}
}

// checker.go:3808
bool Checker::isUnwrappedReturnTypeUndefinedVoidOrAny(Node* fn, Type* returnType) {
	Type* t = unwrapReturnType(returnType, getFunctionFlags(fn));
	return t != nullptr && (maybeTypeOfKind(t, TypeFlagsVoid) ||
	                        (t->flags & (TypeFlagsAny | TypeFlagsUndefined)) != 0);
}

// checker.go:3813
void Checker::checkBlock(Node* node) {
	// Grammar checking for SyntaxKind.Block
	if (node->kind == Kind::Block) {
		checkGrammarStatementInAmbientContext(node);
	}
	if (isFunctionOrModuleBlock(node)) {
		bool saveFlowAnalysisDisabled = flowAnalysisDisabled;
		checkSourceElements(node->statements());
		flowAnalysisDisabled = saveFlowAnalysisDisabled;
	} else {
		checkSourceElements(node->statements());
	}
	SymbolTable* locals = node->locals();
	if (locals != nullptr && !locals->empty()) {
		registerForUnusedIdentifiersCheck(node);
	}
}

// ---------------------------------------------------------------------------
// === dep stubs — owned by other slices; deleted when the owner's real
// definition lands. ===
// ---------------------------------------------------------------------------

// owner: declchecks varchecks range (checker.go:5929-6264)
void Checker::checkVariableLikeDeclaration(Node* node) {
	TSC_UNREACHABLE("checkVariableLikeDeclaration — varchecks slice");
}
void Checker::checkDecorators(Node* node) {
	TSC_UNREACHABLE("checkDecorators — varchecks slice");
}
// owner: declchecks alias/type-params/unused range (checker.go:6906-7500)
void Checker::checkTypeNameIsReserved(Node* name, const DiagnosticMessage* message) {
	TSC_UNREACHABLE("checkTypeNameIsReserved — aliasunused slice");
}
void Checker::checkTypeParameters(const std::vector<Node*>& typeParameterDeclarations) {
	TSC_UNREACHABLE("checkTypeParameters — aliasunused slice");
}
// owner: declchecks iterations range (checker.go:6265-6905)
bool Checker::isReferenceToType(Type* t, Type* target) {
	TSC_UNREACHABLE("isReferenceToType — iterations slice");
}
// owner: decltypes slice (checker.go:16720-19097)
// (deduped: getConstraintOfTypeParameter defined in cpp/internal/checker/checker_decltypes.cpp)

// (deduped: getTypeParametersForTypeReferenceOrImport defined in cpp/internal/checker/checker_decltypes.cpp)

// (deduped: getTypeOfAccessors defined in cpp/internal/checker/checker_decltypes.cpp)

// (deduped: reportImplicitAny defined in cpp/internal/checker/checker_decltypes.cpp)

// owner: signatures slice (checker.go:20143-20986)
// owner: members slice (checker.go:19186-20143, 20987-22284)
// (deduped: getCombinedModifierFlagsCached defined in cpp/internal/checker/checker_decltypes.cpp)

// owner: instantiate slice (checker.go:22285-23219)
// owner: typenodes slice (checker.go:23220-25738)
// (deduped: getNullableType defined in cpp/internal/checker/checker_decltypes.cpp)

// owner: typeops slice (checker.go:26020-28654)
void Checker::checkIndexedAccessIndexType(Type* t, Node* node) {
	TSC_UNREACHABLE("checkIndexedAccessIndexType — typeops slice");
}
// markrefs slice landed in checker_markrefs.cpp — checkExternalEmitHelpers moved there.
// owner: relater slice (relater.go)
Type* Checker::createMarkerType(Symbol* symbol, Type* source, Type* target) {
	TSC_UNREACHABLE("createMarkerType — relater slice");
}
// owner: checker.go:10502 slice (contextual-call machinery)
Signature* Checker::getContextualCallSignature(Type* t, Node* node) {
	TSC_UNREACHABLE("getContextualCallSignature — contextual slice");
}
// owner: relater.go (whole file)
bool Checker::checkTypeAssignableToEx(Type* source, Type* target, Node* errorNode,
                                      const DiagnosticMessage* headMessage,
                                      std::vector<Diagnostic*>* diagnosticOutput) {
	TSC_UNREACHABLE("checkTypeAssignableToEx — relater slice");
}
bool Checker::isSignatureAssignableTo(Signature* source, Signature* target,
                                    bool ignoreReturnTypes) {
	TSC_UNREACHABLE("isSignatureAssignableTo — relater slice");
}
TypePredicate* Checker::getTypePredicateOfSignature(Signature* sig) {
	TSC_UNREACHABLE("getTypePredicateOfSignature — relater slice");
}
// owner: expressions slice (checker.go:8090-14185)
void Checker::checkCollisionsForDeclarationName(Node* node, Node* name) {
	TSC_UNREACHABLE("checkCollisionsForDeclarationName — expressions slice");
}
void Checker::setNodeLinksForPrivateIdentifierScope(Node* node) {
	TSC_UNREACHABLE("setNodeLinksForPrivateIdentifierScope — expressions slice");
}
// (deduped: classDeclarationExtendsNull defined in cpp/internal/checker/checker_expressions_c.cpp)
// (deduped: getResolvedSymbolOrNil defined in cpp/internal/checker/checker_expressions_c.cpp)
// owner: diagnostics tail (checker.go:14185-14304)
bool Checker::IsDeprecatedDeclaration(Node* declaration) {
	TSC_UNREACHABLE("IsDeprecatedDeclaration — diagtail slice");
}
Diagnostic* Checker::addDeprecatedSuggestion(Node* location,
                                             std::vector<Node*> declarations,
                                             const std::string& deprecatedEntity) {
	TSC_UNREACHABLE("addDeprecatedSuggestion — diagtail slice");
}
// owner: checker.go ~7509-8090 (statement/expression checks outside this slice)
void Checker::checkImportAttributes(Node* node) {
	TSC_UNREACHABLE("checkImportAttributes — modulechecks slice");
}
ModifierFlags Checker::getTypeParameterModifiers(Type* typeParameter) {
	TSC_UNREACHABLE("getTypeParameterModifiers — decltypes slice");
}

}  // namespace tsc::checker
