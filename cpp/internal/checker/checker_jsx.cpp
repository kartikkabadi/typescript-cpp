// === slice: jsx === — port of tsc/internal/checker/jsx.go (1488 lines).
// JSX checking: element/self-closing/fragment checking, intrinsic-element
// resolution via the JSX namespace, attributes type synthesis, children
// checking, and jsxFactory/jsxFragmentFactory resolution for the classic and
// automatic JSX runtimes. All functions are in Go file order.
//
// Free Go functions in jsx.go are file-local below (anonymous namespace);
// markAsSynthetic, getSemanticJsxChildren (ast/utilities.go:3834),
// isHyphenatedJsxName (relater.go:743) and isJsxAttributeLike
// (ast/utilities.go:624) are likewise file-local replicas because the same
// applies — none of them collide with Checker member names, so no jsx_detail
// namespace is needed. Dep stubs for cross-slice callees with no other
// declaration live at the bottom of this file and are removed when the owning
// slice lands.

#include "internal/checker/checker.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/ast/symbol.h"
#include "internal/checker/mapper.h"
#include "internal/parser/parser.h"
#include "internal/scanner/scanner.h"

namespace tsc::checker {

// Free helpers defined (non-static) in checker.cpp — declared here, not
// replicated (see PORTING.md).
bool someType(Type* t, const std::function<bool(Type*)>& f);

namespace {

// core.IfElse (file-local replica).
template <class T>
T jsxIfElse(bool cond, T a, T b) {
	return cond ? a : b;
}

// debug.Assert — Go panics; TSC_UNREACHABLE is the port's panic.
[[noreturn]] void jsxAssertFail() { TSC_UNREACHABLE("Assertion failed"); }
inline void debugAssert(bool cond) {
	if (!cond) {
		jsxAssertFail();
	}
}

// getStringLiteralValue — replica of the checker.cpp file-local static
// (checker.cpp:701). The same replica exists in the members/inference/typeops/
// widen/decltypes TUs.
std::string getStringLiteralValue(Type* t) {
	return std::get<std::string>(t->AsLiteralType()->value);
}

// jsx.go:1443 — markAsSynthetic (ast.Visitor: returns false after marking).
bool markAsSynthetic(Node* node) {
	node->loc = TextRange{-1, -1};
	node->forEachChild(markAsSynthetic);
	return false;
}

// relater.go:743 — isHyphenatedJsxName.
bool isHyphenatedJsxName(const std::string& name) {
	return name.find('-') != std::string::npos;
}

// ast/utilities.go:624 — IsJsxAttributeLike.
bool isJsxAttributeLike(Node* node) {
	return isJsxAttribute(node) || isJsxSpreadAttribute(node);
}

// jsx.go:42 — JsxNames / ReactNames (Go package vars; file-local here).
struct JsxNamesStruct {
	const char* JSX;
	const char* IntrinsicElements;
	const char* ElementClass;
	const char* ElementAttributesPropertyNameContainer;
	const char* ElementChildrenAttributeNameContainer;
	const char* Element;
	const char* ElementType;
	const char* IntrinsicAttributes;
	const char* IntrinsicClassAttributes;
	const char* LibraryManagedAttributes;
};
const JsxNamesStruct JsxNames = {
	"JSX",
	"IntrinsicElements",
	"ElementClass",
	"ElementAttributesProperty",
	"ElementChildrenAttribute",
	"Element",
	"ElementType",
	"IntrinsicAttributes",
	"IntrinsicClassAttributes",
	"LibraryManagedAttributes",
};

struct ReactNamesStruct {
	const char* Fragment;
};
const ReactNamesStruct ReactNames = {"Fragment"};

} // namespace

// jsx.go:72
Type* Checker::checkJsxElement(Node* node, CheckMode checkMode) {
	checkNodeDeferred(node);
	return getJsxElementTypeAt(node);
}

// jsx.go:77
void Checker::checkJsxElementDeferred(Node* node) {
	JsxElement* jsxElement = node->as<JsxElement>();
	checkJsxOpeningLikeElementOrOpeningFragment(jsxElement->OpeningElement);
	// Perform resolution on the closing tag so that rename/go to definition/etc work
	if (isJsxIntrinsicTagName(jsxElement->ClosingElement->tagName())) {
		getIntrinsicTagSymbol(jsxElement->ClosingElement);
	} else {
		checkExpression(jsxElement->ClosingElement->tagName());
	}
	checkJsxChildren(node, CheckModeNormal);
}

// jsx.go:89
Type* Checker::checkJsxExpression(Node* node, CheckMode checkMode) {
	checkGrammarJsxExpression(node->as<JsxExpression>());
	if (node->expression() == nullptr) {
		return errorType;
	}
	Type* t = checkExpressionEx(node->expression(), checkMode);
	if (node->as<JsxExpression>()->DotDotDotToken != nullptr && t != anyType &&
		!isArrayType(t)) {
		error(node, JSX_spread_child_must_be_an_array_type);
	}
	return t;
}

// jsx.go:101
Type* Checker::checkJsxSelfClosingElement(Node* node, CheckMode checkMode) {
	checkNodeDeferred(node);
	return getJsxElementTypeAt(node);
}

// jsx.go:106
void Checker::checkJsxSelfClosingElementDeferred(Node* node) {
	checkJsxOpeningLikeElementOrOpeningFragment(node);
}

// jsx.go:110
Type* Checker::checkJsxFragment(Node* node) {
	checkJsxOpeningLikeElementOrOpeningFragment(node->as<JsxFragment>()->OpeningFragment);
	// by default, jsx:'react' will use jsxFactory = React.createElement and jsxFragmentFactory = React.Fragment
	// if jsxFactory compiler option is provided, ensure jsxFragmentFactory compiler option or @jsxFrag pragma is provided too
	SourceFile* nodeSourceFile = getSourceFileOfNode(node);
	if (compilerOptions->GetJSXTransformEnabled() &&
		(compilerOptions->JsxFactory != "" ||
			getPragmaFromSourceFile(nodeSourceFile, "jsx") != nullptr) &&
		compilerOptions->JsxFragmentFactory == "" &&
		getPragmaFromSourceFile(nodeSourceFile, "jsxfrag") == nullptr) {
		const DiagnosticMessage* message = jsxIfElse(
			compilerOptions->JsxFactory != "",
			The_jsxFragmentFactory_compiler_option_must_be_provided_to_use_JSX_fragments_with_the_jsxFactory_compiler_option,
			An_jsxFrag_pragma_is_required_when_using_an_jsx_pragma_with_JSX_fragments);
		error(node, message);
	}
	checkJsxChildren(node, CheckModeNormal);
	Type* t = getJsxElementTypeAt(node);
	return jsxIfElse(isErrorType(t), anyType, t);
}

// jsx.go:126
Type* Checker::checkJsxAttributes(Node* node, CheckMode checkMode) {
	checkNodeDeferred(node);
	return createJsxAttributesTypeFromAttributesProperty(node->parent, checkMode);
}

// jsx.go:131
void Checker::checkJsxOpeningLikeElementOrOpeningFragment(Node* node) {
	bool isNodeOpeningLikeElement = isJsxOpeningLikeElement(node);
	if (isNodeOpeningLikeElement) {
		checkGrammarJsxElement(node);
	}
	checkJsxPreconditions(node);
	markJsxAliasReferenced(node);
	Signature* sig = getResolvedSignature(node, nullptr, CheckModeNormal);
	checkDeprecatedSignature(sig, node);
	if (isNodeOpeningLikeElement) {
		Type* elementTypeConstraint = getJsxElementTypeTypeAt(node);
		if (elementTypeConstraint != nullptr) {
			Node* tagName = node->tagName();
			Type* tagType;
			if (isJsxIntrinsicTagName(tagName)) {
				tagType = getStringLiteralType(tagName->text());
			} else {
				tagType = checkExpression(tagName);
			}
			std::vector<Diagnostic*> diags;
			if (!checkTypeRelatedToEx(tagType, elementTypeConstraint, assignableRelation,
					tagName, Its_type_0_is_not_a_valid_JSX_element_type, &diags)) {
				addDiagnostic(newDiagnosticChain(
					diags[0], X_0_cannot_be_used_as_a_JSX_component,
					{getTextOfNode(tagName)}));
			}
		} else {
			checkJsxReturnAssignableToAppropriateBound(getJsxReferenceKind(node),
				getReturnTypeOfSignature(sig), node);
		}
	}
}

// jsx.go:160
void Checker::checkJsxPreconditions(Node* errorNode) {
	// Preconditions for using JSX
	if (compilerOptions->Jsx == JsxEmit::None) {
		error(errorNode, Cannot_use_JSX_unless_the_jsx_flag_is_provided);
	}
	if (noImplicitAny && getJsxElementTypeAt(errorNode) == nullptr) {
		error(errorNode, JSX_element_implicitly_has_type_any_because_the_global_type_JSX_Element_does_not_exist);
	}
}

// jsx.go:170
void Checker::checkJsxReturnAssignableToAppropriateBound(JsxReferenceKind refKind,
	Type* elemInstanceType, Node* openingLikeElement) {
	std::vector<Diagnostic*> diags;
	switch (refKind) {
	case JsxReferenceKind::Function: {
		Type* sfcReturnConstraint = getJsxStatelessElementTypeAt(openingLikeElement);
		if (sfcReturnConstraint != nullptr) {
			checkTypeRelatedToEx(elemInstanceType, sfcReturnConstraint,
				assignableRelation, openingLikeElement->tagName(),
				Its_return_type_0_is_not_a_valid_JSX_element, &diags);
		}
		break;
	}
	case JsxReferenceKind::Component: {
		Type* classConstraint = getJsxElementClassTypeAt(openingLikeElement);
		if (classConstraint != nullptr) {
			// Issue an error if this return type isn't assignable to JSX.ElementClass, failing that
			checkTypeRelatedToEx(elemInstanceType, classConstraint, assignableRelation,
				openingLikeElement->tagName(),
				Its_instance_type_0_is_not_a_valid_JSX_element, &diags);
		}
		break;
	}
	default: {
		Type* sfcReturnConstraint = getJsxStatelessElementTypeAt(openingLikeElement);
		Type* classConstraint = getJsxElementClassTypeAt(openingLikeElement);
		if (sfcReturnConstraint == nullptr || classConstraint == nullptr) {
			return;
		}
		Type* combined = getUnionType({sfcReturnConstraint, classConstraint});
		checkTypeRelatedToEx(elemInstanceType, combined, assignableRelation,
			openingLikeElement->tagName(),
			Its_element_type_0_is_not_a_valid_JSX_element, &diags);
		break;
	}
	}
	if (!diags.empty()) {
		addDiagnostic(newDiagnosticChain(diags[0],
			X_0_cannot_be_used_as_a_JSX_component,
			{getTextOfNode(openingLikeElement->tagName())}));
	}
}

// jsx.go:198
std::vector<Type*> Checker::inferJsxTypeArguments(Node* node, Signature* signature,
	CheckMode checkMode, InferenceContext* context) {
	Type* paramType = getEffectiveFirstArgumentForJsxSignature(signature, node);
	Type* checkAttrType = checkExpressionWithContextualType(
		node->attributes(), paramType, context, checkMode);
	inferTypes(context->inferences, checkAttrType, paramType, InferencePriorityNone,
		false);
	return getInferredTypes(context);
}

// jsx.go:205
Type* Checker::getContextualTypeForJsxExpression(Node* node, ContextFlags contextFlags) {
	if (isJsxAttributeLike(node->parent)) {
		return getContextualType(node, contextFlags);
	}
	if (isJsxElement(node->parent)) {
		return getContextualTypeForChildJsxExpression(node->parent, node, contextFlags);
	}
	return nullptr;
}

// jsx.go:215
Type* Checker::getContextualTypeForJsxAttribute(Node* attribute, ContextFlags contextFlags) {
	// When we trying to resolve JsxOpeningLikeElement as a stateless function element, we will already give its attributes a contextual type
	// which is a type of the parameter of the signature we are trying out.
	// If there is no contextual type (e.g. we are trying to resolve stateful component), get attributes type from resolving element's tagName
	if (isJsxAttribute(attribute)) {
		Type* attributesType = getApparentTypeOfContextualType(attribute->parent, contextFlags);
		if (attributesType == nullptr || isTypeAny(attributesType)) {
			return nullptr;
		}
		return getTypeOfPropertyOfContextualType(attributesType, attribute->name()->text());
	}
	return getContextualType(attribute->parent, contextFlags);
}

// jsx.go:229
Type* Checker::getContextualJsxElementAttributesType(Node* node, ContextFlags contextFlags) {
	if (isJsxOpeningElement(node) && contextFlags != ContextFlagsIgnoreNodeInferences) {
		int index = findContextualNode(node->parent, contextFlags == ContextFlagsNone /*includeCaches*/);
		if (index >= 0) {
			// Contextually applied type is moved from attributes up to the outer jsx attributes so when walking up from the children they get hit
			// _However_ to hit them from the _attributes_ we must look for them here; otherwise we'll used the declared type
			// (as below) instead!
			return contextualInfos[index].t;
		}
	}
	return getContextualTypeForArgumentAtIndex(node, 0);
}

// jsx.go:242
Type* Checker::getContextualTypeForChildJsxExpression(Node* node, Node* child,
	ContextFlags contextFlags) {
	Type* attributesType = getApparentTypeOfContextualType(
		node->as<JsxElement>()->OpeningElement->attributes(), contextFlags);
	// JSX expression is in children of JSX Element, we will look for an "children" attribute (we get the name from JSX.ElementAttributesProperty)
	std::string jsxChildrenPropertyName =
		getJsxElementChildrenPropertyName(getJsxNamespaceAt(node));
	if (!(attributesType != nullptr && !isTypeAny(attributesType) &&
			jsxChildrenPropertyName != InternalSymbolNameMissing &&
			jsxChildrenPropertyName != "")) {
		return nullptr;
	}
	std::vector<Node*> realChildren = getSemanticJsxChildren(node->children()->nodes);
	auto childIt = std::find(realChildren.begin(), realChildren.end(), child);
	int childIndex = childIt != realChildren.end()
		? static_cast<int>(childIt - realChildren.begin())
		: -1;
	Type* childFieldType =
		getTypeOfPropertyOfContextualType(attributesType, jsxChildrenPropertyName);
	if (childFieldType == nullptr) {
		return nullptr;
	}
	if (realChildren.size() == 1) {
		return childFieldType;
	}
	return mapTypeEx(childFieldType, [this, childIndex](Type* t) -> Type* {
		if (isArrayLikeType(t)) {
			return getIndexedAccessType(t, getNumberLiteralType(Number(childIndex)));
		}
		return t;
	}, true /*noReductions*/);
}

// jsx.go:266
Type* Checker::discriminateContextualTypeByJSXAttributes(Node* node, Type* contextualType) {
	DiscriminatedContextualTypeKey key{getNodeId(node), contextualType->id};
	auto cachedIt = discriminatedContextualTypes.find(key);
	if (cachedIt != discriminatedContextualTypes.end()) {
		return cachedIt->second;
	}
	std::string jsxChildrenPropertyName =
		getJsxElementChildrenPropertyName(getJsxNamespaceAt(node));
	std::vector<Node*> discriminantProperties;
	for (Node* p : node->properties()) {
		Symbol* symbol = p->symbol();
		if (symbol == nullptr || !isJsxAttribute(p)) {
			continue;
		}
		Node* initializer = p->initializer();
		if ((initializer == nullptr || isPossiblyDiscriminantValue(initializer)) &&
			isDiscriminantProperty(contextualType, symbol->data->name)) {
			discriminantProperties.push_back(p);
		}
	}
	std::vector<Symbol*> discriminantMembers;
	for (Symbol* s : getPropertiesOfType(contextualType)) {
		if ((s->flags & SymbolFlagsOptional) == 0 || node->symbol() == nullptr) {
			continue;
		}
		Node* element = node->parent->parent;
		if (s->data->name == jsxChildrenPropertyName && isJsxElement(element) &&
			!getSemanticJsxChildren(element->children()->nodes).empty()) {
			continue;
		}
		if (node->symbol()->data->members.find(s->data->name) == node->symbol()->data->members.end() &&
			isDiscriminantProperty(contextualType, s->data->name)) {
			discriminantMembers.push_back(s);
		}
	}
	ObjectLiteralDiscriminator discriminator;
	discriminator.c = this;
	discriminator.props = discriminantProperties;
	discriminator.members = discriminantMembers;
	Type* discriminated =
		discriminateTypeByDiscriminableItems(contextualType, discriminator);
	discriminatedContextualTypes[key] = discriminated;
	return discriminated;
}

// jsx.go:296
bool Checker::elaborateJsxComponents(Node* node, Type* source, Type* target,
	Relation* relation, std::vector<Diagnostic*>* diagnosticOutput) {
	bool reportedError = false;
	for (Node* prop : node->properties()) {
		if (!isJsxSpreadAttribute(prop) &&
			!isHyphenatedJsxName(prop->name()->text())) {
			Type* nameType = getStringLiteralType(prop->name()->text());
			if (nameType != nullptr && (nameType->flags & TypeFlagsNever) == 0) {
				reportedError =
					elaborateElement(source, target, relation, prop->name(),
						prop->initializer(), nameType, nullptr, nullptr,
						diagnosticOutput) ||
					reportedError;
			}
		}
	}
	if (isJsxOpeningElement(node->parent) && isJsxElement(node->parent->parent)) {
		Node* containingElement = node->parent->parent; // Containing JSXElement
		std::string childrenPropName =
			getJsxElementChildrenPropertyName(getJsxNamespaceAt(node));
		if (childrenPropName == InternalSymbolNameMissing) {
			childrenPropName = "children";
		}
		Type* childrenNameType = getStringLiteralType(childrenPropName);
		Type* childrenTargetType = getIndexedAccessType(target, childrenNameType);
		std::vector<Node*> validChildren =
			getSemanticJsxChildren(containingElement->children()->nodes);
		if (validChildren.empty()) {
			return reportedError;
		}
		bool moreThanOneRealChildren = validChildren.size() > 1;
		Type* arrayLikeTargetParts = nullptr;
		Type* nonArrayLikeTargetParts = nullptr;
		Type* iterableType = getGlobalIterableType();
		if (iterableType != emptyGenericType) {
			Type* anyIterable = createIterableType(anyType);
			arrayLikeTargetParts = filterType(childrenTargetType,
				[this, anyIterable](Type* t) { return isTypeAssignableTo(t, anyIterable); });
			nonArrayLikeTargetParts = filterType(childrenTargetType,
				[this, anyIterable](Type* t) {
					return !isTypeAssignableTo(t, anyIterable);
				});
		} else {
			arrayLikeTargetParts = filterType(childrenTargetType,
				[this](Type* t) { return isArrayOrTupleLikeType(t); });
			nonArrayLikeTargetParts = filterType(childrenTargetType,
				[this](Type* t) { return !isArrayOrTupleLikeType(t); });
		}
		const DiagnosticMessage* invalidTextDiagnostic = nullptr;
		std::vector<std::string> invalidTextDiagnosticArgs;
		GetInvalidTextDiagnostic getInvalidTextualChildDiagnostic =
			[this, node, childrenPropName, childrenTargetType,
				&invalidTextDiagnostic,
				&invalidTextDiagnosticArgs]() -> std::pair<const DiagnosticMessage*, std::vector<std::string>> {
			if (invalidTextDiagnostic == nullptr) {
				std::string tagNameText = getTextOfNode(node->parent->tagName());
				invalidTextDiagnostic = X_0_components_don_t_accept_text_as_child_elements_Text_in_JSX_has_the_type_string_but_the_expected_type_of_1_is_2;
				invalidTextDiagnosticArgs = {tagNameText, childrenPropName,
					TypeToString(childrenTargetType)};
			}
			return {invalidTextDiagnostic, invalidTextDiagnosticArgs};
		};
		if (moreThanOneRealChildren) {
			if (arrayLikeTargetParts != neverType) {
				Type* realSource = createTupleType(
					checkJsxChildren(containingElement, CheckModeNormal));
				JsxElaborationSeq children = generateJsxChildren(
					containingElement, getInvalidTextualChildDiagnostic);
				reportedError = elaborateIterableOrArrayLikeTargetElementwise(
						children, realSource, arrayLikeTargetParts, relation,
						diagnosticOutput) ||
					reportedError;
			} else if (!isTypeRelatedTo(getIndexedAccessType(source, childrenNameType),
						   childrenTargetType, relation)) {
				// arity mismatch
				Diagnostic* diag = error(
					containingElement->as<JsxElement>()->OpeningElement->tagName(),
					This_JSX_tag_s_0_prop_expects_a_single_child_of_type_1_but_multiple_children_were_provided,
					{childrenPropName, TypeToString(childrenTargetType)});
				reportDiagnostic(diag, diagnosticOutput);
				reportedError = true;
			}
		} else {
			if (nonArrayLikeTargetParts != neverType) {
				Node* child = validChildren[0];
				JsxElaborationElement e = getElaborationElementForJsxChild(
					child, childrenNameType, getInvalidTextualChildDiagnostic);
				if (e.errorNode != nullptr) {
					reportedError = elaborateElement(source, target, relation,
							e.errorNode, e.innerExpression, e.nameType, nullptr,
							e.createDiagnostic, diagnosticOutput) ||
						reportedError;
				}
			} else if (!isTypeRelatedTo(getIndexedAccessType(source, childrenNameType),
						   childrenTargetType, relation)) {
				// arity mismatch
				Diagnostic* diag = error(
					containingElement->as<JsxElement>()->OpeningElement->tagName(),
					This_JSX_tag_s_0_prop_expects_type_1_which_requires_multiple_children_but_only_a_single_child_was_provided,
					{childrenPropName, TypeToString(childrenTargetType)});
				reportDiagnostic(diag, diagnosticOutput);
				reportedError = true;
			}
		}
	}
	return reportedError;
}

// jsx.go:376 — iter.Seq[JsxElaborationElement] becomes a std::function taking
// the yield callback.
JsxElaborationSeq Checker::generateJsxChildren(
	Node* node, const GetInvalidTextDiagnostic& getInvalidTextDiagnostic) {
	return [this, node, getInvalidTextDiagnostic](
			   const std::function<bool(JsxElaborationElement)>& yield) {
		int memberOffset = 0;
		const std::vector<Node*>& children = node->children()->nodes;
		for (size_t i = 0; i < children.size(); i++) {
			Type* nameType =
				getNumberLiteralType(Number(static_cast<int>(i) - memberOffset));
			JsxElaborationElement e = getElaborationElementForJsxChild(
				children[i], nameType, getInvalidTextDiagnostic);
			if (e.errorNode != nullptr) {
				if (!yield(e)) {
					return;
				}
			} else {
				memberOffset++;
			}
		}
	};
}

// jsx.go:393
JsxElaborationElement Checker::getElaborationElementForJsxChild(
	Node* child, Type* nameType,
	const GetInvalidTextDiagnostic& getInvalidTextDiagnostic) {
	switch (child->kind) {
	case Kind::JsxExpression:
		// child is of the type of the expression
		return JsxElaborationElement{child, child->expression(), nameType, {}};
	case Kind::JsxText:
		if (child->as<JsxText>()->ContainsOnlyTriviaWhiteSpaces) {
			// Whitespace only jsx text isn't real jsx text
			return JsxElaborationElement{};
		}
		// child is a string
		return JsxElaborationElement{
			child, nullptr, nameType,
			[getInvalidTextDiagnostic](Node* prop) -> Diagnostic* {
				auto [errorMessage, errorArgs] = getInvalidTextDiagnostic();
				return NewDiagnosticForNode(prop, errorMessage, errorArgs);
			}};
	case Kind::JsxElement:
	case Kind::JsxSelfClosingElement:
	case Kind::JsxFragment:
		// child is of type JSX.Element
		return JsxElaborationElement{child, child, nameType, {}};
	default:
		break;
	}
	TSC_UNREACHABLE("Unhandled case in getElaborationElementForJsxChild");
}

// jsx.go:420
bool Checker::elaborateIterableOrArrayLikeTargetElementwise(
	JsxElaborationSeq iterator, Type* source, Type* target, Relation* relation,
	std::vector<Diagnostic*>* diagnosticOutput) {
	Type* tupleOrArrayLikeTargetParts =
		filterType(target, [this](Type* t) { return isArrayOrTupleLikeType(t); });
	Type* nonTupleOrArrayLikeTargetParts =
		filterType(target, [this](Type* t) { return !isArrayOrTupleLikeType(t); });
	// If `nonTupleOrArrayLikeTargetParts` is not `never`, then that should mean `Iterable` is defined.
	Type* iterationType = nullptr;
	if (nonTupleOrArrayLikeTargetParts != neverType) {
		iterationType = getIterationTypeOfIterable(IterationUseForOf,
			IterationTypeKind::Yield, nonTupleOrArrayLikeTargetParts,
			nullptr /*errorNode*/);
	}
	bool reportedError = false;
	iterator([this, &reportedError, source, target, relation, diagnosticOutput,
				tupleOrArrayLikeTargetParts, iterationType](
				JsxElaborationElement e) -> bool {
		Node* prop = e.errorNode;
		Node* next = e.innerExpression;
		Type* nameType = e.nameType;
		Type* targetPropType = iterationType;
		Type* targetIndexedPropType = nullptr;
		if (tupleOrArrayLikeTargetParts != neverType) {
			targetIndexedPropType = getBestMatchIndexedAccessTypeOrUndefined(
				source, tupleOrArrayLikeTargetParts, nameType);
		}
		if (targetIndexedPropType != nullptr &&
			(targetIndexedPropType->flags & TypeFlagsIndexedAccess) == 0) {
			if (iterationType != nullptr) {
				targetPropType = getUnionType({iterationType, targetIndexedPropType});
			} else {
				targetPropType = targetIndexedPropType;
			}
		}
		if (targetPropType == nullptr) {
			return true; // continue
		}
		Type* sourcePropType = getIndexedAccessTypeOrUndefined(source, nameType,
			AccessFlagsNone, nullptr, nullptr);
		if (sourcePropType == nullptr) {
			return true; // continue
		}
		std::string propName = getPropertyNameFromIndex(nameType, nullptr /*accessNode*/);
		if (!checkTypeRelatedTo(sourcePropType, targetPropType, relation,
				nullptr /*errorNode*/)) {
			bool elaborated = next != nullptr &&
				elaborateError(next, sourcePropType, targetPropType, relation,
					nullptr /*headMessage*/, diagnosticOutput);
			reportedError = true;
			if (!elaborated) {
				// Issue error on the prop itself, since the prop couldn't elaborate the error. Use the expression type, if available.
				Type* specificSource = sourcePropType;
				if (next != nullptr) {
					specificSource = checkExpressionForMutableLocationWithContextualType(
						next, sourcePropType);
				}
				if (e.createDiagnostic != nullptr) {
					// Use the custom diagnostic factory if provided (e.g., for JSX text children with dynamic error messages)
					reportDiagnostic(e.createDiagnostic(prop), diagnosticOutput);
				} else if (exactOptionalPropertyTypes &&
					isExactOptionalPropertyMismatch(specificSource, targetPropType)) {
					Diagnostic* diag = createDiagnosticForNode(prop,
						Type_0_is_not_assignable_to_type_1_with_exactOptionalPropertyTypes_Colon_true_Consider_adding_undefined_to_the_type_of_the_target,
						{TypeToString(specificSource), TypeToString(targetPropType)});
					reportDiagnostic(diag, diagnosticOutput);
				} else {
					Symbol* targetProp = propName != InternalSymbolNameMissing
						? getPropertyOfType(tupleOrArrayLikeTargetParts, propName)
						: nullptr;
					bool targetIsOptional =
						propName != InternalSymbolNameMissing &&
						((targetProp ? targetProp : unknownSymbol)->flags &
							SymbolFlagsOptional) != 0;
					Symbol* sourceProp = propName != InternalSymbolNameMissing
						? getPropertyOfType(source, propName)
						: nullptr;
					bool sourceIsOptional =
						propName != InternalSymbolNameMissing &&
						((sourceProp ? sourceProp : unknownSymbol)->flags &
							SymbolFlagsOptional) != 0;
					targetPropType = removeMissingType(targetPropType, targetIsOptional);
					sourcePropType =
						removeMissingType(sourcePropType, targetIsOptional && sourceIsOptional);
					bool result = checkTypeRelatedToEx(specificSource, targetPropType,
						relation, prop, nullptr, diagnosticOutput);
					if (result && specificSource != sourcePropType) {
						// If for whatever reason the expression type doesn't yield an error, make sure we still issue an error on the sourcePropType
						checkTypeRelatedToEx(sourcePropType, targetPropType, relation,
							prop, nullptr, diagnosticOutput);
					}
				}
			}
		}
		return true;
	});
	return reportedError;
}

// jsx.go:485
Symbol* Checker::getSuggestedSymbolForNonexistentJSXAttribute(
	const std::string& name, Type* containingType) {
	std::vector<Symbol*> properties = getPropertiesOfType(containingType);
	Symbol* jsxSpecific = nullptr;
	if (name == "for") {
		auto it = std::find_if(properties.begin(), properties.end(),
			[](Symbol* x) { return symbolName(x) == "htmlFor"; });
		if (it != properties.end()) {
			jsxSpecific = *it;
		}
	} else if (name == "class") {
		auto it = std::find_if(properties.begin(), properties.end(),
			[](Symbol* x) { return symbolName(x) == "className"; });
		if (it != properties.end()) {
			jsxSpecific = *it;
		}
	}
	if (jsxSpecific != nullptr) {
		return jsxSpecific;
	}
	return getSpellingSuggestionForName(name, properties, SymbolFlagsValue);
}

// jsx.go:500
Type* Checker::getJSXFragmentType(Node* node) {
	// An opening fragment is required in order for `getJsxNamespace` to give the fragment factory
	SourceFileLinks* links = sourceFileLinks.Get(getSourceFileOfNode(node));
	if (links->jsxFragmentType != nullptr) {
		return links->jsxFragmentType;
	}
	std::string jsxFragmentFactoryName = getJsxNamespace(node);
	// #38720/60122, allow null as jsxFragmentFactory
	bool shouldResolveFactoryReference =
		(compilerOptions->Jsx == JsxEmit::React ||
			compilerOptions->JsxFragmentFactory != "") &&
		jsxFragmentFactoryName != "null";
	if (!shouldResolveFactoryReference) {
		links->jsxFragmentType = anyType;
		return links->jsxFragmentType;
	}
	Symbol* jsxFactorySymbol = getJsxNamespaceContainerForImplicitImport(node);
	if (jsxFactorySymbol == nullptr) {
		bool shouldModuleRefErr = compilerOptions->Jsx != JsxEmit::Preserve &&
			compilerOptions->Jsx != JsxEmit::ReactNative;
		SymbolFlags flags = SymbolFlagsValue;
		if (!shouldModuleRefErr) {
			flags &= ~SymbolFlagsEnum;
		}
		jsxFactorySymbol = resolveName(node, jsxFragmentFactoryName, flags,
			Using_JSX_fragments_requires_fragment_factory_0_to_be_in_scope_but_it_could_not_be_found,
			true /*isUse*/, false /*excludeGlobals*/);
	}
	if (jsxFactorySymbol == nullptr) {
		links->jsxFragmentType = errorType;
		return links->jsxFragmentType;
	}
	if (jsxFactorySymbol->data->name == ReactNames.Fragment) {
		links->jsxFragmentType = getTypeOfSymbol(jsxFactorySymbol);
		return links->jsxFragmentType;
	}
	Symbol* resolvedAlias = jsxFactorySymbol;
	if ((jsxFactorySymbol->flags & SymbolFlagsAlias) != 0) {
		resolvedAlias = resolveAlias(jsxFactorySymbol);
	}

	const SymbolTable& reactExports = getExportsOfSymbol(resolvedAlias);
	Symbol* typeSymbol = getSymbol(reactExports, ReactNames.Fragment,
		SymbolFlagsBlockScopedVariable);
	if (typeSymbol != nullptr) {
		links->jsxFragmentType = getTypeOfSymbol(typeSymbol);
	} else {
		links->jsxFragmentType = errorType;
	}
	return links->jsxFragmentType;
}

// jsx.go:545
Signature* Checker::resolveJsxOpeningLikeElement(Node* node,
	std::vector<Signature*>* candidatesOutArray, CheckMode checkMode) {
	bool isJsxOpenFragment = isJsxOpeningFragment(node);
	Type* exprTypes = nullptr;
	if (!isJsxOpenFragment) {
		if (isJsxIntrinsicTagName(node->tagName())) {
			Type* result = getIntrinsicAttributesTypeFromJsxOpeningLikeElement(node);
			Signature* fakeSignature = createSignatureForJSXIntrinsic(node, result);
			checkTypeAssignableToAndOptionallyElaborate(
				checkExpressionWithContextualType(node->attributes(),
					getEffectiveFirstArgumentForJsxSignature(fakeSignature, node),
					nullptr /*inferenceContext*/, CheckModeNormal),
				result, node->tagName(), node->attributes(), nullptr, nullptr);
			auto typeArguments = node->typeArguments();
			if (!typeArguments.empty()) {
				checkSourceElements(typeArguments);
				SourceFile* sourceFile = getSourceFileOfNode(node);
				NodeList* typeArgumentList = node->typeArgumentList();
				TextRange loc{skipTrivia(sourceFile->text, typeArgumentList->loc.pos()),
					typeArgumentList->loc.end()};
				addDiagnostic(newDiagnostic(sourceFile, loc,
					Expected_0_type_arguments_but_got_1,
					{std::to_string(0), std::to_string(typeArguments.size())}));
			}
			return fakeSignature;
		}
		exprTypes = checkExpression(node->tagName());
	} else {
		exprTypes = getJSXFragmentType(node);
	}
	Type* apparentType = getApparentType(exprTypes);
	if (isErrorType(apparentType)) {
		return resolveErrorCall(node);
	}
	std::vector<Signature*> signatures =
		getUninstantiatedJsxSignaturesOfType(exprTypes, node);
	if (isUntypedFunctionCall(exprTypes, apparentType,
			static_cast<int>(signatures.size()), 0 /*constructSignatures*/)) {
		return resolveUntypedCall(node);
	}
	if (signatures.empty()) {
		// We found no signatures at all, which is an error
		if (isJsxOpenFragment) {
			error(node, JSX_element_type_0_does_not_have_any_construct_or_call_signatures,
				{getTextOfNode(node)});
		} else {
			error(node->tagName(),
				JSX_element_type_0_does_not_have_any_construct_or_call_signatures,
				{getTextOfNode(node->tagName())});
		}
		return resolveErrorCall(node);
	}
	return resolveCall(node, signatures, candidatesOutArray, checkMode,
		SignatureFlagsNone, nullptr);
}

// Check if the given signature can possibly be a signature called by the JSX opening-like element.
// @param node a JSX opening-like element we are trying to figure its call signature
// @param signature a candidate signature we are trying whether it is a call signature
// @param relation a relationship to check parameter and argument type
// jsx.go:591
bool Checker::checkApplicableSignatureForJsxCallLikeElement(Node* node,
	Signature* signature, Relation* relation, CheckMode checkMode, bool reportErrors,
	std::vector<Diagnostic*>* diagnosticOutput) {
	// Stateless function components can have maximum of three arguments: "props", "context", and "updater".
	// However "context" and "updater" are implicit and can't be specify by users. Only the first parameter, props,
	// can be specified by users through attributes property.
	Type* paramType = getEffectiveFirstArgumentForJsxSignature(signature, node);
	Type* attributesType = nullptr;
	if (isJsxOpeningFragment(node)) {
		attributesType = createJsxAttributesTypeFromAttributesProperty(node, CheckModeNormal);
	} else {
		attributesType = checkExpressionWithContextualType(node->attributes(),
			paramType, nullptr /*inferenceContext*/, checkMode);
	}
	Type* checkAttributesType = nullptr;
	std::function<bool()> checkTagNameDoesNotExpectTooManyArguments =
		[this, node, reportErrors, diagnosticOutput]() -> bool {
		if (getJsxNamespaceContainerForImplicitImport(node) != nullptr) {
			return true; // factory is implicitly jsx/jsxdev - assume it fits the bill, since we don't strongly look for the jsx/jsxs/jsxDEV factory APIs anywhere else (at least not yet)
		}
		// We assume fragments have the correct arity since the node does not have attributes
		Type* tagType = nullptr;
		if ((isJsxOpeningElement(node) || isJsxSelfClosingElement(node)) &&
			!(isJsxIntrinsicTagName(node->tagName()) ||
				isJsxNamespacedName(node->tagName()))) {
			tagType = checkExpression(node->tagName());
		}
		if (tagType == nullptr) {
			return true;
		}
		std::vector<Signature*> tagCallSignatures =
			getSignaturesOfType(tagType, SignatureKind::Call);
		if (tagCallSignatures.empty()) {
			return true;
		}
		Node* factory = getJsxFactoryEntity(node);
		if (factory == nullptr) {
			return true;
		}
		Symbol* factorySymbol = resolveEntityName(factory, SymbolFlagsValue,
			true /*ignoreErrors*/, false /*dontResolveAlias*/, node);
		if (factorySymbol == nullptr) {
			return true;
		}

		Type* factoryType = getTypeOfSymbol(factorySymbol);
		std::vector<Signature*> callSignatures =
			getSignaturesOfType(factoryType, SignatureKind::Call);
		if (callSignatures.empty()) {
			return true;
		}
		bool hasFirstParamSignatures = false;
		int maxParamCount = 0;
		// Check that _some_ first parameter expects a FC-like thing, and that some overload of the SFC expects an acceptable number of arguments
		for (Signature* sig : callSignatures) {
			Type* firstparam = getTypeAtPosition(sig, 0);
			std::vector<Signature*> signaturesOfParam =
				getSignaturesOfType(firstparam, SignatureKind::Call);
			if (signaturesOfParam.empty()) {
				continue;
			}
			for (Signature* paramSig : signaturesOfParam) {
				hasFirstParamSignatures = true;
				if (hasEffectiveRestParameter(paramSig)) {
					return true; // some signature has a rest param, so function components can have an arbitrary number of arguments
				}
				int paramCount = getParameterCount(paramSig);
				if (paramCount > maxParamCount) {
					maxParamCount = paramCount;
				}
			}
		}
		if (!hasFirstParamSignatures) {
			// Not a single signature had a first parameter which expected a signature - for back compat, and
			// to guard against generic factories which won't have signatures directly, do not error
			return true;
		}
		int absoluteMinArgCount = std::numeric_limits<int>::max();
		for (Signature* tagSig : tagCallSignatures) {
			int tagRequiredArgCount = getMinArgumentCount(tagSig);
			if (tagRequiredArgCount < absoluteMinArgCount) {
				absoluteMinArgCount = tagRequiredArgCount;
			}
		}
		if (absoluteMinArgCount <= maxParamCount) {
			return true; // some signature accepts the number of arguments the function component provides
		}
		if (reportErrors) {
			Node* tagName = node->tagName();
			// We will not report errors in this function for fragments, since we do not check them in this function
			Diagnostic* diag = NewDiagnosticForNode(tagName,
				Tag_0_expects_at_least_1_arguments_but_the_JSX_factory_2_provides_at_most_3,
				{entityNameToString(tagName), std::to_string(absoluteMinArgCount),
					entityNameToString(factory), std::to_string(maxParamCount)});
			Symbol* tagNameSymbol = getSymbolAtLocation(tagName, false);
			if (tagNameSymbol != nullptr && tagNameSymbol->data->valueDeclaration != nullptr) {
				diag->AddRelatedInfo(NewDiagnosticForNode(tagNameSymbol->data->valueDeclaration,
					X_0_is_declared_here, {entityNameToString(tagName)}));
			}
			reportDiagnostic(diag, diagnosticOutput);
		}
		return false;
	};
	if ((checkMode & CheckModeSkipContextSensitive) != 0) {
		checkAttributesType = getRegularTypeOfObjectLiteral(attributesType);
	} else {
		checkAttributesType = attributesType;
	}
	if (!checkTagNameDoesNotExpectTooManyArguments()) {
		return false;
	}
	Node* errorNode = nullptr;
	if (reportErrors) {
		if (isJsxOpeningFragment(node)) {
			errorNode = node;
		} else {
			errorNode = node->tagName();
		}
	}
	Node* attributes = nullptr;
	if (!isJsxOpeningFragment(node)) {
		attributes = node->attributes();
	}
	return checkTypeRelatedToAndOptionallyElaborate(checkAttributesType, paramType,
		relation, errorNode, attributes, nullptr, diagnosticOutput);
}

// Get attributes type of the JSX opening-like element. The result is from resolving "attributes" property of the opening-like element.
//
// @param openingLikeElement a JSX opening-like element
// @param filter a function to remove attributes that will not participate in checking whether attributes are assignable
// @return an anonymous type (similar to the one returned by checkObjectLiteral) in which its properties are attributes property.
// @remarks Because this function calls getSpreadType, it needs to use the same checks as checkObjectLiteral,
// which also calls getSpreadType.
// jsx.go:710
Type* Checker::createJsxAttributesTypeFromAttributesProperty(
	Node* openingLikeElement, CheckMode checkMode) {
	std::optional<SymbolTable> allAttributesTable;
	if (strictNullChecks) {
		allAttributesTable.emplace();
	}
	SymbolTable attributesTable;
	Symbol* attributesSymbol = nullptr;
	Node* attributeParent = openingLikeElement;
	Type* spread = emptyJsxObjectType;
	bool hasSpreadAnyType = false;
	Type* typeToIntersect = nullptr;
	bool explicitlySpecifyChildrenAttribute = false;
	ObjectFlags objectFlags = ObjectFlagsJsxAttributes;
	std::function<Type*()> createJsxAttributesType =
		[this, &attributesSymbol, &attributesTable, &objectFlags]() -> Type* {
		objectFlags |= ObjectFlagsFreshLiteral;
		Type* result = newAnonymousType(attributesSymbol, attributesTable, {}, {}, {});
		result->objectFlags |=
			objectFlags | ObjectFlagsObjectLiteral | ObjectFlagsContainsObjectOrArrayLiteral;
		return result;
	};
	std::string jsxChildrenPropertyName =
		getJsxElementChildrenPropertyName(getJsxNamespaceAt(openingLikeElement));
	bool isJsxOpenFragment = isJsxOpeningFragment(openingLikeElement);
	if (!isJsxOpenFragment) {
		Node* attributes = openingLikeElement->attributes();
		attributesSymbol = attributes->symbol();
		attributeParent = attributes;
		Type* contextualType = getContextualType(attributes, ContextFlagsNone);
		// Create anonymous type from given attributes symbol table.
		// @param symbol a symbol of JsxAttributes containing attributes corresponding to attributesTable
		// @param attributesTable a symbol table of attributes property
		for (Node* attributeDecl : attributes->properties()) {
			Symbol* member = attributeDecl->symbol();
			if (isJsxAttribute(attributeDecl)) {
				Type* exprType = checkJsxAttribute(attributeDecl, checkMode);
				objectFlags |= exprType->objectFlags & ObjectFlagsPropagatingFlags;
				Symbol* attributeSymbol =
					newSymbol(SymbolFlagsProperty | member->flags, member->data->name);
				attributeSymbol->data->declarations = member->data->declarations;
				attributeSymbol->data->parent = member->data->parent;
				if (member->data->valueDeclaration != nullptr) {
					attributeSymbol->data->valueDeclaration = member->data->valueDeclaration;
				}
				ValueSymbolLinks* links = valueSymbolLinks.Get(attributeSymbol);
				links->resolvedType = exprType;
				links->target = member;
				attributesTable[attributeSymbol->data->name] = attributeSymbol;
				if (allAttributesTable.has_value()) {
					(*allAttributesTable)[attributeSymbol->data->name] = attributeSymbol;
				}
				if (attributeDecl->name()->text() == jsxChildrenPropertyName) {
					explicitlySpecifyChildrenAttribute = true;
				}
				if (contextualType != nullptr &&
					(checkMode & CheckModeInferential) != 0 &&
					(checkMode & CheckModeSkipContextSensitive) == 0 &&
					isContextSensitive(attributeDecl)) {
					InferenceContext* inferenceContext = getInferenceContext(attributes);
					debugAssert(inferenceContext != nullptr);
					// In CheckMode.Inferential we should always have an inference context
					Node* inferenceNode = attributeDecl->initializer()->expression();
					addIntraExpressionInferenceSite(inferenceContext, inferenceNode, exprType);
				}
			} else {
				debugAssert(attributeDecl->kind == Kind::JsxSpreadAttribute);
				if (!attributesTable.empty()) {
					// createJsxAttributesType() mutates objectFlags (adds FreshLiteral);
					// evaluate it before reading objectFlags — Go's L->R arg order.
					Type* attributesType = createJsxAttributesType();
					spread = getSpreadType(spread, attributesType,
						attributesSymbol, objectFlags, false /*readonly*/);
					attributesTable.clear();
				}
				Type* exprType = getReducedType(checkExpressionEx(
					attributeDecl->expression(), checkMode & CheckModeInferential));
				if (isTypeAny(exprType)) {
					hasSpreadAnyType = true;
				}
				if (isValidSpreadType(exprType)) {
					spread = getSpreadType(spread, exprType, attributesSymbol,
						objectFlags, false /*readonly*/);
					if (allAttributesTable.has_value()) {
						checkSpreadPropOverrides(exprType, *allAttributesTable,
							attributeDecl);
					}
				} else {
					error(attributeDecl->expression(),
						Spread_types_may_only_be_created_from_object_types);
					if (typeToIntersect != nullptr) {
						typeToIntersect = getIntersectionType({typeToIntersect, exprType});
					} else {
						typeToIntersect = exprType;
					}
				}
			}
		}
		if (!hasSpreadAnyType) {
			if (!attributesTable.empty()) {
				Type* attributesType = createJsxAttributesType();
				spread = getSpreadType(spread, attributesType,
					attributesSymbol, objectFlags, false /*readonly*/);
			}
		}
	}
	std::function<bool(Node*)> parentHasSemanticJsxChildren =
		[](Node* openingLikeElement) -> bool {
		// Handle children attribute
		Node* parent = openingLikeElement->parent;
		if (parent == nullptr) {
			return false;
		}
		std::vector<Node*> children;

		if (isJsxElement(parent)) {
			// We have to check that openingElement of the parent is the one we are visiting as this may not be true for selfClosingElement
			if (parent->as<JsxElement>()->OpeningElement == openingLikeElement) {
				children = parent->children()->nodes;
			}
		} else if (isJsxFragment(parent)) {
			if (parent->as<JsxFragment>()->OpeningFragment == openingLikeElement) {
				children = parent->children()->nodes;
			}
		}
		return !getSemanticJsxChildren(children).empty();
	};
	if (parentHasSemanticJsxChildren(openingLikeElement)) {
		std::vector<Type*> childTypes =
			checkJsxChildren(openingLikeElement->parent, checkMode);
		if (!hasSpreadAnyType && jsxChildrenPropertyName != InternalSymbolNameMissing &&
			jsxChildrenPropertyName != "") {
			// Error if there is a attribute named "children" explicitly specified and children element.
			// This is because children element will overwrite the value from attributes.
			// Note: we will not warn "children" attribute overwritten if "children" attribute is specified in object spread.
			if (explicitlySpecifyChildrenAttribute) {
				error(attributeParent,
					X_0_are_specified_twice_The_attribute_named_0_will_be_overwritten,
					{jsxChildrenPropertyName});
			}
			Type* childrenContextualType = nullptr;
			if (isJsxOpeningElement(openingLikeElement)) {
				if (Type* contextualType = getApparentTypeOfContextualType(
						openingLikeElement->attributes(), ContextFlagsNone);
					contextualType != nullptr) {
					childrenContextualType = getTypeOfPropertyOfContextualType(
						contextualType, jsxChildrenPropertyName);
				}
			}
			// If there are children in the body of JSX element, create dummy attribute "children" with the union of children types so that it will pass the attribute checking process
			Symbol* childrenPropSymbol =
				newSymbol(SymbolFlagsProperty, jsxChildrenPropertyName);
			ValueSymbolLinks* links = valueSymbolLinks.Get(childrenPropSymbol);
			if (childTypes.size() == 1) {
				links->resolvedType = childTypes[0];
			} else if (childrenContextualType != nullptr &&
				someType(childrenContextualType,
					[this](Type* t) { return isTupleLikeType(t); })) {
				links->resolvedType = createTupleType(childTypes);
			} else {
				links->resolvedType = createArrayType(getUnionType(childTypes));
			}
			// Fake up a property declaration for the children
			childrenPropSymbol->data->valueDeclaration = factory.newPropertySignatureDeclaration(
				nullptr, factory.newIdentifier(jsxChildrenPropertyName),
				nullptr /*postfixToken*/, nullptr /*type*/, nullptr /*initializer*/);
			childrenPropSymbol->data->valueDeclaration->parent = attributeParent;
			childrenPropSymbol->data->valueDeclaration->as<PropertySignatureDeclaration>()
				->Symbol = childrenPropSymbol;
			SymbolTable childPropMap;
			childPropMap[jsxChildrenPropertyName] = childrenPropSymbol;
			spread = getSpreadType(spread,
				newAnonymousType(attributesSymbol, childPropMap, {}, {}, {}),
				attributesSymbol,
				objectFlags | getPropagatingFlagsOfTypes(childTypes, TypeFlagsNone),
				false /*readonly*/);
		}
	}
	if (hasSpreadAnyType) {
		return anyType;
	}
	if (typeToIntersect != nullptr) {
		if (spread != emptyJsxObjectType) {
			return getIntersectionType({typeToIntersect, spread});
		}
		return typeToIntersect;
	}
	if (spread == emptyJsxObjectType) {
		return createJsxAttributesType();
	}
	return spread;
}

// jsx.go:869
Type* Checker::checkJsxAttribute(Node* node, CheckMode checkMode) {
	if (node->initializer() != nullptr) {
		return checkExpressionForMutableLocation(node->initializer(), checkMode);
	}
	// <Elem attr /> is sugar for <Elem attr={true} />
	return trueType;
}

// jsx.go:877
std::vector<Type*> Checker::checkJsxChildren(Node* node, CheckMode checkMode) {
	std::vector<Type*> childTypes;
	for (Node* child : node->children()->nodes) {
		// In React, JSX text that contains only whitespaces will be ignored so we don't want to type-check that
		// because then type of children property will have constituent of string type.
		if (isJsxText(child)) {
			if (!child->as<JsxText>()->ContainsOnlyTriviaWhiteSpaces) {
				childTypes.push_back(stringType);
			}
		} else if (isJsxExpression(child) && child->expression() == nullptr) {
			// empty jsx expressions don't *really* count as present children
			continue;
		} else {
			childTypes.push_back(checkExpressionForMutableLocation(child, checkMode));
		}
	}
	return childTypes;
}

// jsx.go:896
std::vector<Signature*> Checker::getUninstantiatedJsxSignaturesOfType(
	Type* elementType, Node* caller) {
	if ((elementType->flags & TypeFlagsString) != 0) {
		return {anySignature};
	}
	if ((elementType->flags & TypeFlagsStringLiteral) != 0) {
		Type* intrinsicType =
			getIntrinsicAttributesTypeFromStringLiteralType(elementType, caller);
		if (intrinsicType == nullptr) {
			error(caller, Property_0_does_not_exist_on_type_1,
				{getStringLiteralValue(elementType),
					"JSX." + std::string(JsxNames.IntrinsicElements)});
			return {};
		}
		Signature* fakeSignature = createSignatureForJSXIntrinsic(caller, intrinsicType);
		return {fakeSignature};
	}
	Type* apparentElemType = getApparentType(elementType);
	// Resolve the signatures, preferring constructor
	std::vector<Signature*> signatures =
		getSignaturesOfType(apparentElemType, SignatureKind::Construct);
	if (signatures.empty()) {
		// No construct signatures, try call signatures
		signatures = getSignaturesOfType(apparentElemType, SignatureKind::Call);
	}
	if (signatures.empty() && (apparentElemType->flags & TypeFlagsUnion) != 0) {
		// If each member has some combination of new/call signatures; make a union signature list for those
		std::vector<std::vector<Signature*>> signatureLists;
		for (Type* t : apparentElemType->types()) {
			signatureLists.push_back(getUninstantiatedJsxSignaturesOfType(t, caller));
		}
		signatures = getUnionSignatures(signatureLists);
	}
	return signatures;
}

// jsx.go:925
Type* Checker::getEffectiveFirstArgumentForJsxSignature(Signature* signature,
	Node* node) {
	if (isJsxOpeningFragment(node) ||
		getJsxReferenceKind(node) != JsxReferenceKind::Component) {
		return getJsxPropsTypeFromCallSignature(signature, node);
	}
	return getJsxPropsTypeFromClassType(signature, node);
}

// jsx.go:932
Type* Checker::getJsxPropsTypeFromCallSignature(Signature* sig, Node* context) {
	Type* propsType = getTypeOfFirstParameterOfSignatureWithFallback(sig, unknownType);
	propsType = getJsxManagedAttributesFromLocatedAttributes(context,
		getJsxNamespaceAt(context), propsType);
	Type* intrinsicAttribs = getJsxType(JsxNames.IntrinsicAttributes, context);
	if (!isErrorType(intrinsicAttribs)) {
		propsType = intersectTypes(intrinsicAttribs, propsType);
	}
	return propsType;
}

// jsx.go:942
Type* Checker::getJsxPropsTypeFromClassType(Signature* sig, Node* context) {
	Symbol* ns = getJsxNamespaceAt(context);
	std::string forcedLookupLocation = getJsxElementPropertiesName(ns);
	Type* attributesType = nullptr;
	if (forcedLookupLocation == InternalSymbolNameMissing) {
		attributesType = getTypeOfFirstParameterOfSignatureWithFallback(sig, unknownType);
	} else if (forcedLookupLocation == "") {
		attributesType = getReturnTypeOfSignature(sig);
	} else {
		attributesType = getJsxPropsTypeForSignatureFromMember(sig, forcedLookupLocation);
		if (attributesType == nullptr && !context->attributes()->properties().empty()) {
			// There is no property named 'props' on this instance type
			error(context,
				JSX_element_class_does_not_support_attributes_because_it_does_not_have_a_0_property,
				{forcedLookupLocation});
		}
	}
	if (attributesType == nullptr) {
		return unknownType;
	}
	attributesType =
		getJsxManagedAttributesFromLocatedAttributes(context, ns, attributesType);
	if (isTypeAny(attributesType)) {
		// Props is of type 'any' or unknown
		return attributesType;
	}
	// Normal case -- add in IntrinsicClassAttributes<T> and IntrinsicAttributes
	Type* apparentAttributesType = attributesType;
	Type* intrinsicClassAttribs = getJsxType(JsxNames.IntrinsicClassAttributes, context);
	if (!isErrorType(intrinsicClassAttribs)) {
		std::vector<Type*> typeParams =
			getLocalTypeParametersOfClassOrInterfaceOrTypeAlias(
				intrinsicClassAttribs->symbol);
		Type* hostClassType = getReturnTypeOfSignature(sig);
		Type* libraryManagedAttributeType;
		if (!typeParams.empty()) {
			// apply JSX.IntrinsicClassAttributes<hostClassType, ...>
			std::vector<Type*> inferredArgs = fillMissingTypeArguments({hostClassType},
				typeParams, getMinTypeArgumentCount(typeParams), isInJSFile(context));
			libraryManagedAttributeType = instantiateType(intrinsicClassAttribs,
				newTypeMapper(typeParams, inferredArgs));
		} else {
			libraryManagedAttributeType = intrinsicClassAttribs;
		}
		apparentAttributesType =
			intersectTypes(libraryManagedAttributeType, apparentAttributesType);
	}
	Type* intrinsicAttribs = getJsxType(JsxNames.IntrinsicAttributes, context);
	if (!isErrorType(intrinsicAttribs)) {
		apparentAttributesType = intersectTypes(intrinsicAttribs, apparentAttributesType);
	}
	return apparentAttributesType;
}

// jsx.go:989
Type* Checker::getJsxPropsTypeForSignatureFromMember(Signature* sig,
	const std::string& forcedLookupLocation) {
	if (sig->composite != nullptr) {
		// JSX Elements using the legacy `props`-field based lookup (eg, react class components) need to treat the `props` member as an input
		// instead of an output position when resolving the signature. We need to go back to the input signatures of the composite signature,
		// get the type of `props` on each return type individually, and then _intersect them_, rather than union them (as would normally occur
		// for a union signature). It's an unfortunate quirk of looking in the output of the signature for the type we want to use for the input.
		// The default behavior of `getTypeOfFirstParameterOfSignatureWithFallback` when no `props` member name is defined is much more sane.
		std::vector<Type*> results;
		for (Signature* signature : sig->composite->signatures) {
			Type* instance = getReturnTypeOfSignature(signature);
			if (isTypeAny(instance)) {
				return instance;
			}
			Type* propType = getTypeOfPropertyOfType(instance, forcedLookupLocation);
			if (propType == nullptr) {
				return nullptr;
			}
			results.push_back(propType);
		}
		return getIntersectionType(results);
		// Same result for both union and intersection signatures
	}
	Type* instanceType = getReturnTypeOfSignature(sig);
	if (isTypeAny(instanceType)) {
		return instanceType;
	}
	return getTypeOfPropertyOfType(instanceType, forcedLookupLocation);
}

// jsx.go:1018
Type* Checker::getJsxManagedAttributesFromLocatedAttributes(Node* context,
	Symbol* ns, Type* attributesType) {
	Symbol* managedSym = getJsxLibraryManagedAttributes(ns);
	if (managedSym != nullptr) {
		Type* ctorType = getStaticTypeOfReferencedJsxConstructor(context);
		Type* result = instantiateAliasOrInterfaceWithDefaults(managedSym,
			{ctorType, attributesType}, isInJSFile(context));
		if (result != nullptr) {
			return result;
		}
	}
	return attributesType;
}

// jsx.go:1030
Type* Checker::instantiateAliasOrInterfaceWithDefaults(Symbol* managedSym,
	const std::vector<Type*>& typeArguments, bool inJavaScript) {
	Type* declaredManagedType = getDeclaredTypeOfSymbol(managedSym);
	// fetches interface type, or initializes symbol links type parameters
	if ((managedSym->flags & SymbolFlagsTypeAlias) != 0) {
		std::vector<Type*> params = typeAliasLinks.Get(managedSym)->typeParameters;
		if (params.size() >= typeArguments.size()) {
			std::vector<Type*> args = fillMissingTypeArguments(typeArguments, params,
				static_cast<int>(typeArguments.size()), inJavaScript);
			if (args.empty()) {
				return declaredManagedType;
			}
			return getTypeAliasInstantiation(managedSym, args, nullptr);
		}
	}
	if ((declaredManagedType->objectFlags & ObjectFlagsClassOrInterface) != 0 &&
		interfaceTypeTypeParameters(declaredManagedType->AsInterfaceType()).size() >=
			typeArguments.size()) {
		std::vector<Type*> args = fillMissingTypeArguments(typeArguments,
			interfaceTypeTypeParameters(declaredManagedType->AsInterfaceType()),
			static_cast<int>(typeArguments.size()), inJavaScript);
		return createTypeReference(declaredManagedType, args);
	}
	return nullptr;
}

// jsx.go:1050
Symbol* Checker::getJsxLibraryManagedAttributes(Symbol* jsxNamespace) {
	if (jsxNamespace != nullptr) {
		return getSymbol(jsxNamespace->data->exports, JsxNames.LibraryManagedAttributes,
			SymbolFlagsType);
	}
	return nullptr;
}

// jsx.go:1057
Symbol* Checker::getJsxElementTypeSymbol(Symbol* jsxNamespace) {
	// JSX.ElementType [symbol]
	if (jsxNamespace != nullptr) {
		return getSymbol(jsxNamespace->data->exports, JsxNames.ElementType, SymbolFlagsType);
	}
	return nullptr;
}

// e.g. "props" for React.d.ts,
// or InternalSymbolNameMissing if ElementAttributesProperty doesn't exist (which means all
//
//	non-intrinsic elements' attributes type is 'any'),
//
// or "" if it has 0 properties (which means every
//
//	non-intrinsic elements' attributes type is the element instance type)
// jsx.go:1073
std::string Checker::getJsxElementPropertiesName(Symbol* jsxNamespace) {
	return getNameFromJsxElementAttributesContainer(
		JsxNames.ElementAttributesPropertyNameContainer, jsxNamespace);
}

// jsx.go:1077
std::string Checker::getJsxElementChildrenPropertyName(Symbol* jsxNamespace) {
	if (compilerOptions->Jsx == JsxEmit::ReactJSX ||
		compilerOptions->Jsx == JsxEmit::ReactJSXDev) {
		// In these JsxEmit modes the children property is fixed to 'children'
		return "children";
	}
	return getNameFromJsxElementAttributesContainer(
		JsxNames.ElementChildrenAttributeNameContainer, jsxNamespace);
}

// Look into JSX namespace and then look for container with matching name as nameOfAttribPropContainer.
// Get a single property from that container if existed. Report an error if there are more than one property.
//
// @param nameOfAttribPropContainer a string of value JsxNames.ElementAttributesPropertyNameContainer or JsxNames.ElementChildrenAttributeNameContainer
//
//	if other string is given or the container doesn't exist, return undefined.
// jsx.go:1091
std::string Checker::getNameFromJsxElementAttributesContainer(
	const std::string& nameOfAttribPropContainer, Symbol* jsxNamespace) {
	// JSX.ElementAttributesProperty | JSX.ElementChildrenAttribute [symbol]
	if (jsxNamespace != nullptr) {
		Symbol* jsxElementAttribPropInterfaceSym = getSymbol(jsxNamespace->data->exports,
			nameOfAttribPropContainer, SymbolFlagsType);
		if (jsxElementAttribPropInterfaceSym != nullptr) {
			Type* jsxElementAttribPropInterfaceType =
				getDeclaredTypeOfSymbol(jsxElementAttribPropInterfaceSym);
			std::vector<Symbol*> propertiesOfJsxElementAttribPropInterface =
				getPropertiesOfType(jsxElementAttribPropInterfaceType);
			// Element Attributes has zero properties, so the element attributes type will be the class instance type
			if (propertiesOfJsxElementAttribPropInterface.empty()) {
				return "";
			}
			if (propertiesOfJsxElementAttribPropInterface.size() == 1) {
				return propertiesOfJsxElementAttribPropInterface[0]->data->name;
			}
			if (propertiesOfJsxElementAttribPropInterface.size() > 1 &&
				!jsxElementAttribPropInterfaceSym->data->declarations.empty()) {
				// More than one property on ElementAttributesProperty is an error
				error(jsxElementAttribPropInterfaceSym->data->declarations[0],
					The_global_type_JSX_0_may_not_have_more_than_one_property,
					{nameOfAttribPropContainer});
			}
		}
	}
	return InternalSymbolNameMissing;
}

// jsx.go:1114
Type* Checker::getStaticTypeOfReferencedJsxConstructor(Node* context) {
	if (isJsxOpeningFragment(context)) {
		return getJSXFragmentType(context);
	}
	if (isJsxIntrinsicTagName(context->tagName())) {
		Type* result = getIntrinsicAttributesTypeFromJsxOpeningLikeElement(context);
		Signature* fakeSignature = createSignatureForJSXIntrinsic(context, result);
		return getOrCreateTypeFromSignature(fakeSignature);
	}
	Type* tagType = checkExpressionCached(context->tagName());
	if ((tagType->flags & TypeFlagsStringLiteral) != 0) {
		Type* result = getIntrinsicAttributesTypeFromStringLiteralType(tagType, context);
		if (result == nullptr) {
			return errorType;
		}
		Signature* fakeSignature = createSignatureForJSXIntrinsic(context, result);
		return getOrCreateTypeFromSignature(fakeSignature);
	}
	return tagType;
}

// jsx.go:1135
Type* Checker::getIntrinsicAttributesTypeFromStringLiteralType(Type* t,
	Node* location) {
	// If the elemType is a stringLiteral type, we can then provide a check to make sure that the string literal type is one of the Jsx intrinsic element type
	// For example:
	//      var CustomTag: "h1" = "h1";
	//      <CustomTag> Hello World </CustomTag>
	Type* intrinsicElementsType = getJsxType(JsxNames.IntrinsicElements, location);
	if (!isErrorType(intrinsicElementsType)) {
		std::string stringLiteralTypeName = getStringLiteralValue(t);
		Symbol* intrinsicProp =
			getPropertyOfType(intrinsicElementsType, stringLiteralTypeName);
		if (intrinsicProp != nullptr) {
			return getTypeOfSymbol(intrinsicProp);
		}
		Type* indexSignatureType =
			getIndexTypeOfType(intrinsicElementsType, stringType);
		if (indexSignatureType != nullptr) {
			return indexSignatureType;
		}
		return nullptr;
	}
	// If we need to report an error, we already done so here. So just return any to prevent any more error downstream
	return anyType;
}

// jsx.go:1157
JsxReferenceKind Checker::getJsxReferenceKind(Node* node) {
	if (isJsxIntrinsicTagName(node->tagName())) {
		return JsxReferenceKind::Mixed;
	}
	Type* tagType = getApparentType(checkExpression(node->tagName()));
	if (!getSignaturesOfType(tagType, SignatureKind::Construct).empty()) {
		return JsxReferenceKind::Component;
	}
	if (!getSignaturesOfType(tagType, SignatureKind::Call).empty()) {
		return JsxReferenceKind::Function;
	}
	return JsxReferenceKind::Mixed;
}

// jsx.go:1171
Signature* Checker::createSignatureForJSXIntrinsic(Node* node, Type* result) {
	Type* elementType = errorType;
	if (Symbol* namespace_ = getJsxNamespaceAt(node); namespace_ != nullptr) {
		const SymbolTable& exports = getExportsOfSymbol(namespace_);
		if (Symbol* typeSymbol = getSymbol(exports, JsxNames.Element, SymbolFlagsType);
			typeSymbol != nullptr) {
			elementType = getDeclaredTypeOfSymbol(typeSymbol);
		}
	}
	// returnNode := typeSymbol && c.nodeBuilder.symbolToEntityName(typeSymbol, ast.SymbolFlagsType, node)
	// declaration := factory.createFunctionTypeNode(nil, []ParameterDeclaration{factory.createParameterDeclaration(nil, nil /*dotDotDotToken*/, "props", nil /*questionToken*/, c.nodeBuilder.typeToTypeNode(result, node))}, ifElse(returnNode != nil, factory.createTypeReferenceNode(returnNode, nil /*typeArguments*/), factory.createKeywordTypeNode(ast.KindAnyKeyword)))
	Symbol* parameterSymbol = newSymbol(SymbolFlagsFunctionScopedVariable, "props");
	valueSymbolLinks.Get(parameterSymbol)->resolvedType = result;
	return newSignature(SignatureFlagsNone, nullptr, {}, nullptr, {parameterSymbol},
		elementType, nullptr, 1);
}

// Get attributes type of the given intrinsic opening-like Jsx element by resolving the tag name.
// The function is intended to be called from a function which has checked that the opening element is an intrinsic element.
// @param node an intrinsic JSX opening-like element
// jsx.go:1188
Type* Checker::getIntrinsicAttributesTypeFromJsxOpeningLikeElement(Node* node) {
	debugAssert(isJsxIntrinsicTagName(node->tagName()));
	JsxElementLinks* links = jsxElementLinks.Get(node);
	if (links->resolvedJsxElementAttributesType != nullptr) {
		return links->resolvedJsxElementAttributesType;
	}
	Symbol* symbol = getIntrinsicTagSymbol(node);
	if ((links->jsxFlags & JsxFlagsIntrinsicNamedElement) != 0) {
		Type* symbolType = getTypeOfSymbol(symbol);
		links->resolvedJsxElementAttributesType =
			symbolType ? symbolType : errorType;
		return links->resolvedJsxElementAttributesType;
	}
	if ((links->jsxFlags & JsxFlagsIntrinsicIndexedElement) != 0) {
		IndexInfo* indexInfo = getApplicableIndexInfoForName(
			getJsxType(JsxNames.IntrinsicElements, node), node->tagName()->text());
		if (indexInfo != nullptr) {
			links->resolvedJsxElementAttributesType = indexInfo->valueType;
			return links->resolvedJsxElementAttributesType;
		}
	}
	links->resolvedJsxElementAttributesType = errorType;
	return links->resolvedJsxElementAttributesType;
}

// Looks up an intrinsic tag name and returns a symbol that either points to an intrinsic
// property (in which case nodeLinks.jsxFlags will be IntrinsicNamedElement) or an intrinsic
// string index signature (in which case nodeLinks.jsxFlags will be IntrinsicIndexedElement).
// May also return unknownSymbol if both of these lookups fail.
// jsx.go:1214
Symbol* Checker::getIntrinsicTagSymbol(Node* node) {
	SymbolNodeLinks* links = symbolNodeLinks.Get(node);
	if (links->resolvedSymbol != nullptr &&
		!staleForCheckFile(links->resolvedSymbolCheckFile)) {
		return links->resolvedSymbol;
	}
	links->resolvedSymbol = nullptr;
	links->resolvedSymbolCheckFile = checkFileTag();
	Type* intrinsicElementsType = getJsxType(JsxNames.IntrinsicElements, node);
	if (!isErrorType(intrinsicElementsType)) {
		// Property case
		Node* tagName = node->tagName();
		if (!isIdentifier(tagName) && !isJsxNamespacedName(tagName)) {
			TSC_UNREACHABLE("Invalid tag name");
		}
		std::string propName = tagName->text();
		Symbol* intrinsicProp = getPropertyOfType(intrinsicElementsType, propName);
		if (intrinsicProp != nullptr) {
			jsxElementLinks.Get(node)->jsxFlags |= JsxFlagsIntrinsicNamedElement;
			links->resolvedSymbol = intrinsicProp;
			return links->resolvedSymbol;
		}
		// Intrinsic string indexer case
		Symbol* indexSymbol = getApplicableIndexSymbol(intrinsicElementsType,
			getStringLiteralType(propName));
		if (indexSymbol != nullptr) {
			jsxElementLinks.Get(node)->jsxFlags |= JsxFlagsIntrinsicIndexedElement;
			links->resolvedSymbol = indexSymbol;
			return links->resolvedSymbol;
		}
		if (getTypeOfPropertyOrIndexSignatureOfType(intrinsicElementsType, propName) !=
			nullptr) {
			jsxElementLinks.Get(node)->jsxFlags |= JsxFlagsIntrinsicIndexedElement;
			links->resolvedSymbol = intrinsicElementsType->symbol;
			return links->resolvedSymbol;
		}
		// Wasn't found
		error(node, Property_0_does_not_exist_on_type_1,
			{tagName->text(), "JSX." + std::string(JsxNames.IntrinsicElements)});
		links->resolvedSymbol = unknownSymbol;
		return links->resolvedSymbol;
	}
	if (noImplicitAny) {
		error(node, JSX_element_implicitly_has_type_any_because_no_interface_JSX_0_exists,
			std::vector<std::string>{JsxNames.IntrinsicElements});
	}
	links->resolvedSymbol = unknownSymbol;
	return links->resolvedSymbol;
}

// jsx.go:1257
Type* Checker::getJsxStatelessElementTypeAt(Node* location) {
	Type* jsxElementType = getJsxElementTypeAt(location);
	if (jsxElementType == nullptr) {
		return nullptr;
	}
	return getUnionType({jsxElementType, nullType});
}

// jsx.go:1265
Type* Checker::getJsxElementClassTypeAt(Node* location) {
	Type* t = getJsxType(JsxNames.ElementClass, location);
	if (isErrorType(t)) {
		return nullptr;
	}
	return t;
}

// jsx.go:1273
Type* Checker::getJsxElementTypeAt(Node* location) {
	return getJsxType(JsxNames.Element, location);
}

// jsx.go:1277
Type* Checker::getJsxElementTypeTypeAt(Node* location) {
	Symbol* ns = getJsxNamespaceAt(location);
	if (ns == nullptr) {
		return nullptr;
	}
	Symbol* sym = getJsxElementTypeSymbol(ns);
	if (sym == nullptr) {
		return nullptr;
	}
	Type* t = instantiateAliasOrInterfaceWithDefaults(sym, {}, isInJSFile(location));
	if (t == nullptr || isErrorType(t)) {
		return nullptr;
	}
	return t;
}

// jsx.go:1293
Type* Checker::getJsxType(const std::string& name, Node* location) {
	if (Symbol* namespace_ = getJsxNamespaceAt(location); namespace_ != nullptr) {
		const SymbolTable& exports = getExportsOfSymbol(namespace_);
		if (!exports.empty()) {
			if (Symbol* typeSymbol = getSymbol(exports, name, SymbolFlagsType);
				typeSymbol != nullptr) {
				return getDeclaredTypeOfSymbol(typeSymbol);
			}
		}
	}
	return errorType;
}

// jsx.go:1304
Symbol* Checker::getJsxNamespaceAt(Node* location) {
	JsxElementLinks* links = nullptr;
	if (location != nullptr) {
		links = jsxElementLinks.Get(location);
	}
	if (links != nullptr && links->jsxNamespace != nullptr &&
		links->jsxNamespace != unknownSymbol) {
		return links->jsxNamespace;
	}
	if (links == nullptr || links->jsxNamespace != unknownSymbol) {
		Symbol* resolvedNamespace = getJsxNamespaceContainerForImplicitImport(location);
		if (resolvedNamespace == nullptr || resolvedNamespace == unknownSymbol) {
			std::string namespaceName = getJsxNamespace(location);
			resolvedNamespace = resolveName(location, namespaceName,
				SymbolFlagsNamespace, nullptr /*nameNotFoundMessage*/,
				false /*isUse*/, false /*excludeGlobals*/);
		}
		if (resolvedNamespace != nullptr) {
			const SymbolTable& exportsOfResolved =
				getExportsOfSymbol(resolveSymbol(resolvedNamespace));
			Symbol* candidate = resolveSymbol(
				getSymbol(exportsOfResolved, JsxNames.JSX, SymbolFlagsNamespace));
			if (candidate != nullptr && candidate != unknownSymbol) {
				if (links != nullptr) {
					links->jsxNamespace = candidate;
				}
				return candidate;
			}
		}
		if (links != nullptr) {
			links->jsxNamespace = unknownSymbol;
		}
	}
	// JSX global fallback
	Symbol* s =
		resolveSymbol(getGlobalSymbol(JsxNames.JSX, SymbolFlagsNamespace, nullptr /*diagnostic*/));
	if (s == unknownSymbol) {
		return nullptr;
	}
	return s;
}

// jsx.go:1339
std::string Checker::getJsxNamespace(Node* location) {
	if (location != nullptr) {
		SourceFile* file = getSourceFileOfNode(location);
		if (file != nullptr) {
			SourceFileLinks* links = sourceFileLinks.Get(file);
			if (isJsxOpeningFragment(location)) {
				if (links->localJsxFragmentNamespace != "") {
					return links->localJsxFragmentNamespace;
				}
				const Pragma* jsxFragmentPragma =
					getPragmaFromSourceFile(file, "jsxfrag");
				if (jsxFragmentPragma != nullptr) {
					links->localJsxFragmentFactory = parseIsolatedEntityName(
						getPragmaArgument(jsxFragmentPragma, "factory"));
					if (links->localJsxFragmentFactory != nullptr) {
						links->localJsxFragmentNamespace =
							getFirstIdentifier(links->localJsxFragmentFactory)->text();
						return links->localJsxFragmentNamespace;
					}
				}
				Node* entity = getJsxFragmentFactoryEntity(location);
				if (entity != nullptr) {
					links->localJsxFragmentFactory = entity;
					links->localJsxFragmentNamespace =
						getFirstIdentifier(entity)->text();
					return links->localJsxFragmentNamespace;
				}
			} else {
				std::string localJsxNamespace = getLocalJsxNamespace(file);
				if (localJsxNamespace != "") {
					links->localJsxNamespace = localJsxNamespace;
					return links->localJsxNamespace;
				}
			}
		}
	}
	if (_jsxNamespace == "") {
		_jsxNamespace = "React";
		if (compilerOptions->JsxFactory != "") {
			_jsxFactoryEntity = parseIsolatedEntityName(compilerOptions->JsxFactory);
			if (_jsxFactoryEntity != nullptr) {
				_jsxNamespace = getFirstIdentifier(_jsxFactoryEntity)->text();
			}
		} else if (compilerOptions->ReactNamespace != "") {
			_jsxNamespace = compilerOptions->ReactNamespace;
		}
	}
	if (_jsxFactoryEntity == nullptr) {
		_jsxFactoryEntity = factory.newQualifiedName(
			factory.newIdentifier(_jsxNamespace), factory.newIdentifier("createElement"));
	}
	return _jsxNamespace;
}

// jsx.go:1388
std::string Checker::getLocalJsxNamespace(SourceFile* file) {
	SourceFileLinks* links = sourceFileLinks.Get(file);
	if (links->localJsxNamespace != "") {
		return links->localJsxNamespace;
	}
	const Pragma* jsxPragma = getPragmaFromSourceFile(file, "jsx");
	if (jsxPragma != nullptr) {
		links->localJsxFactory =
			parseIsolatedEntityName(getPragmaArgument(jsxPragma, "factory"));
		if (links->localJsxFactory != nullptr) {
			links->localJsxNamespace = getFirstIdentifier(links->localJsxFactory)->text();
			return links->localJsxNamespace;
		}
	}
	return "";
}

// jsx.go:1404
Node* Checker::getJsxFactoryEntity(Node* location) {
	if (location != nullptr) {
		getJsxNamespace(location);
		if (Node* localJsxFactory =
				sourceFileLinks.Get(getSourceFileOfNode(location))->localJsxFactory;
			localJsxFactory != nullptr) {
			return localJsxFactory;
		}
	}
	return _jsxFactoryEntity;
}

// jsx.go:1414 — returns *ast.EntityName (a Node in the C++ port).
Node* Checker::getJsxFragmentFactoryEntity(Node* location) {
	if (location != nullptr) {
		SourceFile* file = getSourceFileOfNode(location);
		if (file != nullptr) {
			SourceFileLinks* links = sourceFileLinks.Get(file);
			if (links->localJsxFragmentFactory != nullptr) {
				return links->localJsxFragmentFactory;
			}
			const Pragma* jsxFragPragma = getPragmaFromSourceFile(file, "jsxfrag");
			if (jsxFragPragma != nullptr) {
				links->localJsxFragmentFactory = parseIsolatedEntityName(
					getPragmaArgument(jsxFragPragma, "factory"));
				return links->localJsxFragmentFactory;
			}
		}
	}
	if (compilerOptions->JsxFragmentFactory != "") {
		return parseIsolatedEntityName(compilerOptions->JsxFragmentFactory);
	}
	return nullptr;
}

// jsx.go:1435
Node* Checker::parseIsolatedEntityName(const std::string& name) {
	Node* result = tsc::parseIsolatedEntityName(name);
	if (result != nullptr) {
		markAsSynthetic(result);
	}
	return result;
}

// jsx.go:1449
Symbol* Checker::getJsxNamespaceContainerForImplicitImport(Node* location) {
	SourceFile* file = getSourceFileOfNode(location);
	JsxElementLinks* links = jsxElementLinks.Get(file->asNode());
	if (links->jsxImplicitImportContainer != nullptr) {
		return jsxIfElse<Symbol*>(links->jsxImplicitImportContainer == unknownSymbol,
			nullptr, links->jsxImplicitImportContainer);
	}
	Node* canonicalErrorTag = links->firstJSXTagInFile;
	if (canonicalErrorTag == nullptr) {
		std::function<bool(Node*)> visit;
		visit = [&visit, links](Node* node) -> bool {
			if (isJsxElement(node) || isJsxSelfClosingElement(node)) {
				links->firstJSXTagInFile = node;
				return true;
			}
			if (isJsxFragment(node)) {
				links->firstJSXTagInFile =
					node->as<JsxFragment>()->OpeningFragment; // to match strada, fragments issue errors on the opening fragment instead of the whole tag
				return true;
			}
			return node->forEachChild(visit);
		};
		file->forEachChild(visit);
		canonicalErrorTag = links->firstJSXTagInFile;
	}
	auto [moduleReference, specifier] = getJSXRuntimeImportSpecifier(file);
	if (moduleReference == "") {
		return nullptr;
	}
	const DiagnosticMessage* errorMessage =
		This_JSX_tag_requires_the_module_path_0_to_exist_but_none_could_be_found_Make_sure_you_have_types_for_the_appropriate_package_installed;
	Symbol* mod = resolveExternalModule(specifier ? specifier : canonicalErrorTag,
		moduleReference, errorMessage, canonicalErrorTag, false,
		nullptr /*importAttributesType*/);
	Symbol* result = nullptr;
	if (mod != nullptr && mod != unknownSymbol) {
		result = getMergedSymbol(resolveSymbol(mod));
	}
	links->jsxImplicitImportContainer = result ? result : unknownSymbol;
	return result;
}

// jsx.go:1486
std::pair<std::string, Node*> Checker::getJSXRuntimeImportSpecifier(SourceFile* file) {
	return program->GetJSXRuntimeImportSpecifier(file->Path());
}

// (dep stubs removed on merge — all owned by landed slices or earlier-landed files)

} // namespace tsc::checker
