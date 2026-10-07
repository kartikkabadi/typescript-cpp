// Port of tsc/internal/checker/checker.go:5082-7499 — the "declchecks2" slice:
// interface/enum/module/import/export declaration checks, variable-like
// declaration checks, iteration-type machinery, alias-symbol checks,
// merged-declaration checks, and the unused-identifier reporters.
//
// Callees owned by slices that have not landed are declared in
// `// === slice: declchecks2 ===` in checker.h and stubbed once at the bottom
// of this file with `TSC_UNREACHABLE("<name> — <slice> dep")`; each stub is
// deleted when its owner's real definition merges.

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/binder/binder.h"
#include "internal/checker/checker.h"
#include "internal/checker/mapper.h"
#include "internal/checker/types.h"
#include "internal/scanner/scanner.h"
#include "internal/tracing/tracing.h"
#include "internal/tspath/tspath.h"

namespace tsc::checker {

namespace {

// ---------------------------------------------------------------------------
// core.* helpers — per-translation-unit copies (PORTING.md)
// ---------------------------------------------------------------------------

// core.OrElse — returns the first non-nil value.
template <class T>
T orElse(T a, T b) {
	return a != nullptr ? a : b;
}

// core.Find
template <class R, class Pred>
auto find(R&& v, Pred pred) -> std::decay_t<std::ranges::range_value_t<R>> {
	for (auto e : v) {
		if (pred(e)) {
			return e;
		}
	}
	return nullptr;
}

// core.Some
template <class R, class Pred>
bool some(R&& v, Pred pred) {
	for (auto e : v) {
		if (pred(e)) {
			return true;
		}
	}
	return false;
}

// core.Every
template <class R, class Pred>
bool every(R&& v, Pred pred) {
	for (auto e : v) {
		if (!pred(e)) {
			return false;
		}
	}
	return true;
}

// core.Map
template <class T, class F>
auto mapVec(const std::vector<T>& v, F f) {
	using R = decltype(f(v.front()));
	std::vector<R> out;
	out.reserve(v.size());
	for (const T& e : v) {
		out.push_back(f(e));
	}
	return out;
}

// core.MapNonNil — maps and drops nil results.
template <class T, class F>
auto mapNonNil(const std::vector<T>& v, F f) {
	using R = decltype(f(v.front()));
	std::vector<R> out;
	for (const T& e : v) {
		if (R r = f(e); r != nullptr) {
			out.push_back(r);
		}
	}
	return out;
}

// core.Filter
template <class T, class Pred>
std::vector<T> filter(const std::vector<T>& v, Pred pred) {
	std::vector<T> out;
	for (const T& e : v) {
		if (pred(e)) {
			out.push_back(e);
		}
	}
	return out;
}

// core.CountWhere
template <class T, class Pred>
int countWhere(const std::vector<T>& v, Pred pred) {
	int count = 0;
	for (const T& e : v) {
		if (pred(e)) {
			count++;
		}
	}
	return count;
}

// core.Same — same elements in the same order.
template <class T>
bool same(const std::vector<T>& a, const std::vector<T>& b) {
	return a == b;
}

// core.LastOrNil
template <class R>
auto lastOrNil(R&& v) -> std::decay_t<decltype(v.back())> {
	return v.empty() ? nullptr : static_cast<Node*>(v.back());
}

// ---------------------------------------------------------------------------
// File-local helpers — faithful ports of free functions whose home files are
// not ported yet, or that the port keeps file-local per translation unit.
// ---------------------------------------------------------------------------

// utilities.go:245 — isTopLevelInExternalModuleAugmentation
bool isTopLevelInExternalModuleAugmentation(Node* node) {
	return node != nullptr && node->parent != nullptr && isModuleBlock(node->parent) &&
		isExternalModuleAugmentation(node->parent->parent);
}

// (deduped: local replica of hasExportAssignmentSymbol removed)

// utilities.go:272 — hasDotDotDotToken
bool hasDotDotDotToken(Node* node) {
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

// (deduped: local replica of isTypeAny removed)

// utilities.go:298 — isOptionalDeclaration
bool isOptionalDeclaration(Node* declaration) {
	return hasQuestionToken(declaration);
}

// utilities.go:63 — getSelectedModifierFlags
ModifierFlags getSelectedModifierFlags(Node* node, ModifierFlags flags) {
	return static_cast<ModifierFlags>(node->modifierFlags() & flags);
}

// utilities.go:922 — isTypeUsableAsPropertyName
bool isTypeUsableAsPropertyName(Type* t) {
	return (t->flags & TypeFlagsStringOrNumberLiteralOrUnique) != 0;
}

// utilities.go:926 — getPropertyNameFromType
std::string getPropertyNameFromType(Type* t) {
	if (t->flags & TypeFlagsStringLiteral) {
		return std::get<std::string>(t->AsLiteralType()->value);
	}
	if (t->flags & TypeFlagsNumberLiteral) {
		return std::get<Number>(t->AsLiteralType()->value).string();
	}
	if (t->flags & TypeFlagsUniqueESSymbol) {
		return t->AsUniqueESSymbolType()->name;
	}
	TSC_UNREACHABLE("Unhandled case in getPropertyNameFromType");
}

// checker/utilities.go:1298 — getEnclosingContainer
Node* getEnclosingContainer(Node* node) {
	return findAncestor(node->parent, [](Node* n) -> bool {
		return (getContainerFlags(n) & ContainerFlagsIsContainer) != 0;
	});
}

// ast/utilities.go:2639 — getDeclarationContainer
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

// ast/utilities.go:279 — isPropertyName is only used outside this slice
// (checker.go:27825); its owner TU defines the file-local copy it needs.

// ast/utilities.go:1286 — walkUpBindingElementsAndPatterns
Node* walkUpBindingElementsAndPatterns(Node* node) {
	node = node->parent;
	while (isBindingElement(node->parent)) {
		node = node->parent->parent;
	}
	return node->parent;
}

// ast/utilities.go:2510 — isInternalModuleImportEqualsDeclaration
bool isInternalModuleImportEqualsDeclaration(Node* node) {
	return node->kind == Kind::ImportEqualsDeclaration &&
		node->as<ImportEqualsDeclaration>()->ModuleReference->kind != Kind::ExternalModuleReference;
}

// ast/utilities.go:3077 — isVariableLike
bool isVariableLike(Node* node) {
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

// ast/utilities.go:4115 — isImportOrImportEqualsDeclaration
bool isImportOrImportEqualsDeclaration(Node* node) {
	return isImportDeclaration(node) || isImportEqualsDeclaration(node);
}

// (deduped: local replica of rangeOfTypeParameters removed)

// (deduped: local replica of allDeclarationsInSameSourceFile removed)

// nodebuilderimpl.go:1193 — TryGetModuleSpecifierFromDeclaration
Node* tryGetModuleSpecifierFromDeclarationWorker(Node* node);
Node* TryGetModuleSpecifierFromDeclaration(Node* node) {
	Node* res = tryGetModuleSpecifierFromDeclarationWorker(node);
	if (res == nullptr || !isStringLiteral(res)) {
		return nullptr;
	}
	return res;
}

Node* tryGetModuleSpecifierFromDeclarationWorker(Node* node) {
	switch (node->kind) {
	case Kind::VariableDeclaration:
	case Kind::BindingElement: {
		Node* moduleCall = findAncestor(node->initializer(), [](Node* node) -> bool {
			return isRequireCall(node, true /*requireStringLiteralLikeArgument*/) || isImportCall(node);
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
			return node->as<ImportTypeNode>()->Argument->as<LiteralTypeNode>()->Literal;
		}
		return nullptr;
	default:
		TSC_UNREACHABLE("Unhandled case in tryGetModuleSpecifierFromDeclarationWorker");
	}
}

// core.ModuleKind.String() — modulekind_stringer_generated.go
std::string moduleKindString(ModuleKind k) {
	switch (k) {
	case ModuleKind::None:
		return "None";
	case ModuleKind::CommonJS:
		return "CommonJS";
	case ModuleKind::AMD:
		return "AMD";
	case ModuleKind::UMD:
		return "UMD";
	case ModuleKind::System:
		return "System";
	case ModuleKind::ES2015:
		return "ES2015";
	case ModuleKind::ES2020:
		return "ES2020";
	case ModuleKind::ES2022:
		return "ES2022";
	case ModuleKind::ESNext:
		return "ESNext";
	case ModuleKind::Node16:
		return "Node16";
	case ModuleKind::Node18:
		return "Node18";
	case ModuleKind::Node20:
		return "Node20";
	case ModuleKind::NodeNext:
		return "NodeNext";
	case ModuleKind::Preserve:
		return "Preserve";
	}
	return "ModuleKind(" + std::to_string(static_cast<int32_t>(k)) + ")";
}

// intrinsicTypeKinds — checker.go:4165-ish (checker.cpp keeps a copy with
// internal linkage; this TU needs its own).
const std::unordered_map<std::string, IntrinsicTypeKind> intrinsicTypeKinds = {
	{"Uppercase", IntrinsicTypeKind::Uppercase},
	{"Lowercase", IntrinsicTypeKind::Lowercase},
	{"Capitalize", IntrinsicTypeKind::Capitalize},
	{"Uncapitalize", IntrinsicTypeKind::Uncapitalize},
	{"NoInfer", IntrinsicTypeKind::NoInfer},
};

// Type.Mapper() — no accessor on Type; dispatch locally like
// checker_instantiate.cpp does.
TypeMapper* typeMapperOf(Type* t) {
	if (t->flags & TypeFlagsObject) {
		return t->AsObjectType()->mapper;
	}
	if (t->flags & TypeFlagsTypeParameter) {
		return t->AsTypeParameter()->mapper;
	}
	if (t->flags & TypeFlagsConditional) {
		return t->AsConditionalType()->mapper;
	}
	TSC_UNREACHABLE("Unhandled case in Type.Mapper");
}

// InheritanceInfo — checker.go:5123
struct InheritanceInfo {
	Symbol* prop{};
	Type* containingType{};
};

// IterationTypes helpers — checker.go:6574-6587 (Go methods on *IterationTypes;
// IterationTypes is a plain struct here, so they are file-local functions).
bool iterationTypesHasTypes(const IterationTypes& iterationTypes) {
	return iterationTypes.yieldType != nullptr || iterationTypes.returnType != nullptr ||
		iterationTypes.nextType != nullptr;
}

Type* iterationTypesGetType(const IterationTypes& iterationTypes, IterationTypeKind typeKind) {
	switch (typeKind) {
	case IterationTypeKind::Yield:
		return iterationTypes.yieldType;
	case IterationTypeKind::Return:
		return iterationTypes.returnType;
	case IterationTypeKind::Next:
		return iterationTypes.nextType;
	}
	TSC_UNREACHABLE("Unhandled case in getType(IterationTypeKind)");
}

// IterationTypesResolver::getResolvedIterationTypes — checker.go:6554
IterationTypes getResolvedIterationTypes(IterationTypesResolver* r, Type* yieldType,
                                         Type* returnType, Type* nextType) {
	return IterationTypes{
		orElse(r->resolveIterationType(yieldType, nullptr /*errorNode*/), yieldType),
		orElse(r->resolveIterationType(returnType, nullptr /*errorNode*/), returnType),
		nextType,
	};
}

// checker.go:5530 — hasTypeJsonImportAttribute
bool hasTypeJsonImportAttribute(Node* node) {
	Node* attributes = node->as<ImportDeclaration>()->Attributes;
	return attributes != nullptr && some(attributes->as<ImportAttributes>()->Attributes->nodes, [](Node* attr) {
		return attr->name()->text() == "type" && isStringLiteralLike(attr->as<ImportAttribute>()->Value) &&
			attr->as<ImportAttribute>()->Value->text() == "json";
	});
}

// checker.go:5921 — isNotOverload
bool isNotOverload(Node* node) {
	return (!isFunctionDeclaration(node) && !isMethodDeclaration(node)) || node->body() != nullptr;
}

// checker.go:6898 — isES2015OrLaterIterable
bool isES2015OrLaterIterable(const std::string& n) {
	if (n == "Float32Array" || n == "Float64Array" || n == "Int16Array" || n == "Int32Array" ||
		n == "Int8Array" || n == "NodeList" || n == "Uint16Array" || n == "Uint32Array" ||
		n == "Uint8Array" || n == "Uint8ClampedArray") {
		return true;
	}
	return false;
}

// checker.go:7444 — isIdentifierThatStartsWithUnderscore
bool isIdentifierThatStartsWithUnderscore(Node* node) {
	return isIdentifier(node) && !node->text().empty() && node->text()[0] == '_';
}

// checker.go:7448 — importClauseFromImported
Node* importClauseFromImported(Node* node) {
	switch (node->kind) {
	case Kind::ImportClause:
		return node;
	case Kind::NamespaceImport:
		return node->parent;
	default:
		return node->parent->parent;
	}
}

// checker.go:5357 — forward decl; definition follows checkModuleDeclaration below
Node* getFirstNonAmbientClassOrFunctionDeclaration(Symbol* symbol);

}  // namespace

// ---------------------------------------------------------------------------
// checker.go:5082-5126 — checkInterfaceDeclaration
// ---------------------------------------------------------------------------

void Checker::checkInterfaceDeclaration(Node* node) {
	if (!checkGrammarModifiers(node)) {
		checkGrammarInterfaceDeclaration(node->as<InterfaceDeclaration>());
	}
	if (!containerAllowsBlockScopedVariable(node->parent)) {
		grammarErrorOnNode(node, X_0_declarations_can_only_be_declared_inside_a_block, {"interface"});
	}
	checkTypeParameters(node->typeParameters());
	checkTypeNameIsReserved(node->name(), Interface_name_cannot_be_0);
	checkExportsOnMergedDeclarations(node);
	Symbol* symbol = getSymbolOfDeclaration(node);
	checkTypeParameterListsIdentical(symbol);
	// Only check this symbol once (per check file — see staleForCheckFile:
	// the checks below report errors on declarations that may live in other
	// files when the symbol is merged, so each file's check must re-run them).
	if (DeclaredTypeLinks* links = declaredTypeLinks.Get(symbol);
		!links->interfaceChecked || staleForCheckFile(links->interfaceCheckedFor)) {
		links->interfaceChecked = true;
		links->interfaceCheckedFor = checkFileTag();
		Type* t = getDeclaredTypeOfSymbol(symbol);
		Type* typeWithThis = getTypeWithThisArgument(t, nullptr, false);
		// run subsequent checks only if first set succeeded
		if (checkInheritedPropertiesAreIdentical(t, node->name())) {
			for (Type* baseType : getBaseTypes(t)) {
				checkTypeAssignableTo(typeWithThis,
					getTypeWithThisArgument(baseType, t->AsInterfaceType()->thisType, false),
					node->name(), Interface_0_incorrectly_extends_interface_1);
			}
			checkIndexConstraints(t, symbol, false /*isStaticIndex*/);
		}
	}
	checkObjectTypeForDuplicateDeclarations(node, false /*checkPrivateNames*/);
	for (Node* heritageElement : getExtendsHeritageClauseElements(node)) {
		if (isExpressionWithTypeArguments(heritageElement)) {
			Node* expr = heritageElement->expression();
			if (!isEntityNameExpression(expr) || isOptionalChain(expr)) {
				error(expr, An_interface_can_only_extend_an_identifier_Slashqualified_name_with_optional_type_arguments);
			}
		}
		checkTypeReferenceNode(heritageElement);
	}
	checkSourceElements(node->members());
	checkClassOrInterfaceForDuplicateIndexSignatures(node);
	registerForUnusedIdentifiersCheck(node);
}

// checker.go:5127 — checkInheritedPropertiesAreIdentical
bool Checker::checkInheritedPropertiesAreIdentical(Type* t, Node* typeNode) {
	std::vector<Type*> baseTypes = getBaseTypes(t);
	if (baseTypes.size() < 2) {
		return true;
	}
	std::unordered_map<std::string, InheritanceInfo> seen;
	for (auto& entry : resolveDeclaredMembers(t)->declaredMembers) {
		const std::string& id = entry.first;
		Symbol* p = entry.second;
		if (isNamedMember(p, id)) {
			seen[p->name] = InheritanceInfo{p, t};
		}
	}
	bool identical = true;
	for (Type* base : baseTypes) {
		std::vector<Symbol*> properties =
			getPropertiesOfType(getTypeWithThisArgument(base, t->AsInterfaceType()->thisType, false));
		for (Symbol* prop : properties) {
			auto it = seen.find(prop->name);
			if (it == seen.end()) {
				seen[prop->name] = InheritanceInfo{prop, base};
			} else {
				InheritanceInfo& existing = it->second;
				bool isInheritedProperty = existing.containingType != t;
				if (isInheritedProperty && !isPropertyIdenticalTo(existing.prop, prop)) {
					identical = false;
					std::string typeName1 = TypeToString(existing.containingType);
					std::string typeName2 = TypeToString(base);
					Diagnostic* errorInfo = NewDiagnosticForNode(
						typeNode, Named_property_0_of_types_1_and_2_are_not_identical,
						{symbolToString(prop), typeName1, typeName2});
					addDiagnostic(newDiagnosticChain(
						errorInfo, Interface_0_cannot_simultaneously_extend_types_1_and_2,
						{TypeToString(t), typeName1, typeName2}));
				}
			}
		}
	}
	return identical;
}

// checker.go:5159 — isPropertyIdenticalTo
bool Checker::isPropertyIdenticalTo(Symbol* sourceProp, Symbol* targetProp) {
	return compareProperties(sourceProp, targetProp,
		[this](Type* s, Type* t) { return compareTypesIdentical(s, t); }) != Ternary::False;
}

// checker.go:5163 — checkEnumDeclaration
void Checker::checkEnumDeclaration(Node* node) {
	checkGrammarModifiers(node);
	checkCollisionsForDeclarationName(node, node->name());
	checkExportsOnMergedDeclarations(node);
	checkSourceElements(node->members());

	if (shouldCheckErasableSyntax(node) && (node->flags & NodeFlagsAmbient) == 0) {
		error(node, This_syntax_is_not_allowed_when_erasableSyntaxOnly_is_enabled);
	}

	computeEnumMemberValues(node);
	// Spec 2014 - Section 9.3:
	// It isn't possible for one enum declaration to continue the automatic numbering sequence of another,
	// and when an enum type has multiple declarations, only one declaration is permitted to omit a value
	// for the first member.
	//
	// Only perform this check once per symbol
	Symbol* enumSymbol = getSymbolOfDeclaration(node);
	if (DeclaredTypeLinks* links = declaredTypeLinks.Get(enumSymbol);
		!links->enumChecked || staleForCheckFile(links->enumCheckedFor)) {
		links->enumChecked = true;
		links->enumCheckedFor = checkFileTag();
		if (enumSymbol->declarations.size() > 1) {
			bool enumIsConst = isEnumConst(node);
			// check that const is placed\omitted on all enum declarations
			for (Node* decl : enumSymbol->declarations) {
				if (isEnumDeclaration(decl) && isEnumConst(decl) != enumIsConst) {
					error(getNameOfDeclaration(decl), Enum_declarations_must_all_be_const_or_non_const);
				}
			}
		}
		bool seenEnumMissingInitialInitializer = false;
		for (Node* declaration : enumSymbol->declarations) {
			// return true if we hit a violation of the rule, false otherwise
			if (declaration->kind != Kind::EnumDeclaration) {
				continue;
			}
			auto members = declaration->members();
			if (members.empty()) {
				continue;
			}
			Node* firstEnumMember = members[0];
			if (firstEnumMember->initializer() == nullptr) {
				if (seenEnumMissingInitialInitializer) {
					error(firstEnumMember->name(), In_an_enum_with_multiple_declarations_only_one_declaration_can_omit_an_initializer_for_its_first_enum_element);
				} else {
					seenEnumMissingInitialInitializer = true;
				}
			}
		}
	}
}

// checker.go:5214 — checkEnumMember
void Checker::checkEnumMember(Node* node) {
	if (isPrivateIdentifier(node->name())) {
		error(node, An_enum_member_cannot_be_named_with_a_private_identifier);
	}
	if (node->initializer() != nullptr) {
		checkExpression(node->initializer());
	}
}

// checker.go:5223 — checkModuleDeclaration
void Checker::checkModuleDeclaration(Node* node) {
	if (Node* body = node->body(); body != nullptr) {
		checkSourceElement(body);
		if (!isGlobalScopeAugmentation(node)) {
			registerForUnusedIdentifiersCheck(node);
		}
	}
	bool isGlobalAugmentation = isGlobalScopeAugmentation(node);
	bool inAmbientContext = (node->flags & NodeFlagsAmbient) != 0;
	if (isGlobalAugmentation && !inAmbientContext) {
		error(node->name(), Augmentations_for_the_global_scope_should_have_declare_modifier_unless_they_appear_in_already_ambient_context);
	}
	Node* attributes = node->as<ModuleDeclaration>()->Attributes;
	if (attributes != nullptr) {
		checkImportAttributesType(attributes);
	}
	bool isAmbientExternalModule = isAmbientModule(node);
	const DiagnosticMessage* contextErrorMessage = ifElse(isAmbientExternalModule,
		(const DiagnosticMessage*)An_ambient_module_declaration_is_only_allowed_at_the_top_level_in_a_file,
		(const DiagnosticMessage*)A_namespace_declaration_is_only_allowed_at_the_top_level_of_a_namespace_or_module);
	if (checkGrammarModuleElementContext(node, contextErrorMessage)) {
		// If we hit a module declaration in an illegal context, just bail out to avoid cascading errors.
		return;
	}
	if (!checkGrammarModifiers(node)) {
		if (!inAmbientContext && isStringLiteral(node->name())) {
			grammarErrorOnNode(node->name(), Only_ambient_modules_can_use_quoted_names);
		}
	}
	if (isIdentifier(node->name())) {
		checkCollisionsForDeclarationName(node, node->name());
		if (node->as<ModuleDeclaration>()->Keyword == Kind::ModuleKeyword) {
			error(node->name(), A_namespace_declaration_should_not_be_declared_using_the_module_keyword_Please_use_the_namespace_keyword_instead);
		}
	}
	checkExportsOnMergedDeclarations(node);
	Symbol* symbol = getSymbolOfDeclaration(node);
	// The following checks only apply on a non-ambient instantiated module declaration.
	if ((symbol->flags & SymbolFlagsValueModule) != 0 && !inAmbientContext &&
		isInstantiatedModule(node, compilerOptions->ShouldPreserveConstEnums())) {
		if (shouldCheckErasableSyntax(node)) {
			error(node, This_syntax_is_not_allowed_when_erasableSyntaxOnly_is_enabled);
		}
		if (compilerOptions->GetIsolatedModules() && getSourceFileOfNode(node)->ExternalModuleIndicator == nullptr) {
			// This could be loosened a little if needed. The only problem we are trying to avoid is unqualified
			// references to namespace members declared in other files. But use of namespaces is discouraged anyway,
			// so for now we will just not allow them in scripts, which is the only place they can merge cross-file.
			error(node->name(), Namespaces_are_not_allowed_in_global_script_files_when_0_is_enabled_If_this_file_is_not_intended_to_be_a_global_script_set_moduleDetection_to_force_or_add_an_empty_export_statement, getIsolatedModulesLikeFlagName());
		}
		if (symbol->declarations.size() > 1) {
			Node* firstNonAmbientClassOrFunc = getFirstNonAmbientClassOrFunctionDeclaration(symbol);
			if (firstNonAmbientClassOrFunc != nullptr) {
				if (getSourceFileOfNode(node) != getSourceFileOfNode(firstNonAmbientClassOrFunc)) {
					error(node->name(), A_namespace_declaration_cannot_be_in_a_different_file_from_a_class_or_function_with_which_it_is_merged);
				} else if (node->pos() < firstNonAmbientClassOrFunc->pos()) {
					error(node->name(), A_namespace_declaration_cannot_be_located_prior_to_a_class_or_function_with_which_it_is_merged);
				}
			}
		}
		if (tristateIsTrue(compilerOptions->VerbatimModuleSyntax) && isSourceFile(node->parent) &&
			(node->modifierFlags() & ModifierFlagsExport) != 0 &&
			program->GetEmitModuleFormatOfFile(static_cast<SourceFile*>(node->parent)) == ModuleKind::CommonJS) {
			Node* exportModifier = find(node->modifierNodes(), [](Node* m) { return m->kind == Kind::ExportKeyword; });
			error(exportModifier, A_top_level_export_modifier_cannot_be_used_on_value_declarations_in_a_CommonJS_module_when_verbatimModuleSyntax_is_enabled);
		}
	}
	if (isAmbientExternalModule) {
		if (isExternalModuleAugmentation(node)) {
			if (attributes != nullptr) {
				error(attributes, Import_attributes_are_not_allowed_on_a_module_augmentation);
			}
			// body of the augmentation should be checked for consistency only if augmentation was applied to its target (either global scope or module)
			// otherwise we'll be swamped in cascading errors.
			// We can detect if augmentation was applied using following rules:
			// - augmentation for a global scope is always applied
			// - augmentation for some external module is applied if symbol for augmentation is merged (it was combined with target module).
			bool checkBody = isGlobalAugmentation ||
				(getSymbolOfDeclaration(node)->flags & SymbolFlagsTransient) != 0;
			if (checkBody && node->body() != nullptr) {
				for (Node* statement : node->body()->statements()) {
					checkModuleAugmentationElement(statement);
				}
			}
		} else if (isGlobalSourceFile(node->parent)) {
			if (isGlobalAugmentation) {
				error(node->name(), Augmentations_for_the_global_scope_can_only_be_directly_nested_in_external_modules_or_ambient_module_declarations);
			} else if (tspath::isExternalModuleNameRelative(node->name()->text())) {
				error(node->name(), Ambient_module_declaration_cannot_specify_relative_module_name);
			}
		} else {
			if (isGlobalAugmentation) {
				error(node->name(), Augmentations_for_the_global_scope_can_only_be_directly_nested_in_external_modules_or_ambient_module_declarations);
			} else {
				// Node is not an augmentation and is not located on the script level.
				// This means that this is declaration of ambient module that is located in other module or namespace which is prohibited.
				error(node->name(), Ambient_modules_cannot_be_nested_in_other_modules_or_namespaces);
			}
		}
	}
}

// checker.go:5320 — checkImportAttributesType
void Checker::checkImportAttributesType(Node* attributes) {
	checkGrammarImportAttributesType(attributes->as<TypeLiteralNode>());
	checkSourceElement(attributes);
	Type* importAttributesType = getGlobalImportAttributesTypeChecked();
	Type* moduleAttributesType = getTypeOfModuleDeclarationImportAttributes(attributes);
	if (importAttributesType != emptyObjectType) {
		checkTypeAssignableTo(moduleAttributesType, importAttributesType, attributes, nullptr);
	}
}

// (deduped: getTypeOfModuleDeclarationImportAttributes already defined in
// cpp/internal/checker/checker.cpp — real impl, not a stub)

// (deduped: getTypeOfModuleImportAttributes already defined in
// cpp/internal/checker/checker.cpp — real impl, not a stub)

// checker.go:5357 — getFirstNonAmbientClassOrFunctionDeclaration
namespace {
Node* getFirstNonAmbientClassOrFunctionDeclaration(Symbol* symbol) {
	for (Node* declaration : symbol->declarations) {
		if ((isClassDeclaration(declaration) ||
			 (isFunctionDeclaration(declaration) && nodeIsPresent(declaration->body()))) &&
			(declaration->flags & NodeFlagsAmbient) == 0) {
			return declaration;
		}
	}
	return nullptr;
}
}  // namespace

// checker.go:5366 — getIsolatedModulesLikeFlagName
std::string Checker::getIsolatedModulesLikeFlagName() {
	return tristateIsTrue(compilerOptions->VerbatimModuleSyntax) ? "verbatimModuleSyntax" : "isolatedModules";
}

// checker.go:5370 — checkModuleAugmentationElement
void Checker::checkModuleAugmentationElement(Node* node) {
	switch (node->kind) {
	case Kind::VariableStatement:
		// error each individual name in variable statement instead of marking the entire variable statement
		for (Node* decl : node->as<VariableStatement>()->DeclarationList->as<VariableDeclarationList>()->Declarations->nodes) {
			checkModuleAugmentationElement(decl);
		}
		break;
	case Kind::ExportAssignment:
	case Kind::ExportDeclaration:
		grammarErrorOnFirstToken(node, Exports_and_export_assignments_are_not_permitted_in_module_augmentations);
		break;
	case Kind::ImportEqualsDeclaration:
		// import a = e.x; in module augmentation is ok, but not import a = require('fs)
		if (isInternalModuleImportEqualsDeclaration(node)) {
			break;
		}
		[[fallthrough]];
	case Kind::ImportDeclaration:
	case Kind::JSImportDeclaration:
		grammarErrorOnFirstToken(node, Imports_are_not_permitted_in_module_augmentations_Consider_moving_them_to_the_enclosing_external_module);
		break;
	case Kind::BindingElement:
	case Kind::VariableDeclaration: {
		Node* name = node->name();
		if (isBindingPattern(name)) {
			for (Node* el : name->elements()) {
				// mark individual names in binding pattern
				checkModuleAugmentationElement(el);
			}
		}
		break;
	}
	}
}

// checker.go:5398 — checkImportDeclaration
void Checker::checkImportDeclaration(Node* node) {
	// Grammar checking
	const DiagnosticMessage* diagnostic;
	if (isInJSFile(node)) {
		diagnostic = An_import_declaration_can_only_be_used_at_the_top_level_of_a_module;
	} else {
		diagnostic = An_import_declaration_can_only_be_used_at_the_top_level_of_a_namespace_or_module;
	}
	if (checkGrammarModuleElementContext(node, diagnostic)) {
		// If we hit an import declaration in an illegal context, just bail out to avoid cascading errors.
		checkExternalModuleNameInGlobalScope(node);
		return;
	}
	if (!checkGrammarModifiers(node) && node->modifiers() != nullptr) {
		grammarErrorOnFirstToken(node, An_import_declaration_cannot_have_modifiers);
	}
	if (checkExternalImportOrExportDeclaration(node)) {
		Node* attributes = getImportAttributes(node);
		Symbol* resolvedModule = nullptr;
		Node* importClause = node->importClause();
		Node* moduleSpecifier = node->moduleSpecifier();
		if (importClause != nullptr && !checkGrammarImportClause(importClause->as<ImportClause>())) {
			if (importClause->name() != nullptr) {
				checkImportBinding(importClause);
			}
			bool needsImportStar = false;
			Node* namedBindings = importClause->as<ImportClause>()->NamedBindings;
			if (namedBindings != nullptr) {
				if (isNamespaceImport(namedBindings)) {
					checkImportBinding(namedBindings);
					if (program->GetEmitModuleFormatOfFile(getSourceFileOfNode(node)) == ModuleKind::CommonJS) {
						// import * as ns from "foo";
						needsImportStar = true;
						checkExternalEmitHelpers(node, ExternalEmitHelpersImportStar);
					}
				} else {
					resolvedModule = resolveExternalModuleName(node, node->moduleSpecifier(), false,
						getTypeFromImportAttributes(attributes));
					if (resolvedModule != nullptr) {
						for (Node* binding : namedBindings->elements()) {
							checkImportBinding(binding);
						}
					}
				}
			}
			if (importClause->name() != nullptr &&
				!needsImportStar &&
				program->GetEmitModuleFormatOfFile(getSourceFileOfNode(node)) == ModuleKind::CommonJS) {
				// import d from "foo";
				checkExternalEmitHelpers(node, ExternalEmitHelpersImportDefault);
			}

			if (!importClause->isTypeOnly() &&
				ModuleKind::Node18 <= moduleKind && moduleKind <= ModuleKind::NodeNext &&
				isOnlyImportableAsDefault(moduleSpecifier, resolvedModule,
					getTypeFromImportAttributes(attributes)) &&
				!hasTypeJsonImportAttribute(node)) {
				error(moduleSpecifier, Importing_a_JSON_file_into_an_ECMAScript_module_requires_a_type_Colon_json_import_attribute_when_module_is_set_to_0, moduleKindString(moduleKind));
			}
		} else if (tristateIsTrueOrUnknown(compilerOptions->NoUncheckedSideEffectImports) && importClause == nullptr) {
			bool ignoreErrors = tristateIsTrue(compilerOptions->NoCheck);
			const DiagnosticMessage* errorMessage = nullptr;
			if (!ignoreErrors) {
				errorMessage = Cannot_find_module_or_type_declarations_for_side_effect_import_of_0;
			}
			resolveExternalModuleNameWorker(node, moduleSpecifier, errorMessage, ignoreErrors,
				false /*isForAugmentation*/, getTypeFromImportAttributes(attributes));
		}
	}
	checkImportAttributes(node);
}

// checker.go:5467 — checkExternalImportOrExportDeclaration
bool Checker::checkExternalImportOrExportDeclaration(Node* node) {
	Node* moduleName = getExternalModuleName(node);
	if (moduleName == nullptr || nodeIsMissing(moduleName)) {
		// Should be a parse error.
		return false;
	}
	if (!isStringLiteral(moduleName)) {
		error(moduleName, String_literal_expected);
		return false;
	}
	bool inAmbientExternalModule = isModuleBlock(node->parent) && isAmbientModule(node->parent->parent);
	if (!isSourceFile(node->parent) && !inAmbientExternalModule) {
		error(moduleName, ifElse(isExportDeclaration(node),
			(const DiagnosticMessage*)Export_declarations_are_not_permitted_in_a_namespace,
			(const DiagnosticMessage*)Import_declarations_in_a_namespace_cannot_reference_a_module));
		return false;
	}
	if (inAmbientExternalModule && tspath::isExternalModuleNameRelative(moduleName->text())) {
		// we have already reported errors on top level imports/exports in external module augmentations in checkModuleDeclaration
		// no need to do this again.
		if (!isTopLevelInExternalModuleAugmentation(node)) {
			// TypeScript 1.0 spec (April 2013): 12.1.6
			// An ExternalImportDeclaration in an AmbientExternalModuleDeclaration may reference
			// other external modules only through top - level external module names.
			// Relative external module names are not permitted.
			error(node, Import_or_export_declaration_in_an_ambient_module_declaration_cannot_reference_module_through_relative_module_name);
			return false;
		}
	}
	if (!isImportEqualsDeclaration(node)) {
		Node* attributes = getImportAttributes(node);
		if (attributes != nullptr) {
			if (checkGrammarImportAttributeValues(attributes->as<ImportAttributes>())) {
				return false;
			}
		}
	}
	return true;
}

// checker.go:5505 — checkImportBinding
void Checker::checkImportBinding(Node* node) {
	checkCollisionsForDeclarationName(node, node->name());
	checkAliasSymbol(node);
	if (isImportSpecifier(node)) {
		checkModuleExportName(node->propertyName(), true /*allowStringLiteral*/);
		if (moduleExportNameIsDefault(node->propertyNameOrName()) &&
			program->GetEmitModuleFormatOfFile(getSourceFileOfNode(node)) == ModuleKind::CommonJS) {
			checkExternalEmitHelpers(node, ExternalEmitHelpersImportDefault);
		}
	}
}

// checker.go:5517 — checkModuleExportName
void Checker::checkModuleExportName(Node* name, bool allowStringLiteral) {
	if (name == nullptr || name->kind != Kind::StringLiteral) {
		return;
	}
	if (!allowStringLiteral) {
		grammarErrorOnNode(name, Identifier_expected);
	} else if (moduleKind == ModuleKind::ES2015 || moduleKind == ModuleKind::ES2020) {
		if (!getSourceFileOfNode(name)->IsDeclarationFile) {
			grammarErrorOnNode(name, String_literal_import_and_export_names_are_not_supported_when_the_module_flag_is_set_to_es2015_or_es2020);
		}
	}
}

// checker.go:5537 — checkImportAttributes
void Checker::checkImportAttributes(Node* declaration) {
	Node* node = getImportAttributes(declaration);
	if (node == nullptr) {
		return;
	}
	Type* importAttributesType = getGlobalImportAttributesTypeChecked();
	if (importAttributesType != emptyObjectType) {
		checkTypeAssignableTo(getTypeFromImportAttributes(node),
			getNullableType(importAttributesType, TypeFlagsUndefined), node, nullptr);
	}
	bool isTypeOnly = isExclusivelyTypeOnlyImportOrExport(declaration) || isImportTypeNode(declaration);
	ResolutionMode override = getResolutionModeOverride(node->as<ImportAttributes>(), isTypeOnly);
	if (isTypeOnly) {
		return;  // Other grammar checks do not apply to type-only imports with import attributes
	}

	if (!moduleKindSupportsImportAttributes(moduleKind)) {
		grammarErrorOnNode(node, Import_attributes_are_only_supported_when_the_module_option_is_set_to_esnext_node18_node20_nodenext_or_preserve);
		return;
	}

	if (Node* moduleSpecifier = getExternalModuleName(declaration); moduleSpecifier != nullptr) {
		if (getEmitSyntaxForModuleSpecifierExpression(moduleSpecifier) == ModuleKind::CommonJS) {
			grammarErrorOnNode(node, Import_attributes_are_not_allowed_on_statements_that_compile_to_CommonJS_require_calls);
			return;
		}
	}

	if (override != ResolutionModeNone) {
		grammarErrorOnNode(node, X_resolution_mode_can_only_be_set_for_type_only_imports);
	}
}

// checker.go:5569 — getTypeFromImportAttributes
Type* Checker::getTypeFromImportAttributes(Node* node) {
	if (node == nullptr) {
		return nullptr;
	}
	if (isImportAttributes(node)) {
		return checkImportAttributesExpression(node);
	}
	return checkExpressionCached(node);
}

// checker.go:5579 — checkImportAttributesExpression
Type* Checker::checkImportAttributesExpression(Node* node) {
	TypeNodeLinks* links = typeNodeLinks.Get(node);
	if (links->resolvedType == nullptr ||
		staleForCheckFile(links->resolvedTypeCheckFile)) {
		// Go: fresh per-checker cache — recompute under this file.
		links->resolvedType = nullptr;
		links->resolvedTypeCheckFile = checkFileTag();
		Symbol* symbol = newSymbol(SymbolFlagsObjectLiteral, InternalSymbolNameImportAttributes);
		SymbolTable members;
		for (Node* attribute : node->as<ImportAttributes>()->Attributes->nodes) {
			Symbol* member = newSymbol(SymbolFlagsProperty, attribute->name()->text());
			valueSymbolLinks.Get(member)->resolvedType =
				getRegularTypeOfLiteralType(checkExpressionCached(attribute->as<ImportAttribute>()->Value));
			members[member->name] = member;
		}
		Type* t = newAnonymousType(symbol, members, {}, {}, {});
		t->objectFlags |= ObjectFlagsObjectLiteral | ObjectFlagsNonInferrableType;
		links->resolvedType = t;
	}
	return links->resolvedType;
}

// checker.go:5596 — getImportAttributesTypeForModuleSpecifier
Type* Checker::getImportAttributesTypeForModuleSpecifier(Node* moduleSpecifier) {
	Node* parent = moduleSpecifier->parent;
	if (isImportDeclarationOrJSImportDeclaration(parent) || isExportDeclaration(parent)) {
		return getTypeFromImportAttributes(getImportAttributes(parent));
	}
	if (isLiteralTypeNode(parent) && isLiteralImportTypeNode(parent->parent)) {
		return getTypeFromImportAttributes(getImportAttributes(parent->parent));
	}
	if (isImportCall(parent) && parent->arguments().size() > 1) {
		Node* options = parent->arguments()[1];
		return getTypeOfPropertyOfType(checkExpressionCached(options), "with");
	}
	return nullptr;
}

// checker.go:5610 — checkImportEqualsDeclaration
void Checker::checkImportEqualsDeclaration(Node* node) {
	const DiagnosticMessage* diagnostic = ifElse(isInJSFile(node),
		(const DiagnosticMessage*)An_import_declaration_can_only_be_used_at_the_top_level_of_a_module,
		(const DiagnosticMessage*)An_import_declaration_can_only_be_used_at_the_top_level_of_a_namespace_or_module);
	if (checkGrammarModuleElementContext(node, diagnostic)) {
		checkExternalModuleNameInGlobalScope(node);
		return;  // If we hit an import declaration in an illegal context, just bail out to avoid cascading errors.
	}
	checkGrammarModifiers(node);
	if (shouldCheckErasableSyntax(node) && (node->flags & NodeFlagsAmbient) == 0) {
		error(node, This_syntax_is_not_allowed_when_erasableSyntaxOnly_is_enabled);
	}
	if (isInternalModuleImportEqualsDeclaration(node) || checkExternalImportOrExportDeclaration(node)) {
		checkImportBinding(node);
		markLinkedReferences(node, ReferenceHint::ExportImportEquals, nullptr, nullptr);
		Node* moduleReference = node->as<ImportEqualsDeclaration>()->ModuleReference;
		if (!isExternalModuleReference(moduleReference)) {
			Symbol* target = resolveAlias(getSymbolOfDeclaration(node));
			if (target != unknownSymbol) {
				SymbolFlags targetFlags = getSymbolFlags(target);
				if ((targetFlags & SymbolFlagsValue) != 0) {
					// Target is a value symbol, check that it is not hidden by a local declaration with the same name
					Node* moduleName = getFirstIdentifier(moduleReference);
					if ((resolveEntityName(moduleName, SymbolFlagsValue | SymbolFlagsNamespace, false, false, nullptr)->flags & SymbolFlagsNamespace) == 0) {
						error(moduleName, Module_0_is_hidden_by_a_local_declaration_with_the_same_name, declarationNameToString(moduleName));
					}
				}
				if ((targetFlags & SymbolFlagsType) != 0) {
					checkTypeNameIsReserved(node->name(), Import_name_cannot_be_0);
				}
			}
			if (node->isTypeOnly()) {
				grammarErrorOnNode(node, An_import_alias_cannot_use_import_type);
			}
		} else {
			if (ModuleKind::ES2015 <= moduleKind && moduleKind <= ModuleKind::ESNext &&
				!node->isTypeOnly() && (node->flags & NodeFlagsAmbient) == 0) {
				// Import equals declaration cannot be emitted as ESM
				grammarErrorOnNode(node, Import_assignment_cannot_be_used_when_targeting_ECMAScript_modules_Consider_using_import_Asterisk_as_ns_from_mod_import_a_from_mod_import_d_from_mod_or_another_module_format_instead);
			}
		}
	}
}

// checker.go:5653 — checkExportDeclaration
void Checker::checkExportDeclaration(Node* node) {
	const DiagnosticMessage* diagnostic = ifElse(isInJSFile(node),
		(const DiagnosticMessage*)An_export_declaration_can_only_be_used_at_the_top_level_of_a_module,
		(const DiagnosticMessage*)An_export_declaration_can_only_be_used_at_the_top_level_of_a_namespace_or_module);
	if (checkGrammarModuleElementContext(node, diagnostic)) {
		checkExternalModuleNameInGlobalScope(node);
		return;  // If we hit an export in an illegal context, just bail out to avoid cascading errors.
	}
	ExportDeclaration* exportDecl = node->as<ExportDeclaration>();
	if (!checkGrammarModifiers(node) && exportDecl->modifiers != nullptr) {
		grammarErrorOnFirstToken(node, An_export_declaration_cannot_have_modifiers);
	}
	checkGrammarExportDeclaration(exportDecl);
	if (exportDecl->ModuleSpecifier == nullptr || checkExternalImportOrExportDeclaration(node)) {
		if (exportDecl->ExportClause != nullptr && !isNamespaceExport(exportDecl->ExportClause)) {
			// export { x, y }
			// export { x, y } from "foo"
			for (Node* binding : exportDecl->ExportClause->elements()) {
				checkExportSpecifier(binding);
			}
			bool inAmbientExternalModule = isModuleBlock(node->parent) && isAmbientModule(node->parent->parent);
			bool inAmbientNamespaceDeclaration = !inAmbientExternalModule && isModuleBlock(node->parent) &&
				exportDecl->ModuleSpecifier == nullptr && (node->flags & NodeFlagsAmbient) != 0;
			if (!isSourceFile(node->parent) && !inAmbientExternalModule && !inAmbientNamespaceDeclaration) {
				error(node, Export_declarations_are_not_permitted_in_a_namespace);
			}
		} else {
			// export * from "foo"
			// export * as ns from "foo";
			Symbol* moduleSymbol = resolveExternalModuleName(node, exportDecl->ModuleSpecifier, false,
				getTypeFromImportAttributes(getImportAttributes(node)));
			if (moduleSymbol != nullptr && hasExportAssignmentSymbol(moduleSymbol)) {
				error(exportDecl->ModuleSpecifier, Module_0_uses_export_and_cannot_be_used_with_export_Asterisk, symbolToString(moduleSymbol));
			} else if (exportDecl->ExportClause != nullptr) {
				checkAliasSymbol(exportDecl->ExportClause);
				checkModuleExportName(exportDecl->ExportClause->name(), true /*allowStringLiteral*/);
			}
			if (program->GetEmitModuleFormatOfFile(getSourceFileOfNode(node)) == ModuleKind::CommonJS) {
				if (node->as<ExportDeclaration>()->ExportClause != nullptr) {
					// export * as ns from "foo";
					checkExternalEmitHelpers(node, ExternalEmitHelpersImportStar);
				} else {
					// export * from "foo"
					checkExternalEmitHelpers(node, ExternalEmitHelpersExportStar);
				}
			}
		}
	}
	checkImportAttributes(node);
}

// checker.go:5702 — checkExternalModuleNameInGlobalScope
void Checker::checkExternalModuleNameInGlobalScope(Node* node) {
	if (getEnclosingContainer(node)->kind != Kind::SourceFile ||
		(isImportDeclarationOrJSImportDeclaration(node) && node->importClause() == nullptr)) {
		return;
	}
	if (Node* moduleName = getExternalModuleName(node); moduleName != nullptr) {
		Node* attributes = nullptr;
		if (hasImportAttributes(node)) {
			attributes = getImportAttributes(node);
		}
		Type* importAttributesType = getTypeFromImportAttributes(attributes);
		resolveExternalModuleName(node, moduleName, false, importAttributesType);
	}
}

// checker.go:5716 — checkExportSpecifier
void Checker::checkExportSpecifier(Node* node) {
	checkAliasSymbol(node);
	bool hasModuleSpecifier = node->parent->parent->moduleSpecifier() != nullptr;
	checkModuleExportName(node->propertyName(), hasModuleSpecifier);
	checkModuleExportName(node->name(), true /*allowStringLiteral*/);

	if (!hasModuleSpecifier) {
		Node* exportedName = node->propertyNameOrName();
		if (exportedName->kind == Kind::StringLiteral) {
			return;  // Skip for invalid syntax like this: export { "x" }
		}
		// find immediate value referenced by exported name (SymbolFlags.Alias is set so we don't chase down aliases)
		Symbol* symbol = resolveName(exportedName, exportedName->text(),
			SymbolFlagsValue | SymbolFlagsType | SymbolFlagsNamespace | SymbolFlagsAlias,
			nullptr /*nameNotFoundMessage*/, true /*isUse*/, false);
		if (symbol != nullptr && (symbol == undefinedSymbol || symbol == globalThisSymbol ||
			(!symbol->declarations.empty() &&
			 isGlobalSourceFile(getDeclarationContainer(symbol->declarations[0]))))) {
			error(exportedName, Cannot_export_0_Only_local_declarations_can_be_exported_from_a_module, exportedName->text());
		} else {
			markLinkedReferences(node, ReferenceHint::ExportSpecifier, nullptr /*propSymbol*/, nullptr /*parentType*/);
		}
	} else if (program->GetEmitModuleFormatOfFile(getSourceFileOfNode(node)) == ModuleKind::CommonJS &&
		moduleExportNameIsDefault(node->propertyNameOrName())) {
		checkExternalEmitHelpers(node, ExternalEmitHelpersImportDefault);
	}
}

// checker.go:5740 — isContainedByNamespace
namespace {
bool isContainedByNamespace(Node* node) {
	Node* container = node->parent;
	if (!isSourceFile(container)) {
		container = container->parent;
	}
	return isModuleDeclaration(container) && !isAmbientModule(container);
}

// checker.go:5846 — getVerbatimModuleSyntaxErrorMessage
const DiagnosticMessage* getVerbatimModuleSyntaxErrorMessage(Node* node) {
	SourceFile* sourceFile = getSourceFileOfNode(node);
	const std::string& fileName = sourceFile->FileName();

	// Check if the file is .cts or .cjs (CommonJS-specific extensions)
	if (tspath::fileExtensionIsOneOf(fileName, {tspath::extensionCts, tspath::extensionCjs})) {
		return ECMAScript_imports_and_exports_cannot_be_written_in_a_CommonJS_file_under_verbatimModuleSyntax;
	}
	// For .ts, .tsx, .js, etc.
	return ECMAScript_imports_and_exports_cannot_be_written_in_a_CommonJS_file_under_verbatimModuleSyntax_Adjust_the_type_field_in_the_nearest_package_json_to_make_this_file_an_ECMAScript_module_or_adjust_your_verbatimModuleSyntax_module_and_moduleResolution_settings_in_TypeScript;
}
}  // namespace

// checker.go:5748 — checkExportAssignment
void Checker::checkExportAssignment(Node* node) {
	bool isExportEquals = node->as<ExportAssignment>()->IsExportEquals;
	// Always check the exported expression so its identifiers are resolved even when the
	// export assignment is misplaced (grammar error), keeping diagnostics stable
	// regardless of traversal order.
	Type* exprType = checkExpressionCached(node->expression());
	const DiagnosticMessage* illegalContextMessage = ifElse(isExportEquals,
		(const DiagnosticMessage*)An_export_assignment_must_be_at_the_top_level_of_a_file_or_module_declaration,
		(const DiagnosticMessage*)A_default_export_must_be_at_the_top_level_of_a_file_or_module_declaration);
	if (checkGrammarModuleElementContext(node, illegalContextMessage)) {
		return;  // If we hit an export assignment in an illegal context, just bail out to avoid cascading errors.
	}
	if (shouldCheckErasableSyntax(node) && node->as<ExportAssignment>()->IsExportEquals &&
		(node->flags & NodeFlagsAmbient) == 0) {
		error(node, This_syntax_is_not_allowed_when_erasableSyntaxOnly_is_enabled);
	}
	if (isContainedByNamespace(node)) {
		// TODO(danielr): should these be grammar errors?
		if (isExportEquals) {
			error(node, An_export_assignment_cannot_be_used_in_a_namespace);
		} else {
			error(node, A_default_export_can_only_be_used_in_an_ECMAScript_style_module);
		}
		return;
	}
	if (!checkGrammarModifiers(node) && isExportAssignment(node) &&
		node->as<ExportAssignment>()->modifiers != nullptr) {
		grammarErrorOnFirstToken(node, An_export_assignment_cannot_have_modifiers);
	}
	bool isIllegalExportDefaultInCJS = !isExportEquals && (node->flags & NodeFlagsAmbient) == 0 &&
		tristateIsTrue(compilerOptions->VerbatimModuleSyntax) &&
		program->GetEmitModuleFormatOfFile(getSourceFileOfNode(node)) == ModuleKind::CommonJS;
	if (isIdentifier(node->expression())) {
		Node* id = node->expression();
		Symbol* sym = getExportSymbolOfValueSymbolIfExported(
			resolveEntityName(id, SymbolFlagsAll, true /*ignoreErrors*/, true /*dontResolveAlias*/, node));
		if (sym != nullptr) {
			markLinkedReferences(node, ReferenceHint::ExportAssignment, nullptr, nullptr);
			Node* typeOnlyDeclaration = getTypeOnlyAliasDeclarationEx(sym, SymbolFlagsValue);
			// If not a value, we're interpreting the identifier as a type export, along the lines of (`export { Id as default }`)
			if ((getSymbolFlags(sym) & SymbolFlagsValue) != 0) {
				// However if it is a value, we need to check it's being used correctly
				if (!isIllegalExportDefaultInCJS && (node->flags & NodeFlagsAmbient) == 0 &&
					tristateIsTrue(compilerOptions->VerbatimModuleSyntax) && typeOnlyDeclaration != nullptr) {
					const DiagnosticMessage* message = ifElse(isExportEquals,
						(const DiagnosticMessage*)An_export_declaration_must_reference_a_real_value_when_verbatimModuleSyntax_is_enabled_but_0_resolves_to_a_type_only_declaration,
						(const DiagnosticMessage*)An_export_default_must_reference_a_real_value_when_verbatimModuleSyntax_is_enabled_but_0_resolves_to_a_type_only_declaration);
					error(id, message, id->text());
				}
			} else if (!isIllegalExportDefaultInCJS && (node->flags & NodeFlagsAmbient) == 0 &&
					   tristateIsTrue(compilerOptions->VerbatimModuleSyntax)) {
				const DiagnosticMessage* message = ifElse(isExportEquals,
					(const DiagnosticMessage*)An_export_declaration_must_reference_a_value_when_verbatimModuleSyntax_is_enabled_but_0_only_refers_to_a_type,
					(const DiagnosticMessage*)An_export_default_must_reference_a_value_when_verbatimModuleSyntax_is_enabled_but_0_only_refers_to_a_type);
				error(id, message, id->text());
			}
			if (!isIllegalExportDefaultInCJS && (node->flags & NodeFlagsAmbient) == 0 &&
				compilerOptions->GetIsolatedModules() && (sym->flags & SymbolFlagsValue) == 0) {
				SymbolFlags nonLocalMeanings = getSymbolFlagsEx(sym, false /*excludeTypeOnlyMeanings*/, true /*excludeLocalMeanings*/);
				if ((sym->flags & SymbolFlagsAlias) != 0 && (nonLocalMeanings & SymbolFlagsType) != 0 &&
					(nonLocalMeanings & SymbolFlagsValue) == 0 &&
					(typeOnlyDeclaration == nullptr ||
					 getSourceFileOfNode(typeOnlyDeclaration) != getSourceFileOfNode(node))) {
					// import { SomeType } from "./someModule";
					// export default SomeType; OR
					// export = SomeType;
					const DiagnosticMessage* message = ifElse(isExportEquals,
						(const DiagnosticMessage*)X_0_resolves_to_a_type_and_must_be_marked_type_only_in_this_file_before_re_exporting_when_1_is_enabled_Consider_using_import_type_where_0_is_imported,
						(const DiagnosticMessage*)X_0_resolves_to_a_type_and_must_be_marked_type_only_in_this_file_before_re_exporting_when_1_is_enabled_Consider_using_export_type_0_as_default);
					error(id, message, {id->text(), getIsolatedModulesLikeFlagName()});
				} else if (typeOnlyDeclaration != nullptr &&
						   getSourceFileOfNode(typeOnlyDeclaration) != getSourceFileOfNode(node)) {
					// import { SomeTypeOnlyValue } from "./someModule";
					// export default SomeTypeOnlyValue; OR
					// export = SomeTypeOnlyValue;
					const DiagnosticMessage* message = ifElse(isExportEquals,
						(const DiagnosticMessage*)X_0_resolves_to_a_type_only_declaration_and_must_be_marked_type_only_in_this_file_before_re_exporting_when_1_is_enabled_Consider_using_import_type_where_0_is_imported,
						(const DiagnosticMessage*)X_0_resolves_to_a_type_only_declaration_and_must_be_marked_type_only_in_this_file_before_re_exporting_when_1_is_enabled_Consider_using_export_type_0_as_default);
					addTypeOnlyDeclarationRelatedInfo(
						error(id, message, {id->text(), getIsolatedModulesLikeFlagName()}),
						typeOnlyDeclaration, id->text());
				}
			}
		}
	}
	if (isIllegalExportDefaultInCJS) {
		error(node, getVerbatimModuleSyntaxErrorMessage(node));
	}
	Node* container = node->parent;
	if (!isSourceFile(container)) {
		container = container->parent;
	}
	checkExternalModuleExports(container);
	if (Node* typeNode = node->type(); typeNode != nullptr && node->kind == Kind::ExportAssignment) {
		Type* t = getTypeFromTypeNode(typeNode);
		checkTypeAssignableToAndOptionallyElaborate(exprType, t, node->expression(), node->expression(),
			nullptr /*headMessage*/, nullptr);
	}
	if ((node->flags & NodeFlagsAmbient) != 0 && !isEntityNameExpression(node->expression())) {
		grammarErrorOnNode(node->expression(), The_expression_of_an_export_assignment_must_be_an_identifier_or_qualified_name_in_an_ambient_context);
	}
	if (isExportEquals) {
		// Forbid export= in esm implementation files, and esm mode declaration files
		if (moduleKind >= ModuleKind::ES2015 && moduleKind != ModuleKind::Preserve &&
			(((node->flags & NodeFlagsAmbient) != 0 &&
			  program->GetImpliedNodeFormatForEmit(getSourceFileOfNode(node)) == ModuleKind::ESNext) ||
			 ((node->flags & NodeFlagsAmbient) == 0 &&
			  program->GetImpliedNodeFormatForEmit(getSourceFileOfNode(node)) != ModuleKind::CommonJS))) {
			// export assignment is not supported in es6 modules
			grammarErrorOnNode(node, Export_assignment_cannot_be_used_when_targeting_ECMAScript_modules_Consider_using_export_default_or_another_module_format_instead);
		} else if (moduleKind == ModuleKind::System && (node->flags & NodeFlagsAmbient) == 0) {
			// system modules does not support export assignment
			grammarErrorOnNode(node, Export_assignment_is_not_supported_when_module_flag_is_system);
		}
	}
}

// checker.go:5858 — checkExternalModuleExports
void Checker::checkExternalModuleExports(Node* node) {
	Symbol* moduleSymbol = getSymbolOfDeclaration(node);
	ModuleSymbolLinks* links = moduleSymbolLinks.Get(moduleSymbol);
	// Per-checker once-flag: re-run under each distinct check file (see
	// staleForCheckFile).
	if (!links->exportsChecked || staleForCheckFile(links->exportsCheckedFor)) {
		links->exportsCheckedFor = checkFileTag();
		Symbol* exportEqualsSymbol = nullptr;
		if (auto it = moduleSymbol->exports.find(InternalSymbolNameExportEquals); it != moduleSymbol->exports.end()) {
			exportEqualsSymbol = it->second;
		}
		// An export assignment is in error if (a) the module exports value members or (b) if the module exports type or
		// namespace members and the exported entity also exports type or namespace members.
		if (exportEqualsSymbol != nullptr &&
			(hasExportedMembersOfKind(moduleSymbol, SymbolFlagsValue) || hasShadowedNamespace(exportEqualsSymbol))) {
			Node* declaration = orElse(getDeclarationOfAliasSymbol(exportEqualsSymbol),
									   exportEqualsSymbol->valueDeclaration);
			if (declaration != nullptr && !isTopLevelInExternalModuleAugmentation(declaration)) {
				error(declaration, An_export_assignment_cannot_be_used_in_a_module_with_other_exported_elements);
			}
		}
		// Checks for export * conflicts
		const SymbolTable& exports = getExportsOfModule(moduleSymbol);
		for (auto& entry : exports) {
			const std::string& id = entry.first;
			Symbol* symbol = entry.second;
			if (id == InternalSymbolNameExportStar) {
				continue;
			}
			// ECMA262: 15.2.1.1 It is a Syntax Error if the ExportedNames of ModuleItemList contains any duplicate entries.
			// (TS Exceptions: namespaces, function overloads, enums, and interfaces)
			if ((symbol->flags & (SymbolFlagsNamespace | SymbolFlagsEnum)) != 0) {
				continue;
			}
			int exportedDeclarationsCount = countWhere(symbol->declarations, [](Node* d) {
				return isNotOverload(d) && !isAccessor(d) && !isInterfaceDeclaration(d);
			});
			if ((symbol->flags & SymbolFlagsTypeAlias) != 0 && exportedDeclarationsCount <= 2) {
				// it is legal to merge type alias with other values
				// so count should be either 1 (just type alias) or 2 (type alias + merged value)
				continue;
			}
			if (exportedDeclarationsCount > 1 &&
				!every(symbol->declarations, [](Node* node) {
					return getAssignmentDeclarationKind(node) == JSDeclarationKind::ExportsProperty;
				})) {
				for (Node* declaration : symbol->declarations) {
					if (isNotOverload(declaration)) {
						error(declaration, Cannot_redeclare_exported_variable_0, id);
					}
				}
			}
		}
	}
}

// checker.go:5903 — hasExportedMembersOfKind
bool Checker::hasExportedMembersOfKind(Symbol* moduleSymbol, SymbolFlags kind) {
	for (auto& entry : moduleSymbol->exports) {
		Symbol* symbol = entry.second;
		if (symbol->name != InternalSymbolNameExportEquals &&
			(getSymbolFlags(symbol) & kind) != 0) {
			return true;
		}
	}
	return false;
}

// checker.go:5912 — hasShadowedNamespace
bool Checker::hasShadowedNamespace(Symbol* symbol) {
	if ((symbol->flags & SymbolFlagsNamespaceModule) != 0 && (symbol->flags & SymbolFlagsAlias) != 0) {
		if (Symbol* target = resolveAlias(symbol);
			(target->flags & SymbolFlagsNamespace) != 0 &&
			hasExportedMembersOfKind(target, SymbolFlagsType | SymbolFlagsNamespace)) {
			return true;
		}
	}
	return false;
}

// checker.go:5925 — checkMissingDeclaration
void Checker::checkMissingDeclaration(Node* node) {
	checkDecorators(node);
}

// checker.go:5929 — checkVariableStatement
void Checker::checkVariableStatement(Node* node) {
	VariableStatement* varStatement = node->as<VariableStatement>();
	Node* declarationList = varStatement->DeclarationList;
	if (!checkGrammarModifiers(node) &&
		!checkGrammarVariableDeclarationList(declarationList->as<VariableDeclarationList>())) {
		checkGrammarForDisallowedBlockScopedVariableStatement(varStatement);
	}
	checkVariableDeclarationList(declarationList);
}

// checker.go:5938 — checkVariableDeclarationList
void Checker::checkVariableDeclarationList(Node* node) {
	NodeFlags blockScopeKind = getCombinedNodeFlags(node) & NodeFlagsBlockScoped;
	if ((blockScopeKind == NodeFlagsUsing || blockScopeKind == NodeFlagsAwaitUsing) &&
		languageVersion < LanguageFeatureMinimumTarget.UsingAndAwaitUsing) {
		checkExternalEmitHelpers(node, ExternalEmitHelpersAddDisposableResourceAndDisposeResources);
	}
	checkSourceElements(node->as<VariableDeclarationList>()->Declarations->nodes);
}

// checker.go:5946 — checkVariableDeclaration
void Checker::checkVariableDeclaration(Node* node) {
	// checker.go:5948 — `defer tr.Push(PhaseCheck,
	// "checkVariableDeclaration", {"kind","pos","end","path"}, false)()`.
	tracing::TraceScope traceCheckVariableDeclaration(
	    tracer, tracing::PhaseCheck, "checkVariableDeclaration",
	    [&] {
	        return tracing::TraceArgs{
	            {"kind", node->kind},
	            {"pos", node->pos()},
	            {"end", node->end()},
	            {"path", getSourceFileOfNode(node)->FileName()}};
	    },
	    false);
	checkGrammarVariableDeclaration(node->as<VariableDeclaration>());
	checkVariableLikeDeclaration(node);
}

// checker.go:5955 — checkVariableLikeDeclaration
// Check variable, parameter, or property declaration
void Checker::checkVariableLikeDeclaration(Node* node) {
	checkDecorators(node);
	Node* name = node->name();
	if (name == nullptr) {
		return;  // Missing array binding elements have no name
	}
	Node* typeNode = node->type();
	Node* initializer = node->initializer();
	if (!isBindingElement(node)) {
		checkSourceElement(typeNode);
	}
	// For a computed property, just check the initializer and exit
	// Do not use hasDynamicName here, because that returns false for well known symbols.
	// We want to perform checkComputedPropertyName for all computed properties, including
	// well known symbols.
	if (isComputedPropertyName(name)) {
		checkComputedPropertyName(name);
		if (initializer != nullptr) {
			checkExpressionCached(initializer);
		}
	}
	if (isBindingElement(node)) {
		Node* propName = node->propertyName();

		if (propName != nullptr && isPrivateIdentifier(propName)) {
			grammarErrorOnNode(propName, Private_identifiers_cannot_be_used_in_destructuring_patterns);
		}

		if (propName != nullptr && isIdentifier(node->name()) && isPartOfParameterDeclaration(node) &&
			nodeIsMissing(getContainingFunction(node)->body())) {
			// type F = ({a: string}) => void;
			//               ^^^^^^
			// variable renaming in function type notation is confusing,
			// so we forbid it even if noUnusedLocals is not enabled
			renamedBindingElementsInTypes.push_back(node);
			return;
		}
		if (isObjectBindingPattern(node->parent) && hasDotDotDotToken(node) &&
			languageVersion < LanguageFeatureMinimumTarget.ObjectSpreadRest) {
			checkExternalEmitHelpers(node, ExternalEmitHelpersRest);
		}
		// check computed properties inside property names of binding elements
		if (propName != nullptr && isComputedPropertyName(propName)) {
			checkComputedPropertyName(propName);
		}
		// check private/protected variable access
		Node* parent = node->parent->parent;
		CheckMode parentCheckMode =
			ifElse(hasDotDotDotToken(node), CheckModeRestBindingElement, CheckModeNormal);
		Type* parentType = getTypeForBindingElementParent(parent, parentCheckMode);
		Node* propNameName = node->propertyNameOrName();
		if (parentType != nullptr && !isBindingPattern(propNameName)) {
			Type* exprType = getLiteralTypeFromPropertyName(propNameName);
			if (isTypeUsableAsPropertyName(exprType)) {
				std::string nameText = getPropertyNameFromType(exprType);
				Symbol* property = getPropertyOfType(parentType, nameText);
				if (property != nullptr) {
					markPropertyAsReferenced(property, nullptr /*nodeForCheckWriteOnly*/, false /*isSelfTypeAccess*/);
					// A destructuring is never a write-only reference.
					checkPropertyAccessibility(node,
						parent->initializer() != nullptr && parent->initializer()->kind == Kind::SuperKeyword,
						false /*writing*/, parentType, property);
				}
			}
		}
	}
	// For a binding pattern, check contained binding elements
	if (isBindingPattern(name)) {
		checkSourceElements(name->elements());
	}
	// For a parameter declaration with an initializer, error and exit if the containing function doesn't have a body
	if (initializer != nullptr && isPartOfParameterDeclaration(node) &&
		nodeIsMissing(getContainingFunction(node)->body())) {
		error(node, A_parameter_initializer_is_only_allowed_in_a_function_or_constructor_implementation);
		return;
	}
	// For a binding pattern, validate the initializer and exit
	if (isBindingPattern(name)) {
		if (isInAmbientOrTypeNode(node)) {
			return;
		}
		bool needCheckInitializer = initializer != nullptr && node->parent->parent->kind != Kind::ForInStatement;
		bool needCheckWidenedType = !some(name->elements(), [](Node* n) { return n->name() != nullptr; });
		if (needCheckInitializer || needCheckWidenedType) {
			// Don't validate for-in initializer as it is already an error
			Type* widenedType = getWidenedTypeForVariableLikeDeclaration(node, false /*reportErrors*/);
			if (needCheckInitializer) {
				Type* initializerType = checkExpressionCached(initializer);
				if (strictNullChecks && needCheckWidenedType) {
					checkNonNullNonVoidType(initializerType, node);
				} else {
					checkTypeAssignableToAndOptionallyElaborate(initializerType,
						getWidenedTypeForVariableLikeDeclaration(node, false), node, initializer, nullptr, nullptr);
				}
			}
			// check the binding pattern with empty elements
			if (needCheckWidenedType) {
				if (isArrayBindingPattern(name)) {
					checkIteratedTypeOrElementType(IterationUseDestructuring, widenedType, undefinedType, node);
				} else if (strictNullChecks) {
					checkNonNullNonVoidType(widenedType, node);
				}
			}
		}
		return;
	}
	// For a commonjs `const x = require`, validate the alias and exit
	Symbol* symbol = getSymbolOfDeclaration(node);
	if ((symbol->flags & SymbolFlagsAlias) != 0 && isVariableDeclarationInitializedToRequire(node)) {
		checkAliasSymbol(node);
		return;
	}
	if (isBigIntLiteral(name)) {
		error(name, A_bigint_literal_cannot_be_used_as_a_property_name);
	}
	Type* t = convertAutoToAny(getTypeOfSymbol(symbol));
	if (node == symbol->valueDeclaration) {
		// Node is the primary declaration of the symbol, just validate the initializer
		// Don't validate for-in initializer as it is already an error
		if (initializer != nullptr && !isForInStatement(node->parent->parent)) {
			Type* initializerType = checkExpressionCached(initializer);
			checkTypeAssignableToAndOptionallyElaborate(initializerType, t, node, initializer,
				nullptr /*headMessage*/, nullptr);
			NodeFlags blockScopeKind = getCombinedNodeFlagsCached(node) & NodeFlagsBlockScoped;
			if (blockScopeKind == NodeFlagsAwaitUsing) {
				Type* globalAsyncDisposableType = getGlobalAsyncDisposableType();
				Type* globalDisposableType = getGlobalDisposableType();
				if (globalAsyncDisposableType != emptyObjectType && globalDisposableType != emptyObjectType) {
					Type* optionalDisposableType = getUnionType(
						{globalAsyncDisposableType, globalDisposableType, nullType, undefinedType});
					checkTypeAssignableTo(widenTypeForVariableLikeDeclaration(initializerType, node, false),
						optionalDisposableType, initializer,
						The_initializer_of_an_await_using_declaration_must_be_either_an_object_with_a_Symbol_asyncDispose_or_Symbol_dispose_method_or_be_null_or_undefined);
				}
			} else if (blockScopeKind == NodeFlagsUsing) {
				Type* globalDisposableType = getGlobalDisposableType();
				if (globalDisposableType != emptyObjectType) {
					Type* optionalDisposableType = getUnionType({globalDisposableType, nullType, undefinedType});
					checkTypeAssignableTo(widenTypeForVariableLikeDeclaration(initializerType, node, false),
						optionalDisposableType, initializer,
						The_initializer_of_a_using_declaration_must_be_either_an_object_with_a_Symbol_dispose_method_or_be_null_or_undefined);
				}
			}
		}
		if (symbol->declarations.size() > 1) {
			if (some(symbol->declarations, [&](Node* d) {
					return d != node && isVariableLike(d) && !areDeclarationFlagsIdentical(d, node);
				})) {
				error(name, All_declarations_of_0_must_have_identical_modifiers, declarationNameToString(name));
			}
		}
	} else {
		// Node is a secondary declaration, check that type is identical to primary declaration and check that
		// initializer is consistent with type associated with the node
		Type* declarationType = convertAutoToAny(getWidenedTypeForVariableLikeDeclaration(node, false));
		if (!isErrorType(t) && !isErrorType(declarationType) && !isTypeIdenticalTo(t, declarationType) &&
			(symbol->flags & SymbolFlagsAssignment) == 0) {
			errorNextVariableOrPropertyDeclarationMustHaveSameType(symbol->valueDeclaration, t, node, declarationType);
		}
		if (initializer != nullptr) {
			checkTypeAssignableToAndOptionallyElaborate(checkExpressionCached(initializer), declarationType,
				node, initializer, nullptr /*headMessage*/, nullptr);
		}
		if (symbol->valueDeclaration != nullptr && !areDeclarationFlagsIdentical(node, symbol->valueDeclaration)) {
			error(name, All_declarations_of_0_must_have_identical_modifiers, declarationNameToString(name));
		}
	}
	if (!isPropertyDeclaration(node) && !isPropertySignatureDeclaration(node)) {
		// We know we don't have a binding pattern or computed name here
		checkExportsOnMergedDeclarations(node);
		if (isVariableDeclaration(node) || isBindingElement(node)) {
			checkVarDeclaredNamesNotShadowed(node);
		}
		checkCollisionsForDeclarationName(node, node->name());
	}
}

// checker.go:6119 — errorNextVariableOrPropertyDeclarationMustHaveSameType
void Checker::errorNextVariableOrPropertyDeclarationMustHaveSameType(Node* firstDeclaration, Type* firstType,
                                                                     Node* nextDeclaration, Type* nextType) {
	Node* nextDeclarationName = getNameOfDeclaration(nextDeclaration);
	const DiagnosticMessage* message = ifElse(
		isPropertyDeclaration(nextDeclaration) || isPropertySignatureDeclaration(nextDeclaration),
		(const DiagnosticMessage*)Subsequent_property_declarations_must_have_the_same_type_Property_0_must_be_of_type_1_but_here_has_type_2,
		(const DiagnosticMessage*)Subsequent_variable_declarations_must_have_the_same_type_Variable_0_must_be_of_type_1_but_here_has_type_2);
	std::string declName = declarationNameToString(nextDeclarationName);
	Diagnostic* err = error(nextDeclarationName, message,
		{declName, TypeToString(firstType), TypeToString(nextType)});
	if (firstDeclaration != nullptr) {
		err->AddRelatedInfo(createDiagnosticForNode(firstDeclaration,
			X_0_was_also_declared_here, {declName}));
	}
}

// checker.go:6131 — checkVarDeclaredNamesNotShadowed
void Checker::checkVarDeclaredNamesNotShadowed(Node* node) {
	// - ScriptBody : StatementList
	// It is a Syntax Error if any element of the LexicallyDeclaredNames of StatementList
	// also occurs in the VarDeclaredNames of StatementList.

	// - Block : { StatementList }
	// It is a Syntax Error if any element of the LexicallyDeclaredNames of StatementList
	// also occurs in the VarDeclaredNames of StatementList.

	// Variable declarations are hoisted to the top of their function scope. They can shadow
	// block scoped declarations, which bind tighter. this will not be flagged as duplicate definition
	// by the binder as the declaration scope is different.
	// A non-initialized declaration is a no-op as the block declaration will resolve before the var
	// declaration. the problem is if the declaration has an initializer. this will act as a write to the
	// block declared value. this is fine for let, but not const.
	// Only consider declarations with initializers, uninitialized const declarations will not
	// step on a let/const variable.
	// Do not consider const and const declarations, as duplicate block-scoped declarations
	// are handled by the binder.
	// We are only looking for const declarations that step on let\const declarations from a
	// different scope. e.g.:
	//      {
	//          const x = 0; // localDeclarationSymbol obtained after name resolution will correspond to this declaration
	//          const x = 0; // symbol for this declaration will be 'symbol'
	//      }

	// skip block-scoped variables and parameters
	if ((getCombinedNodeFlagsCached(node) & NodeFlagsBlockScoped) != 0 || isPartOfParameterDeclaration(node)) {
		return;
	}
	// NOTE: in ES6 spec initializer is required in variable declarations where name is binding pattern
	// so we'll always treat binding elements as initialized
	Symbol* symbol = getSymbolOfDeclaration(node);
	Node* name = node->name();
	if ((symbol->flags & SymbolFlagsFunctionScopedVariable) != 0) {
		if (!isIdentifier(name)) {
			TSC_UNREACHABLE("Identifier expected");
		}
		Symbol* localDeclarationSymbol = resolveName(node, name->text(), SymbolFlagsVariable,
			nullptr /*nameNotFoundMessage*/, false /*isUse*/, false);
		if (localDeclarationSymbol != nullptr && localDeclarationSymbol != symbol &&
			(localDeclarationSymbol->flags & SymbolFlagsBlockScopedVariable) != 0) {
			if ((getDeclarationNodeFlagsFromSymbol(localDeclarationSymbol) & NodeFlagsBlockScoped) != 0) {
				Node* varDeclList =
					findAncestorKind(localDeclarationSymbol->valueDeclaration, Kind::VariableDeclarationList);
				Node* container = nullptr;
				if (isVariableStatement(varDeclList->parent) && varDeclList->parent->parent != nullptr) {
					container = varDeclList->parent->parent;
				}
				// names of block-scoped and function scoped variables can collide only
				// if block scoped variable is defined in the function\module\source file scope (because of variable hoisting)
				bool namesShareScope = container != nullptr &&
					((isBlock(container) && isFunctionLike(container->parent)) ||
					 isModuleBlock(container) || isModuleDeclaration(container) || isSourceFile(container));
				// here we know that function scoped variable is "shadowed" by block scoped one
				// a var declaration can't hoist past a lexical declaration and it results in a SyntaxError at runtime
				if (!namesShareScope) {
					std::string name = symbolToString(localDeclarationSymbol);
					error(node, Cannot_initialize_outer_scoped_variable_0_in_the_same_scope_as_block_scoped_declaration_1, {name, name});
				}
			}
		}
	}
}

// checker.go:6192 — checkDecorators
void Checker::checkDecorators(Node* node) {
	// skip this check for nodes that cannot have decorators. These should have already had an error reported by
	// checkGrammarModifiers.
	if (!canHaveDecorators(node) || !hasDecorators(node) ||
		!nodeCanBeDecorated(legacyDecorators, node, node->parent, node->parent->parent)) {
		return;
	}
	Node* firstDecorator = find(node->modifierNodes(), isDecorator);
	if (firstDecorator == nullptr) {
		return;
	}
	if (legacyDecorators) {
		checkExternalEmitHelpers(firstDecorator, ExternalEmitHelpersDecorate);
		if (isParameterDeclaration(node)) {
			checkExternalEmitHelpers(firstDecorator, ExternalEmitHelpersParam);
		}
	} else if (languageVersion < LanguageFeatureMinimumTarget.ClassAndClassElementDecorators) {
		checkExternalEmitHelpers(firstDecorator, ExternalEmitHelpersESDecorateAndRunInitializers);
		if (isClassDeclaration(node)) {
			if (node->name() == nullptr || getFirstTransformableStaticClassElement(node) != nullptr) {
				checkExternalEmitHelpers(firstDecorator, ExternalEmitHelpersSetFunctionName);
			}
		} else if (!isClassExpression(node)) {
			Node* name = node->name();
			if (isPrivateIdentifier(name) &&
				(isMethodDeclaration(node) || isAccessor(node) || isAutoAccessorPropertyDeclaration(node))) {
				checkExternalEmitHelpers(firstDecorator, ExternalEmitHelpersSetFunctionName);
			}
			if (isComputedPropertyName(name)) {
				checkExternalEmitHelpers(firstDecorator, ExternalEmitHelpersPropKey);
			}
		}
	}
	markLinkedReferences(node, ReferenceHint::Decorator, nullptr, nullptr);
	for (Node* modifier : node->modifierNodes()) {
		if (isDecorator(modifier)) {
			checkDecorator(modifier);
		}
	}
}

// checker.go:6231 — checkDecorator
void Checker::checkDecorator(Node* node) {
	checkGrammarDecorator(node->as<Decorator>());
	Signature* signature = getResolvedSignature(node, nullptr, CheckModeNormal);
	checkDeprecatedSignature(signature, node);
	Type* returnType = getReturnTypeOfSignature(signature);
	if ((returnType->flags & TypeFlagsAny) != 0) {
		return;
	}
	// if we fail to get a signature and return type here, we will have already reported a grammar error in `checkDecorators`.
	Signature* decoratorSignature = getDecoratorCallSignature(node);
	if (decoratorSignature == nullptr || decoratorSignature->resolvedReturnType == nullptr) {
		return;
	}
	const DiagnosticMessage* headMessage = nullptr;
	Type* expectedReturnType = decoratorSignature->resolvedReturnType;
	switch (node->parent->kind) {
	case Kind::ClassDeclaration:
	case Kind::ClassExpression:
		headMessage = Decorator_function_return_type_0_is_not_assignable_to_type_1;
		break;
	case Kind::PropertyDeclaration:
		if (!legacyDecorators) {
			headMessage = Decorator_function_return_type_0_is_not_assignable_to_type_1;
			break;
		}
		[[fallthrough]];
	case Kind::Parameter:
		headMessage = Decorator_function_return_type_is_0_but_is_expected_to_be_void_or_any;
		break;
	case Kind::MethodDeclaration:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
		headMessage = Decorator_function_return_type_0_is_not_assignable_to_type_1;
		break;
	default:
		TSC_UNREACHABLE("Unhandled case in checkDecorator");
	}
	checkTypeAssignableTo(returnType, expectedReturnType, node->expression(), headMessage);
}

// checker.go:6265 — checkIteratedTypeOrElementType
Type* Checker::checkIteratedTypeOrElementType(IterationUse use, Type* inputType,
                                              Type* sentType, Node* errorNode) {
	if (isTypeAny(inputType)) {
		return inputType;
	}
	Type* t = getIteratedTypeOrElementType(use, inputType, sentType, errorNode,
		true /*checkAssignability*/);
	if (t != nullptr) {
		return t;
	}
	return anyType;
}

// checker.go:6276 — getIteratedTypeOrElementType
Type* Checker::getIteratedTypeOrElementType(IterationUse use, Type* inputType, Type* sentType,
                                            Node* errorNode, bool checkAssignability) {
	bool allowAsyncIterables = (use & IterationUseAllowsAsyncIterablesFlag) != 0;
	if (inputType == neverType) {
		if (errorNode != nullptr) {
			reportTypeNotIterableError(errorNode, inputType, allowAsyncIterables);
		}
		return nullptr;
	}
	bool iterableExists = getGlobalIterableType() != emptyGenericType;
	bool possibleOutOfBounds = compilerOptions->NoUncheckedIndexedAccess == Tristate::True &&
		(use & IterationUsePossiblyOutOfBounds) != 0;
	if (iterableExists || allowAsyncIterables) {
		IterationTypes iterationTypes =
			getIterationTypesOfIterable(inputType, use, ifElse(iterableExists, errorNode, (Node*)nullptr));
		if (checkAssignability) {
			if (iterationTypes.nextType != nullptr) {
				const DiagnosticMessage* diagnostic = nullptr;
				if ((use & IterationUseForOfFlag) != 0) {
					diagnostic = Cannot_iterate_value_because_the_next_method_of_its_iterator_expects_type_1_but_for_of_will_always_send_0;
				} else if ((use & IterationUseSpreadFlag) != 0) {
					diagnostic = Cannot_iterate_value_because_the_next_method_of_its_iterator_expects_type_1_but_array_spread_will_always_send_0;
				} else if ((use & IterationUseDestructuringFlag) != 0) {
					diagnostic = Cannot_iterate_value_because_the_next_method_of_its_iterator_expects_type_1_but_array_destructuring_will_always_send_0;
				} else if ((use & IterationUseYieldStarFlag) != 0) {
					diagnostic = Cannot_delegate_iteration_to_value_because_the_next_method_of_its_iterator_expects_type_1_but_the_containing_generator_will_always_send_0;
				}
				if (diagnostic != nullptr) {
					checkTypeAssignableTo(sentType, iterationTypes.nextType, errorNode, diagnostic);
				}
			}
		}
		if (iterationTypes.yieldType != nullptr || iterableExists) {
			if (iterationTypes.yieldType == nullptr) {
				return nullptr;
			}
			if (possibleOutOfBounds) {
				return includeUndefinedInIndexSignature(iterationTypes.yieldType);
			}
			return iterationTypes.yieldType;
		}
	}
	Type* arrayType = inputType;
	bool hasStringConstituent = false;
	// If strings are permitted, remove any string-like constituents from the array type.
	// This allows us to find other non-string element types from an array unioned with
	// a string.
	if ((use & IterationUseAllowsStringInputFlag) != 0) {
		if ((arrayType->flags & TypeFlagsUnion) != 0) {
			// After we remove all types that are StringLike, we will know if there was a string constituent
			// based on whether the result of filter is a new array.
			const std::vector<Type*>& arrayTypes = inputType->types();
			std::vector<Type*> filteredTypes = filter(arrayTypes, [](Type* t) {
				return (t->flags & TypeFlagsStringLike) == 0;
			});
			if (!same(filteredTypes, arrayTypes)) {
				arrayType = getUnionTypeEx(filteredTypes, UnionReductionSubtype, nullptr, nullptr);
			}
		} else if ((arrayType->flags & TypeFlagsStringLike) != 0) {
			arrayType = neverType;
		}
		hasStringConstituent = arrayType != inputType;
		if (hasStringConstituent) {
			// Now that we've removed all the StringLike types, if no constituents remain, then the entire
			// arrayOrStringType was a string.
			if ((arrayType->flags & TypeFlagsNever) != 0) {
				if (possibleOutOfBounds) {
					return includeUndefinedInIndexSignature(stringType);
				}
				return stringType;
			}
		}
	}
	if (!isArrayLikeType(arrayType)) {
		if (errorNode != nullptr) {
			// Which error we report depends on whether we allow strings or if there was a
			// string constituent. For example, if the input type is number | string, we
			// want to say that number is not an array type. But if the input was just
			// number and string input is allowed, we want to say that number is not an
			// array type or a string type.
			bool allowsStrings = (use & IterationUseAllowsStringInputFlag) != 0 && !hasStringConstituent;
			auto [defaultDiagnostic, maybeMissingAwait] =
				getIterationDiagnosticDetails(use, inputType, allowsStrings);
			errorAndMaybeSuggestAwait(errorNode,
				maybeMissingAwait && getAwaitedTypeOfPromise(arrayType) != nullptr,
				defaultDiagnostic, {TypeToString(arrayType)});
		}
		if (hasStringConstituent) {
			if (possibleOutOfBounds) {
				return includeUndefinedInIndexSignature(stringType);
			}
			return stringType;
		}
		return nullptr;
	}
	Type* arrayElementType = getIndexTypeOfType(arrayType, numberType);
	if (hasStringConstituent && arrayElementType != nullptr) {
		// This is just an optimization for the case where arrayOrStringType is string | string[]
		if ((arrayElementType->flags & TypeFlagsStringLike) != 0 &&
			compilerOptions->NoUncheckedIndexedAccess != Tristate::True) {
			return stringType;
		}
		if (possibleOutOfBounds) {
			return getUnionTypeEx({arrayElementType, stringType, undefinedType},
				UnionReductionSubtype, nullptr, nullptr);
		}
		return getUnionTypeEx({arrayElementType, stringType}, UnionReductionSubtype, nullptr, nullptr);
	}
	if ((use & IterationUsePossiblyOutOfBounds) != 0) {
		return includeUndefinedInIndexSignature(arrayElementType);
	}
	return arrayElementType;
}

// Gets the requested "iteration type" from a type that is either `Iterable`-like, `Iterator`-like,
// `IterableIterator`-like, or `Generator`-like (for a non-async generator); or `AsyncIterable`-like,
// `AsyncIterator`-like, `AsyncIterableIterator`-like, or `AsyncGenerator`-like (for an async generator).
//
// checker.go:6386 — getIterationTypeOfGeneratorFunctionReturnType
Type* Checker::getIterationTypeOfGeneratorFunctionReturnType(IterationTypeKind typeKind,
                                                             Type* returnType,
                                                             bool isAsyncGenerator) {
	if (isTypeAny(returnType)) {
		return nullptr;
	}
	IterationTypes iterationTypes = getIterationTypesOfGeneratorFunctionReturnType(returnType, isAsyncGenerator);
	return iterationTypesGetType(iterationTypes, typeKind);
}

// checker.go:6394 — getIterationTypesOfGeneratorFunctionReturnType
IterationTypes Checker::getIterationTypesOfGeneratorFunctionReturnType(Type* t, bool isAsyncGenerator) {
	if (isTypeAny(t)) {
		return IterationTypes{anyType, anyType, anyType};
	}
	IterationUse use = isAsyncGenerator ? IterationUseAsyncGeneratorReturnType : IterationUseGeneratorReturnType;
	IterationTypesResolver* resolver =
		isAsyncGenerator ? asyncIterationTypesResolver : syncIterationTypesResolver;
	IterationTypes result = getIterationTypesOfIterable(t, use, nullptr /*errorNode*/);
	if (iterationTypesHasTypes(result)) {
		return result;
	}
	return getIterationTypesOfIterator(t, resolver, nullptr /*errorNode*/, nullptr /*diagnosticOutput*/);
}

// Gets the requested "iteration type" from an `Iterable`-like or `AsyncIterable`-like type.
//
// checker.go:6408 — getIterationTypeOfIterable
Type* Checker::getIterationTypeOfIterable(IterationUse use, IterationTypeKind typeKind,
                                          Type* inputType, Node* errorNode) {
	if (isTypeAny(inputType)) {
		return nullptr;
	}
	IterationTypes iterationTypes = getIterationTypesOfIterable(inputType, use, errorNode);
	return iterationTypesGetType(iterationTypes, typeKind);
}

// Gets the *yield*, *return*, and *next* types from an `Iterable`-like or `AsyncIterable`-like type.
//
// At every level that involves analyzing return types of signatures, we union the return types of all the signatures.
//
// Another thing to note is that at any step of this process, we could run into a dead end,
// meaning either the property is missing, or we run into the anyType. If either of these things
// happens, we return a default `IterationTypes{}` to signal that we could not find the iteration type.
// If a property is missing, and the previous step did not result in `any`, then we also give an error
// if the caller requested it. Then the caller can decide what to do in the case where there is no
// iterated type.
//
// For a **for-of** statement, `yield*` (in a normal generator), spread, array
// destructuring, or normal generator we will only ever look for a `[Symbol.iterator]()`
// method.
//
// For an async generator we will only ever look at the `[Symbol.asyncIterator]()` method.
//
// For a **for-await-of** statement or a `yield*` in an async generator we will look for
// the `[Symbol.asyncIterator]()` method first, and then the `[Symbol.iterator]()` method.
//
// checker.go:6435 — getIterationTypesOfIterable
IterationTypes Checker::getIterationTypesOfIterable(Type* t, IterationUse use, Node* errorNode) {
	t = getReducedType(t);
	if (isTypeAny(t)) {
		return IterationTypes{anyType, anyType, anyType};
	}
	IterationTypesKey key{t->id, use & IterationUseCacheFlags};
	// If we are reporting errors and encounter a cached `noIterationTypes`, we should ignore the cached value and continue as if nothing was cached.
	// In addition, we should not cache any new results for this call.
	bool noCache = false;
	if (auto it = iterationTypesCache.find(key); it != iterationTypesCache.end()) {
		if (errorNode == nullptr || iterationTypesHasTypes(it->second)) {
			return it->second;
		}
		noCache = true;
	}
	IterationTypes result = getIterationTypesOfIterableWorker(t, use, errorNode, noCache);
	if (!noCache) {
		iterationTypesCache[key] = result;
	}
	return result;
}

// checker.go:6457 — getIterationTypesOfIterableWorker
IterationTypes Checker::getIterationTypesOfIterableWorker(Type* t, IterationUse use,
                                                        Node* errorNode, bool noCache) {
	if ((t->flags & TypeFlagsUnion) != 0) {
		std::vector<IterationTypes> allIterationTypes;
		allIterationTypes.reserve(t->types().size());
		for (Type* constituent : t->types()) {
			IterationTypes iterationTypes =
				getIterationTypesOfIterableWorker(constituent, use, nullptr, noCache);
			if (!iterationTypesHasTypes(iterationTypes)) {
				if (errorNode != nullptr) {
					addDeferredDiagnostic([this, errorNode, t, use]() {
						reportTypeNotIterableError(errorNode, t,
							(use & IterationUseAllowsAsyncIterablesFlag) != 0);
					});
				}
				return IterationTypes{};
			}
			allIterationTypes.push_back(iterationTypes);
		}
		return combineIterationTypes(allIterationTypes);
	}
	std::vector<Diagnostic*> diags;
	if ((use & IterationUseAllowsAsyncIterablesFlag) != 0) {
		IterationTypes iterationTypes = getIterationTypesOfIterableFast(t, asyncIterationTypesResolver);
		if (iterationTypesHasTypes(iterationTypes)) {
			if ((use & IterationUseForOfFlag) != 0) {
				return getAsyncFromSyncIterationTypes(iterationTypes, errorNode);
			}
			return iterationTypes;
		}
		iterationTypes = getIterationTypesOfIterableSlow(t, asyncIterationTypesResolver, errorNode, &diags);
		if (iterationTypesHasTypes(iterationTypes)) {
			if (!diags.empty()) {
				for (Diagnostic* d : diags) {
					addDiagnostic(d);
				}
			}
			return iterationTypes;
		}
	}
	if ((use & IterationUseAllowsSyncIterablesFlag) != 0) {
		IterationTypes iterationTypes = getIterationTypesOfIterableFast(t, syncIterationTypesResolver);
		if (iterationTypesHasTypes(iterationTypes)) {
			if ((use & IterationUseAllowsAsyncIterablesFlag) != 0) {
				return getAsyncFromSyncIterationTypes(iterationTypes, errorNode);
			}
			return iterationTypes;
		}
		iterationTypes = getIterationTypesOfIterableSlow(t, syncIterationTypesResolver, errorNode, &diags);
		if (iterationTypesHasTypes(iterationTypes)) {
			if (!diags.empty()) {
				for (Diagnostic* d : diags) {
					addDiagnostic(d);
				}
			}
			if ((use & IterationUseAllowsAsyncIterablesFlag) != 0) {
				return getAsyncFromSyncIterationTypes(iterationTypes, errorNode);
			}
			return iterationTypes;
		}
	}
	if (errorNode != nullptr) {
		// We defer the diagnostic because TypeToString may attempt to resolve symbols that are already being
		// resolved, possibly causing circularities.
		addDeferredDiagnostic([this, errorNode, t, use, diags]() {
			Diagnostic* diagnostic = reportTypeNotIterableError(errorNode, t,
				(use & IterationUseAllowsAsyncIterablesFlag) != 0);
			for (Diagnostic* d : diags) {
				diagnostic->AddRelatedInfo(d);
			}
		});
	}
	return IterationTypes{};
}

// checker.go:6527 — getIterationTypesOfIterableFast
IterationTypes Checker::getIterationTypesOfIterableFast(Type* t, IterationTypesResolver* r) {
	// As an optimization, if the type is an instantiation of the following global type, then
	// just grab its related type arguments:
	// - `Iterable<T, TReturn, TNext>` or `AsyncIterable<T, TReturn, TNext>`
	// - `IteratorObject<T, TReturn, TNext>` or `AsyncIteratorObject<T, TReturn, TNext>`
	// - `IterableIterator<T, TReturn, TNext>` or `AsyncIterableIterator<T, TReturn, TNext>`
	// - `Generator<T, TReturn, TNext>` or `AsyncGenerator<T, TReturn, TNext>`
	if (isReferenceToType(t, r->getGlobalIterableType()) ||
		isReferenceToType(t, r->getGlobalIteratorObjectType()) ||
		isReferenceToType(t, r->getGlobalIterableIteratorType()) ||
		isReferenceToType(t, r->getGlobalGeneratorType())) {
		std::vector<Type*> typeArguments = getTypeArguments(t);
		return getResolvedIterationTypes(r, typeArguments[0], typeArguments[1], typeArguments[2]);
	}
	// As an optimization, if the type is an instantiation of one of the following global types, then
	// just grab the related type argument:
	// - `ArrayIterator<T>`
	// - `MapIterator<T>`
	// - `SetIterator<T>`
	// - `StringIterator<T>`
	// - `ReadableStreamAsyncIterator<T>`
	if (isReferenceToSomeType(t, r->getGlobalBuiltinIteratorTypes())) {
		return getResolvedIterationTypes(r, getTypeArguments(t)[0], getBuiltinIteratorReturnType(), unknownType);
	}
	return IterationTypes{};
}

// checker.go:6562 — isReferenceToType
bool Checker::isReferenceToType(Type* t, Type* target) {
	return t != nullptr && (t->objectFlags & ObjectFlagsReference) != 0 && t->Target() == target;
}

// checker.go:6566 — isReferenceToSomeType
bool Checker::isReferenceToSomeType(Type* t, const std::vector<Type*>& targets) {
	return t != nullptr && (t->objectFlags & ObjectFlagsReference) != 0 &&
		std::find(targets.begin(), targets.end(), t->Target()) != targets.end();
}

// checker.go:6570 — getBuiltinIteratorReturnType
Type* Checker::getBuiltinIteratorReturnType() {
	return strictBuiltinIteratorReturn ? undefinedType : anyType;
}

// checker.go:6590 — combineIterationTypes
IterationTypes Checker::combineIterationTypes(std::vector<IterationTypes> iterationTypes) {
	return IterationTypes{
		getIterationTypeUnion(iterationTypes, [](const IterationTypes& t) { return t.yieldType; }),
		getIterationTypeUnion(iterationTypes, [](const IterationTypes& t) { return t.returnType; }),
		getIterationTypeUnion(iterationTypes, [](const IterationTypes& t) { return t.nextType; }),
	};
}

// checker.go:6598 — getIterationTypeUnion
Type* Checker::getIterationTypeUnion(const std::vector<IterationTypes>& iterationTypes,
                                     const std::function<Type*(const IterationTypes&)>& f) {
	std::vector<Type*> types = mapNonNil(iterationTypes, f);
	if (types.empty()) {
		return nullptr;
	}
	return getUnionType(types);
}

// checker.go:6606 — getAsyncFromSyncIterationTypes
IterationTypes Checker::getAsyncFromSyncIterationTypes(IterationTypes iterationTypes,
                                                       Node* errorNode) {
	if (!iterationTypesHasTypes(iterationTypes) ||
		(iterationTypes.yieldType == anyType && iterationTypes.returnType == anyType &&
		 iterationTypes.nextType == anyType)) {
		return iterationTypes;
	}
	// if we're requesting diagnostics, report errors for a missing `Awaited<T>`.
	if (errorNode != nullptr) {
		getGlobalAwaitedSymbol();
	}
	return IterationTypes{
		orElse(getAwaitedTypeEx(iterationTypes.yieldType, errorNode, nullptr), anyType),
		orElse(getAwaitedTypeEx(iterationTypes.returnType, errorNode, nullptr), anyType),
		iterationTypes.nextType,
	};
}

// Gets the *yield*, *return*, and *next* types of an `Iterable`-like or `AsyncIterable`-like
// type from its members.
//
// If we successfully found the *yield*, *return*, and *next* types, an `IterationTypes` with non-nil
// members is returned. Otherwise, a default `IterationTypes{}` is returned.
//
// NOTE: You probably don't want to call this directly and should be calling
// `getIterationTypesOfIterable` instead.
//
// checker.go:6630 — getIterationTypesOfIterableSlow
IterationTypes Checker::getIterationTypesOfIterableSlow(Type* t, IterationTypesResolver* r,
                                                      Node* errorNode,
                                                      std::vector<Diagnostic*>* diagnosticOutput) {
	Symbol* method = getPropertyOfType(t, getPropertyNameForKnownSymbolName(r->iteratorSymbolName));
	if (method != nullptr && (method->flags & SymbolFlagsOptional) == 0) {
		Type* methodType = getTypeOfSymbol(method);
		if (isTypeAny(methodType)) {
			return IterationTypes{anyType, anyType, anyType};
		}
		std::vector<Signature*> allSignatures = getSignaturesOfType(methodType, SignatureKind::Call);
		std::vector<Signature*> validSignatures = filter(allSignatures, [this](Signature* sig) {
			return getMinArgumentCount(sig) == 0;
		});
		if (!validSignatures.empty()) {
			Type* iteratorType = getIntersectionType(
				mapVec(validSignatures, [this](Signature* s) { return getReturnTypeOfSignature(s); }));
			return getIterationTypesOfIteratorWorker(iteratorType, r, errorNode, diagnosticOutput);
		}
		if (errorNode != nullptr && !allSignatures.empty()) {
			checkTypeAssignableToEx(t, r->getGlobalIterableTypeChecked(), errorNode, nullptr, diagnosticOutput);
		}
	}
	return IterationTypes{};
}

// Gets the *yield*, *return*, and *next* types from an `Iterator`-like or `AsyncIterator`-like type.
//
// If we successfully found the *yield*, *return*, and *next* types, an `IterationTypes` with non-nil
// members is returned. Otherwise, a default `IterationTypes{}` is returned.
//
// checker.go:6655 — getIterationTypesOfIterator
IterationTypes Checker::getIterationTypesOfIterator(Type* t, IterationTypesResolver* r,
                                                  Node* errorNode,
                                                  std::vector<Diagnostic*>* diagnosticOutput) {
	return getIterationTypesOfIteratorWorker(t, r, errorNode, diagnosticOutput);
}

// Gets the *yield*, *return*, and *next* types from an `Iterator`-like or `AsyncIterator`-like type.
//
// If we successfully found the *yield*, *return*, and *next* types, an `IterationTypes` with non-nil
// members is returned. Otherwise, a default `IterationTypes{}` is returned.
//
// NOTE: You probably don't want to call this directly and should be calling
// `getIterationTypesOfIterator` instead.
//
// checker.go:6665 — getIterationTypesOfIteratorWorker
IterationTypes Checker::getIterationTypesOfIteratorWorker(Type* t, IterationTypesResolver* r,
                                                        Node* errorNode,
                                                        std::vector<Diagnostic*>* diagnosticOutput) {
	if (isTypeAny(t)) {
		return IterationTypes{anyType, anyType, anyType};
	}
	IterationTypes iterationTypes = getIterationTypesOfIteratorFast(t, r);
	if (iterationTypesHasTypes(iterationTypes)) {
		return iterationTypes;
	}
	return getIterationTypesOfIteratorSlow(t, r, errorNode, diagnosticOutput);
}

// checker.go:6676 — getIterationTypesOfIteratorFast
IterationTypes Checker::getIterationTypesOfIteratorFast(Type* t, IterationTypesResolver* r) {
	// As an optimization, if the type is an instantiation of the following global type, then
	// just grab its related type arguments:
	// - `Iterable<T, TReturn, TNext>` or `AsyncIterable<T, TReturn, TNext>`
	// - `IteratorObject<T, TReturn, TNext>` or `AsyncIteratorObject<T, TReturn, TNext>`
	// - `IterableIterator<T, TReturn, TNext>` or `AsyncIterableIterator<T, TReturn, TNext>`
	// - `Generator<T, TReturn, TNext>` or `AsyncGenerator<T, TReturn, TNext>`
	if (isReferenceToType(t, r->getGlobalIteratorType()) ||
		isReferenceToType(t, r->getGlobalIteratorObjectType()) ||
		isReferenceToType(t, r->getGlobalIterableIteratorType()) ||
		isReferenceToType(t, r->getGlobalGeneratorType())) {
		std::vector<Type*> typeArguments = getTypeArguments(t);
		return getResolvedIterationTypes(r, typeArguments[0], typeArguments[1], typeArguments[2]);
	}
	// As an optimization, if the type is an instantiation of one of the following global types, then
	// just grab the related type argument:
	// - `ArrayIterator<T>`
	// - `MapIterator<T>`
	// - `SetIterator<T>`
	// - `StringIterator<T>`
	// - `ReadableStreamAsyncIterator<T>`
	if (isReferenceToSomeType(t, r->getGlobalBuiltinIteratorTypes())) {
		return getResolvedIterationTypes(r, getTypeArguments(t)[0], getBuiltinIteratorReturnType(), unknownType);
	}
	return IterationTypes{};
}

// checker.go:6703 — getIterationTypesOfIteratorSlow
IterationTypes Checker::getIterationTypesOfIteratorSlow(Type* t, IterationTypesResolver* r,
                                                      Node* errorNode,
                                                      std::vector<Diagnostic*>* diagnosticOutput) {
	return combineIterationTypes({
		getIterationTypesOfMethod(t, r, "next", errorNode, diagnosticOutput),
		getIterationTypesOfMethod(t, r, "return", errorNode, diagnosticOutput),
		getIterationTypesOfMethod(t, r, "throw", errorNode, diagnosticOutput),
	});
}

// checker.go:6711 — getIterationTypesOfMethod
IterationTypes Checker::getIterationTypesOfMethod(Type* t, IterationTypesResolver* resolver,
                                                  const std::string& methodName, Node* errorNode,
                                                  std::vector<Diagnostic*>* diagnosticOutput) {
	Symbol* method = getPropertyOfType(t, methodName);
	// Ignore 'return' or 'throw' if they are missing.
	if (method == nullptr && methodName != "next") {
		return IterationTypes{};
	}
	Type* methodType = nullptr;
	if (method != nullptr && !(methodName == "next" && (method->flags & SymbolFlagsOptional) != 0)) {
		if (methodName == "next") {
			methodType = getTypeOfSymbol(method);
		} else {
			methodType = getTypeWithFacts(getTypeOfSymbol(method), TypeFactsNEUndefinedOrNull);
		}
	}
	if (isTypeAny(methodType)) {
		return IterationTypes{anyType, anyType, anyType};
	}
	// Both async and non-async iterators *must* have a `next` method.
	std::vector<Signature*> methodSignatures;
	if (methodType != nullptr) {
		methodSignatures = getSignaturesOfType(methodType, SignatureKind::Call);
	}
	if (methodSignatures.empty()) {
		if (errorNode != nullptr) {
			const DiagnosticMessage* diagnostic =
				methodName == "next" ? resolver->mustHaveANextMethodDiagnostic : resolver->mustBeAMethodDiagnostic;
			reportDiagnostic(NewDiagnosticForNode(errorNode, diagnostic, {methodName}), diagnosticOutput);
		}
		return IterationTypes{};
	}
	// If the method signature comes exclusively from the global iterator or generator type,
	// create iteration types from its type arguments like `getIterationTypesOfIteratorFast`
	// does (so as to remove `undefined` from the next and return types). We arrive here when
	// a contextual type for a generator was not a direct reference to one of those global types,
	// but looking up `methodType` referred to one of them (and nothing else). E.g., in
	// `interface SpecialIterator extends Iterator<number> {}`, `SpecialIterator` is not a
	// reference to `Iterator`, but its `next` member derives exclusively from `Iterator`.
	if (methodSignatures.size() == 1 && methodType->symbol != nullptr) {
		Type* globalGeneratorType = resolver->getGlobalGeneratorType();
		Type* globalIteratorType = resolver->getGlobalIteratorType();
		bool isGeneratorMethod = false;
		if (globalGeneratorType->symbol != nullptr) {
			auto it = globalGeneratorType->symbol->members.find(methodName);
			isGeneratorMethod = it != globalGeneratorType->symbol->members.end() &&
				it->second == methodType->symbol;
		}
		bool isIteratorMethod = false;
		if (!isGeneratorMethod && globalIteratorType->symbol != nullptr) {
			auto it = globalIteratorType->symbol->members.find(methodName);
			isIteratorMethod = it != globalIteratorType->symbol->members.end() &&
				it->second == methodType->symbol;
		}
		if (isGeneratorMethod || isIteratorMethod) {
			std::vector<Type*> typeParameters = interfaceTypeTypeParameters(
				ifElse(isGeneratorMethod, globalGeneratorType, globalIteratorType)->AsInterfaceType());
			TypeMapper* mapper = typeMapperOf(methodType);
			Type* nextType = nullptr;
			if (methodName == "next") {
				nextType = getMappedType(typeParameters[2], mapper);
			}
			return IterationTypes{getMappedType(typeParameters[0], mapper),
			                      getMappedType(typeParameters[1], mapper), nextType};
		}
	}
	// Extract the first parameter and return type of each signature.
	std::vector<Type*> methodParameterTypes;
	std::vector<Type*> methodReturnTypes;
	for (Signature* signature : methodSignatures) {
		if (methodName != "throw" && !signature->parameters.empty()) {
			methodParameterTypes.push_back(getTypeAtPosition(signature, 0));
		}
		methodReturnTypes.push_back(getReturnTypeOfSignature(signature));
	}
	// Resolve the *next* or *return* type from the first parameter of a `next()` or
	// `return()` method, respectively.
	std::vector<Type*> returnTypes;
	Type* nextType = nullptr;
	if (methodName != "throw") {
		Type* methodParameterType;
		if (!methodParameterTypes.empty()) {
			methodParameterType = getUnionType(methodParameterTypes);
		} else {
			methodParameterType = unknownType;
		}
		if (methodName == "next") {
			// The value of `next(value)` is *not* awaited by async generators
			nextType = methodParameterType;
		} else if (methodName == "return") {
			// The value of `return(value)` *is* awaited by async generators
			Type* resolvedMethodParameterType =
				orElse(resolver->resolveIterationType(methodParameterType, errorNode), anyType);
			returnTypes.push_back(resolvedMethodParameterType);
		}
	}
	// Resolve the *yield* and *return* types from the return type of the method (i.e. `IteratorResult`)
	Type* yieldType = nullptr;
	Type* methodReturnType;
	if (!methodReturnTypes.empty()) {
		methodReturnType = getIntersectionType(methodReturnTypes);
	} else {
		methodReturnType = neverType;
	}
	Type* resolvedMethodReturnType =
		orElse(resolver->resolveIterationType(methodReturnType, errorNode), anyType);
	IterationTypes iterationTypes = getIterationTypesOfIteratorResult(resolvedMethodReturnType);
	if (!iterationTypesHasTypes(iterationTypes)) {
		if (errorNode != nullptr) {
			reportDiagnostic(NewDiagnosticForNode(errorNode, resolver->mustHaveAValueDiagnostic, {methodName}),
				diagnosticOutput);
		}
		yieldType = anyType;
		returnTypes.push_back(anyType);
	} else {
		yieldType = iterationTypes.yieldType;
		returnTypes.push_back(iterationTypes.returnType);
	}
	return IterationTypes{yieldType, getUnionType(returnTypes), nextType};
}

// Gets the *yield* and *return* types of an `IteratorResult`-like type.
//
// If we are unable to determine a *yield* or a *return* type, `noIterationTypes` is
// returned to indicate to the caller that it should handle the error. Otherwise, an
// `IterationTypes` record is returned.
//
// checker.go:6819 — getIterationTypesOfIteratorResult
IterationTypes Checker::getIterationTypesOfIteratorResult(Type* t) {
	if (isTypeAny(t)) {
		return IterationTypes{anyType, anyType, anyType};
	}
	// As an optimization, if the type is an instantiation of one of the global `IteratorYieldResult<T>`
	// or `IteratorReturnResult<TReturn>` types, then just grab its type argument.
	if (isReferenceToType(t, getGlobalIteratorYieldResultType())) {
		return IterationTypes{getTypeArguments(t)[0], nullptr, nullptr};
	}
	if (isReferenceToType(t, getGlobalIteratorReturnResultType())) {
		return IterationTypes{nullptr, getTypeArguments(t)[0], nullptr};
	}
	// Choose any constituents that can produce the requested iteration type.
	Type* yieldIteratorResult = filterType(t, [this](Type* u) { return isYieldIteratorResult(u); });
	Type* yieldType = nullptr;
	if (yieldIteratorResult != neverType) {
		yieldType = getTypeOfPropertyOfType(yieldIteratorResult, "value" /* as __String */);
	}
	Type* returnIteratorResult = filterType(t, [this](Type* u) { return isReturnIteratorResult(u); });
	Type* returnType = nullptr;
	if (returnIteratorResult != neverType) {
		returnType = getTypeOfPropertyOfType(returnIteratorResult, "value" /* as __String */);
	}
	if (yieldType == nullptr && returnType == nullptr) {
		return IterationTypes{};
	}
	// From https://tc39.github.io/ecma262/#sec-iteratorresult-interface
	// > ... If the iterator does not have a return value, `value` is `undefined`. In that case, the
	// > `value` property may be absent from the conforming object if it does not inherit an explicit
	// > `value` property.
	return IterationTypes{yieldType, orElse(returnType, voidType), nullptr};
}

// checker.go:6852 — isYieldIteratorResult
bool Checker::isYieldIteratorResult(Type* t) {
	return isIteratorResult(t, IterationTypeKind::Yield);
}

// checker.go:6856 — isReturnIteratorResult
bool Checker::isReturnIteratorResult(Type* t) {
	return isIteratorResult(t, IterationTypeKind::Return);
}

// checker.go:6860 — isIteratorResult
bool Checker::isIteratorResult(Type* t, IterationTypeKind kind) {
	// From https://tc39.github.io/ecma262/#sec-iteratorresult-interface:
	// > [done] is the result status of an iterator `next` method call. If the end of the iterator was reached `done` is `true`.
	// > If the end was not reached `done` is `false` and a value is available.
	// > If a `done` property (either own or inherited) does not exist, it is consider to have the value `false`.
	Type* doneType = orElse(getTypeOfPropertyOfType(t, "done"), falseType);
	return isTypeAssignableTo(ifElse(kind == IterationTypeKind::Yield, falseType, trueType), doneType);
}

// checker.go:6869 — reportTypeNotIterableError
Diagnostic* Checker::reportTypeNotIterableError(Node* errorNode, Type* t, bool allowAsyncIterables) {
	const DiagnosticMessage* message;
	if (allowAsyncIterables) {
		message = Type_0_must_have_a_Symbol_asyncIterator_method_that_returns_an_async_iterator;
	} else {
		message = Type_0_must_have_a_Symbol_iterator_method_that_returns_an_iterator;
	}
	bool suggestAwait = getAwaitedTypeOfPromise(t) != nullptr ||
		(!allowAsyncIterables &&
		 isForOfStatement(errorNode->parent) &&
		 errorNode->parent->expression() == errorNode &&
		 getGlobalAsyncIterableType() != emptyGenericType &&
		 isTypeAssignableTo(t, createTypeFromGenericGlobalType(getGlobalAsyncIterableType(),
															 {anyType, anyType, anyType})));
	return errorAndMaybeSuggestAwait(errorNode, suggestAwait, message, {TypeToString(t)});
}

// checker.go:6884 — getIterationDiagnosticDetails
std::pair<const DiagnosticMessage*, bool> Checker::getIterationDiagnosticDetails(
	IterationUse use, Type* inputType, bool allowsStrings) {
	Type* yieldType = getIterationTypeOfIterable(use, IterationTypeKind::Yield, inputType,
		nullptr /*errorNode*/);
	if (yieldType != nullptr) {
		return {Type_0_can_only_be_iterated_through_when_using_the_downlevelIteration_flag_or_with_a_target_of_es2015_or_higher, false};
	}
	if (inputType->symbol != nullptr && isES2015OrLaterIterable(inputType->symbol->name)) {
		return {Type_0_can_only_be_iterated_through_when_using_the_downlevelIteration_flag_or_with_a_target_of_es2015_or_higher, true};
	}
	if (allowsStrings) {
		return {Type_0_is_not_an_array_type_or_a_string_type, true};
	}
	return {Type_0_is_not_an_array_type, true};
}

// checker.go:6906 — checkAliasSymbol
void Checker::checkAliasSymbol(Node* node) {
	Symbol* symbol = getSymbolOfDeclaration(node);
	Symbol* target = resolveAlias(symbol);
	if (target == unknownSymbol) {
		return;
	}
	// For external modules, `symbol` represents the local symbol for an alias.
	// This local symbol will merge any other local declarations (excluding other aliases)
	// and symbol.flags will contains combined representation for all merged declaration.
	// Based on symbol.flags we can compute a set of excluded meanings (meaning that resolved alias should not have,
	// otherwise it will conflict with some local declaration). Note that in addition to normal flags we include matching SymbolFlags.Export*
	// in order to prevent collisions with declarations that were exported from the current module (they still contribute to local names).
	symbol = getMergedSymbol(orElse(symbol->exportSymbol, symbol));
	SymbolFlags targetFlags = getSymbolFlags(target);
	// A type-only import/export will already have a grammar error in a JS file, so no need to issue more errors within
	if (isInJSFile(node) && (targetFlags & SymbolFlagsValue) == 0 &&
		!isTypeOnlyImportOrExportDeclaration(node)) {
		Node* errorNode = orElse(node->propertyNameOrName(), node);
		TSC_ASSERT(node->kind != Kind::NamespaceExport, "");
		if (isExportSpecifier(node)) {
			Diagnostic* diag = error(errorNode, Types_cannot_appear_in_export_declarations_in_JavaScript_files);
			if (Symbol* sourceSymbol = getSourceFileOfNode(node)->asNode()->symbol(); sourceSymbol != nullptr) {
				auto it = sourceSymbol->exports.find(node->propertyNameOrName()->text());
				if (it != sourceSymbol->exports.end() && it->second == target) {
					Symbol* alreadyExportedSymbol = it->second;
					if (Node* exportingDeclaration = find(alreadyExportedSymbol->declarations, isJSTypeAliasDeclaration);
						exportingDeclaration != nullptr) {
						diag->AddRelatedInfo(NewDiagnosticForNode(exportingDeclaration,
							X_0_is_automatically_exported_here, {alreadyExportedSymbol->name}));
					}
				}
			}
		} else {
			std::string identifierText = symbol->name;
			if (isIdentifier(errorNode)) {
				identifierText = errorNode->text();
			}
			std::string specifierText = "...";
			if (Node* importDeclaration = findAncestor(node, [](Node* n) {
					return isImportOrImportEqualsDeclaration(n) || isVariableDeclaration(n);
				}); importDeclaration != nullptr) {
				if (Node* moduleSpecifier = TryGetModuleSpecifierFromDeclaration(importDeclaration);
					moduleSpecifier != nullptr) {
					specifierText = moduleSpecifier->text();
				}
			}
			std::string importText = "import(\"" + specifierText + "\")";
			if (isImportSpecifier(node)) {
				importText = importText + "." + identifierText;
			}
			error(errorNode, X_0_is_a_type_and_cannot_be_imported_in_JavaScript_files_Use_1_in_a_JSDoc_type_annotation,
				{identifierText, importText});
		}
		return;
	}
	SymbolFlags excludedMeanings =
		((symbol->flags & (SymbolFlagsValue | SymbolFlagsExportValue)) != 0 ? SymbolFlagsValue : 0) |
		((symbol->flags & SymbolFlagsType) != 0 ? SymbolFlagsType : 0) |
		((symbol->flags & SymbolFlagsNamespace) != 0 ? SymbolFlagsNamespace : 0);
	if ((targetFlags & excludedMeanings) != 0) {
		const DiagnosticMessage* message = ifElse(isExportSpecifier(node),
			(const DiagnosticMessage*)Export_declaration_conflicts_with_exported_declaration_of_0,
			(const DiagnosticMessage*)Import_declaration_conflicts_with_local_declaration_of_0);
		error(node, message, symbolToString(symbol));
	} else if (!isExportSpecifier(node)) {
		// Look at 'compilerOptions.isolatedModules' and not 'getIsolatedModules(...)' (which considers 'verbatimModuleSyntax')
		// here because 'verbatimModuleSyntax' will already have an error for importing a type without 'import type'.
		bool appearsValueyToTranspiler = tristateIsTrue(compilerOptions->IsolatedModules) &&
			findAncestor(node, isTypeOnlyImportOrExportDeclaration) == nullptr;
		if (appearsValueyToTranspiler &&
			(symbol->flags & (SymbolFlagsValue | SymbolFlagsExportValue)) != 0) {
			error(node, Import_0_conflicts_with_local_value_so_must_be_declared_with_a_type_only_import_when_isolatedModules_is_enabled,
				{symbolToString(symbol), getIsolatedModulesLikeFlagName()});
		}
	}
	if (compilerOptions->GetIsolatedModules() && !isTypeOnlyImportOrExportDeclaration(node) &&
		(node->flags & NodeFlagsAmbient) == 0) {
		Node* typeOnlyAlias = getTypeOnlyAliasDeclaration(symbol);
		bool isType = (targetFlags & SymbolFlagsValue) == 0;
		if (isType || typeOnlyAlias != nullptr) {
			switch (node->kind) {
			case Kind::ImportClause:
			case Kind::ImportSpecifier:
			case Kind::ImportEqualsDeclaration:
				if (tristateIsTrue(compilerOptions->VerbatimModuleSyntax)) {
					TSC_ASSERT(node->name() != nullptr, "An ImportClause with a symbol should have a name");
					const DiagnosticMessage* message;
					if (tristateIsTrue(compilerOptions->VerbatimModuleSyntax) &&
						isInternalModuleImportEqualsDeclaration(node)) {
						message = An_import_alias_cannot_resolve_to_a_type_or_type_only_declaration_when_verbatimModuleSyntax_is_enabled;
					} else if (isType) {
						message = X_0_is_a_type_and_must_be_imported_using_a_type_only_import_when_verbatimModuleSyntax_is_enabled;
					} else {
						message = X_0_resolves_to_a_type_only_declaration_and_must_be_imported_using_a_type_only_import_when_verbatimModuleSyntax_is_enabled;
					}
					std::string name = node->propertyNameOrName()->text();
					addTypeOnlyDeclarationRelatedInfo(error(node, message, name),
						ifElse(isType, (Node*)nullptr, typeOnlyAlias), name);
				}
				if (isType && node->kind == Kind::ImportEqualsDeclaration &&
					hasModifier(node, ModifierFlagsExport)) {
					error(node, Cannot_use_export_import_on_a_type_or_type_only_namespace_when_0_is_enabled,
						getIsolatedModulesLikeFlagName());
				}
				break;
			case Kind::ExportSpecifier:
				// Don't allow re-exporting an export that will be elided when `--isolatedModules` is set.
				// The exception is that `import type { A } from './a'; export { A }` is allowed
				// because single-file analysis can determine that the export should be dropped.
				if (tristateIsTrue(compilerOptions->VerbatimModuleSyntax) ||
					getSourceFileOfNode(typeOnlyAlias) != getSourceFileOfNode(node)) {
					std::string name = node->propertyNameOrName()->text();
					Diagnostic* diagnostic;
					if (isType) {
						diagnostic = error(node, Re_exporting_a_type_when_0_is_enabled_requires_using_export_type,
							getIsolatedModulesLikeFlagName());
					} else {
						diagnostic = error(node, X_0_resolves_to_a_type_only_declaration_and_must_be_re_exported_using_a_type_only_re_export_when_1_is_enabled,
							{name, getIsolatedModulesLikeFlagName()});
					}
					addTypeOnlyDeclarationRelatedInfo(diagnostic,
						ifElse(isType, (Node*)nullptr, typeOnlyAlias), name);
				}
				break;
			default:
				break;
			}
		}
		if (tristateIsTrue(compilerOptions->VerbatimModuleSyntax) && !isImportEqualsDeclaration(node) &&
			!isInJSFile(node) &&
			program->GetEmitModuleFormatOfFile(getSourceFileOfNode(node)) == ModuleKind::CommonJS) {
			error(node, getVerbatimModuleSyntaxErrorMessage(node));
		} else if (moduleKind == ModuleKind::Preserve && !isImportEqualsDeclaration(node) &&
				   !isVariableDeclaration(node) && !isBindingElement(node) &&
				   program->GetEmitModuleFormatOfFile(getSourceFileOfNode(node)) == ModuleKind::CommonJS) {
			// In `--module preserve`, ESM input syntax emits ESM output syntax, but there will be times
			// when we look at the `impliedNodeFormat` of this file and decide it's CommonJS (i.e., currently,
			// only if the file extension is .cjs/.cts). To avoid that inconsistency, we disallow ESM syntax
			// in files that are unambiguously CommonJS in this mode.
			error(node, ECMAScript_module_syntax_is_not_allowed_in_a_CommonJS_module_when_module_is_set_to_preserve);
		}
		if (tristateIsTrue(compilerOptions->VerbatimModuleSyntax) &&
			!isTypeOnlyImportOrExportDeclaration(node) && (node->flags & NodeFlagsAmbient) == 0 &&
			(targetFlags & SymbolFlagsConstEnum) != 0) {
			Node* constEnumDeclaration = target->valueDeclaration;
			if (constEnumDeclaration != nullptr &&
				(constEnumDeclaration->flags & NodeFlagsAmbient) != 0) {
				auto* redirect = program->GetProjectReferenceFromOutputDts(
					getSourceFileOfNode(constEnumDeclaration)->Path());
				// Go: redirect.Resolved.CompilerOptions() — Resolved is a
				// *ParsedCommandLine; nil receiver returns nil, then
				// ShouldPreserveConstEnums would panic — Resolved is never nil in
				// practice; the guard keeps us graceful.
				if (redirect == nullptr || redirect->resolved == nullptr ||
					!redirect->resolved->CompilerOptions()->ShouldPreserveConstEnums()) {
					error(node, Cannot_access_ambient_const_enums_when_0_is_enabled,
						getIsolatedModulesLikeFlagName());
				}
			}
		}
	}
	if (isImportSpecifier(node)) {
		Symbol* targetSymbol = resolveAliasWithDeprecationCheck(symbol, node);
		if (isDeprecatedSymbol(targetSymbol) && !targetSymbol->declarations.empty()) {
			addDeprecatedSuggestion(node, targetSymbol->declarations, targetSymbol->name);
		}
	}
}

// checker.go:7036 — areDeclarationFlagsIdentical
bool Checker::areDeclarationFlagsIdentical(Node* left, Node* right) {
	if ((isParameterDeclaration(left) && isVariableDeclaration(right)) ||
		(isVariableDeclaration(left) && isParameterDeclaration(right))) {
		// Differences in optionality between parameters and variables are allowed.
		return true;
	}
	if (isOptionalDeclaration(left) != isOptionalDeclaration(right)) {
		return false;
	}
	constexpr ModifierFlags interestingFlags =
		ModifierFlagsPrivate | ModifierFlagsProtected | ModifierFlagsAsync |
		ModifierFlagsAbstract | ModifierFlagsReadonly | ModifierFlagsStatic;
	return getSelectedModifierFlags(left, interestingFlags) == getSelectedModifierFlags(right, interestingFlags);
}

// checker.go:7048 — checkTypeAliasDeclaration
void Checker::checkTypeAliasDeclaration(Node* node) {
	// Grammar checking
	checkGrammarModifiers(node);
	checkTypeNameIsReserved(node->name(), Type_alias_name_cannot_be_0);
	if (!containerAllowsBlockScopedVariable(node->parent)) {
		grammarErrorOnNode(node, X_0_declarations_can_only_be_declared_inside_a_block, {"type"});
	}
	checkExportsOnMergedDeclarations(node);

	Node* typeNode = node->type();
	auto typeParameters = node->typeParameters();
	checkTypeParameters(typeParameters);
	if (typeNode != nullptr && typeNode->kind == Kind::IntrinsicKeyword) {
		if (!((typeParameters.empty() && node->name()->text() == "BuiltinIteratorReturn") ||
			  (typeParameters.size() == 1 &&
			   intrinsicTypeKinds.count(node->name()->text()) &&
			   intrinsicTypeKinds.at(node->name()->text()) != IntrinsicTypeKind::Unknown))) {
			error(typeNode, The_intrinsic_keyword_can_only_be_used_to_declare_compiler_provided_intrinsic_types);
		}
		// The `intrinsic` keyword is a leaf type node with no child nodes to check,
		// so skipping the checkSourceElement below visits nothing.
		return;  // intrinsic keyword has no children to check
	}
	checkSourceElement(typeNode);
	registerForUnusedIdentifiersCheck(node);
}

// checker.go:7073 — checkTypeNameIsReserved
void Checker::checkTypeNameIsReserved(Node* name, const DiagnosticMessage* message) {
	// TS 1.0 spec (April 2014): 3.6.1
	// The predefined type keywords are reserved and cannot be used as names of user defined types.
	std::string scratch;
	std::string_view text = name->textView(scratch);
	using namespace std::literals;
	if (text == "any"sv || text == "unknown"sv || text == "never"sv || text == "number"sv ||
		text == "bigint"sv || text == "boolean"sv || text == "string"sv || text == "symbol"sv ||
		text == "void"sv || text == "object"sv || text == "undefined"sv) {
		error(name, message, std::string(text));
	}
}

// checker.go:7082 — checkExportsOnMergedDeclarations
void Checker::checkExportsOnMergedDeclarations(Node* node) {
	// If localSymbol is defined on node then node itself is exported - check is required.
	Symbol* symbol = node->localSymbol();
	if (symbol == nullptr) {
		// Local symbol is undefined => this declaration is non-exported.
		// However, symbol might contain other declarations that are exported.
		symbol = getSymbolOfDeclaration(node);
		if (symbol->exportSymbol == nullptr) {
			// This is a pure local symbol (all declarations are non-exported) - no need to check anything.
			return;
		}
	}
	// Run the check only for the first declaration in the list.
	if (getDeclarationOfKind(symbol, node->kind) != node) {
		return;
	}
	DeclarationSpaces exportedDeclarationSpaces = DeclarationSpacesNone;
	DeclarationSpaces nonExportedDeclarationSpaces = DeclarationSpacesNone;
	DeclarationSpaces defaultExportedDeclarationSpaces = DeclarationSpacesNone;
	for (Node* d : symbol->declarations) {
		DeclarationSpaces declarationSpaces = getDeclarationSpaces(d);
		ModifierFlags effectiveDeclarationFlags =
			getEffectiveDeclarationFlags(d, ModifierFlagsExport | ModifierFlagsDefault);
		if ((effectiveDeclarationFlags & ModifierFlagsExport) != 0) {
			if ((effectiveDeclarationFlags & ModifierFlagsDefault) != 0) {
				defaultExportedDeclarationSpaces |= declarationSpaces;
			} else {
				exportedDeclarationSpaces |= declarationSpaces;
			}
		} else {
			nonExportedDeclarationSpaces |= declarationSpaces;
		}
	}
	// Spaces for anything not declared a 'default export'.
	DeclarationSpaces nonDefaultExportedDeclarationSpaces =
		exportedDeclarationSpaces | nonExportedDeclarationSpaces;
	DeclarationSpaces commonDeclarationSpacesForExportsAndLocals =
		exportedDeclarationSpaces & nonExportedDeclarationSpaces;
	DeclarationSpaces commonDeclarationSpacesForDefaultAndNonDefault =
		defaultExportedDeclarationSpaces & nonDefaultExportedDeclarationSpaces;
	if (commonDeclarationSpacesForExportsAndLocals != 0 ||
		commonDeclarationSpacesForDefaultAndNonDefault != 0) {
		// declaration spaces for exported and non-exported declarations intersect
		for (Node* d : symbol->declarations) {
			DeclarationSpaces declarationSpaces = getDeclarationSpaces(d);
			Node* name = getNameOfDeclaration(d);
			// Only error on the declarations that contributed to the intersecting spaces.
			if ((declarationSpaces & commonDeclarationSpacesForDefaultAndNonDefault) != 0) {
				error(name, Merged_declaration_0_cannot_include_a_default_export_declaration_Consider_adding_a_separate_export_default_0_declaration_instead,
					declarationNameToString(name));
			} else if ((declarationSpaces & commonDeclarationSpacesForExportsAndLocals) != 0) {
				error(name, Individual_declarations_in_merged_declaration_0_must_be_all_exported_or_all_local,
					declarationNameToString(name));
			}
		}
	}
}

// checker.go:7133 — getDeclarationSpaces
DeclarationSpaces Checker::getDeclarationSpaces(Node* node) {
	switch (node->kind) {
	case Kind::InterfaceDeclaration:
	case Kind::TypeAliasDeclaration:
	case Kind::JSTypeAliasDeclaration:
	case Kind::JSDocTypedefTag:
	case Kind::JSDocCallbackTag:
		return DeclarationSpacesExportType;
	case Kind::ModuleDeclaration:
		if (isAmbientModule(node) || getModuleInstanceState(node) != ModuleInstanceState::NonInstantiated) {
			return DeclarationSpacesExportNamespace | DeclarationSpacesExportValue;
		}
		return DeclarationSpacesExportNamespace;
	case Kind::ClassDeclaration:
	case Kind::EnumDeclaration:
	case Kind::EnumMember:
		return DeclarationSpacesExportType | DeclarationSpacesExportValue;
	case Kind::SourceFile:
		return DeclarationSpacesExportType | DeclarationSpacesExportValue |
			DeclarationSpacesExportNamespace;
	case Kind::ExportAssignment:
	case Kind::BinaryExpression: {
		Node* expression;
		if (isExportAssignment(node)) {
			expression = node->expression();
		} else {
			expression = node->as<BinaryExpression>()->Right;
		}
		// Export assigned entity name expressions act as aliases and should fall through, otherwise they export values.
		if (!isEntityNameExpression(expression) ||
			(getSymbolOfDeclaration(node)->flags & SymbolFlagsAlias) == 0) {
			return DeclarationSpacesExportValue;
		}
	}
		[[fallthrough]];
	case Kind::ImportEqualsDeclaration:
	case Kind::NamespaceImport:
	case Kind::ImportClause: {
		DeclarationSpaces result = DeclarationSpacesNone;
		Symbol* target = resolveAlias(getSymbolOfDeclaration(node));
		for (Node* d : target->declarations) {
			result |= getDeclarationSpaces(d);
		}
		return result;
	}
	case Kind::VariableDeclaration:
	case Kind::BindingElement:
	case Kind::FunctionDeclaration:
	case Kind::ImportSpecifier:
		return DeclarationSpacesExportValue;
	case Kind::MethodSignature:
	case Kind::PropertySignature:
		return DeclarationSpacesExportType;
	default:
		TSC_UNREACHABLE("Unhandled case in getDeclarationSpaces");
	}
}

// checker.go:7174 — checkTypeParameters
void Checker::checkTypeParameters(std::span<Node* const> typeParameterDeclarations) {
	bool seenDefault = false;
	for (size_t i = 0; i < typeParameterDeclarations.size(); i++) {
		Node* node = typeParameterDeclarations[i];
		checkTypeParameter(node);
		Node* defaultTypeNode = node->as<TypeParameterDeclaration>()->DefaultType;
		if (defaultTypeNode != nullptr) {
			seenDefault = true;
			checkTypeParametersNotReferenced(defaultTypeNode, typeParameterDeclarations, i);
		} else if (seenDefault) {
			error(node, Required_type_parameters_may_not_follow_optional_type_parameters);
		}
		for (size_t j = 0; j < i; j++) {
			if (typeParameterDeclarations[j]->symbol() == node->symbol()) {
				error(node->name(), Duplicate_identifier_0,
					declarationNameToString(node->name()));
			}
		}
	}
}

// Check that type parameter defaults only reference previously declared type parameters */
// checker.go:7194 — checkTypeParametersNotReferenced
void Checker::checkTypeParametersNotReferenced(Node* root,
                                               std::span<Node* const> typeParameters,
                                               size_t index) {
	std::function<bool(Node*)> visit = [&](Node* node) -> bool {
		if (isTypeReferenceNode(node)) {
			Type* t = getTypeFromTypeReference(node);
			if ((t->flags & TypeFlagsTypeParameter) != 0) {
				for (size_t i = index; i < typeParameters.size(); i++) {
					if (t->symbol == getSymbolOfDeclaration(typeParameters[i])) {
						error(node, Type_parameter_defaults_can_only_reference_previously_declared_type_parameters);
					}
				}
			}
		}
		return node->forEachChild(visit);
	};
	visit(root);
}

// checker.go:7212 — registerForUnusedIdentifiersCheck
void Checker::registerForUnusedIdentifiersCheck(Node* node) {
	SourceFile* sourceFile = getSourceFileOfNode(node);
	SourceFileLinks* links = sourceFileLinks.Get(sourceFile);
	links->identifierCheckNodes.push_back(node);
}

// checker.go:7218 — checkUnusedIdentifiers
void Checker::checkUnusedIdentifiers(const std::vector<Node*>& potentiallyUnusedIdentifiers) {
	for (Node* node : potentiallyUnusedIdentifiers) {
		switch (node->kind) {
		case Kind::ClassDeclaration:
		case Kind::ClassExpression:
			checkUnusedClassMembers(node);
			checkUnusedTypeParameters(node);
			break;
		case Kind::SourceFile:
		case Kind::ModuleDeclaration:
		case Kind::Block:
		case Kind::CaseBlock:
		case Kind::ForStatement:
		case Kind::ForInStatement:
		case Kind::ForOfStatement:
		case Kind::ClassStaticBlockDeclaration:
			checkUnusedLocalsAndParameters(node);
			break;
		case Kind::Constructor:
		case Kind::FunctionExpression:
		case Kind::FunctionDeclaration:
		case Kind::ArrowFunction:
		case Kind::MethodDeclaration:
		case Kind::GetAccessor:
		case Kind::SetAccessor:
			// Only report unused parameters on the implementation, not overloads.
			if (node->body() != nullptr) {
				checkUnusedLocalsAndParameters(node);
			}
			checkUnusedTypeParameters(node);
			break;
		case Kind::MethodSignature:
		case Kind::CallSignature:
		case Kind::ConstructSignature:
		case Kind::FunctionType:
		case Kind::ConstructorType:
		case Kind::TypeAliasDeclaration:
		case Kind::JSTypeAliasDeclaration:
		case Kind::InterfaceDeclaration:
			checkUnusedTypeParameters(node);
			break;
		case Kind::InferType:
			checkUnusedInferTypeParameter(node);
			break;
		default:
			TSC_UNREACHABLE("Unhandled case in checkUnusedIdentifiers");
		}
	}
}

// checker.go:7245 — isReferenced
bool Checker::isReferenced(Symbol* symbol) {
	return symbolReferenceLinks.Get(symbol)->referenceKinds != 0;
}

// checker.go:7249 — UnusedKind
// (declared in checker.h — slice block)

// checker.go:7256 — reportUnusedVariable
void Checker::reportUnusedVariable(Node* location, Diagnostic* diagnostic) {
	while (isBindingElement(location) || isBindingPattern(location)) {
		location = location->parent;
	}
	reportUnused(location,
		ifElse(isParameterDeclaration(location), UnusedKind::Parameter, UnusedKind::Local),
		diagnostic);
}

// checker.go:7263 — reportUnused
void Checker::reportUnused(Node* location, UnusedKind kind, Diagnostic* diagnostic) {
	if ((location->flags & (NodeFlagsAmbient | NodeFlagsThisNodeOrAnySubNodesHasError)) == 0) {
		bool isError = unusedIsError(kind);
		if (isError) {
			addDiagnostic(diagnostic);
		} else {
			Diagnostic* suggestion = new Diagnostic(*diagnostic);
			suggestion->SetCategory(DiagnosticCategory::Suggestion);
			addSuggestionDiagnostic(suggestion);
		}
	}
}

// checker.go:7276 — unusedIsError
bool Checker::unusedIsError(UnusedKind kind) {
	switch (kind) {
	case UnusedKind::Local:
		return tristateIsTrue(compilerOptions->NoUnusedLocals);
	case UnusedKind::Parameter:
		return tristateIsTrue(compilerOptions->NoUnusedParameters);
	default:
		TSC_UNREACHABLE("Unhandled case in unusedIsError");
	}
}

// checker.go:7289 — checkUnusedClassMembers
void Checker::checkUnusedClassMembers(Node* node) {
	for (Node* member : node->members()) {
		switch (member->kind) {
		case Kind::MethodDeclaration:
		case Kind::PropertyDeclaration:
		case Kind::GetAccessor:
		case Kind::SetAccessor:
			if (isSetAccessorDeclaration(member) &&
				(member->symbol()->flags & SymbolFlagsGetAccessor) != 0) {
				break;  // Already would have reported an error on the getter.
			}
			{
				Symbol* symbol = getSymbolOfDeclaration(member);
				if (!isReferenced(symbol) &&
					(hasModifier(member, ModifierFlagsPrivate) ||
					 (member->name() != nullptr && isPrivateIdentifier(member->name()))) &&
					(member->flags & NodeFlagsAmbient) == 0) {
					reportUnused(member, UnusedKind::Local,
						NewDiagnosticForNode(member->name(),
							X_0_is_declared_but_its_value_is_never_read,
							{symbolToString(symbol)}));
				}
			}
			break;
		case Kind::Constructor:
			for (Node* parameter : member->as<ConstructorDeclaration>()->Parameters->nodes) {
				if (!isReferenced(parameter->symbol()) &&
					hasSyntacticModifier(parameter, ModifierFlagsPrivate)) {
					reportUnused(parameter, UnusedKind::Local,
						NewDiagnosticForNode(parameter->name(),
							Property_0_is_declared_but_its_value_is_never_read,
							{symbolName(parameter->symbol())}));
				}
			}
			break;
		case Kind::IndexSignature:
		case Kind::SemicolonClassElement:
		case Kind::ClassStaticBlockDeclaration:
		case Kind::JSTypeAliasDeclaration:
			// Can't be private
			break;
		default:
			TSC_UNREACHABLE("Unhandled case in checkUnusedClassMembers");
		}
	}
}

// checker.go:7313 — checkUnusedLocalsAndParameters
void Checker::checkUnusedLocalsAndParameters(Node* node) {
	std::unordered_set<Node*> variableParents;
	std::unordered_map<Node*, std::vector<Node*>> importClauses;
	SymbolTable* locals = node->locals();
	if (locals != nullptr) {
		for (auto& entry : *locals) {
			Symbol* local = entry.second;
			SymbolFlags referenceKinds = symbolReferenceLinks.Get(local)->referenceKinds;
			if (((local->flags & SymbolFlagsTypeParameter) != 0 &&
				 ((local->flags & SymbolFlagsVariable) == 0 ||
				  (referenceKinds & SymbolFlagsVariable) != 0)) ||
				((local->flags & SymbolFlagsTypeParameter) == 0 &&
				 (referenceKinds != 0 || local->exportSymbol != nullptr ||
				  (local->flags & SymbolFlagsModuleExports) != 0))) {
				continue;
			}
			for (Node* declaration : local->declarations) {
				if (isVariableDeclaration(declaration) || isParameterDeclaration(declaration) ||
					isBindingElement(declaration)) {
					variableParents.insert(getRootDeclaration(declaration)->parent);
				} else if (isImportClause(declaration) || isImportSpecifier(declaration) ||
						   isNamespaceImport(declaration)) {
					if (!isIdentifierThatStartsWithUnderscore(declaration->name())) {
						Node* importClause = importClauseFromImported(declaration);
						importClauses[importClause].push_back(declaration);
					}
				} else {
					if (!isTypeParameterDeclaration(declaration) && !isAmbientModule(declaration)) {
						reportUnusedLocal(declaration, symbolName(local));
					}
				}
			}
		}
	}
	for (Node* declaration : variableParents) {
		if (isVariableDeclarationList(declaration)) {
			reportUnusedVariables(declaration);
		} else {
			reportUnusedParameters(declaration);
		}
	}
	for (auto& entry : importClauses) {
		reportUnusedImports(entry.first, entry.second);
	}
}

// checker.go:7355 — reportUnusedLocal
void Checker::reportUnusedLocal(Node* node, const std::string& name) {
	const DiagnosticMessage* message = ifElse(isTypeDeclaration(node),
		(const DiagnosticMessage*)X_0_is_declared_but_never_used,
		(const DiagnosticMessage*)X_0_is_declared_but_its_value_is_never_read);
	reportUnused(node, UnusedKind::Local,
		NewDiagnosticForNode(orElse(node->name(), node), message, {name}));
}

// checker.go:7360 — reportUnusedVariables
void Checker::reportUnusedVariables(Node* node) {
	std::vector<Node*> declarations = node->as<VariableDeclarationList>()->Declarations->nodes;
	if (declarations.size() > 1 &&
		every(declarations, [this](Node* d) { return isUnreferencedVariableDeclaration(d); })) {
		reportUnusedVariable(node, NewDiagnosticForNode(node, All_variables_are_unused, {}));
	} else {
		reportUnusedVariableDeclarations(declarations);
	}
}

// checker.go:7369 — reportUnusedParameters
void Checker::reportUnusedParameters(Node* node) {
	reportUnusedVariableDeclarations(node->parameters());
}

// checker.go:7373 — reportUnusedBindingElements
void Checker::reportUnusedBindingElements(Node* node) {
	auto declarations = node->elements();
	if (declarations.size() > 1 &&
		every(declarations, [this](Node* d) { return isUnreferencedVariableDeclaration(d); })) {
		reportUnusedVariable(node,
			NewDiagnosticForNode(node, All_destructured_elements_are_unused, {}));
	} else {
		reportUnusedVariableDeclarations(declarations);
	}
}

// checker.go:7382 — reportUnusedVariableDeclarations
void Checker::reportUnusedVariableDeclarations(std::vector<Node*> declarations) {
	for (Node* declaration : declarations) {
		Node* name = declaration->name();
		if (name != nullptr &&
			!isParameterPropertyDeclaration(declaration, declaration->parent) &&
			!isThisParameter(declaration)) {
			if (isBindingPattern(name)) {
				reportUnusedBindingElements(name);
			} else if (isUnreferencedVariableDeclaration(declaration)) {
				reportUnusedVariable(declaration,
					NewDiagnosticForNode(name,
						X_0_is_declared_but_its_value_is_never_read, {name->text()}));
			}
		}
	}
}

// checker.go:7396 — isUnreferencedVariableDeclaration
bool Checker::isUnreferencedVariableDeclaration(Node* node) {
	Node* name = node->name();
	if (name == nullptr) {
		return true;
	}
	if (isBindingPattern(name)) {
		return every(node->name()->elements(),
			[this](Node* d) { return isUnreferencedVariableDeclaration(d); });
	}
	if ((symbolReferenceLinks.Get(getSymbolOfDeclaration(node))->referenceKinds &
		 SymbolFlagsVariable) != 0) {
		return false;
	}
	if (isBindingElement(node) && isObjectBindingPattern(node->parent)) {
		// In `{ a, ...b }, `a` is considered used since it removes a property from `b`. `b` may still be unused though.
		Node* lastElement = lastOrNil(node->parent->elements());
		if (node != lastElement && hasDotDotDotToken(lastElement)) {
			return false;
		}
	}
	if ((isParameterDeclaration(node) ||
		 (isVariableDeclaration(node) &&
		  (isForInOrOfStatement(node->parent->parent) ||
		   (getCombinedNodeFlagsCached(node) & NodeFlagsUsing) != 0)) ||
		 (isBindingElement(node) &&
		  !(isObjectBindingPattern(node->parent) && node->propertyName() == nullptr))) &&
		isIdentifierThatStartsWithUnderscore(name)) {
		return false;
	}
	return true;
}

// checker.go:7424 — reportUnusedImports
void Checker::reportUnusedImports(Node* node, std::vector<Node*> unuseds) {
	size_t declarationCount = node->name() != nullptr ? 1 : 0;
	Node* namedBindings = node->as<ImportClause>()->NamedBindings;
	if (namedBindings != nullptr) {
		if (isNamespaceImport(namedBindings)) {
			declarationCount++;
		} else {
			declarationCount += namedBindings->elements().size();
		}
	}
	if (declarationCount > 1 && declarationCount == unuseds.size()) {
		reportUnused(node, UnusedKind::Local,
			NewDiagnosticForNode(node->parent,
				All_imports_in_import_declaration_are_unused, {}));
	} else {
		for (Node* unused : unuseds) {
			reportUnusedLocal(unused, unused->name()->text());
		}
	}
}


// checker.go:7458 — checkUnusedInferTypeParameter
void Checker::checkUnusedInferTypeParameter(Node* node) {
	Node* typeParameter = node->as<InferTypeNode>()->TypeParameter;
	if (isUnreferencedTypeParameter(typeParameter)) {
		reportUnused(node, UnusedKind::Parameter,
			NewDiagnosticForNode(typeParameter->name(),
				X_0_is_declared_but_never_used, {typeParameter->name()->text()}));
	}
}

// checker.go:7465 — checkUnusedTypeParameters
void Checker::checkUnusedTypeParameters(Node* node) {
	if (!allDeclarationsInSameSourceFile(getSymbolOfDeclaration(node))) {
		return;
	}
	NodeList* typeParameterList = node->typeParameterList();
	if (typeParameterList == nullptr) {
		return;
	}
	if (typeParameterList->nodes.size() > 1 &&
		every(typeParameterList->nodes,
			  [this](Node* d) { return isUnreferencedTypeParameter(d); })) {
		SourceFile* file = getSourceFileOfNode(node);
		TextRange loc = rangeOfTypeParameters(file, typeParameterList);
		reportUnused(node, UnusedKind::Parameter,
			newDiagnostic(file, loc, All_type_parameters_are_unused));
	} else {
		for (Node* typeParameter : typeParameterList->nodes) {
			if (isUnreferencedTypeParameter(typeParameter)) {
				reportUnused(node, UnusedKind::Parameter,
					NewDiagnosticForNode(typeParameter,
						X_0_is_declared_but_never_used,
						{typeParameter->name()->text()}));
			}
		}
	}
}

// checker.go:7486 — isUnreferencedTypeParameter
bool Checker::isUnreferencedTypeParameter(Node* typeParameter) {
	return (symbolReferenceLinks.Get(getMergedSymbol(typeParameter->symbol()))->referenceKinds &
			SymbolFlagsTypeParameter) == 0 &&
		!isIdentifierThatStartsWithUnderscore(typeParameter->name());
}

// checker.go:7491 — checkUnusedRenamedBindingElements
void Checker::checkUnusedRenamedBindingElements() {
	for (Node* node : renamedBindingElementsInTypes) {
		if (symbolReferenceLinks.Get(getSymbolOfDeclaration(node))->referenceKinds == 0) {
			Node* wrappingDeclaration = walkUpBindingElementsAndPatterns(node);
			TSC_ASSERT(isPartOfParameterDeclaration(wrappingDeclaration),
				"Only parameter declaration should be checked here");
			Diagnostic* diagnostic = NewDiagnosticForNode(node->name(),
				X_0_is_an_unused_renaming_of_1_Did_you_intend_to_use_it_as_a_type_annotation,
				{declarationNameToString(node->name()),
				 declarationNameToString(node->propertyName())});
			if (wrappingDeclaration->type() == nullptr) {
				// entire parameter does not have type annotation, suggest adding an annotation
				diagnostic->AddRelatedInfo(newDiagnostic(
					getSourceFileOfNode(wrappingDeclaration),
					TextRange{wrappingDeclaration->end(), wrappingDeclaration->end()},
					We_can_only_write_a_type_for_0_by_adding_a_type_for_the_entire_parameter_here,
					{declarationNameToString(node->propertyName())}));
			}
			addDiagnostic(diagnostic);
		}
	}
}
// (deduped: reportDiagnostic defined in checker_relater.cpp)
// IterationTypesKeyHash — declared on Checker at checker.h; defined with the
// iterationTypesCache owner (checker.go IterationTypesKey).
size_t Checker::IterationTypesKeyHash::operator()(const IterationTypesKey& k) const noexcept {
	size_t h = std::hash<uint32_t>{}(k.typeId);
	h = h * 31u + std::hash<uint32_t>{}(k.use);
	return h;
}
}  // namespace tsc::checker
