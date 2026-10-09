// Port of tsc/internal/checker/nodebuilder*.go — the type-to-syntax-node
// serializer used to render types in diagnostics and declarations:
//   nodebuilder.go          — NodeBuilder struct + API entry points + context
//                             stack + simplifyClassDeclaration/simplifyModifiers
//   nodebuilderimpl.go      — NodeBuilderImpl: typeToTypeNode pipeline, symbol
//                             name/chain resolution, anonymous/mapped/
//                             conditional serialization, module specifiers,
//                             truncation
//   nodebuilder_hover.go    — hover expansion (expandSymbolForHover et al.)
//   nodebuilderscopes.go    — context scope machinery (enterNewScope et al.)
//   pseudotypenodebuilder.go — pseudo-type → type node translation
// Free Go helpers are file-local here unless they exist in checker.h.

#include "internal/checker/checker.h"
#include "internal/checker/mapper.h"
#include "internal/collections/collections.h"
#include "internal/pseudochecker/pseudochecker.h"
#include "internal/modulespecifiers/types.h"

#include "internal/ast/visitor.h"
#include "internal/jsnum/jsnum.h"
#include "internal/scanner/scanner.h"
#include "internal/stringutil/stringutil.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tsc::checker {
// utilities.go:757 — defined (non-static) in checker_decltypes.cpp.
ModifierFlags getDeclarationModifierFlagsFromSymbol(Symbol* symbol);

using pseudochecker::PseudoType;
using pseudochecker::PseudoChecker;
using pseudochecker::PseudoParameter;
using pseudochecker::PseudoObjectElement;
using pseudochecker::PseudoTypeKind;

// ---------------------------------------------------------------------------
// constants (nodebuilderimpl.go)
// ---------------------------------------------------------------------------

static constexpr int defaultMaximumTruncationLength = 160;
static constexpr int noTruncationMaximumTruncationLength = 1'000'000;
static constexpr int MAX_REVERSE_MAPPED_NESTING_INSPECTION_DEPTH = 3;

// ---------------------------------------------------------------------------
// extern declarations for helpers defined in sibling slice files
// ---------------------------------------------------------------------------

// mapper.cpp
extern TypeMapper* newTypeMapper(std::vector<Type*> sources,
                                 std::vector<Type*> targets);
// checker_contextual.cpp
extern MappedTypeModifiers getMappedTypeModifiers(Type* t);
// checker.cpp
extern Type* getNonDistributedTypeParameter(Type* t);
// checker_utilities.cpp
extern bool isPrivateIdentifierSymbol(Symbol* symbol);
extern std::string pseudoBigIntToString(const PseudoBigInt& value);
// checker.cpp / checker_contextual.cpp — literal value extraction
// (replica) checker_contextual.cpp getStringLiteralValue — file-local there too.
static std::string getStringLiteralValue(Type* t) {
	return std::get<std::string>(t->AsLiteralType()->value);
}

// (replica) checker.cpp getBigIntLiteralValue — file-static there too.
static PseudoBigInt getBigIntLiteralValue(Type* t) {
	return std::get<PseudoBigInt>(t->AsLiteralType()->value);
}

// ---------------------------------------------------------------------------
// file-local helpers
// ---------------------------------------------------------------------------

// RAII Go `defer` emulation.
template <typename F>
struct ScopeExit {
	F f_;
	ScopeExit(F&& f) : f_(std::forward<F>(f)) {}
	~ScopeExit() { f_(); }
};
template <typename F>
static ScopeExit<F> scopeExit(F&& f) {
	return ScopeExit<F>(std::forward<F>(f));
}


// isTupleType (checker.go) — file-local replica (see checker_contextual.cpp).
static bool isTupleType(Type* t) {
	return t->AsStructuredType() != nullptr &&
	       (t->objectFlags & ObjectFlagsReference) != 0 &&
	       (t->AsTypeReference()->target->objectFlags &
	        ObjectFlagsTuple) != 0;
}

// ast.IsVariableLike (ast/utilities.go:3077).
static bool isVariableLike(Node* node) {
	switch (node->kind) {
	case Kind::BindingElement:
	case Kind::EnumMember:
	case Kind::Parameter:
	case Kind::PropertyAssignment:
	case Kind::PropertyDeclaration:
	case Kind::PropertySignature:
	case Kind::ShorthandPropertyAssignment:
	case Kind::VariableDeclaration:
		return true;
	}
	return false;
}

// ast.HasInferredType (ast/utilities.go:4142).
static bool hasInferredType(Node* node) {
	// Debug.type<HasInferredType>(node); // !!!
	switch (node->kind) {
	case Kind::Parameter:
	case Kind::PropertySignature:
	case Kind::PropertyDeclaration:
	case Kind::BindingElement:
	case Kind::PropertyAccessExpression:
	case Kind::ElementAccessExpression:
	case Kind::BinaryExpression:
	case Kind::CallExpression:
	case Kind::VariableDeclaration:
	case Kind::ExportAssignment:
	case Kind::PropertyAssignment:
	case Kind::ShorthandPropertyAssignment:
	case Kind::JSDocParameterTag:
	case Kind::JSDocPropertyTag:
		return true;
	default:
		return false;
	}
}

// isRestParameter (checker.go:28258).
static bool isRestParameter(Node* param) {
	return param->as<ParameterDeclaration>()->DotDotDotToken != nullptr;
}

// signatureHasRestParameter (checker.go:17358).
static bool signatureHasRestParameter(Signature* sig) {
	return (sig->flags & SignatureFlagsHasRestParameter) != 0;
}

// hasNonGlobalAugmentationExternalModuleSymbol (symbolaccessibility.go:105).
static bool hasNonGlobalAugmentationExternalModuleSymbol(Node* declaration) {
	return isModuleWithStringLiteralName(declaration) ||
	       (declaration->kind == Kind::SourceFile &&
	        isExternalOrCommonJSModule(declaration->as<SourceFile>()));
}

// getQualifiedLeftMeaning (symbolaccessibility.go:109).
static SymbolFlags getQualifiedLeftMeaning(SymbolFlags rightMeaning) {
	// If we are looking in value space, the parent meaning is value, other wise
	// it is namespace
	if (rightMeaning == SymbolFlagsValue) {
		return SymbolFlagsValue;
	}
	return SymbolFlagsNamespace;
}

// getNameFromIndexInfo (checker.go:28262).
static std::string getNameFromIndexInfo(IndexInfo* info) {
	if (info->declaration != nullptr) {
		return declarationNameToString(
			info->declaration->parameters()[0]->name());
	}
	return "x";
}

// isConstEnumSymbol (checker.go:28134).
static bool isConstEnumSymbol(Symbol* symbol) {
	return (symbol->flags & SymbolFlagsConstEnum) != 0;
}

// ast.isEnumConst (ast/utilities.go).
[[maybe_unused]] static bool isEnumConst(Node* node) {
	return (node->modifierFlags() & ModifierFlagsConst) != 0;
}

// stringutil.StripQuotes (stringutil/util.go:222).
static std::string stripQuotes(const std::string& name) {
	if (name.size() < 2) {
		return name;
	}
	char firstChar = name[0];
	char lastChar = name[name.size() - 1];
	if (firstChar == lastChar &&
	    (firstChar == '\'' || firstChar == '"' || firstChar == '`')) {
		return name.substr(1, name.size() - 2);
	}
	return name;
}

// stringutil.UnquoteString (stringutil/util.go:240) — removes every
// backslash-something pair after stripping quotes (faithful, per Go comment).
static std::string unquoteString(const std::string& str) {
	std::string inner = stripQuotes(str);
	std::string out;
	out.reserve(inner.size());
	for (size_t i = 0; i < inner.size(); i++) {
		if (inner[i] == '\\' && i + 1 < inner.size()) {
			out += inner[i + 1];
			i++;
		} else {
			out += inner[i];
		}
	}
	return out;
}

// isLateBoundName (utilities.go:1029).
static bool isLateBoundName(const std::string& name) {
	return name.size() >= 2 && name[0] == kInternalSymbolNamePrefix &&
	       name[1] == '@';
}

// isExpanding (nodebuilder_hover.go:16).
static bool isExpanding(NodeBuilderContext* ctx) {
	return ctx->maxExpansionDepth != -1;
}

// isOptionalDeclaration (utilities.go:298).
static bool isOptionalDeclaration(Node* declaration) {
	return hasQuestionToken(declaration);
}

// ast.isPropertyAccessEntityNameExpression (ast/utilities.go:1630).
static bool isEntityNameExpressionEx(Node* node, bool allowJS);
static bool isPropertyAccessEntityNameExpression(Node* node, bool allowJS) {
	return isPropertyAccessExpression(node) && isIdentifier(node->name()) &&
	       isEntityNameExpressionEx(node->expression(), allowJS);
}

// ast.isElementAccessEntityNameExpression (ast/utilities.go:1634).
static bool isElementAccessEntityNameExpression(Node* node, bool allowJS) {
	return isElementAccessExpression(node) &&
	       isStringOrNumericLiteralLike(
		       node->as<ElementAccessExpression>()->ArgumentExpression) &&
	       isEntityNameExpressionEx(node->expression(), allowJS);
}

// ast.IsEntityNameExpressionEx (ast/utilities.go:1624).
static bool isEntityNameExpressionEx(Node* node, bool allowJS) {
	return isIdentifier(node) ||
	       isPropertyAccessEntityNameExpression(node, allowJS) ||
	       (allowJS && (node->kind == Kind::ThisKeyword ||
	                    isElementAccessEntityNameExpression(node, allowJS)));
}

// StructuredType.CallSignatures() / ConstructSignatures() — views over
// signatures split at callSignatureCount (replica of checker_members.cpp).
static std::vector<Signature*> callSignaturesOf(const StructuredType* t) {
	return std::vector<Signature*>(t->signatures.begin(),
	                               t->signatures.begin() +
	                                   t->callSignatureCount);
}
static std::vector<Signature*> constructSignaturesOf(
	const StructuredType* t) {
	return std::vector<Signature*>(
		t->signatures.begin() + t->callSignatureCount, t->signatures.end());
}

// containsNonMissingUndefinedType (utilities.go:1647).
static bool containsNonMissingUndefinedType(Checker* c, Type* t) {
	Type* candidate;
	if ((t->flags & TypeFlagsUnion) != 0) {
		candidate = t->AsUnionType()->types[0];
	} else {
		candidate = t;
	}
	return (candidate->flags & TypeFlagsUndefined) != 0 &&
	       candidate != c->missingType;
}

// ---------------------------------------------------------------------------
// nodebuilderimpl.go free helpers
// ---------------------------------------------------------------------------

// getAccessStack (nodebuilderimpl.go:325).
static std::vector<Node*> getAccessStack(Node* ref) {
	Node* state = ref->as<TypeReferenceNode>()->TypeName;
	std::vector<Node*> ids;
	while (!isIdentifier(state)) {
		Node* entity = state->as<QualifiedName>();
		ids.insert(ids.begin(), entity->as<QualifiedName>()->Right);
		state = entity->as<QualifiedName>()->Left;
	}
	ids.insert(ids.begin(), state);
	return ids;
}

// isClassInstanceSide (nodebuilderimpl.go:337).
static bool isClassInstanceSide(Checker* c, Type* t) {
	return t->symbol != nullptr &&
	       (t->symbol->flags & SymbolFlagsClass) != 0 &&
	       (t == c->getDeclaredTypeOfClassOrInterface(t->symbol) ||
	        ((t->flags & TypeFlagsObject) != 0 &&
	         (t->objectFlags & ObjectFlagsIsClassInstanceClone) != 0));
}

// isIdentifierTypeReference (nodebuilderimpl.go:463).
static bool isIdentifierTypeReference(Node* node) {
	return isTypeReferenceNode(node) &&
	       isIdentifier(node->as<TypeReferenceNode>()->TypeName);
}

// arrayIsHomogeneous[T] (nodebuilderimpl.go:467).
template <typename T, typename F>
static bool arrayIsHomogeneous(const std::vector<T>& array, F&& comparer) {
	if (array.size() < 2) {
		return true;
	}
	const T& first = array[0];
	for (size_t i = 1; i < array.size(); i++) {
		if (!comparer(first, array[i])) {
			return false;
		}
	}
	return true;
}

// typesAreSameReference (nodebuilderimpl.go:481).
static bool typesAreSameReference(Type* a, Type* b) {
	return a == b ||
	       (a->symbol != nullptr && a->symbol == b->symbol) ||
	       (a->alias != nullptr && a->alias == b->alias);
}

// getTopmostIndexedAccessType (nodebuilderimpl.go:745).
static Node* getTopmostIndexedAccessType(Node* node) {
	if (isIndexedAccessTypeNode(
	        node->as<IndexedAccessTypeNode>()->ObjectType)) {
		return getTopmostIndexedAccessType(
			node->as<IndexedAccessTypeNode>()->ObjectType);
	}
	return node;
}

// canUsePropertyAccess (nodebuilderimpl.go:912).
static bool canUsePropertyAccess(const std::string& name) {
	if (name.empty()) {
		return false;
	}
	// TODO: in strada, this only used `isIdentifierStart` on the first
	// character, while this checks the whole string for validity
	// - possible strada bug?
	if (name[0] == '#') {
		return name.size() > 1 &&
		       isIdentifierText(name.substr(1), LanguageVariant::Standard);
	}
	return isIdentifierText(name, LanguageVariant::Standard);
}

// startsWithSingleOrDoubleQuote (nodebuilderimpl.go:924).
static bool startsWithSingleOrDoubleQuote(const std::string& str) {
	return !str.empty() && (str[0] == '\'' || str[0] == '"');
}

// startsWithSquareBracket (nodebuilderimpl.go:928).
static bool startsWithSquareBracket(const std::string& str) {
	return !str.empty() && str[0] == '[';
}

// isDefaultBindingContext (nodebuilderimpl.go:932).
static bool isDefaultBindingContext(Node* location) {
	return location->kind == Kind::SourceFile || isAmbientModule(location);
}

// getEffectiveParameterDeclaration (nodebuilderimpl.go:1715).
static Node* getEffectiveParameterDeclaration(Symbol* symbol) {
	Node* parameterDeclaration = getDeclarationOfKind(symbol, Kind::Parameter);
	if (parameterDeclaration != nullptr) {
		return parameterDeclaration;
	}
	if ((symbol->flags & SymbolFlagsTransient) == 0) {
		return getDeclarationOfKind(symbol, Kind::JSDocParameterTag);
	}
	return nullptr;
}

// hasTypeAnnotation (nodebuilderimpl.go:2234).
static bool hasTypeAnnotation(Node* declaration) {
	if (declaration == nullptr || declaration->type() == nullptr) {
		return false;
	}
	// Type alias declarations have a .Type() that is their type definition, not
	// a type annotation on a value.
	// Exclude them so callers don't mistake them for annotated value
	// declarations.
	if (isTypeAliasDeclaration(declaration) ||
	    isJSTypeAliasDeclaration(declaration)) {
		return false;
	}
	return true;
}

// getTypeAliasForTypeLiteral (nodebuilderimpl.go:2842).
static Symbol* getTypeAliasForTypeLiteral(Checker* c, Type* t) {
	if (t->symbol != nullptr &&
	    (t->symbol->flags & SymbolFlagsTypeLiteral) != 0 &&
	    !t->symbol->declarations.empty()) {
		Node* node = walkUpParenthesizedTypes(
			t->symbol->declarations[0]->parent);
		if (isTypeAliasDeclaration(node)) {
			return c->getSymbolOfDeclaration(node);
		}
	}
	return nullptr;
}

// isHashPrivate (nodebuilder_hover.go:603).
static bool isHashPrivate(Symbol* s) {
	return s->valueDeclaration != nullptr &&
	       s->valueDeclaration->name() != nullptr &&
	       isPrivateIdentifier(s->valueDeclaration->name());
}

// isStructuralPseudoType (pseudotypenodebuilder.go:632).
static bool isStructuralPseudoType(PseudoType* t) {
	switch (t->kind) {
	case PseudoTypeKind::ObjectLiteral:
	case PseudoTypeKind::Tuple:
	case PseudoTypeKind::SingleCallSignature:
		return true;
	case PseudoTypeKind::MaybeConstLocation: {
		auto* d = t->AsPseudoTypeMaybeConstLocation();
		return isStructuralPseudoType(d->constType) ||
		       isStructuralPseudoType(d->regularType);
	}
	default:
		return false;
	}
}

// ---------------------------------------------------------------------------
// ast.CreateModifiersFromModifierFlags (ast/utilities.go:3291) — replica.
// ---------------------------------------------------------------------------
static std::vector<Node*> createModifiersFromModifierFlags(
	ModifierFlags flags, Node* (*createModifier)(NodeFactory& f, Kind kind),
	NodeFactory& f) {
	std::vector<Node*> result;
	if ((flags & ModifierFlagsExport) != 0) {
		result.push_back(createModifier(f, Kind::ExportKeyword));
	}
	if ((flags & ModifierFlagsAmbient) != 0) {
		result.push_back(createModifier(f, Kind::DeclareKeyword));
	}
	if ((flags & ModifierFlagsDefault) != 0) {
		result.push_back(createModifier(f, Kind::DefaultKeyword));
	}
	if ((flags & ModifierFlagsConst) != 0) {
		result.push_back(createModifier(f, Kind::ConstKeyword));
	}
	if ((flags & ModifierFlagsPublic) != 0) {
		result.push_back(createModifier(f, Kind::PublicKeyword));
	}
	if ((flags & ModifierFlagsPrivate) != 0) {
		result.push_back(createModifier(f, Kind::PrivateKeyword));
	}
	if ((flags & ModifierFlagsProtected) != 0) {
		result.push_back(createModifier(f, Kind::ProtectedKeyword));
	}
	if ((flags & ModifierFlagsAbstract) != 0) {
		result.push_back(createModifier(f, Kind::AbstractKeyword));
	}
	if ((flags & ModifierFlagsStatic) != 0) {
		result.push_back(createModifier(f, Kind::StaticKeyword));
	}
	if ((flags & ModifierFlagsOverride) != 0) {
		result.push_back(createModifier(f, Kind::OverrideKeyword));
	}
	if ((flags & ModifierFlagsReadonly) != 0) {
		result.push_back(createModifier(f, Kind::ReadonlyKeyword));
	}
	if ((flags & ModifierFlagsAccessor) != 0) {
		result.push_back(createModifier(f, Kind::AccessorKeyword));
	}
	if ((flags & ModifierFlagsAsync) != 0) {
		result.push_back(createModifier(f, Kind::AsyncKeyword));
	}
	if ((flags & ModifierFlagsIn) != 0) {
		result.push_back(createModifier(f, Kind::InKeyword));
	}
	if ((flags & ModifierFlagsOut) != 0) {
		result.push_back(createModifier(f, Kind::OutKeyword));
	}
	return result;
}

// ---------------------------------------------------------------------------
// ast.ReplaceModifiers (ast/utilities.go:3352) — replica.
// ---------------------------------------------------------------------------
static Node* replaceModifiers(NodeFactory& f, Node* node,
                              ModifierList* modifierArray) {
	switch (node->kind) {
	case Kind::TypeParameter: {
		auto* d = node->as<TypeParameterDeclaration>();
		return f.updateTypeParameterDeclaration(
			d, modifierArray, node->name(), d->Constraint, d->Expression,
			d->DefaultType);
	}
	case Kind::Parameter: {
		auto* d = node->as<ParameterDeclaration>();
		return f.updateParameterDeclaration(
			d, modifierArray, d->DotDotDotToken, node->name(),
			node->questionToken(), node->type(), node->initializer());
	}
	case Kind::ConstructorType: {
		auto* d = node->as<ConstructorTypeNode>();
		return f.updateConstructorTypeNode(
			d, modifierArray, node->typeParameterList(),
			node->parameterList(), node->type());
	}
	case Kind::PropertySignature: {
		auto* d = node->as<PropertySignatureDeclaration>();
		return f.updatePropertySignatureDeclaration(
			d, modifierArray, node->name(), node->postfixToken(),
			node->type(), node->initializer());
	}
	case Kind::PropertyDeclaration: {
		auto* d = node->as<PropertyDeclaration>();
		return f.updatePropertyDeclaration(
			d, modifierArray, node->name(), node->postfixToken(),
			node->type(), node->initializer());
	}
	case Kind::MethodSignature: {
		auto* d = node->as<MethodSignatureDeclaration>();
		return f.updateMethodSignatureDeclaration(
			d, modifierArray, node->name(), node->postfixToken(),
			node->typeParameterList(), node->parameterList(), node->type());
	}
	case Kind::MethodDeclaration: {
		auto* d = node->as<MethodDeclaration>();
		return f.updateMethodDeclaration(
			d, modifierArray, d->AsteriskToken, node->name(),
			node->postfixToken(), node->typeParameterList(),
			node->parameterList(), node->type(), d->FullSignature,
			node->body());
	}
	case Kind::Constructor: {
		auto* d = node->as<ConstructorDeclaration>();
		return f.updateConstructorDeclaration(
			d, modifierArray, node->typeParameterList(),
			node->parameterList(), node->type(), d->FullSignature,
			node->body());
	}
	case Kind::GetAccessor: {
		auto* d = node->as<GetAccessorDeclaration>();
		return f.updateGetAccessorDeclaration(
			d, modifierArray, node->name(), node->typeParameterList(),
			node->parameterList(), node->type(), d->FullSignature,
			node->body());
	}
	case Kind::SetAccessor: {
		auto* d = node->as<SetAccessorDeclaration>();
		return f.updateSetAccessorDeclaration(
			d, modifierArray, node->name(), node->typeParameterList(),
			node->parameterList(), node->type(), d->FullSignature,
			node->body());
	}
	case Kind::IndexSignature: {
		auto* d = node->as<IndexSignatureDeclaration>();
		return f.updateIndexSignatureDeclaration(
			d, modifierArray, node->parameterList(), node->type());
	}
	case Kind::FunctionExpression: {
		auto* d = node->as<FunctionExpression>();
		return f.updateFunctionExpression(
			d, modifierArray, d->AsteriskToken, node->name(),
			node->typeParameterList(), node->parameterList(), node->type(),
			d->FullSignature, node->body());
	}
	case Kind::ArrowFunction: {
		auto* d = node->as<ArrowFunction>();
		return f.updateArrowFunction(
			d, modifierArray, node->typeParameterList(),
			node->parameterList(), node->type(), d->FullSignature,
			d->EqualsGreaterThanToken, node->body());
	}
	case Kind::ClassExpression: {
		auto* d = node->as<ClassExpression>();
		return f.updateClassExpression(
			d, modifierArray, node->name(), node->typeParameterList(),
			d->HeritageClauses, node->memberList());
	}
	case Kind::VariableStatement: {
		auto* d = node->as<VariableStatement>();
		return f.updateVariableStatement(d, modifierArray,
		                                 d->DeclarationList);
	}
	case Kind::FunctionDeclaration: {
		auto* d = node->as<FunctionDeclaration>();
		return f.updateFunctionDeclaration(
			d, modifierArray, d->AsteriskToken, node->name(),
			node->typeParameterList(), node->parameterList(), node->type(),
			d->FullSignature, node->body());
	}
	case Kind::ClassDeclaration: {
		auto* d = node->as<ClassDeclaration>();
		return f.updateClassDeclaration(
			d, modifierArray, node->name(), node->typeParameterList(),
			d->HeritageClauses, node->memberList());
	}
	case Kind::InterfaceDeclaration: {
		auto* d = node->as<InterfaceDeclaration>();
		return f.updateInterfaceDeclaration(
			d, modifierArray, node->name(), node->typeParameterList(),
			d->HeritageClauses, node->memberList());
	}
	case Kind::TypeAliasDeclaration: {
		auto* d = node->as<TypeAliasDeclaration>();
		return f.updateTypeAliasDeclaration(
			d, modifierArray, node->name(), node->typeParameterList(),
			node->type());
	}
	case Kind::EnumDeclaration: {
		auto* d = node->as<EnumDeclaration>();
		return f.updateEnumDeclaration(d, modifierArray, node->name(),
		                               node->memberList());
	}
	case Kind::ModuleDeclaration: {
		auto* d = node->as<ModuleDeclaration>();
		return f.updateModuleDeclaration(
			d, modifierArray, d->Keyword, node->name(), node->attributes(),
			node->body());
	}
	case Kind::ImportEqualsDeclaration: {
		auto* d = node->as<ImportEqualsDeclaration>();
		return f.updateImportEqualsDeclaration(
			d, modifierArray, node->isTypeOnly(), node->name(),
			d->ModuleReference);
	}
	case Kind::ImportDeclaration: {
		auto* d = node->as<ImportDeclaration>();
		return f.updateImportDeclaration(
			d, modifierArray, node->importClause(), node->moduleSpecifier(),
			d->Attributes);
	}
	case Kind::ExportAssignment: {
		auto* d = node->as<ExportAssignment>();
		return f.updateExportAssignment(
			d, modifierArray, d->IsExportEquals, node->type(),
			node->expression());
	}
	case Kind::ExportDeclaration: {
		auto* d = node->as<ExportDeclaration>();
		return f.updateExportDeclaration(
			d, modifierArray, node->isTypeOnly(), d->ExportClause,
			node->moduleSpecifier(), node->attributes());
	}
	default:
		break;
	}
	TSC_UNREACHABLE("Node that does not have modifiers tried to have modifier "
	                "replaced");
}

// ---------------------------------------------------------------------------
// Small container helpers mirroring tsc/internal/collections funcs
// (Filter/Map/Find/FirstOrNil/Some/Every) used by the nodebuilder slices.
// ---------------------------------------------------------------------------
template <typename T, typename F>
static std::vector<T> filterVec(const std::vector<T>& v, F&& pred) {
	std::vector<T> out;
	for (const T& x : v) {
		if (pred(x)) {
			out.push_back(x);
		}
	}
	return out;
}

template <typename T, typename F>
static auto mapVec(const std::vector<T>& v, F&& fn)
	-> std::vector<std::invoke_result_t<F, T>> {
	using R = std::invoke_result_t<F, T>;
	std::vector<R> out;
	out.reserve(v.size());
	for (const T& x : v) {
		out.push_back(fn(x));
	}
	return out;
}

template <typename T, typename F>
static T findVec(const std::vector<T>& v, F&& pred) {
	for (const T& x : v) {
		if (pred(x)) {
			return x;
		}
	}
	return T{};
}

template <typename T>
static T firstOrNilVec(const std::vector<T>& v) {
	return v.empty() ? T{} : v[0];
}

template <typename T, typename F>
static bool someVec(const std::vector<T>& v, F&& pred) {
	for (const T& x : v) {
		if (pred(x)) {
			return true;
		}
	}
	return false;
}

template <typename T, typename F>
static bool everyVec(const std::vector<T>& v, F&& pred) {
	for (const T& x : v) {
		if (!pred(x)) {
			return false;
		}
	}
	return true;
}
// ===========================================================================
// nodebuilderimpl.go — NodeBuilderImpl
// ===========================================================================

// newNodeBuilderImpl (nodebuilderimpl.go:127).
static NodeBuilderImpl* newNodeBuilderImpl(
	Checker* ch, printer::EmitContext* e,
	std::unordered_map<Node*, Symbol*>* idToSymbol) {
	auto* b = new NodeBuilderImpl();
	b->f = &e->factory;
	b->ch = ch;
	b->e = e;
	if (idToSymbol != nullptr) {
		b->idToSymbol = *idToSymbol;
		// Go: b.idToSymbol is the caller's map — mirror writes back out.
		b->idToSymbolOut = idToSymbol;
	}
	b->pc = pseudochecker::newPseudoChecker(ch->strictNullChecks,
	                                      ch->exactOptionalPropertyTypes);
	NodeBuilderImpl* bp = b;
	b->cloneBindingNameVisitor = newNodeVisitor(
		[bp](Node* n) { return bp->cloneBindingName(n); }, b->f,
		NodeVisitorHooks{});
	return b;
}

// saveRestoreFlags (nodebuilderimpl.go:136).
std::function<void()> NodeBuilderImpl::saveRestoreFlags() {
	nodebuilder::Flags flags = ctx->flags;
	nodebuilder::InternalFlags internalFlags = ctx->internalFlags;
	int depth = ctx->depth;

	return [this, flags, internalFlags, depth]() {
		ctx->flags = flags;
		ctx->internalFlags = internalFlags;
		ctx->depth = depth;
	};
}

// checkTruncationLength (nodebuilderimpl.go:148).
bool NodeBuilderImpl::checkTruncationLength() {
	if (ctx->truncating) {
		return ctx->truncating;
	}
	int maxLength;
	if ((ctx->flags & nodebuilder::FlagsNoTruncation) != 0) {
		maxLength = noTruncationMaximumTruncationLength;
	} else if (ctx->maxTruncationLength > 0) {
		maxLength = ctx->maxTruncationLength;
	} else {
		maxLength = defaultMaximumTruncationLength;
	}
	ctx->truncating = ctx->approximateLength > maxLength;
	return ctx->truncating;
}

// checkTruncationLengthIfExpanding (nodebuilderimpl.go:165).
// Returns true if maxExpansionDepth >= 0 and truncation length exceeded.
// When expanding, we need to mark the output as truncated so we know not to
// offer further expansion.
bool NodeBuilderImpl::checkTruncationLengthIfExpanding() {
	if (ctx->maxExpansionDepth >= 0 && checkTruncationLength()) {
		ctx->expansionTruncated = true;
		return true;
	}
	return false;
}

// isExpandableType (nodebuilderimpl.go:176).
// Reports whether t has a named representation that could be inlined as its
// structural form during hover expansion. Filters out lib types.
// When isAlias is true, checks whether t's alias symbol is from user code
// (not lib).
bool NodeBuilderImpl::isExpandableType(Type* t, bool isAlias) {
	if (isAlias) {
		return !ch->IsLibSymbolForHoverVerbosity(t->alias->symbol);
	}
	if (ch->IsLibTypeForHoverVerbosity(t)) {
		return false;
	}
	ObjectFlags objectFlags = t->objectFlags;
	if ((t->flags & TypeFlagsEnumLike) != 0 ||
	    (objectFlags & ObjectFlagsReference) != 0 ||
	    (objectFlags & ObjectFlagsClassOrInterface) != 0) {
		return true;
	}
	if ((objectFlags & ObjectFlagsAnonymous) != 0 && t->symbol != nullptr &&
	    (t->symbol->flags & (SymbolFlagsClass | SymbolFlagsEnum |
	                         SymbolFlagsValueModule | SymbolFlagsFunction |
	                         SymbolFlagsMethod)) != 0) {
		return true;
	}
	return false;
}

// isTypeOnStack (nodebuilderimpl.go:196).
// Reports whether t is already being processed in the current expansion,
// excluding the last element (which is the type currently being serialized by
// typeToTypeNode).
bool NodeBuilderImpl::isTypeOnStack(Type* t) {
	for (int i = 0; i < (int)ctx->typeStack.size() - 1; i++) {
		if (ctx->typeStack[i] == t) {
			return true;
		}
	}
	return false;
}

// shouldExpandType (nodebuilderimpl.go:205).
// Decides whether to expand this type at the current depth. Returns true when
// depth < maxExpansionDepth (expand now). At the boundary
// (depth == maxExpansionDepth), sets canIncreaseExpansionDepth to signal that
// a higher verbosity level would reveal more detail. Returns false when
// expansion is disabled (maxExpansionDepth < 0), the type is not expandable,
// or the type is cyclic.
bool NodeBuilderImpl::shouldExpandType(Type* t, bool isAlias) {
	if (ctx->maxExpansionDepth < 0) {
		return false;
	}
	if (!isExpandableType(t, isAlias)) {
		return false;
	}
	if (isTypeOnStack(t)) {
		return false;
	}
	if (ctx->depth < ctx->maxExpansionDepth) {
		return true;
	}
	ctx->canIncreaseExpansionDepth = true;
	return false;
}

// isActivelyExpanding (nodebuilderimpl.go:226).
// Reports whether the current depth is below maxExpansionDepth, meaning
// type-node reuse should be skipped so typeToTypeNode can expand named types.
bool NodeBuilderImpl::isActivelyExpanding() {
	return ctx->maxExpansionDepth > 0 && ctx->depth < ctx->maxExpansionDepth;
}

// checkTypeExpandability (nodebuilderimpl.go:231).
// Probes whether a type (or its type arguments) could be expanded, for use
// after type-node reuse where shouldExpandType was never called. Delegates to
// shouldExpandType for the actual check, then recurses into type arguments of
// reference types (e.g., Apple inside Promise<Apple>).
void NodeBuilderImpl::checkTypeExpandability(Type* t) {
	if (ctx->maxExpansionDepth < 0 || t == nullptr ||
	    ctx->canIncreaseExpansionDepth) {
		return;
	}
	// Push t onto the type stack so shouldExpandType's cycle detection works
	// correctly.
	ctx->typeStack.push_back(t);
	auto pop = scopeExit([this] { ctx->typeStack.pop_back(); });
	// If t is an ancestor in the current expansion, return early to avoid
	// unbounded recursion.
	if (isTypeOnStack(t)) {
		return;
	}
	if (t->alias != nullptr) {
		shouldExpandType(t, true);
	}
	if (!ctx->canIncreaseExpansionDepth) {
		shouldExpandType(t, false);
	}
	if (ctx->canIncreaseExpansionDepth) {
		return;
	}
	// Recurse into type arguments (e.g., check Apple in Promise<Apple>).
	if ((t->objectFlags & ObjectFlagsReference) != 0) {
		for (Type* arg : ch->getTypeArguments(t)) {
			checkTypeExpandability(arg);
			if (ctx->canIncreaseExpansionDepth) {
				return;
			}
		}
	}
}

// appendReferenceToType (nodebuilderimpl.go:268).
Node* NodeBuilderImpl::appendReferenceToType(Node* root, Node* ref) {
	if (isImportTypeNode(root)) {
		// first shift type arguments

		// !!! In the old emitter, an Identifier could have type arguments for
		// use with quickinfo — without it, nested type args are silently elided
		auto* imprt = root->as<ImportTypeNode>();
		// then move qualifiers
		std::vector<Node*> ids = getAccessStack(ref);
		Node* qualifier = imprt->Qualifier;
		for (Node* id : ids) {
			if (qualifier != nullptr) {
				qualifier = f->newQualifiedName(qualifier, id);
			} else {
				qualifier = id;
			}
		}
		return f->updateImportTypeNode(imprt, imprt->IsTypeOf,
		                               imprt->Argument, imprt->Attributes,
		                               qualifier, ref->typeArgumentList());
	} else if (isTypeReferenceNode(root)) {
		auto* typeRef = root->as<TypeReferenceNode>();
		if ((ctx->flags & nodebuilder::FlagsUseInstantiationExpressions) != 0 &&
		    typeRef->TypeArguments != nullptr &&
		    !typeRef->TypeArguments->nodes.empty()) {
			Node* expr = createExpressionWithTypeArguments(
				createAccessExpression(typeRef->TypeName),
				typeRef->TypeArguments);
			for (Node* id : getAccessStack(ref)) {
				expr = f->newPropertyAccessExpression(
					expr, nullptr, id, NodeFlagsNone);
			}
			return expr;
		}
		Node* typeName = typeRef->TypeName;
		for (Node* id : getAccessStack(ref)) {
			typeName = f->newQualifiedName(typeName, id);
		}
		return f->updateTypeReferenceNode(typeRef, typeName,
		                                  ref->typeArgumentList());
	}
	Node* expr = createAccessExpression(root);
	for (Node* id : getAccessStack(ref)) {
		expr = f->newPropertyAccessExpression(expr, nullptr, id,
		                                      NodeFlagsNone);
	}
	return expr;
}

// createElidedInformationPlaceholder (nodebuilderimpl.go:346).
Node* NodeBuilderImpl::createElidedInformationPlaceholder() {
	ctx->approximateLength += 3;
	if ((ctx->flags & nodebuilder::FlagsNoTruncation) == 0) {
		return f->newTypeReferenceNode(f->newIdentifier("..."),
		                               nullptr /*typeArguments*/);
	}
	return e->addSyntheticLeadingComment(
		f->newKeywordTypeNode(Kind::AnyKeyword), Kind::MultiLineCommentTrivia,
		"elided", false /*hasTrailingNewLine*/);
}

// mapToTypeNodes (nodebuilderimpl.go:350).
NodeList* NodeBuilderImpl::mapToTypeNodes(std::vector<Type*> list,
                                          bool isBareList) {
	if (list.empty()) {
		return nullptr;
	}

	if (checkTruncationLength()) {
		if (!isBareList) {
			Node* node;
			if ((ctx->flags & nodebuilder::FlagsNoTruncation) != 0) {
				node = e->addSyntheticLeadingComment(
					f->newKeywordTypeNode(Kind::AnyKeyword),
					Kind::MultiLineCommentTrivia, "elided",
					false /*hasTrailingNewLine*/);
			} else {
				node = f->newTypeReferenceNode(f->newIdentifier("..."),
				                               nullptr /*typeArguments*/);
			}
			return f->newNodeList({node});
		} else if (list.size() > 2) {
			std::vector<Node*> nodes{typeToTypeNode(list[0]), nullptr,
			                         typeToTypeNode(list[list.size() - 1])};

			if ((ctx->flags & nodebuilder::FlagsNoTruncation) != 0) {
				nodes[1] = e->addSyntheticLeadingComment(
					f->newKeywordTypeNode(Kind::AnyKeyword),
					Kind::MultiLineCommentTrivia,
					"... " + std::to_string(list.size() - 2) +
					    " more elided ...",
					false /*hasTrailingNewLine*/);
			} else {
				std::string text =
				    "... " + std::to_string(list.size() - 2) + " more ...";
				nodes[1] = f->newTypeReferenceNode(f->newIdentifier(text),
				                                   nullptr /*typeArguments*/);
			}
			return f->newNodeList(nodes);
		}
	}

	bool mayHaveNameCollisions =
	    (ctx->flags & nodebuilder::FlagsUseFullyQualifiedType) == 0;
	struct seenName {
		Type* t;
		int i;
	};
	std::unordered_map<std::string, std::vector<seenName>> seenNames;

	std::vector<Node*> result;
	result.reserve(list.size());

	for (size_t i = 0; i < list.size(); i++) {
		Type* t = list[i];
		int displayIndex = (int)i + 1;
		if (checkTruncationLength() &&
		    (displayIndex + 2 < (int)list.size() - 1)) {
			if ((ctx->flags & nodebuilder::FlagsNoTruncation) != 0) {
				result.push_back(e->addSyntheticLeadingComment(
					f->newKeywordTypeNode(Kind::AnyKeyword),
					Kind::MultiLineCommentTrivia,
					"... " + std::to_string(list.size() - displayIndex) +
					    " more elided ...",
					false /*hasTrailingNewLine*/));
			} else {
				std::string text =
				    "... " + std::to_string(list.size() - displayIndex) +
				    " more ...";
				result.push_back(f->newTypeReferenceNode(
					f->newIdentifier(text), nullptr /*typeArguments*/));
			}
			Node* typeNode = typeToTypeNode(list[list.size() - 1]);
			if (typeNode != nullptr) {
				result.push_back(typeNode);
			}
			break;
		}
		ctx->approximateLength += 2; // Account for whitespace + separator
		Node* typeNode = typeToTypeNode(t);
		if (typeNode != nullptr) {
			result.push_back(typeNode);
			if (mayHaveNameCollisions &&
			    isIdentifierTypeReference(typeNode)) {
				seenNames[typeNode->as<TypeReferenceNode>()
				              ->TypeName->text()]
				    .push_back(seenName{t, (int)result.size() - 1});
			}
		}
	}

	if (mayHaveNameCollisions) {
		// To avoid printing types like `[Foo, Foo]` or `Bar & Bar` where
		// occurrences of the same name actually come from different
		// namespaces, go through the single-identifier type reference nodes
		// we just generated, and see if any names were generated more than
		// once while referring to different types. If so, regenerate the
		// type node for each entry by that name with the
		// `UseFullyQualifiedType` flag enabled.
		auto restoreFlags = saveRestoreFlags();
		ctx->flags |= nodebuilder::FlagsUseFullyQualifiedType;
		for (auto& entry : seenNames) {
			std::vector<seenName>& types = entry.second;
			if (!arrayIsHomogeneous<seenName>(
					types, [](const seenName& a, const seenName& b) {
						return typesAreSameReference(a.t, b.t);
					})) {
				for (const seenName& seen : types) {
					result[seen.i] = typeToTypeNode(seen.t);
				}
			}
		}
		restoreFlags();
	}

	return f->newNodeList(result);
}

// serializeTypeName (nodebuilderimpl.go:438).
Node* NodeBuilderImpl::serializeTypeName(Node* node, bool isTypeOf,
                                         NodeList* typeArguments) {
	SymbolFlags meaning = SymbolFlagsType;
	if (isTypeOf) {
		meaning = SymbolFlagsValue;
	}
	Symbol* symbol = ch->resolveEntityName(node, meaning, true, false, node);
	if (symbol == nullptr) {
		return nullptr;
	}

	Symbol* resolvedSymbol = symbol;
	if ((symbol->flags & SymbolFlagsAlias) != 0) {
		resolvedSymbol = ch->resolveAlias(symbol);
	}

	if (ch->IsSymbolAccessible(symbol, ctx->enclosingDeclaration, meaning,
	                           false)
	        .Accessibility != printer::SymbolAccessibility::Accessible) {
		return nullptr;
	}
	return symbolToTypeNode(resolvedSymbol, meaning, typeArguments);
}

// setCommentRange (nodebuilderimpl.go:486).
void NodeBuilderImpl::setCommentRange(Node* node, Node* range_) {
	if (range_ != nullptr && ctx->enclosingFile != nullptr &&
	    ctx->enclosingFile == getSourceFileOfNode(range_)) {
		// Copy comments to node for declaration emit
		e->assignCommentRange(node, range_);
	}
}

// typeNodeIsEquivalentToType (nodebuilderimpl.go:493).
bool NodeBuilderImpl::typeNodeIsEquivalentToType(Node* annotatedDeclaration,
                                               Type* t,
                                               Type* typeFromTypeNode) {
	if (typeFromTypeNode == t) {
		return true;
	}
	if (annotatedDeclaration == nullptr) {
		return false;
	}
	// !!!
	// used to be hasEffectiveQuestionToken for JSDoc
	if (isOptionalDeclaration(annotatedDeclaration)) {
		return ch->getTypeWithFacts(t, TypeFactsNEUndefined) ==
		       typeFromTypeNode;
	}
	return false;
}

// canReuseExistingJSTypeNode (nodebuilderimpl.go:509).
bool NodeBuilderImpl::canReuseExistingJSTypeNode(Node* existing, Type* t) {
	return ch->getIntendedTypeFromJSDocTypeReference(existing) == nullptr &&
	       existingTypeNodeIsNotReferenceOrIsReferenceWithCompatibleTypeArgumentCount(
		       existing, t);
}

// tryGetResolvedSymbolFromTypeNode (nodebuilderimpl.go:513).
Symbol* NodeBuilderImpl::tryGetResolvedSymbolFromTypeNode(Node* node) {
	if (node == nullptr || node->parent == nullptr) {
		return nullptr;
	}
	ch->getTypeFromTypeNode(node);
	// call to ensure symbol is resolved
	SymbolNodeLinks* links = ch->symbolNodeLinks.TryGet(node);
	if (links == nullptr) {
		return nullptr;
	}
	return links->resolvedSymbol;
}

// existingTypeNodeIsNotReferenceOrIsReferenceWithCompatibleTypeArgumentCount
// (nodebuilderimpl.go:525).
bool NodeBuilderImpl::
	existingTypeNodeIsNotReferenceOrIsReferenceWithCompatibleTypeArgumentCount(
		Node* existing, Type* t) {
	// In JS, you can say something like `Foo` and get a `Foo<any>` implicitly -
	// we don't want to preserve that original `Foo` in these cases, though.
	if ((t->objectFlags & ObjectFlagsReference) == 0) {
		return true;
	}
	if (!isTypeReferenceNode(existing)) {
		return true;
	}

	Symbol* symbol = tryGetResolvedSymbolFromTypeNode(existing);
	if (symbol == nullptr) {
		return true;
	}

	// `type` is a reference type, and `existing` is a type reference node, but
	// we still need to make sure they refer to the _same_ target type
	// before we go comparing their type argument counts.
	Type* existingTarget = ch->getDeclaredTypeOfSymbol(symbol);
	if (existingTarget == nullptr ||
	    existingTarget != t->AsTypeReference()->target) {
		return true;
	}
	return (int)existing->typeArguments().size() >=
	       ch->getMinTypeArgumentCount(interfaceTypeTypeParameters(
		       t->AsTypeReference()->target->AsInterfaceType()));
}

// tryReuseExistingNonParameterTypeNode (nodebuilderimpl.go:546).
Node* NodeBuilderImpl::tryReuseExistingNonParameterTypeNode(
	Node* existing, Type* t, Node* host, Type* annotationType) {
	if (host == nullptr) {
		host = ctx->enclosingDeclaration;
	}
	if (annotationType == nullptr) {
		annotationType = getTypeFromTypeNode(existing, true);
	}
	if (annotationType != nullptr &&
	    typeNodeIsEquivalentToType(host, t, annotationType) &&
	    canReuseExistingJSTypeNode(existing, t)) {
		Node* result = tryReuseExistingNodeHelper(existing);
		if (result != nullptr) {
			return result;
		}
	}
	return nullptr;
}

// getResolvedTypeWithoutAbstractConstructSignatures
// (nodebuilderimpl.go:562).
Type* NodeBuilderImpl::getResolvedTypeWithoutAbstractConstructSignatures(
	StructuredType* t) {
	if (constructSignaturesOf(t).empty()) {
		return t->AsType();
	}
	if (t->objectTypeWithoutAbstractConstructSignatures != nullptr) {
		return t->objectTypeWithoutAbstractConstructSignatures;
	}
	std::vector<Signature*> constructSignatures;
	for (Signature* signature : constructSignaturesOf(t)) {
		if ((signature->flags & SignatureFlagsAbstract) == 0) {
			constructSignatures.push_back(signature);
		}
	}
	if (constructSignatures.size() == constructSignaturesOf(t).size()) {
		t->objectTypeWithoutAbstractConstructSignatures = t->AsType();
		return t->AsType();
	}
	Type* typeCopy = ch->newAnonymousType(
		t->type_.symbol, t->members, callSignaturesOf(t),
		constructSignatures.size() > 0 ? constructSignatures
		                               : std::vector<Signature*>{},
		t->indexInfos);
	t->objectTypeWithoutAbstractConstructSignatures = typeCopy;
	typeCopy->AsStructuredType()
	    ->objectTypeWithoutAbstractConstructSignatures = typeCopy;
	return typeCopy;
}

// symbolToNode (nodebuilderimpl.go:578).
Node* NodeBuilderImpl::symbolToNode(Symbol* symbol, SymbolFlags meaning) {
	if ((ctx->internalFlags & nodebuilder::InternalFlagsWriteComputedProps) !=
	    0) {
		if (symbol->valueDeclaration != nullptr) {
			Node* name = getNameOfDeclaration(symbol->valueDeclaration);
			if (name != nullptr && isComputedPropertyName(name)) {
				return name;
			}
		}
		if (ch->valueSymbolLinks.Has(symbol)) {
			Type* nameType = ch->valueSymbolLinks.Get(symbol)->nameType;
			if (nameType != nullptr &&
			    (nameType->flags &
			     (TypeFlagsEnumLiteral | TypeFlagsUniqueESSymbol)) != 0) {
				Node* oldEnclosing = ctx->enclosingDeclaration;
				ctx->enclosingDeclaration =
				    nameType->symbol->valueDeclaration;
				Node* result = f->newComputedPropertyName(
					symbolToExpressionWorker(nameType->symbol, meaning));
				ctx->enclosingDeclaration = oldEnclosing;
				return result;
			}
		}
	}
	return symbolToExpression(symbol, meaning);
}

// symbolToName (nodebuilderimpl.go:598).
Node* NodeBuilderImpl::symbolToName(Symbol* symbol, SymbolFlags meaning,
                                    bool expectsIdentifier) {
	std::vector<Symbol*> chain = lookupSymbolChain(symbol, meaning, false);
	if (expectsIdentifier && chain.size() != 1 && !ctx->encounteredError &&
	    (ctx->flags &
	     nodebuilder::FlagsAllowQualifiedNameInPlaceOfIdentifier) != 0) {
		ctx->encounteredError = true;
	}
	return createEntityNameFromSymbolChain(chain, (int)chain.size() - 1);
}

// createEntityNameFromSymbolChain (nodebuilderimpl.go:607).
Node* NodeBuilderImpl::createEntityNameFromSymbolChain(
	std::vector<Symbol*> chain, int index) {
	Symbol* symbol = chain[index];

	if (index == 0) {
		ctx->flags |= nodebuilder::FlagsInInitialEntityName;
	}
	std::string symbolName = getNameOfSymbolAsWritten(symbol);
	if (index == 0) {
		ctx->flags ^= nodebuilder::FlagsInInitialEntityName;
	}

	Node* identifier = newIdentifier(symbolName, symbol);
	e->addEmitFlags(identifier, printer::EFNoAsciiEscaping);
	// !!! TODO: smuggle type arguments out
	if (index > 0) {
		return f->newQualifiedName(
			createEntityNameFromSymbolChain(chain, index - 1), identifier);
	}
	return identifier;
}

// TODO: Audit usages of symbolToEntityNameNode - they should probably all be
// symbolToName
// symbolToEntityNameNode (nodebuilderimpl.go:633).
Node* NodeBuilderImpl::symbolToEntityNameNode(Symbol* symbol) {
	Node* identifier = newIdentifier(symbol->name, symbol);
	if (symbol->parent != nullptr) {
		return f->newQualifiedName(symbolToEntityNameNode(symbol->parent),
		                           identifier);
	}
	return identifier;
}

// symbolToTypeNode (nodebuilderimpl.go:641).
Node* NodeBuilderImpl::symbolToTypeNode(Symbol* symbol, SymbolFlags mask,
                                        NodeList* typeArguments) {
	std::vector<Symbol*> chain = lookupSymbolChain(
		symbol, mask,
		(ctx->flags &
	     nodebuilder::FlagsUseAliasDefinedOutsideCurrentScope) == 0); // If
	// we're using aliases outside the current scope, dont bother with the
	// module
	if (chain.empty()) {
		return nullptr; // TODO: shouldn't be possible, `lookupSymbolChain`
		                // should always at least return the input symbol and
		                // issue an error
	}
	bool isTypeOf = mask == SymbolFlagsValue;
	bool rootIsModule = false;
	for (Node* d : chain[0]->declarations) {
		if (hasNonGlobalAugmentationExternalModuleSymbol(d)) {
			rootIsModule = true;
			break;
		}
	}
	if (rootIsModule) {
		// module is root, must use `ImportTypeNode`
		Node* nonRootParts = nullptr;
		if (chain.size() > 1) {
			nonRootParts = createAccessFromSymbolChain(
				chain, (int)chain.size() - 1, 1, typeArguments);
		}
		NodeList* typeParameterNodes = typeArguments;
		if (typeParameterNodes == nullptr) {
			typeParameterNodes = lookupTypeParameterNodes(chain, 0);
		}
		SourceFile* contextFile = getSourceFileOfNode(
			e->mostOriginal(ctx->enclosingDeclaration));
		SourceFile* targetFile = getSourceFileOfModule(chain[0]);
		moduleSpecifierResult specifierResult;
		ResolutionMode importModeOverride = ResolutionModeNone;
		if (ch->compilerOptions->GetModuleResolutionKind() ==
		        ModuleResolutionKind::Node16 ||
		    ch->compilerOptions->GetModuleResolutionKind() ==
		        ModuleResolutionKind::NodeNext) {
			// An `import` type directed at an esm format file is only going to
			// resolve in esm mode - set the esm mode assertion
			if (targetFile != nullptr && contextFile != nullptr &&
			    ch->program->GetEmitModuleFormatOfFile(targetFile) ==
			        ModuleKind::ESNext &&
			    ch->program->GetEmitModuleFormatOfFile(targetFile) !=
			        ch->program->GetEmitModuleFormatOfFile(contextFile)) {
				specifierResult = getSpecifierForModuleSymbol(
					chain[0], ResolutionModeESM);
				importModeOverride = ResolutionModeESM;
			}
		}
		if (specifierResult.specifier.empty()) {
			specifierResult = getSpecifierForModuleSymbol(
				chain[0], ResolutionModeNone);
		}
		if ((ctx->flags & nodebuilder::FlagsAllowNodeModulesRelativePaths) ==
		            0 /* && b.ch.compilerOptions.GetModuleResolutionKind() != core.ModuleResolutionKindClassic */ &&
		    specifierResult.specifier.find("/node_modules/") !=
		        std::string::npos) {
			moduleSpecifierResult oldSpecifierResult = specifierResult;

			if (ch->compilerOptions->GetModuleResolutionKind() ==
			        ModuleResolutionKind::Node16 ||
			    ch->compilerOptions->GetModuleResolutionKind() ==
			        ModuleResolutionKind::NodeNext) {
				// We might be able to write a portable import type using a
				// mode override; try specifier generation again, but with a
				// different mode set
				ResolutionMode swappedMode =
				    ResolutionModeESM;
				if (ch->program->GetEmitModuleFormatOfFile(contextFile) ==
				    ModuleKind::ESNext) {
					swappedMode = ResolutionModeCommonJS;
				}
				specifierResult =
				    getSpecifierForModuleSymbol(chain[0], swappedMode);

				if (specifierResult.specifier.find("/node_modules/") !=
				    std::string::npos) {
					// Still unreachable :(
					specifierResult = oldSpecifierResult;
				} else {
					importModeOverride = swappedMode;
				}
			}

			if (importModeOverride == ResolutionModeNone) {
				// If ultimately we can only name the symbol with a reference
				// that dives into a `node_modules` folder, we should error
				// since declaration files with these kinds of references are
				// liable to fail when published :(
				ctx->encounteredError = true;
				ctx->tracker->ReportLikelyUnsafeImportRequiredError(
					oldSpecifierResult.specifier, symbol->name);
			}
		}

		Node* attributes = createImportAttributesForModuleSpecifier(
			specifierResult, importModeOverride);
		Node* lit =
		    f->newLiteralTypeNode(newStringLiteral(specifierResult.specifier));
		ctx->approximateLength += (int)specifierResult.specifier.size() +
		                          10; // specifier + import("")
		if (nonRootParts == nullptr || isEntityName(nonRootParts)) {
			if (nonRootParts != nullptr) {
				// !!! TODO: smuggle type arguments out
			}
			return f->newImportTypeNode(isTypeOf, lit, attributes,
			                            nonRootParts, typeParameterNodes);
		}

		Node* splitNode = getTopmostIndexedAccessType(
			nonRootParts->as<IndexedAccessTypeNode>());
		Node* qualifier =
		    splitNode->as<IndexedAccessTypeNode>()
		        ->ObjectType->as<TypeReferenceNode>()
		        ->TypeName;
		return f->newIndexedAccessTypeNode(
			f->newImportTypeNode(isTypeOf, lit, attributes, qualifier,
			                     typeParameterNodes),
			splitNode->as<IndexedAccessTypeNode>()->IndexType);
	}

	Node* entityName = createAccessFromSymbolChain(
		chain, (int)chain.size() - 1, 0, typeArguments);
	if (isIndexedAccessTypeNode(entityName)) {
		return entityName; // Indexed accesses can never be `typeof`
	}
	if (isEntityName(entityName)) {
		if (isTypeOf) {
			return f->newTypeQueryNode(entityName, nullptr);
		}
		return f->newTypeReferenceNode(entityName, typeArguments);
	}
	if (isTypeOf && isExpressionWithTypeArguments(entityName)) {
		auto* expr = entityName->as<ExpressionWithTypeArguments>();
		return f->newTypeQueryNode(deepCloneNode(*f, expr->Expression),
		                           expr->TypeArguments);
	}
	return entityName;
}

// createAccessFromSymbolChain (nodebuilderimpl.go:753).
Node* NodeBuilderImpl::createAccessFromSymbolChain(
	std::vector<Symbol*> chain, int index, int stopper,
	NodeList* overrideTypeArguments) {
	NodeList* typeParameterNodes = overrideTypeArguments;
	if (index != ((int)chain.size() - 1)) {
		typeParameterNodes = lookupTypeParameterNodes(chain, index);
	}
	Symbol* symbol = chain[index];
	Symbol* parent = nullptr;
	if (index > 0) {
		parent = chain[index - 1];
	}

	std::string symbolName;
	if (index == 0) {
		ctx->flags |= nodebuilder::FlagsInInitialEntityName;
		symbolName = getNameOfSymbolAsWritten(symbol);
		ctx->approximateLength += (int)symbolName.size() + 1;
		ctx->flags ^= nodebuilder::FlagsInInitialEntityName;
	} else {
		// lookup a ref to symbol within parent to handle export aliases
		if (parent != nullptr) {
			const SymbolTable& exports = ch->getExportsOfSymbol(parent);
			if (!exports.empty()) {
				// avoid exhaustive iteration in the common case
				auto it = exports.find(symbol->name);
				Symbol* res = it != exports.end() ? it->second : nullptr;
				if (symbol->name != InternalSymbolNameExportEquals &&
				    !isLateBoundName(symbol->name) && res != nullptr &&
				    ch->getSymbolIfSameReference(res, symbol) != nullptr) {
					symbolName = symbol->name;
				} else {
					std::vector<std::pair<Symbol*, std::string>> results;
					for (auto& kv : exports) {
						Symbol* ex = kv.second;
						const std::string& name = kv.first;
						if (ch->getSymbolIfSameReference(ex, symbol) !=
						        nullptr &&
						    !isLateBoundName(name) &&
						    name != InternalSymbolNameExportEquals) {
							results.push_back({ex, name});
						}
					}
					std::vector<Symbol*> resultSymbols;
					for (auto& kv : results) {
						resultSymbols.push_back(kv.first);
					}
					if (!resultSymbols.empty()) {
						ch->sortSymbols(resultSymbols);
						std::string chosen;
						for (auto& kv : results) {
							if (kv.first == resultSymbols[0]) {
								chosen = kv.second;
								break;
							}
						}
						symbolName = chosen;
					}
				}
			}
		}
	}

	if (symbolName.empty()) {
		Node* name = nullptr;
		for (Node* d : symbol->declarations) {
			name = getNameOfDeclaration(d);
			if (name != nullptr) {
				break;
			}
		}
		if (name != nullptr && isComputedPropertyName(name) &&
		    isEntityName(name->expression())) {
			Node* lhs = createAccessFromSymbolChain(chain, index - 1, stopper,
			                                        overrideTypeArguments);
			if (isEntityName(lhs)) {
				return f->newIndexedAccessTypeNode(
					f->newParenthesizedTypeNode(
						f->newTypeQueryNode(lhs, nullptr)),
					f->newTypeQueryNode(name->expression(), nullptr));
			}
			return lhs;
		}
		symbolName = getNameOfSymbolAsWritten(symbol);
	}
	ctx->approximateLength += (int)symbolName.size() + 1;

	if ((ctx->flags &
	     nodebuilder::FlagsForbidIndexedAccessSymbolReferences) == 0 &&
	    parent != nullptr) {
		const SymbolTable& members = ch->getMembersOfSymbol(parent);
		auto mit = members.find(symbol->name);
		if (mit != members.end() && mit->second != nullptr &&
		    ch->getSymbolIfSameReference(mit->second, symbol) != nullptr) {
			// Should use an indexed access
			Node* lhs = createAccessFromSymbolChain(chain, index - 1, stopper,
			                                        overrideTypeArguments);
			if (isIndexedAccessTypeNode(lhs)) {
				return f->newIndexedAccessTypeNode(
					lhs,
					f->newLiteralTypeNode(newStringLiteral(symbolName)));
			}
			return f->newIndexedAccessTypeNode(
				f->newTypeReferenceNode(lhs, typeParameterNodes),
				f->newLiteralTypeNode(newStringLiteral(symbolName)));
		}
	}

	Node* identifier = newIdentifier(symbolName, symbol);
	e->addEmitFlags(identifier, printer::EFNoAsciiEscaping);

	if (index > stopper) {
		Node* lhs = createAccessFromSymbolChain(chain, index - 1, stopper,
		                                        overrideTypeArguments);
		if ((ctx->flags & nodebuilder::FlagsUseInstantiationExpressions) ==
		        0 ||
		    (isEntityName(lhs) &&
		     (typeParameterNodes == nullptr ||
		      typeParameterNodes->nodes.empty()))) {
			return f->newQualifiedName(lhs, identifier);
		}
		return createExpressionWithTypeArguments(
			f->newPropertyAccessExpression(createAccessExpression(lhs),
			                               nullptr, identifier,
			                               NodeFlagsNone),
			typeParameterNodes);
	}
	return identifier;
}

// symbolToExpression (nodebuilderimpl.go:850).
Node* NodeBuilderImpl::symbolToExpression(Symbol* symbol, SymbolFlags mask) {
	ctx->tracker->TrackSymbol(symbol, ctx->enclosingDeclaration, mask);
	return symbolToExpressionWorker(symbol, mask);
}

// symbolToExpressionWorker (nodebuilderimpl.go:855).
Node* NodeBuilderImpl::symbolToExpressionWorker(Symbol* symbol,
                                                SymbolFlags mask) {
	std::vector<Symbol*> chain =
	    lookupSymbolChainWorker(symbol, mask, false);
	return createExpressionFromSymbolChain(chain, (int)chain.size() - 1);
}

// createExpressionFromSymbolChain (nodebuilderimpl.go:860).
Node* NodeBuilderImpl::createExpressionFromSymbolChain(
	std::vector<Symbol*> chain, int index) {
	NodeList* typeParameterNodes =
	    lookupExpressionChainTypeArgumentNodes(chain, index);
	Symbol* symbol = chain[index];

	if (index == 0) {
		ctx->flags |= nodebuilder::FlagsInInitialEntityName;
	}
	std::string symbolName = getNameOfSymbolAsWritten(symbol);
	if (index == 0) {
		ctx->flags ^= nodebuilder::FlagsInInitialEntityName;
	}

	if (startsWithSingleOrDoubleQuote(symbolName)) {
		bool isModule = false;
		for (Node* d : symbol->declarations) {
			if (hasNonGlobalAugmentationExternalModuleSymbol(d)) {
				isModule = true;
				break;
			}
		}
		if (isModule) {
			moduleSpecifierResult specifierResult =
			    getSpecifierForModuleSymbol(symbol, ResolutionModeNone);
			ctx->approximateLength +=
			    2 + (int)specifierResult.specifier.size();
			return newStringLiteral(specifierResult.specifier);
		}
	}

	if (index == 0 || canUsePropertyAccess(symbolName)) {
		Node* identifier = newIdentifier(symbolName, symbol);
		e->addEmitFlags(identifier, printer::EFNoAsciiEscaping);
		ctx->approximateLength += 1 + (int)symbolName.size();
		if (index > 0) {
			Node* result = f->newPropertyAccessExpression(
				createExpressionFromSymbolChain(chain, index - 1), nullptr,
				identifier, NodeFlagsNone);
			e->addEmitFlags(result, printer::EFNoIndentation);
			return createExpressionWithTypeArguments(result,
			                                         typeParameterNodes);
		}
		return createExpressionWithTypeArguments(identifier,
		                                         typeParameterNodes);
	}

	if (startsWithSquareBracket(symbolName)) {
		symbolName = symbolName.substr(1, symbolName.size() - 2);
	}

	Node* expression = nullptr;
	if (startsWithSingleOrDoubleQuote(symbolName) &&
	    (symbol->flags & SymbolFlagsEnumMember) == 0) {
		std::string literalText = unquoteString(symbolName);
		ctx->approximateLength += (int)literalText.size() + 2;
		expression = newStringLiteralEx(literalText, symbolName[0] == '\'');
	} else if (numberFromString(symbolName).string() == symbolName) {
		// TODO: the follwing in strada would assert if the number is negative,
		// but no such assertion exists here
		// Moreover, what's even guaranteeing the name *isn't* -1 here anyway?
		// Needs double-checking.
		ctx->approximateLength += (int)symbolName.size();
		expression = f->newNumericLiteral(symbolName, TokenFlagsNone);
	}
	if (expression == nullptr) {
		ctx->approximateLength += (int)symbolName.size();
		expression = newIdentifier(symbolName, symbol);
		e->addEmitFlags(expression, printer::EFNoAsciiEscaping);
	}
	ctx->approximateLength += 2; // []
	return createExpressionWithTypeArguments(
		f->newElementAccessExpression(
			createExpressionFromSymbolChain(chain, index - 1), nullptr,
			expression, NodeFlagsNone),
		typeParameterNodes);
}

// getNameOfSymbolFromNameType (nodebuilderimpl.go:936).
std::string NodeBuilderImpl::getNameOfSymbolFromNameType(Symbol* symbol) {
	if (ch->valueSymbolLinks.Has(symbol)) {
		Type* nameType = ch->valueSymbolLinks.Get(symbol)->nameType;
		if (nameType == nullptr) {
			return "";
		}
		if ((nameType->flags & TypeFlagsStringOrNumberLiteral) != 0) {
			std::string name;
			if (auto* s = std::get_if<std::string>(
					&nameType->AsLiteralType()->value)) {
				name = *s;
			} else if (auto* n = std::get_if<Number>(
						   &nameType->AsLiteralType()->value)) {
				name = n->string();
			}
			if (!isIdentifierText(name, LanguageVariant::Standard) &&
			    !isNumericLiteralName(name)) {
				return ValueToString(nameType->AsLiteralType()->value);
			}
			if (isNumericLiteralName(name) && name.size() > 0 &&
			    name[0] == '-') {
				return "[" + name + "]";
			}
			return name;
		}
		if ((nameType->flags & TypeFlagsUniqueESSymbol) != 0) {
			std::string text = getNameOfSymbolAsWritten(
				nameType->symbol);
			return "[" + text + "]";
		}
	}
	return "";
}

// getNameOfSymbolAsWritten (nodebuilderimpl.go:962).
// Gets a human-readable name for a symbol.
// Should *not* be used for the right-hand side of a `.` -- use
// `symbolName(symbol)` for that instead.
std::string NodeBuilderImpl::getNameOfSymbolAsWritten(Symbol* symbol) {
	auto rit = ctx->remappedSymbolReferences.find(getSymbolId(symbol));
	if (rit != ctx->remappedSymbolReferences.end()) {
		symbol = rit->second;
	}
	if (symbol->name == InternalSymbolNameDefault &&
	    (ctx->flags &
	     nodebuilder::FlagsUseAliasDefinedOutsideCurrentScope) == 0 &&
	    // If it's not the first part of an entity name, it must print as
	    // `default`
	    ((ctx->flags & nodebuilder::FlagsInInitialEntityName) == 0 ||
	     // if the symbol is synthesized, it will only be referenced externally
	     // it must print as `default`
	     symbol->declarations.empty() ||
	     // if not in the same binding context (source file, module
	     // declaration), it must print as `default`
	     (ctx->enclosingDeclaration != nullptr &&
	      findAncestor(symbol->declarations[0], isDefaultBindingContext) !=
	          findAncestor(ctx->enclosingDeclaration,
	                       isDefaultBindingContext)))) {
		return "default";
	}
	if (!symbol->declarations.empty()) {
		Node* name = nullptr; // Try using a declaration with a name, first
		for (Node* d : symbol->declarations) {
			name = getNameOfDeclaration(d);
			if (name != nullptr) {
				break;
			}
		}
		if (name != nullptr) {
			if (isComputedPropertyName(name) &&
			    (symbol->checkFlags & CheckFlagsLate) == 0) {
				if (ch->valueSymbolLinks.Has(symbol) &&
				    ch->valueSymbolLinks.Get(symbol)->nameType != nullptr &&
				    (ch->valueSymbolLinks.Get(symbol)->nameType->flags &
				     TypeFlagsStringOrNumberLiteral) != 0) {
					std::string result =
					    getNameOfSymbolFromNameType(symbol);
					if (!result.empty()) {
						return result;
					}
				}
			}
			return declarationNameToString(name);
		}
		Node* declaration =
		    symbol
		        ->declarations[0]; // Declaration may be nameless, but we'll try anyway
		if (declaration->parent != nullptr &&
		    declaration->parent->kind == Kind::VariableDeclaration) {
			return declarationNameToString(
				declaration->parent->as<VariableDeclaration>()->name);
		}
		if (isClassExpression(declaration) ||
		    isFunctionExpression(declaration) ||
		    isArrowFunction(declaration)) {
			if (ctx != nullptr && !ctx->encounteredError &&
			    (ctx->flags & nodebuilder::FlagsAllowAnonymousIdentifier) ==
			        0) {
				ctx->encounteredError = true;
			}
			switch (declaration->kind) {
			case Kind::ClassExpression:
				return "(Anonymous class)";
			case Kind::FunctionExpression:
			case Kind::ArrowFunction:
				return "(Anonymous function)";
			default:
				break;
			}
		}
	}
	std::string name = getNameOfSymbolFromNameType(symbol);
	if (!name.empty()) {
		return name;
	}
	return escapeInternalSymbolName(symbol->name);
}

// getTypeParametersOfClassOrInterface (nodebuilderimpl.go:1020).
// The full set of type parameters for a generic class or interface type
// consists of its outer type parameters plus its locally declared type
// parameters.
std::vector<Type*> NodeBuilderImpl::getTypeParametersOfClassOrInterface(
	Symbol* symbol) {
	std::vector<Type*> result;
	auto outer = ch->getOuterTypeParametersOfClassOrInterface(symbol);
	result.insert(result.end(), outer.begin(), outer.end());
	auto local =
	    ch->getLocalTypeParametersOfClassOrInterfaceOrTypeAlias(symbol);
	result.insert(result.end(), local.begin(), local.end());
	return result;
}

// lookupTypeParameterNodes (nodebuilderimpl.go:1027).
NodeList* NodeBuilderImpl::lookupTypeParameterNodes(
	std::vector<Symbol*> chain, int index) {
	Symbol* symbol = chain[index];
	SymbolId symbolId = getSymbolId(symbol);
	if (ctx->typeParameterSymbolList.Has(symbolId)) {
		return nullptr;
	}
	ctx->typeParameterSymbolList.Add(symbolId);

	if ((ctx->flags &
	     nodebuilder::FlagsWriteTypeParametersInQualifiedName) != 0 &&
	    index < ((int)chain.size() - 1)) {
		if (NodeList* typeArgumentNodes =
		        lookupInstantiatedTypeArgumentNodes(chain, index);
		    typeArgumentNodes != nullptr) {
			return typeArgumentNodes;
		} else {
			std::vector<Node*> typeParameterNodes =
			    typeParametersToTypeParameterDeclarations(symbol);
			if (!typeParameterNodes.empty()) {
				return f->newNodeList(typeParameterNodes);
			}
			return nullptr;
		}
	}

	return nullptr;
}

// TODO: move `lookupSymbolChain` and co to `symbolaccessibility.go` (but
// getSpecifierForModuleSymbol uses much context which makes that hard?)
// lookupSymbolChain (nodebuilderimpl.go:1054).
std::vector<Symbol*> NodeBuilderImpl::lookupSymbolChain(
	Symbol* symbol, SymbolFlags meaning, bool yieldModuleSymbol) {
	ctx->tracker->TrackSymbol(symbol, ctx->enclosingDeclaration, meaning);
	return lookupSymbolChainWorker(symbol, meaning, yieldModuleSymbol);
}

// lookupSymbolChainWorker (nodebuilderimpl.go:1059).
std::vector<Symbol*> NodeBuilderImpl::lookupSymbolChainWorker(
	Symbol* symbol, SymbolFlags meaning, bool yieldModuleSymbol) {
	// Try to get qualified name if the symbol is not a type parameter and
	// there is an enclosing declaration.
	std::vector<Symbol*> chain;
	bool isTypeParameter = (symbol->flags & SymbolFlagsTypeParameter) != 0;
	if (!isTypeParameter &&
	    (ctx->enclosingDeclaration != nullptr ||
	     (ctx->flags & nodebuilder::FlagsUseFullyQualifiedType) != 0) &&
	    (ctx->internalFlags &
	     nodebuilder::InternalFlagsDoNotIncludeSymbolChain) == 0) {
		chain = getSymbolChain(symbol, meaning /*endOfChain*/, true,
		                       yieldModuleSymbol);
	} else {
		chain.push_back(symbol);
	}
	return chain;
}

// getSymbolChain (nodebuilderimpl.go:1085).
/** @param endOfChain Set to false for recursive calls; non-recursive calls
 * should always output something. */
std::vector<Symbol*> NodeBuilderImpl::getSymbolChain(
	Symbol* symbol, SymbolFlags meaning, bool endOfChain,
	bool yieldModuleSymbol) {
	std::vector<Symbol*> accessibleSymbolChain = ch->getAccessibleSymbolChain(
		symbol, ctx->enclosingDeclaration, meaning,
		(ctx->flags & nodebuilder::FlagsUseOnlyExternalAliasing) != 0);
	SymbolFlags qualifierMeaning = meaning;
	if (accessibleSymbolChain.size() > 1) {
		qualifierMeaning = getQualifiedLeftMeaning(meaning);
	}
	if (accessibleSymbolChain.empty() ||
	    ch->needsQualification(accessibleSymbolChain[0],
	                           ctx->enclosingDeclaration, qualifierMeaning)) {
		// Go up and add our parent.
		Symbol* root = symbol;
		if (!accessibleSymbolChain.empty()) {
			root = accessibleSymbolChain[0];
		}
		std::vector<Symbol*> parents = ch->getContainersOfSymbol(
			root, ctx->enclosingDeclaration, meaning);
		if (!parents.empty()) {
			std::vector<sortedSymbolNamePair> parentSpecifiers;
			parentSpecifiers.reserve(parents.size());
			for (Symbol* s : parents) {
				bool isModule = false;
				for (Node* d : s->declarations) {
					if (hasNonGlobalAugmentationExternalModuleSymbol(d)) {
						isModule = true;
						break;
					}
				}
				if (isModule) {
					parentSpecifiers.push_back(
						{s,
						 getSpecifierForModuleSymbol(s, ResolutionModeNone)
						     .specifier});
				} else {
					parentSpecifiers.push_back({s, ""});
				}
			}
			std::stable_sort(
				parentSpecifiers.begin(), parentSpecifiers.end(),
				[this](const sortedSymbolNamePair& a,
				       const sortedSymbolNamePair& b) {
					return sortByBestName(a, b) < 0;
				});
			for (const sortedSymbolNamePair& pair : parentSpecifiers) {
				Symbol* parent = pair.sym;
				std::vector<Symbol*> parentChain = getSymbolChain(
					parent, getQualifiedLeftMeaning(meaning), false,
					yieldModuleSymbol);
				if (!parentChain.empty()) {
					auto eit = parent->exports.find(
						InternalSymbolNameExportEquals);
					if (eit != parent->exports.end() &&
					    ch->getSymbolIfSameReference(eit->second, symbol) !=
					        nullptr) {
						// parentChain root _is_ symbol - symbol is a module
						// export=, so it kinda looks like it's own parent
						// No need to lookup an alias for the symbol in itself
						accessibleSymbolChain = parentChain;
						break;
					}
					std::vector<Symbol*> nextSyms = accessibleSymbolChain;
					if (nextSyms.empty()) {
						Symbol* fallback =
						    ch->getAliasForSymbolInContainer(parent, symbol);
						if (fallback == nullptr) {
							fallback = symbol;
						}
						nextSyms.push_back(fallback);
					}
					accessibleSymbolChain = parentChain;
					accessibleSymbolChain.insert(
						accessibleSymbolChain.end(), nextSyms.begin(),
						nextSyms.end());
					break;
				}
			}
		}
	}
	if (!accessibleSymbolChain.empty()) {
		return accessibleSymbolChain;
	}
	if
	// If this is the last part of outputting the symbol, always output. The
	// cases apply only to parent symbols.
	(endOfChain ||
	 // If a parent symbol is an anonymous type, don't write it.
	 ((symbol->flags &
	   (SymbolFlagsTypeLiteral | SymbolFlagsObjectLiteral)) == 0)) {
		// If a parent symbol is an external module, don't write it. (We prefer
		// just `x` vs `"foo/bar".x`.)
		if (!endOfChain && !yieldModuleSymbol) {
			bool isModule = false;
			for (Node* d : symbol->declarations) {
				if (hasNonGlobalAugmentationExternalModuleSymbol(d)) {
					isModule = true;
					break;
				}
			}
			if (isModule) {
				return {};
			}
		}
		return {symbol};
	}
	return {};
}

// sortByBestName (nodebuilderimpl.go:1151).
int NodeBuilderImpl::sortByBestName(const sortedSymbolNamePair& a,
                                    const sortedSymbolNamePair& b) {
	const std::string& specifierA = a.name;
	const std::string& specifierB = b.name;
	if (!specifierA.empty() && !specifierB.empty()) {
		bool isBRelative = tspath::pathIsRelative(specifierB);
		if (tspath::pathIsRelative(specifierA) == isBRelative) {
			// Both relative or both non-relative, sort by number of parts
			return modulespecifiers::CountPathComponents(specifierA) -
			       modulespecifiers::CountPathComponents(specifierB);
		}
		if (isBRelative) {
			// A is non-relative, B is relative: prefer A
			return -1;
		}
		// A is relative, B is non-relative: prefer B
		return 1;
	}
	return ch->compareSymbols(a.sym, b.sym); // must sort symbols for stable
	                                         // ordering
}

// canHaveModuleSpecifier (nodebuilderimpl.go:1170).
static bool canHaveModuleSpecifier(Node* node) {
	if (node == nullptr) {
		return false;
	}
	switch (node->kind) {
	case Kind::VariableDeclaration:
	case Kind::BindingElement:
	case Kind::ImportDeclaration:
	case Kind::ExportDeclaration:
	case Kind::ImportEqualsDeclaration:
	case Kind::ImportClause:
	case Kind::NamespaceExport:
	case Kind::NamespaceImport:
	case Kind::ExportSpecifier:
	case Kind::ImportSpecifier:
	case Kind::ImportType:
		return true;
	default:
		break;
	}
	return false;
}

// TryGetModuleSpecifierFromDeclaration (nodebuilderimpl.go:1192).
Node* tryGetModuleSpecifierFromDeclaration(Node* node);

static Node* tryGetModuleSpecifierFromDeclarationWorker(Node* node);

Node* tryGetModuleSpecifierFromDeclaration(Node* node) {
	Node* res = tryGetModuleSpecifierFromDeclarationWorker(node);
	if (res == nullptr || !isStringLiteral(res)) {
		return nullptr;
	}
	return res;
}

// tryGetModuleSpecifierFromDeclarationWorker (nodebuilderimpl.go:1200).
static Node* tryGetModuleSpecifierFromDeclarationWorker(Node* node) {
	switch (node->kind) {
	case Kind::VariableDeclaration:
	case Kind::BindingElement: {
		Node* moduleCall = findAncestor(
			node->initializer(),
			[](Node* node) {
				return isRequireCall(node,
				                     true /*requireStringLiteralLikeArgument*/) ||
				       isImportCall(node);
			});
		if (moduleCall == nullptr) {
			return nullptr;
		}
		return moduleCall->arguments()[0];
	}
	case Kind::ImportDeclaration:
	case Kind::ExportDeclaration:
	case Kind::JSDocImportTag:
		return node->moduleSpecifier();
	case Kind::ImportEqualsDeclaration: {
		Node* ref = node->as<ImportEqualsDeclaration>()->ModuleReference;
		if (ref->kind != Kind::ExternalModuleReference) {
			return nullptr;
		}
		return ref->expression();
	}
	case Kind::ImportClause:
		if (isImportDeclaration(node->parent)) {
			return node->parent->moduleSpecifier();
		}
		return node->parent->moduleSpecifier();
	case Kind::NamespaceExport:
		return node->parent->moduleSpecifier();
	case Kind::NamespaceImport:
		if (isImportDeclaration(node->parent->parent)) {
			return node->parent->parent->moduleSpecifier();
		}
		return node->parent->parent->moduleSpecifier();
	case Kind::ExportSpecifier:
		return node->parent->parent->moduleSpecifier();
	case Kind::ImportSpecifier:
		if (isImportDeclaration(node->parent->parent->parent)) {
			return node->parent->parent->parent->moduleSpecifier();
		}
		return node->parent->parent->parent->moduleSpecifier();
	case Kind::ImportType:
		if (isLiteralImportTypeNode(node)) {
			return node->as<ImportTypeNode>()
			    ->Argument->as<LiteralTypeNode>()
			    ->Literal;
		}
		return nullptr;
	default:
		TSC_UNREACHABLE(
			"tryGetModuleSpecifierFromDeclarationWorker: unhandled kind");
	}
}

// getSpecifierForModuleSymbol (nodebuilderimpl.go:1253).
moduleSpecifierResult NodeBuilderImpl::getSpecifierForModuleSymbol(
	Symbol* symbol, ResolutionMode overrideImportMode) {
	Node* enclosingDeclaration =
	    e->mostOriginal(ctx->enclosingDeclaration);
	Node* originalModuleSpecifier = nullptr;
	if (canHaveModuleSpecifier(enclosingDeclaration)) {
		originalModuleSpecifier =
		    tryGetModuleSpecifierFromDeclaration(enclosingDeclaration);
	}
	Type* originalImportAttributesType = nullptr;
	if (originalModuleSpecifier != nullptr) {
		originalImportAttributesType =
		    ch->getImportAttributesTypeForModuleSpecifier(
		        originalModuleSpecifier);
	}

	Node* file = getDeclarationOfKind(symbol, Kind::SourceFile);
	if (file == nullptr) {
		Symbol* equivalentSymbol = nullptr;
		for (Node* d : symbol->declarations) {
			equivalentSymbol =
			    ch->getFileSymbolIfFileSymbolExportEqualsContainer(d, symbol);
			if (equivalentSymbol != nullptr) {
				break;
			}
		}
		if (equivalentSymbol != nullptr) {
			file = getDeclarationOfKind(equivalentSymbol, Kind::SourceFile);
		}
	}

	if (file == nullptr) {
		Node* declaration = nullptr;
		for (Node* d : symbol->declarations) {
			if (isModuleWithStringLiteralName(d)) {
				declaration = d;
				break;
			}
		}
		if (declaration != nullptr) {
			std::string specifier = declaration->name()->text();
			if (originalImportAttributesType != nullptr &&
			    moduleSpecifierResolvesToSymbol(
					specifier, originalImportAttributesType, symbol)) {
				return moduleSpecifierResult{
					specifier, originalImportAttributesType};
			}
			return moduleSpecifierResult{
				specifier, ch->getTypeOfModuleImportAttributes(symbol)};
		}
		if (auto [specifier, ok] =
		        tryGetAmbientModuleNameFromSymbolName(symbol->name);
		    ok) {
			return moduleSpecifierResult{specifier};
		}
	}
	if (ctx->enclosingFile == nullptr) {
		if (auto [specifier, ok] =
		        tryGetAmbientModuleNameFromSymbolName(symbol->name);
		    ok) {
			return moduleSpecifierResult{specifier};
		}
		return moduleSpecifierResult{getSourceFileOfModule(symbol)->FileName()};
	}

	SourceFile* contextFile = ctx->enclosingFile;
	ResolutionMode resolutionMode = overrideImportMode;
	if (resolutionMode == ResolutionModeNone &&
	    originalModuleSpecifier != nullptr) {
		resolutionMode = ch->program->GetModeForUsageLocation(
			contextFile, originalModuleSpecifier);
	} else if (resolutionMode == ResolutionModeNone &&
	           contextFile != nullptr) {
		resolutionMode =
		    ch->program->GetDefaultResolutionModeForFile(contextFile);
	}
	ModeAwareCacheKey cacheKey{std::string(contextFile->Path()),
	                           resolutionMode};
	NodeBuilderSymbolLinks* links = symbolLinks.Get(symbol);
	auto cached = links->specifierCache.find(cacheKey);
	if (cached != links->specifierCache.end()) {
		return moduleSpecifierResultForSymbol(
			cached->second, originalImportAttributesType, symbol);
	}
	// For declaration bundles, we need to generate absolute paths relative to
	// the common source dir for imports, just like how the declaration
	// emitter does for the ambient module declarations - we can easily
	// accomplish this using the `baseUrl` compiler option (which we would
	// otherwise never use in declaration emit) and a non-relative specifier
	// preference
	Program* host = ctx->host;
	const CompilerOptions* specifierCompilerOptions = ch->compilerOptions;
	modulespecifiers::ImportModuleSpecifierPreference specifierPref =
	    modulespecifiers::ImportModuleSpecifierPreferenceProjectRelative;
	modulespecifiers::ImportModuleSpecifierEndingPreference endingPref =
	    modulespecifiers::ImportModuleSpecifierEndingPreferenceNone;
	if (resolutionMode == ResolutionModeESM) {
		endingPref = modulespecifiers::ImportModuleSpecifierEndingPreferenceJs;
	}
	modulespecifiers::ModuleSpecifiersResult moduleSpecifiersResult =
	    modulespecifiers::GetModuleSpecifiers(
			symbol, ch, specifierCompilerOptions, contextFile, host,
			modulespecifiers::UserPreferences{
				specifierPref, endingPref, {}},
			modulespecifiers::ModuleSpecifierOptions{overrideImportMode},
			false /*forAutoImports*/);
	moduleSpecifierResult result{moduleSpecifiersResult.Specifiers[0]};
	if (moduleSpecifiersResult.AmbientModuleSymbol != nullptr) {
		result.importAttributesType =
		    ch->getTypeOfModuleImportAttributes(
		        moduleSpecifiersResult.AmbientModuleSymbol);
	}
	links->specifierCache[cacheKey] = result;
	return moduleSpecifierResultForSymbol(result, originalImportAttributesType,
	                                      symbol);
}

// moduleSpecifierResultForSymbol (nodebuilderimpl.go:1329).
moduleSpecifierResult NodeBuilderImpl::moduleSpecifierResultForSymbol(
	moduleSpecifierResult result, Type* importAttributesType,
	Symbol* symbol) {
	if (importAttributesType != nullptr &&
	    moduleSpecifierResolvesToSymbol(result.specifier,
	                                    importAttributesType, symbol)) {
		result.importAttributesType = importAttributesType;
	}
	return result;
}

// moduleSpecifierResolvesToSymbol (nodebuilderimpl.go:1336).
bool NodeBuilderImpl::moduleSpecifierResolvesToSymbol(
	const std::string& specifier, Type* importAttributesType,
	Symbol* symbol) {
	Node* location = ctx->enclosingDeclaration;
	if (location == nullptr && ctx->enclosingFile != nullptr) {
		location = ctx->enclosingFile;
	}
	if (location == nullptr) {
		return false;
	}
	Symbol* resolved =
	    ch->resolveExternalModule(location, specifier, nullptr, nullptr,
	                              false /*isForAugmentation*/,
	                              importAttributesType);
	return resolved != nullptr &&
	       ch->getMergedSymbol(resolved) == ch->getMergedSymbol(symbol);
}

// createImportAttributesForModuleSpecifier (nodebuilderimpl.go:1351).
Node* NodeBuilderImpl::createImportAttributesForModuleSpecifier(
	moduleSpecifierResult result, ResolutionMode importModeOverride) {
	bool isEmptyAttributesType =
	    result.importAttributesType == nullptr ||
	    ch->isEmptyObjectType(result.importAttributesType);
	if (isEmptyAttributesType && importModeOverride == ResolutionModeNone) {
		return nullptr;
	}

	std::vector<Symbol*> properties;
	if (!isEmptyAttributesType) {
		properties = ch->getPropertiesOfType(result.importAttributesType);
	}
	std::sort(properties.begin(), properties.end(),
	          [](Symbol* a, Symbol* b) { return a->name < b->name; });
	std::vector<Node*> attributes;
	std::string resolutionMode;
	if (importModeOverride != ResolutionModeNone) {
		resolutionMode = importModeOverride == ResolutionModeESM ? "import"
		                                                       : "require";
		attributes.push_back(f->newImportAttribute(
			newStringLiteral("resolution-mode"),
			newStringLiteral(resolutionMode)));
		ctx->approximateLength +=
		    (int)std::string("resolution-mode").size() +
		    (int)resolutionMode.size() + 6; // `"resolution-mode": "value"`
	}
	for (Symbol* property : properties) {
		const std::string& name = property->name;
		Type* propertyType = ch->getTypeOfSymbol(property);
		if ((propertyType->flags & TypeFlagsStringLiteral) == 0) {
			continue;
		}
		std::string value = getStringLiteralValue(propertyType);
		Node* nameNode;
		if (isIdentifierText(name, LanguageVariant::Standard)) {
			nameNode = f->newIdentifier(name);
		} else {
			nameNode = newStringLiteral(name);
			ctx->approximateLength += 2;
		}
		attributes.push_back(f->newImportAttribute(
			nameNode, newStringLiteral(value)));
		ctx->approximateLength += (int)name.size() + (int)value.size() +
		                          4; // `name: "value"`
	}
	if (attributes.empty()) {
		return nullptr;
	}
	ctx->approximateLength += 16 + 2 * ((int)attributes.size() - 1); // `,
	// { with: { , ... , } }
	return f->newImportAttributes(Kind::WithKeyword,
	                              f->newNodeList(attributes), false);
}

// typeParameterToDeclarationWithConstraint (nodebuilderimpl.go:1391).
Node* NodeBuilderImpl::typeParameterToDeclarationWithConstraint(
	Type* typeParameter, Node* constraintNode) {
	auto restoreFlags = saveRestoreFlags();
	ctx->flags &=
	    ~nodebuilder::
	        FlagsWriteTypeParametersInQualifiedName; // Avoids potential infinite
	// loop when building for a claimspace with a generic
	std::vector<Node*> modifiers = createModifiersFromModifierFlags(
		ch->getTypeParameterModifiers(typeParameter),
		[](NodeFactory& f, Kind kind) { return f.newToken(kind); }, *f);
	ModifierList* modifiersList = nullptr;
	if (!modifiers.empty()) {
		modifiersList = f->newModifierList(modifiers);
	}
	Node* name = typeParameterToName(typeParameter);
	Type* defaultParameter =
	    ch->getDefaultFromTypeParameter(typeParameter);
	Node* defaultParameterDeclarationNode = nullptr;
	if (defaultParameter != nullptr) {
		defaultParameterDeclarationNode =
		    typeToTypeNode(defaultParameter);
	}
	restoreFlags();
	return f->newTypeParameterDeclaration(
		modifiersList, name, constraintNode, nullptr /* expression */,
		defaultParameterDeclarationNode);
}

// setTextRange (nodebuilderimpl.go:1417).
/**
* Unlike the utilities `setTextRange`, this checks if the `location` we're
* trying to set on `range` is within the same file as the active context. If
* not, the range is not applied. This prevents us from copying ranges across
* files, which will confuse the node printer (as it assumes all node ranges
* are within the current file).
* Additionally, if `range` _isn't synthetic_, or isn't in the current file,
* it will _copy_ it to _remove_ its' position information.
*
* It also calls `setOriginalNode` to setup a `.original` pointer, since you
* basically *always* want these in the node builder.
*/
Node* NodeBuilderImpl::setTextRange(Node* range_, Node* location) {
	if (range_ == nullptr) {
		return range_;
	}
	if (!nodeIsSynthesized(range_) ||
	    (range_->flags & NodeFlagsSynthesized) == 0 ||
	    ctx->enclosingFile == nullptr ||
	    ctx->enclosingFile !=
	        getSourceFileOfNode(e->mostOriginal(range_))) {
		Node* original = range_;
		range_ = range_->clone(
			*f); // if `range` is synthesized or originates in another file,
		// copy it so it definitely has synthetic positions
		range_->loc = TextRange{-1, -1};
		auto it = idToSymbol.find(original);
		if (it != idToSymbol.end()) {
			recordIdSymbol(range_, it->second);
		}
	}
	if (range_ == location || location == nullptr) {
		return range_;
	}
	// Don't overwrite the original node if `range` has an `original` node that
	// points either directly or indirectly to `location`
	Node* original = e->original(range_);
	for (; original != nullptr && original != location;
	     original = e->original(original)) {
	}
	if (original == nullptr) {
		e->setOriginalEx(range_, location, true);
	}

	// only set positions if range comes from the same file since copying text
	// across files isn't supported by the emitter
	if (ctx->enclosingFile != nullptr &&
	    ctx->enclosingFile ==
	        getSourceFileOfNode(e->mostOriginal(location))) {
		range_->loc = location->loc;
		return range_;
	} else {
		range_->loc = TextRange{-1, -1};
	}
	return range_;
}

// typeParameterShadowsOtherTypeParameterInScope (nodebuilderimpl.go:1453).
bool NodeBuilderImpl::typeParameterShadowsOtherTypeParameterInScope(
	const std::string& name, Type* typeParameter) {
	Symbol* result = ch->resolveName(ctx->enclosingDeclaration, name,
	                                 SymbolFlagsType, nullptr, false, false);
	if (result != nullptr &&
	    (result->flags & SymbolFlagsTypeParameter) != 0) {
		return result != typeParameter->symbol;
	}
	return false;
}

// typeParameterToName (nodebuilderimpl.go:1461).
Node* NodeBuilderImpl::typeParameterToName(Type* typeParameter) {
	if ((ctx->flags &
	     nodebuilder::FlagsGenerateNamesForShadowedTypeParams) != 0) {
		auto [cached, ok] = ctx->typeParameterNames.Get(typeParameter->id);
		if (ok) {
			return cached;
		}
	}
	Node* result =
	    symbolToName(typeParameter->symbol, SymbolFlagsType /*expectsIdentifier*/,
	                 true);
	if (!isIdentifier(result)) {
		return f->newIdentifier("(Missing type parameter)");
	}
	if (typeParameter->symbol != nullptr &&
	    !typeParameter->symbol->declarations.empty()) {
		Node* decl = typeParameter->symbol->declarations[0];
		if (decl != nullptr && isTypeParameterDeclaration(decl)) {
			result = setTextRange(result, decl->name());
		}
	}
	if ((ctx->flags &
	     nodebuilder::FlagsGenerateNamesForShadowedTypeParams) != 0) {
		std::string rawText = result->text();
		auto [i, _] =
		    ctx->typeParameterNamesByTextNextNameCount.Get(rawText);
		std::string text = rawText;

		for (;;) {
			if (!ctx->typeParameterNamesByText.Has(text) &&
			    !typeParameterShadowsOtherTypeParameterInScope(
					text, typeParameter)) {
				break;
			}
			i++;
			text = rawText + "_" + std::to_string(i);
		}

		if (text != rawText) {
			// !!! TODO: smuggle type arguments out
			result = newIdentifier(text, typeParameter->symbol);
		}

		// avoiding iterations of the above loop turns out to be worth it when
		// `i` starts to get large, so we cache the max `i` we've used thus
		// far, to save work later
		ctx->typeParameterNamesByTextNextNameCount.Set(rawText, i);
		ctx->typeParameterNames.Set(typeParameter->id, result);
		ctx->typeParameterNamesByText.Add(text);
	}

	return result;
}

// isMappedTypeHomomorphic (nodebuilderimpl.go:1520).
bool NodeBuilderImpl::isMappedTypeHomomorphic(Type* mapped) {
	return ch->getHomomorphicTypeVariable(mapped) != nullptr;
}

// isHomomorphicMappedTypeWithNonHomomorphicInstantiation
// (nodebuilderimpl.go:1524).
bool NodeBuilderImpl::
	isHomomorphicMappedTypeWithNonHomomorphicInstantiation(MappedType* mapped) {
	return mapped->type_.AsObjectType()->target != nullptr &&
	       !isMappedTypeHomomorphic(mapped->AsType()) &&
	       isMappedTypeHomomorphic(mapped->type_.AsObjectType()->target);
}

// createMappedTypeNodeFromType (nodebuilderimpl.go:1528).
Node* NodeBuilderImpl::createMappedTypeNodeFromType(Type* t) {
	auto* mapped = t->AsMappedType();
	MappedTypeNode* mappedDecl = mapped->declaration->as<MappedTypeNode>();
	Node* readonlyToken = nullptr;
	if (mappedDecl->ReadonlyToken != nullptr) {
		readonlyToken = f->newToken(mappedDecl->ReadonlyToken->kind);
	}
	Node* questionToken = nullptr;
	if (mappedDecl->QuestionToken != nullptr) {
		questionToken = f->newToken(mappedDecl->QuestionToken->kind);
	}
	Node* appropriateConstraintTypeNode = nullptr;
	Node* newTypeVariable = nullptr;
	Type* templateType = ch->getTemplateTypeFromMappedType(t);
	Type* typeParameter = ch->getTypeParameterFromMappedType(t);

	// If the mapped type isn't `keyof` constraint-declared, _but_ still has
	// modifiers preserved, and its naive instantiation won't preserve
	// modifiers because its constraint isn't `keyof` constrained, we have
	// work to do
	bool needsModifierPreservingWrapper =
	    !ch->isMappedTypeWithKeyofConstraintDeclaration(t) &&
	    (ch->getModifiersTypeFromMappedType(t)->flags & TypeFlagsUnknown) ==
	        0 &&
	    (ctx->flags &
	     nodebuilder::FlagsGenerateNamesForShadowedTypeParams) != 0 &&
	    !((ch->getConstraintTypeFromMappedType(t)->flags &
	       TypeFlagsTypeParameter) != 0 &&
	      (ch->getConstraintOfTypeParameter(
	           ch->getConstraintTypeFromMappedType(t))
	           ->flags & TypeFlagsIndex) != 0);

	if (ch->isMappedTypeWithKeyofConstraintDeclaration(t)) {
		// We have a { [P in keyof T]: X }
		// We do this to ensure we retain the toplevel keyof-ness of the type
		// which may be lost due to keyof distribution during
		// `getConstraintTypeFromMappedType`
		if ((ctx->flags &
		     nodebuilder::FlagsGenerateNamesForShadowedTypeParams) != 0 &&
		    isHomomorphicMappedTypeWithNonHomomorphicInstantiation(mapped)) {
			Type* newConstraintParam = ch->newTypeParameter(
				ch->newSymbol(SymbolFlagsTypeParameter, "T"));
			Node* name = typeParameterToName(newConstraintParam);
			Type* target = t->Target();
			newTypeVariable = f->newTypeReferenceNode(name, nullptr);
			templateType = ch->instantiateType(
				ch->getTemplateTypeFromMappedType(target),
				newTypeMapper(
					{ch->getTypeParameterFromMappedType(target),
					 ch->getModifiersTypeFromMappedType(target)},
					{typeParameter, newConstraintParam}));
		}
		Node* indexTarget = newTypeVariable;
		if (indexTarget == nullptr) {
			indexTarget = typeToTypeNode(ch->getModifiersTypeFromMappedType(t));
		}
		appropriateConstraintTypeNode =
		    f->newTypeOperatorNode(Kind::KeyOfKeyword, indexTarget);
	} else if (needsModifierPreservingWrapper) {
		// So, step 1: new type variable
		Type* newParam =
		    ch->newTypeParameter(ch->newSymbol(SymbolFlagsTypeParameter, "T"));
		Node* name = typeParameterToName(newParam);
		newTypeVariable = f->newTypeReferenceNode(name, nullptr);
		// step 2: make that new type variable itself the constraint node,
		// making the mapped type `{[K in T_1]: Template}`
		appropriateConstraintTypeNode = newTypeVariable;
	} else {
		appropriateConstraintTypeNode =
		    typeToTypeNode(ch->getConstraintTypeFromMappedType(t));
	}

	// nameType and templateType nodes have to be in the new scope
	auto cleanup = enterNewScope(mapped->declaration, {},
	                             {ch->getTypeParameterFromMappedType(t)},
	                             {}, nullptr);
	Node* typeParameterDeclarationNode =
	    typeParameterToDeclarationWithConstraint(typeParameter,
	                                             appropriateConstraintTypeNode);
	Node* nameTypeNode = nullptr;
	if (mappedDecl->NameType != nullptr) {
		nameTypeNode =
		    typeToTypeNode(ch->getNameTypeFromMappedType(t));
	}
	Node* templateTypeNode = typeToTypeNode(ch->removeMissingType(
		templateType,
		(getMappedTypeModifiers(t) &
		 MappedTypeModifiersIncludeOptional) != 0));
	cleanup();
	Node* result = f->newMappedTypeNode(readonlyToken,
	                                    typeParameterDeclarationNode,
	                                    nameTypeNode, questionToken,
	                                    templateTypeNode, nullptr);
	ctx->approximateLength += 10;
	e->addEmitFlags(result, printer::EFSingleLine);

	if ((ctx->flags &
	     nodebuilder::FlagsGenerateNamesForShadowedTypeParams) != 0 &&
	    isHomomorphicMappedTypeWithNonHomomorphicInstantiation(mapped)) {
		// homomorphic mapped type with a non-homomorphic naive inlining
		// wrap it with a conditional like `SomeModifiersType extends infer U ?
		// {..the mapped type...} : never` to ensure the resulting type stays
		// homomorphic

		Type* rawConstraintTypeFromDeclaration = getTypeFromTypeNode(
			mappedDecl->TypeParameter->as<TypeParameterDeclaration>()
			    ->Constraint->type(),
			false);
		if (rawConstraintTypeFromDeclaration != nullptr) {
			rawConstraintTypeFromDeclaration =
			    ch->getConstraintOfTypeParameter(
				    rawConstraintTypeFromDeclaration);
		}
		if (rawConstraintTypeFromDeclaration == nullptr) {
			rawConstraintTypeFromDeclaration = ch->unknownType;
		}
		Type* originalConstraint = ch->instantiateType(
			rawConstraintTypeFromDeclaration, mapped->mapper);

		Node* originalConstraintNode = nullptr;
		if ((originalConstraint->flags & TypeFlagsUnknown) == 0) {
			originalConstraintNode = typeToTypeNode(originalConstraint);
		}

		return f->newConditionalTypeNode(
			typeToTypeNode(ch->getModifiersTypeFromMappedType(t)),
			f->newInferTypeNode(f->newTypeParameterDeclaration(
				nullptr,
				newTypeVariable->as<TypeReferenceNode>()
				    ->TypeName->clone(*f),
				originalConstraintNode, nullptr, nullptr)),
			result, f->newKeywordTypeNode(Kind::NeverKeyword));
	} else if (needsModifierPreservingWrapper) {
		// and step 3: once the mapped type is reconstructed, create a
		// `ConstraintType extends infer T_1 extends keyof ModifiersType ?
		// {[K in T_1]: Template} : never`
		// subtly different from the `keyof` constraint case, by including the
		// `keyof` constraint on the `infer` type parameter, it doesn't rely
		// on the constraint type being itself constrained to a `keyof` type
		// to preserve its modifier-preserving behavior. This is all basically
		// because we preserve modifiers for a wider set of mapped types than
		// just homomorphic ones.
		return f->newConditionalTypeNode(
			typeToTypeNode(ch->getConstraintTypeFromMappedType(t)),
			f->newInferTypeNode(f->newTypeParameterDeclaration(
				nullptr,
				newTypeVariable->as<TypeReferenceNode>()
				    ->TypeName->clone(*f),
				f->newTypeOperatorNode(
					Kind::KeyOfKeyword,
					typeToTypeNode(
						ch->getModifiersTypeFromMappedType(t))),
				nullptr, nullptr)),
			result, f->newKeywordTypeNode(Kind::NeverKeyword));
	}

	return result;
}

// typePredicateToTypePredicateNode (nodebuilderimpl.go:1641).
Node* NodeBuilderImpl::typePredicateToTypePredicateNode(
	TypePredicate* predicate) {
	Node* assertsModifier = nullptr;
	if (predicate->kind == TypePredicateKind::AssertsIdentifier ||
	    predicate->kind == TypePredicateKind::AssertsThis) {
		assertsModifier = f->newToken(Kind::AssertsKeyword);
	}
	Node* parameterName;
	if (predicate->kind == TypePredicateKind::Identifier ||
	    predicate->kind == TypePredicateKind::AssertsIdentifier) {
		parameterName = f->newIdentifier(predicate->parameterName);
		e->addEmitFlags(parameterName, printer::EFNoAsciiEscaping);
	} else {
		parameterName = f->newThisTypeNode();
	}
	Node* typeNode = nullptr;
	if (predicate->t != nullptr) {
		typeNode = typeToTypeNode(predicate->t);
	}
	return f->newTypePredicateNode(assertsModifier, parameterName, typeNode);
}

// typeToTypeNodeHelperWithPossibleReusableTypeNode
// (nodebuilderimpl.go:1663).
Node* NodeBuilderImpl::typeToTypeNodeHelperWithPossibleReusableTypeNode(
	Type* t, Node* typeNode) {
	if (t == nullptr) {
		return f->newKeywordTypeNode(Kind::AnyKeyword);
	}
	if (!isActivelyExpanding() && typeNode != nullptr &&
	    getTypeFromTypeNode(typeNode, false) == t) {
		Node* reused = tryReuseExistingNodeHelper(typeNode);
		if (reused != nullptr) {
			checkTypeExpandability(t);
			return reused;
		}
	}
	return typeToTypeNode(t);
}

// typeParameterToDeclaration (nodebuilderimpl.go:1677).
Node* NodeBuilderImpl::typeParameterToDeclaration(Type* parameter) {
	Type* constraint = ch->getConstraintOfTypeParameter(parameter);
	Node* constraintNode = nullptr;
	if (constraint != nullptr) {
		constraintNode = typeToTypeNodeHelperWithPossibleReusableTypeNode(
			constraint, ch->getConstraintDeclaration(parameter));
	}
	return typeParameterToDeclarationWithConstraint(parameter,
	                                              constraintNode);
}

// symbolToTypeParameterDeclarations (nodebuilderimpl.go:1686).
std::vector<Node*> NodeBuilderImpl::symbolToTypeParameterDeclarations(
	Symbol* symbol) {
	return typeParametersToTypeParameterDeclarations(symbol);
}

// typeParametersToTypeParameterDeclarations (nodebuilderimpl.go:1690).
std::vector<Node*> NodeBuilderImpl::typeParametersToTypeParameterDeclarations(
	Symbol* symbol) {
	Symbol* targetSymbol = ch->getTargetSymbol(symbol);
	if ((targetSymbol->flags & (SymbolFlagsClass | SymbolFlagsInterface |
	                            SymbolFlagsAlias)) != 0) {
		std::vector<Node*> results;
		for (Type* param :
		     ch->getLocalTypeParametersOfClassOrInterfaceOrTypeAlias(
			     symbol)) {
			results.push_back(typeParameterToDeclaration(param));
		}
		return results;
	} else if ((targetSymbol->flags & SymbolFlagsFunction) != 0) {
		std::vector<Node*> results;
		for (Type* param :
		     ch->getTypeParametersFromDeclaration(
			     symbol->valueDeclaration)) {
			results.push_back(typeParameterToDeclaration(param));
		}
		return results;
	}
	return {};
}

// symbolToParameterDeclaration (nodebuilderimpl.go:1724).
Node* NodeBuilderImpl::symbolToParameterDeclaration(Symbol* parameterSymbol,
                                                    bool preserveModifierFlags) {
	Node* parameterDeclaration =
	    getEffectiveParameterDeclaration(parameterSymbol);

	Type* parameterType = ch->getTypeOfSymbol(parameterSymbol);
	Node* parameterTypeNode =
	    serializeTypeForDeclaration(parameterDeclaration, parameterType,
	                                parameterSymbol, true);
	ModifierList* modifiers = nullptr;
	if ((ctx->flags & nodebuilder::FlagsOmitParameterModifiers) == 0 &&
	    preserveModifierFlags && parameterDeclaration != nullptr &&
	    canHaveModifiers(parameterDeclaration)) {
		std::vector<Node*> clones;
		for (Node* m : parameterDeclaration->modifierNodes()) {
			if (isModifier(m)) {
				clones.push_back(m->clone(*f));
			}
		}
		if (!clones.empty()) {
			modifiers = f->newModifierList(clones);
		}
	}
	bool isRest =
	    (parameterDeclaration != nullptr &&
	     isRestParameter(parameterDeclaration)) ||
	    (parameterSymbol->checkFlags & CheckFlagsRestParameter) != 0;
	Node* dotDotDotToken = nullptr;
	if (isRest) {
		dotDotDotToken = f->newToken(Kind::DotDotDotToken);
	}
	Node* name = parameterToParameterDeclarationName(parameterSymbol,
	                                               parameterDeclaration);
	bool isOptional =
	    (parameterDeclaration != nullptr &&
	     ch->isOptionalParameter(parameterDeclaration)) ||
	    (parameterSymbol->checkFlags & CheckFlagsOptionalParameter) != 0;
	Node* questionToken = nullptr;
	if (isOptional) {
		questionToken = f->newToken(Kind::QuestionToken);
	}

	Node* parameterNode = f->newParameterDeclaration(
		modifiers, dotDotDotToken, name, questionToken, parameterTypeNode,
		nullptr /*initializer*/);
	ctx->approximateLength += (int)parameterSymbol->name.size() + 3;
	return parameterNode;
}

// parameterToParameterDeclarationName (nodebuilderimpl.go:1756).
Node* NodeBuilderImpl::parameterToParameterDeclarationName(
	Symbol* parameterSymbol, Node* parameterDeclaration) {
	if (parameterDeclaration == nullptr ||
	    parameterDeclaration->name() == nullptr) {
		return newIdentifier(parameterSymbol->name, parameterSymbol);
	}

	Node* name = parameterDeclaration->name();
	switch (name->kind) {
	case Kind::Identifier: {
		Node* cloned = deepCloneNode(*f, name);
		e->setEmitFlags(cloned, printer::EFNoAsciiEscaping);
		recordIdSymbol(cloned, parameterSymbol);
		return cloned;
	}
	case Kind::QualifiedName: {
		Node* cloned =
		    deepCloneNode(*f, name->as<QualifiedName>()->Right);
		e->setEmitFlags(cloned, printer::EFNoAsciiEscaping);
		recordIdSymbol(cloned, parameterSymbol);
		return cloned;
	}
	default:
		return cloneBindingName(name);
	}
}

// cloneBindingName (nodebuilderimpl.go:1777).
Node* NodeBuilderImpl::cloneBindingName(Node* node) {
	if (isComputedPropertyName(node) && ch->isLateBindableName(node)) {
		trackComputedName(node->expression(), ctx->enclosingDeclaration);
	}

	Node* visited = cloneBindingNameVisitor->visitEachChild(node);

	if (isBindingElement(visited)) {
		auto* bindingElement = visited->as<BindingElement>();
		visited = f->updateBindingElement(bindingElement,
		                                  bindingElement->DotDotDotToken,
		                                  bindingElement->PropertyName,
		                                  bindingElement->name,
		                                  nullptr /* remove initializer */);
	}

	if (!nodeIsSynthesized(visited)) {
		visited = deepCloneNode(*f, visited);
	}

	e->setEmitFlags(visited,
	                printer::EFSingleLine | printer::EFNoAsciiEscaping);
	return visited;
}

// serializeTypeForExpression (nodebuilderimpl.go:1800).
Node* NodeBuilderImpl::serializeTypeForExpression(Node* expr) {
	// !!! TODO: shim, add node reuse
	Type* t = ch->instantiateType(
		ch->getWidenedType(ch->getRegularTypeOfExpression(expr)),
		ctx->mapper);
	return typeToTypeNode(t);
}

// serializeInferredReturnTypeForSignature (nodebuilderimpl.go:1805).
Node* NodeBuilderImpl::serializeInferredReturnTypeForSignature(
	Signature* signature, Type* returnType) {
	bool oldSuppressReportInferenceFallback =
	    ctx->suppressReportInferenceFallback;
	ctx->suppressReportInferenceFallback = true;
	TypePredicate* typePredicate = ch->getTypePredicateOfSignature(signature);
	Node* returnTypeNode;
	if (typePredicate != nullptr) {
		TypePredicate* predicate;
		if (ctx->mapper != nullptr) {
			predicate = ch->instantiateTypePredicate(typePredicate, ctx->mapper);
		} else {
			predicate = typePredicate;
		}
		returnTypeNode = typePredicateToTypePredicateNodeHelper(predicate);
	} else {
		returnTypeNode = typeToTypeNode(returnType);
	}
	ctx->suppressReportInferenceFallback = oldSuppressReportInferenceFallback;
	return returnTypeNode;
}

// typePredicateToTypePredicateNodeHelper (nodebuilderimpl.go:1823).
Node* NodeBuilderImpl::typePredicateToTypePredicateNodeHelper(
	TypePredicate* typePredicate) {
	Node* assertsModifier = nullptr;
	if (typePredicate->kind == TypePredicateKind::AssertsThis ||
	    typePredicate->kind == TypePredicateKind::AssertsIdentifier) {
		assertsModifier = f->newToken(Kind::AssertsKeyword);
	}
	Node* parameterName;
	if (typePredicate->kind == TypePredicateKind::Identifier ||
	    typePredicate->kind == TypePredicateKind::AssertsIdentifier) {
		parameterName =
		    newIdentifier(typePredicate->parameterName, nullptr /*symbol*/);
		e->setEmitFlags(parameterName, printer::EFNoAsciiEscaping);
	} else {
		parameterName = f->newThisTypeNode();
	}
	Node* typeNode = nullptr;
	if (typePredicate->t != nullptr) {
		typeNode = typeToTypeNode(typePredicate->t);
	}
	return f->newTypePredicateNode(assertsModifier, parameterName, typeNode);
}

// signatureToSignatureDeclarationHelper (nodebuilderimpl.go:1863).
Node* NodeBuilderImpl::signatureToSignatureDeclarationHelper(
	Signature* signature, Kind kind,
	SignatureToSignatureDeclarationOptions* options) {
	std::vector<Node*> typeParameters;

	auto [expandedParams, cleanup] = enterSignatureScope(signature);
	ctx->approximateLength += 3;
	// Usually a signature contributes a few more characters than this, but 3
	// is the minimum

	if ((ctx->flags & nodebuilder::FlagsWriteTypeArgumentsOfSignature) != 0 &&
	    signature->target != nullptr && signature->mapper != nullptr &&
	    !signature->target->typeParameters.empty()) {
		for (Type* parameter :
		     signature->target->typeParameters) {
			typeParameters.push_back(typeToTypeNode(
				ch->instantiateType(parameter, signature->mapper)));
		}
	} else {
		for (Type* parameter : signature->typeParameters) {
			typeParameters.push_back(typeParameterToDeclaration(parameter));
		}
	}

	auto restoreFlags = saveRestoreFlags();
	ctx->flags &= ~nodebuilder::FlagsSuppressAnyReturnType;
	// If the expanded parameter list had a variadic in a non-trailing
	// position, don't expand it
	const auto& expandedParamsRef = expandedParams;
	bool hasNonTrailingRest =
	    !expandedParams.empty() &&
	    expandedParamsRef.size() > 0 &&
	    [&]() {
			for (size_t i = 0; i < expandedParamsRef.size(); i++) {
				if (expandedParamsRef[i] != expandedParamsRef.back() &&
				    (expandedParamsRef[i]->checkFlags &
				     CheckFlagsRestParameter) != 0) {
					return true;
				}
			}
			return false;
		}();
	const std::vector<Symbol*>& paramsToUse =
	    hasNonTrailingRest ? signature->parameters : expandedParams;
	std::vector<Node*> parameters;
	parameters.reserve(paramsToUse.size());
	for (Symbol* parameter : paramsToUse) {
		parameters.push_back(symbolToParameterDeclaration(
			parameter, kind == Kind::Constructor));
	}
	Node* thisParameter;
	if ((ctx->flags & nodebuilder::FlagsOmitThisParameter) != 0) {
		thisParameter = nullptr;
	} else {
		thisParameter = tryGetThisParameterDeclaration(signature);
	}
	if (thisParameter != nullptr) {
		parameters.insert(parameters.begin(), thisParameter);
	}
	restoreFlags();

	Node* returnTypeNode = serializeReturnTypeForSignature(signature, true);

	std::vector<Node*> modifiers;
	if (options != nullptr) {
		modifiers = options->modifiers;
	}
	if (kind == Kind::ConstructorType &&
	    (signature->flags & SignatureFlagsAbstract) != 0) {
		ModifierFlags flags = NodeFactory::modifiersToFlags(modifiers);
		modifiers = createModifiersFromModifierFlags(
			flags | ModifierFlagsAbstract,
			[](NodeFactory& nf, Kind k) { return nf.newToken(k); }, *f);
	}

	NodeList* paramList = f->newNodeList(parameters);
	NodeList* typeParamList = nullptr;
	if (!typeParameters.empty()) {
		typeParamList = f->newNodeList(typeParameters);
	}
	ModifierList* modifierList = nullptr;
	if (!modifiers.empty()) {
		modifierList = f->newModifierList(modifiers);
	}
	Node* name = nullptr;
	if (options != nullptr) {
		name = options->name;
	}
	if (name == nullptr) {
		name = f->newIdentifier("");
	}

	Node* node;
	switch (kind) {
	case Kind::CallSignature:
		node = f->newCallSignatureDeclaration(typeParamList, paramList,
		                                      returnTypeNode);
		break;
	case Kind::ConstructSignature:
		node = f->newConstructSignatureDeclaration(typeParamList, paramList,
		                                           returnTypeNode);
		break;
	case Kind::MethodSignature: {
		Node* questionToken = nullptr;
		if (options != nullptr) {
			questionToken = options->questionToken;
		}
		node = f->newMethodSignatureDeclaration(modifierList, name,
		                                        questionToken, typeParamList,
		                                        paramList, returnTypeNode);
		break;
	}
	case Kind::MethodDeclaration:
		node = f->newMethodDeclaration(modifierList, nullptr /*asteriskToken*/,
		                               name, nullptr /*questionToken*/,
		                               typeParamList, paramList,
		                               returnTypeNode, nullptr /*fullSignature*/,
		                               nullptr /*body*/);
		break;
	case Kind::Constructor:
		node = f->newConstructorDeclaration(
			modifierList, nullptr /*typeParamList*/, paramList,
			nullptr /*returnTypeNode*/, nullptr /*fullSignature*/,
			nullptr /*body*/);
		break;
	case Kind::GetAccessor:
		node = f->newGetAccessorDeclaration(
			modifierList, name, nullptr /*typeParamList*/, paramList,
			returnTypeNode, nullptr /*fullSignature*/, nullptr /*body*/);
		break;
	case Kind::SetAccessor:
		node = f->newSetAccessorDeclaration(
			modifierList, name, nullptr /*typeParamList*/, paramList,
			nullptr /*returnTypeNode*/, nullptr /*fullSignature*/,
			nullptr /*body*/);
		break;
	case Kind::IndexSignature:
		node = f->newIndexSignatureDeclaration(modifierList, paramList,
		                                       returnTypeNode);
		break;
	// !!! JSDoc Support
	case Kind::FunctionType:
		if (returnTypeNode == nullptr) {
			returnTypeNode =
			    f->newTypeReferenceNode(f->newIdentifier(""), nullptr);
		}
		node = f->newFunctionTypeNode(typeParamList, paramList,
		                              returnTypeNode);
		break;
	case Kind::ConstructorType:
		if (returnTypeNode == nullptr) {
			returnTypeNode =
			    f->newTypeReferenceNode(f->newIdentifier(""), nullptr);
		}
		node = f->newConstructorTypeNode(modifierList, typeParamList,
		                                 paramList, returnTypeNode);
		break;
	case Kind::FunctionDeclaration:
		// TODO: assert name is Identifier
		node = f->newFunctionDeclaration(
			modifierList, nullptr /*asteriskToken*/, name, typeParamList,
			paramList, returnTypeNode, nullptr /*fullSignature*/,
			nullptr /*body*/);
		break;
	case Kind::FunctionExpression:
		// TODO: assert name is Identifier
		node = f->newFunctionExpression(
			modifierList, nullptr /*asteriskToken*/, name, typeParamList,
			paramList, returnTypeNode, nullptr /*fullSignature*/,
			f->newBlock(f->newNodeList({}), false));
		break;
	case Kind::ArrowFunction:
		node = f->newArrowFunction(
			modifierList, typeParamList, paramList, returnTypeNode,
			nullptr /*fullSignature*/, nullptr /*equalsGreaterThanToken*/,
			f->newBlock(f->newNodeList({}), false));
		break;
	default:
		TSC_UNREACHABLE(
			"Unhandled kind in signatureToSignatureDeclarationHelper");
	}

	cleanup();
	return node;
}

// Checker::getExpandedParameters (nodebuilderimpl.go:1954).
std::vector<std::vector<Symbol*>> Checker::getExpandedParameters(
	Signature* sig, bool skipUnionExpanding) {
	if (signatureHasRestParameter(sig)) {
		int restIndex = (int)sig->parameters.size() - 1;
		Symbol* restSymbol = sig->parameters[restIndex];
		Type* restType = getTypeOfSymbol(restSymbol);
		auto getUniqAssociatedNamesFromTupleType =
		    [&](Type* t, Symbol* restSymbol) -> std::vector<std::string> {
			std::vector<std::string> names;
			auto* tuple = t->Target()->AsTupleType();
			names.reserve(tuple->elementInfos.size());
			for (size_t i = 0; i < tuple->elementInfos.size(); i++) {
				names.push_back(getTupleElementLabel(tuple->elementInfos[i],
				                                     restSymbol, (int)i));
			}
			if (!names.empty()) {
				std::vector<int> duplicates;
				std::unordered_map<std::string, bool> uniqueNames;
				for (size_t i = 0; i < names.size(); i++) {
					if (uniqueNames.count(names[i])) {
						duplicates.push_back((int)i);
					} else {
						uniqueNames[names[i]] = true;
					}
				}
				std::unordered_map<std::string, int> counters;
				for (int i : duplicates) {
					int counter = 1;
					auto it = counters.find(names[i]);
					if (it != counters.end()) {
						counter = it->second;
					}
					std::string name;
					for (;;) {
						name = names[i] + "_" + std::to_string(counter);
						if (uniqueNames.count(name)) {
							counter++;
							continue;
						}
						uniqueNames[name] = true;
						break;
					}
					names[i] = name;
					counters[names[i]] = counter + 1;
				}
			}
			return names;
		};
		auto expandSignatureParametersWithTupleMembers =
		    [&](Type* restType, int restIndex,
		        Symbol* restSymbol) -> std::vector<Symbol*> {
			std::vector<Type*> elementTypes = getTypeArguments(restType);
			std::vector<std::string> associatedNames =
			    getUniqAssociatedNamesFromTupleType(restType, restSymbol);
			std::vector<Symbol*> restParams;
			for (size_t i = 0; i < elementTypes.size(); i++) {
				Type* t = elementTypes[i];
				// Lookup the label from the individual tuple passed in before
				// falling back to the signature `rest` parameter name
				// TODO: getTupleElementLabel can no longer fail, investigate
				// if this lack of falliability meaningfully changes output
				std::string name = associatedNames[i];
				ElementFlags flags = restType->Target()
				                         ->AsTupleType()
				                         ->elementInfos[i]
				                         .flags;
				CheckFlags checkFlags = CheckFlagsNone;
				if ((flags & ElementFlagsVariable) != 0) {
					checkFlags = CheckFlagsRestParameter;
				} else if ((flags & ElementFlagsOptional) != 0) {
					checkFlags = CheckFlagsOptionalParameter;
				}
				Symbol* symbol =
				    newSymbolEx(SymbolFlagsFunctionScopedVariable, name,
				                checkFlags);
				ValueSymbolLinks* links =
				    valueSymbolLinks.Get(symbol);
				if ((flags & ElementFlagsRest) != 0) {
					links->resolvedType = createArrayType(t);
				} else {
					links->resolvedType = t;
				}
				restParams.push_back(symbol);
			}
			std::vector<Symbol*> result(sig->parameters.begin(),
			                            sig->parameters.begin() + restIndex);
			result.insert(result.end(), restParams.begin(),
			              restParams.end());
			return result;
		};

		if (isTupleType(restType)) {
			return {expandSignatureParametersWithTupleMembers(
				restType, restIndex, restSymbol)};
		} else if (!skipUnionExpanding &&
		           (restType->flags & TypeFlagsUnion) != 0) {
			bool allTuple = true;
			for (Type* t : restType->AsUnionType()->types) {
				if (!isTupleType(t)) {
					allTuple = false;
					break;
				}
			}
			if (allTuple) {
				std::vector<std::vector<Symbol*>> result;
				for (Type* t : restType->AsUnionType()->types) {
					result.push_back(
						expandSignatureParametersWithTupleMembers(
							t, restIndex, restSymbol));
				}
				return result;
			}
		}
	}
	return {sig->parameters};
}

// tryGetThisParameterDeclaration (nodebuilderimpl.go:2047).
Node* NodeBuilderImpl::tryGetThisParameterDeclaration(Signature* signature) {
	if (signature->thisParameter != nullptr) {
		return symbolToParameterDeclaration(signature->thisParameter, false);
	}
	if (signature->declaration != nullptr &&
	    isInJSFile(signature->declaration)) {
		// !!! JSDoc Support
	}
	return nullptr;
}

// serializeReturnTypeForSignature (nodebuilderimpl.go:2067).
Node* NodeBuilderImpl::serializeReturnTypeForSignature(Signature* signature,
                                                     bool tryReuse) {
	bool suppressAny =
	    (ctx->flags & nodebuilder::FlagsSuppressAnyReturnType) != 0;
	auto restoreFlags = saveRestoreFlags();
	if (suppressAny) {
		ctx->flags &= ~nodebuilder::FlagsSuppressAnyReturnType; // suppress
		                                                      // only toplevel
		                                                      // `any`s
	}
	Node* returnTypeNode = nullptr;

	Type* returnType;
	if (signature->declaration != nullptr &&
	    !nodeIsSynthesized(signature->declaration)) {
		Symbol* symbol = ch->getSymbolOfDeclaration(signature->declaration);
		auto it =
		    ctx->enclosingSymbolTypes.find(getSymbolId(symbol));
		if (it == ctx->enclosingSymbolTypes.end() ||
		    it->second == nullptr) {
			returnType = ch->instantiateType(
				ch->getReturnTypeOfSignature(signature), ctx->mapper);
		} else {
			returnType = it->second;
		}
	} else {
		returnType = ch->getReturnTypeOfSignature(signature);
	}
	if (!(suppressAny && isTypeAny(returnType))) {
		if (!isActivelyExpanding() && tryReuse &&
		    ctx->enclosingDeclaration != nullptr &&
		    signature->declaration != nullptr &&
		    !nodeIsSynthesized(signature->declaration)) {
			Symbol* declarationSymbol =
			    ch->getSymbolOfDeclaration(signature->declaration);
			auto restore =
			    addSymbolTypeToContext(declarationSymbol, returnType);
			pseudochecker::PseudoType* pt =
			    pc->getReturnTypeOfSignature(signature->declaration);
			if (pseudoTypeEquivalentToType(
				    pt, returnType, false,
				    !ctx->suppressReportInferenceFallback)) {
				// Also verify the pseudo type captures any inferred type
				// predicate, not just the boolean return type.
				// The pseudochecker is unaware of inferred type predicates,
				// so it produces boolean where the checker infers e.g. `x is
				// string`.
				TypePredicate* typePredicate =
				    ch->getTypePredicateOfSignature(signature);
				if (typePredicate != nullptr &&
				    !pseudoReturnTypeMatchesPredicate(pt, typePredicate)) {
					if (!ctx->suppressReportInferenceFallback) {
						ctx->tracker->ReportInferenceFallback(
							signature->declaration);
					}
					pt = nullptr;
				}
				if (pt != nullptr) {
					// !!! TODO: If annotated type node is a reference with
					// insufficient type arguments, we should still fall back
					// to type serialization
					// see: canReuseTypeNodeAnnotation in strada for context
					returnTypeNode = pseudoTypeToNodeWithCheckerFallback(
						pt, returnType);
				}
			}
			restore();
		}
		if (returnTypeNode == nullptr) {
			returnTypeNode =
			    serializeInferredReturnTypeForSignature(signature,
			                                            returnType);
		}
	}

	if (returnTypeNode == nullptr && !suppressAny) {
		returnTypeNode = f->newKeywordTypeNode(Kind::AnyKeyword);
	}
	restoreFlags();
	return returnTypeNode;
}

// isTriviallySerializableComputedName (nodebuilderimpl.go:2118).
bool NodeBuilderImpl::isTriviallySerializableComputedName(Node* e) {
	bool shapeGood = e != nullptr && e->name() != nullptr &&
	                 isComputedPropertyName(e->name()) &&
	                 isEntityNameExpression(e->name()->expression());
	if (!shapeGood) {
		return false;
	}
	return ch->isEntityNameVisible(e->name()->expression(),
	                                        ctx->enclosingDeclaration, false)
	           .Accessibility ==
	       printer::SymbolAccessibility::Accessible;
}

// indexInfoToObjectComputedNamesOrSignatureDeclaration
// (nodebuilderimpl.go:2126).
std::vector<Node*>
NodeBuilderImpl::indexInfoToObjectComputedNamesOrSignatureDeclaration(
	IndexInfo* indexInfo, Node* typeNode) {
	if (!indexInfo->components.empty()) {
		// Index info is derived from object or class computed property names
		// (plus explicit named members) - we can clone those instead of
		// writing out the result computed index signature
		bool allComponentComputedNamesSerializable =
		    ctx->enclosingDeclaration != nullptr &&
		    [this, indexInfo]() {
				for (Node* c : indexInfo->components) {
					if (!isTriviallySerializableComputedName(c)) {
						return false;
					}
				}
				return true;
			}();
		if (allComponentComputedNamesSerializable) {
			// Only use computed name serialization form if all components are
			// visible and take the `a.b.c` form
			std::vector<Node*> newComponents;
			for (Node* c : indexInfo->components) {
				// skip late bound props that contribute to the index
				// signature - they'll be created by property creation anyway
				if (!ch->hasLateBindableName(c)) {
					newComponents.push_back(c);
				}
			}
			bool bailed = false;
			std::vector<Node*> results;
			results.reserve(newComponents.size());
			for (Node* e : newComponents) {
				Node* name = reuseNode(e->name());
				if (name != nullptr) {
					// Still need to track visibility even if we've already
					// checked it to paint references as used
					trackComputedName(e->name()->expression(),
					                  ctx->enclosingDeclaration);
					ModifierList* mods = nullptr;
					if (indexInfo->isReadonly) {
						mods = f->newModifierList(
							{f->newToken(Kind::ReadonlyKeyword)});
					}
					Node* postfixToken = nullptr;
					if (e->postfixToken() != nullptr) {
						postfixToken = e->postfixToken()->clone(*f);
					}
					Node* currentTypeNode = nullptr;
					if (typeNode != nullptr) {
						currentTypeNode = deepCloneNode(*f, typeNode);
					} else {
						currentTypeNode =
						    typeToTypeNode(ch->getTypeOfSymbol(e->symbol()));
					}
					Node* sig = f->newPropertySignatureDeclaration(
						mods, name, postfixToken, currentTypeNode, nullptr);
					sig->loc = e->loc;
					results.push_back(sig);
				} else {
					bailed = true;
					results.push_back(nullptr);
				}
			}
			if (!bailed) {
				return results;
			}
		}
	}
	return {indexInfoToIndexSignatureDeclarationHelper(indexInfo, typeNode)};
}

// indexInfoToIndexSignatureDeclarationHelper (nodebuilderimpl.go:2167).
Node* NodeBuilderImpl::indexInfoToIndexSignatureDeclarationHelper(
	IndexInfo* indexInfo, Node* typeNode) {
	std::string name = getNameFromIndexInfo(indexInfo);
	Node* indexerTypeNode = typeToTypeNode(indexInfo->keyType);

	Node* indexingParameter = f->newParameterDeclaration(
		nullptr, nullptr, newIdentifier(name, nullptr /*symbol*/), nullptr,
		indexerTypeNode, nullptr);
	if (typeNode == nullptr) {
		if (indexInfo->valueType == nullptr) {
			typeNode = f->newKeywordTypeNode(Kind::AnyKeyword);
		} else {
			typeNode = typeToTypeNode(indexInfo->valueType);
		}
	}
	if (indexInfo->valueType == nullptr &&
	    (ctx->flags & nodebuilder::FlagsAllowEmptyIndexInfoType) == 0) {
		ctx->encounteredError = true;
	}
	ctx->approximateLength += (int)name.size() + 4;
	ModifierList* modifiers = nullptr;
	if (indexInfo->isReadonly) {
		ctx->approximateLength += 9;
		modifiers =
		    f->newModifierList({f->newToken(Kind::ReadonlyKeyword)});
	}
	return f->newIndexSignatureDeclaration(
		modifiers, f->newNodeList({indexingParameter}), typeNode);
}

// serializeTypeForDeclaration (nodebuilderimpl.go:2245).
Node* NodeBuilderImpl::serializeTypeForDeclaration(Node* declaration,
                                                   Type* t, Symbol* symbol,
                                                   bool tryReuse) {
	if (declaration == nullptr) {
		if (symbol != nullptr) {
			declaration = symbol->valueDeclaration;
			if (declaration == nullptr) {
				// TODO: prefer annotated declarations like in strada (but
				// does this ever even matter in practice? All callers should
				// supply a declaration!)
				declaration = symbol->declarations.empty()
				                  ? nullptr
				                  : symbol->declarations[0];
			}
		}
	}
	if (symbol == nullptr) {
		symbol = ch->getSymbolOfDeclaration(declaration);
	}
	if (t == nullptr) {
		if (symbol == nullptr) {
			if (isVariableLike(declaration)) {
				t = ch->getTypeForVariableLikeDeclaration(
					declaration, false, CheckModeNormal);
			} else {
				t = ch->errorType;
			}
		} else {
			auto it = ctx->enclosingSymbolTypes.find(getSymbolId(symbol));
			t = it != ctx->enclosingSymbolTypes.end() ? it->second
			                                          : nullptr;
			if (t == nullptr) {
				if ((symbol->flags & SymbolFlagsAccessor) != 0 &&
				    declaration->kind == Kind::SetAccessor) {
					t = ch->instantiateType(
						ch->getWriteTypeOfSymbol(symbol), ctx->mapper);
				} else if (symbol != nullptr &&
				           (symbol->flags & (SymbolFlagsTypeLiteral |
				                             SymbolFlagsSignature)) == 0) {
					t = ch->instantiateType(
						ch->getWidenedLiteralType(
							ch->getTypeOfSymbol(symbol)),
						ctx->mapper);
				} else {
					t = ch->errorType;
				}
			}
		}
	}

	bool requiresAddingUndefined =
	    declaration != nullptr &&
	    (isParameterDeclaration(declaration) ||
	     isPropertySignatureDeclaration(declaration) ||
	     isPropertyDeclaration(declaration)) &&
	    ch->requiresAddingImplicitUndefined(
		    declaration, symbol, ctx->enclosingDeclaration);
	bool addUndefinedForParameter =
	    requiresAddingUndefined &&
	    (isParameterDeclaration(
		 declaration) /*|| isJSDocParameterTag(declaration)*/);
	if (addUndefinedForParameter) {
		t = ch->getOptionalType(t, false);
	}

	auto restoreFlags = saveRestoreFlags();
	if ((t->flags & TypeFlagsUniqueESSymbol) != 0 && t->symbol == symbol &&
	    (ctx->enclosingDeclaration == nullptr ||
	     [&]() {
			for (Node* d : symbol->declarations) {
				if (getSourceFileOfNode(d) == ctx->enclosingFile) {
					return true;
				}
			}
			return false;
		}())) {
		ctx->flags |= nodebuilder::FlagsAllowUniqueESSymbolType;
	}
	Node* result = nullptr;
	bool reportedInferenceFallback = false;
	// !!! expandable hover support
	if (!isActivelyExpanding() && tryReuse &&
	    ctx->enclosingDeclaration != nullptr && declaration != nullptr &&
	    (isAccessor(declaration) ||
	     (hasInferredType(declaration) &&
	      !nodeIsSynthesized(declaration) &&
	      (t->objectFlags & ObjectFlagsRequiresWidening) == 0))) {
		std::function<void()> remove;
		if (symbol != nullptr) {
			remove = addSymbolTypeToContext(symbol, t);
		}
		pseudochecker::PseudoType* pt;
		if (isAccessor(declaration)) {
			pt = pc->getTypeOfAccessor(declaration);
		} else {
			pt = pc->getTypeOfDeclaration(declaration);
		}
		if ((pt == nullptr ||
		     pt->kind == pseudochecker::PseudoTypeKind::NoResult) &&
		    isBinaryExpression(declaration) && symbol != nullptr) {
			Node* decl = nullptr;
			for (Node* d : symbol->declarations) {
				if (hasTypeAnnotation(d)) {
					decl = d;
					break;
				}
			}
			if (decl != nullptr) {
				// Binary expressions have a first-in-wins type annotation
				// system. The first one with an annotation supplies the type
				// for the rest.
				pt = pc->getTypeOfDeclaration(decl);
			}
		}
		bool reportErrors = !ctx->suppressReportInferenceFallback;
		if (pseudoTypeEquivalentToType(
			    pt, t,
			    !requiresAddingUndefined &&
			        (isParameterDeclaration(declaration) ||
			         isPropertySignatureDeclaration(declaration) ||
			         isPropertyDeclaration(declaration)) &&
			        isOptionalDeclaration(declaration),
			    reportErrors)) {
			// !!! TODO: If annotated type node is a reference with
			// insufficient type arguments, we should still fall back to type
			// serialization
			// see: canReuseTypeNodeAnnotation in strada for context
			Type* ptt = pseudoTypeToType(pt);
			if (ptt != nullptr && requiresAddingUndefined &&
			    containsNonMissingUndefinedType(ch, t) &&
			    !containsNonMissingUndefinedType(ch, ptt)) {
				pt = pseudochecker::newPseudoTypeUnion(
					{pt, pseudochecker::PseudoTypeUndefined});
			}
			result = pseudoTypeToNodeWithCheckerFallback(pt, t);
		} else {
			// Equivalence failed; if errors from inferred-with-errors pseudo
			// types were reported, note it so we can suppress nested errors
			// during the fallback typeToTypeNode serialization (mirroring
			// the suppression that pseudoTypeToNodeWithCheckerFallback
			// provides).
			reportedInferenceFallback =
			    reportErrors &&
			    pt->kind == pseudochecker::PseudoTypeKind::Inferred &&
			    !pt->AsPseudoTypeInferred()->errorNodes.empty();
			bool shouldAddUndefined = false;
			if (requiresAddingUndefined) {
				if (Type* ptt = pseudoTypeToType(pt); ptt != nullptr) {
					shouldAddUndefined =
					    !containsNonMissingUndefinedType(ch, ptt);
				} else {
					shouldAddUndefined =
					    !pseudochecker::couldAlreadyReferToUndefinedType(
						    pt);
				}
			}
			if (shouldAddUndefined) {
				pt = pseudochecker::newPseudoTypeUnion(
					{pt, pseudochecker::PseudoTypeUndefined});
				if (pseudoTypeEquivalentToType(pt, t, false,
				                             reportErrors)) {
					result = pseudoTypeToNodeWithCheckerFallback(pt, t);
					reportedInferenceFallback = false;
				}
			}
		}
		if (remove) {
			remove();
		}
	}
	if (result == nullptr) {
		if (reportedInferenceFallback) {
			bool oldSuppress = ctx->suppressReportInferenceFallback;
			ctx->suppressReportInferenceFallback = true;
			result = typeToTypeNode(t);
			ctx->suppressReportInferenceFallback = oldSuppress;
		} else {
			result = typeToTypeNode(t);
		}
	}
	restoreFlags();
	if (result == nullptr) {
		return f->newKeywordTypeNode(Kind::AnyKeyword);
	}
	return result;
}

// shouldUsePlaceholderForProperty (nodebuilderimpl.go:2381).
bool NodeBuilderImpl::shouldUsePlaceholderForProperty(
	Symbol* propertySymbol) {
	// Reverse mapped type placeholders are for display, not
	// declaration emit.
	if ((ctx->flags &
	     nodebuilder::FlagsAllowAnonymousIdentifier) == 0) {
		return false;
	}
	// Use placeholders for reverse mapped types we've either
	// (1) already descended into, or
	// (2) are nested reverse mappings within a mapping over a non-anonymous
	// type, or
	// (3) are deeply nested properties that originate from the same mapped
	// type.
	// Condition (2) is a restriction mostly just to
	// reduce the blowup in printback size from doing, eg, a deep reverse
	// mapping over `Window`.
	// Since anonymous types usually come from expressions, this allows us to
	// preserve the output for deep mappings which likely come from
	// expressions, while truncating those parts which come from mappings over
	// library functions.
	// Condition (3) limits printing of possibly infinitely deep reverse
	// mapped types.
	if ((propertySymbol->checkFlags & CheckFlagsReverseMapped) == 0) {
		return false;
	}
	// (1)
	if (std::find(ctx->reverseMappedStack.begin(),
	              ctx->reverseMappedStack.end(),
	              propertySymbol) != ctx->reverseMappedStack.end()) {
		return true;
	}
	// (2)
	if (!ctx->reverseMappedStack.empty()) {
		Symbol* last = ctx->reverseMappedStack.back();
		if (ch->reverseMappedSymbolLinks.Has(last)) {
			ReverseMappedSymbolLinks* links =
			    ch->reverseMappedSymbolLinks.TryGet(last);
			Type* propertyType = links->propertyType;
			if (propertyType != nullptr &&
			    (propertyType->objectFlags & ObjectFlagsAnonymous) == 0) {
				return true;
			}
		}
	}
	// (3) - we only inspect the last
	// MAX_REVERSE_MAPPED_NESTING_INSPECTION_DEPTH elements of the stack for
	// approximate matches to catch tight infinite loops
	// TODO: Why? Reasoning lost to time. this could probably stand to be
	// improved?
	if ((int)ctx->reverseMappedStack.size() <
	    MAX_REVERSE_MAPPED_NESTING_INSPECTION_DEPTH) {
		return false;
	}
	if (!ch->reverseMappedSymbolLinks.Has(propertySymbol)) {
		return false;
	}
	ReverseMappedSymbolLinks* propertyLinks =
	    ch->reverseMappedSymbolLinks.TryGet(propertySymbol);
	Type* propMappedType = propertyLinks->mappedType;
	if (propMappedType == nullptr || propMappedType->symbol == nullptr) {
		return false;
	}
	for (size_t i = 0; i < ctx->reverseMappedStack.size(); i++) {
		if ((int)i > MAX_REVERSE_MAPPED_NESTING_INSPECTION_DEPTH) {
			break;
		}
		Symbol* prop =
		    ctx->reverseMappedStack[ctx->reverseMappedStack.size() - 1 - i];
		if (ch->reverseMappedSymbolLinks.Has(prop)) {
			ReverseMappedSymbolLinks* links =
			    ch->reverseMappedSymbolLinks.TryGet(prop);
			Type* mappedType = links->mappedType;
			if (mappedType != nullptr &&
			    mappedType->symbol == propMappedType->symbol) {
				return true;
			}
		}
	}
	return false;
}

// trackComputedName (nodebuilderimpl.go:2431).
void NodeBuilderImpl::trackComputedName(Node* accessExpression,
                                        Node* enclosingDeclaration) {
	// get symbol of the first identifier of the entityName
	Node* firstIdentifier = getFirstIdentifier(accessExpression);
	Symbol* name = ch->resolveName(
		enclosingDeclaration, firstIdentifier->text(),
		SymbolFlagsValue | SymbolFlagsExportValue,
		nullptr /*nameNotFoundMessage*/, true /*isUse*/, false);
	if (name != nullptr) {
		ctx->tracker->TrackSymbol(name, enclosingDeclaration,
		                          SymbolFlagsValue);
	} else {
		// Name does not resolve at target location, track symbol at dest
		// location (should be inaccessible)
		Symbol* fallback = ch->resolveName(
			firstIdentifier, firstIdentifier->text(),
			SymbolFlagsValue | SymbolFlagsExportValue,
			nullptr /*nameNotFoundMessage*/, true /*isUse*/, false);
		if (fallback != nullptr) {
			ctx->tracker->TrackSymbol(fallback, enclosingDeclaration,
			                          SymbolFlagsValue);
		}
	}
}

// createPropertyNameNodeForIdentifierOrLiteral (nodebuilderimpl.go:2455).
Node* NodeBuilderImpl::createPropertyNameNodeForIdentifierOrLiteral(
	const std::string& name, bool singleQuote, bool stringNamed,
	bool isMethod, Symbol* symbol) {
	switch (classifyPropertyName(name, stringNamed, isMethod)) {
	case propertyNameNodeKind::Identifier:
		return newIdentifier(name, symbol);
	case propertyNameNodeKind::NumericLiteral:
		return f->newNumericLiteral(name, TokenFlagsNone);
	default:
		return f->newStringLiteral(
			name, singleQuote ? TokenFlagsSingleQuote : TokenFlagsNone);
	}
}

// isStringNamed (nodebuilderimpl.go:2467).
bool NodeBuilderImpl::isStringNamed(Node* d) {
	Node* name = getNameOfDeclaration(d);
	if (name == nullptr) {
		return false;
	}
	if (isComputedPropertyName(name)) {
		Type* t = ch->checkExpression(name->expression());
		return (t->flags & TypeFlagsStringLike) != 0;
	}
	if (isElementAccessExpression(name)) {
		Type* t = ch->checkExpression(
			name->as<ElementAccessExpression>()->ArgumentExpression);
		return (t->flags & TypeFlagsStringLike) != 0;
	}
	return isStringLiteral(name);
}

// isSingleQuotedStringNamed (nodebuilderimpl.go:2483).
bool NodeBuilderImpl::isSingleQuotedStringNamed(Node* d) {
	Node* name = getNameOfDeclaration(d);
	return name != nullptr && isStringLiteral(name) &&
	       (name->as<StringLiteral>()->TokenFlags &
	        TokenFlagsSingleQuote) != 0;
}

// getPropertyNameNodeForSymbol (nodebuilderimpl.go:2488).
Node* NodeBuilderImpl::getPropertyNameNodeForSymbol(
	Symbol* symbol, Node* enclosingDeclaration) {
	// For hash-private names, clone the original private identifier from the
	// declaration
	if (symbol->valueDeclaration != nullptr) {
		Node* declName = symbol->valueDeclaration->name();
		if (declName != nullptr && isPrivateIdentifier(declName)) {
			return deepCloneNode(*f, declName);
		}
	}
	bool stringNamed =
	    !symbol->declarations.empty() &&
	    [this, symbol]() {
			for (Node* d : symbol->declarations) {
				if (!isStringNamed(d)) {
					return false;
				}
			}
			return true;
		}();
	bool singleQuote =
	    !symbol->declarations.empty() &&
	    [this, symbol]() {
			for (Node* d : symbol->declarations) {
				if (!isSingleQuotedStringNamed(d)) {
					return false;
				}
			}
			return true;
		}();
	bool isMethod = (symbol->flags & SymbolFlagsMethod) != 0;
	Node* fromNameType = getPropertyNameNodeForSymbolFromNameType(
		symbol, enclosingDeclaration, singleQuote, stringNamed, isMethod);
	if (fromNameType != nullptr) {
		return fromNameType;
	}

	std::string name = symbol->name;
	const std::string privateNamePrefix =
	    std::string(1, kInternalSymbolNamePrefix) + "#";
	if (name.starts_with(privateNamePrefix)) {
		// symbol IDs are unstable - replace #nnn# with #private#
		name = name.substr(privateNamePrefix.size());
		size_t pos = 0;
		while (pos < name.size() && isDigit(name[pos])) {
			pos++;
		}
		name = "__#private" + name.substr(pos);
	}

	return createPropertyNameNodeForIdentifierOrLiteral(
		name, singleQuote, stringNamed, isMethod, symbol);
}

// getPropertyNameNodeForSymbolFromNameType (nodebuilderimpl.go:2517).
Node* NodeBuilderImpl::getPropertyNameNodeForSymbolFromNameType(
	Symbol* symbol, Node* enclosingDeclaration, bool singleQuote,
	bool stringNamed, bool isMethod) {
	if (!ch->valueSymbolLinks.Has(symbol)) {
		return nullptr;
	}
	Type* nameType = ch->valueSymbolLinks.TryGet(symbol)->nameType;
	if (nameType == nullptr) {
		return nullptr;
	}
	Node* enumEnclosingDeclaration = enclosingDeclaration;
	if (enumEnclosingDeclaration == nullptr &&
	    ctx->enclosingFile != nullptr) {
		enumEnclosingDeclaration = ctx->enclosingFile->asNode();
	}
	if ((nameType->flags & TypeFlagsEnumLiteral) != 0) {
		Symbol* enumSymbol = nameType->symbol->parent;
		if (enumSymbol == nullptr) {
			enumSymbol = nameType->symbol;
		}
		if (enumEnclosingDeclaration != nullptr &&
		    ch->IsSymbolAccessibleByFlags(enumSymbol,
		                                  enumEnclosingDeclaration,
		                                  SymbolFlagsValue)) {
			Node* saveEnclosingDeclaration = ctx->enclosingDeclaration;
			ctx->enclosingDeclaration = enumEnclosingDeclaration;
			Node* result = f->newComputedPropertyName(
				symbolToExpression(nameType->symbol, SymbolFlagsValue));
			ctx->enclosingDeclaration = saveEnclosingDeclaration;
			return result;
		}
	}
	if ((nameType->flags & TypeFlagsStringOrNumberLiteral) != 0) {
		std::string name;
		if (auto* num = std::get_if<Number>(
			    &nameType->AsLiteralType()->value)) {
			name = num->string();
		} else if (auto* str = std::get_if<std::string>(
			       &nameType->AsLiteralType()->value)) {
			name = *str;
		}
		if (!isIdentifierText(name, LanguageVariant::Standard) &&
		    (stringNamed || !isNumericLiteralName(name))) {
			Node* node = f->newStringLiteral(
				name,
				singleQuote ? TokenFlagsSingleQuote : TokenFlagsNone);
			return node;
		}
		if (isNumericLiteralName(name) && name[0] == '-') {
			return f->newComputedPropertyName(
				f->newPrefixUnaryExpression(
					Kind::MinusToken,
					f->newNumericLiteral(name.substr(1),
					                     TokenFlagsNone)));
		}
		return createPropertyNameNodeForIdentifierOrLiteral(
			name, singleQuote, stringNamed, isMethod, symbol);
	}
	if ((nameType->flags & TypeFlagsUniqueESSymbol) != 0) {
		// The reference was tracked in the destination scope by
		// trackComputedName.
		// Reconstructing its spelling in the source scope must not paint that
		// scope's declarations visible.
		return f->newComputedPropertyName(symbolToExpressionWorker(
			nameType->symbol, SymbolFlagsValue));
	}
	return nullptr;
}

// addPropertyToElementList (nodebuilderimpl.go:2567).
std::vector<Node*> NodeBuilderImpl::addPropertyToElementList(
	Symbol* propertySymbol, std::vector<Node*> typeElements) {
	bool propertyIsReverseMapped =
	    (propertySymbol->checkFlags & CheckFlagsReverseMapped) != 0;
	Type* propertyType;
	if (shouldUsePlaceholderForProperty(propertySymbol)) {
		propertyType = ch->anyType;
	} else {
		propertyType = ch->getNonMissingTypeOfSymbol(propertySymbol);
	}
	Node* saveEnclosingDeclaration = ctx->enclosingDeclaration;
	ctx->enclosingDeclaration = nullptr;
	if (isLateBoundName(propertySymbol->name)) {
		if (!propertySymbol->declarations.empty()) {
			Node* decl = propertySymbol->declarations[0];
			if (ch->hasLateBindableName(decl)) {
				if (isBinaryExpression(decl)) {
					Node* name = getNameOfDeclaration(decl);
					if (name != nullptr &&
					    isElementAccessExpression(name) &&
					    isPropertyAccessEntityNameExpression(
						    name->as<ElementAccessExpression>()
						        ->ArgumentExpression,
						    false /*allowJs*/)) {
						trackComputedName(
							name->as<ElementAccessExpression>()
							    ->ArgumentExpression,
							saveEnclosingDeclaration);
					}
				} else {
					trackComputedName(decl->name()->expression(),
					                  saveEnclosingDeclaration);
				}
			}
		} else {
			ctx->tracker->ReportNonSerializableProperty(
				ch->symbolToString(propertySymbol));
		}
	}
	if (propertySymbol->valueDeclaration != nullptr) {
		ctx->enclosingDeclaration = propertySymbol->valueDeclaration;
	} else if (!propertySymbol->declarations.empty() &&
	           propertySymbol->declarations[0] != nullptr) {
		ctx->enclosingDeclaration = propertySymbol->declarations[0];
	} else {
		ctx->enclosingDeclaration = saveEnclosingDeclaration;
	}
	Node* propertyName = getPropertyNameNodeForSymbol(
		propertySymbol, saveEnclosingDeclaration);
	ctx->enclosingDeclaration = saveEnclosingDeclaration;
	ctx->approximateLength += (int)symbolName(propertySymbol).size() + 1;

	if ((propertySymbol->flags & SymbolFlagsAccessor) != 0) {
		Type* writeType = ch->getWriteTypeOfSymbol(propertySymbol);
		if (!ch->isErrorType(propertyType) &&
		    !ch->isErrorType(writeType)) {
			Node* propDeclaration = getDeclarationOfKind(
				propertySymbol, Kind::PropertyDeclaration);
			if (propertyType != writeType ||
			    (propertySymbol->parent != nullptr &&
			     (propertySymbol->parent->flags & SymbolFlagsClass) != 0 &&
			     propDeclaration == nullptr)) {
				TypeMapper* symbolMapper =
				    ch->valueSymbolLinks.Get(propertySymbol)->mapper;
				if (Node* getterDeclaration = getDeclarationOfKind(
					    propertySymbol, Kind::GetAccessor);
				    getterDeclaration != nullptr) {
					Signature* getterSignature =
					    ch->getSignatureFromDeclaration(
						    getterDeclaration);
					if (symbolMapper != nullptr) {
						getterSignature = ch->instantiateSignature(
							getterSignature, symbolMapper);
					}
					SignatureToSignatureDeclarationOptions getterOpts;
					getterOpts.name = propertyName;
					Node* getter =
					    signatureToSignatureDeclarationHelper(
						    getterSignature, Kind::GetAccessor,
						    &getterOpts);
					setCommentRange(getter, getterDeclaration);
					typeElements.push_back(getter);
				}
				if (Node* setterDeclaration = getDeclarationOfKind(
					    propertySymbol, Kind::SetAccessor);
				    setterDeclaration != nullptr) {
					Signature* setterSignature =
					    ch->getSignatureFromDeclaration(
						    setterDeclaration);
					if (symbolMapper != nullptr) {
						setterSignature = ch->instantiateSignature(
							setterSignature, symbolMapper);
					}
					SignatureToSignatureDeclarationOptions setterOpts;
					setterOpts.name = propertyName;
					Node* setter =
					    signatureToSignatureDeclarationHelper(
						    setterSignature, Kind::SetAccessor,
						    &setterOpts);
					setCommentRange(setter, setterDeclaration);
					typeElements.push_back(setter);
				}
				return typeElements;
			} else if (propertySymbol->parent != nullptr &&
			           (propertySymbol->parent->flags &
			            SymbolFlagsClass) != 0 &&
			           propDeclaration != nullptr &&
			           [propDeclaration]() {
				           for (Node* m : propDeclaration->modifierNodes()) {
					           if (m->kind == Kind::AccessorKeyword) {
						           return true;
					           }
				           }
				           return false;
			           }()) {
				Signature* fakeGetterSignature = ch->newSignature(
					SignatureFlagsNone, nullptr, {}, nullptr, {},
					propertyType, nullptr, 0);
				SignatureToSignatureDeclarationOptions getterOpts;
				getterOpts.name = propertyName;
				Node* fakeGetterDeclaration =
				    signatureToSignatureDeclarationHelper(
					    fakeGetterSignature, Kind::GetAccessor,
					    &getterOpts);
				setCommentRange(fakeGetterDeclaration, propDeclaration);
				typeElements.push_back(fakeGetterDeclaration);

				Symbol* setterParam =
				    ch->newSymbol(SymbolFlagsFunctionScopedVariable,
				                  "arg");
				ch->valueSymbolLinks.Get(setterParam)->resolvedType =
				    writeType;
				Signature* fakeSetterSignature = ch->newSignature(
					SignatureFlagsNone, nullptr, {}, nullptr,
					{setterParam}, ch->voidType, nullptr, 0);
				SignatureToSignatureDeclarationOptions setterOpts;
				setterOpts.name = propertyName;
				Node* fakeSetterDeclaration =
				    signatureToSignatureDeclarationHelper(
					    fakeSetterSignature, Kind::SetAccessor,
					    &setterOpts);
				typeElements.push_back(fakeSetterDeclaration);
				return typeElements;
			}
		}
	}

	Node* optionalToken = nullptr;
	if ((propertySymbol->flags & SymbolFlagsOptional) != 0) {
		optionalToken = f->newToken(Kind::QuestionToken);
	}
	if ((propertySymbol->flags &
	     (SymbolFlagsFunction | SymbolFlagsMethod)) != 0 &&
	    ch->getPropertiesOfObjectType(propertyType).empty() &&
	    !ch->isReadonlySymbol(propertySymbol)) {
		std::vector<Signature*> signatures = ch->getSignaturesOfType(
			ch->filterType(propertyType,
			               [](Type* t) {
				               return (t->flags & TypeFlagsUndefined) == 0;
			               }),
			SignatureKind::Call);
		for (Signature* signature : signatures) {
			SignatureToSignatureDeclarationOptions methodOpts;
			methodOpts.name = propertyName;
			methodOpts.questionToken = optionalToken;
			Node* methodDeclaration =
			    signatureToSignatureDeclarationHelper(
				    signature, Kind::MethodSignature, &methodOpts);
			Node* commentDecl = signature->declaration != nullptr
			                        ? signature->declaration
			                        : propertySymbol->valueDeclaration;
			setCommentRange(methodDeclaration, commentDecl);
			typeElements.push_back(methodDeclaration);
		}
		if (!signatures.empty() || optionalToken == nullptr) {
			return typeElements;
		}
	}
	Node* propertyTypeNode = nullptr;
	if (shouldUsePlaceholderForProperty(propertySymbol)) {
		propertyTypeNode = createElidedInformationPlaceholder();
	} else {
		if (propertyIsReverseMapped) {
			ctx->reverseMappedStack.push_back(propertySymbol);
		}
		if (propertyType != nullptr) {
			propertyTypeNode = serializeTypeForDeclaration(
				nullptr /*declaration*/, propertyType, propertySymbol,
				true);
		} else {
			propertyTypeNode = f->newKeywordTypeNode(Kind::AnyKeyword);
		}
		if (propertyIsReverseMapped) {
			ctx->reverseMappedStack.pop_back();
		}
	}

	ModifierList* modifiers = nullptr;
	if (ch->isReadonlySymbol(propertySymbol)) {
		modifiers =
		    f->newModifierList({f->newToken(Kind::ReadonlyKeyword)});
		ctx->approximateLength += 9;
	}
	Node* propertySignature = f->newPropertySignatureDeclaration(
		modifiers, propertyName, optionalToken, propertyTypeNode, nullptr);

	setCommentRange(propertySignature, propertySymbol->valueDeclaration);
	typeElements.push_back(propertySignature);

	return typeElements;
}

// createTypeNodesFromResolvedType (nodebuilderimpl.go:2690).
NodeList* NodeBuilderImpl::createTypeNodesFromResolvedType(
	StructuredType* resolvedType) {
	if (checkTruncationLength()) {
		if ((ctx->flags & nodebuilder::FlagsNoTruncation) != 0) {
			Node* elem = f->newNotEmittedTypeElement();
			return f->newNodeList({e->addSyntheticTrailingComment(
				elem, Kind::MultiLineCommentTrivia, "elided",
				false /*hasTrailingNewLine*/)});
		}
		return f->newNodeList({f->newPropertySignatureDeclaration(
			nullptr, f->newIdentifier("..."), nullptr, nullptr, nullptr)});
	}
	std::vector<Node*> typeElements;
	for (Signature* signature : callSignaturesOf(resolvedType)) {
		typeElements.push_back(signatureToSignatureDeclarationHelper(
			signature, Kind::CallSignature, nullptr));
	}
	for (Signature* signature : constructSignaturesOf(resolvedType)) {
		if ((signature->flags & SignatureFlagsAbstract) != 0) {
			continue;
		}
		typeElements.push_back(signatureToSignatureDeclarationHelper(
			signature, Kind::ConstructSignature, nullptr));
	}
	for (IndexInfo* info : resolvedType->indexInfos) {
		Node* typeNode = nullptr;
		if ((resolvedType->type_.objectFlags &
		     ObjectFlagsReverseMapped) != 0 &&
		    (ctx->flags &
		     nodebuilder::FlagsAllowAnonymousIdentifier) != 0) {
			typeNode = createElidedInformationPlaceholder();
		}
		std::vector<Node*> decls =
		    indexInfoToObjectComputedNamesOrSignatureDeclaration(
			    info, typeNode);
		typeElements.insert(typeElements.end(), decls.begin(),
		                    decls.end());
	}

	std::vector<Symbol*>& properties = resolvedType->properties;
	if (properties.empty()) {
		return f->newNodeList(typeElements);
	}

	int i = 0;
	for (Symbol* propertySymbol : properties) {
		if (isExpanding(ctx) &&
		    (propertySymbol->flags & SymbolFlagsPrototype) != 0) {
			continue;
		}
		i++;
		if ((ctx->flags &
		     nodebuilder::FlagsWriteClassExpressionAsTypeLiteral) != 0) {
			if ((propertySymbol->flags & SymbolFlagsPrototype) != 0) {
				continue;
			}
			if ((getDeclarationModifierFlagsFromSymbol(propertySymbol) &
			     (ModifierFlagsPrivate | ModifierFlagsProtected)) != 0) {
				ctx->tracker->ReportPrivateInBaseOfClassExpression(
					propertySymbol->name);
			}
			if (isPrivateIdentifierSymbol(propertySymbol)) {
				ctx->tracker->ReportPrivateInBaseOfClassExpression(
					symbolName(propertySymbol));
			}
		}
		if (checkTruncationLength() &&
		    (i + 2 < (int)properties.size() - 1)) {
			if ((ctx->flags & nodebuilder::FlagsNoTruncation) != 0) {
				typeElements.back() = e->addSyntheticTrailingComment(
					typeElements.back(), Kind::MultiLineCommentTrivia,
					"... " + std::to_string(properties.size() - i) +
					    " more elided ...",
					false /*hasTrailingNewLine*/);
			} else {
				std::string text =
				    "... " + std::to_string(properties.size() - i) +
				    " more ...";
				typeElements.push_back(
					f->newPropertySignatureDeclaration(
						nullptr, f->newIdentifier(text), nullptr,
						nullptr, nullptr));
			}
			typeElements = addPropertyToElementList(
				properties[properties.size() - 1], typeElements);
			break;
		}
		typeElements =
		    addPropertyToElementList(propertySymbol, typeElements);
	}
	if (!typeElements.empty()) {
		return f->newNodeList(typeElements);
	}
	return nullptr;
}

// createTypeNodeFromObjectType (nodebuilderimpl.go:2733).
Node* NodeBuilderImpl::createTypeNodeFromObjectType(Type* t) {
	if (ch->isGenericMappedType(t) ||
	    ((t->objectFlags & ObjectFlagsMapped) != 0 &&
	     t->AsMappedType()->containsError)) {
		return createMappedTypeNodeFromType(t);
	}

	StructuredType* resolved = ch->resolveStructuredTypeMembers(t);
	std::vector<Signature*> callSigs = callSignaturesOf(resolved);
	std::vector<Signature*> ctorSigs = constructSignaturesOf(resolved);
	if (resolved->properties.empty() && resolved->indexInfos.empty()) {
		if (callSigs.empty() && ctorSigs.empty()) {
			ctx->approximateLength += 2;
			Node* result = f->newTypeLiteralNode(f->newNodeList({}));
			e->setEmitFlags(result, printer::EFSingleLine);
			return result;
		}

		if (callSigs.size() == 1 && ctorSigs.empty()) {
			Signature* signature = callSigs[0];
			Node* signatureNode = signatureToSignatureDeclarationHelper(
				signature, Kind::FunctionType, nullptr);
			return signatureNode;
		}

		if (ctorSigs.size() == 1 && callSigs.empty()) {
			Signature* signature = ctorSigs[0];
			Node* signatureNode = signatureToSignatureDeclarationHelper(
				signature, Kind::ConstructorType, nullptr);
			return signatureNode;
		}
	}

	std::vector<Signature*> abstractSignatures;
	for (Signature* signature : ctorSigs) {
		if ((signature->flags & SignatureFlagsAbstract) != 0) {
			abstractSignatures.push_back(signature);
		}
	}
	if (!abstractSignatures.empty()) {
		std::vector<Type*> types;
		types.reserve(abstractSignatures.size() + 1);
		for (Signature* s : abstractSignatures) {
			types.push_back(ch->getOrCreateTypeFromSignature(s));
		}
		// count the number of type elements excluding abstract constructors
		int propertyCount;
		if ((ctx->flags &
		     nodebuilder::FlagsWriteClassExpressionAsTypeLiteral) != 0) {
			propertyCount = 0;
			for (Symbol* p : resolved->properties) {
				if ((p->flags & SymbolFlagsPrototype) == 0) {
					propertyCount++;
				}
			}
		} else {
			propertyCount = (int)resolved->properties.size();
		}
		int typeElementCount = (int)callSigs.size() +
		                       ((int)ctorSigs.size() -
		                        (int)abstractSignatures.size()) +
		                       (int)resolved->indexInfos.size() +
		                       propertyCount;
		// don't include an empty object literal if there were no other
		// static-side properties to write, i.e. `abstract class C { }`
		// becomes `abstract new () => {}` and not
		// `(abstract new () => {}) & {}`
		if (typeElementCount != 0) {
			// create a copy of the object type without any abstract
			// construct signatures.
			types.push_back(
				getResolvedTypeWithoutAbstractConstructSignatures(
					resolved));
		}
		return typeToTypeNode(ch->getIntersectionType(types));
	}

	auto restoreFlags = saveRestoreFlags();
	ctx->flags |= nodebuilder::FlagsInObjectTypeLiteral;
	NodeList* members = createTypeNodesFromResolvedType(resolved);
	restoreFlags();
	Node* typeLiteralNode = f->newTypeLiteralNode(members);
	ctx->approximateLength += 2;
	e->setEmitFlags(
		typeLiteralNode,
		(ctx->flags & nodebuilder::FlagsMultilineObjectLiterals) != 0
		    ? 0
		    : printer::EFSingleLine);
	return typeLiteralNode;
}

// shouldWriteTypeOfFunctionSymbol (nodebuilderimpl.go:2800).
bool NodeBuilderImpl::shouldWriteTypeOfFunctionSymbol(
	Symbol* symbol, TypeId typeId, Symbol** outSymbol) {
	// `typeof C.name` can only be written when the member name is a valid
	// identifier
	bool isStaticMethodSymbol =
	    (symbol->flags & SymbolFlagsMethod) != 0 &&
	    isIdentifierText(symbol->name, LanguageVariant::Standard) &&
	    [this, symbol]() {
			for (Node* declaration : symbol->declarations) {
				if (isStatic(declaration) &&
				    !ch->isLateBindableIndexSignature(
					    getNameOfDeclaration(declaration))) {
					return true;
				}
			}
			return false;
		}();
	bool isNonLocalFunctionSymbol = false;
	bool isFunctionExpressionSymbol = false;
	if ((symbol->flags & SymbolFlagsFunction) != 0) {
		if (symbol->parent != nullptr) {
			isNonLocalFunctionSymbol = true;
		} else {
			for (Node* declaration : symbol->declarations) {
				if (declaration->parent->kind == Kind::SourceFile ||
				    declaration->parent->kind == Kind::ModuleBlock) {
					isNonLocalFunctionSymbol = true;
					break;
				}
				if (isFunctionExpressionOrArrowFunction(declaration) &&
				    isVariableDeclaration(declaration->parent) &&
				    isVariableDeclarationList(
				        declaration->parent->parent) &&
				    isVariableStatement(
				        declaration->parent->parent->parent) &&
				    declaration->parent->parent->parent->parent !=
				        nullptr &&
				    (declaration->parent->parent->parent->parent
				             ->kind == Kind::SourceFile ||
				     declaration->parent->parent->parent->parent
				             ->kind == Kind::ModuleBlock)) {
					isNonLocalFunctionSymbol = true;
					isFunctionExpressionSymbol = true;
					break;
				}
			}
		}
	}
	if (isStaticMethodSymbol || isNonLocalFunctionSymbol) {
		if (isFunctionExpressionSymbol &&
		    symbol->valueDeclaration != nullptr &&
		    symbol->valueDeclaration->parent != nullptr &&
		    symbol->valueDeclaration->parent != ctx->enclosingDeclaration) {
			symbol = ch->getMergedSymbol(
				symbol->valueDeclaration->parent->symbol());
		}
		// typeof is allowed only for static/non local functions
		*outSymbol = symbol;
		return ((ctx->flags & nodebuilder::FlagsUseTypeOfFunction) != 0 ||
		        ctx->visitedTypes.count(typeId) > 0) &&
		       ((ctx->flags &
		         nodebuilder::FlagsUseStructuralFallback) == 0 ||
		        ch->IsValueSymbolAccessible(symbol,
		                                    ctx->enclosingDeclaration));
	}
	*outSymbol = symbol;
	return false;
}

// createAnonymousTypeNode (nodebuilderimpl.go:2831).
Node* NodeBuilderImpl::createAnonymousTypeNode(Type* t) {
	return createAnonymousTypeNodeEx(t, false, false);
}

// shouldEmitTypeOfSymbol (nodebuilderimpl.go:2835).
bool NodeBuilderImpl::shouldEmitTypeOfSymbol(
	bool forceExpansion, bool forceClassExpansion,
	SymbolFlags isInstanceType, Symbol* symbol, TypeId typeId,
	Symbol** outSymbol) {
	if (forceExpansion) {
		*outSymbol = symbol;
		return false;
	}
	bool nonFunctionResult =
	    ((symbol->flags & SymbolFlagsClass) != 0 && !forceClassExpansion &&
	    ch->getBaseTypeVariableOfClass(symbol) == nullptr &&
	    !(symbol->valueDeclaration != nullptr &&
	      isClassLike(symbol->valueDeclaration) &&
	      (ctx->flags &
	       nodebuilder::FlagsWriteClassExpressionAsTypeLiteral) != 0 &&
	      (!isClassDeclaration(symbol->valueDeclaration) ||
	       ch->IsSymbolAccessible(symbol, ctx->enclosingDeclaration,
	                              isInstanceType,
	                              false /*shouldComputeAliasesToMakeVisible*/)
	               .Accessibility != printer::SymbolAccessibility::Accessible))) ||
	    (symbol->flags &
	     (SymbolFlagsEnum | SymbolFlagsValueModule)) != 0;
	if (nonFunctionResult) {
		*outSymbol = symbol;
		return true;
	}
	return shouldWriteTypeOfFunctionSymbol(symbol, typeId, outSymbol);
}

// createAnonymousTypeNodeEx (nodebuilderimpl.go:2843).
Node* NodeBuilderImpl::createAnonymousTypeNodeEx(Type* t,
                                                 bool forceClassExpansion,
                                                 bool forceExpansion) {
	TypeId typeId = t->id;
	Symbol* symbol = t->symbol;
	if (symbol != nullptr) {
		bool isInstantiationExpressionType =
		    (t->objectFlags &
		     ObjectFlagsInstantiationExpressionType) != 0;
		if (isInstantiationExpressionType) {
			InstantiationExpressionType* instantiationExpressionType =
			    t->AsInstantiationExpressionType();
			Node* existing = instantiationExpressionType->node;
			//  instantiationExpressionType.node is unreliable for
			//  constituents of unions and intersections.
			// declare const Err: typeof ErrImpl & (<T>() => T);
			// type ErrAlias<U> = typeof Err<U>;
			// declare const e: ErrAlias<number>;
			// ErrAlias<number> = typeof Err<number> = typeof ErrImpl &
			// (<number>() => number)
			// The problem is each constituent of the intersection will be
			// associated with typeof Err<number>
			// And when extracting a type for typeof ErrImpl from typeof
			// Err<number> does not make sense.
			if (isTypeQueryNode(existing) &&
			    getTypeFromTypeNode(existing, false) == t) {
				// Guard against unbounded recursion when the existing
				// typeof node fails to be reused (e.g. its entity name
				// isn't accessible from this scope) and the recovery
				// boundary's fallback re-enters typeToTypeNode with the
				// very same instantiation type, which would in turn try to
				// reuse the same node again. Mark the type as visited
				// around the reuse attempt so the inner recursion bottoms
				// out via the visitedTypes guard below.
				if (ctx->visitedTypes.count(typeId)) {
					return createCyclicStructurePlaceholder();
				}
				ctx->visitedTypes.insert(typeId);
				Node* typeNode = tryReuseExistingNonParameterTypeNode(
					existing, t, nullptr, nullptr);
				ctx->visitedTypes.erase(typeId);
				if (typeNode != nullptr) {
					return typeNode;
				}
			}
			if (ctx->visitedTypes.count(typeId)) {
				return createCyclicStructurePlaceholder();
			}
			return visitAndTransformType(
				t, &NodeBuilderImpl::createTypeNodeFromObjectType);
		}
		SymbolFlags isInstanceType;
		if (isClassInstanceSide(ch, t)) {
			isInstanceType = SymbolFlagsType;
		} else {
			isInstanceType = SymbolFlagsValue;
		}

		// !!! JS support
		// if c.isJSConstructor(symbol.ValueDeclaration) {
		// 	// Instance and static types share the same symbol; only add
		// 	// 'typeof' for the static side.
		// 	return b.symbolToTypeNode(symbol, isInstanceType, nil)
		// } else
		Symbol* emitSymbol = symbol;
		if (shouldEmitTypeOfSymbol(forceExpansion, forceClassExpansion,
		                           isInstanceType, symbol, typeId,
		                           &emitSymbol)) {
			if (shouldExpandType(t, false /*isAlias*/)) {
				ctx->depth++;
			} else {
				return symbolToTypeNode(emitSymbol, isInstanceType,
				                        nullptr);
			}
		}
		if (ctx->visitedTypes.count(typeId)) {
			// If type is an anonymous type literal in a type alias
			// declaration, use type alias name
			Symbol* typeAlias = getTypeAliasForTypeLiteral(ch, t);
			if (typeAlias != nullptr) {
				// The specified symbol flags need to be reinterpreted as
				// type flags
				return symbolToTypeNode(typeAlias, SymbolFlagsType,
				                        nullptr);
			} else {
				return createCyclicStructurePlaceholder();
			}
		} else {
			return visitAndTransformType(
				t, &NodeBuilderImpl::createTypeNodeFromObjectType);
		}
	} else if ((t->objectFlags & ObjectFlagsReverseMapped) != 0 &&
	           (ctx->flags &
	            nodebuilder::FlagsAllowAnonymousIdentifier) == 0) {
		if (ctx->visitedTypes.count(typeId)) {
			return createCyclicStructurePlaceholder();
		}
		return visitAndTransformType(
			t, &NodeBuilderImpl::createTypeNodeFromObjectType);
	} else {
		// Reverse mapped types use property and index signature
		// placeholders for display.
		return createTypeNodeFromObjectType(t);
	}
}

// getTypeFromTypeNode (nodebuilderimpl.go:2896).
Type* NodeBuilderImpl::getTypeFromTypeNode(Node* node,
                                           bool noMappedTypes) {
	// !!! noMappedTypes optional param support
	if (node->parent == nullptr) {
		return ch->errorType;
	}
	Type* t = ch->getTypeFromTypeNode(node);
	if (ctx->mapper == nullptr) {
		return t;
	}

	Type* instantiated = ch->instantiateType(t, ctx->mapper);
	if (noMappedTypes && instantiated != t) {
		return nullptr;
	}
	return instantiated;
}

// typeToTypeNodeOrCircularityElision (nodebuilderimpl.go:2912).
Node* NodeBuilderImpl::typeToTypeNodeOrCircularityElision(Type* t) {
	if ((t->flags & TypeFlagsUnion) != 0) {
		if (ctx->visitedTypes.count(t->id)) {
			return createCyclicStructurePlaceholder();
		}
		return visitAndTransformType(t, &NodeBuilderImpl::typeToTypeNode);
	}
	return typeToTypeNode(t);
}

// createCyclicStructurePlaceholder (nodebuilderimpl.go:3014).
Node* NodeBuilderImpl::createCyclicStructurePlaceholder() {
	if ((ctx->flags & nodebuilder::FlagsAllowAnonymousIdentifier) == 0) {
		ctx->encounteredError = true;
		ctx->tracker->ReportCyclicStructureError();
	}
	return createElidedInformationPlaceholder();
}

// conditionalTypeToTypeNode (nodebuilderimpl.go:2925).
Node* NodeBuilderImpl::conditionalTypeToTypeNode(Type* _t) {
	if (checkTruncationLength()) {
		return createElidedInformationPlaceholder();
	}
	ConditionalType* t = _t->AsConditionalType();
	Node* checkTypeNode = typeToTypeNode(t->checkType);
	ctx->approximateLength += 15;
	if ((ctx->flags &
	     nodebuilder::FlagsGenerateNamesForShadowedTypeParams) != 0 &&
	    t->root->isDistributive &&
	    (t->checkType->flags & TypeFlagsTypeParameter) == 0) {
		Type* newParam = ch->newTypeParameter(
			ch->newSymbol(SymbolFlagsTypeParameter, "T"));
		Node* name = typeParameterToName(newParam);
		Node* newTypeVariable =
		    f->newTypeReferenceNode(name, nullptr);
		ctx->approximateLength += 37;
		// 15 each for two added conditionals, 7 for an added infer type
		TypeMapper* newMapper =
		    prependTypeMapping(t->root->checkType, newParam, t->mapper);
		std::vector<Type*> saveInferTypeParameters =
		    ctx->inferTypeParameters;
		ctx->inferTypeParameters = t->root->inferTypeParameters;
		Node* extendsTypeNode = typeToTypeNode(
			ch->instantiateType(t->root->extendsType, newMapper));
		ctx->inferTypeParameters = saveInferTypeParameters;
		Node* trueTypeNode = typeToTypeNodeOrCircularityElision(
			ch->instantiateType(
				getTypeFromTypeNode(t->root->node->as<ConditionalTypeNode>()->TrueType, false),
				newMapper));
		Node* falseTypeNode = typeToTypeNodeOrCircularityElision(
			ch->instantiateType(
				getTypeFromTypeNode(t->root->node->as<ConditionalTypeNode>()->FalseType, false),
				newMapper));

		// outermost conditional makes `T` a type parameter, allowing the
		// inner conditionals to be distributive
		// second conditional makes `T` have `T & checkType` substitution,
		// so it is correctly usable as the checkType
		// inner conditional runs the check the user provided on the check
		// type (distributively) and returns the result
		// checkType extends infer T ? T extends checkType ? T extends
		// extendsType<T> ? trueType<T> : falseType<T> : never : never;
		// this is potentially simplifiable to
		// checkType extends infer T ? T extends checkType & extendsType<T>
		// ? trueType<T> : falseType<T> : never;
		// but that may confuse users who read the output more.
		// On the other hand,
		// checkType extends infer T extends checkType ? T extends
		// extendsType<T> ? trueType<T> : falseType<T> : never;
		// may also work with `infer ... extends ...` in, but would produce
		// declarations only compatible with the latest TS.
		Node* newId = newTypeVariable->as<TypeReferenceNode>()
		                  ->TypeName->as<Identifier>()
		                  ->clone(*f);
		Node* syntheticExtendsNode = f->newInferTypeNode(
			f->newTypeParameterDeclaration(nullptr, newId, nullptr,
			                               nullptr, nullptr));
		Node* innerCheckConditionalNode = f->newConditionalTypeNode(
			newTypeVariable, extendsTypeNode, trueTypeNode,
			falseTypeNode);
		Node* syntheticTrueNode = f->newConditionalTypeNode(
			f->newTypeReferenceNode(name->clone(*f), nullptr),
			deepCloneNode(*f, checkTypeNode), innerCheckConditionalNode,
			f->newKeywordTypeNode(Kind::NeverKeyword));
		return f->newConditionalTypeNode(
			checkTypeNode, syntheticExtendsNode, syntheticTrueNode,
			f->newKeywordTypeNode(Kind::NeverKeyword));
	}
	std::vector<Type*> saveInferTypeParameters = ctx->inferTypeParameters;
	ctx->inferTypeParameters = t->root->inferTypeParameters;
	Node* extendsTypeNode = typeToTypeNode(t->extendsType);
	ctx->inferTypeParameters = saveInferTypeParameters;
	Node* trueTypeNode = typeToTypeNodeOrCircularityElision(
		ch->getTrueTypeFromConditionalType(_t));
	Node* falseTypeNode = typeToTypeNodeOrCircularityElision(
		ch->getFalseTypeFromConditionalType(_t));
	return f->newConditionalTypeNode(checkTypeNode, extendsTypeNode,
	                                 trueTypeNode, falseTypeNode);
}

// getParentSymbolOfTypeParameter (nodebuilderimpl.go:2973).
Symbol* NodeBuilderImpl::getParentSymbolOfTypeParameter(
	Type* typeParameter) {
	Node* tp = getDeclarationOfKind(typeParameter->symbol,
	                              Kind::TypeParameter);
	Node* host;
	// !!! JSDoc support
	host = tp->parent;
	if (host == nullptr) {
		return nullptr;
	}
	return ch->getSymbolOfNode(host);
}

// ---------------------------------------------------------------------------
// keyBuilder/getTypeListKey — same content-keyed hash as the other checker
// TUs (checker.go). Used for CompositeTypeCacheIdentity.inferTypeParameters.
namespace {
struct keyBuilder {
	std::string buf;
	void writeUint64(uint64_t v) {
		char b[8];
		std::memcpy(b, &v, 8);
		buf.append(b, 8);
	}
	void writeInt(int v) { writeUint64(static_cast<uint64_t>(v)); }
	void writeType(Type* t) {
		uint32_t id = static_cast<uint32_t>(t->id);
		char b[4];
		std::memcpy(b, &id, 4);
		buf.append(b, 4);
	}
	void writeTypes(const std::vector<Type*>& types) {
		writeInt(static_cast<int>(types.size()));
		for (Type* t : types) {
			writeType(t);
		}
	}
	CacheKey hash() {
		CacheKey key;
		uint64_t v = 0;
		int shift = 0;
		for (char c : buf) {
			v |= static_cast<uint64_t>(static_cast<uint8_t>(c)) << (shift * 8);
			if (++shift == 8) {
				key.w.push_back(v);
				v = 0;
				shift = 0;
			}
		}
		if (shift != 0) {
			key.w.push_back(v);
		}
		key.w.push_back(buf.size());
		return key;
	}
};

CacheKey getTypeListKey(const std::vector<Type*>& types) {
	keyBuilder b;
	b.writeTypes(types);
	return b.hash();
}
}  // namespace

// arrayOrTupleTypeToNode (nodebuilderimpl.go:3088).
Node* NodeBuilderImpl::arrayOrTupleTypeToNode(Type* t) {
	std::vector<Type*> typeArguments = ch->getTypeArguments(t);
	if (t->Target() == ch->globalArrayType ||
	    t->Target() == ch->globalReadonlyArrayType) {
		if ((ctx->flags & nodebuilder::FlagsWriteArrayAsGenericType) != 0) {
			Node* typeArgumentNode = typeToTypeNode(typeArguments[0]);
			return f->newTypeReferenceNode(
				newIdentifier(
					t->Target() == ch->globalArrayType ? "Array"
					                                   : "ReadonlyArray",
					t->Target()->symbol),
				f->newNodeList({typeArgumentNode}));
		}
		Node* elementType = typeToTypeNode(typeArguments[0]);
		Node* arrayType = f->newArrayTypeNode(elementType);
		if (t->Target() == ch->globalArrayType) {
			return arrayType;
		} else {
			return f->newTypeOperatorNode(Kind::ReadonlyKeyword, arrayType);
		}
	} else {
		TSC_ASSERT((t->Target()->objectFlags & ObjectFlagsTuple) != 0,
		           "expected array or tuple type");
		bool same = true;
		std::vector<Type*> newArgs(typeArguments.begin(),
		                           typeArguments.end());
		for (size_t i = 0; i < typeArguments.size(); i++) {
			bool isOptional = false;
			if (i < t->Target()->AsTupleType()->elementInfos.size()) {
				isOptional = (t->Target()
				                  ->AsTupleType()
				                  ->elementInfos[i]
				                  .flags &
				              ElementFlagsOptional) != 0;
			}
			Type* nt = ch->removeMissingType(typeArguments[i], isOptional);
			if (nt != typeArguments[i]) {
				same = false;
			}
			newArgs[i] = nt;
		}
		if (same) {
			typeArguments = std::move(newArgs);
		} else {
			typeArguments = std::move(newArgs);
		}
		if (!typeArguments.empty()) {
			int arity = ch->getTypeReferenceArity(t);
			NodeList* tupleConstituentNodes = mapToTypeNodes(
				std::vector<Type*>(typeArguments.begin(),
				                   typeArguments.begin() + arity),
				false /*isBareList*/);
			if (tupleConstituentNodes != nullptr) {
				for (size_t i = 0; i < tupleConstituentNodes->nodes.size();
				     i++) {
					ElementFlags flags = t->Target()
					                         ->AsTupleType()
					                         ->elementInfos[i]
					                         .flags;
					Node* labeledElementDeclaration =
					    t->Target()
					        ->AsTupleType()
					        ->elementInfos[i]
					        .labeledDeclaration;

					if (labeledElementDeclaration != nullptr) {
						tupleConstituentNodes->nodes[i] =
						    f->newNamedTupleMember(
							    (flags & ElementFlagsVariable) != 0
							        ? f->newToken(Kind::DotDotDotToken)
							        : nullptr,
							    newIdentifier(
								    ch->getTupleElementLabel(
									    t->Target()
									        ->AsTupleType()
									        ->elementInfos[i],
									    nullptr, (int)i),
								    nullptr /*symbol*/),
							    (flags & ElementFlagsOptional) != 0
							        ? f->newToken(Kind::QuestionToken)
							        : nullptr,
							    (flags & ElementFlagsRest) != 0
							        ? f->newArrayTypeNode(
										  tupleConstituentNodes->nodes[i])
							        : tupleConstituentNodes->nodes[i]);
					} else {
						if ((flags & ElementFlagsVariable) != 0) {
							tupleConstituentNodes->nodes[i] =
							    f->newRestTypeNode(
								    (flags & ElementFlagsRest) != 0
								        ? f->newArrayTypeNode(
											  tupleConstituentNodes
											      ->nodes[i])
								        : tupleConstituentNodes->nodes[i]);
						} else if ((flags & ElementFlagsOptional) != 0) {
							tupleConstituentNodes->nodes[i] =
							    f->newOptionalTypeNode(
								    tupleConstituentNodes->nodes[i]);
						}
					}
				}
				Node* tupleTypeNode =
				    f->newTupleTypeNode(tupleConstituentNodes);
				e->setEmitFlags(tupleTypeNode, printer::EFSingleLine);
				if (t->Target()->AsTupleType()->readonly) {
					return f->newTypeOperatorNode(Kind::ReadonlyKeyword,
					                              tupleTypeNode);
				} else {
					return tupleTypeNode;
				}
			}
		}
		if (ctx->encounteredError ||
		    (ctx->flags & nodebuilder::FlagsAllowEmptyTuple) != 0) {
			Node* tupleTypeNode = f->newTupleTypeNode(f->newNodeList({}));
			e->setEmitFlags(tupleTypeNode, printer::EFSingleLine);
			if (t->Target()->AsTupleType()->readonly) {
				return f->newTypeOperatorNode(Kind::ReadonlyKeyword,
				                              tupleTypeNode);
			} else {
				return tupleTypeNode;
			}
		}
		ctx->encounteredError = true;
		return nullptr;
		// TODO: GH#18217
	}
}

// typeReferenceToTypeNode (nodebuilderimpl.go:3160).
Node* NodeBuilderImpl::typeReferenceToTypeNode(Type* t) {
	std::vector<Type*> typeArguments = ch->getTypeArguments(t);
	if ((ctx->flags &
	            nodebuilder::FlagsWriteClassExpressionAsTypeLiteral) != 0 &&
	           t->symbol->valueDeclaration != nullptr &&
	           isClassLike(t->symbol->valueDeclaration) &&
	           !ch->IsValueSymbolAccessible(t->symbol,
	                                        ctx->enclosingDeclaration)) {
		return createAnonymousTypeNode(t);
	} else {
		std::vector<Type*> outerTypeParameters =
		    interfaceTypeOuterTypeParameters(t->Target()->AsInterfaceType());
		int i = 0;
		Node* resultType = nullptr;
		if (!outerTypeParameters.empty()) {
			int length = (int)outerTypeParameters.size();
			while (i < length) {
				// Find group of type arguments for type parameters with the
				// same declaring container.
				int start = i;
				Symbol* parent = getParentSymbolOfTypeParameter(
					outerTypeParameters[i]);
				for (bool ok = true; ok;
				     ok = i < length &&
				          getParentSymbolOfTypeParameter(
					          outerTypeParameters[i]) == parent) { // do-while
																  // loop
					i++;
				}
				// When type parameters are their own type arguments for the
				// whole group (i.e. we have the default outer type
				// arguments), we don't show the group.

				bool groupEqual =
				    (i - start) <= (int)typeArguments.size() &&
				    std::equal(outerTypeParameters.begin() + start,
				               outerTypeParameters.begin() + i,
				               typeArguments.begin() + start);
				if (!groupEqual) {
					NodeList* typeArgumentSlice = mapToTypeNodes(
						std::vector<Type*>(typeArguments.begin() + start,
						                   typeArguments.begin() + i),
						false /*isBareList*/);
					auto restoreFlags = saveRestoreFlags();
					ctx->flags |=
					    nodebuilder::FlagsForbidIndexedAccessSymbolReferences;
					Node* ref = symbolToTypeNode(
						parent, SymbolFlagsType, typeArgumentSlice);
					restoreFlags();
					if (resultType == nullptr) {
						resultType = ref;
					} else {
						resultType =
						    appendReferenceToType(resultType, ref);
					}
				}
			}
		}
		NodeList* typeArgumentNodes = nullptr;
		if (!typeArguments.empty()) {
			int typeParameterCount = 0;
			std::vector<Type*> typeParams =
			    interfaceTypeTypeParameters(
				    t->Target()->AsInterfaceType());
			if (!typeParams.empty()) {
				typeParameterCount =
				    (int)std::min(typeParams.size(),
				                  typeArguments.size());

				// Maybe we should do this for more types, but for now we
				// only elide type arguments that are identical to their
				// associated type parameters' defaults for `Iterable`,
				// `IterableIterator`, `AsyncIterable`, and
				// `AsyncIterableIterator` to provide backwards-compatible
				// .d.ts emit due to each now having three type parameters
				// instead of only one.
				if (ch->isReferenceToType(t,
				                          ch->getGlobalIterableType()) ||
				    ch->isReferenceToType(
					    t, ch->getGlobalIterableIteratorType()) ||
				    ch->isReferenceToType(
					    t, ch->getGlobalAsyncIterableType()) ||
				    ch->isReferenceToType(
					    t, ch->getGlobalAsyncIterableIteratorType())) {
					if (t->AsTypeReference()->node == nullptr ||
					    !isTypeReferenceNode(
						    t->AsTypeReference()->node) ||
					    t->AsTypeReference()
					            ->node->typeArguments()
					            .empty() ||
					    (int)t->AsTypeReference()
					            ->node->typeArguments()
					            .size() <
					        typeParameterCount) {
						for (;
						     typeParameterCount > 0;) {
							Type* typeArgument =
							    typeArguments[typeParameterCount - 1];
							Type* typeParameter =
							    interfaceTypeTypeParameters(
								    t->Target()->AsInterfaceType())
								    [typeParameterCount - 1];
							Type* defaultType =
							    ch->getDefaultFromTypeParameter(
								    typeParameter);
							if (defaultType == nullptr ||
							    !ch->isTypeIdenticalTo(typeArgument,
							                           defaultType)) {
								break;
							}
							typeParameterCount--;
						}
					}
				}
			}

			typeArgumentNodes = mapToTypeNodes(
				std::vector<Type*>(typeArguments.begin() + i,
				                   typeArguments.begin() +
				                       typeParameterCount),
				false /*isBareList*/);
		}
		auto restoreFlags = saveRestoreFlags();
		ctx->flags |=
		    nodebuilder::FlagsForbidIndexedAccessSymbolReferences;
		Node* finalRef = symbolToTypeNode(t->symbol, SymbolFlagsType,
		                                  typeArgumentNodes);
		restoreFlags();
		if (resultType == nullptr) {
			return finalRef;
		} else {
			return appendReferenceToType(resultType, finalRef);
		}
	}
}

// visitAndTransformType (nodebuilderimpl.go:3200).
Node* NodeBuilderImpl::visitAndTransformType(
	Type* t, Node* (NodeBuilderImpl::*transform)(Type*)) {
	if (checkTruncationLength()) {
		return createElidedInformationPlaceholder();
	}

	TypeId typeId = t->id;
	bool isArrayOrTuple = ch->isArrayOrTupleType(t);
	if (isArrayOrTuple) {
		// Deferred and regular references share a cycle identity.
		typeId = ch->createTypeReference(t->Target(),
		                                 ch->getTypeArguments(t))
		             ->id;
	}
	if (ctx->visitedTypes.count(typeId)) {
		return createCyclicStructurePlaceholder();
	}

	bool isConstructorObject =
	    (t->objectFlags & ObjectFlagsAnonymous) != 0 &&
	    t->symbol != nullptr &&
	    (t->symbol->flags & SymbolFlagsClass) != 0;
	std::optional<CompositeSymbolIdentity> id;
	if (isArrayOrTuple) {
		// Do not bound finite container nesting by the shared Array
		// symbol or tuple origin.
		id = std::nullopt;
	} else if ((t->objectFlags & ObjectFlagsReference) != 0 &&
	    t->AsTypeReference()->node != nullptr) {
		id = CompositeSymbolIdentity{
			false, 0, getNodeId(t->AsTypeReference()->node)};
	} else if ((t->flags & TypeFlagsConditional) != 0) {
		id = CompositeSymbolIdentity{
			false, 0,
			getNodeId(t->AsConditionalType()->root->node)};
	} else if (t->symbol != nullptr) {
		id = CompositeSymbolIdentity{isConstructorObject,
		                             getSymbolId(t->symbol), 0};
	}
	// Since instantiations of the same anonymous type have the same symbol,
	// tracking symbols instead of types allows us to catch circular
	// references to instantiations of the same anonymous type

	CompositeTypeCacheIdentity key{typeId, ctx->flags, ctx->internalFlags,
	                               {}};
	if (!ctx->inferTypeParameters.empty()) {
		key.inferTypeParameters = getTypeListKey(ctx->inferTypeParameters);
	}
	// Don't rely on type cache if we're expanding a type, because we need to
	// compute `canIncreaseExpansionDepth`.
	bool canUseCache = ctx->maxExpansionDepth < 0;
	if (canUseCache && ctx->enclosingDeclaration != nullptr &&
	    links.Has(ctx->enclosingDeclaration)) {
		NodeBuilderLinks* enclosingLinks =
		    links.Get(ctx->enclosingDeclaration);
		auto it = enclosingLinks->serializedTypes.find(key);
		if (it != enclosingLinks->serializedTypes.end()) {
			SerializedTypeEntry* cachedResult = it->second;
			// TODO:: check if we instead store late painted statements
			// associated with this?
			for (TrackedSymbolArgs* arg : cachedResult->trackedSymbols) {
				ctx->tracker->TrackSymbol(arg->symbol,
				                          arg->enclosingDeclaration,
				                          arg->meaning);
			}
			if (cachedResult->truncating) {
				ctx->truncating = true;
			}
			ctx->approximateLength += cachedResult->addedLength;
			return deepCloneNode(*f, cachedResult->node);
		}
	}

	bool pushedOriginDepth = false;
	CompositeSymbolIdentity origin{};
	int originDepth = 0;
	if ((t->objectFlags & ObjectFlagsReverseMapped) != 0) {
		// Growing type arguments can prevent a reverse mapped type
		// from repeating. Bound expansion by its mapped declaration
		// as well as its type identity.
		origin = CompositeSymbolIdentity{
		    false, 0,
		    getNodeId(t->AsReverseMappedType()
		                  ->mappedType->AsMappedType()
		                  ->declaration)};
		originDepth = ctx->symbolDepth[origin];
		if (originDepth >= 100) {
			ctx->truncating = true;
			return createElidedInformationPlaceholder();
		}
		ctx->symbolDepth[origin] = originDepth + 1;
		pushedOriginDepth = true;
	}
	// Go `defer`: restore runs at function exit (after transform), not at
	// the end of the if-block.
	auto restoreOriginDepth = scopeExit(
	    [this, origin, originDepth, pushedOriginDepth] {
		    if (pushedOriginDepth) {
			    ctx->symbolDepth[origin] = originDepth;
		    }
	    });

	int depth = 0;
	if (id.has_value()) {
		depth = ctx->symbolDepth[*id];
		if (depth > 10) {
			ctx->truncating = true;
			return createElidedInformationPlaceholder();
		}
		ctx->symbolDepth[*id] = depth + 1;
	}
	ctx->visitedTypes.insert(typeId);
	std::vector<TrackedSymbolArgs*> prevTrackedSymbols =
	    ctx->trackedSymbols;
	ctx->trackedSymbols.clear();
	int startLength = ctx->approximateLength;
	Node* result = (this->*transform)(t);
	int addedLength = ctx->approximateLength - startLength;
	if (canUseCache && !ctx->reportedDiagnostic &&
	    !ctx->encounteredError) {
		NodeBuilderLinks* enclosingLinks =
		    links.Get(ctx->enclosingDeclaration);
		auto* entry = own(new SerializedTypeEntry());
		entry->node = result;
		entry->truncating = ctx->truncating;
		entry->addedLength = addedLength;
		entry->trackedSymbols = ctx->trackedSymbols;
		enclosingLinks->serializedTypes[key] = entry;
	}
	ctx->visitedTypes.erase(typeId);
	if (id.has_value()) {
		ctx->symbolDepth[*id] = depth;
	}
	ctx->trackedSymbols = prevTrackedSymbols;
	return result;
}

// typeToTypeNode (nodebuilderimpl.go:3299).
Node* NodeBuilderImpl::typeToTypeNode(Type* t) {
	nodebuilder::Flags inTypeAlias =
	    ctx->flags & nodebuilder::FlagsInTypeAlias;
	ctx->flags &= ~nodebuilder::FlagsInTypeAlias;

	if (t == nullptr) {
		if ((ctx->flags &
		     nodebuilder::FlagsAllowEmptyUnionOrIntersection) == 0) {
			ctx->encounteredError = true;
			return nullptr;
			// TODO: GH#18217
		}
		ctx->approximateLength += 3;
		return f->newKeywordTypeNode(Kind::AnyKeyword);
	}

	t = getNonDistributedTypeParameter(t);

	// Push type onto typeStack for expansion depth tracking
	bool pushedTypeStack = false;
	if (ctx->maxExpansionDepth >= 0) {
		ctx->typeStack.push_back(t);
		pushedTypeStack = true;
	}
	auto popTypeStack = scopeExit([this, pushedTypeStack]() {
		if (pushedTypeStack) {
			ctx->typeStack.pop_back();
		}
	});

	if ((ctx->flags & nodebuilder::FlagsNoTypeReduction) == 0) {
		t = ch->getReducedType(t);
	}

	if ((t->flags & TypeFlagsAny) != 0) {
		if (t->alias != nullptr) {
			return t->alias->toTypeReferenceNode(this);
		}
		if (t == ch->unresolvedType) {
			return e->addSyntheticLeadingComment(
				f->newKeywordTypeNode(Kind::AnyKeyword),
				Kind::MultiLineCommentTrivia, "unresolved",
				false /*hasTrailingNewLine*/);
		}
		ctx->approximateLength += 3;
		return f->newKeywordTypeNode(
			t == ch->intrinsicMarkerType ? Kind::IntrinsicKeyword
			                             : Kind::AnyKeyword);
	}
	if ((t->flags & TypeFlagsUnknown) != 0) {
		return f->newKeywordTypeNode(Kind::UnknownKeyword);
	}
	if ((t->flags & TypeFlagsString) != 0) {
		ctx->approximateLength += 6;
		return f->newKeywordTypeNode(Kind::StringKeyword);
	}
	if ((t->flags & TypeFlagsNumber) != 0) {
		ctx->approximateLength += 6;
		return f->newKeywordTypeNode(Kind::NumberKeyword);
	}
	if ((t->flags & TypeFlagsBigInt) != 0) {
		ctx->approximateLength += 6;
		return f->newKeywordTypeNode(Kind::BigIntKeyword);
	}
	if ((t->flags & TypeFlagsBoolean) != 0 && t->alias == nullptr) {
		ctx->approximateLength += 7;
		return f->newKeywordTypeNode(Kind::BooleanKeyword);
	}
	bool expandingEnum = false;
	if ((t->flags & TypeFlagsEnumLike) != 0) {
		if ((t->symbol->flags & SymbolFlagsEnumMember) != 0) {
			Symbol* parentSymbol = ch->getParentOfSymbol(t->symbol);
			Node* parentName =
			    symbolToTypeNode(parentSymbol, SymbolFlagsType, nullptr);
			if (ch->getDeclaredTypeOfSymbol(parentSymbol) == t) {
				return parentName;
			}
			std::string memberName = symbolName(t->symbol);
			if (isIdentifierText(memberName,
			                     LanguageVariant::Standard)) {
				return appendReferenceToType(
					parentName /* as TypeReference | ImportTypeNode */,
					f->newTypeReferenceNode(
						f->newIdentifier(memberName),
						nullptr /*typeArguments*/));
			}
			if (isImportTypeNode(parentName)) {
				parentName->as<ImportTypeNode>()->IsTypeOf = true;
				// mutably update, node is freshly manufactured anyhow
				return f->newIndexedAccessTypeNode(
					parentName,
					f->newLiteralTypeNode(
						newStringLiteral(memberName)));
			} else if (isTypeReferenceNode(parentName)) {
				return f->newIndexedAccessTypeNode(
					f->newTypeQueryNode(
						parentName->as<TypeReferenceNode>()
						    ->TypeName,
						nullptr),
					f->newLiteralTypeNode(
						newStringLiteral(memberName)));
			} else {
				TSC_UNREACHABLE(
					"Unhandled type node kind returned from "
					"`symbolToTypeNode`.");
			}
		}
		if ((t->flags & TypeFlagsUnion) == 0 ||
		    !shouldExpandType(t, false /*isAlias*/)) {
			return symbolToTypeNode(t->symbol, SymbolFlagsType, nullptr);
		}
		expandingEnum = true;
	}
	if ((t->flags & TypeFlagsStringLiteral) != 0) {
		std::string value =
		    std::get<std::string>(t->AsLiteralType()->value);
		ctx->approximateLength += (int)value.size() + 2;
		Node* lit = newStringLiteral(value);
		e->addEmitFlags(lit, printer::EFNoAsciiEscaping);
		return f->newLiteralTypeNode(lit);
	}
	if ((t->flags & TypeFlagsNumberLiteral) != 0) {
		Number value = std::get<Number>(t->AsLiteralType()->value);
		std::string valueStr = value.string();
		ctx->approximateLength += (int)valueStr.size();
		if (value < Number(0)) {
			return f->newLiteralTypeNode(f->newPrefixUnaryExpression(
				Kind::MinusToken,
				f->newNumericLiteral(valueStr.substr(1),
				                     TokenFlagsNone)));
		} else {
			return f->newLiteralTypeNode(
				f->newNumericLiteral(valueStr, TokenFlagsNone));
		}
	}
	if ((t->flags & TypeFlagsBigIntLiteral) != 0) {
		ctx->approximateLength +=
		    (int)pseudoBigIntToString(getBigIntLiteralValue(t)).size() + 1;
		return f->newLiteralTypeNode(f->newBigIntLiteral(
			pseudoBigIntToString(getBigIntLiteralValue(t)) + "n",
			TokenFlagsNone));
	}
	if ((t->flags & TypeFlagsBooleanLiteral) != 0) {
		if (std::get<bool>(t->AsLiteralType()->value)) {
			ctx->approximateLength += 4;
			return f->newLiteralTypeNode(
				f->newKeywordExpression(Kind::TrueKeyword));
		} else {
			ctx->approximateLength += 5;
			return f->newLiteralTypeNode(
				f->newKeywordExpression(Kind::FalseKeyword));
		}
	}
	if ((t->flags & TypeFlagsUniqueESSymbol) != 0) {
		if ((ctx->flags &
		     nodebuilder::FlagsAllowUniqueESSymbolType) == 0) {
			if (ch->IsValueSymbolAccessible(t->symbol,
			                                ctx->enclosingDeclaration)) {
				ctx->approximateLength += 6;
				return symbolToTypeNode(t->symbol, SymbolFlagsValue,
				                        nullptr);
			}
			ctx->tracker->ReportInaccessibleUniqueSymbolError();
		}
		ctx->approximateLength += 13;
		return f->newTypeOperatorNode(
			Kind::UniqueKeyword,
			f->newKeywordTypeNode(Kind::SymbolKeyword));
	}
	if ((t->flags & TypeFlagsVoid) != 0) {
		ctx->approximateLength += 4;
		return f->newKeywordTypeNode(Kind::VoidKeyword);
	}
	if ((t->flags & TypeFlagsUndefined) != 0) {
		ctx->approximateLength += 9;
		return f->newKeywordTypeNode(Kind::UndefinedKeyword);
	}
	if ((t->flags & TypeFlagsNull) != 0) {
		ctx->approximateLength += 4;
		return f->newLiteralTypeNode(
			f->newKeywordExpression(Kind::NullKeyword));
	}
	if ((t->flags & TypeFlagsNever) != 0) {
		ctx->approximateLength += 5;
		return f->newKeywordTypeNode(Kind::NeverKeyword);
	}
	if ((t->flags & TypeFlagsESSymbol) != 0) {
		ctx->approximateLength += 6;
		return f->newKeywordTypeNode(Kind::SymbolKeyword);
	}
	if ((t->flags & TypeFlagsNonPrimitive) != 0) {
		ctx->approximateLength += 6;
		return f->newKeywordTypeNode(Kind::ObjectKeyword);
	}
	if (isThisTypeParameter(t)) {
		if ((ctx->flags & nodebuilder::FlagsInObjectTypeLiteral) != 0) {
			if (!ctx->encounteredError &&
			    (ctx->flags &
			     nodebuilder::FlagsAllowThisInObjectLiteral) == 0) {
				ctx->encounteredError = true;
			}
			ctx->tracker->ReportInaccessibleThisError();
		}
		ctx->approximateLength += 4;
		return f->newThisTypeNode();
	}

	bool expandedAlias = false;
	if (inTypeAlias == 0 && t->alias != nullptr &&
	    ((ctx->flags &
	      nodebuilder::FlagsUseAliasDefinedOutsideCurrentScope) != 0 ||
	     ch->IsTypeSymbolAccessible(t->alias->symbol,
	                                ctx->enclosingDeclaration))) {
		// If we should expand this type alias, skip the alias and fall
		// through to expand the underlying type
		if (!shouldExpandType(t, true /*isAlias*/)) {
			Symbol* sym = t->alias->symbol;
			NodeList* typeArgumentNodes =
			    mapToTypeNodes(t->alias->TypeArguments(),
			                   false /*isBareList*/);
			if (isReservedMemberName(sym->name) &&
			    (sym->flags & SymbolFlagsClass) == 0) {
				return f->newTypeReferenceNode(f->newIdentifier(""),
				                             typeArgumentNodes);
			}
			if (typeArgumentNodes != nullptr &&
			    typeArgumentNodes->nodes.size() == 1 &&
			    sym == ch->globalArrayType->symbol) {
				return f->newArrayTypeNode(
					typeArgumentNodes->nodes[0]);
			}
			return symbolToTypeNode(sym, SymbolFlagsType,
			                        typeArgumentNodes);
		}
		// Expanding: increment depth and process the underlying type
		ctx->depth++;
		expandedAlias = true;
	}
	auto popAliasDepth = scopeExit([this, expandedAlias]() {
		if (expandedAlias) {
			ctx->depth--;
		}
	});

	ObjectFlags objectFlags = t->objectFlags;

	if ((objectFlags & ObjectFlagsReference) != 0) {
		// When expanding, expand type references to their structural form
		if (shouldExpandType(t, false /*isAlias*/)) {
			ctx->depth++;
			Node* result = createAnonymousTypeNodeEx(
				t, true /*forceClassExpansion*/,
				true /*forceExpansion*/);
			ctx->depth--;
			return result;
		}
		if (ch->isArrayOrTupleType(t)) {
			return visitAndTransformType(
				t, &NodeBuilderImpl::arrayOrTupleTypeToNode);
		} else if (t->AsTypeReference()->node != nullptr) {
			return visitAndTransformType(
				t, &NodeBuilderImpl::typeReferenceToTypeNode);
		} else {
			return typeReferenceToTypeNode(t);
		}
	}
	if ((t->flags & TypeFlagsTypeParameter) != 0 ||
	    (objectFlags & ObjectFlagsClassOrInterface) != 0) {
		// When expanding class or interface types, show their structural
		// form
		if ((objectFlags & ObjectFlagsClassOrInterface) != 0 &&
		    shouldExpandType(t, false /*isAlias*/)) {
			ctx->depth++;
			Node* result = createAnonymousTypeNodeEx(
				t, true /*forceClassExpansion*/,
				true /*forceExpansion*/);
			ctx->depth--;
			return result;
		}
		if ((t->flags & TypeFlagsTypeParameter) != 0 &&
		    std::find(ctx->inferTypeParameters.begin(),
		              ctx->inferTypeParameters.end(),
		              t) != ctx->inferTypeParameters.end()) {
			ctx->approximateLength +=
			    (int)symbolName(t->symbol).size() + 6;
			Node* constraintNode = nullptr;
			Type* constraint = ch->getConstraintOfTypeParameter(t);
			if (constraint != nullptr) {
				// If the infer type has a constraint that is not the same
				// as the constraint we would have normally inferred based
				// on b, we emit the constraint using `infer T extends ?`.
				// We omit inferred constraints from type references as
				// they may be elided.
				Type* inferredConstraint =
				    ch->getInferredTypeParameterConstraint(
					    t, true /*omitTypeReferences*/);
				if (!(inferredConstraint != nullptr &&
				      ch->isTypeIdenticalTo(constraint,
				                            inferredConstraint))) {
					ctx->approximateLength += 9;
					constraintNode = typeToTypeNode(constraint);
				}
			}
			return f->newInferTypeNode(
				typeParameterToDeclarationWithConstraint(
					t, constraintNode));
		}
		if ((ctx->flags &
		     nodebuilder::FlagsGenerateNamesForShadowedTypeParams) != 0 &&
		    (t->flags & TypeFlagsTypeParameter) != 0) {
			Node* name = typeParameterToName(t);
			ctx->approximateLength += (int)name->text().size();
			return f->newTypeReferenceNode(
				newIdentifier(name->text(), t->symbol),
				nullptr /*typeArguments*/);
		}
		// Ignore constraint/default when creating a usage (as opposed to
		// declaration) of a type parameter.
		if (t->symbol != nullptr) {
			return symbolToTypeNode(t->symbol, SymbolFlagsType, nullptr);
		}
		std::string name;
		if ((t == ch->markerSuperTypeForCheck ||
		     t == ch->markerSubTypeForCheck) &&
		    ch->varianceTypeParameter != nullptr &&
		    ch->varianceTypeParameter->symbol != nullptr) {
			name = (t == ch->markerSubTypeForCheck ? "sub-" : "super-") +
			       symbolName(ch->varianceTypeParameter->symbol);
		} else {
			name = "?";
		}
		return f->newTypeReferenceNode(
			newIdentifier(name, nullptr /*symbol*/),
			nullptr /*typeArguments*/);
	}
	if ((t->flags & TypeFlagsUnion) != 0 &&
	    t->AsUnionType()->origin != nullptr) {
		t = t->AsUnionType()->origin;
	}
	if ((t->flags & (TypeFlagsUnion | TypeFlagsIntersection)) != 0) {
		std::vector<Type*> types;
		if ((t->flags & TypeFlagsUnion) != 0) {
			types = ch->formatUnionTypes(t->AsUnionType()->types,
			                         expandingEnum);
		} else {
			types = t->AsIntersectionType()->types;
		}
		if (types.size() == 1) {
			return typeToTypeNode(types[0]);
		}
		NodeList* typeNodes = mapToTypeNodes(types, true /*isBareList*/);
		if (typeNodes != nullptr && !typeNodes->nodes.empty()) {
			if ((t->flags & TypeFlagsUnion) != 0) {
				return f->newUnionTypeNode(typeNodes);
			} else {
				return f->newIntersectionTypeNode(typeNodes);
			}
		} else {
			if (!ctx->encounteredError &&
			    (ctx->flags &
			     nodebuilder::FlagsAllowEmptyUnionOrIntersection) == 0) {
				ctx->encounteredError = true;
			}
			return nullptr;
			// TODO: GH#18217
		}
	}
	if ((objectFlags & (ObjectFlagsAnonymous | ObjectFlagsMapped)) != 0) {
		// The type is an object literal type.
		return createAnonymousTypeNode(t);
	}
	if ((t->flags & TypeFlagsIndex) != 0) {
		Type* indexedType = t->Target();
		ctx->approximateLength += 6;
		Node* indexTypeNode = typeToTypeNode(indexedType);
		return f->newTypeOperatorNode(Kind::KeyOfKeyword, indexTypeNode);
	}
	if ((t->flags & TypeFlagsTemplateLiteral) != 0) {
		std::vector<std::string>& texts =
		    t->AsTemplateLiteralType()->texts;
		std::vector<Type*>& types = t->AsTemplateLiteralType()->types;
		Node* templateHead =
		    f->newTemplateHead(texts[0], "", TokenFlagsNone);
		e->addEmitFlags(templateHead, printer::EFNoAsciiEscaping);
		std::vector<Node*> spans;
		spans.reserve(types.size());
		for (size_t i = 0; i < types.size(); i++) {
			Node* res;
			if (i < types.size() - 1) {
				res = f->newTemplateMiddle(texts[i + 1], "",
				                           TokenFlagsNone);
			} else {
				res = f->newTemplateTail(texts[i + 1], "",
				                         TokenFlagsNone);
			}
			e->addEmitFlags(res, printer::EFNoAsciiEscaping);
			spans.push_back(f->newTemplateLiteralTypeSpan(
				typeToTypeNode(types[i]), res));
		}
		NodeList* templateSpans = f->newNodeList(spans);
		ctx->approximateLength += 2;
		return f->newTemplateLiteralTypeNode(templateHead,
		                                     templateSpans);
	}
	if ((t->flags & TypeFlagsStringMapping) != 0) {
		Node* typeNode = typeToTypeNode(t->Target());
		return symbolToTypeNode(t->symbol, SymbolFlagsType,
		                        f->newNodeList({typeNode}));
	}
	if ((t->flags & TypeFlagsIndexedAccess) != 0) {
		Node* objectTypeNode =
		    typeToTypeNode(t->AsIndexedAccessType()->objectType);
		Node* indexTypeNode =
		    typeToTypeNode(t->AsIndexedAccessType()->indexType);
		ctx->approximateLength += 2;
		return f->newIndexedAccessTypeNode(objectTypeNode,
		                                   indexTypeNode);
	}
	if ((t->flags & TypeFlagsConditional) != 0) {
		return visitAndTransformType(
			t, &NodeBuilderImpl::conditionalTypeToTypeNode);
	}
	if ((t->flags & TypeFlagsSubstitution) != 0) {
		Node* typeNode =
		    typeToTypeNode(t->AsSubstitutionType()->baseType);
		if (!ch->isNoInferType(t)) {
			return typeNode;
		}
		Symbol* noInferSymbol =
		    ch->getGlobalTypeAliasSymbol("NoInfer", 1, false);
		if (noInferSymbol != nullptr) {
			return symbolToTypeNode(noInferSymbol, SymbolFlagsType,
			                        f->newNodeList({typeNode}));
		} else {
			return typeNode;
		}
	}

	TSC_UNREACHABLE("Should be unreachable.");
}

// newStringLiteral (nodebuilderimpl.go:3655).
Node* NodeBuilderImpl::newStringLiteral(const std::string& text) {
	return newStringLiteralEx(text, false /*isSingleQuote*/);
}

// newStringLiteralEx (nodebuilderimpl.go:3659).
Node* NodeBuilderImpl::newStringLiteralEx(const std::string& text,
                                          bool isSingleQuote) {
	TokenFlags flags = TokenFlagsNone;
	if (isSingleQuote ||
	    (ctx->flags &
	     nodebuilder::FlagsUseSingleQuotesForStringLiteralType) != 0) {
		flags = (TokenFlags)(flags | TokenFlagsSingleQuote);
	}
	Node* node = f->newStringLiteral(text, flags);
	return node;
}

// TypeAlias::toTypeReferenceNode (nodebuilderimpl.go:3670).
Node* TypeAlias::toTypeReferenceNode(NodeBuilderImpl* b) {
	return b->f->newTypeReferenceNode(
		b->symbolToEntityNameNode(symbol),
		b->mapToTypeNodes(typeArguments, false /*isBareList*/));
}

// newIdentifier (nodebuilderimpl.go:3674).
Node* NodeBuilderImpl::newIdentifier(const std::string& text,
                                     Symbol* symbol) {
	Node* id = f->newIdentifier(text);
	if (symbol != nullptr) {
		recordIdSymbol(id, symbol);
	}
	return id;
}

// createAccessExpression (nodebuilderimpl.go:3682).
Node* NodeBuilderImpl::createAccessExpression(Node* node) {
	if (isQualifiedName(node)) {
		return f->newPropertyAccessExpression(
			createAccessExpression(
				node->as<QualifiedName>()->Left),
			nullptr /*questionDotToken*/,
			deepCloneNode(*f, node->as<QualifiedName>()->Right),
			NodeFlagsNone);
	}
	if (isIdentifier(node) || isPropertyAccessExpression(node) ||
	    isExpressionWithTypeArguments(node)) {
		return deepCloneNode(*f, node);
	}
	TSC_UNREACHABLE(
		("unexpected access node kind: " + std::to_string((int)node->kind))
		    .c_str());
}

// createExpressionWithTypeArguments (nodebuilderimpl.go:3692).
Node* NodeBuilderImpl::createExpressionWithTypeArguments(
	Node* expr, NodeList* typeArguments) {
	if (typeArguments == nullptr || typeArguments->nodes.empty()) {
		return expr;
	}
	return f->newExpressionWithTypeArguments(expr, typeArguments);
}

// lookupInstantiatedTypeArgumentNodes (nodebuilderimpl.go:3699).
NodeList* NodeBuilderImpl::lookupInstantiatedTypeArgumentNodes(
	std::vector<Symbol*> chain, int index) {
	if (shouldWriteTypeParametersInQualifiedName(chain, index)) {
		Symbol* symbol = chain[index];
		Symbol* nextSymbol = chain[index + 1];
		if ((nextSymbol->checkFlags & CheckFlagsInstantiated) == 0) {
			return nullptr;
		}

		Symbol* targetSymbol = symbol;
		if ((symbol->flags & SymbolFlagsAlias) != 0 &&
		    !ch->canGetTypeParametersOfClassOrInterface(symbol)) {
			targetSymbol = ch->resolveAlias(symbol);
		}

		if (!ch->canGetTypeParametersOfClassOrInterface(targetSymbol)) {
			return nullptr;
		}

		std::vector<Type*> params =
		    getTypeParametersOfClassOrInterface(targetSymbol);
		TypeMapper* targetMapper =
		    ch->valueSymbolLinks.Get(nextSymbol)->mapper;
		if (targetMapper != nullptr) {
			std::vector<Type*> mapped;
			mapped.reserve(params.size());
			for (Type* p : params) {
				mapped.push_back(targetMapper->map(p));
			}
			params = std::move(mapped);
		}
		return mapToTypeNodes(params, false /*isBareList*/);
	}
	return nullptr;
}

// lookupExpressionChainTypeArgumentNodes (nodebuilderimpl.go:3726).
NodeList* NodeBuilderImpl::lookupExpressionChainTypeArgumentNodes(
	std::vector<Symbol*> chain, int index) {
	if (shouldWriteTypeParametersInQualifiedName(chain, index)) {
		Symbol* symbol = chain[index];
		SymbolId symbolId = getSymbolId(symbol);
		if (ctx->typeParameterSymbolList.Has(symbolId)) {
			return nullptr;
		}

		ctx->typeParameterSymbolList.Add(symbolId);
		if (NodeList* typeArgumentNodes =
			    lookupInstantiatedTypeArgumentNodes(chain, index);
		    typeArgumentNodes != nullptr) {
			return typeArgumentNodes;
		}
		std::vector<Node*> typeParameterNodes =
		    typeParametersToTypeParameterDeclarations(symbol);
		if (!typeParameterNodes.empty()) {
			return f->newNodeList(typeParameterNodes);
		}
	}
	return nullptr;
}

// shouldWriteTypeParametersInQualifiedName (nodebuilderimpl.go:3744).
bool NodeBuilderImpl::shouldWriteTypeParametersInQualifiedName(
	std::vector<Symbol*> chain, int index) {
	return (ctx->flags &
	        nodebuilder::FlagsWriteTypeParametersInQualifiedName) != 0 &&
	       index < (int)chain.size() - 1;
}

// ===========================================================================
// nodebuilderscopes.go
// ===========================================================================

// cloneNodeBuilderContext (nodebuilderscopes.go:9).
static std::function<void()> cloneNodeBuilderContext(NodeBuilderContext* context) {
	// Make type parameters created within this context not consume the name
	// outside this context
	// The symbol serializer ends up creating many sibling scopes that all
	// need "separate" contexts when it comes to naming things - within a
	// normal `typeToTypeNode` call, the node builder only ever descends
	// through the type tree, so the only cases where we could have used
	// distinct sibling scopes was when there were multiple generic overloads
	// with similar generated type parameter names
	// The effect:
	// When we write out
	// export const x: <T>(x: T) => T
	// export const y: <T>(x: T) => T
	// we write it out like that, rather than as
	// export const x: <T>(x: T) => T
	// export const y: <T_1>(x: T_1) => T_1
	auto restoreNames = context->typeParameterNames.EnterScope();
	auto restoreNamesByText = context->typeParameterNamesByText.EnterScope();
	auto restoreNamesByTextNextNameCount =
	    context->typeParameterNamesByTextNextNameCount.EnterScope();
	auto restoreSymbolList = context->typeParameterSymbolList.EnterScope();
	return [restoreNames = std::move(restoreNames),
	        restoreNamesByText = std::move(restoreNamesByText),
	        restoreNamesByTextNextNameCount =
	            std::move(restoreNamesByTextNextNameCount),
	        restoreSymbolList = std::move(restoreSymbolList)]() {
		restoreNames();
		restoreNamesByText();
		restoreNamesByTextNextNameCount();
		restoreSymbolList();
	};
}

// addSymbolTypeToContext (nodebuilderscopes.go:33).
std::function<void()> NodeBuilderImpl::addSymbolTypeToContext(Symbol* symbol,
                                                              Type* t) {
	SymbolId id = getSymbolId(symbol);
	auto it = ctx->enclosingSymbolTypes.find(id);
	bool oldTypeExists = it != ctx->enclosingSymbolTypes.end();
	Type* oldType = oldTypeExists ? it->second : nullptr;
	ctx->enclosingSymbolTypes[id] = t;
	return [this, id, oldType, oldTypeExists]() {
		if (oldTypeExists) {
			ctx->enclosingSymbolTypes[id] = oldType;
		} else {
			ctx->enclosingSymbolTypes.erase(id);
		}
	};
}

// enterSignatureScope (nodebuilderscopes.go:44).
std::pair<std::vector<Symbol*>, std::function<void()>>
NodeBuilderImpl::enterSignatureScope(Signature* signature) {
	std::vector<Symbol*> expandedParams =
	    ch->getExpandedParameters(signature, true /*skipUnionExpanding*/)[0];
	auto cleanup = enterNewScope(signature->declaration, expandedParams,
	                             signature->typeParameters,
	                             signature->parameters, signature->mapper);
	return {expandedParams, cleanup};
}

// enterNewScope (nodebuilderscopes.go:50).
std::function<void()> NodeBuilderImpl::enterNewScope(
	Node* declaration, std::vector<Symbol*> expandedParams,
	std::vector<Type*> typeParameters,
	std::vector<Symbol*> originalParameters, TypeMapper* mapper) {
	auto cleanupContext = cloneNodeBuilderContext(ctx);
	// For regular function/method declarations, the enclosing declaration
	// will already be signature.declaration, so this is a no-op, but for
	// arrow functions and function expressions, the enclosing declaration
	// will be the declaration that the arrow function / function expression
	// is assigned to.
	//
	// If the parameters or return type include "typeof globalThis.paramName",
	// using the wrong scope will lead us to believe that we can emit "typeof
	// paramName" instead, even though that would refer to the parameter, not
	// the global. Make sure we are in the right scope by changing the
	// enclosingDeclaration to the function.
	//
	// We can't use the declaration directly; it may be in another file and
	// so we may lose access to symbols accessible to the current enclosing
	// declaration, or gain access to symbols not accessible to the current
	// enclosing declaration. To keep this chain accurate, insert a fake
	// scope into the chain which makes the function's parameters visible.
	std::function<void()> cleanupParams;
	std::function<void()> cleanupTypeParams;
	Node* oldEnclosingDecl = ctx->enclosingDeclaration;
	TypeMapper* oldMapper = ctx->mapper;
	if (mapper != nullptr) {
		ctx->mapper = mapper;
	}
	if (ctx->enclosingDeclaration != nullptr && declaration != nullptr) {
		// As a performance optimization, reuse the same fake scope within
		// this chain. This is especially needed when we are working on an
		// excessively deep type; if we don't do this, then we spend all of
		// our time adding more and more scopes that need to be searched in
		// isSymbolAccessible later. Since all we really want to do is to mark
		// certain names as unavailable, we can just keep all of the names
		// we're introducing in one large table and push/pop from it as
		// needed; isSymbolAccessible will walk upward and find the closest
		// "fake" scope, which will conveniently report on any and all faked
		// scopes in the chain.
		//
		// It'd likely be better to store this somewhere else for
		// isSymbolAccessible, but since that API _only_ uses the enclosing
		// declaration (and its parents), this is seems like the best way to
		// inject names into that search process.
		//
		// Note that we only check the most immediate enclosingDeclaration;
		// the only place we could potentially add another fake scope into the
		// chain is right here, so we don't traverse all ancestors.
		auto pushFakeScope =
		    [this](const std::string& kind,
		           std::function<void(
		               std::function<void(const std::string&, Symbol*)>)>
		               addAll) -> std::function<void()> {
			// We only ever need to look two declarations upward.
			Node* existingFakeScope = nullptr;
			if (links.Has(ctx->enclosingDeclaration)) {
				NodeBuilderLinks* enclosingLinks =
				    links.Get(ctx->enclosingDeclaration);
				if (enclosingLinks->fakeScopeForSignatureDeclaration
				        .has_value() &&
				    *enclosingLinks->fakeScopeForSignatureDeclaration ==
				        kind) {
					existingFakeScope = ctx->enclosingDeclaration;
				}
			}
			if (existingFakeScope == nullptr &&
			    ctx->enclosingDeclaration->parent != nullptr) {
				if (links.Has(ctx->enclosingDeclaration->parent)) {
					NodeBuilderLinks* enclosingLinks =
					    links.Get(ctx->enclosingDeclaration->parent);
					if (enclosingLinks->fakeScopeForSignatureDeclaration
					        .has_value() &&
					    *enclosingLinks->fakeScopeForSignatureDeclaration ==
					        kind) {
						existingFakeScope =
						    ctx->enclosingDeclaration->parent;
					}
				}
			}

			SymbolTable stackTable;
			SymbolTable* locals = nullptr;
			if (existingFakeScope != nullptr) {
				locals = existingFakeScope->locals();
			}
			if (locals == nullptr) {
				locals = &stackTable;
			}
			std::vector<std::string> newLocals;
			struct localsRecord {
				std::string name;
				Symbol* oldSymbol;
			};
			std::vector<localsRecord> oldLocals;
			addAll([&](const std::string& name, Symbol* symbol) {
				// Add cleanup information only if we don't own the fake scope
				if (existingFakeScope != nullptr) {
					auto it = locals->find(name);
					if (it == locals->end() || it->second == nullptr) {
						newLocals.push_back(name);
					} else {
						oldLocals.push_back({name, it->second});
					}
				}
				(*locals)[name] = symbol;
			});

			if (existingFakeScope == nullptr) {
				// Use a Block for this; the type of the node doesn't matter
				// so long as it has locals, and this is cheaper/easier than
				// using a function-ish Node.
				Node* fakeScope =
				    f->newBlock(f->newNodeList({}), false);
				links.Get(fakeScope)->fakeScopeForSignatureDeclaration =
				    kind;
				auto data = fakeScope->localsContainerData();
				*data.locals = *locals;
				fakeScope->parent = ctx->enclosingDeclaration;
				ctx->enclosingDeclaration = fakeScope;
				return std::function<void()>{};
			} else {
				// We did not create the current scope, so we have to clean
				// it up
				SymbolTable* capturedLocals = locals;
				auto undo = [capturedLocals, newLocals = std::move(newLocals),
				             oldLocals = std::move(oldLocals)]() {
					for (auto& s : newLocals) {
						capturedLocals->erase(s);
					}
					for (auto& s : oldLocals) {
						(*capturedLocals)[s.name] = s.oldSymbol;
					}
				};
				return undo;
			}
		};

		bool anyExpandedParam = false;
		for (Symbol* p : expandedParams) {
			if (p != nullptr) {
				anyExpandedParam = true;
				break;
			}
		}
		if (!anyExpandedParam) {
			cleanupParams = nullptr;
		} else {
			cleanupParams = pushFakeScope(
				"params",
				[&](std::function<void(const std::string&, Symbol*)> add) {
					for (size_t pIndex = 0; pIndex < expandedParams.size();
					     pIndex++) {
						Symbol* param = expandedParams[pIndex];
						Symbol* originalParam = nullptr;
						if (pIndex < originalParameters.size()) {
							originalParam = originalParameters[pIndex];
						}
						if (!originalParameters.empty() &&
						    originalParam != param) {
							// Can't reference the expanded parameter name,
							// just the original, unless we've expanded the
							// param list for some reason
							if (originalParam != nullptr) {
								add(originalParam->name, originalParam);
							}
						} else {
							bool handled = false;
							for (Node* d : param->declarations) {
								std::function<void(BindingElement*)>
								    bindElement;
								std::function<void(BindingPattern*)>
								    bindPattern;
								auto bindPatternWorker =
								    [&](BindingPattern* p) {
									    for (Node* e : p->Elements->nodes) {
										    if (e->kind ==
										        Kind::OmittedExpression) {
											    return;
										    }
										    if (e->kind ==
										        Kind::BindingElement) {
											    bindElement(
												    e->as<BindingElement>());
											    return;
										    }
										    TSC_UNREACHABLE(
											    "Unhandled binding element "
											    "kind");
									    }
								    };
								auto bindElementWorker =
								    [&](BindingElement* e) {
									    if (e->name != nullptr &&
									        isBindingPattern(e->name)) {
										    bindPattern(
											    e->name->as<BindingPattern>());
										    return;
									    }
									    Symbol* symbol =
									        ch->getSymbolOfDeclaration(e);
									    if (symbol !=
									        nullptr) { // omitted expressions
											           // are now parsed as
											           // nameless binding
											           // patterns and also
											           // have no symbol
										    add(symbol->name, symbol);
									    }
								    };
								bindElement = bindElementWorker;
								bindPattern = bindPatternWorker;

								if (isParameterDeclaration(d) &&
								    d->name() != nullptr &&
								    isBindingPattern(d->name())) {
									bindPattern(
									    d->name()->as<BindingPattern>());
									handled = true;
								}
								if (handled) {
									break;
								}
							}
							if (!handled) {
								add(param->name, param);
							}
						}
					}
				});
		}

		bool anyTypeParam = false;
		for (Type* p : typeParameters) {
			if (p != nullptr) {
				anyTypeParam = true;
				break;
			}
		}
		if ((ctx->flags &
		     nodebuilder::FlagsGenerateNamesForShadowedTypeParams) != 0 &&
		    !typeParameters.empty() && anyTypeParam) {
			cleanupTypeParams = pushFakeScope(
				"typeParams",
				[&](std::function<void(const std::string&, Symbol*)> add) {
					for (Type* typeParam : typeParameters) {
						if (typeParam == nullptr) {
							continue;
						}
						std::string typeParamName =
						    typeParameterToName(typeParam)->text();
						add(typeParamName, typeParam->symbol);
					}
				});
		}
	}

	return [this, cleanupParams = std::move(cleanupParams),
	        cleanupTypeParams = std::move(cleanupTypeParams),
	        cleanupContext = std::move(cleanupContext),
	        oldEnclosingDecl, oldMapper]() {
		if (cleanupParams) {
			cleanupParams();
		}
		if (cleanupTypeParams) {
			cleanupTypeParams();
		}
		cleanupContext();
		ctx->enclosingDeclaration = oldEnclosingDecl;
		ctx->mapper = oldMapper;
	};
}

// ===========================================================================
// pseudotypenodebuilder.go
// ===========================================================================

// pseudoTypeToNodeWithCheckerFallback is like pseudoTypeToNode but when the
// top-level pseudo type is PseudoTypeInferred, it reports any error nodes and
// then serializes from the checker's type. This avoids incorrect type output
// when PseudoTypeInferred would derive the type from the original declaration
// expression in an instantiated context.
Node* NodeBuilderImpl::pseudoTypeToNodeWithCheckerFallback(
	pseudochecker::PseudoType* t, Type* checkerType) {
	if (t->kind == pseudochecker::PseudoTypeKind::Inferred) {
		if (!ctx->suppressReportInferenceFallback) {
			auto& errorNodes = t->AsPseudoTypeInferred()->errorNodes;
			if (!errorNodes.empty()) {
				for (Node* n : errorNodes) {
					ctx->tracker->ReportInferenceFallback(n);
				}
			} else {
				ctx->tracker->ReportInferenceFallback(
					t->AsPseudoTypeInferred()->expression);
			}
		}
		bool oldSuppress = ctx->suppressReportInferenceFallback;
		ctx->suppressReportInferenceFallback = true;
		Node* result = typeToTypeNode(checkerType);
		ctx->suppressReportInferenceFallback = oldSuppress;
		return result;
	} else if (t->kind == pseudochecker::PseudoTypeKind::Direct) {
		Node* existing = t->AsPseudoTypeDirect()->typeNode;
		if (!canReuseExistingJSTypeNode(existing, checkerType)) {
			if (!ctx->suppressReportInferenceFallback) {
				ctx->tracker->ReportInferenceFallback(existing);
			}
			bool oldSuppress = ctx->suppressReportInferenceFallback;
			ctx->suppressReportInferenceFallback = true;
			Node* result = typeToTypeNode(checkerType);
			ctx->suppressReportInferenceFallback = oldSuppress;
			return result;
		}
	}
	return pseudoTypeToNode(t);
}

// pseudoTypeToNode — maps a pseudochecker's pseudotypes into ast nodes and
// reports any inference fallback errors the pseudotype structure implies.
Node* NodeBuilderImpl::pseudoTypeToNode(pseudochecker::PseudoType* t) {
	TSC_ASSERT(t != nullptr, "Attempted to serialize nil pseudotype");
	switch (t->kind) {
	case pseudochecker::PseudoTypeKind::Direct:
		return reuseTypeNode(t->AsPseudoTypeDirect()->typeNode);
	case pseudochecker::PseudoTypeKind::Inferred: {
		auto* inferred = t->AsPseudoTypeInferred();
		Node* node = inferred->expression;
		if (!inferred->errorNodes.empty()) {
			for (Node* n : inferred->errorNodes) {
				ctx->tracker->ReportInferenceFallback(n);
			}
		} else if (isEntityNameExpression(node) &&
		           isDeclaration(node->parent)) {
			ctx->tracker->ReportInferenceFallback(node->parent);
		} else {
			ctx->tracker->ReportInferenceFallback(node);
		}
		if (inferred->isSignatureReturn) {
			return serializeReturnTypeForSignature(
				ch->getSignatureFromDeclaration(node), false);
		}
		// use symbol type from parent declaration to automatically handle
		// expression type widening without duplicating logic
		if (isReturnStatement(node->parent)) {
			Node* enclosing = getContainingFunction(node);
			if (isAccessor(enclosing)) {
				return serializeTypeForDeclaration(enclosing, nullptr,
				                                   nullptr, false);
			}
			return serializeReturnTypeForSignature(
				ch->getSignatureFromDeclaration(enclosing), false);
		}
		if (isArrowFunction(node->parent) &&
		    node->parent->as<ArrowFunction>()->Body == node) {
			return serializeReturnTypeForSignature(
				ch->getSignatureFromDeclaration(node->parent), false);
		}
		if (isDeclaration(node->parent)) {
			return serializeTypeForDeclaration(node->parent, nullptr,
			                                   nullptr, false);
		}
		// This might be effectively unreachable. If it's not, it may need
		// more widening rules to mirror checker behavior for whatever
		// expressions are serialized here
		Type* ty = ch->getTypeOfExpression(node);
		return typeToTypeNode(ty);
	}
	case pseudochecker::PseudoTypeKind::NoResult: {
		Node* node = t->AsPseudoTypeNoResult()->declaration;
		ctx->tracker->ReportInferenceFallback(node);
		if (isFunctionLike(node) && !isAccessor(node)) {
			return serializeReturnTypeForSignature(
				ch->getSignatureFromDeclaration(node), false);
		}
		return serializeTypeForDeclaration(node, nullptr, nullptr, false);
	}
	case pseudochecker::PseudoTypeKind::MaybeConstLocation: {
		auto* d = t->AsPseudoTypeMaybeConstLocation();
		// see checkExpressionWithContextualType for general literal widening
		// rules which need to be emulated here, plus
		// checkTemplateLiteralExpression for template literal widening rules
		// if the pseudochecker ever supports literalized templates
		bool isInConstContext = ch->isConstContext(d->node);
		if (!isInConstContext && pseudochecker::isInConstContext(d->node)) {
			// Only consult the contextual type if the pseudochecker's
			// syntactic check also puts us in a const context.
			// getContextualType returns post-inference results at
			// node-printing time which may not have existed during initial
			// checking (e.g. when the contextual type depends on inference),
			// causing incorrect literal type preservation.
			Type* contextualType =
			    ch->getContextualType(d->node, ContextFlagsNone);
			Type* t2 = pseudoTypeToType(d->constType);
			if (t2 != nullptr &&
			    ch->isLiteralOfContextualType(
					t2, ch->instantiateContextualType(contextualType,
					                                d->node,
					                                ContextFlagsNone))) {
				isInConstContext = true;
			}
		}
		if (isInConstContext) {
			return pseudoTypeToNode(d->constType);
		} else {
			return pseudoTypeToNode(d->regularType);
		}
	}
	case pseudochecker::PseudoTypeKind::Union: {
		std::vector<Node*> res;
		bool hasElidedType = false;
		bool hasUndefined = false;
		auto& members = t->AsPseudoTypeUnion()->types;
		std::function<void(Node*)> appendTypeNode =
		    [&](Node* node) {
			    if (isUnionTypeNode(node)) {
				    for (Node* n : node->as<UnionTypeNode>()->Types->nodes) {
					    appendTypeNode(n);
				    }
				    return;
			    }
			    if (node->kind == Kind::UndefinedKeyword) {
				    if (hasUndefined) {
					    return;
				    }
				    hasUndefined = true;
			    }
			    res.push_back(node);
		    };
		for (pseudochecker::PseudoType* m : members) {
			if (!ch->strictNullChecks) {
				if (m->kind == pseudochecker::PseudoTypeKind::Undefined ||
				    m->kind == pseudochecker::PseudoTypeKind::Null) {
					hasElidedType = true;
					continue;
				}
			}
			appendTypeNode(pseudoTypeToNode(m));
		}
		if (res.size() == 1) {
			return res[0];
		}
		if (res.empty()) {
			if (hasElidedType) {
				return f->newKeywordTypeNode(Kind::AnyKeyword);
			}
			return f->newKeywordTypeNode(Kind::NeverKeyword);
		}
		return f->newUnionTypeNode(f->newNodeList(res));
	}
	case pseudochecker::PseudoTypeKind::Undefined:
		if (!ch->strictNullChecks) {
			return f->newKeywordTypeNode(Kind::AnyKeyword);
		}
		return f->newKeywordTypeNode(Kind::UndefinedKeyword);
	case pseudochecker::PseudoTypeKind::Null:
		if (!ch->strictNullChecks) {
			return f->newKeywordTypeNode(Kind::AnyKeyword);
		}
		return f->newLiteralTypeNode(f->newKeywordExpression(Kind::NullKeyword));
	case pseudochecker::PseudoTypeKind::Any:
		return f->newKeywordTypeNode(Kind::AnyKeyword);
	case pseudochecker::PseudoTypeKind::String:
		return f->newKeywordTypeNode(Kind::StringKeyword);
	case pseudochecker::PseudoTypeKind::Number:
		return f->newKeywordTypeNode(Kind::NumberKeyword);
	case pseudochecker::PseudoTypeKind::BigInt:
		return f->newKeywordTypeNode(Kind::BigIntKeyword);
	case pseudochecker::PseudoTypeKind::Boolean:
		return f->newKeywordTypeNode(Kind::BooleanKeyword);
	case pseudochecker::PseudoTypeKind::False:
		return f->newLiteralTypeNode(f->newKeywordExpression(Kind::FalseKeyword));
	case pseudochecker::PseudoTypeKind::True:
		return f->newLiteralTypeNode(f->newKeywordExpression(Kind::TrueKeyword));
	case pseudochecker::PseudoTypeKind::SingleCallSignature: {
		auto* d = t->AsPseudoTypeSingleCallSignature();
		Signature* signature =
		    ch->getSignatureFromDeclaration(d->signature);
		std::vector<Symbol*> expandedParams =
		    ch->getExpandedParameters(signature,
		                            true /*skipUnionExpanding*/)[0];
		auto cleanup = enterNewScope(d->signature, expandedParams,
		                             signature->typeParameters,
		                             signature->parameters, signature->mapper);
		auto guard = scopeExit(std::move(cleanup));
		NodeList* typeParams = nullptr;
		if (!d->typeParameters.empty()) {
			std::vector<Node*> res;
			res.reserve(d->typeParameters.size());
			for (Node* tp : d->typeParameters) {
				res.push_back(reuseNode(tp));
			}
			typeParams = f->newNodeList(res);
		}
		NodeList* params = pseudoParametersToNodeList(d->parameters);
		Node* returnType = pseudoTypeToNode(d->returnType);
		return f->newFunctionTypeNode(typeParams, params, returnType);
	}
	case pseudochecker::PseudoTypeKind::Tuple: {
		std::vector<Node*> res;
		auto& elements = t->AsPseudoTypeTuple()->elements;
		for (pseudochecker::PseudoType* el : elements) {
			res.push_back(pseudoTypeToNode(el));
		}
		// pseudo-tuples are implicitly `readonly` since they originate from
		// `as const` contexts but strada *sometimes* fails to add the
		// `readonly` modifier to the generated node.
		Node* result = f->newTupleTypeNode(f->newNodeList(res));
		e->addEmitFlags(result, printer::EFSingleLine);
		return f->newTypeOperatorNode(Kind::ReadonlyKeyword, result);
	}
	case pseudochecker::PseudoTypeKind::ObjectLiteral: {
		auto& elements = t->AsPseudoTypeObjectLiteral()->elements;
		if (elements.empty()) {
			Node* result = f->newTypeLiteralNode(f->newNodeList({}));
			e->addEmitFlags(result, printer::EFSingleLine);
			return result;
		}
		// NOTE: using the checker's `isConstContext` instead of the
		// pseudochecker's `isInConstContext` results in different results
		// here. The checker one is more "correct" but means we'll mark
		// objects in parameter positions contextually typed by const type
		// parameters as readonly - something a true syntactic ID emitter
		// couldn't possibly know (since the signature could be from across
		// files). This can't *really* happen in any cases ID doesn't already
		// error on, though. Just something to keep in mind if the ID checker
		// keeps growing.
		bool isConst = ch->isConstContext(elements[0]->name->parent->parent);
		std::vector<Node*> newElements;
		newElements.reserve(elements.size());

		// Member types are serialized within an object type literal, so set
		// the corresponding flag to mirror createTypeNodeFromObjectType.
		// This ensures inaccessible `this` references inside the members are
		// reported (TS2527).
		auto restoreObjectLiteralFlags = saveRestoreFlags();
		ctx->flags |= nodebuilder::FlagsInObjectTypeLiteral;

		for (pseudochecker::PseudoObjectElement* el : elements) {
			ModifierList* modifiers = nullptr;
			if (isConst ||
			    (el->kind ==
			         pseudochecker::PseudoObjectElementKind::
			             PropertyAssignment &&
			     el->propertyAssignment->readonly)) {
				modifiers = f->newModifierList(
					{f->newToken(Kind::ReadonlyKeyword)});
			}
			std::function<void()> cleanup;
			if (el->kind !=
			    pseudochecker::PseudoObjectElementKind::PropertyAssignment) {
				Signature* signature =
				    ch->getSignatureFromDeclaration(el->Signature());
				std::vector<Symbol*> expandedParams =
				    ch->getExpandedParameters(
						signature, true /*skipUnionExpanding*/)[0];
				cleanup = enterNewScope(el->Signature(), expandedParams,
				                        signature->typeParameters,
				                        signature->parameters,
				                        signature->mapper);
			}
			Node* newProp = nullptr;
			switch (el->kind) {
			case pseudochecker::PseudoObjectElementKind::Method: {
				auto* d = el->method;
				NodeList* typeParams = nullptr;
				if (!d->typeParameters.empty()) {
					std::vector<Node*> res;
					res.reserve(d->typeParameters.size());
					for (Node* tp : d->typeParameters) {
						res.push_back(reuseNode(tp));
					}
					typeParams = f->newNodeList(res);
				}
				if (isConst) {
					newProp = f->newPropertySignatureDeclaration(
						modifiers,
						reuseName(el->name, false /*isMethod*/), nullptr,
						f->newFunctionTypeNode(
							typeParams,
							pseudoParametersToNodeList(d->parameters),
							pseudoTypeToNode(d->returnType)),
						nullptr);
					break;
				}
				newProp = f->newMethodSignatureDeclaration(
					modifiers, reuseName(el->name, true /*isMethod*/),
					nullptr, typeParams,
					pseudoParametersToNodeList(d->parameters),
					pseudoTypeToNode(d->returnType));
				break;
			}
			case pseudochecker::PseudoObjectElementKind::PropertyAssignment: {
				auto* d = el->propertyAssignment;
				newProp = f->newPropertySignatureDeclaration(
					modifiers, reuseName(el->name, false /*isMethod*/),
					nullptr, pseudoTypeToNode(d->type), nullptr);
				break;
			}
			case pseudochecker::PseudoObjectElementKind::SetAccessor: {
				auto* d = el->setAccessor;
				newProp = f->newSetAccessorDeclaration(
					nullptr, reuseName(el->name, false /*isMethod*/),
					nullptr,
					f->newNodeList({pseudoParameterToNode(d->parameter)}),
					nullptr, nullptr, nullptr);
				break;
			}
			case pseudochecker::PseudoObjectElementKind::GetAccessor: {
				auto* d = el->getAccessor;
				newProp = f->newGetAccessorDeclaration(
					nullptr, reuseName(el->name, false /*isMethod*/),
					nullptr, nullptr, pseudoTypeToNode(d->type), nullptr,
					nullptr);
				break;
			}
			default:
				break;
			}
			if (ctx->enclosingFile == getSourceFileOfNode(el->name)) {
				e->setCommentRange(newProp, el->name->parent->loc);
			}
			newElements.push_back(newProp);
			if (cleanup) {
				cleanup();
			}
		}
		restoreObjectLiteralFlags();
		Node* result = f->newTypeLiteralNode(f->newNodeList(newElements));
		if ((ctx->flags & nodebuilder::FlagsMultilineObjectLiterals) == 0) {
			e->addEmitFlags(result, printer::EFSingleLine);
		}
		return result;
	}
	case pseudochecker::PseudoTypeKind::StringLiteral:
	case pseudochecker::PseudoTypeKind::NumericLiteral:
	case pseudochecker::PseudoTypeKind::BigIntLiteral: {
		Node* source = t->AsPseudoTypeLiteral()->node;
		return f->newLiteralTypeNode(reuseNode(source));
	}
	default:
		TSC_UNREACHABLE(("Unhandled pseudotype kind in pseudotype node "
		                 "construction: " +
		                std::to_string((int)t->kind))
		                   .c_str());
		return nullptr;
	}
}

// pseudoParametersToNodeList (pseudotypenodebuilder.go:327).
NodeList* NodeBuilderImpl::pseudoParametersToNodeList(
	std::vector<pseudochecker::PseudoParameter*> params) {
	std::vector<Node*> res;
	res.reserve(params.size());
	for (pseudochecker::PseudoParameter* p : params) {
		res.push_back(pseudoParameterToNode(p));
	}
	return f->newNodeList(res);
}

// pseudoParameterToNode (pseudotypenodebuilder.go:335).
Node* NodeBuilderImpl::pseudoParameterToNode(
	pseudochecker::PseudoParameter* p) {
	Node* dotDotDot = nullptr;
	Node* questionMark = nullptr;
	if (p->rest) {
		dotDotDot = f->newToken(Kind::DotDotDotToken);
	}
	if (p->optional) {
		questionMark = f->newToken(Kind::QuestionToken);
	}
	Node* parameter = f->newParameterDeclaration(
		nullptr, dotDotDot,
		// matches strada behavior of always reserializing param names from
		// scratch
		parameterToParameterDeclarationName(p->name->parent->symbol(),
		                                    p->name->parent),
		questionMark, pseudoTypeToNode(p->type), nullptr);
	if (Node* original = p->name->parent;
	    isParameterDeclaration(original)) {
		setCommentRange(parameter, original);
	}
	return parameter;
}

// pseudoTypeEquivalentToType — see `typeNodeIsEquivalentToType` in strada,
// but applied more broadly here, so is setup to handle more equivalences -
// strada only used it via the `canReuseTypeNodeAnnotation` host hook and not
// the `canReuseTypeNode` hook, which meant locations using the later were
// reliant on over-invalidation by the ID inference engine to not emit
// incorrect types.
bool NodeBuilderImpl::pseudoTypeEquivalentToType(
	pseudochecker::PseudoType* t, Type* type_, bool isOptionalAnnotated,
	bool reportErrors) {
	// if type_ resolves to an error, we charitably assume equality, since we
	// might be in a single-file checking mode
	if (type_ != nullptr && ch->isErrorType(type_)) {
		return true;
	}
	// If we can easily operate on just types, we should
	Type* typeFromPseudo = pseudoTypeToType(
		t); // note: cannot convert complex types like objects, which must be
		    // validated separately
	if (typeFromPseudo == type_) {
		return true;
	}
	Type* undefinedStripped = type_;
	if (isOptionalAnnotated) {
		undefinedStripped = ch->getTypeWithFacts(type_, TypeFactsNEUndefined);
	}
	if (typeFromPseudo != nullptr && type_ != nullptr) {
		if (isOptionalAnnotated) {
			if (undefinedStripped == typeFromPseudo) {
				return true;
			}
			if ((typeFromPseudo->flags & TypeFlagsUnion) != 0 &&
			    (undefinedStripped->flags & TypeFlagsUnion) != 0) {
				// does union comparison in general, since the unions may not
				// be `==` identical due to aliasing and the like
				if (ch->compareTypesIdentical(typeFromPseudo,
				                            undefinedStripped) ==
				    Ternary::True) {
					return true;
				}
			}
		}
		// handles freshness mismatches (e.g., fresh true vs regular true in
		// as const)
		if (ch->getRegularTypeOfLiteralType(typeFromPseudo) ==
		    ch->getRegularTypeOfLiteralType(type_)) {
			return true;
		}
		if ((typeFromPseudo->flags & TypeFlagsUnion) != 0 &&
		    (type_->flags & TypeFlagsUnion) != 0) {
			// handles union comparison in general, since unions may not be
			// `==` identical due to aliasing
			if (ch->compareTypesIdentical(typeFromPseudo, type_) ==
			    Ternary::True) {
				return true;
			}
		}
	}
	// otherwise, fallback to actual pseudo/type cross-comparisons
	switch (t->kind) {
	case pseudochecker::PseudoTypeKind::Inferred: {
		// PseudoTypeInferred with error nodes identifies specific
		// problematic children. Report fine-grained errors on them, then
		// return false so the parent falls back to checker-based
		// serialization (avoiding issues like reusing raw JSON string
		// literal property names from the pseudochecker's AST).
		if (!t->AsPseudoTypeInferred()->errorNodes.empty()) {
			if (reportErrors) {
				for (Node* n : t->AsPseudoTypeInferred()->errorNodes) {
					ctx->tracker->ReportInferenceFallback(n);
				}
			}
			return false;
		}
		if (reportErrors) {
			ctx->tracker->ReportInferenceFallback(
				t->AsPseudoTypeInferred()->expression);
		}
		return false;
	}
	case pseudochecker::PseudoTypeKind::ObjectLiteral: {
		auto* pt = t->AsPseudoTypeObjectLiteral();
		if (type_ == nullptr) {
			return false;
		}
		std::vector<Symbol*> targetProps =
		    ch->getPropertiesOfType(undefinedStripped);
		// Count total declarations across all target prop symbols to handle
		// getter/setter pairs, which are two elements in pt.Elements but
		// only one symbol in targetProps.
		int targetDeclCount = 0;
		for (Symbol* prop : targetProps) {
			targetDeclCount += (int)prop->declarations.size();
		}
		if ((int)pt->elements.size() != targetDeclCount) {
			return false;
		}
		for (pseudochecker::PseudoObjectElement* el : pt->elements) {
			Symbol* targetProp = nullptr;
			Symbol* elemSymbol = el->name->parent->symbol();
			if (elemSymbol != nullptr) {
				targetProp = ch->getPropertyOfType(undefinedStripped,
				                                   elemSymbol->name);
			}
			if (targetProp == nullptr) {
				// Name lookup failed or returned no result; search target
				// properties for one whose declaration name node matches the
				// one we have
				for (Symbol* prop : targetProps) {
					if (prop->valueDeclaration != nullptr &&
					    prop->valueDeclaration->name() == el->name) {
						targetProp = prop;
						break;
					}
				}
				if (targetProp == nullptr) {
					if (reportErrors) {
						ctx->tracker->ReportInferenceFallback(
							el->name->parent);
					}
					return false;
				}
			}
			bool targetIsOptional =
			    (targetProp->flags & SymbolFlagsOptional) != 0;
			if (el->optional != targetIsOptional) {
				if (reportErrors) {
					ctx->tracker->ReportInferenceFallback(el->name->parent);
				}
				return false;
			}
			Type* propType = ch->getTypeOfSymbol(targetProp);
			propType = ch->removeMissingType(propType, targetIsOptional);
			switch (el->kind) {
			case pseudochecker::PseudoObjectElementKind::PropertyAssignment: {
				auto* d = el->propertyAssignment;
				if (!pseudoTypeEquivalentToType(d->type, propType,
				                              el->optional, false)) {
					if (reportErrors) {
						if (d->type->kind ==
						        pseudochecker::PseudoTypeKind::Inferred &&
						    !d->type->AsPseudoTypeInferred()
							     ->errorNodes.empty()) {
							// Re-report the fine-grained error nodes; the
							// recursive call used reportErrors=false
							for (Node* n :
							     d->type->AsPseudoTypeInferred()
								     ->errorNodes) {
								ctx->tracker->ReportInferenceFallback(n);
							}
						} else if (!isStructuralPseudoType(d->type)) {
							ctx->tracker->ReportInferenceFallback(
								el->name->parent);
						}
					}
					return false;
				}
				break;
			}
			case pseudochecker::PseudoObjectElementKind::Method: {
				auto* d = el->method;
				Signature* targetSig = ch->getSingleCallSignature(propType);
				if (targetSig == nullptr) {
					// Target property type doesn't have a single call
					// signature; can't validate
					continue;
				}
				bool paramEq = pseudoParametersEquivalentToParameters(
					d->parameters, targetSig, reportErrors,
					el->name->parent);
				if (!paramEq) {
					return false;
				}
				TypePredicate* targetPredicate =
				    ch->getTypePredicateOfSignature(targetSig);
				if (targetPredicate != nullptr) {
					if (!pseudoReturnTypeMatchesPredicate(d->returnType,
					                                      targetPredicate)) {
						if (reportErrors) {
							ctx->tracker->ReportInferenceFallback(
								el->name->parent);
						}
						return false;
					}
				} else if (!pseudoTypeEquivalentToType(
					           d->returnType,
					           ch->getReturnTypeOfSignature(targetSig), false,
					           false)) {
					if (reportErrors) {
						ctx->tracker->ReportInferenceFallback(
							el->name->parent);
					}
					return false;
				}
				break;
			}
			case pseudochecker::PseudoObjectElementKind::GetAccessor: {
				auto* d = el->getAccessor;
				if (!pseudoTypeEquivalentToType(d->type, propType, false,
				                              false)) {
					if (reportErrors) {
						ctx->tracker->ReportInferenceFallback(
							el->name->parent);
					}
					return false;
				}
				break;
			}
			case pseudochecker::PseudoObjectElementKind::SetAccessor: {
				auto* d = el->setAccessor;
				Type* writeType = ch->getWriteTypeOfSymbol(targetProp);
				if (!pseudoTypeEquivalentToType(d->parameter->type,
				                              writeType, false, false)) {
					if (reportErrors) {
						ctx->tracker->ReportInferenceFallback(
							el->name->parent);
					}
					return false;
				}
				break;
			}
			default:
				break;
			}
		}
		return true;
	}
	case pseudochecker::PseudoTypeKind::Tuple: {
		auto* pt = t->AsPseudoTypeTuple();
		if (undefinedStripped == nullptr ||
		    !isTupleType(undefinedStripped)) {
			return false;
		}
		TupleType* tupleTarget = undefinedStripped->TargetTupleType();
		// Pseudo-tuples come from `as const` array literals, so they only
		// ever have required elements. If the target tuple has optional,
		// rest, or variadic elements, the structures can't match.
		if ((tupleTarget->combinedFlags & ElementFlagsNonRequired) != 0) {
			return false;
		}
		std::vector<Type*> elementTypes =
		    ch->getTypeArguments(undefinedStripped);
		if (pt->elements.size() != elementTypes.size()) {
			return false;
		}
		for (size_t i = 0; i < pt->elements.size(); i++) {
			if (!pseudoTypeEquivalentToType(pt->elements[i], elementTypes[i],
			                              false, reportErrors)) {
				return false;
			}
		}
		return true;
	}
	case pseudochecker::PseudoTypeKind::SingleCallSignature: {
		Signature* targetSig = ch->getSingleCallSignature(undefinedStripped);
		if (targetSig == nullptr) {
			return false;
		}
		auto* pt = t->AsPseudoTypeSingleCallSignature();
		if (targetSig->typeParameters.size() != pt->typeParameters.size()) {
			if (reportErrors) {
				ctx->tracker->ReportInferenceFallback(pt->signature);
			}
			return false;
		}
		bool paramEq = pseudoParametersEquivalentToParameters(
			pt->parameters, targetSig, reportErrors, pt->signature);
		if (!paramEq) {
			return false;
		}
		TypePredicate* targetPredicate =
		    ch->getTypePredicateOfSignature(targetSig);
		if (targetPredicate != nullptr) {
			if (!pseudoReturnTypeMatchesPredicate(pt->returnType,
			                                      targetPredicate)) {
				if (reportErrors) {
					ctx->tracker->ReportInferenceFallback(pt->signature);
				}
				return false;
			}
		} else if (!pseudoTypeEquivalentToType(
		               pt->returnType,
		               ch->getReturnTypeOfSignature(targetSig), false,
		               reportErrors)) {
			// error reported within the return type
			return false;
		}
		return true;
	}
	case pseudochecker::PseudoTypeKind::NoResult:
		if (reportErrors) {
			ctx->tracker->ReportInferenceFallback(
				t->AsPseudoTypeNoResult()->declaration);
		}
		return false;
	default:
		return false;
	}
}

// pseudoParametersEquivalentToParameters (pseudotypenodebuilder.go:584).
bool NodeBuilderImpl::pseudoParametersEquivalentToParameters(
	std::vector<pseudochecker::PseudoParameter*> params, Signature* targetSig,
	bool reportErrors, Node* nonParamErrorLocation) {
	if (targetSig->thisParameter != nullptr && params.empty()) {
		if (reportErrors) {
			ctx->tracker->ReportInferenceFallback(
				nonParamErrorLocation); // missing `this` param
		}
		return false;
	} else if (targetSig->thisParameter != nullptr &&
	           isThisIdentifier(params[0]->name)) {
		Symbol* targetParam = targetSig->thisParameter;
		Type* paramType = ch->getTypeOfParameter(targetParam);
		if (!pseudoTypeEquivalentToType(params[0]->type, paramType,
		                              params[0]->optional, false)) {
			if (reportErrors) {
				ctx->tracker->ReportInferenceFallback(
					params[0]->name->parent);
			}
			return false;
		}
		params.erase(params.begin());
	} else if (targetSig->thisParameter != nullptr) {
		if (reportErrors) {
			ctx->tracker->ReportInferenceFallback(nonParamErrorLocation);
		}
		return false;
	}
	if (targetSig->parameters.size() != params.size()) {
		if (reportErrors) {
			ctx->tracker->ReportInferenceFallback(nonParamErrorLocation);
		}
		return false; // TODO: spread tuple params may mess with this check
	}
	for (size_t i = 0; i < params.size(); i++) {
		pseudochecker::PseudoParameter* p = params[i];
		Symbol* targetParam = targetSig->parameters[i];
		if (p->optional !=
		    ch->isOptionalParameter(targetParam->valueDeclaration)) {
			if (reportErrors) {
				ctx->tracker->ReportInferenceFallback(p->name->parent);
			}
			return false;
		}
		Type* paramType = ch->getTypeOfParameter(targetParam);
		if (!pseudoTypeEquivalentToType(p->type, paramType, p->optional,
		                              false)) {
			if (reportErrors) {
				ctx->tracker->ReportInferenceFallback(p->name->parent);
			}
			return false;
		}
	}
	return true;
}

// isStructuralPseudoType — ported in the helpers section above.

// pseudoReturnTypeMatchesPredicate checks if a pseudo return type (which
// should be a Direct type wrapping a TypePredicate) matches the given type
// predicate from the checker.
bool NodeBuilderImpl::pseudoReturnTypeMatchesPredicate(
	pseudochecker::PseudoType* rt, TypePredicate* predicate) {
	if (rt->kind != pseudochecker::PseudoTypeKind::Direct) {
		return false;
	}
	Node* node = rt->AsPseudoTypeDirect()->typeNode;
	if (!isTypePredicateNode(node)) {
		return false;
	}
	auto* tp = node->as<TypePredicateNode>();
	// Check asserts modifier matches
	bool isAsserts = tp->AssertsModifier != nullptr;
	bool predicateIsAsserts =
	    predicate->kind == TypePredicateKind::AssertsThis ||
	    predicate->kind == TypePredicateKind::AssertsIdentifier;
	if (isAsserts != predicateIsAsserts) {
		return false;
	}
	// Check this vs identifier matches
	bool isThis = isThisTypeNode(tp->ParameterName);
	bool predicateIsThis = predicate->kind == TypePredicateKind::This ||
	                       predicate->kind == TypePredicateKind::AssertsThis;
	if (isThis != predicateIsThis) {
		return false;
	}
	// For identifier predicates, check parameter name matches
	if (!isThis) {
		if (tp->ParameterName->text() != predicate->parameterName) {
			return false;
		}
	}
	// Check the narrowed type, if any
	if (predicate->t != nullptr) {
		if (tp->Type == nullptr) {
			return false;
		}
		Type* predicateTypeFromNode = ch->getTypeFromTypeNode(tp->Type);
		if (predicateTypeFromNode != predicate->t) {
			if (ch->compareTypesIdentical(predicateTypeFromNode,
			                            predicate->t) != Ternary::True) {
				return false;
			}
		}
	} else if (tp->Type != nullptr) {
		return false;
	}
	return true;
}

// pseudoTypeToType (pseudotypenodebuilder.go:695).
Type* NodeBuilderImpl::pseudoTypeToType(pseudochecker::PseudoType* t) {
	// !!! TODO: only literal types currently mapped because this is only
	// used to determine if literal contextual typing need apply to the
	// pseudotype. If this is used more broadly, the implementation needs to
	// be filled out more to handle the structural pseudotypes - signatures,
	// objects, tuples, etc
	TSC_ASSERT(t != nullptr, "Attempted to realize nil pseudotype");
	switch (t->kind) {
	case pseudochecker::PseudoTypeKind::Direct:
		return ch->getTypeFromTypeNode(t->AsPseudoTypeDirect()->typeNode);
	case pseudochecker::PseudoTypeKind::Inferred: {
		Node* node = t->AsPseudoTypeInferred()->expression;
		if (t->AsPseudoTypeInferred()->isSignatureReturn) {
			return ch->getReturnTypeOfSignature(
				ch->getSignatureFromDeclaration(node));
		}
		Type* ty =
		    ch->getWidenedType(ch->getRegularTypeOfExpression(node));
		return ty;
	}
	case pseudochecker::PseudoTypeKind::NoResult:
		return nullptr; // TODO: extract type selection logic from
		                // `serializeTypeForDeclaration`, not needed for
		                // current usecases but needed if completeness
		                // becomes required
	case pseudochecker::PseudoTypeKind::MaybeConstLocation: {
		auto* d = t->AsPseudoTypeMaybeConstLocation();
		if (ch->isConstContext(d->node)) {
			return pseudoTypeToType(d->constType);
		}
		return pseudoTypeToType(d->regularType);
	}
	case pseudochecker::PseudoTypeKind::Union: {
		std::vector<Type*> res;
		bool hasElidedType = false;
		auto& members = t->AsPseudoTypeUnion()->types;
		for (pseudochecker::PseudoType* m : members) {
			if (!ch->strictNullChecks) {
				if (m->kind == pseudochecker::PseudoTypeKind::Undefined ||
				    m->kind == pseudochecker::PseudoTypeKind::Null) {
					hasElidedType = true;
					continue;
				}
			}
			Type* t2 = pseudoTypeToType(m);
			if (t2 == nullptr) {
				return nullptr; // propagate failure
			}
			res.push_back(t2);
		}
		if (res.size() == 1) {
			return res[0];
		}
		if (res.empty()) {
			if (hasElidedType) {
				return ch->anyType;
			}
			return ch->neverType;
		}
		return ch->getUnionType(res);
	}
	case pseudochecker::PseudoTypeKind::Undefined:
		return ch->undefinedWideningType;
	case pseudochecker::PseudoTypeKind::Null:
		return ch->nullWideningType;
	case pseudochecker::PseudoTypeKind::Any:
		return ch->anyType;
	case pseudochecker::PseudoTypeKind::String:
		return ch->stringType;
	case pseudochecker::PseudoTypeKind::Number:
		return ch->numberType;
	case pseudochecker::PseudoTypeKind::BigInt:
		return ch->bigintType;
	case pseudochecker::PseudoTypeKind::Boolean:
		return ch->booleanType;
	case pseudochecker::PseudoTypeKind::False:
		return ch->falseType;
	case pseudochecker::PseudoTypeKind::True:
		return ch->trueType;
	case pseudochecker::PseudoTypeKind::StringLiteral:
	case pseudochecker::PseudoTypeKind::NumericLiteral:
	case pseudochecker::PseudoTypeKind::BigIntLiteral: {
		Node* source = t->AsPseudoTypeLiteral()->node;
		return ch->getRegularTypeOfExpression(
			source); // big shortcut, uses cached expression types where
		         // possible
	}
	case pseudochecker::PseudoTypeKind::ObjectLiteral:
	case pseudochecker::PseudoTypeKind::SingleCallSignature:
	case pseudochecker::PseudoTypeKind::Tuple:
		return nullptr; // no simple mapping to a type, since these are
		                // structural types
	default:
		TSC_ASSERT(false, "Unhandled pseudochecker.PseudoTypeKind in pseudoTypeToType");
		return nullptr;
	}
}

// ===========================================================================
// nodebuilder_hover.go
// ===========================================================================

// isExpanding — ported in the helpers section above.

// expandSymbolForHover produces declaration nodes (class, interface, enum,
// module) for a symbol for expandable hover. This is a focused alternative to
// the full symbolTableToDeclarationStatements machinery used by declaration
// emit — it directly builds the declaration nodes hover needs without the
// declaration-emit scaffolding (deferred privates, symbol name remapping,
// export modifier computation, alias resolution, visited symbols tracking).
std::vector<Node*> NodeBuilderImpl::expandSymbolForHover(Symbol* symbol) {
	std::vector<Node*> results;
	if ((symbol->flags & SymbolFlagsEnum) != 0) {
		if (Node* node = expandEnumDecl(symbol); node != nullptr) {
			results.push_back(node);
		}
	}
	if ((symbol->flags & SymbolFlagsClass) != 0) {
		if (Node* node = expandClassDecl(symbol); node != nullptr) {
			results.push_back(node);
		}
	}
	// Module/namespace before interface (matching Strada ordering for
	// merged declarations)
	if ((symbol->flags &
	     (SymbolFlagsValueModule | SymbolFlagsNamespaceModule)) != 0) {
		if (Node* node = expandModuleDecl(symbol); node != nullptr) {
			results.push_back(node);
		}
	}
	if ((symbol->flags & SymbolFlagsInterface) != 0 &&
	    (symbol->flags & SymbolFlagsClass) == 0) {
		if (Node* node = expandInterfaceDecl(symbol); node != nullptr) {
			results.push_back(node);
		}
	}
	return results;
}

// expandEnumDecl produces an EnumDeclaration node with all members.
Node* NodeBuilderImpl::expandEnumDecl(Symbol* symbol) {
	const std::string& name = symbol->name;
	ctx->approximateLength += 9 + (int)name.size();
	std::vector<Symbol*> memberProps = filterVec(
		ch->getPropertiesOfType(ch->getTypeOfSymbol(symbol)),
		[](Symbol* p) {
			return (p->flags & SymbolFlagsEnumMember) != 0;
		});
	std::vector<Node*> members;
	for (size_t i = 0; i < memberProps.size(); i++) {
		Symbol* p = memberProps[i];
		if (checkTruncationLengthIfExpanding() &&
		    (int)i + 3 < (int)memberProps.size() - 1) {
			ctx->expansionTruncated = true;
			members.push_back(f->newEnumMember(
				f->newStringLiteral(
					" ... " +
					    std::to_string((int)memberProps.size() - (int)i - 1) +
					    " more ... ",
					0),
				nullptr));
			Symbol* last = memberProps[memberProps.size() - 1];
			members.push_back(f->newEnumMember(f->newIdentifier(last->name),
			                                   enumMemberInitializer(last)));
			break;
		}
		Node* memberDecl = findVec(p->declarations, isEnumMember);
		Node* initializer = nullptr;
		if (memberDecl != nullptr &&
		    memberDecl->as<EnumMember>()->Initializer != nullptr) {
			initializer = deepCloneNode(
				*f, memberDecl->as<EnumMember>()->Initializer);
		} else {
			initializer = enumMemberInitializer(p);
		}
		ctx->approximateLength += 4 + (int)p->name.size();
		if (initializer != nullptr) {
			ctx->approximateLength += 5; // " = " + value estimate
		}
		members.push_back(
			f->newEnumMember(f->newIdentifier(p->name), initializer));
	}

	ModifierFlags constModifier = ModifierFlagsNone;
	if (isConstEnumSymbol(symbol)) {
		constModifier = ModifierFlagsConst;
	}
	ModifierList* mods = nullptr;
	if (constModifier != 0) {
		mods = f->newModifierList(createModifiersFromModifierFlags(
			constModifier,
			[](NodeFactory& factory, Kind k) {
				return factory.newToken(k);
			},
			*f));
	}
	return f->newEnumDeclaration(mods, f->newIdentifier(name),
	                             f->newNodeList(members));
}

// enumMemberInitializer (nodebuilder_hover.go:90).
Node* NodeBuilderImpl::enumMemberInitializer(Symbol* p) {
	Node* memberDecl = findVec(p->declarations, isEnumMember);
	if (memberDecl == nullptr) {
		return nullptr;
	}
	LiteralValue val = ch->GetConstantValue(memberDecl);
	if (std::holds_alternative<std::monostate>(val)) {
		return nullptr;
	}
	if (auto* v = std::get_if<std::string>(&val)) {
		return f->newStringLiteral(*v, 0);
	}
	if (auto* v = std::get_if<Number>(&val)) {
		return f->newNumericLiteral(v->string(), 0);
	}
	return nullptr;
}

static std::vector<Node*> typeElementsToClassElements(
	NodeFactory& f, std::vector<Node*> members);

// expandClassDecl produces a ClassDeclaration node with heritage clauses and
// members.
Node* NodeBuilderImpl::expandClassDecl(Symbol* symbol) {
	const std::string& name = symbol->name;
	ctx->approximateLength += 9 + (int)name.size();

	std::vector<Node*> classLikeDeclarations =
	    filterVec(symbol->declarations, isClassLike);
	Node* originalDecl = firstOrNilVec(classLikeDeclarations);
	Node* oldEnclosing = ctx->enclosingDeclaration;
	if (originalDecl != nullptr) {
		ctx->enclosingDeclaration = originalDecl;
	}
	auto restoreEnclosing = scopeExit(
		[this, oldEnclosing] { ctx->enclosingDeclaration = oldEnclosing; });

	std::vector<Type*> localParams =
	    ch->getLocalTypeParametersOfClassOrInterfaceOrTypeAlias(symbol);
	std::vector<Node*> typeParamDecls = mapVec(
		localParams, [this](Type* p) { return typeParameterToDeclaration(p); });

	Type* declaredType = ch->getDeclaredTypeOfClassOrInterface(symbol);
	Type* classType = ch->getTypeWithThisArgument(declaredType, nullptr, false);
	std::vector<Type*> baseTypes = ch->getBaseTypes(ch->getTargetType(classType));
	Type* staticType = ch->getTypeOfSymbol(symbol);
	bool isClass = staticType->symbol != nullptr &&
	               staticType->symbol->valueDeclaration != nullptr &&
	               isClassLike(staticType->symbol->valueDeclaration);
	Type* staticBaseType;
	if (isClass) {
		staticBaseType = ch->getBaseConstructorTypeOfClass(declaredType);
	} else {
		staticBaseType = ch->anyType;
	}

	// Heritage clauses
	std::vector<Node*> heritageClauses =
	    hoverHeritageClauses(classLikeDeclarations);

	// Instance members via addPropertyToElementList (reusing existing
	// serialization), then convert TypeElements to ClassElements and add
	// class-specific modifiers
	std::vector<Symbol*> allProps = ch->getPropertiesOfType(classType);
	std::vector<Symbol*> symbolProps =
	    filterInheritedProperties(classType, baseTypes, allProps);
	std::vector<Symbol*> publicProps =
	    filterVec(symbolProps,
	              [](Symbol* s) { return !isHashPrivate(s); });
	bool hasPrivate = someVec(symbolProps, isHashPrivate);

	std::vector<Node*> instanceMembers;
	instanceMembers =
	    serializePropertiesWithTruncation(publicProps, instanceMembers);
	instanceMembers = typeElementsToClassElements(*f, instanceMembers);
	instanceMembers = addClassModifiers(instanceMembers, false);

	// Static members
	std::vector<Symbol*> staticProps =
	    filterVec(ch->getPropertiesOfType(staticType), [this](Symbol* p) {
		    return (p->flags & SymbolFlagsPrototype) == 0 &&
		           p->name != "prototype" && !isNamespaceMember(p);
	    });
	std::vector<Node*> staticMembers;
	staticMembers =
	    serializePropertiesWithTruncation(staticProps, staticMembers);
	staticMembers = typeElementsToClassElements(*f, staticMembers);
	staticMembers = addClassModifiers(staticMembers, true);

	// Hash-private members
	std::vector<Node*> privateMembers;
	if (hasPrivate) {
		privateMembers = serializePropertiesWithTruncation(
			filterVec(symbolProps, isHashPrivate), privateMembers);
		privateMembers = typeElementsToClassElements(*f, privateMembers);
	}

	// Constructors
	std::vector<Node*> constructors =
	    serializeConstructors(staticType, staticBaseType, isClass, symbol);

	// Index signatures
	std::vector<Node*> indexSigs =
	    serializeIndexSignaturesOfType(classType, firstOrNilVec(baseTypes));

	std::vector<Node*> allMembers;
	allMembers.reserve(indexSigs.size() + staticMembers.size() +
	                   constructors.size() + instanceMembers.size() +
	                   privateMembers.size());
	allMembers.insert(allMembers.end(), indexSigs.begin(), indexSigs.end());
	allMembers.insert(allMembers.end(), staticMembers.begin(),
	                  staticMembers.end());
	allMembers.insert(allMembers.end(), constructors.begin(),
	                  constructors.end());
	allMembers.insert(allMembers.end(), instanceMembers.begin(),
	                  instanceMembers.end());
	allMembers.insert(allMembers.end(), privateMembers.begin(),
	                  privateMembers.end());

	return f->newClassDeclaration(nullptr, f->newIdentifier(name),
	                              f->newNodeList(typeParamDecls),
	                              f->newNodeList(heritageClauses),
	                              f->newNodeList(allMembers));
}

// addClassModifiers post-processes class member nodes to add class-specific
// modifiers (private, protected, public, abstract, static) based on the
// original symbol declarations.
std::vector<Node*> NodeBuilderImpl::addClassModifiers(
	std::vector<Node*> members, bool isStatic) {
	for (size_t i = 0; i < members.size(); i++) {
		Node* m = members[i];
		// Find the symbol for this member by matching the property name
		Symbol* memberSymbol = nullptr;
		Node* memberName = m->name();
		if (memberName != nullptr) {
			auto it = idToSymbol.find(memberName);
			if (it != idToSymbol.end()) {
				memberSymbol = it->second;
			}
		}
		if (memberSymbol == nullptr) {
			continue;
		}
		ModifierFlags modFlags =
		    getDeclarationModifierFlagsFromSymbol(memberSymbol) &
		    ~ModifierFlagsAsync;
		if (isStatic) {
			modFlags |= ModifierFlagsStatic;
		}
		if (modFlags != 0 && canHaveModifiers(m)) {
			ModifierFlags existing = m->modifierFlags();
			if (modFlags != existing) {
				members[i] = replaceModifiers(
					*f, m,
					f->newModifierList(createModifiersFromModifierFlags(
						modFlags | existing,
						[](NodeFactory& factory, Kind k) {
							return factory.newToken(k);
						},
						*f)));
			}
		}
	}
	return members;
}

// typeElementsToClassElements converts TypeElement nodes (PropertySignature,
// MethodSignature) to their ClassElement equivalents (PropertyDeclaration,
// MethodDeclaration) so they can be used as members of a ClassDeclaration.
// Nodes that are already ClassElements pass through unchanged.
static std::vector<Node*> typeElementsToClassElements(
	NodeFactory& f, std::vector<Node*> members) {
	for (size_t i = 0; i < members.size(); i++) {
		Node* m = members[i];
		switch (m->kind) {
		case Kind::PropertySignature: {
			auto* ps = m->as<PropertySignatureDeclaration>();
			members[i] = f.newPropertyDeclaration(m->modifiers(), ps->name,
			                                      ps->PostfixToken, ps->Type,
			                                      nullptr);
			break;
		}
		case Kind::MethodSignature: {
			auto* ms = m->as<MethodSignatureDeclaration>();
			members[i] = f.newMethodDeclaration(
				m->modifiers(), nullptr, ms->name, ms->PostfixToken,
				ms->TypeParameters, ms->Parameters, ms->Type, nullptr,
				nullptr);
			break;
		}
		default:
			break;
		}
	}
	return members;
}

// expandInterfaceDecl produces an InterfaceDeclaration with members.
// Reuses addPropertyToElementList for property serialization and
// signatureToSignatureDeclarationHelper for signatures.
Node* NodeBuilderImpl::expandInterfaceDecl(Symbol* symbol) {
	const std::string& name = symbol->name;
	ctx->approximateLength += 14 + (int)name.size();

	Type* interfaceType = ch->getDeclaredTypeOfClassOrInterface(symbol);
	std::vector<Node*> interfaceDeclarations =
	    filterVec(symbol->declarations, isInterfaceDeclaration);
	std::vector<Type*> localParams =
	    ch->getLocalTypeParametersOfClassOrInterfaceOrTypeAlias(symbol);
	std::vector<Node*> typeParamDecls = mapVec(
		localParams, [this](Type* p) { return typeParameterToDeclaration(p); });
	std::vector<Type*> baseTypes = ch->getBaseTypes(interfaceType);
	Type* baseType = nullptr;
	if (!baseTypes.empty()) {
		baseType = ch->getIntersectionType(baseTypes);
	}

	// Members: reuse existing serialization functions
	StructuredType* resolved = ch->resolveStructuredTypeMembers(interfaceType);
	std::vector<Node*> members;

	// Index signatures, filtering those identical to base
	std::vector<Node*> indexSigs =
	    serializeIndexSignaturesOfType(interfaceType, baseType);
	members.insert(members.end(), indexSigs.begin(), indexSigs.end());
	// Construct signatures (skip abstract)
	for (Signature* sig : constructSignaturesOf(resolved)) {
		if ((sig->flags & SignatureFlagsAbstract) != 0) {
			continue;
		}
		members.push_back(signatureToSignatureDeclarationHelper(
			sig, Kind::ConstructSignature, nullptr));
	}
	// Call signatures
	for (Signature* sig : callSignaturesOf(resolved)) {
		members.push_back(signatureToSignatureDeclarationHelper(
			sig, Kind::CallSignature, nullptr));
	}
	// Properties, filtering inherited
	std::vector<Symbol*> filteredProps = filterInheritedProperties(
		interfaceType, baseTypes, resolved->properties);
	members = serializePropertiesWithTruncation(filteredProps, members);

	// Heritage clauses
	std::vector<Node*> heritageClauses =
	    hoverHeritageClauses(interfaceDeclarations);

	return f->newInterfaceDeclaration(nullptr, f->newIdentifier(name),
	                                  f->newNodeList(typeParamDecls),
	                                  f->newNodeList(heritageClauses),
	                                  f->newNodeList(members));
}

// hoverHeritageClauses (nodebuilder_hover.go:261).
std::vector<Node*> NodeBuilderImpl::hoverHeritageClauses(
	std::vector<Node*> declarations) {
	std::vector<Node*> extendsTypes;
	std::vector<Node*> implementsTypes;
	for (Node* declaration : declarations) {
		for (Node* heritageElement :
		     getExtendsHeritageClauseElements(declaration)) {
			extendsTypes.push_back(deepCloneNode(*f, heritageElement));
		}
		for (Node* heritageElement :
		     getHeritageElements(declaration, Kind::ImplementsKeyword)) {
			implementsTypes.push_back(deepCloneNode(*f, heritageElement));
		}
	}

	std::vector<Node*> heritageClauses;
	if (!extendsTypes.empty()) {
		heritageClauses.push_back(f->newHeritageClause(
			Kind::ExtendsKeyword, f->newNodeList(extendsTypes)));
	}
	if (!implementsTypes.empty()) {
		heritageClauses.push_back(f->newHeritageClause(
			Kind::ImplementsKeyword, f->newNodeList(implementsTypes)));
	}
	return heritageClauses;
}

// serializePropertiesWithTruncation iterates properties using
// addPropertyToElementList, with truncation checks matching Strada's
// createTypeNodesFromResolvedType behavior.
std::vector<Node*> NodeBuilderImpl::serializePropertiesWithTruncation(
	std::vector<Symbol*> properties, std::vector<Node*> elements) {
	properties = filterVec(properties, [](Symbol* p) {
		return (p->flags & SymbolFlagsPrototype) == 0;
	});
	for (size_t i = 0; i < properties.size(); i++) {
		Symbol* p = properties[i];
		if (checkTruncationLengthIfExpanding() &&
		    ((int)i + 3 < (int)properties.size() - 1)) {
			ctx->expansionTruncated = true;
			std::string text =
			    "... " +
			    std::to_string((int)properties.size() - (int)i - 1) +
			    " more ...";
			elements.push_back(f->newPropertySignatureDeclaration(
				nullptr, f->newIdentifier(text), nullptr, nullptr,
				nullptr));
			elements = addPropertyToElementList(
				properties[properties.size() - 1], elements);
			break;
		}
		elements = addPropertyToElementList(p, elements);
	}
	return elements;
}

// serializeConstructors builds constructor signature(s) for a class, with
// base type filtering.
std::vector<Node*> NodeBuilderImpl::serializeConstructors(
	Type* staticType, Type* staticBaseType, bool isClass, Symbol* symbol) {
	bool isNonConstructable =
	    !isClass && symbol->valueDeclaration != nullptr &&
	    isInJSFile(symbol->valueDeclaration) &&
	    ch->getSignaturesOfType(staticType, SignatureKind::Construct).empty();
	if (isNonConstructable) {
		ctx->approximateLength += 21;
		std::vector<Node*> modifiers = createModifiersFromModifierFlags(
			ModifierFlagsPrivate,
			[](NodeFactory& factory, Kind k) {
				return factory.newToken(k);
			},
			*f);
		return {f->newConstructorDeclaration(f->newModifierList(modifiers),
		                                     nullptr, f->newNodeList({}),
		                                     nullptr, nullptr, nullptr)};
	}
	std::vector<Signature*> signatures =
	    ch->getSignaturesOfType(staticType, SignatureKind::Construct);
	if (staticBaseType != nullptr) {
		std::vector<Signature*> baseSigs =
		    ch->getSignaturesOfType(staticBaseType, SignatureKind::Construct);
		if (baseSigs.empty() &&
		    everyVec(signatures, [](Signature* sig) {
			    return sig->parameters.empty();
		    })) {
			return {};
		}
		if (baseSigs.size() == signatures.size()) {
			bool allMatch = true;
			for (size_t i = 0; i < baseSigs.size(); i++) {
				if (ch->compareSignaturesIdentical(
					    signatures[i], baseSigs[i], false, false, true,
					    [&](Type* a, Type* b) { return ch->compareTypesIdentical(a, b); }) != Ternary::True) {
					allMatch = false;
					break;
				}
			}
			if (allMatch) {
				return {};
			}
		}
		ModifierFlags privateProtected = 0;
		for (Signature* sig : signatures) {
			if (sig->declaration != nullptr) {
				privateProtected |=
				    sig->declaration->modifierFlags() &
				    (ModifierFlagsPrivate | ModifierFlagsProtected);
			}
		}
		if (privateProtected != 0) {
			return {f->newConstructorDeclaration(
				f->newModifierList(createModifiersFromModifierFlags(
					privateProtected,
					[](NodeFactory& factory, Kind k) {
						return factory.newToken(k);
					},
					*f)),
				nullptr, f->newNodeList({}), nullptr, nullptr, nullptr)};
		}
	} else if (everyVec(signatures, [](Signature* sig) {
				   return sig->parameters.empty();
			   })) {
		return {};
	}
	std::vector<Node*> result;
	for (Signature* sig : signatures) {
		ctx->approximateLength++;
		result.push_back(signatureToSignatureDeclarationHelper(
			sig, Kind::Constructor, nullptr));
	}
	return result;
}

// serializeIndexSignaturesOfType builds index signature declarations,
// filtering those identical to baseType.
std::vector<Node*> NodeBuilderImpl::serializeIndexSignaturesOfType(
	Type* input, Type* baseType) {
	std::vector<Node*> result;
	for (IndexInfo* info : ch->getIndexInfosOfType(input)) {
		if (baseType != nullptr) {
			IndexInfo* baseInfo =
			    ch->getIndexInfoOfType(baseType, info->keyType);
			if (baseInfo != nullptr &&
			    ch->isTypeIdenticalTo(info->valueType,
			                          baseInfo->valueType)) {
				continue;
			}
		}
		result.push_back(
			indexInfoToIndexSignatureDeclarationHelper(info, nullptr));
	}
	return result;
}

// serializeNamespaceMember produces the appropriate declaration node for a
// namespace member based on its symbol flags (type alias, enum, class,
// interface, nested namespace, or variable).
Node* NodeBuilderImpl::serializeNamespaceMember(Symbol* resolved,
                                                const std::string& name) {
	if ((resolved->flags & SymbolFlagsTypeAlias) != 0) {
		return serializeTypeAliasForNamespace(resolved, name);
	}
	if ((resolved->flags & SymbolFlagsEnum) != 0) {
		return expandEnumDecl(resolved);
	}
	if ((resolved->flags & SymbolFlagsClass) != 0) {
		return expandClassDecl(resolved);
	}
	if ((resolved->flags & SymbolFlagsInterface) != 0) {
		return expandInterfaceDecl(resolved);
	}
	if ((resolved->flags &
	     (SymbolFlagsValueModule | SymbolFlagsNamespaceModule)) != 0) {
		return expandModuleDecl(resolved);
	}
	Type* t = ch->getWidenedType(ch->getTypeOfSymbol(resolved));
	ctx->approximateLength += (int)name.size() + 5;
	return f->newVariableStatement(
		nullptr,
		f->newVariableDeclarationList(
			f->newNodeList({f->newVariableDeclaration(
				f->newIdentifier(name), nullptr,
				serializeTypeForDeclaration(nullptr, t, resolved, true),
				nullptr)}),
			NodeFlagsLet));
}

// expandModuleDecl produces a ModuleDeclaration with exported members.
Node* NodeBuilderImpl::expandModuleDecl(Symbol* symbol) {
	const SymbolTable& exports = ch->getExportsOfSymbol(symbol);
	std::vector<Symbol*> members;
	for (auto& [symName, sym] : exports) {
		(void)symName;
		// Filter to namespace-relevant members
		if (!isNamespaceMember(sym)) {
			continue;
		}
		if (!isIdentifierText(sym->name,
		                               LanguageVariant::Standard)) {
			continue;
		}
		members.push_back(sym);
	}
	ch->sortSymbols(members);
	ctx->approximateLength += 14;

	// Use the same name as symbol display.
	nodebuilder::Flags oldFlags = ctx->flags;
	auto restoreFlags =
	    scopeExit([this, oldFlags] { ctx->flags = oldFlags; });
	ctx->flags |= nodebuilder::FlagsWriteTypeParametersInQualifiedName |
	              nodebuilder::Flags(
					  SymbolFormatFlagsUseOnlyExternalAliasing);
	Node* localName = symbolToNode(symbol, SymbolFlagsAll);
	ctx->flags = oldFlags;

	struct hoverStatement {
		Node* node;
		bool isLocal; // local declarations (e.g. alias targets) should not
		              // get export modifier
	};
	std::vector<hoverStatement> bodyStmts;
	collections::Set<Symbol*> emittedLocals;
	for (size_t i = 0; i < members.size(); i++) {
		Symbol* m = members[i];
		if (checkTruncationLengthIfExpanding() &&
		    (int)i + 3 < (int)members.size() - 1) {
			ctx->expansionTruncated = true;
			bodyStmts.push_back({f->newExpressionStatement(
									 f->newIdentifier(
									 "... (" +
									 std::to_string((int)members.size() -
									                (int)i - 1) +
									 " more) ...")),
			                     false});
			i = members.size() -
			    2; // skip to last member after i++ at end of iteration
			continue;
		}

		// Handle alias/re-export symbols
		if ((m->flags & SymbolFlagsAlias) != 0) {
			Node* aliasDecl = ch->getDeclarationOfAliasSymbol(m);
			Symbol* target =
			    ch->getMergedSymbol(ch->getTargetOfAliasDeclaration(aliasDecl));
			if (target != nullptr) {
				// If the alias target is a local symbol (not itself an
				// export), emit its declaration first
				if ((target->flags &
				     (SymbolFlagsBlockScopedVariable |
				      SymbolFlagsFunctionScopedVariable |
				      SymbolFlagsProperty)) != 0) {
					if (!emittedLocals.Has(target)) {
						emittedLocals.Add(target);
						Type* localType = ch->getWidenedType(
							ch->getTypeOfSymbol(target));
						ctx->approximateLength +=
						    (int)target->name.size() + 5;
						Node* localStmt = f->newVariableStatement(
							nullptr,
							f->newVariableDeclarationList(
								f->newNodeList({f->newVariableDeclaration(
									f->newIdentifier(target->name), nullptr,
									serializeTypeForDeclaration(
										nullptr, localType, target, true),
									nullptr)}),
								NodeFlagsLet));
						bodyStmts.push_back({localStmt, true});
					}
				}
				const std::string& targetName = target->name;
				ctx->approximateLength += 16 + (int)m->name.size();
				Node* propertyName = nullptr;
				if (m->name != targetName) {
					propertyName = f->newIdentifier(targetName);
				}
				Node* stmt = f->newExportDeclaration(
					nullptr, false,
					f->newNamedExports(f->newNodeList({f->newExportSpecifier(
						false, propertyName, f->newIdentifier(m->name))})),
					nullptr, nullptr);
				bodyStmts.push_back({stmt, false});
				continue;
			}
		}

		Symbol* resolved = ch->resolveSymbol(m);

		// Handle functions as function declarations
		if ((resolved->flags &
		     (SymbolFlagsFunction | SymbolFlagsMethod)) != 0) {
			Type* t = ch->getTypeOfSymbol(resolved);
			std::vector<Signature*> sigs =
			    ch->getSignaturesOfType(t, SignatureKind::Call);
			for (Signature* sig : sigs) {
				ctx->approximateLength++;
				SignatureToSignatureDeclarationOptions options;
				options.name = f->newIdentifier(m->name);
				Node* decl = signatureToSignatureDeclarationHelper(
					sig, Kind::FunctionDeclaration, &options);
				bodyStmts.push_back({decl, false});
			}
			// If the function also has namespace characteristics, emit an
			// empty namespace.
			Symbol* merged = ch->getMergedSymbol(resolved);
			bool hasModuleExports =
			    (merged->flags & (SymbolFlagsValueModule |
			                      SymbolFlagsNamespaceModule)) != 0 &&
			    !merged->exports.empty();
			if (!hasModuleExports) {
				bodyStmts.push_back(
					{f->newModuleDeclaration(
						 nullptr, Kind::NamespaceKeyword,
						 f->newIdentifier(m->name), nullptr /*attributes*/,
						 f->newModuleBlock(f->newNodeList({}))),
				     false});
			}
			continue;
		}

		// Handle remaining member kinds (type alias, enum, class,
		// interface, namespace, variable)
		if (Node* node = serializeNamespaceMember(resolved, m->name);
		    node != nullptr) {
			bodyStmts.push_back({node, false});
		}
	}

	// Add export modifier to exported statements (skip local declarations
	// and ExportDeclarations).
	for (size_t i = 0; i < bodyStmts.size(); i++) {
		hoverStatement& s = bodyStmts[i];
		if (s.isLocal || isExportDeclaration(s.node)) {
			continue;
		}
		if (canHaveModifiers(s.node)) {
			ModifierFlags mf =
			    s.node->modifierFlags() | ModifierFlagsExport;
			s.node = replaceModifiers(
				*f, s.node,
				f->newModifierList(createModifiersFromModifierFlags(
					mf,
					[](NodeFactory& factory, Kind k) {
						return factory.newToken(k);
					},
					*f)));
		}
	}

	// Collect nodes, stripping export if all statements are exported.
	std::vector<Node*> bodyStatements(bodyStmts.size());
	for (size_t i = 0; i < bodyStmts.size(); i++) {
		bodyStatements[i] = bodyStmts[i].node;
	}
	bool allExported =
	    !bodyStatements.empty() &&
	    everyVec(bodyStatements, [](Node* d) {
		    return hasSyntacticModifier(d, ModifierFlagsExport);
	    });
	if (allExported) {
		for (size_t i = 0; i < bodyStatements.size(); i++) {
			Node* stmt = bodyStatements[i];
			if (canHaveModifiers(stmt)) {
				ModifierFlags mf =
				    stmt->modifierFlags() & ~ModifierFlagsExport;
				bodyStatements[i] = replaceModifiers(
					*f, stmt,
					f->newModifierList(createModifiersFromModifierFlags(
						mf,
						[](NodeFactory& factory, Kind k) {
							return factory.newToken(k);
						},
						*f)));
			}
		}
	}

	Kind keyword = Kind::NamespaceKeyword;
	if (!isIdentifier(localName)) {
		keyword = Kind::ModuleKeyword;
	}
	Node* attributes = nullptr;
	Node* declaration = findVec(symbol->declarations, [](Node* declaration) {
		return isModuleDeclaration(declaration) &&
		       declaration->as<ModuleDeclaration>()->Attributes != nullptr;
	});
	if (declaration != nullptr) {
		attributes = deepCloneNode(
			*f, declaration->as<ModuleDeclaration>()->Attributes);
		e->setEmitFlags(attributes, printer::EFSingleLine);
	}
	return f->newModuleDeclaration(nullptr, keyword, localName, attributes,
	                               f->newModuleBlock(
								   f->newNodeList(bodyStatements)));
}

// serializeTypeAliasForNamespace produces a TypeAliasDeclaration for a type
// alias inside a namespace body.
Node* NodeBuilderImpl::serializeTypeAliasForNamespace(
	Symbol* symbol, const std::string& name) {
	Type* aliasType = ch->getDeclaredTypeOfTypeAlias(symbol);
	std::vector<Type*> typeParams =
	    ch->getLocalTypeParametersOfClassOrInterfaceOrTypeAlias(symbol);
	std::vector<Node*> typeParamDecls = mapVec(
		typeParams, [this](Type* p) { return typeParameterToDeclaration(p); });
	auto restoreFlags = saveRestoreFlags();
	ctx->flags |= nodebuilder::FlagsInTypeAlias;
	Node* typeNode = typeToTypeNode(aliasType);
	restoreFlags();
	ctx->approximateLength += 8 + (int)name.size();
	return f->newTypeAliasDeclaration(nullptr, f->newIdentifier(name),
	                                  f->newNodeList(typeParamDecls),
	                                  typeNode);
}

// filterInheritedProperties removes properties already present in base
// types.
std::vector<Symbol*> NodeBuilderImpl::filterInheritedProperties(
	Type* t, std::vector<Type*> baseTypes,
	std::vector<Symbol*> properties) {
	if (baseTypes.empty()) {
		return properties;
	}
	// Build a lookup from property name to symbol for parent-identity
	// comparison.
	std::unordered_map<std::string, Symbol*> propsByName;
	propsByName.reserve(properties.size());
	for (Symbol* p : properties) {
		propsByName[p->name] = p;
	}
	// Collect names of properties inherited unchanged from base types.
	collections::Set<std::string> inherited;
	for (Type* base : baseTypes) {
		Type* baseWithThis = ch->getTypeWithThisArgument(
			base, ch->getTargetType(t)->AsInterfaceType()->thisType, false);
		for (Symbol* prop : ch->getPropertiesOfType(baseWithThis)) {
			auto it = propsByName.find(prop->name);
			if (it != propsByName.end() && prop->parent == it->second->parent) {
				inherited.Add(prop->name);
			}
		}
	}
	if (inherited.Size() == 0) {
		return properties;
	}
	return filterVec(properties, [&](Symbol* p) {
		return !inherited.Has(p->name);
	});
}

// isNamespaceMember (nodebuilder_hover.go:597).
bool NodeBuilderImpl::isNamespaceMember(Symbol* p) {
	return (p->flags & (SymbolFlagsType | SymbolFlagsNamespace |
	                  SymbolFlagsAlias)) != 0 ||
	       !((p->flags & SymbolFlagsPrototype) != 0 ||
	         p->name == "prototype" ||
	         (p->valueDeclaration != nullptr &&
	          hasStaticModifier(p->valueDeclaration) &&
	          isClassLike(p->valueDeclaration->parent)));
}

// ===========================================================================
// nodebuilder.go
// ===========================================================================

// emitContext — implements NodeBuilderInterface.
printer::EmitContext* NodeBuilder::EmitContext() { return impl->e; }

// ~NodeBuilder / ~NodeBuilderImpl — the builder's lifetime follows Go: it
// dies when its emit context is reset or destroyed (registered via
// EmitContext::addCleanup in newNodeBuilderEx). Deleting impl frees both
// link arenas wholesale; ownedDeletes reclaims the heap objects helpers
// created (contexts, trackers, boundaries, cache entries).
NodeBuilder::~NodeBuilder() { delete impl; }

NodeBuilderImpl::~NodeBuilderImpl() {
	delete cloneBindingNameVisitor;
	delete pc;
	auto pending = std::move(ownedDeletes);
	ownedDeletes.clear();
	for (auto& fn : pending) {
		fn();
	}
}

// markEmitRoots (checker.h) — marks every node the builder's caches and
// in-flight contexts keep alive across the emit context's ReleaseArenas,
// mirroring Go liveness: serializedTypes entries (their TypeNodes are
// handed out via DeepCloneNode and must stay valid), idToSymbol keys,
// current/pushed contexts (enclosingDeclaration, typeParameterNames,
// namedParameters, trackedSymbols, tracker graph including recovery
// boundaries and their deferred-report closures).
void NodeBuilder::markEmitRoots(Arena& a) {
	std::unordered_set<NodeBuilderContext*> seenContexts;
	std::unordered_set<nodebuilder::SymbolTracker*> seenTrackers;
	auto markNode = [&a](Node* n) { a.markPointer(n); };
	auto markTrackedSymbols = [&a, &markNode](
		const std::vector<TrackedSymbolArgs*>& ts) {
		for (TrackedSymbolArgs* t : ts) {
			if (t == nullptr) {
				continue;
			}
			markNode(t->enclosingDeclaration);
			a.scanObject(t, sizeof(TrackedSymbolArgs));
		}
	};
	std::function<void(nodebuilder::SymbolTracker*)> markTracker;
	std::function<void(NodeBuilderContext*)> markContext;
	markContext = [&](NodeBuilderContext* c) {
		if (c == nullptr || !seenContexts.insert(c).second) {
			return;
		}
		a.scanObject(c, sizeof(NodeBuilderContext));
		markNode(c->enclosingDeclaration);
		markNode(c->enclosingFile);
		if (c->typeParameterNames.m != nullptr) {
			for (auto& [id, name] : *c->typeParameterNames.m) {
				markNode(name);
			}
		}
		markTrackedSymbols(c->trackedSymbols);
		markTracker(c->tracker);
	};
	markTracker = [&](nodebuilder::SymbolTracker* t) {
		if (t == nullptr || !seenTrackers.insert(t).second) {
			return;
		}
		if (auto* sti = dynamic_cast<SymbolTrackerImpl*>(t)) {
			markContext(sti->context);
			markTracker(sti->inner);
			return;
		}
		if (auto* wt = dynamic_cast<wrappingTracker*>(t)) {
			markTracker(wt->wrapped);
			if (recoveryBoundary* bound = wt->bound) {
				markContext(bound->ctx);
				markTrackedSymbols(bound->trackedSymbols);
				markTrackedSymbols(bound->oldTrackedSymbols);
				markTracker(bound->oldTracker);
				for (auto& f : bound->deferredReports) {
					a.scanObject(&f, sizeof(f));
				}
			}
		}
		// Other SymbolTracker impls (transformer-side) own no node
		// references into this arena.
	};
	for (auto& [key, links] : impl->links.entries) {
		markNode(key);
		for (auto& [id, entry] : links->serializedTypes) {
			markNode(entry->node);
			a.scanObject(entry, sizeof(SerializedTypeEntry));
			markTrackedSymbols(entry->trackedSymbols);
		}
	}
	for (auto& [key, sym] : impl->idToSymbol) {
		markNode(key);
	}
	markContext(impl->ctx);
	for (NodeBuilderContext* c : ctxStack) {
		markContext(c);
	}
	if (impl->cloneBindingNameVisitor != nullptr) {
		a.scanObject(impl->cloneBindingNameVisitor, sizeof(NodeVisitor));
	}
	if (impl->pc != nullptr) {
		a.scanObject(impl->pc, sizeof(*impl->pc));
	}
}

// enterContext (nodebuilder.go:30).
void NodeBuilder::enterContext(Node* enclosingDeclaration,
                               nodebuilder::Flags flags,
                               nodebuilder::InternalFlags internalFlags,
                               nodebuilder::SymbolTracker* tracker) {
	int verbosityLevel = -1;
	int maxTruncationLength = 0;
	if (verbosity != nullptr) {
		verbosityLevel = verbosity->Level;
		maxTruncationLength = verbosity->MaxTruncationLength;
	}
	ctxStack.push_back(impl->ctx);
	impl->ctx = impl->own(new NodeBuilderContext());
	impl->ctx->impl = impl;
	impl->ctx->host = host;
	impl->ctx->tracker = tracker;
	impl->ctx->flags = flags;
	impl->ctx->internalFlags = internalFlags;
	impl->ctx->maxExpansionDepth = verbosityLevel;
	impl->ctx->maxTruncationLength = maxTruncationLength;
	impl->ctx->enclosingDeclaration = enclosingDeclaration;
	impl->ctx->enclosingFile = getSourceFileOfNode(enclosingDeclaration);
	impl->ctx->inferTypeParameters = {};
	impl->ctx->symbolDepth = {};
	impl->ctx->trackedSymbols = {};
	impl->ctx->reverseMappedStack = {};
	impl->ctx->enclosingSymbolTypes = {};
	impl->ctx->remappedSymbolReferences = {};
	tracker = newSymbolTrackerImpl(impl->ctx, tracker);
	impl->ctx->tracker = tracker;
}

// propagateVerbosityOut copies expansion signals from the context to the
// VerbosityContext output.
void NodeBuilder::propagateVerbosityOut() {
	if (verbosity != nullptr) {
		// Only set to true, never clear — multiple calls share the same
		// VerbosityContext
		if (impl->ctx->canIncreaseExpansionDepth) {
			verbosity->CanIncreaseVerbosity = true;
		}
		if (impl->ctx->expansionTruncated) {
			verbosity->Truncated = true;
		}
	}
}

// popContext (nodebuilder.go:76).
void NodeBuilder::popContext() {
	size_t stackSize = ctxStack.size();
	if (stackSize == 0) {
		impl->ctx = nullptr;
	} else {
		impl->ctx = ctxStack[stackSize - 1];
		ctxStack.pop_back();
	}
}

// exitContext (nodebuilder.go:85).
Node* NodeBuilder::exitContext(Node* result) {
	propagateVerbosityOut();
	exitContextCheck();
	auto pop = scopeExit([this] { popContext(); });
	if (impl->ctx->encounteredError) {
		return nullptr;
	}
	return result;
}

// exitContextSlice (nodebuilder.go:93).
static Node* simplifyClassDeclaration(NodeFactory* f, Node* classDecl,
                                      Symbol* symbol);
static Node* simplifyModifiers(NodeFactory* f, Node* newDecl,
                               bool (*isDeclKind)(const Node*),
                               Symbol* symbol);

std::vector<Node*> NodeBuilder::exitContextSlice(std::vector<Node*> result) {
	propagateVerbosityOut();
	exitContextCheck();
	auto pop = scopeExit([this] { popContext(); });
	if (impl->ctx->encounteredError) {
		return {};
	}
	return result;
}

// exitContextCheck (nodebuilder.go:101).
void NodeBuilder::exitContextCheck() {
	if (impl->ctx->truncating &&
	    (impl->ctx->flags & nodebuilder::FlagsNoTruncation) != 0) {
		impl->ctx->tracker->ReportTruncationError();
	}
}

// indexInfoToIndexSignatureDeclaration — implements NodeBuilderInterface.
Node* NodeBuilder::IndexInfoToIndexSignatureDeclaration(
	IndexInfo* info, Node* enclosingDeclaration, nodebuilder::Flags flags,
	nodebuilder::InternalFlags internalFlags,
	nodebuilder::SymbolTracker* tracker) {
	enterContext(enclosingDeclaration, flags, internalFlags, tracker);
	return exitContext(
		impl->indexInfoToIndexSignatureDeclarationHelper(info, nullptr));
}

// serializeReturnTypeForSignature — implements NodeBuilderInterface.
Node* NodeBuilder::SerializeReturnTypeForSignature(
	Node* signatureDeclaration, Node* enclosingDeclaration,
	nodebuilder::Flags flags, nodebuilder::InternalFlags internalFlags,
	nodebuilder::SymbolTracker* tracker) {
	enterContext(enclosingDeclaration, flags, internalFlags, tracker);
	Signature* signature =
	    impl->ch->getSignatureFromDeclaration(signatureDeclaration);
	auto [_, cleanup] = impl->enterSignatureScope(signature);
	Node* result = impl->serializeReturnTypeForSignature(signature, true);
	cleanup();
	return exitContext(result);
}

// serializeTypeParametersForSignature — implements NodeBuilderInterface.
std::vector<Node*> NodeBuilder::SerializeTypeParametersForSignature(
	Node* signatureDeclaration, Node* enclosingDeclaration,
	nodebuilder::Flags flags, nodebuilder::InternalFlags internalFlags,
	nodebuilder::SymbolTracker* tracker) {
	enterContext(enclosingDeclaration, flags, internalFlags, tracker);
	Symbol* symbol = impl->ch->getSymbolOfDeclaration(signatureDeclaration);
	std::vector<Node*> typeParams = SymbolToTypeParameterDeclarations(
		symbol, enclosingDeclaration, flags, internalFlags, tracker);
	return exitContextSlice(typeParams);
}

// serializeTypeForDeclaration — implements NodeBuilderInterface.
Node* NodeBuilder::SerializeTypeForDeclaration(
	Node* declaration, Symbol* symbol, Node* enclosingDeclaration,
	nodebuilder::Flags flags, nodebuilder::InternalFlags internalFlags,
	nodebuilder::SymbolTracker* tracker) {
	enterContext(enclosingDeclaration, flags, internalFlags, tracker);
	return exitContext(
		impl->serializeTypeForDeclaration(declaration, nullptr, symbol, true));
}

// serializeTypeForExpression — implements NodeBuilderInterface.
Node* NodeBuilder::SerializeTypeForExpression(
	Node* expr, Node* enclosingDeclaration, nodebuilder::Flags flags,
	nodebuilder::InternalFlags internalFlags,
	nodebuilder::SymbolTracker* tracker) {
	enterContext(enclosingDeclaration, flags, internalFlags, tracker);
	return exitContext(impl->serializeTypeForExpression(expr));
}

// signatureToSignatureDeclaration — implements NodeBuilderInterface.
Node* NodeBuilder::SignatureToSignatureDeclaration(
	Signature* signature, Kind kind, Node* enclosingDeclaration,
	nodebuilder::Flags flags, nodebuilder::InternalFlags internalFlags,
	nodebuilder::SymbolTracker* tracker) {
	enterContext(enclosingDeclaration, flags, internalFlags, tracker);
	return exitContext(impl->signatureToSignatureDeclarationHelper(
		signature, kind, nullptr));
}

// expandSymbolForHover produces declaration nodes for a symbol with
// verbosity level support.
std::vector<Node*> NodeBuilder::ExpandSymbolForHover(Symbol* symbol,
                                                   SymbolFlags meaning) {
	enterContext(nullptr,
	             nodebuilder::FlagsIgnoreErrors |
	                 nodebuilder::FlagsMultilineObjectLiterals |
	                 nodebuilder::FlagsUseAliasDefinedOutsideCurrentScope,
	             nodebuilder::InternalFlagsNone, nullptr);

	// Push the declared type onto the type stack to prevent re-expansion.
	// We push a nil sentinel after the real type so that isTypeOnStack
	// (which skips the last element) still checks declaredType.
	Type* declaredType = impl->ch->getDeclaredTypeOfSymbol(symbol);
	impl->ctx->typeStack.push_back(declaredType);
	impl->ctx->typeStack.push_back(nullptr);

	std::vector<Node*> nodes = impl->expandSymbolForHover(symbol);

	impl->ctx->typeStack.resize(impl->ctx->typeStack.size() - 2);

	propagateVerbosityOut();

	// Simplify declarations by applying original modifiers
	std::vector<Node*> result;
	result.reserve(nodes.size());
	for (Node* node : nodes) {
		switch (node->kind) {
		case Kind::ClassDeclaration:
			result.push_back(
				simplifyClassDeclaration(impl->f, node, symbol));
			break;
		case Kind::EnumDeclaration:
			result.push_back(simplifyModifiers(impl->f, node,
			                                   isEnumDeclaration, symbol));
			break;
		case Kind::InterfaceDeclaration:
			if ((meaning & SymbolFlagsInterface) != 0) {
				result.push_back(simplifyModifiers(
					impl->f, node, isInterfaceDeclaration, symbol));
			}
			break;
		case Kind::ModuleDeclaration:
			result.push_back(simplifyModifiers(impl->f, node,
			                                   isModuleDeclaration, symbol));
			break;
		default:
			break;
		}
	}

	return exitContextSlice(result);
}

// simplifyClassDeclaration (nodebuilder.go:172).
static Node* simplifyClassDeclaration(NodeFactory* f, Node* classDecl,
                                      Symbol* symbol) {
	std::vector<Node*> classDeclarations =
	    filterVec(symbol->declarations, isClassLike);
	Node* originalClassDecl;
	if (!classDeclarations.empty()) {
		originalClassDecl = classDeclarations[0];
	} else {
		originalClassDecl = classDecl;
	}
	ModifierFlags modifiers = originalClassDecl->modifierFlags() &
	                          ~(ModifierFlagsExport | ModifierFlagsAmbient);
	bool isAnonymous = isClassExpression(originalClassDecl);
	if (isAnonymous) {
		auto* cd = classDecl->as<ClassDeclaration>();
		classDecl = f->updateClassDeclaration(
			cd, classDecl->modifiers(), nullptr, cd->TypeParameters,
			cd->HeritageClauses, cd->Members);
	}
	return replaceModifiers(
		*f, classDecl,
		f->newModifierList(createModifiersFromModifierFlags(
			modifiers,
			[](NodeFactory& factory, Kind k) {
				return factory.newToken(k);
			},
			*f)));
}

// simplifyModifiers (nodebuilder.go:196).
static Node* simplifyModifiers(NodeFactory* f, Node* newDecl,
                               bool (*isDeclKind)(const Node*),
                               Symbol* symbol) {
	std::vector<Node*> decls = filterVec(symbol->declarations, isDeclKind);
	Node* declWithModifiers;
	if (!decls.empty()) {
		declWithModifiers = decls[0];
	} else {
		declWithModifiers = newDecl;
	}
	ModifierFlags modifiers = declWithModifiers->modifierFlags() &
	                          ~(ModifierFlagsExport | ModifierFlagsAmbient);
	return replaceModifiers(
		*f, newDecl,
		f->newModifierList(createModifiersFromModifierFlags(
			modifiers,
			[](NodeFactory& factory, Kind k) {
				return factory.newToken(k);
			},
			*f)));
}

// symbolToEntityName — implements NodeBuilderInterface.
Node* NodeBuilder::SymbolToEntityName(
	Symbol* symbol, SymbolFlags meaning, Node* enclosingDeclaration,
	nodebuilder::Flags flags, nodebuilder::InternalFlags internalFlags,
	nodebuilder::SymbolTracker* tracker) {
	enterContext(enclosingDeclaration, flags, internalFlags, tracker);
	return exitContext(impl->symbolToName(symbol, meaning, false));
}

// symbolToExpression — implements NodeBuilderInterface.
Node* NodeBuilder::SymbolToExpression(
	Symbol* symbol, SymbolFlags meaning, Node* enclosingDeclaration,
	nodebuilder::Flags flags, nodebuilder::InternalFlags internalFlags,
	nodebuilder::SymbolTracker* tracker) {
	enterContext(enclosingDeclaration, flags, internalFlags, tracker);
	return exitContext(impl->symbolToExpression(symbol, meaning));
}

// symbolToNode — implements NodeBuilderInterface.
Node* NodeBuilder::SymbolToNode(Symbol* symbol, SymbolFlags meaning,
                                Node* enclosingDeclaration,
                                nodebuilder::Flags flags,
                                nodebuilder::InternalFlags internalFlags,
                                nodebuilder::SymbolTracker* tracker) {
	enterContext(enclosingDeclaration, flags, internalFlags, tracker);
	return exitContext(impl->symbolToNode(symbol, meaning));
}

// symbolToParameterDeclaration — implements NodeBuilderInterface.
Node* NodeBuilder::symbolToParameterDeclaration(
	Symbol* symbol, Node* enclosingDeclaration, nodebuilder::Flags flags,
	nodebuilder::InternalFlags internalFlags,
	nodebuilder::SymbolTracker* tracker) {
	enterContext(enclosingDeclaration, flags, internalFlags, tracker);
	return exitContext(impl->symbolToParameterDeclaration(symbol, false));
}

// symbolToTypeParameterDeclarations — implements NodeBuilderInterface.
std::vector<Node*> NodeBuilder::SymbolToTypeParameterDeclarations(
	Symbol* symbol, Node* enclosingDeclaration, nodebuilder::Flags flags,
	nodebuilder::InternalFlags internalFlags,
	nodebuilder::SymbolTracker* tracker) {
	enterContext(enclosingDeclaration, flags, internalFlags, tracker);
	return exitContextSlice(impl->symbolToTypeParameterDeclarations(symbol));
}

// typeParameterToDeclaration — implements NodeBuilderInterface.
Node* NodeBuilder::TypeParameterToDeclaration(
	Type* parameter, Node* enclosingDeclaration, nodebuilder::Flags flags,
	nodebuilder::InternalFlags internalFlags,
	nodebuilder::SymbolTracker* tracker) {
	enterContext(enclosingDeclaration, flags, internalFlags, tracker);
	return exitContext(impl->typeParameterToDeclaration(parameter));
}

// typePredicateToTypePredicateNode — implements NodeBuilderInterface.
Node* NodeBuilder::TypePredicateToTypePredicateNode(
	TypePredicate* predicate, Node* enclosingDeclaration,
	nodebuilder::Flags flags, nodebuilder::InternalFlags internalFlags,
	nodebuilder::SymbolTracker* tracker) {
	enterContext(enclosingDeclaration, flags, internalFlags, tracker);
	return exitContext(impl->typePredicateToTypePredicateNode(predicate));
}

// typeToTypeNode — implements NodeBuilderInterface.
Node* NodeBuilder::TypeToTypeNode(Type* typ, Node* enclosingDeclaration,
                                  nodebuilder::Flags flags,
                                  nodebuilder::InternalFlags internalFlags,
                                  nodebuilder::SymbolTracker* tracker) {
	enterContext(enclosingDeclaration, flags, internalFlags, tracker);
	return exitContext(impl->typeToTypeNode(typ));
}

// tryJSTypeNodeToTypeNode — implements NodeBuilderInterface.
Node* NodeBuilder::TryJSTypeNodeToTypeNode(
	Node* node, Node* enclosingDeclaration, nodebuilder::Flags flags,
	nodebuilder::InternalFlags internalFlags,
	nodebuilder::SymbolTracker* tracker) {
	enterContext(enclosingDeclaration, flags, internalFlags, tracker);
	return exitContext(impl->tryJSTypeNodeToTypeNode(node));
}

// newNodeBuilder (nodebuilder.go:280).
NodeBuilder* newNodeBuilder(Checker* ch, printer::EmitContext* e) {
	return newNodeBuilderEx(ch, e, nullptr /*idToSymbol*/);
}

// newNodeBuilderEx (nodebuilder.go:284).
NodeBuilder* newNodeBuilderEx(
	Checker* ch, printer::EmitContext* e,
	std::unordered_map<Node*, Symbol*>* idToSymbol) {
	NodeBuilderImpl* impl = newNodeBuilderImpl(ch, e, idToSymbol);
	auto* b = new NodeBuilder();
	b->impl = impl;
	b->ctxStack = {};
	b->ctxStack.reserve(1);
	b->host = ch->program;
	// Go: the builder dies when this transform's emit context is done (the
	// pooled context's Reset; getNodeBuilderEx contexts are GC-owned).
	// Register the delete on the context — reset() runs it after the
	// transform's nodes are printed.
	e->addCleanup([b] { delete b; });
	if (e->factory.arena().isTracked()) {
		// When the context's factory arena is tracked, the builder's
		// caches are extra GC roots for releaseArenas (nodebuilder.go:
		// the builder outlives each ReleaseArenas call in Go, so its
		// serializedTypes TypeNodes stay alive).
		e->addRootTracer([b](Arena& arena) { b->markEmitRoots(arena); });
	}
	return b;
}

// NewNodeBuilder (nodebuilder.go:279).
NodeBuilder* NewNodeBuilder(Checker* ch, printer::EmitContext* e) {
	return newNodeBuilderEx(ch, e, nullptr /*idToSymbol*/);
}

// NewNodeBuilderEx (nodebuilder.go:283).
NodeBuilder* NewNodeBuilderEx(Checker* ch, printer::EmitContext* e,
                              std::unordered_map<Node*, Symbol*>* idToSymbol) {
	return newNodeBuilderEx(ch, e, idToSymbol);
}

// getNodeBuilder (nodebuilder.go:291).
std::pair<NodeBuilder*, std::function<void()>> Checker::getNodeBuilder() {
	std::function<void()> releaseNodes = [this]() {
		// Go: EmitContext().Factory.ReleaseArenas() — drops arena slices so
		// the GC can reclaim nodes no longer referenced. The tracked factory
		// arena does the same: releaseArenas keeps nodes reachable from the
		// context maps and the builder's caches (serializedTypes) and frees
		// the rest.
		typeToStringNodebuilder->EmitContext()->releaseArenas();
	};
	if (typeToStringNodebuilder != nullptr) {
		return {typeToStringNodebuilder, releaseNodes};
	}
	typeToStringNodebuilder = getNodeBuilderEx(nullptr /*idToSymbol*/);
	return {typeToStringNodebuilder, releaseNodes};
}

// getNodeBuilderEx (nodebuilder.go:304).
NodeBuilder* Checker::getNodeBuilderEx(
	std::unordered_map<Node*, Symbol*>* idToSymbol) {
	NodeBuilder* b = newNodeBuilderEx(this,
	                                new printer::EmitContext(
	                                    /*trackFactoryArena*/ true),
	                                idToSymbol);
	return b;
}

// (deduped: all dep-stubs here — real defs landed in symbolaccess.cpp,
// services2.cpp, checker_relater.cpp, checker_utilities.cpp,
// modulespecifiers/specifiers.cpp)

} // namespace tsc::checker
