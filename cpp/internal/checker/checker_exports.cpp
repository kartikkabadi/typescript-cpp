// checker_exports.cpp — port of tsc/internal/checker/exports.go: the exported
// API surface (capitalized getters/wrappers over lowercase checker internals,
// plus a few exported free functions).

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "internal/checker/checker.h"
#include "internal/checker/types.h"

namespace tsc::checker {

// Cross-TU free function with its canonical def in checker_decltypes.cpp (no
// header decl).
ModifierFlags getDeclarationModifierFlagsFromSymbol(Symbol* s);  // utilities.go:757

namespace {

// ---------------------------------------------------------------------------
// checker.go / utilities.go file-local replicas (per PORTING.md these helpers
// are tiny and copied per translation unit rather than shared).
// ---------------------------------------------------------------------------

// checker.go:23946 — isTupleType
bool isTupleType(Type* t) {
	return (t->objectFlags & ObjectFlagsReference) != 0 &&
		   (t->Target()->objectFlags & ObjectFlagsTuple) != 0;
}

// utilities.go:922 — isTypeUsableAsPropertyName
bool isTypeUsableAsPropertyName(Type* t) {
	return (t->flags & TypeFlagsStringOrNumberLiteralOrUnique) != 0;
}

// utilities.go:929 — gets the symbolic name for a member from its type.
std::string getPropertyNameFromType(Type* t) {
	if ((t->flags & TypeFlagsStringLiteral) != 0) {
		return std::get<std::string>(t->AsLiteralType()->value);
	}
	if ((t->flags & TypeFlagsNumberLiteral) != 0) {
		return std::get<Number>(t->AsLiteralType()->value).string();
	}
	if ((t->flags & TypeFlagsUniqueESSymbol) != 0) {
		return t->AsUniqueESSymbolType()->name;
	}
	TSC_UNREACHABLE("Unhandled case in getPropertyNameFromType");
}

}  // namespace

// ---------------------------------------------------------------------------
// exports.go
// ---------------------------------------------------------------------------

Type* Checker::GetStringType() {
	return stringType;
}

Type* Checker::GetNumberType() {
	return numberType;
}

Type* Checker::GetBooleanType() {
	return booleanType;
}

Type* Checker::GetVoidType() {
	return voidType;
}

Type* Checker::GetUndefinedType() {
	return undefinedType;
}

Type* Checker::GetNullType() {
	return nullType;
}

Type* Checker::GetAnyType() {
	return anyType;
}

Type* Checker::GetErrorType() {
	return errorType;
}

Type* Checker::GetNeverType() {
	return neverType;
}

Type* Checker::GetUnknownType() {
	return unknownType;
}

Type* Checker::GetBigIntType() {
	return bigintType;
}

Type* Checker::GetESSymbolType() {
	return esSymbolType;
}

Type* Checker::GetNonPrimitiveType() {
	return nonPrimitiveType;
}

Type* Checker::GetBaseTypeOfLiteralType(Type* t) {
	return getBaseTypeOfLiteralType(t);
}

Symbol* Checker::GetUnknownSymbol() {
	return unknownSymbol;
}

Symbol* Checker::GetUndefinedSymbol() {
	return undefinedSymbol;
}

Symbol* Checker::GetArgumentsSymbol() {
	return argumentsSymbol;
}

Signature* Checker::GetUnknownSignature() {
	return unknownSignature;
}

Type* Checker::GetUnionType(std::vector<Type*> types) {
	return getUnionType(types);
}

Type* Checker::GetNameTypeOfSymbol(Symbol* symbol) {
	if (ValueSymbolLinks* links = valueSymbolLinks.TryGet(symbol);
		links != nullptr) {
		return links->nameType;
	}
	return nullptr;
}

bool IsTypeUsableAsPropertyName(Type* t) {
	return isTypeUsableAsPropertyName(t);
}

std::string GetPropertyNameFromType(Type* t) {
	return getPropertyNameFromType(t);
}

Symbol* Checker::GetGlobalSymbol(const std::string& name, SymbolFlags meaning,
								 const DiagnosticMessage* diagnostic) {
	return getGlobalSymbol(name, meaning, diagnostic);
}

Symbol* Checker::GetMergedSymbol(Symbol* symbol) {
	return getMergedSymbol(symbol);
}

Symbol* Checker::TryFindAmbientModule(const std::string& moduleName) {
	return tryFindAmbientModule(moduleName, true /* withAugmentations */);
}

Symbol* Checker::GetImmediateAliasedSymbol(Symbol* symbol) {
	return getImmediateAliasedSymbol(symbol);
}

Symbol* Checker::GetTargetSymbol(Symbol* symbol) {
	return getTargetSymbol(symbol);
}

Node* Checker::GetTypeOnlyAliasDeclaration(Symbol* symbol) {
	return getTypeOnlyAliasDeclaration(symbol);
}

Symbol* Checker::ResolveExternalModuleName(Node* moduleSpecifier,
										   Type* importAttributesType) {
	return resolveExternalModuleName(moduleSpecifier, moduleSpecifier,
									 true /*ignoreErrors*/,
									 importAttributesType);
}

Symbol* Checker::ResolveExternalModuleSymbol(Symbol* moduleSymbol) {
	return resolveExternalModuleSymbol(moduleSymbol, false /*dontResolveAlias*/);
}

Type* Checker::GetTypeFromTypeNode(Node* node) {
	return getTypeFromTypeNode(node);
}

bool Checker::IsArrayLikeType(Type* t) {
	return isArrayLikeType(t);
}

std::vector<Symbol*> Checker::GetPropertiesOfType(Type* t) {
	return getPropertiesOfType(t);
}

Symbol* Checker::GetPropertyOfType(Type* t, const std::string& name) {
	return getPropertyOfType(t, name);
}

bool Checker::TypeHasCallOrConstructSignatures(Type* t) {
	return typeHasCallOrConstructSignatures(t);
}

// Checks if a property can be accessed in a location.
// The location is given by the `node` parameter.
// The node does not need to be a property access.
// @param node location where to check property accessibility
// @param isSuper whether to consider this a `super` property access, e.g.
// `super.foo`.
// @param isWrite whether this is a write access, e.g. `++foo.x`.
// @param containingType type where the property comes from.
// @param property property symbol.
bool Checker::IsPropertyAccessible(Node* node, bool isSuper, bool isWrite,
								   Type* containingType, Symbol* property) {
	return isPropertyAccessible(node, isSuper, isWrite, containingType,
								property);
}

Type* Checker::GetTypeOfPropertyOfContextualType(Type* t,
												 const std::string& name) {
	return getTypeOfPropertyOfContextualType(t, name);
}

ModifierFlags GetDeclarationModifierFlagsFromSymbol(Symbol* s) {
	return getDeclarationModifierFlagsFromSymbol(s);
}

bool Checker::WasCanceled() {
	return wasCanceled;
}

std::vector<Signature*> Checker::GetSignaturesOfType(Type* t,
												   SignatureKind kind) {
	return getSignaturesOfType(t, kind);
}

Type* Checker::GetDeclaredTypeOfSymbol(Symbol* symbol) {
	return getDeclaredTypeOfSymbol(symbol);
}

Type* Checker::GetTypeOfSymbol(Symbol* symbol) {
	return getTypeOfSymbol(symbol);
}

Type* Checker::GetNonMissingTypeOfSymbol(Symbol* symbol) {
	return getNonMissingTypeOfSymbol(symbol);
}

Type* Checker::GetConstraintOfTypeParameter(Type* typeParameter) {
	return getConstraintOfTypeParameter(typeParameter);
}

Type* Checker::GetTrueTypeOfConditionalType(Type* t) {
	return getTrueTypeFromConditionalType(t);
}

Type* Checker::GetFalseTypeOfConditionalType(Type* t) {
	return getFalseTypeFromConditionalType(t);
}

Type* Checker::GetDefaultFromTypeParameter(Type* typeParameter) {
	return getDefaultFromTypeParameter(typeParameter);
}

ModifierFlags Checker::GetEffectiveDeclarationFlags(Node* n,
												  ModifierFlags flagsToCheck) {
	return getEffectiveDeclarationFlags(n, flagsToCheck);
}

Type* Checker::GetBaseConstraintOfType(Type* t) {
	return getBaseConstraintOfType(t);
}

TypePredicate* Checker::GetTypePredicateOfSignature(Signature* sig) {
	return getTypePredicateOfSignature(sig);
}

bool IsTupleType(Type* t) {
	return isTupleType(t);
}

bool IsTupleTypeTarget(Type* t) {
	return isTupleType(t) && t->Target() == t;
}

bool Checker::IsArrayType(Type* t) {
	return isArrayType(t);
}

bool Checker::IsReadonlySymbol(Symbol* symbol) {
	return isReadonlySymbol(symbol);
}

Type* Checker::GetReturnTypeOfSignature(Signature* sig) {
	return getReturnTypeOfSignature(sig);
}

bool Checker::HasEffectiveRestParameter(Signature* signature) {
	return hasEffectiveRestParameter(signature);
}

std::vector<Type*>
Checker::GetLocalTypeParametersOfClassOrInterfaceOrTypeAlias(Symbol* symbol) {
	return getLocalTypeParametersOfClassOrInterfaceOrTypeAlias(symbol);
}

Type* Checker::GetContextualTypeForObjectLiteralElement(
	Node* element, ContextFlags contextFlags) {
	return getContextualTypeForObjectLiteralElement(element, contextFlags);
}

std::string Checker::TypePredicateToString(TypePredicate* t) {
	return typePredicateToString(t);
}

std::vector<std::vector<Symbol*>>
Checker::GetExpandedParameters(Signature* signature, bool skipUnionExpanding) {
	return getExpandedParameters(signature, skipUnionExpanding);
}

Signature* Checker::GetResolvedSignature(Node* node) {
	return getResolvedSignature(node, nullptr, CheckModeNormal);
}

// Return the type of the given property in the given type, or nil if no such
// property exists
Type* Checker::GetTypeOfPropertyOfType(Type* t, const std::string& name) {
	return getTypeOfPropertyOfType(t, name);
}

Type* Checker::GetContextualTypeForArgumentAtIndex(Node* node, int argIndex) {
	return getContextualTypeForArgumentAtIndex(node, argIndex);
}

Type* Checker::GetAwaitedType(Type* t) {
	return getAwaitedType(t);
}

std::vector<Node*> Checker::GetIndexSignaturesAtLocation(Node* node) {
	return getIndexSignaturesAtLocation(node);
}

Symbol* Checker::GetResolvedSymbol(Node* node) {
	return getResolvedSymbol(node);
}

std::string Checker::GetJsxNamespace(Node* location) {
	return getJsxNamespace(location);
}

std::string Checker::GetJsxFragmentFactory(Node* location) {
	Node* entity = getJsxFragmentFactoryEntity(location);
	if (entity != nullptr) {
		return getFirstIdentifier(entity)->text();
	}
	return "";
}

Symbol* Checker::ResolveName(const std::string& name, Node* location,
							 SymbolFlags meaning, bool excludeGlobals) {
	return resolveName(location, name, meaning, nullptr, true, excludeGlobals);
}

SymbolFlags Checker::GetSymbolFlags(Symbol* symbol) {
	return getSymbolFlags(symbol);
}

std::vector<Type*> Checker::GetBaseTypes(Type* t) {
	return getBaseTypes(t);
}

Type* Checker::GetApparentType(Type* t) {
	return getApparentType(t);
}

Type* Checker::GetReducedType(Type* t) {
	return getReducedType(t);
}

// GetFullyQualifiedName returns the fully qualified name of a symbol, walking
// up its parent chain (e.g. `"/path/to/module".Namespace.Name`).
std::string Checker::GetFullyQualifiedName(Symbol* symbol) {
	return getFullyQualifiedName(symbol, nullptr /*containingLocation*/);
}

Type* Checker::GetBaseConstructorTypeOfClass(Type* t) {
	return getBaseConstructorTypeOfClass(t);
}

MemberOverrideStatus
Checker::GetMemberOverrideModifierStatus(Node* node, Node* member,
										 Symbol* memberSymbol) {
	return getMemberOverrideModifierStatus(node, member, memberSymbol);
}

Type* Checker::GetRestTypeOfSignature(Signature* sig) {
	return getRestTypeOfSignature(sig);
}

std::vector<Type*> Checker::GetTypeArguments(Type* t) {
	return getTypeArguments(t);
}

IndexInfo* Checker::GetIndexInfoOfType(Type* t, Type* keyType) {
	return getIndexInfoOfType(t, keyType);
}

Type* Checker::GetIndexTypeOfType(Type* t, Type* keyType) {
	return getIndexTypeOfType(t, keyType);
}

std::vector<IndexInfo*> Checker::GetIndexInfosOfType(Type* t) {
	return getIndexInfosOfType(t);
}

bool Checker::IsContextSensitive(Node* node) {
	return isContextSensitive(node);
}

std::vector<Type*> Checker::FillMissingTypeArguments(
	std::vector<Type*> typeArguments, std::vector<Type*> typeParameters,
	int minTypeArgumentCount, bool isJavaScriptImplicitAny) {
	return fillMissingTypeArguments(typeArguments, typeParameters,
									minTypeArgumentCount,
									isJavaScriptImplicitAny);
}

int Checker::GetMinTypeArgumentCount(std::vector<Type*> typeParameters) {
	return getMinTypeArgumentCount(typeParameters);
}

Type* Checker::GetWidenedLiteralType(Type* t) {
	return getWidenedLiteralType(t);
}

bool Checker::IsTypeAssignableTo(Type* source, Type* target) {
	return isTypeAssignableTo(source, target);
}

Type* Checker::GetUnionTypeEx(std::vector<Type*> types,
							  UnionReduction unionReduction) {
	return getUnionTypeEx(types, unionReduction, nullptr, nullptr);
}

bool Checker::RequiresAddingImplicitUndefined(Node* node) {
	Node* enclosingDeclaration = findAncestor(node, isDeclaration);
	if (enclosingDeclaration == nullptr) {
		enclosingDeclaration = getSourceFileOfNode(node);
	}
	Symbol* symbol = node->symbol();
	if (symbol == nullptr) {
		return false;
	}
	return requiresAddingImplicitUndefined(
		node, symbol, enclosingDeclaration);
}

Type* Checker::RemoveMissingOrUndefinedType(Type* t) {
	return removeMissingOrUndefinedType(t);
}

Type* Checker::GetWidenedType(Type* t) {
	return getWidenedType(t);
}

int Checker::CompareSymbols(Symbol* s1, Symbol* s2) {
	return compareSymbols(s1, s2);
}

bool IsDistributedTypeParameter(Type* t) {
	return (t->flags & TypeFlagsTypeParameter) != 0 &&
		   t->AsTypeParameter()->isDistributed;
}

// ---------------------------------------------------------------------------
// Dep stubs — removed when the owner slice lands.
// ---------------------------------------------------------------------------


}  // namespace tsc::checker
