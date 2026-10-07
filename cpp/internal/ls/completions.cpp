// Port of tsc/internal/ls/completions.go — see PORTING.md for conventions.
// ErrNeedsAutoImports lives in api.cpp (api.go exports it there).

#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/astnav/tokens.h"
#include "internal/checker/checker.h"
#include "internal/collections/collections.h"
#include "internal/compiler/program.h"
#include "internal/format/format.h"
#include "internal/jsnum/jsnum.h"
#include "internal/locale/locale.h"
#include "internal/ls/ls.h"
#include "internal/ls/autoimport/autoimport.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/nodebuilder/types.h"
#include "internal/printer/printer.h"
#include "internal/scanner/scanner.h"
#include "internal/spanmap/spanmap.h"
#include "internal/stringutil/stringutil.h"
#include "internal/tspath/tspath.h"

namespace tsc::ls {

namespace {

// === file-local replicas of ast helpers not yet ported ===
// (kept verbatim-semantics; each cites its Go source)

// tspath/path.go:50 IsDynamicFileName — tspath has no port yet.
bool isDynamicFileName(std::string_view fileName) {
	return fileName.starts_with("^/");
}

// ast/utilities.go:713 IsFunctionBlock.
bool isFunctionBlock(Node* node) {
	return node != nullptr && node->kind == Kind::Block &&
	       node->parent != nullptr && isFunctionLike(node->parent);
}

// ast/utilities.go:2134 IsJSDocTag.
bool isJSDocTag(Node* node) {
	return node->kind >= KindFirstJSDocTagNode &&
	       node->kind <= KindLastJSDocTagNode;
}

// checker/utilities.go:168 IsInTypeQuery — checker has only file-local
// replicas; replicate here.
bool isInTypeQuery(Node* node) {
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

// core/compileroptions.go:490 GetNewLineKind — file-local replica.
NewLineKind getNewLineKind(std::string_view s) {
	if (s == "\r\n") {
		return NewLineKind::CarriageReturnLineFeed;
	}
	if (s == "\n") {
		return NewLineKind::LineFeed;
	}
	return NewLineKind::None;
}

// ast/utilities.go:2336 IsBreakOrContinueStatement.
bool isBreakOrContinueStatement(Node* node) {
	return nodeKindIs(node, Kind::BreakStatement, Kind::ContinueStatement);
}

// core.FindIn — first element matching f, or nil.
template <class T, class F>
T* findIn(const std::vector<T*>& list, F f) {
	for (T* v : list) {
		if (f(v)) return v;
	}
	return nullptr;
}

// checker/utilities.go:1029 isLateBoundName.
bool isLateBoundName(const std::string& name) {
	return name.size() >= 2 && name[0] == '\xfe' && name[1] == '@';
}

// checker/utilities.go:1018 IsKnownSymbol.
bool isKnownSymbol(Symbol* symbol) {
	return isLateBoundName(symbol->name);
}

// Iterates utf8.DecodeRuneInString over s (Go range-over-runes).
std::vector<char32_t> decodeUtf8Runes(std::string_view s) {
	std::vector<char32_t> out;
	size_t i = 0;
	while (i < s.size()) {
		int w = 0;
		char32_t r = decodeUtf8Rune(s.substr(i), &w);
		out.push_back(r);
		i += w;
	}
	return out;
}

// ast/utilities.go:3077 — ast.IsVariableLike.
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

// ast/utilities.go:3086 — ast.HasInitializer.
bool hasInitializer(Node* node) {
	switch (node->kind) {
	case Kind::VariableDeclaration:
	case Kind::Parameter:
	case Kind::BindingElement:
	case Kind::PropertyDeclaration:
	case Kind::PropertyAssignment:
	case Kind::EnumMember:
	case Kind::ForStatement:
	case Kind::ForInStatement:
	case Kind::ForOfStatement:
	case Kind::JsxAttribute:
		return node->initializer() != nullptr;
	default:
		return false;
	}
}

// ast/utilities.go:3106 — ast.GetTypeAnnotationNode.
Node* getTypeAnnotationNode(Node* node) {
	switch (node->kind) {
	case Kind::VariableDeclaration:
	case Kind::Parameter:
	case Kind::PropertySignature:
	case Kind::PropertyDeclaration:
	case Kind::TypePredicate:
	case Kind::ParenthesizedType:
	case Kind::TypeOperator:
	case Kind::MappedType:
	case Kind::TypeAssertionExpression:
	case Kind::AsExpression:
	case Kind::SatisfiesExpression:
	case Kind::TypeAliasDeclaration:
	case Kind::JSTypeAliasDeclaration:
	case Kind::NamedTupleMember:
	case Kind::OptionalType:
	case Kind::RestType:
	case Kind::TemplateLiteralTypeSpan:
	case Kind::JSDocTypeExpression:
	case Kind::JSDocPropertyTag:
	case Kind::JSDocNullableType:
	case Kind::JSDocNonNullableType:
	case Kind::JSDocOptionalType:
		return node->type();
	default: {
		auto funcLike = node->functionLikeData();
		if (funcLike.type != nullptr) {
			return *funcLike.type;
		}
		return nullptr;
	}
	}
}

// ast/utilities.go:2994 GetClassLikeDeclarationOfSymbol.
Node* getClassLikeDeclarationOfSymbol(Symbol* symbol) {
	return findIn(symbol->declarations, isClassLike);
}

// ast/utilities.go:3021 nodeHasKind.
bool nodeHasKind(Node* node, Kind kind) {
	if (node == nullptr) {
		return false;
	}
	return node->kind == kind;
}

// ast/utilities.go:3028 IsContextualKeyword.
bool isContextualKeyword(Kind token) {
	return KindFirstContextualKeyword <= token &&
	       token <= KindLastContextualKeyword;
}

// ast/utilities.go:3123 IsObjectTypeDeclaration.
bool isObjectTypeDeclaration(Node* node) {
	return isClassLike(node) || isInterfaceDeclaration(node) ||
	       isTypeLiteralNode(node);
}

// ast/utilities.go:3127 IsClassOrTypeElement.
bool isClassOrTypeElement(Node* node) {
	return isClassElement(node) || isTypeElement(node);
}

// ast/utilities.go:3139 IsTypeKeywordToken.
bool isTypeKeywordToken(Node* node) { return node->kind == Kind::TypeKeyword; }

// ast/utilities.go:3801 IsInitializedProperty.
bool isInitializedProperty(Node* member) {
	return member->kind == Kind::PropertyDeclaration &&
	       member->initializer() != nullptr;
}

// ast/utilities.go:4170 IsNonContextualKeyword.
bool isNonContextualKeyword(Kind token) {
	return isKeywordKind(token) && !isContextualKeyword(token);
}

// ast/utilities.go:4525 IsTagName.
bool isTagName(Node* node) {
	return node->parent != nullptr && isJSDocTag(node->parent) &&
	       node->parent->tagName() == node;
}

// ast/utilities.go:4541 isArgumentOfElementAccessExpression.
bool isArgumentOfElementAccessExpression(Node* node) {
	return node != nullptr && node->parent != nullptr &&
	       node->parent->kind == Kind::ElementAccessExpression &&
	       node->parent->as<ElementAccessExpression>()->ArgumentExpression ==
	           node;
}

// ast/utilities.go:4533 literalIsName.
bool literalIsName(Node* node) {
	return isDeclarationName(node) ||
	       node->parent->kind == Kind::ExternalModuleReference ||
	       isArgumentOfElementAccessExpression(node) ||
	       isLiteralComputedPropertyDeclarationName(node);
}

// ast/ast.go:2852 SourceFile.GetNameTable — stores into the source file's
// nameTableOnce/nameTable fields (unordered_map<string,int32_t>).
const std::unordered_map<std::string, int32_t>& getNameTable(SourceFile* file) {
	file->nameTableOnce.run([&]() {
		file->nameTable.reserve(file->IdentifierCount);

		std::function<bool(Node*)> walk = [&](Node* node) -> bool {
			if ((isIdentifier(node) && !isTagName(node) &&
			     !node->text().empty()) ||
			    (isStringOrNumericLiteralLike(node) &&
			     literalIsName(node)) ||
			    isPrivateIdentifier(node)) {
				const std::string& text = node->text();
				if (file->nameTable.count(text)) {
					file->nameTable[text] = -1;
				} else {
					file->nameTable[text] = node->pos();
				}
			}

			node->forEachChild(walk);
			for (Node* jsdoc : node->jsDoc(file)) {
				jsdoc->forEachChild(walk);
			}
			return false;
		};
		file->forEachChild(walk);
	});
	return file->nameTable;
}

// ast/utilities.go:3291 CreateModifiersFromModifierFlags.
std::vector<Node*> createModifiersFromModifierFlags(
    ModifierFlags flags,
    const std::function<Node*(Kind)>& createModifier) {
	std::vector<Node*> result;
	if (flags & ModifierFlagsExport) {
		result.push_back(createModifier(Kind::ExportKeyword));
	}
	if (flags & ModifierFlagsAmbient) {
		result.push_back(createModifier(Kind::DeclareKeyword));
	}
	if (flags & ModifierFlagsDefault) {
		result.push_back(createModifier(Kind::DefaultKeyword));
	}
	if (flags & ModifierFlagsConst) {
		result.push_back(createModifier(Kind::ConstKeyword));
	}
	if (flags & ModifierFlagsPublic) {
		result.push_back(createModifier(Kind::PublicKeyword));
	}
	if (flags & ModifierFlagsPrivate) {
		result.push_back(createModifier(Kind::PrivateKeyword));
	}
	if (flags & ModifierFlagsProtected) {
		result.push_back(createModifier(Kind::ProtectedKeyword));
	}
	if (flags & ModifierFlagsAbstract) {
		result.push_back(createModifier(Kind::AbstractKeyword));
	}
	if (flags & ModifierFlagsStatic) {
		result.push_back(createModifier(Kind::StaticKeyword));
	}
	if (flags & ModifierFlagsOverride) {
		result.push_back(createModifier(Kind::OverrideKeyword));
	}
	if (flags & ModifierFlagsReadonly) {
		result.push_back(createModifier(Kind::ReadonlyKeyword));
	}
	if (flags & ModifierFlagsAccessor) {
		result.push_back(createModifier(Kind::AccessorKeyword));
	}
	if (flags & ModifierFlagsAsync) {
		result.push_back(createModifier(Kind::AsyncKeyword));
	}
	if (flags & ModifierFlagsIn) {
		result.push_back(createModifier(Kind::InKeyword));
	}
	if (flags & ModifierFlagsOut) {
		result.push_back(createModifier(Kind::OutKeyword));
	}
	return result;
}

// ast/utilities.go:3352 ReplaceModifiers.
Node* replaceModifiers(NodeFactory* factory, Node* node,
                       ModifierList* modifierArray) {
	switch (node->kind) {
	case Kind::TypeParameter:
		return factory->updateTypeParameterDeclaration(
		    node->as<TypeParameterDeclaration>(), modifierArray,
		    node->name(), node->as<TypeParameterDeclaration>()->Constraint,
		    node->as<TypeParameterDeclaration>()->Expression,
		    node->as<TypeParameterDeclaration>()->DefaultType);
	case Kind::Parameter:
		return factory->updateParameterDeclaration(
		    node->as<ParameterDeclaration>(), modifierArray,
		    node->as<ParameterDeclaration>()->DotDotDotToken, node->name(),
		    node->questionToken(), node->type(), node->initializer());
	case Kind::ConstructorType:
		return factory->updateConstructorTypeNode(
		    node->as<ConstructorTypeNode>(), modifierArray,
		    node->typeParameterList(), node->parameterList(), node->type());
	case Kind::PropertySignature:
		return factory->updatePropertySignatureDeclaration(
		    node->as<PropertySignatureDeclaration>(), modifierArray,
		    node->name(), node->postfixToken(), node->type(),
		    node->initializer());
	case Kind::PropertyDeclaration:
		return factory->updatePropertyDeclaration(
		    node->as<PropertyDeclaration>(), modifierArray, node->name(),
		    node->postfixToken(), node->type(), node->initializer());
	case Kind::MethodSignature:
		return factory->updateMethodSignatureDeclaration(
		    node->as<MethodSignatureDeclaration>(), modifierArray,
		    node->name(), node->postfixToken(), node->typeParameterList(),
		    node->parameterList(), node->type());
	case Kind::MethodDeclaration:
		return factory->updateMethodDeclaration(
		    node->as<MethodDeclaration>(), modifierArray,
		    node->as<MethodDeclaration>()->AsteriskToken, node->name(),
		    node->postfixToken(), node->typeParameterList(),
		    node->parameterList(), node->type(),
		    node->as<MethodDeclaration>()->FullSignature, node->body());
	case Kind::Constructor:
		return factory->updateConstructorDeclaration(
		    node->as<ConstructorDeclaration>(), modifierArray,
		    node->typeParameterList(), node->parameterList(), node->type(),
		    node->as<ConstructorDeclaration>()->FullSignature,
		    node->body());
	case Kind::GetAccessor:
		return factory->updateGetAccessorDeclaration(
		    node->as<GetAccessorDeclaration>(), modifierArray,
		    node->name(), node->typeParameterList(), node->parameterList(),
		    node->type(),
		    node->as<GetAccessorDeclaration>()->FullSignature,
		    node->body());
	case Kind::SetAccessor:
		return factory->updateSetAccessorDeclaration(
		    node->as<SetAccessorDeclaration>(), modifierArray,
		    node->name(), node->typeParameterList(), node->parameterList(),
		    node->type(),
		    node->as<SetAccessorDeclaration>()->FullSignature,
		    node->body());
	case Kind::IndexSignature:
		return factory->updateIndexSignatureDeclaration(
		    node->as<IndexSignatureDeclaration>(), modifierArray,
		    node->parameterList(), node->type());
	case Kind::FunctionExpression:
		return factory->updateFunctionExpression(
		    node->as<FunctionExpression>(), modifierArray,
		    node->as<FunctionExpression>()->AsteriskToken, node->name(),
		    node->typeParameterList(), node->parameterList(), node->type(),
		    node->as<FunctionExpression>()->FullSignature, node->body());
	case Kind::ArrowFunction:
		return factory->updateArrowFunction(
		    node->as<ArrowFunction>(), modifierArray,
		    node->typeParameterList(), node->parameterList(), node->type(),
		    node->as<ArrowFunction>()->FullSignature,
		    node->as<ArrowFunction>()->EqualsGreaterThanToken,
		    node->body());
	case Kind::ClassExpression:
		return factory->updateClassExpression(
		    node->as<ClassExpression>(), modifierArray, node->name(),
		    node->typeParameterList(),
		    node->as<ClassExpression>()->HeritageClauses,
		    node->memberList());
	case Kind::VariableStatement:
		return factory->updateVariableStatement(
		    node->as<VariableStatement>(), modifierArray,
		    node->as<VariableStatement>()->DeclarationList);
	case Kind::FunctionDeclaration:
		return factory->updateFunctionDeclaration(
		    node->as<FunctionDeclaration>(), modifierArray,
		    node->as<FunctionDeclaration>()->AsteriskToken, node->name(),
		    node->typeParameterList(), node->parameterList(), node->type(),
		    node->as<FunctionDeclaration>()->FullSignature,
		    node->body());
	case Kind::ClassDeclaration:
		return factory->updateClassDeclaration(
		    node->as<ClassDeclaration>(), modifierArray, node->name(),
		    node->typeParameterList(),
		    node->as<ClassDeclaration>()->HeritageClauses,
		    node->memberList());
	case Kind::InterfaceDeclaration:
		return factory->updateInterfaceDeclaration(
		    node->as<InterfaceDeclaration>(), modifierArray, node->name(),
		    node->typeParameterList(),
		    node->as<InterfaceDeclaration>()->HeritageClauses,
		    node->memberList());
	case Kind::TypeAliasDeclaration:
		return factory->updateTypeAliasDeclaration(
		    node->as<TypeAliasDeclaration>(), modifierArray, node->name(),
		    node->typeParameterList(), node->type());
	case Kind::EnumDeclaration:
		return factory->updateEnumDeclaration(
		    node->as<EnumDeclaration>(), modifierArray, node->name(),
		    node->memberList());
	case Kind::ModuleDeclaration:
		return factory->updateModuleDeclaration(
		    node->as<ModuleDeclaration>(), modifierArray,
		    node->as<ModuleDeclaration>()->Keyword, node->name(),
		    node->attributes(), node->body());
	case Kind::ImportEqualsDeclaration:
		return factory->updateImportEqualsDeclaration(
		    node->as<ImportEqualsDeclaration>(), modifierArray,
		    node->isTypeOnly(), node->name(),
		    node->as<ImportEqualsDeclaration>()->ModuleReference);
	case Kind::ImportDeclaration:
		return factory->updateImportDeclaration(
		    node->as<ImportDeclaration>(), modifierArray,
		    node->importClause(), node->moduleSpecifier(),
		    node->as<ImportDeclaration>()->Attributes);
	case Kind::ExportAssignment:
		return factory->updateExportAssignment(
		    node->as<ExportAssignment>(), modifierArray,
		    node->as<ExportAssignment>()->IsExportEquals, node->type(),
		    node->expression());
	case Kind::ExportDeclaration:
		return factory->updateExportDeclaration(
		    node->as<ExportDeclaration>(), modifierArray,
		    node->isTypeOnly(),
		    node->as<ExportDeclaration>()->ExportClause,
		    node->moduleSpecifier(),
		    node->as<ExportDeclaration>()->Attributes);
	}
	TSC_UNREACHABLE(
	    "Node that does not have modifiers tried to have modifier replaced");
}

// ast/utilities.go:4203 TryGetImportFromModuleSpecifier — canonical in ast/utilities.cpp.
// RAII guard for the `done` callback returned by GetTypeCheckerForFile —
// Go `defer done()`.
struct DoneGuard {
	std::function<void()> fn;
	~DoneGuard() { fn(); }
};

// core/core.go — generic slice helpers (per-file convention; same idiom as
// utilities.cpp / checker.cpp).

template <class T, class F>
bool someList(const std::vector<T>& ts, F&& f) {
	for (const T& t : ts) {
		if (f(t)) {
			return true;
		}
	}
	return false;
}

template <class T, class F>
bool everyList(const std::vector<T>& ts, F&& f) {
	for (const T& t : ts) {
		if (!f(t)) {
			return false;
		}
	}
	return true;
}

template <class T, class F>
T findIn(const std::vector<T>& ts, F&& f) {
	for (const T& t : ts) {
		if (f(t)) {
			return t;
		}
	}
	return T{};
}

template <class T, class F>
std::vector<T> filterList(const std::vector<T>& ts, F&& f) {
	std::vector<T> out;
	for (const T& t : ts) {
		if (f(t)) {
			out.push_back(t);
		}
	}
	return out;
}

template <class T, class F>
auto mapList(const std::vector<T>& ts, F&& f)
    -> std::vector<std::invoke_result_t<F, T>> {
	std::vector<std::invoke_result_t<F, T>> out;
	out.reserve(ts.size());
	for (const T& t : ts) {
		out.push_back(f(t));
	}
	return out;
}

// core.FirstNonNil
template <class T, class F>
std::invoke_result_t<F, T> firstNonNil(const std::vector<T>& ts, F&& f) {
	for (const T& t : ts) {
		if (auto v = f(t); v != nullptr) {
			return v;
		}
	}
	return nullptr;
}

// core.MapNonNil — slice→slice variant returning the mapped non-nil values.
template <class T, class F>
auto mapNonNil(const std::vector<T>& ts, F&& f)
    -> std::vector<std::invoke_result_t<F, T>> {
	std::vector<std::invoke_result_t<F, T>> out;
	for (const T& t : ts) {
		auto v = f(t);
		if (v != std::invoke_result_t<F, T>{}) {
			out.push_back(v);
		}
	}
	return out;
}

// core.FirstOrNil
template <class T>
T firstOrNil(const std::vector<T>& ts) {
	return ts.empty() ? T{} : ts.front();
}

// core.CheckEachDefined — debug-only check; returns the slice.
template <class T>
const std::vector<T*>& checkEachDefined(const std::vector<T*>& s,
                                        const char* msg) {
	for (auto* x : s) {
		TSC_ASSERT(x != nullptr, msg);
	}
	return s;
}

// core.SingleElementSlice
template <class T>
std::vector<T*> singleElementSlice(T* element) {
	if (element == nullptr) {
		return {};
	}
	return {element};
}

// core.OrElse (pointer variant)
template <class T>
T* orElse(T* value, T* defaultValue) {
	return value != nullptr ? value : defaultValue;
}

} // namespace

// completions.go:122 ensureItemData — forward decl (defined below).
lsproto::CompletionList* ensureItemData(
    SourceFile* file, int pos, lsproto::CompletionList* list);

// --- completions.go:37 ProvideCompletion ---

std::pair<lsproto::CompletionResponse, gostd::Error>
LanguageService::ProvideCompletion(const ContextPtr& ctx,
                                   const lsproto::DocumentUri& documentURI,
                                   lsproto::Position LSPPosition,
                                   lsproto::CompletionContext* context) {
	auto [program, file] = getProgramAndFile(documentURI);
	std::string* triggerCharacter = nullptr;
	if (context != nullptr && context->TriggerCharacter.has_value()) {
		triggerCharacter = &*context->TriggerCharacter;
	}
	format::FormatRequestContext formatCtx = format::WithFormatCodeSettings(
	    format::FormatRequestContext{}, FormatOptions(),
	    FormatOptions().NewLineCharacter);
	(void)formatCtx; // attached ctx flows only to format calls (see Go)
	auto positions = converters->FromLSPPositionForSourceFile(
	    file, LSPPosition, spanmap::FeatureCompletion);
	if (positions.empty() || !positions[0].Fidelity.IsExact()) {
		// In a content-mapped file the cursor is outside a verbatim span, so any
		// completion committed here could not be applied to the original text.
		// Offer nothing rather than edits at a bogus location.
		return {lsproto::CompletionItemsOrListOrNull{}, nullptr};
	}
	file = positions[0].Script;
	int position = int(positions[0].Position);
	auto [completionListInternal, err] = getCompletionsAtPosition(
	    ctx, file, position, triggerCharacter, /*includeSymbols*/ false);
	if (err != nullptr) {
		return {lsproto::CompletionItemsOrListOrNull{}, err};
	}
	// Go allows a nil-receiver method call (`completionListInternal.toLSP()`
	// where getCompletionsAtPosition returned nil, nil). In C++ calling a
	// member function on a null pointer is UB and optimizers may elide
	// toLSP's `this == nullptr` guard, so null-check at the call site.
	lsproto::CompletionList* completionList = ensureItemData(
	    file, position,
	    completionListInternal != nullptr ? completionListInternal->toLSP()
	                                      : nullptr);
	if (file->SpanMap() != nullptr) {
		filterContentMappedAutoImports(ctx, program, file, completionList);
	}
	lsproto::CompletionItemsOrListOrNull resp;
	resp.List = std::shared_ptr<lsproto::CompletionList>(completionList);
	return {resp, nullptr};
}

// --- completions.go:78 filterContentMappedAutoImports ---

void LanguageService::filterContentMappedAutoImports(
    const ContextPtr& ctx, compiler::SimpleProgram* program, SourceFile* file,
    lsproto::CompletionList* list) {
	if (list == nullptr) {
		return;
	}
	std::vector<std::shared_ptr<lsproto::CompletionItem>> filtered;
	filtered.reserve(list->Items ? list->Items->size() : 0);
	for (auto& item : list->Items.value_or(
	         std::vector<std::shared_ptr<lsproto::CompletionItem>>{})) {
		if (item->Data == nullptr || item->Data->AutoImport == nullptr) {
			filtered.push_back(item);
			continue;
		}
		autoimport::Fix fix;
		fix.AutoImportFix = item->Data->AutoImport.get();
		auto [edits, description, ok] =
		    fix.Edits(gostd::contextBackground(), file, program->Options(),
		              FormatOptions(), converters, UserPreferences());
		if (!ok) {
			continue;
		}
		item->AdditionalTextEdits =
		    std::make_shared<lsproto::Slice<std::shared_ptr<lsproto::TextEdit>>>(
		        edits);
		item->Detail = description;
		filtered.push_back(item);
	}
	list->Items = std::move(filtered);
}

// --- completions.go:106 GetCompletionsAtPosition ---

std::pair<CompletionList*, gostd::Error>
LanguageService::GetCompletionsAtPosition(const ContextPtr& ctx,
                                          SourceFile* file, int position,
                                          std::string* triggerCharacter,
                                          bool includeSymbols) {
	return getCompletionsAtPosition(ctx, file, position, triggerCharacter,
	                                includeSymbols);
}

// completions.go:110 `type CompletionItem` — declared in ls.h.
// completions.go:122 ensureItemData.

lsproto::CompletionList* ensureItemData(SourceFile* file, int pos,
                                        lsproto::CompletionList* list) {
	if (list == nullptr) {
		return nullptr;
	}
	for (auto& item : list->Items.value_or(
	         std::vector<std::shared_ptr<lsproto::CompletionItem>>{})) {
		if (item->Data == nullptr) {
			item->Data = std::make_shared<lsproto::CompletionItemData>(
			    lsproto::CompletionItemData{
			        .FileName = file->OriginalFileName(),
			        .Position = int32_t(pos),
			        .SupplementalFileIndex = supplementalFileIndex(file),
			        .Name = item->Label,
			    });
		}
	}
	return list;
}

// completions.go:139 supplementalFileIndex.

std::optional<int32_t> supplementalFileIndex(SourceFile* file) {
	SourceFile* canonical = file->CanonicalSourceFile();
	if (canonical == nullptr) {
		return std::nullopt;
	}
	auto* supplemental = canonical->SupplementalSourceFiles();
	for (size_t i = 0; i < supplemental->size(); i++) {
		if ((*supplemental)[i] == file) {
			return int32_t(i);
		}
	}
	TSC_UNREACHABLE(
	    "supplemental source file is not linked from its canonical source file");
}

// completions.go:152 sourceFileForSupplementalFileIndex.

SourceFile* sourceFileForSupplementalFileIndex(
    SourceFile* file, const std::optional<int32_t>& index) {
	if (!index.has_value()) {
		return file;
	}
	auto* supplemental = file->SupplementalSourceFiles();
	if (*index >= 0 && int64_t(*index) < int64_t(supplemental->size())) {
		return (*supplemental)[*index];
	}
	return nullptr;
}

// completions.go:239 keywordFiltersFromSyntaxKind.

KeywordCompletionFilters keywordFiltersFromSyntaxKind(
    Kind keywordCompletion) {
	switch (keywordCompletion) {
	case Kind::TypeKeyword:
		return KeywordCompletionFiltersTypeKeyword;
	default:
		TSC_UNREACHABLE("Unknown mapping from ast.Kind to "
		                "KeywordCompletionFilters");
	}
}

// completions.go:283 DeprecateSortText.
SortText DeprecateSortText(const SortText& original) {
	return "z" + original;
}

// completions.go:287 ObjectLiteralPropertySortText.
SortText ObjectLiteralPropertySortText(const SortText& presetSortText,
                                       const std::string& symbolDisplayName) {
	return presetSortText + "\x00" + symbolDisplayName + "\x00";
}

// completions.go:291 SortBelow.
SortText SortBelow(const SortText& original) { return original + "1"; }

// completions.go:316 symbolOriginInfo::symbolName.
std::string symbolOriginInfo::symbolName() {
	if (auto* d =
	        std::get_if<symbolOriginInfoComputedPropertyName*>(&data)) {
		return (*d)->symbolName;
	}
	TSC_UNREACHABLE("symbolOriginInfo: unknown data type for symbolName()");
}

// completions.go:331 symbolOriginInfo::asObjectLiteralMethod.
symbolOriginInfoObjectLiteralMethod* symbolOriginInfo::asObjectLiteralMethod() {
	return std::get<symbolOriginInfoObjectLiteralMethod*>(data);
}

// completions.go:384 CompletionList::toLSP.

lsproto::CompletionList* CompletionList::toLSP() const {
	if (this == nullptr) {
		return nullptr;
	}
	auto* items = new lsproto::CompletionList();
	items->IsIncomplete = IsIncomplete;
	items->ItemDefaults = std::shared_ptr<lsproto::CompletionItemDefaults>(ItemDefaults);
	items->ApplyKind = std::shared_ptr<lsproto::CompletionItemApplyKinds>(ApplyKind);
	items->Items.emplace();
	items->Items->reserve(Items.size());
	for (auto* entry : Items) {
		if (entry != nullptr && entry->completionItem != nullptr) {
			items->Items->push_back(std::shared_ptr<lsproto::CompletionItem>(
			    entry->completionItem));
		}
	}
	return items;
}

// --- completions.go:402 getCompletionsAtPosition ---

std::pair<CompletionList*, gostd::Error>
LanguageService::getCompletionsAtPosition(const ContextPtr& ctx,
                                          SourceFile* file, int position,
                                          std::string* triggerCharacter,
                                          bool includeSymbols) {
	auto [contextToken_, previousToken] = getRelevantTokens(position, file);
	(void)contextToken_;
	if (triggerCharacter != nullptr &&
	    !IsInString(file, position, previousToken) &&
	    !isValidTrigger(file, *triggerCharacter, previousToken, position)) {
		return {nullptr, nullptr};
	}

	if (triggerCharacter != nullptr && *triggerCharacter == " ") {
		// `isValidTrigger` ensures we are at `import |`
		if (tristateIsTrue(
		        UserPreferences().IncludeCompletionsForImportStatements)) {
			return {newPtr(CompletionList{.IsIncomplete = true}), nullptr};
		}
		return {nullptr, nullptr};
	}

	if (auto* jsDocSnippetCompletion =
	        getJSDocSnippetCompletion(ctx, file, position);
	    jsDocSnippetCompletion != nullptr) {
		return {jsDocSnippetCompletion, nullptr};
	}

	const CompilerOptions* compilerOptions = GetProgram()->Options();

	// !!! see if incomplete completion list and continue or clean

	auto [checker, done] =
	    GetProgram()->GetTypeCheckerForFile(ctx, file);
	DoneGuard doneGuard{done};

	CompletionList* stringCompletions = getStringLiteralCompletions(
	    ctx, file, position, previousToken, checker, compilerOptions,
	    includeSymbols);
	if (stringCompletions != nullptr) {
		return {stringCompletions, nullptr};
	}

	if (previousToken != nullptr &&
	    (previousToken->kind == Kind::BreakKeyword ||
	     previousToken->kind == Kind::ContinueKeyword ||
	     previousToken->kind == Kind::Identifier) &&
	    isBreakOrContinueStatement(previousToken->parent)) {
		return {getLabelCompletionsAtPosition(
		            ctx, previousToken->parent, file, position,
		            getOptionalReplacementSpan(previousToken, file)),
		        nullptr};
	}

	const lsutil::UserPreferences& preferences = UserPreferences();
	auto [data, err] = getCompletionData(ctx, checker, file, position,
	                                   preferences, /*forItemResolve*/ false);
	if (err != nullptr) {
		return {nullptr, err};
	}
	if (std::holds_alternative<std::monostate>(data)) {
		return {nullptr, nullptr};
	}

	if (auto** d = std::get_if<completionDataData*>(&data)) {
		lsproto::Range* optionalReplacementSpan =
		    getOptionalReplacementSpan((*d)->location, file);
		auto [response, err2] = completionInfoFromData(
		    ctx, checker, file, compilerOptions, *d, position,
		    optionalReplacementSpan, includeSymbols);
		if (err2 != nullptr) {
			return {nullptr, err2};
		}
		return {response, nullptr};
	}
	if (auto** d = std::get_if<completionDataKeyword*>(&data)) {
		lsproto::Range* optionalReplacementSpan =
		    getOptionalReplacementSpan(previousToken, file);
		return {specificKeywordCompletionInfo(ctx, position, file,
		                                      (*d)->keywordCompletions,
		                                      (*d)->isNewIdentifierLocation,
		                                      optionalReplacementSpan),
		        nullptr};
	}
	if (std::holds_alternative<completionDataJSDocTagName*>(data)) {
		// If the current position is a jsDoc tag name, only tag names should be
		// provided for completion
		auto items = getJSDocTagNameCompletions();
		auto paramItems = getJSDocParameterCompletions(
		    ctx, file, position, checker, compilerOptions, preferences,
		    /*tagNameOnly*/ true);
		items.insert(items.end(), paramItems.begin(), paramItems.end());
		return {jsDocCompletionInfo(ctx, position, file, items), nullptr};
	}
	if (std::holds_alternative<completionDataJSDocTag*>(data)) {
		// If the current position is a jsDoc tag, only tags should be provided
		// for completion
		auto items = getJSDocTagCompletions();
		auto paramItems = getJSDocParameterCompletions(
		    ctx, file, position, checker, compilerOptions, preferences,
		    /*tagNameOnly*/ false);
		items.insert(items.end(), paramItems.begin(), paramItems.end());
		return {jsDocCompletionInfo(ctx, position, file, items), nullptr};
	}
	if (auto** d = std::get_if<completionDataJSDocParameterName*>(&data)) {
		return {jsDocCompletionInfo(ctx, position, file,
		                            getJSDocParameterNameCompletions(
		                                (*d)->tag)),
		        nullptr};
	}
	TSC_UNREACHABLE("getCompletionData() returned unexpected type");
}

// --- completions.go:528 getCompletionData ---

std::pair<completionData, gostd::Error> LanguageService::getCompletionData(
    const ContextPtr& ctx, checker::Checker* typeChecker, SourceFile* file,
    int position, const lsutil::UserPreferences& preferences,
    bool forItemResolve) {
	bool inCheckedFile = isCheckedFile(file, GetProgram()->Options());

	Node* currentToken = astnav::getTokenAtPosition(file, position);

	CommentRange* insideComment = isInComment(file, position, currentToken);

	bool insideJSDocTagTypeExpression = false;
	bool insideJsDocImportTag = false;
	bool isInSnippetScope = false;
	if (insideComment != nullptr) {
		if (hasDocComment(file, position)) {
			if (position > 0 && file->Text()[position - 1] == '@') {
				// The current position is next to the '@' sign, when no tag
				// name being provided yet. Provide a full list of tag names.
				return {completionData{new completionDataJSDocTagName{}},
				        nullptr};
			} else {
				// When completion is requested without "@", we will have check
				// to make sure that there are no comments prefix the request
				// position. We will only allow "*" and space.
				// e.g
				//   /** |c| /*
				//
				//   /**
				//     |c|
				//    */
				//
				//   /**
				//    * |c|
				//    */
				//
				//   /**
				//    *         |c|
				//    */
				int lineStart =
				    format::GetLineStartPositionForPosition(position, file);
				bool noCommentPrefix = true;
				for (char32_t r : decodeUtf8Runes(
				         std::string_view(file->Text())
				             .substr(lineStart, position - lineStart))) {
					if (!(isWhiteSpaceSingleLine(r) ||
					      r == '*' || r == '/' || r == '(' || r == ')' ||
					      r == '|')) {
						noCommentPrefix = false;
						break;
					}
				}
				if (noCommentPrefix) {
					return {completionData{new completionDataJSDocTag{}},
					        nullptr};
				}
			}
		}

		// Completion should work inside certain JSDoc tags. For example:
		//     /** @type {number | string} */
		// Completion should work in the brackets
		if (Node* tag = getJSDocTagAtPosition(currentToken, position);
		    tag != nullptr) {
			if (tag->tagName()->pos() <= position &&
			    position <= tag->tagName()->end()) {
				return {completionData{new completionDataJSDocTagName{}},
				        nullptr};
			}
			if (isJSDocImportTag(tag)) {
				insideJsDocImportTag = true;
			} else {
				if (Node* typeExpression =
				        tryGetTypeExpressionFromTag(tag);
				    typeExpression != nullptr) {
					currentToken =
					    astnav::getTokenAtPosition(file, position);
					if (currentToken == nullptr ||
					    (!isDeclarationName(currentToken) &&
					     (currentToken->parent->kind !=
					          Kind::JSDocPropertyTag ||
					      currentToken->parent->name() !=
					          currentToken))) {
						// Use as type location if inside tag's type
						// expression
						insideJSDocTagTypeExpression =
						    isCurrentlyEditingNode(typeExpression, file,
						                           position);
					}
				}
				if (!insideJSDocTagTypeExpression &&
				    isJSDocParameterTag(tag) &&
				    (nodeIsMissing(tag->name()) ||
				     (tag->name()->pos() <= position &&
				      position <= tag->name()->end()))) {
					return {completionData{
					            new completionDataJSDocParameterName{tag}},
					        nullptr};
				}
			}
		}

		if (!insideJSDocTagTypeExpression && !insideJsDocImportTag) {
			// Proceed if the current position is in JSDoc tag expression;
			// otherwise it is a normal comment or the plain text part of a
			// JSDoc comment, so no completion should be available
			return {completionData{}, nullptr};
		}
	}

	// The decision to provide completion depends on the contextToken, which is
	// determined through the previousToken. Note: 'previousToken' (and thus
	// 'contextToken') can be undefined if we are the beginning of the file.
	bool isJSOnlyLocation = !insideJSDocTagTypeExpression &&
	                      !insideJsDocImportTag && isSourceFileJS(file);
	auto relevantTokens_ = getRelevantTokens(position, file);
	Node* contextToken = relevantTokens_.first;
	Node* previousToken = relevantTokens_.second;

	// Find the node where completion is requested on. Also determine whether
	// we are trying to complete with members of that node or attributes of a
	// JSX tag.
	Node* node = currentToken;
	Node* propertyAccessToConvert = nullptr;
	bool isRightOfDot = false;
	bool isRightOfQuestionDot = false;
	bool isRightOfOpenTag = false;
	bool isStartingCloseTag = false;
	jsxInitializer jsxInitializer;
	bool isJsxIdentifierExpected = false;
	importStatementCompletionInfo* importStatementCompletion = nullptr;
	Node* location = astnav::getTouchingPropertyName(file, position);
	KeywordCompletionFilters keywordFilters = KeywordCompletionFiltersNone;
	bool isNewIdentifierLocation = false;
	// !!! flags := CompletionInfoFlagsNone
	std::optional<std::vector<std::string>> defaultCommitCharacters;

	if (contextToken != nullptr) {
		importStatementCompletionInfo iscInfo =
		    getImportStatementCompletionInfo(contextToken, file);
		if (iscInfo.keywordCompletion != Kind::Unknown) {
			if (iscInfo.isKeywordOnlyCompletion) {
				auto* ci = new CompletionItem{};
				ci->completionItem = new lsproto::CompletionItem{
				    .Label = std::string(tokenToString(iscInfo.keywordCompletion)),
				    .Kind = std::make_shared<lsproto::CompletionItemKind>(
				        lsproto::CompletionItemKindKeyword),
				    .SortText = std::string(SortTextGlobalsOrKeywords),
				};
				auto* data = new completionDataKeyword{};
				data->keywordCompletions = {ci};
				data->isNewIdentifierLocation =
				    iscInfo.isNewIdentifierLocation;
				return {completionData{data}, nullptr};
			}
			keywordFilters =
			    keywordFiltersFromSyntaxKind(iscInfo.keywordCompletion);
		}
		if (iscInfo.replacementSpan != nullptr &&
		    tristateIsTrue(
		        preferences.IncludeCompletionsForImportStatements)) {
			// !!! flags |= CompletionInfoFlags.IsImportStatementCompletion;
			importStatementCompletion =
			    new importStatementCompletionInfo(iscInfo);
			isNewIdentifierLocation =
			    iscInfo.isNewIdentifierLocation;
		}
		// Bail out if this is a known invalid completion location.
		if (iscInfo.replacementSpan == nullptr &&
		    isCompletionListBlocker(contextToken, previousToken, location,
		                            file, position, typeChecker)) {
			if (keywordFilters != KeywordCompletionFiltersNone) {
				auto [isNewIdent, _cc] =
				    computeCommitCharactersAndIsNewIdentifier(
				        contextToken, file, position);
				return {completionData{keywordCompletionData(
				            keywordFilters, isJSOnlyLocation,
				            isNewIdent)},
				        nullptr};
			}
			return {completionData{}, nullptr};
		}

		Node* parent = contextToken->parent;
		if (contextToken->kind == Kind::DotToken ||
		    contextToken->kind == Kind::QuestionDotToken) {
			isRightOfDot = contextToken->kind == Kind::DotToken;
			isRightOfQuestionDot =
			    contextToken->kind == Kind::QuestionDotToken;
			switch (parent->kind) {
			case Kind::PropertyAccessExpression: {
				propertyAccessToConvert = parent;
				node = propertyAccessToConvert->expression();
				Node* leftMostAccessExpression =
				    getLeftmostAccessExpression(parent);
				if (nodeIsMissing(leftMostAccessExpression) ||
				    ((isCallExpression(node) || isFunctionLike(node)) &&
				     node->end() == contextToken->pos() &&
				     lsutil::GetLastChild(node, file)->kind !=
				         Kind::CloseParenToken)) {
					// This is likely dot from incorrectly parsed expression and
					// user is starting to write spread
					// eg: Math.min(./**/)
					// const x = function (./**/) {}
					// ({./**/})
					return {completionData{}, nullptr};
				}
				break;
			}
			case Kind::QualifiedName:
				node = parent->as<QualifiedName>()->Left;
				break;
			case Kind::ModuleDeclaration:
				node = parent->name();
				break;
			case Kind::ImportType:
				node = parent;
				break;
			case Kind::MetaProperty:
				node = lsutil::GetFirstToken(parent, file);
				if (node->kind != Kind::ImportKeyword &&
				    node->kind != Kind::NewKeyword) {
					TSC_UNREACHABLE("Unexpected token kind");
				}
				break;
			default:
				// There is nothing that precedes the dot, so this likely just
				// a stray character or leading into a '...' token. Just bail
				// out instead.
				return {completionData{}, nullptr};
			}
		} else if (importStatementCompletion == nullptr) {
			// <UI.Test /* completion position */ />
			// If the tagname is a property access expression, we will then walk
			// up to the top most of property access expression. Then, try to
			// get a JSX container and its associated attributes type.
			if (parent != nullptr &&
			    parent->kind == Kind::PropertyAccessExpression) {
				contextToken = parent;
				parent = parent->parent;
			}

			// Fix location
			if (parent == location) {
				switch (currentToken->kind) {
				case Kind::GreaterThanToken:
					if (parent->kind == Kind::JsxElement ||
					    parent->kind == Kind::JsxOpeningElement) {
						location = currentToken;
					}
					break;
				case Kind::LessThanSlashToken:
					if (parent->kind == Kind::JsxSelfClosingElement) {
						location = currentToken;
					}
					break;
				default:
					break;
				}
			}

			switch (parent->kind) {
			case Kind::JsxClosingElement:
				if (contextToken->kind == Kind::LessThanSlashToken) {
					isStartingCloseTag = true;
					location = contextToken;
				}
				break;
			case Kind::BinaryExpression:
				if (!binaryExpressionMayBeOpenTag(
				        parent->as<BinaryExpression>())) {
					break;
				}
				[[fallthrough]];
			case Kind::JsxSelfClosingElement:
			case Kind::JsxElement:
			case Kind::JsxOpeningElement:
				isJsxIdentifierExpected = true;
				if (contextToken->kind == Kind::LessThanToken) {
					isRightOfOpenTag = true;
					location = contextToken;
				}
				break;
			case Kind::JsxExpression:
			case Kind::JsxSpreadAttribute:
				// First case is for `<div foo={true} [||] />` or
				// `<div foo={true} [||] ></div>`, `parent` will be `{true}`
				// and `previousToken` will be `}`.
				// Second case is for `<div foo={true} t[||] ></div>`.
				// Second case must not match for
				// `<div foo={undefine[||]}></div>`.
				if (previousToken->kind == Kind::CloseBraceToken ||
				    (previousToken->kind == Kind::Identifier &&
				     previousToken->parent->kind ==
				         Kind::JsxAttribute)) {
					isJsxIdentifierExpected = true;
				}
				break;
			case Kind::JsxAttribute:
				// For `<div className="x" [||] ></div>`, `parent` will be
				// JsxAttribute and `previousToken` will be its initializer.
				if (parent->initializer() == previousToken &&
				    previousToken->end() < position) {
					isJsxIdentifierExpected = true;
				} else {
					switch (previousToken->kind) {
					case Kind::EqualsToken:
						jsxInitializer.isInitializer = true;
						break;
					case Kind::Identifier:
						isJsxIdentifierExpected = true;
						// For `<div x=[|f/**/|]`, `parent` will be `x` and
						// `previousToken.parent` will be `f` (which is its own
						// JsxAttribute). Note for `<div someBool f>` we don't
						// want to treat this as a jsx initializer, instead
						// it's the attribute name.
						if (parent != previousToken->parent &&
						    parent->initializer() == nullptr &&
						    astnav::findChildOfKind(
						        parent, Kind::EqualsToken, file) != nullptr) {
							jsxInitializer.initializer =
							    previousToken;
						}
						break;
					default:
						break;
					}
				}
				break;
			default:
				break;
			}
		}
	}

	CompletionKind completionKind = CompletionKindNone;
	bool hasUnresolvedAutoImports = false;
	// This also gets mutated in nested-functions after the return
	std::vector<Symbol*> symbols;
	std::vector<autoimport::FixAndExport*> autoImports;
	// Keys are indexes of `symbols`.
	std::unordered_map<int, symbolOriginInfo*> symbolToOriginInfoMap;
	std::unordered_map<SymbolId, SortText> symbolToSortTextMap;
	collections::Set<SymbolId> seenPropertySymbols;
	bool isTypeOnlyLocation =
	    insideJSDocTagTypeExpression || insideJsDocImportTag ||
	    (importStatementCompletion != nullptr && location->parent != nullptr &&
	     isTypeOnlyImportOrExportDeclaration(location->parent)) ||
	    (!isContextTokenValueLocation(contextToken) &&
	     (isPossiblyTypeArgumentPosition(contextToken, file, typeChecker) ||
	      isPartOfTypeNode(location) ||
	      isContextTokenTypeLocation(contextToken)));

	auto addSymbolOriginInfo = [&](Symbol* symbol, bool insertQuestionDot,
	                               bool insertAwait) {
		SymbolId symbolId = getSymbolId(symbol);
		if (insertAwait && seenPropertySymbols.AddIfAbsent(symbolId)) {
			symbolToOriginInfoMap[int(symbols.size()) - 1] =
			    new symbolOriginInfo{
			        .kind = getNullableSymbolOriginInfoKind(
			            symbolOriginInfoKindPromise, insertQuestionDot)};
		} else if (insertQuestionDot) {
			symbolToOriginInfoMap[int(symbols.size()) - 1] =
			    new symbolOriginInfo{.kind = symbolOriginInfoKindNullable};
		}
	};

	auto addSymbolSortInfo = [&](Symbol* symbol) {
		SymbolId symbolId = getSymbolId(symbol);
		if (isStaticProperty(symbol)) {
			symbolToSortTextMap[symbolId] = SortTextLocalDeclarationPriority;
		}
	};

	std::function<void(Symbol*, bool, bool)> addPropertySymbol =
	    [&](Symbol* symbol, bool insertAwait, bool insertQuestionDot) {
		    // For a computed property with an accessible name like
		    // `Symbol.iterator`, we'll add a completion for the *name*
		    // `Symbol` instead of for the property. If this is e.g.
		    // [Symbol.iterator], add a completion for `Symbol`.
		    Node* computedPropertyName = firstNonNil(
		        symbol->declarations, [](Node* decl) -> Node* {
			        Node* name = getNameOfDeclaration(decl);
			        if (name != nullptr &&
			            name->kind == Kind::ComputedPropertyName) {
				        return name;
			        }
			        return nullptr;
		        });

		    if (computedPropertyName != nullptr) {
			    // The completion is for `Symbol`, not `iterator`.
			    Node* leftMostName = getLeftMostName(
			        computedPropertyName->expression());
			    Symbol* nameSymbol = nullptr;
			    if (leftMostName != nullptr) {
				    nameSymbol =
				        typeChecker->GetSymbolAtLocation(leftMostName);
			    }
			    // If this is nested like for `namespace N { export const sym
			    // = Symbol(); }`, we'll add the completion for `N`.
			    Symbol* firstAccessibleSymbol = nullptr;
			    if (nameSymbol != nullptr) {
				    firstAccessibleSymbol = getFirstSymbolInChain(
				        nameSymbol, contextToken, typeChecker);
			    }
			    SymbolId firstAccessibleSymbolId = 0;
			    if (firstAccessibleSymbol != nullptr) {
				    firstAccessibleSymbolId =
				        getSymbolId(firstAccessibleSymbol);
			    }
			    if (firstAccessibleSymbolId != 0 &&
			        seenPropertySymbols.AddIfAbsent(
			            firstAccessibleSymbolId)) {
				    symbols.push_back(firstAccessibleSymbol);
				    symbolToSortTextMap[firstAccessibleSymbolId] =
				        SortTextGlobalsOrKeywords;
				    Symbol* moduleSymbol =
				        firstAccessibleSymbol->parent;
				    if (moduleSymbol == nullptr ||
				        !checker::isExternalModuleSymbol(moduleSymbol) ||
				        typeChecker
				                ->TryGetMemberInModuleExportsAndProperties(
				                    firstAccessibleSymbol->name,
				                    moduleSymbol) !=
				            firstAccessibleSymbol) {
					    symbolToOriginInfoMap[int(symbols.size()) - 1] =
					        new symbolOriginInfo{
					            .kind = getNullableSymbolOriginInfoKind(
					                symbolOriginInfoKindSymbolMember,
					                insertQuestionDot)};
				    } else {
					    // !!! auto-import symbol
				    }
			    } else if (firstAccessibleSymbolId == 0 ||
			               !seenPropertySymbols.Has(
			                   firstAccessibleSymbolId)) {
				    symbols.push_back(symbol);
				    addSymbolOriginInfo(symbol, insertQuestionDot,
				                        insertAwait);
				    addSymbolSortInfo(symbol);
			    }
		    } else {
			    symbols.push_back(symbol);
			    addSymbolOriginInfo(symbol, insertQuestionDot,
			                        insertAwait);
			    addSymbolSortInfo(symbol);
		    }
	    };

	std::function<void(checker::Type*, bool, bool)> addTypeProperties =
	    [&](checker::Type* t, bool insertAwait, bool insertQuestionDot) {
		    if (typeChecker->GetStringIndexType(t) != nullptr) {
			    isNewIdentifierLocation = true;
			    defaultCommitCharacters = std::vector<std::string>{};
		    }
		    if (isRightOfQuestionDot &&
		        !typeChecker->GetCallSignatures(t).empty()) {
			    isNewIdentifierLocation = true;
			    if (!defaultCommitCharacters.has_value()) {
				    // Only invalid commit character here would be `(`.
				    defaultCommitCharacters = allCommitCharacters;
			    }
		    }

		    Node* propertyAccess;
		    if (node->kind == Kind::ImportType) {
			    propertyAccess = node;
		    } else {
			    propertyAccess = node->parent;
		    }

		    if (inCheckedFile) {
			    for (Symbol* symbol :
			         typeChecker->GetApparentProperties(t)) {
				    if (typeChecker->IsValidPropertyAccessForCompletions(
				            propertyAccess, t, symbol)) {
					    addPropertySymbol(symbol, /*insertAwait*/ false,
					                      insertQuestionDot);
				    }
			    }
		    } else {
			    // In javascript files, for union types, we don't just get the
			    // members that the individual types have in common, we also
			    // include all the members that each individual type has. This
			    // is because we're going to add all identifiers anyways. So
			    // we might as well elevate the members that were at least
			    // part of the individual types to a higher status since we
			    // know what they are.
			    for (Symbol* symbol :
			         getPropertiesForCompletion(t, typeChecker)) {
				    if (typeChecker->IsValidPropertyAccessForCompletions(
				            propertyAccess, t, symbol)) {
					    symbols.push_back(symbol);
				    }
			    }
		    }

		    if (insertAwait) {
			    checker::Type* promiseType =
			        typeChecker->GetPromisedTypeOfPromise(t);
			    if (promiseType != nullptr) {
				    for (Symbol* symbol :
				         typeChecker->GetApparentProperties(promiseType)) {
					    if (typeChecker
					            ->IsValidPropertyAccessForCompletions(
					                propertyAccess, promiseType,
					                symbol)) {
						    addPropertySymbol(symbol,
						                      /*insertAwait*/ true,
						                      insertQuestionDot);
					    }
				    }
			    }
		    }
	    };

	auto getTypeScriptMemberSymbols = [&]() {
		// Right of dot member completion list
		completionKind = CompletionKindPropertyAccess;

		// Since this is qualified name check it's a type node location
		bool isImportType = isLiteralImportTypeNode(node);
		bool isTypeLocation =
		    (isImportType && !node->as<ImportTypeNode>()->IsTypeOf) ||
		    isPartOfTypeNode(node->parent) ||
		    isPossiblyTypeArgumentPosition(contextToken, file, typeChecker);
		bool isRhsOfImportDeclaration =
		    isInRightSideOfInternalImportEqualsDeclaration(node);
		if (isEntityName(node) || isImportType ||
		    isPropertyAccessExpression(node)) {
			bool isNamespaceName = isModuleDeclaration(node->parent);
			if (isNamespaceName) {
				isNewIdentifierLocation = true;
				defaultCommitCharacters = std::vector<std::string>{};
			}
			Symbol* symbol = typeChecker->GetSymbolAtLocation(node);
			if (symbol != nullptr) {
				symbol = checker::SkipAlias(symbol, typeChecker);
				if (symbol->flags & (SymbolFlagsModule | SymbolFlagsEnum)) {
					Node* valueAccessNode;
					if (isImportType) {
						valueAccessNode = node;
					} else {
						valueAccessNode = node->parent;
					}
					// Extract module or enum members
					std::vector<Symbol*> exportedSymbols =
					    typeChecker->GetExportsOfModule(symbol);
					for (Symbol* exportedSymbol : exportedSymbols) {
						if (exportedSymbol == nullptr) {
							TSC_UNREACHABLE(
							    "getExportsOfModule() should all be defined");
						}
						auto isValidValueAccess =
						    [&](Symbol* s) {
							    return typeChecker->IsValidPropertyAccess(
							        valueAccessNode, s->name);
						    };
						auto isValidTypeAccess =
						    [&](Symbol* s) {
							    return symbolCanBeReferencedAtTypeLocation(
							        s, typeChecker,
							        collections::Set<SymbolId>{});
						    };
						bool isValidAccess;
						if (isNamespaceName) {
							// At `namespace N.M/**/`, if this is the only
							// declaration of `M`, don't include `M` as a
							// completion.
							isValidAccess =
							    exportedSymbol->flags &
							        SymbolFlagsNamespace &&
							    !everyList(
							        exportedSymbol->declarations,
							        [&](Node* declaration) {
								        return declaration->parent ==
								               node->parent;
							        });
						} else if (isRhsOfImportDeclaration) {
							// Any kind is allowed when dotting off namespace
							// in internal import equals declaration
							isValidAccess =
							    isValidTypeAccess(exportedSymbol) ||
							    isValidValueAccess(exportedSymbol);
						} else if (isTypeLocation ||
						           insideJSDocTagTypeExpression) {
							isValidAccess =
							    isValidTypeAccess(exportedSymbol);
						} else {
							isValidAccess =
							    isValidValueAccess(exportedSymbol);
						}
						if (isValidAccess) {
							symbols.push_back(exportedSymbol);
						}
					}

					// If the module is merged with a value, we must get the
					// type of the class and add its properties (for inherited
					// static methods).
					if (!isTypeLocation && !insideJSDocTagTypeExpression &&
					    someList(symbol->declarations, [](Node* decl) {
						    return decl->kind != Kind::SourceFile &&
						           decl->kind != Kind::ModuleDeclaration &&
						           decl->kind != Kind::EnumDeclaration;
					    })) {
						checker::Type* t =
						    typeChecker->GetNonOptionalType(
						        typeChecker->GetTypeOfSymbolAtLocation(
						            symbol, node));
						bool insertQuestionDot = false;
						if (typeChecker->IsNullableType(t)) {
							bool canCorrectToQuestionDot =
							    isRightOfDot && !isRightOfQuestionDot &&
							    !tristateIsFalse(
							        preferences
							            .IncludeAutomaticOptionalChainCompletions);
							if (canCorrectToQuestionDot ||
							    isRightOfQuestionDot) {
								t = typeChecker->GetNonNullableType(t);
								if (canCorrectToQuestionDot) {
									insertQuestionDot = true;
								}
							}
						}
						addTypeProperties(
						    t, node->flags & NodeFlagsAwaitContext,
						    insertQuestionDot);
					}

					return;
				}
			}
		}

		if (!isTypeLocation || isInTypeQuery(node)) {
			// microsoft/TypeScript#39946. Pulling on the type of a node inside
			// of a function with a contextual `this` parameter can result in a
			// circularity if the `node` is part of the exprssion of a `yield`
			// or `return`. This circularity doesn't exist at compile time
			// because we will check (and cache) the type of `this` *before*
			// checking the type of the node.
			typeChecker->TryGetThisTypeAtEx(node, /*includeGlobalThis*/ false,
			                                nullptr);
			checker::Type* t = typeChecker->GetNonOptionalType(
			    typeChecker->GetTypeAtLocation(node));

			if (!isTypeLocation) {
				bool insertQuestionDot = false;
				if (typeChecker->IsNullableType(t)) {
					bool canCorrectToQuestionDot =
					    isRightOfDot && !isRightOfQuestionDot &&
					    !tristateIsFalse(
					        preferences
					            .IncludeAutomaticOptionalChainCompletions);
					if (canCorrectToQuestionDot || isRightOfQuestionDot) {
						t = typeChecker->GetNonNullableType(t);
						if (canCorrectToQuestionDot) {
							insertQuestionDot = true;
						}
					}
				}
				addTypeProperties(t, node->flags & NodeFlagsAwaitContext,
				                  insertQuestionDot);
			} else {
				addTypeProperties(typeChecker->GetNonNullableType(t),
				                  /*insertAwait*/ false,
				                  /*insertQuestionDot*/ false);
			}
		}
	};

	// Aggregates relevant symbols for completion in object literals in type
	// argument positions.
	auto tryGetObjectTypeLiteralInTypeArgumentCompletionSymbols =
	    [&]() -> std::pair<globalsSearch, gostd::Error> {
		Node* typeLiteralNode = tryGetTypeLiteralNode(contextToken);
		if (typeLiteralNode == nullptr) {
			return {globalsSearchContinue, nullptr};
		}

		Node* intersectionTypeNode =
		    isIntersectionTypeNode(typeLiteralNode->parent)
		        ? typeLiteralNode->parent
		        : nullptr;
		Node* containerTypeNode = intersectionTypeNode != nullptr
		                              ? intersectionTypeNode
		                              : typeLiteralNode;

		checker::Type* containerExpectedType =
		    getConstraintOfTypeArgumentProperty(containerTypeNode,
		                                        typeChecker);
		if (containerExpectedType == nullptr) {
			return {globalsSearchContinue, nullptr};
		}

		checker::Type* containerActualType =
		    typeChecker->GetTypeFromTypeNode(containerTypeNode);

		std::vector<Symbol*> members =
		    getPropertiesForCompletion(containerExpectedType, typeChecker);
		std::vector<Symbol*> existingMembers =
		    getPropertiesForCompletion(containerActualType, typeChecker);

		collections::Set<std::string> existingMemberNames;
		for (Symbol* member : existingMembers) {
			existingMemberNames.Add(member->name);
		}

		auto filtered = filterList(members, [&](Symbol* member) {
			return !existingMemberNames.Has(member->name);
		});
		symbols.insert(symbols.end(), filtered.begin(), filtered.end());

		completionKind = CompletionKindObjectPropertyDeclaration;
		isNewIdentifierLocation = true;

		return {globalsSearchSuccess, nullptr};
	};

	// Aggregates relevant symbols for completion in object literals and
	// object binding patterns. Relevant symbols are stored in the captured
	// 'symbols' variable.
	auto tryGetObjectLikeCompletionSymbols =
	    [&]() -> std::pair<globalsSearch, gostd::Error> {
		if (contextToken != nullptr &&
		    contextToken->kind == Kind::DotDotDotToken) {
			return {globalsSearchContinue, nullptr};
		}
		Node* objectLikeContainer =
		    tryGetObjectLikeCompletionContainer(contextToken, position,
		                                        file);
		if (objectLikeContainer == nullptr) {
			return {globalsSearchContinue, nullptr};
		}

		// We're looking up possible property names from contextual/inferred/
		// declared type.
		completionKind = CompletionKindObjectPropertyDeclaration;

		std::vector<Symbol*> typeMembers;
		std::vector<Node*> existingMembers;

		if (objectLikeContainer->kind == Kind::ObjectLiteralExpression) {
			checker::Type* instantiatedType =
			    tryGetObjectLiteralContextualType(objectLikeContainer,
			                                      typeChecker);

			// Check completions for Object property value shorthand
			if (instantiatedType == nullptr) {
				if (objectLikeContainer->flags &
				    NodeFlagsInWithStatement) {
					return {globalsSearchFail, nullptr};
				}
				return {globalsSearchContinue, nullptr};
			}
			checker::Type* completionsType =
			    typeChecker->GetContextualType(
			        objectLikeContainer,
			        checker::ContextFlagsIgnoreNodeInferences);
			checker::Type* t = completionsType != nullptr ? completionsType
			                                              : instantiatedType;
			checker::Type* stringIndexType =
			    typeChecker->GetStringIndexType(t);
			checker::Type* numberIndexType =
			    typeChecker->GetNumberIndexType(t);
			isNewIdentifierLocation = stringIndexType != nullptr ||
			                          numberIndexType != nullptr;
			typeMembers = getPropertiesForObjectExpression(
			    instantiatedType, completionsType, objectLikeContainer,
			    typeChecker);
			existingMembers = objectLikeContainer->properties();

			if (typeMembers.empty()) {
				// Edge case: If NumberIndexType exists
				if (numberIndexType == nullptr) {
					return {globalsSearchContinue, nullptr};
				}
			}
		} else {
			if (objectLikeContainer->kind != Kind::ObjectBindingPattern) {
				TSC_UNREACHABLE(
				    "Expected 'objectLikeContainer' to be an object binding "
				    "pattern.");
			}
			// We are *only* completing on properties from the type being
			// destructured.
			isNewIdentifierLocation = false;
			Node* rootDeclaration =
			    getRootDeclaration(objectLikeContainer->parent);
			if (!isVariableLike(rootDeclaration)) {
				TSC_UNREACHABLE("Root declaration is not variable-like.");
			}

			// We don't want to complete using the type acquired by the shape
			// of the binding pattern; we are only interested in types
			// acquired through type declaration or inference. Also proceed if
			// rootDeclaration is a parameter and if its containing function
			// expression/arrow function is contextually typed - type of
			// parameter will flow in from the contextual type of the
			// function.
			bool canGetType =
			    hasInitializer(rootDeclaration) ||
			    getTypeAnnotationNode(rootDeclaration) != nullptr ||
			    rootDeclaration->parent->parent->kind ==
			        Kind::ForOfStatement;
			if (!canGetType &&
			    rootDeclaration->kind == Kind::Parameter) {
				if (isExpression(rootDeclaration->parent)) {
					canGetType =
					    typeChecker->GetContextualType(
					        rootDeclaration->parent,
					        checker::ContextFlagsNone) != nullptr;
				} else if (rootDeclaration->parent->kind ==
				               Kind::MethodDeclaration ||
				           rootDeclaration->parent->kind ==
				               Kind::SetAccessor) {
					canGetType =
					    isExpression(rootDeclaration->parent->parent) &&
					    typeChecker->GetContextualType(
					        rootDeclaration->parent->parent,
					        checker::ContextFlagsNone) != nullptr;
				}
			}
			if (canGetType) {
				checker::Type* typeForObject =
				    typeChecker->GetTypeAtLocation(objectLikeContainer);
				if (typeForObject == nullptr) {
					return {globalsSearchFail, nullptr};
				}
				typeMembers = filterList(
				    typeChecker->GetPropertiesOfType(typeForObject),
				    [&](Symbol* propertySymbol) {
					    return typeChecker->IsPropertyAccessible(
					        objectLikeContainer, /*isSuper*/ false,
					        /*isWrite*/ false, typeForObject,
					        propertySymbol);
				    });
				existingMembers = objectLikeContainer->elements();
			}
		}

		if (!typeMembers.empty()) {
			// Add filtered items to the completion list.
			auto [filteredMembers, spreadMemberNames] =
			    filterObjectMembersList(
			        typeMembers,
			        checkEachDefined(existingMembers,
			                               "object like properties or elements "
			                               "should all be defined"),
			        file, position, typeChecker);
			symbols.insert(symbols.end(), filteredMembers.begin(),
			               filteredMembers.end());

			// Set sort texts.
			for (Symbol* member : filteredMembers) {
				SymbolId symbolId = getSymbolId(member);
				if (spreadMemberNames.Has(member->name)) {
					symbolToSortTextMap[symbolId] =
					    SortTextMemberDeclaredBySpreadAssignment;
				}
				if (member->flags & SymbolFlagsOptional) {
					if (!symbolToSortTextMap.count(symbolId)) {
						symbolToSortTextMap[symbolId] =
						    SortTextOptionalMember;
					}
				}
				if (objectLikeContainer->kind ==
				        Kind::ObjectLiteralExpression &&
				    tristateIsTrue(
				        preferences
				            .IncludeCompletionsWithObjectLiteralMethodSnippets)) {
					auto&& [displayName, displayNameOk] =
					    getCompletionEntryDisplayNameForSymbol(
					        file, preferences, member,
					        /*origin*/ nullptr,
					        CompletionKindObjectPropertyDeclaration,
					        /*isJsxIdentifierExpected*/ false);
					if (!displayName.empty()) {
						auto it = symbolToSortTextMap.find(symbolId);
						SortText originalSortText =
						    it != symbolToSortTextMap.end()
						        ? it->second
						        : SortTextLocationPriority;
						symbolToSortTextMap[symbolId] =
						    ObjectLiteralPropertySortText(
						        originalSortText, displayName);
					}
				}
			}

			if (objectLikeContainer->kind ==
			        Kind::ObjectLiteralExpression &&
			    tristateIsTrue(
			        preferences
			            .IncludeCompletionsWithObjectLiteralMethodSnippets)) {
				for (objectLiteralMethodSymbol& entry :
				     collectObjectLiteralMethodSymbols(
				         ctx, typeChecker, filteredMembers,
				         objectLikeContainer, file)) {
					symbolToOriginInfoMap[int(symbols.size())] =
					    entry.origin;
					symbols.push_back(entry.symbol);
				}
			}
		}

		return {globalsSearchSuccess, nullptr};
	};

	auto shouldOfferImportCompletions = [&]() {
		if (isDynamicFileName(file->FileName())) {
			return false;
		}
		// If already typing an import statement, provide completions for it.
		if (importStatementCompletion != nullptr) {
			return true;
		}
		// If not already a module, must have modules enabled.
		if (tristateIsFalse(
		        preferences.IncludeCompletionsForModuleExports)) {
			return false;
		}
		// Always using ES modules in 6.0+
		return true;
	};

	// Mutates `symbols`, `symbolToOriginInfoMap`, and `symbolToSortTextMap`
	auto collectAutoImports = [&]() -> gostd::Error {
		// `completionItem/resolve` for auto-import completions should be
		// resolved via the completion item data, so we don't need to collect
		// auto-import entries again.
		if (forItemResolve) {
			return nullptr;
		}
		if (!shouldOfferImportCompletions()) {
			return nullptr;
		}

		// import { type | -> token text should be blank
		std::string lowerCaseTokenText;
		auto [usagePosition, fidelity] = createLspPosition(position, file);
		if (!fidelity.IsExact()) {
			return nullptr;
		}
		if (previousToken != nullptr && isIdentifier(previousToken)) {
			auto [up2, f2] = createLspPosition(
			    getTokenPosOfNode(previousToken, file,
			                      /*includeJSDoc*/ false),
			    file);
			if (!f2.IsExact()) {
				return nullptr;
			}
			usagePosition = up2;
			if (!(previousToken == contextToken &&
			      importStatementCompletion != nullptr)) {
				lowerCaseTokenText =
				    stringutil::ToLowerJS(previousToken->text());
			}
		}

		auto [view, err] = getPreparedAutoImportView(file, typeChecker);
		if (err != nullptr) {
			return err;
		}
		if (view == nullptr) {
			return nullptr;
		}

		auto views = view->GetCompletions(lowerCaseTokenText, usagePosition,
		                                isRightOfOpenTag,
		                                isTypeOnlyLocation);
		for (auto& f : views) {
			autoImports.push_back(f.release());
		}
		return nullptr;
	};

	auto tryGetImportCompletionSymbols =
	    [&]() -> std::pair<globalsSearch, gostd::Error> {
		if (importStatementCompletion == nullptr) {
			return {globalsSearchContinue, nullptr};
		}
		isNewIdentifierLocation = true;
		if (gostd::Error err = collectAutoImports(); err != nullptr) {
			return {globalsSearchFail, err};
		}
		return {globalsSearchSuccess, nullptr};
	};

	// Aggregates relevant symbols for completion in import clauses and export
	// clauses whose declarations have a module specifier; for instance,
	// symbols will be aggregated for
	//
	//      import { | } from "moduleName";
	//      export { a as foo, | } from "moduleName";
	//
	// but not for
	//
	//      export { | };
	//
	// Relevant symbols are stored in the captured 'symbols' variable.
	auto tryGetImportOrExportClauseCompletionSymbols =
	    [&]() -> std::pair<globalsSearch, gostd::Error> {
		if (contextToken == nullptr) {
			return {globalsSearchContinue, nullptr};
		}

		// `import { |` or `import { a as 0, | }` or `import { type | }`
		Node* namedImportsOrExports = nullptr;
		if (contextToken->kind == Kind::OpenBraceToken ||
		    contextToken->kind == Kind::CommaToken) {
			namedImportsOrExports =
			    isNamedImportsOrExports(contextToken->parent)
			        ? contextToken->parent
			        : nullptr;
		} else if (isTypeKeywordTokenOrIdentifier(contextToken)) {
			namedImportsOrExports =
			    isNamedImportsOrExports(contextToken->parent->parent)
			        ? contextToken->parent->parent
			        : nullptr;
		}

		if (namedImportsOrExports == nullptr) {
			return {globalsSearchContinue, nullptr};
		}

		// We can at least offer `type` at `import { |`
		if (!isTypeKeywordTokenOrIdentifier(contextToken)) {
			keywordFilters = KeywordCompletionFiltersTypeKeyword;
		}

		// try to show exported member for imported/re-exported module
		Node* holder = namedImportsOrExports->kind == Kind::NamedImports
		                   ? namedImportsOrExports->parent->parent
		                   : namedImportsOrExports->parent;
		Node* moduleSpecifier = holder->moduleSpecifier();
		if (moduleSpecifier == nullptr) {
			isNewIdentifierLocation = true;
			if (namedImportsOrExports->kind == Kind::NamedImports) {
				return {globalsSearchFail, nullptr};
			}
			return {globalsSearchContinue, nullptr};
		}

		Symbol* moduleSpecifierSymbol =
		    typeChecker->GetSymbolAtLocation(moduleSpecifier);
		if (moduleSpecifierSymbol == nullptr) {
			isNewIdentifierLocation = true;
			return {globalsSearchFail, nullptr};
		}

		completionKind = CompletionKindMemberLike;
		isNewIdentifierLocation = false;
		std::vector<Symbol*> exports =
		    typeChecker->GetExportsAndPropertiesOfModule(
		        moduleSpecifierSymbol);

		collections::Set<std::string> existing;
		for (Node* element : namedImportsOrExports->elements()) {
			if (isCurrentlyEditingNode(element, file, position)) {
				continue;
			}
			existing.Add(element->propertyNameOrName()->text());
		}
		auto uniques = filterList(exports, [&](Symbol* symbol) {
			return symbolName(symbol) != InternalSymbolNameDefault &&
			       !existing.Has(symbolName(symbol));
		});

		symbols.insert(symbols.end(), uniques.begin(), uniques.end());
		if (uniques.empty()) {
			// If there's nothing else to import, don't offer `type` either.
			keywordFilters = KeywordCompletionFiltersNone;
		}
		return {globalsSearchSuccess, nullptr};
	};

	// import { x } from "foo" with { | }
	auto tryGetImportAttributesCompletionSymbols =
	    [&]() -> std::pair<globalsSearch, gostd::Error> {
		if (contextToken == nullptr) {
			return {globalsSearchContinue, nullptr};
		}

		Node* importAttributes = nullptr;
		switch (contextToken->kind) {
		case Kind::OpenBraceToken:
		case Kind::CommaToken:
			importAttributes = contextToken->parent;
			break;
		case Kind::ColonToken:
			importAttributes = contextToken->parent->parent;
			break;
		default:
			break;
		}
		if (importAttributes == nullptr ||
		    !isImportAttributes(importAttributes)) {
			return {globalsSearchContinue, nullptr};
		}

		std::vector<Node*> elements;
		if (importAttributes->as<ImportAttributes>()->Attributes !=
		    nullptr) {
			elements =
			    importAttributes->as<ImportAttributes>()->Attributes->nodes;
		}
		collections::Set<std::string> existing;
		for (Node* el : elements) {
			existing.Add(el->as<ImportAttribute>()->name->text());
		}
		auto uniques = filterList(
		    typeChecker->GetApparentProperties(
		        typeChecker->GetTypeAtLocation(importAttributes)),
		    [&](Symbol* symbol) {
			    return !existing.Has(symbolName(symbol));
		    });
		symbols.insert(symbols.end(), uniques.begin(), uniques.end());
		return {globalsSearchSuccess, nullptr};
	};

	// Adds local declarations for completions in named exports:
	//   export { | };
	// Does not check for the absence of a module specifier (`export {} from
	// "./other"`) because `tryGetImportOrExportClauseCompletionSymbols` runs
	// first and handles that, preventing this function from running.
	auto tryGetLocalNamedExportCompletionSymbols =
	    [&]() -> std::pair<globalsSearch, gostd::Error> {
		if (contextToken == nullptr) {
			return {globalsSearchContinue, nullptr};
		}
		Node* namedExports = nullptr;
		if (contextToken->kind == Kind::OpenBraceToken ||
		    contextToken->kind == Kind::CommaToken) {
			namedExports = isNamedExports(contextToken->parent)
			                   ? contextToken->parent
			                   : nullptr;
		}

		if (namedExports == nullptr) {
			return {globalsSearchContinue, nullptr};
		}

		Node* localsContainer = findAncestor(namedExports, [](Node* node) {
			return isSourceFile(node) || isModuleDeclaration(node);
		});
		completionKind = CompletionKindNone;
		isNewIdentifierLocation = false;
		Symbol* localSymbol = localsContainer->symbol();
		SymbolTable localExports;
		if (localSymbol != nullptr) {
			localExports = localSymbol->exports;
		}
		for (auto& [name, symbol] : *localsContainer->locals()) {
			symbols.push_back(symbol);
			if (localExports.count(name)) {
				symbolToSortTextMap[getSymbolId(symbol)] =
				    SortTextOptionalMember;
			}
		}

		return {globalsSearchSuccess, nullptr};
	};

	auto tryGetConstructorCompletion =
	    [&]() -> std::pair<globalsSearch, gostd::Error> {
		if (tryGetConstructorLikeCompletionContainer(contextToken) ==
		    nullptr) {
			return {globalsSearchContinue, nullptr};
		}

		// no members, only keywords
		completionKind = CompletionKindNone;
		// Declaring new property/method/accessor
		isNewIdentifierLocation = true;
		// Has keywords for constructor parameter
		keywordFilters = KeywordCompletionFiltersConstructorParameterKeywords;
		return {globalsSearchSuccess, nullptr};
	};

	// Aggregates relevant symbols for completion in class declaration.
	// Relevant symbols are stored in the captured 'symbols' variable.
	auto tryGetClassLikeCompletionSymbols =
	    [&]() -> std::pair<globalsSearch, gostd::Error> {
		Node* decl = tryGetObjectTypeDeclarationCompletionContainer(
		    file, contextToken, location, position);
		if (decl == nullptr) {
			return {globalsSearchContinue, nullptr};
		}

		// We're looking up possible property names from parent type.
		completionKind = CompletionKindMemberLike;
		// Declaring new property/method/accessor
		isNewIdentifierLocation = true;
		if (contextToken->kind == Kind::AsteriskToken) {
			keywordFilters = KeywordCompletionFiltersNone;
		} else if (isClassLike(decl)) {
			keywordFilters = KeywordCompletionFiltersClassElementKeywords;
		} else {
			keywordFilters =
			    KeywordCompletionFiltersInterfaceElementKeywords;
		}

		// If you're in an interface you don't want to repeat things from
		// super-interface. So just stop here.
		if (!isClassLike(decl)) {
			return {globalsSearchSuccess, nullptr};
		}

		Node* classElement;
		if (contextToken->kind == Kind::SemicolonToken) {
			classElement = contextToken->parent->parent;
		} else {
			classElement = contextToken->parent;
		}
		ModifierFlags classElementModifierFlags = ModifierFlagsNone;
		if (isClassElement(classElement)) {
			classElementModifierFlags = classElement->modifierFlags();
		}
		// If this is context token is not something we are editing now,
		// consider if this would lead to be modifier.
		if (contextToken->kind == Kind::Identifier &&
		    !isCurrentlyEditingNode(contextToken, file, position)) {
			if (contextToken->text() == "private") {
				classElementModifierFlags |= ModifierFlagsPrivate;
			} else if (contextToken->text() == "static") {
				classElementModifierFlags |= ModifierFlagsStatic;
			} else if (contextToken->text() == "override") {
				classElementModifierFlags |= ModifierFlagsOverride;
			}
		}
		if (isClassStaticBlockDeclaration(classElement)) {
			classElementModifierFlags |= ModifierFlagsStatic;
		}

		// No member list for private methods
		if (!(classElementModifierFlags & ModifierFlagsPrivate)) {
			// List of property symbols of base type that are not private and
			// already implemented
			std::vector<Node*> baseTypeNodes;
			if (isClassLike(decl) &&
			    (classElementModifierFlags & ModifierFlagsOverride)) {
				baseTypeNodes =
				    singleElementSlice(getClassExtendsHeritageElement(decl));
			} else {
				baseTypeNodes = getAllSuperTypeNodes(decl);
			}
			std::vector<Symbol*> baseSymbols;
			for (Node* baseTypeNode : baseTypeNodes) {
				checker::Type* t =
				    typeChecker->GetTypeAtLocation(baseTypeNode);
				if (classElementModifierFlags & ModifierFlagsStatic) {
					if (t->symbol != nullptr) {
						auto props =
						    typeChecker->GetPropertiesOfType(
						        typeChecker->GetTypeOfSymbolAtLocation(
						            t->symbol, decl));
						baseSymbols.insert(baseSymbols.end(),
						                   props.begin(), props.end());
					}
				} else if (t != nullptr) {
					auto props = typeChecker->GetPropertiesOfType(t);
					baseSymbols.insert(baseSymbols.end(), props.begin(),
					                   props.end());
				}
			}

			auto filtered = filterClassMembersList(
			    baseSymbols, decl->members(), classElementModifierFlags, file,
			    position);
			symbols.insert(symbols.end(), filtered.begin(), filtered.end());
			for (size_t index = 0; index < symbols.size(); index++) {
				Symbol* symbol = symbols[index];
				Node* declaration = symbol->valueDeclaration;
				if (declaration != nullptr &&
				    isClassElement(declaration) &&
				    declaration->name() != nullptr &&
				    isComputedPropertyName(declaration->name())) {
					symbolToOriginInfoMap[int(index)] =
					    new symbolOriginInfo{
					        .kind =
					            symbolOriginInfoKindComputedPropertyName,
					        .data = new symbolOriginInfoComputedPropertyName{
					            .symbolName =
					                typeChecker->SymbolToString(symbol)}};
				}
			}
		}

		return {globalsSearchSuccess, nullptr};
	};

	auto tryGetJsxCompletionSymbols =
	    [&]() -> std::pair<globalsSearch, gostd::Error> {
		Node* jsxContainer = tryGetContainingJsxElement(contextToken, file);
		if (jsxContainer == nullptr) {
			return {globalsSearchContinue, nullptr};
		}
		// Cursor is inside a JSX self-closing element or opening element.
		Node* attrs = jsxContainer->attributes();
		checker::Type* attrsType = typeChecker->GetContextualType(
		    attrs, checker::ContextFlagsNone);
		if (attrsType == nullptr) {
			return {globalsSearchContinue, nullptr};
		}
		checker::Type* completionsType = typeChecker->GetContextualType(
		    attrs, checker::ContextFlagsIgnoreNodeInferences);
		auto [filteredSymbols, spreadMemberNames] = filterJsxAttributes(
		    getPropertiesForObjectExpression(attrsType, completionsType,
		                                     attrs, typeChecker),
		    attrs->properties(), file, position, typeChecker);

		symbols.insert(symbols.end(), filteredSymbols.begin(),
		               filteredSymbols.end());
		// Set sort texts.
		for (Symbol* symbol : filteredSymbols) {
			SymbolId symbolId = getSymbolId(symbol);
			if (spreadMemberNames.Has(symbolName(symbol))) {
				symbolToSortTextMap[symbolId] =
				    SortTextMemberDeclaredBySpreadAssignment;
			}
			if (symbol->flags & SymbolFlagsOptional) {
				if (!symbolToSortTextMap.count(symbolId)) {
					symbolToSortTextMap[symbolId] = SortTextOptionalMember;
				}
			}
		}

		completionKind = CompletionKindMemberLike;
		isNewIdentifierLocation = false;
		return {globalsSearchSuccess, nullptr};
	};

	auto getGlobalCompletions =
	    [&]() -> std::pair<globalsSearch, gostd::Error> {
		if (tryGetFunctionLikeBodyCompletionContainer(contextToken) !=
		    nullptr) {
			keywordFilters = KeywordCompletionFiltersFunctionLikeBodyKeywords;
		} else {
			keywordFilters = KeywordCompletionFiltersAll;
		}
		// Get all entities in the current scope.
		completionKind = CompletionKindGlobal;
		auto [isNewIdent, commitChars] =
		    computeCommitCharactersAndIsNewIdentifier(contextToken, file,
		                                              position);
		isNewIdentifierLocation = isNewIdent;
		defaultCommitCharacters = commitChars;

		if (previousToken != contextToken) {
			if (previousToken == nullptr) {
				TSC_UNREACHABLE(
				    "Expected 'contextToken' to be defined when different "
				    "from 'previousToken'.");
			}
		}

		// We need to find the node that will give us an appropriate scope to
		// begin aggregating completion candidates. This is achieved in
		// 'getScopeNode' by finding the first node that encompasses a
		// position, accounting for whether a node is "complete" to decide
		// whether a position belongs to the node.
		//
		// However, at the end of an identifier, we are interested in the
		// scope of the identifier itself, but fall outside of the identifier.
		// For instance:
		//
		//      xyz => x$
		//
		// the cursor is outside of both the 'x' and the arrow function
		// 'xyz => x', so 'xyz' is not returned in our results.
		//
		// We define 'adjustedPosition' so that we may appropriately account
		// for being at the end of an identifier. The intention is that if
		// requesting completion at the end of an identifier, it should be
		// effectively equivalent to requesting completion anywhere inside/at
		// the beginning of the identifier. So in the previous case, the
		// 'adjustedPosition' will work as if requesting completion in the
		// following:
		//
		//      xyz => $x
		//
		// If previousToken !== contextToken, then
		//   - 'contextToken' was adjusted to the token prior to
		//     'previousToken' because we were at the end of an identifier.
		//   - 'previousToken' is defined.
		int adjustedPosition;
		if (previousToken != contextToken) {
			adjustedPosition = astnav::getStartOfNode(
			    previousToken, file, /*includeJSDoc*/ false);
		} else {
			adjustedPosition = position;
		}

		Node* scopeNode = getScopeNode(contextToken, adjustedPosition, file);
		if (scopeNode == nullptr) {
			scopeNode = file->asNode();
		}
		isInSnippetScope = isSnippetScope(scopeNode);

		SymbolFlags symbolMeanings =
		    ifElse<SymbolFlags>(isTypeOnlyLocation, SymbolFlagsNone,
		                        SymbolFlagsValue) |
		    SymbolFlagsType | SymbolFlagsNamespace | SymbolFlagsAlias;
		bool typeOnlyAliasNeedsPromotion =
		    previousToken != nullptr &&
		    !isValidTypeOnlyAliasUseSite(previousToken);

		auto inScope =
		    typeChecker->GetSymbolsInScope(scopeNode, symbolMeanings);
		symbols.insert(symbols.end(), inScope.begin(), inScope.end());
		checkEachDefined(symbols,
		                 "getSymbolsInScope() should all be defined");
		for (size_t index = 0; index < symbols.size(); index++) {
			Symbol* symbol = symbols[index];
			SymbolId symbolId = getSymbolId(symbol);
			if (!typeChecker->IsArgumentsSymbol(symbol) &&
			    !someList(symbol->declarations, [&](Node* decl) {
				    return getSourceFileOfNode(decl) == file;
			    })) {
				symbolToSortTextMap[symbolId] = SortTextGlobalsOrKeywords;
			}
			if (typeOnlyAliasNeedsPromotion &&
			    !(symbol->flags & SymbolFlagsValue)) {
				Node* typeOnlyAliasDeclaration =
				    findIn(symbol->declarations,
				           isTypeOnlyImportDeclaration);
				if (typeOnlyAliasDeclaration != nullptr) {
					symbolToOriginInfoMap[int(index)] =
					    new symbolOriginInfo{
					        .kind = symbolOriginInfoKindTypeOnlyAlias,
					        .data = new symbolOriginInfoTypeOnlyAlias{
					            .declaration =
					                typeOnlyAliasDeclaration}};
				}
			}
		}

		// Need to insert 'this.' before properties of `this` type.
		if (scopeNode->kind != Kind::SourceFile) {
			checker::Type* thisType = typeChecker->TryGetThisTypeAtEx(
			    scopeNode, /*includeGlobalThis*/ false,
			    isClassLike(scopeNode->parent) ? scopeNode : nullptr);
			if (thisType != nullptr &&
			    !isProbablyGlobalType(thisType, file, typeChecker)) {
				for (Symbol* symbol :
				     getPropertiesForCompletion(thisType, typeChecker)) {
					SymbolId symbolId = getSymbolId(symbol);
					symbols.push_back(symbol);
					symbolToOriginInfoMap[int(symbols.size()) - 1] =
					    new symbolOriginInfo{
					        .kind = symbolOriginInfoKindThisType};
					symbolToSortTextMap[symbolId] =
					    SortTextSuggestedClassMembers;
				}
			}
		}

		if (gostd::Error err = collectAutoImports(); err != nullptr) {
			return {globalsSearchFail, err};
		}
		if (isTypeOnlyLocation) {
			if (contextToken != nullptr &&
			    isAssertionExpression(contextToken->parent)) {
				keywordFilters =
				    KeywordCompletionFiltersTypeAssertionKeywords;
			} else {
				keywordFilters = KeywordCompletionFiltersTypeKeywords;
			}
		}

		return {globalsSearchSuccess, nullptr};
	};

	auto tryGetGlobalSymbols =
	    [&]() -> std::pair<bool, gostd::Error> {
		globalsSearch result;
		gostd::Error err;
		std::vector<
		    std::function<std::pair<globalsSearch, gostd::Error>()>>
		    globalSearchFuncs = {
		        tryGetObjectTypeLiteralInTypeArgumentCompletionSymbols,
		        tryGetObjectLikeCompletionSymbols,
		        tryGetImportCompletionSymbols,
		        tryGetImportOrExportClauseCompletionSymbols,
		        tryGetImportAttributesCompletionSymbols,
		        tryGetLocalNamedExportCompletionSymbols,
		        tryGetConstructorCompletion,
		        tryGetClassLikeCompletionSymbols,
		        tryGetJsxCompletionSymbols,
		        getGlobalCompletions,
		    };
		for (auto& globalSearchFunc : globalSearchFuncs) {
			auto r = globalSearchFunc();
			result = r.first;
			err = r.second;
			if (err != nullptr) {
				return {false, err};
			}
			if (result != globalsSearchContinue) {
				break;
			}
		}
		return {result == globalsSearchSuccess, nullptr};
	};

	if (isRightOfDot || isRightOfQuestionDot) {
		getTypeScriptMemberSymbols();
	} else if (isRightOfOpenTag) {
		symbols = typeChecker->GetJsxIntrinsicTagNamesAt(location);
		checkEachDefined(symbols,
		                 "GetJsxIntrinsicTagNamesAt() should all be defined");
		if (auto [_, err] = tryGetGlobalSymbols(); err != nullptr) {
			return {completionData{}, err};
		}
		completionKind = CompletionKindGlobal;
		keywordFilters = KeywordCompletionFiltersNone;
	} else if (isStartingCloseTag) {
		Node* tagName = contextToken->parent->parent->as<JsxElement>()
		                    ->OpeningElement->tagName();
		Symbol* tagSymbol = typeChecker->GetSymbolAtLocation(tagName);
		if (tagSymbol != nullptr) {
			symbols = {tagSymbol};
		}
		completionKind = CompletionKindGlobal;
		keywordFilters = KeywordCompletionFiltersNone;
	} else {
		// For JavaScript or TypeScript, if we're not after a dot, then just
		// try to get the global symbols in scope.  These results should be
		// valid for either language as the set of symbols that can be
		// referenced from this location.
		auto [ok, err] = tryGetGlobalSymbols();
		if (!ok) {
			if (err != nullptr) {
				return {completionData{}, err};
			}
			if (keywordFilters != KeywordCompletionFiltersNone) {
				return {completionData{keywordCompletionData(
				            keywordFilters, isJSOnlyLocation,
				            isNewIdentifierLocation)},
				        nullptr};
			}
			return {completionData{}, nullptr};
		}
	}

	checker::Type* contextualTypeOrConstraint = nullptr;
	if (previousToken != nullptr) {
		contextualTypeOrConstraint = getContextualType(
		    previousToken, position, file, typeChecker);
		if (contextualTypeOrConstraint == nullptr) {
			contextualTypeOrConstraint = getConstraintOfTypeArgumentProperty(
			    previousToken, typeChecker);
		}
	}

	// exclude literal suggestions after <input type="text" [||] />
	// (microsoft/TypeScript#51667) and after closing quote
	// (microsoft/TypeScript#52675) for strings getStringLiteralCompletions
	// handles completions
	bool isLiteralExpected =
	    !(previousToken != nullptr &&
	      isStringLiteralLike(previousToken)) &&
	    !isJsxIdentifierExpected;
	std::vector<literalValue> literals;
	if (isLiteralExpected) {
		std::vector<checker::Type*> types;
		if (contextualTypeOrConstraint != nullptr &&
		    contextualTypeOrConstraint->IsUnion()) {
			types = contextualTypeOrConstraint->types();
		} else if (contextualTypeOrConstraint != nullptr) {
			types = {contextualTypeOrConstraint};
		}
		literals = mapNonNil(types, [](checker::Type* t) -> literalValue {
			if (isLiteral(t) && !t->IsEnumLiteral()) {
				return t->AsLiteralType()->value;
			}
			return literalValue{};
		});
	}

	Symbol* recommendedCompletion = nullptr;
	if (previousToken != nullptr && contextualTypeOrConstraint != nullptr) {
		recommendedCompletion = getRecommendedCompletion(
		    previousToken, contextualTypeOrConstraint, typeChecker);
	}

	if (!defaultCommitCharacters.has_value()) {
		defaultCommitCharacters =
		    getDefaultCommitCharacters(isNewIdentifierLocation);
	}

	auto* data = new completionDataData{};
	data->symbols = symbols;
	data->autoImports = autoImports;
	data->completionKind = completionKind;
	data->isInSnippetScope = isInSnippetScope;
	data->propertyAccessToConvert = propertyAccessToConvert;
	data->isNewIdentifierLocation = isNewIdentifierLocation;
	data->location = location;
	data->keywordFilters = keywordFilters;
	data->literals = literals;
	data->symbolToOriginInfoMap = std::move(symbolToOriginInfoMap);
	data->symbolToSortTextMap = std::move(symbolToSortTextMap);
	data->recommendedCompletion = recommendedCompletion;
	data->previousToken = previousToken;
	data->contextToken = contextToken;
	data->jsxInitializer = jsxInitializer;
	data->insideJSDocTagTypeExpression = insideJSDocTagTypeExpression;
	data->isTypeOnlyLocation = isTypeOnlyLocation;
	data->isJsxIdentifierExpected = isJsxIdentifierExpected;
	data->isRightOfOpenTag = isRightOfOpenTag;
	data->isRightOfDotOrQuestionDot = isRightOfDot || isRightOfQuestionDot;
	data->importStatementCompletion = importStatementCompletion;
	data->hasUnresolvedAutoImports = hasUnresolvedAutoImports;
	data->defaultCommitCharacters = std::move(defaultCommitCharacters);
	return {completionData{data}, nullptr};
}

// completions.go:1802
completionDataKeyword* keywordCompletionData(
    KeywordCompletionFilters keywordFilters, bool filterOutTSOnlyKeywords,
    bool isNewIdentifierLocation) {
	auto* data = new completionDataKeyword{};
	data->keywordCompletions =
	    getKeywordCompletions(keywordFilters, filterOutTSOnlyKeywords);
	data->isNewIdentifierLocation = isNewIdentifierLocation;
	return data;
}

// completions.go:1813
std::vector<std::string> getDefaultCommitCharacters(
    bool isNewIdentifierLocation) {
	if (isNewIdentifierLocation) {
		return {};
	}
	return allCommitCharacters; // slices.Clone
}

// completions.go:1820
std::pair<CompletionList*, gostd::Error>
LanguageService::completionInfoFromData(
    const ContextPtr& ctx, checker::Checker* typeChecker, SourceFile* file,
    const CompilerOptions* compilerOptions, completionDataData* data,
    int position, lsproto::Range* optionalReplacementSpan,
    bool includeSymbols) {
	KeywordCompletionFilters keywordFilters = data->keywordFilters;
	bool isNewIdentifierLocation = data->isNewIdentifierLocation;
	Node* contextToken = data->contextToken;
	std::vector<literalValue> literals = data->literals;
	const lsutil::UserPreferences& preferences = UserPreferences();

	// Verify if the file is JSX language variant
	if (file->LanguageVariant == LanguageVariant::JSX) {
		CompletionList* list =
		    getJsxClosingTagCompletion(ctx, data->location, file, position);
		if (list != nullptr) {
			return {list, nullptr};
		}
	}

	// When the completion is for the expression of a case clause (e.g. `case
	// |`), filter literals & enum symbols whose values are already present in
	// existing case clauses.
	Node* caseClause = findAncestor(contextToken, isCaseClause);
	if (caseClause != nullptr &&
	    (contextToken->kind == Kind::CaseKeyword ||
	     isNodeDescendantOf(contextToken,
	                        caseClause->expression()))) {
		caseClauseTracker* tracker = newCaseClauseTracker(
		    typeChecker,
		    caseClause->parent->as<CaseBlock>()->Clauses->nodes);
		literals = filterList(literals, [&](const literalValue& literal) {
			return !tracker->hasValue(literal);
		});
		data->symbols =
		    filterList(data->symbols, [&](Symbol* symbol) {
			    if (symbol->valueDeclaration != nullptr &&
			        isEnumMember(symbol->valueDeclaration)) {
				    auto value = typeChecker->GetConstantValue(
				        symbol->valueDeclaration);
				    if (!std::holds_alternative<std::monostate>(
				            value) &&
				        tracker->hasValue(value)) {
					    return false;
				    }
			    }
			    return true;
		    });
	}

	bool isChecked = isCheckedFile(file, compilerOptions);
	if (isChecked && !isNewIdentifierLocation && data->symbols.empty() &&
	    keywordFilters == KeywordCompletionFiltersNone) {
		return {nullptr, nullptr};
	}

	auto [uniqueNames, sortedEntries, err] = getCompletionEntriesFromSymbols(
	    ctx, typeChecker, data, /*replacementToken*/ nullptr, position, file,
	    compilerOptions, includeSymbols);
	if (err != nullptr) {
		return {nullptr, err};
	}

	if (data->keywordFilters != KeywordCompletionFiltersNone) {
		auto keywordCompletions = getKeywordCompletions(
		    data->keywordFilters,
		    !data->insideJSDocTagTypeExpression && isSourceFileJS(file));
		for (CompletionItem* keywordEntry : keywordCompletions) {
			const std::string& label = keywordEntry->completionItem->Label;
			if ((data->isTypeOnlyLocation &&
			     isTypeKeyword(stringToToken(label))) ||
			    (!data->isTypeOnlyLocation &&
			     isContextualKeywordInAutoImportableExpressionSpace(
			         label)) ||
			    !uniqueNames.Has(label)) {
				uniqueNames.Add(label);
				sortedEntries.push_back(keywordEntry);
			}
		}
	}

	for (lsproto::CompletionItem* keywordEntry :
	     getContextualKeywords(file, contextToken, position)) {
		if (!uniqueNames.Has(keywordEntry->Label)) {
			uniqueNames.Add(keywordEntry->Label);
			auto* item = new CompletionItem{};
			item->completionItem = keywordEntry;
			sortedEntries.push_back(item);
		}
	}

	for (const literalValue& literal : literals) {
		lsproto::CompletionItem* literalEntry =
		    createCompletionItemForLiteral(file, preferences, literal);
		uniqueNames.Add(literalEntry->Label);
		auto* item = new CompletionItem{};
		item->completionItem = literalEntry;
		sortedEntries.push_back(item);
	}

	if (!isChecked) {
		sortedEntries = getJSCompletionEntries(ctx, file, position,
		                                       &uniqueNames, sortedEntries);
	}

	if (contextToken != nullptr && !data->isRightOfOpenTag &&
	    !data->isRightOfDotOrQuestionDot) {
		if (Node* caseBlock =
		        findAncestorKind(contextToken, Kind::CaseBlock)) {
			auto [casesItem, err] = getExhaustiveCaseSnippets(
			    ctx, caseBlock->as<CaseBlock>(), file, position,
			    compilerOptions, program, typeChecker);
			if (err != nullptr) {
				return {nullptr, err};
			}
			if (casesItem != nullptr) {
				auto* item = new CompletionItem{};
				item->completionItem = casesItem;
				sortedEntries.push_back(item);
			}
		}
	}

	lsproto::CompletionItemDefaults* itemDefaults = setItemDefaults(
	    ctx, position, file, sortedEntries,
	    data->defaultCommitCharacters.has_value()
	        ? &data->defaultCommitCharacters.value()
	        : nullptr,
	    optionalReplacementSpan);

	auto* list = new CompletionList{};
	list->IsIncomplete = data->hasUnresolvedAutoImports;
	list->ItemDefaults = itemDefaults;
	list->Items = std::move(sortedEntries);
	return {list, nullptr};
}

// completions.go:1955
std::tuple<collections::Set<std::string>, std::vector<CompletionItem*>,
           gostd::Error>
LanguageService::getCompletionEntriesFromSymbols(
    const ContextPtr& ctx, checker::Checker* typeChecker,
    completionDataData* data, Node* replacementToken, int position,
    SourceFile* file, const CompilerOptions* compilerOptions,
    bool includeSymbols) {
	collections::Set<std::string> uniqueNames;
	std::vector<CompletionItem*> sortedEntries;
	Node* closestSymbolDeclaration =
	    getClosestSymbolDeclaration(data->contextToken, data->location);
	bool useSemicolons = lsutil::ProbablyUsesSemicolons(file);
	const lsutil::UserPreferences& preferences = UserPreferences();
	bool isMemberCompletion = isMemberCompletionKind(data->completionKind);
	sortedEntries.reserve(data->symbols.size() + data->autoImports.size());
	// Tracks unique names.
	// Value is set to false for global variables or completions from external
	// module exports, because we can have multiple of those; true otherwise.
	// Based on the order we add things we will always see locals first, then
	// globals, then module exports. So adding a completion for a local will
	// prevent us from adding completions for external module exports sharing
	// the same name.
	uniqueNamesMap uniques;
	for (size_t index = 0; index < data->symbols.size(); index++) {
		Symbol* symbol = data->symbols[index];
		symbolOriginInfo* origin = nullptr;
		if (auto it = data->symbolToOriginInfoMap.find(int(index));
		    it != data->symbolToOriginInfoMap.end()) {
			origin = it->second;
		}
		auto [name, needsConvertPropertyAccess] =
		    getCompletionEntryDisplayNameForSymbol(
		        file, preferences, symbol, origin, data->completionKind,
		        data->isJsxIdentifierExpected);
		auto uit = uniques.find(name);
		if (name.empty() ||
		    (uit != uniques.end() && uit->second &&
		     (origin == nullptr || !originIsObjectLiteralMethod(origin))) ||
		    (data->completionKind == CompletionKindGlobal &&
		     !shouldIncludeSymbol(symbol, data, closestSymbolDeclaration,
		                          file, typeChecker, compilerOptions))) {
			continue;
		}

		// When in a value location in a JS file, ignore symbols that
		// definitely seem to be type-only.
		if (!data->isTypeOnlyLocation && isSourceFileJS(file) &&
		    symbolAppearsToBeTypeOnly(symbol, typeChecker)) {
			continue;
		}

		SortText originalSortText;
		if (auto it =
		        data->symbolToSortTextMap.find(getSymbolId(symbol));
		    it != data->symbolToSortTextMap.end()) {
			originalSortText = it->second;
		}
		if (originalSortText.empty()) {
			originalSortText = SortTextLocationPriority;
		}

		SortText sortText;
		if (isDeprecated(symbol, typeChecker)) {
			sortText = DeprecateSortText(originalSortText);
		} else {
			sortText = originalSortText;
		}
		auto [entry, err] = createCompletionItem(
		    ctx, typeChecker, symbol, sortText, replacementToken, data,
		    position, file, name, needsConvertPropertyAccess, origin,
		    useSemicolons, compilerOptions, isMemberCompletion);
		if (err != nullptr) {
			return {uniqueNames, {}, err};
		}
		if (entry == nullptr) {
			continue;
		}

		// True for locals; false for globals, module exports from other
		// files, `this.` completions.
		bool shouldShadowLaterSymbols =
		    (origin == nullptr || originIsTypeOnlyAlias(origin)) &&
		    !(symbol->parent == nullptr &&
		      !someList(symbol->declarations, [&](Node* d) {
			      return getSourceFileOfNode(d) == file;
		      }));
		uniques[name] = shouldShadowLaterSymbols;
		Symbol* sym = nullptr;
		if (includeSymbols) {
			sym = symbol;
		}
		auto* item = new CompletionItem{};
		item->completionItem = entry;
		item->Symbol = sym;
		sortedEntries.push_back(item);
	}

	for (autoimport::FixAndExport* autoImport : data->autoImports) {
		// !!! check for type-only in JS
		// !!! deprecation

		lsproto::Range* replacementSpan = nullptr;
		std::string insertText;
		std::string filterText;
		bool isSnippet = false;
		SortText sortText = SortTextAutoImportSuggestions;

		if (data->importStatementCompletion != nullptr) {
			isSnippet = clientSupportsItemSnippet(ctx);
			auto pr = getInsertTextAndReplacementSpanForImportCompletion(
			    autoImport->Fix.get(),
			    autoimport::GetImportKindForImportStatement(
			        file, autoImport->Export, GetProgram()),
			    data->importStatementCompletion, useSemicolons, file,
			    preferences, isSnippet);
			insertText = pr.first;
			replacementSpan = pr.second;
			// The edit range covers the whole import statement typed so
			// far, and clients match that text against the filter text, so
			// it has to be the statement being inserted (as in Strada), not
			// just the bare name.
			filterText = insertText;
			sortText = SortTextLocationPriority;
		}

		// Non-contextual keywords (e.g., `function`, `class`, `const`)
		// cannot be used as identifiers, so auto-imports with these names
		// should not shadow keyword completions.
		if (Kind token = stringToToken(autoImport->Fix->AutoImportFix->Name);
		    token != Kind::Unknown && isNonContextualKeyword(token)) {
			continue;
		}

		if (!autoImport->Export->IsUnresolvedAlias()) {
			if (data->isTypeOnlyLocation) {
				if (!(autoImport->Export->Flags & SymbolFlagsType) &&
				    !(autoImport->Export->Flags & SymbolFlagsModule)) {
					continue;
				}
			} else if (data->importStatementCompletion == nullptr &&
			           !(autoImport->Export->Flags & SymbolFlagsValue)) {
				continue;
			}
		}

		auto* labelDetails = new lsproto::CompletionItemLabelDetails{};
		labelDetails->Description =
		    autoImport->Fix->AutoImportFix->ModuleSpecifier;
		lsproto::CompletionItem* entry = createLSPCompletionItem(
		    ctx, autoImport->Fix->AutoImportFix->Name, insertText,
		    filterText, sortText,
		    autoImport->Export->ScriptElementKind,
		    autoImport->Export->ScriptElementKindModifiers,
		    replacementSpan, /*commitCharacters*/ nullptr, labelDetails,
		    file, position, /*isMemberCompletion*/ false, isSnippet,
		    /*hasAction*/ data->importStatementCompletion == nullptr,
		    /*preselect*/ false,
		    autoImport->Fix->AutoImportFix->ModuleSpecifier,
		    autoImport->Fix->AutoImportFix,
		    /*additionalTextEdits*/ nullptr, /*detail*/ nullptr);

		entry->Data->IsImportStatementCompletion =
		    data->importStatementCompletion != nullptr;

		auto uit = uniques.find(autoImport->Fix->AutoImportFix->Name);
		bool isShadowed = uit != uniques.end() && uit->second;
		if (!isShadowed) {
			uniques[autoImport->Fix->AutoImportFix->Name] = false;
			auto* item = new CompletionItem{};
			item->completionItem = entry;
			sortedEntries.push_back(item);
		}
	}

	auto uniqueSet =
	    collections::NewSetWithSizeHint<std::string>(uniques.size());
	for (auto& [name, _] : uniques) {
		uniqueSet.Add(name);
	}
	return {uniqueSet, sortedEntries, nullptr};
}

// completions.go:2126
std::string completionNameForLiteral(
    SourceFile* file, const lsutil::UserPreferences& preferences,
    const literalValue& literal) {
	if (auto* s = std::get_if<std::string>(&literal)) {
		return quote(file, preferences, *s);
	}
	if (auto* n = std::get_if<Number>(&literal)) {
		// core.StringifyJson(literal, "" /*prefix*/, "" /*suffix*/)
		return n->string();
	}
	if (auto* b = std::get_if<PseudoBigInt>(&literal)) {
		return b->string() + "n";
	}
	TSC_UNREACHABLE("Unhandled literal value in completionNameForLiteral");
}

// completions.go:2143
std::pair<std::string, lsproto::Range*>
getInsertTextAndReplacementSpanForImportCompletion(
    autoimport::Fix* fix, lsproto::ImportKind importKind,
    importStatementCompletionInfo* importStatementCompletion,
    bool useSemicolons, SourceFile* file,
    const lsutil::UserPreferences& preferences, bool isSnippet) {
	std::string quotedModuleSpecifier = escapeSnippetText(
	    quote(file, preferences, fix->AutoImportFix->ModuleSpecifier));
	std::string tabStop = ifElse<std::string>(isSnippet, "$1", "");
	std::string suffix = ifElse<std::string>(useSemicolons, ";", "");
	std::string topLevelTypeOnlyText =
	    ifElse<std::string>(importStatementCompletion->isTopLevelTypeOnly,
	                        " " + std::string(tokenToString(Kind::TypeKeyword)) + " ",
	                        " ");
	std::string name = escapeSnippetText(fix->AutoImportFix->Name);
	lsproto::Range* replacementSpan = importStatementCompletion->replacementSpan;

	switch (importKind) {
	case lsproto::ImportKindCommonJS:
		return {"import" + topLevelTypeOnlyText + name + tabStop +
		            " = require(" + quotedModuleSpecifier + ")" + suffix,
		        replacementSpan};
	case lsproto::ImportKindDefault:
		return {"import" + topLevelTypeOnlyText + name + tabStop +
		            " from " + quotedModuleSpecifier + suffix,
		        replacementSpan};
	case lsproto::ImportKindNamespace:
		return {"import" + topLevelTypeOnlyText + "* as " + name +
		            " from " + quotedModuleSpecifier + suffix,
		        replacementSpan};
	case lsproto::ImportKindNamed:
		return {"import" + topLevelTypeOnlyText + "{ " +
		            ifElse<std::string>(
		                importStatementCompletion
		                    ->couldBeTypeOnlyImportSpecifier,
		                std::string(tokenToString(Kind::TypeKeyword)) + " ",
		                "") +
		            name + tabStop + " } from " + quotedModuleSpecifier +
		            suffix,
		        replacementSpan};
	default:
		TSC_UNREACHABLE("unhandled import kind in "
		                "getInsertTextAndReplacementSpanForImportCompletion");
	}
}

// completions.go:2165
lsproto::CompletionItem* createCompletionItemForLiteral(
    SourceFile* file, const lsutil::UserPreferences& preferences,
    const literalValue& literal) {
	auto* item = new lsproto::CompletionItem{};
	item->Label = completionNameForLiteral(file, preferences, literal);
	item->Kind = std::make_shared<lsproto::CompletionItemKind>(
	    lsproto::CompletionItemKindConstant);
	item->SortText = std::string(SortTextLocationPriority);
	item->CommitCharacters =
	    std::make_shared<lsproto::Slice<std::string>>(
	        std::vector<std::string>{});
	return item;
}

// completions.go:2178
std::pair<lsproto::CompletionItem*, gostd::Error>
LanguageService::createCompletionItem(
    const ContextPtr& ctx, checker::Checker* typeChecker, Symbol* symbol,
    SortText sortText, Node* replacementToken,
    completionDataData* data, int position, SourceFile* file,
    std::string name, bool needsConvertPropertyAccess,
    symbolOriginInfo* origin, bool useSemicolons,
    const CompilerOptions* compilerOptions, bool isMemberCompletion) {
	Node* contextToken = data->contextToken;
	std::string insertText;
	std::string filterText;
	lsproto::Range* replacementSpan = getReplacementRangeForContextToken(
	    file, replacementToken, position);
	bool isSnippet = false;
	bool hasAction = false;
	std::string source = getSourceFromOrigin(origin);
	lsproto::CompletionItemLabelDetails* labelDetails = nullptr;
	const lsutil::UserPreferences& preferences = UserPreferences();
	bool insertQuestionDot = originIsNullableMember(origin);
	bool useBraces =
	    originIsSymbolMember(origin) || needsConvertPropertyAccess;
	if (originIsThisTypeNode(origin)) {
		if (needsConvertPropertyAccess) {
			insertText = "this" +
			    ifElse<std::string>(insertQuestionDot, "?.", "") +
			    "[" + quotePropertyName(file, preferences, name) + "]";
		} else {
			insertText = "this" +
			    ifElse<std::string>(insertQuestionDot, "?.", ".") + name;
		}
	} else if (data->propertyAccessToConvert != nullptr &&
	           (useBraces || insertQuestionDot)) {
		// We should only have needsConvertPropertyAccess if there's a
		// property access to convert. But see microsoft/TypeScript#21790.
		// Somehow there was a global with a non-identifier name. Hopefully
		// someone will complain about getting a "foo bar" global
		// completion and provide a repro.
		if (useBraces) {
			if (needsConvertPropertyAccess) {
				insertText =
				    "[" + quotePropertyName(file, preferences, name) +
				    "]";
			} else {
				insertText = "[" + name + "]";
			}
		} else {
			insertText = name;
		}

		if (insertQuestionDot ||
		    data->propertyAccessToConvert->questionDotToken() != nullptr) {
			insertText = "?." + insertText;
		}

		Node* dot = astnav::findChildOfKind(
		    data->propertyAccessToConvert, Kind::DotToken, file);
		if (dot == nullptr) {
			dot = astnav::findChildOfKind(data->propertyAccessToConvert,
			                              Kind::QuestionDotToken, file);
		}

		if (dot == nullptr) {
			return {nullptr, nullptr};
		}

		// If the text after the '.' starts with this name, write over it.
		// Else, add new text.
		int end;
		if (name.starts_with(
		        data->propertyAccessToConvert->name()->text())) {
			end = data->propertyAccessToConvert->end();
		} else {
			end = dot->end();
		}
		auto [lspRange, fidelity] = createLspRangeFromBounds(
		    astnav::getStartOfNode(dot, file, /*includeJSDoc*/ false),
		    end, file);
		if (!fidelity.IsExact()) {
			return {nullptr, nullptr};
		}
		replacementSpan = new lsproto::Range(lspRange);
	}

	if (data->jsxInitializer.isInitializer) {
		if (insertText.empty()) {
			insertText = name;
		}
		insertText = "{" + insertText + "}";
		if (data->jsxInitializer.initializer != nullptr) {
			auto [lspRange, fidelity] = createLspRangeFromNode(
			    data->jsxInitializer.initializer, file);
			if (!fidelity.IsExact()) {
				return {nullptr, nullptr};
			}
			replacementSpan = new lsproto::Range(lspRange);
		}
	}

	if (originIsPromise(origin) &&
	    data->propertyAccessToConvert != nullptr) {
		if (insertText.empty()) {
			insertText = name;
		}
		Node* precedingToken = astnav::findPrecedingToken(
		    file, data->propertyAccessToConvert->pos());
		std::string awaitText;
		if (precedingToken != nullptr &&
		    lsutil::PositionIsASICandidate(precedingToken->end(),
		                                   precedingToken->parent, file)) {
			awaitText = ";";
		}

		awaitText += "(await " +
		    getTextOfNode(
		        data->propertyAccessToConvert->expression()) +
		    ")";
		if (needsConvertPropertyAccess) {
			insertText = awaitText + insertText;
		} else {
			std::string dotStr =
			    ifElse<std::string>(insertQuestionDot, "?.", ".");
			insertText = awaitText + dotStr + insertText;
		}
		bool isInAwaitExpression =
		    isAwaitExpression(data->propertyAccessToConvert->parent);
		Node* wrapNode =
		    isInAwaitExpression
		        ? data->propertyAccessToConvert->parent
		        : data->propertyAccessToConvert->expression();
		auto [lspRange, fidelity] = createLspRangeFromBounds(
		    astnav::getStartOfNode(wrapNode, file,
		                           /*includeJSDoc*/ false),
		    data->propertyAccessToConvert->end(), file);
		if (!fidelity.IsExact()) {
			return {nullptr, nullptr};
		}
		replacementSpan = new lsproto::Range(lspRange);
	}

	if (originIsTypeOnlyAlias(origin)) {
		hasAction = true;
	}

	// Provide object member completions when missing commas, and insert
	// missing commas.
	// For example:
	//
	//    interface I {
	//        a: string;
	//        b: number
	//     }
	//
	//     const cc: I = { a: "red" | }
	//
	// Completion should add a comma after "red" and provide completions
	// for b
	if (data->completionKind == CompletionKindObjectPropertyDeclaration &&
	    contextToken != nullptr &&
	    !nodeHasKind(astnav::findPrecedingTokenEx(
	                     file, contextToken->pos(), contextToken,
	                     /*excludeJSDoc*/ false),
	                 Kind::CommaToken)) {
		if (isMethodDeclaration(contextToken->parent->parent) ||
		    isGetAccessorDeclaration(contextToken->parent->parent) ||
		    isSetAccessorDeclaration(contextToken->parent->parent) ||
		    isSpreadAssignment(contextToken->parent) ||
		    lsutil::GetLastToken(
		        findAncestor(contextToken->parent, isPropertyAssignment),
		        file) == contextToken ||
		    (isShorthandPropertyAssignment(contextToken->parent) &&
		     getLineOfPosition(file, contextToken->end()) !=
		         getLineOfPosition(file, position))) {
			source = completionSourceObjectLiteralMemberWithComma;
			hasAction = true;
		}
	}

	std::vector<std::shared_ptr<lsproto::TextEdit>>* additionalTextEdits =
	    nullptr;
	if (tristateIsTrue(
	        preferences.IncludeCompletionsWithClassMemberSnippets) &&
	    data->completionKind == CompletionKindMemberLike &&
	    isClassLikeMemberCompletion(symbol, data->location, file)) {
		auto [memberCompletionEntry, err] = getEntryForMemberCompletion(
		    ctx, typeChecker, symbol, name, data->location, position,
		    contextToken, file);
		if (err != nullptr) {
			return {nullptr, err};
		}
		if (memberCompletionEntry == nullptr) {
			return {nullptr, nullptr};
		}
		insertText = memberCompletionEntry->insertText;
		filterText = memberCompletionEntry->filterText;
		isSnippet = memberCompletionEntry->isSnippet;
		if (!memberCompletionEntry->additionalTextEdits.empty()) {
			additionalTextEdits =
			    &memberCompletionEntry->additionalTextEdits;
			hasAction = true;
			source = completionSourceClassMemberSnippet;
		}
	}

	if (originIsObjectLiteralMethod(origin)) {
		insertText = origin->asObjectLiteralMethod()->insertText;
		isSnippet = origin->asObjectLiteralMethod()->isSnippet;
		labelDetails = origin->asObjectLiteralMethod()->labelDetails;
		if (!clientSupportsItemLabelDetails(ctx)) {
			name = name +
			    *origin->asObjectLiteralMethod()->labelDetails->Detail;
			labelDetails = nullptr;
		}
		source = completionSourceObjectLiteralMethodSnippet;
		sortText = SortBelow(sortText);
	}

	if (data->isJsxIdentifierExpected && !data->isRightOfOpenTag &&
	    clientSupportsItemSnippet(ctx) &&
	    preferences.JsxAttributeCompletionStyle !=
	        lsutil::JsxAttributeCompletionStyleNone &&
	    !(data->location->parent != nullptr &&
	      isJsxAttribute(data->location->parent) &&
	      data->location->parent->initializer() != nullptr)) {
		bool useBraces =
		    preferences.JsxAttributeCompletionStyle ==
		    lsutil::JsxAttributeCompletionStyleBraces;
		checker::Type* t =
		    typeChecker->GetTypeOfSymbolAtLocation(symbol, data->location);

		// If is boolean like or undefined, don't return a snippet, we want
		// to return just the completion.
		if (preferences.JsxAttributeCompletionStyle ==
		        lsutil::JsxAttributeCompletionStyleAuto &&
		    !t->IsBooleanLike() &&
		    !(t->IsUnion() &&
		      someList(t->types(),
		               [](checker::Type* t) { return t->IsBooleanLike(); }))) {
			if (t->IsStringLike() ||
			    (t->IsUnion() &&
			     everyList(t->types(), [&](checker::Type* t) {
				     return (t->flags &
				             (checker::TypeFlagsStringLike |
				              checker::TypeFlagsUndefined)) != 0 ||
				         isStringAndEmptyAnonymousObjectIntersection(
				             typeChecker, t);
			     }))) {
				// If type is string-like or undefined, use quotes.
				insertText = escapeSnippetText(name) + "=" +
				    quote(file, preferences, "$1");
				isSnippet = true;
			} else {
				// Use braces for everything else.
				useBraces = true;
			}
		}

		if (useBraces) {
			insertText = escapeSnippetText(name) + "={$1}";
			isSnippet = true;
		}
	}

	Node* parentNamedImportOrExport =
	    findAncestor(data->location, isNamedImportsOrExports);
	if (parentNamedImportOrExport != nullptr) {
		if (!isIdentifierText(name, LanguageVariant::Standard)) {
			insertText = quotePropertyName(file, preferences, name);

			if (parentNamedImportOrExport->kind == Kind::NamedImports) {
				// Check if it is `import { ^here as name } from '...'``.
				// We have to access the scanner here to check if it is
				// `{ ^here as name }`` or `{ ^here, as, name }`.
				Scanner scanner;
				scanner.setText(file->Text());
				scanner.resetPos(position);
				if (!(scanner.scan() == Kind::AsKeyword &&
				      scanner.scan() == Kind::Identifier)) {
					insertText +=
					    " as " +
					    generateIdentifierForArbitraryString(name);
				}
			}
		} else if (parentNamedImportOrExport->kind == Kind::NamedImports) {
			Kind possibleToken = stringToToken(name);
			if (possibleToken != Kind::Unknown &&
			    (possibleToken == Kind::AwaitKeyword ||
			     lsutil::IsNonContextualKeyword(possibleToken))) {
				insertText = name + " as " + name + "_";
			}
		}
	}

	// Commit characters

	lsutil::ScriptElementKind elementKind =
	    lsutil::GetSymbolKind(typeChecker, symbol, data->location);
	std::vector<std::string>* commitCharacters = nullptr;
	if (clientSupportsItemCommitCharacters(ctx)) {
		if (elementKind == lsutil::ScriptElementKindWarning ||
		    elementKind == lsutil::ScriptElementKindString) {
			commitCharacters = new std::vector<std::string>{};
		} else if (!clientSupportsDefaultCommitCharacters(ctx)) {
			commitCharacters = new std::vector<std::string>(
			    data->defaultCommitCharacters.value_or(
			        std::vector<std::string>{}));
		}
		// Otherwise use the completion list default.
	}

	bool preselect = isRecommendedCompletionMatch(
	    symbol, data->recommendedCompletion, typeChecker);
	lsutil::ScriptElementKindModifier kindModifiers =
	    lsutil::GetSymbolModifiers(typeChecker, symbol);

	return {createLSPCompletionItem(
	            ctx, name, insertText, filterText, sortText, elementKind,
	            kindModifiers, replacementSpan, commitCharacters,
	            labelDetails, file, position, isMemberCompletion, isSnippet,
	            hasAction, preselect, source, /*autoImportFix*/ nullptr,
	            additionalTextEdits, /*detail*/ nullptr),
	        nullptr};
}

// completions.go:2474
symbolOriginInfoObjectLiteralMethod*
LanguageService::getEntryForObjectLiteralMethodCompletion(
    const ContextPtr& ctx, checker::Checker* typeChecker, Symbol* symbol,
    Node* enclosingDeclaration, SourceFile* file) {
	snippetPrinter* snippetPrinter = createSnippetPrinter(
	    printer::PrinterOptions{
	        .RemoveComments = true,
	        .NewLine =
	            getNewLineKind(FormatOptions().NewLineCharacter),
	        .Target = GetProgram()->Options()->GetEmitScriptTarget(),
	    },
	    /*emitContext*/ nullptr);

	bool isSnippet = clientSupportsItemSnippet(ctx);
	Node* method =
	    createObjectLiteralMethod(snippetPrinter, typeChecker, symbol,
	                              enclosingDeclaration, file, isSnippet);
	if (method == nullptr) {
		return nullptr;
	}

	std::string insertText =
	    snippetPrinter->printAndFormatNodeWithSettings(
	        ctx, method, file,
	        change::GetFormatCodeSettingsForWriting(FormatOptions(), file));
	insertText += ",";

	auto* entry = new symbolOriginInfoObjectLiteralMethod{};
	entry->insertText = insertText;
	entry->labelDetails = new lsproto::CompletionItemLabelDetails{
	    .Detail = printObjectLiteralMethodLabelDetail(
	        method, file, snippetPrinter->factory)};
	entry->isSnippet = isSnippet;
	return entry;
}

// completions.go:2504
Node* LanguageService::createObjectLiteralMethod(
    snippetPrinter* snippetPrinter, checker::Checker* typeChecker,
    Symbol* symbol, Node* enclosingDeclaration, SourceFile* file,
    bool isSnippet) {
	NodeFactory* factory = snippetPrinter->factory;
	printer::EmitContext* emitContext = snippetPrinter->emitContext;

	Node* declaration = firstOrNil(symbol->declarations);
	if (!isObjectLiteralMethodCompletionCandidateDeclaration(declaration)) {
		return nullptr;
	}

	checker::Type* effectiveType = typeChecker->GetWidenedType(
	    typeChecker->GetTypeOfSymbolAtLocation(symbol,
	                                           enclosingDeclaration));
	if ((effectiveType->flags & checker::TypeFlagsUnion) &&
	    effectiveType->types().size() < 10) {
		effectiveType = typeChecker->GetUnionTypeEx(
		    effectiveType->types(), checker::UnionReductionSubtype);
	}
	if (effectiveType->flags & checker::TypeFlagsUnion) {
		checker::Type* functionType = nullptr;
		for (checker::Type* unionType : effectiveType->types()) {
			if (typeChecker
			        ->GetSignaturesOfType(
			            unionType, checker::SignatureKind::Call)
			        .empty()) {
				continue;
			}
			if (functionType != nullptr) {
				return nullptr;
			}
			functionType = unionType;
		}
		if (functionType == nullptr) {
			return nullptr;
		}
		effectiveType = functionType;
	}

	auto signatures = typeChecker->GetSignaturesOfType(
	    effectiveType, checker::SignatureKind::Call);
	if (signatures.size() != 1) {
		return nullptr;
	}

	nodebuilder::Flags flags = nodebuilder::FlagsOmitThisParameter;
	if (lsutil::GetQuotePreference(file, UserPreferences()) ==
	    lsutil::QuotePreferenceSingle) {
		flags |= nodebuilder::FlagsUseSingleQuotesForStringLiteralType;
	}
	Node* typeNode = typeChecker->TypeToTypeNode(
	    effectiveType, enclosingDeclaration, flags, /*idToSymbol*/ nullptr);
	if (typeNode == nullptr || typeNode->kind != Kind::FunctionType) {
		return nullptr;
	}

	std::vector<Node*> parameters;
	parameters.reserve(
	    typeNode->as<FunctionTypeNode>()->Parameters->nodes.size());
	for (Node* parameter :
	     typeNode->as<FunctionTypeNode>()->Parameters->nodes) {
		parameters.push_back(factory->newParameterDeclaration(
		    /*modifiers*/ nullptr,
		    parameter->as<ParameterDeclaration>()->DotDotDotToken,
		    deepCloneNode(*factory, parameter->name()),
		    /*questionToken*/ nullptr, /*typeNode*/ nullptr,
		    parameter->as<ParameterDeclaration>()->Initializer));
	}

	Node* body = factory->newBlock(factory->newNodeList({}), /*multiLine*/ true);
	if (isSnippet) {
		body = createSnippetTabStopBody(factory, emitContext);
	}

	return factory->newMethodDeclaration(
	    /*modifiers*/ nullptr, /*asteriskToken*/ nullptr,
	    deepCloneNode(*factory, declaration->name()), /*postfixToken*/ nullptr,
	    /*typeParameters*/ nullptr, factory->newNodeList(parameters),
	    /*typeNode*/ nullptr, /*fullSignature*/ nullptr, body);
}

// completions.go:2566
bool isObjectLiteralMethodCompletionCandidateDeclaration(
    Node* declaration) {
	if (declaration == nullptr) {
		return false;
	}
	switch (declaration->kind) {
	case Kind::PropertySignature:
	case Kind::PropertyDeclaration:
	case Kind::MethodSignature:
	case Kind::MethodDeclaration:
		return true;
	default:
		return false;
	}
}

// completions.go:2589
std::vector<objectLiteralMethodSymbol>
LanguageService::collectObjectLiteralMethodSymbols(
    const ContextPtr& ctx, checker::Checker* typeChecker,
    const std::vector<Symbol*>& members, Node* enclosingDeclaration,
    SourceFile* file) {
	if (isSourceFileJS(file)) {
		return {};
	}

	const lsutil::UserPreferences& preferences = UserPreferences();
	std::vector<objectLiteralMethodSymbol> methods;
	for (Symbol* member : members) {
		if (!isObjectLiteralMethodSymbol(member)) {
			continue;
		}
		auto [displayName, _] = getCompletionEntryDisplayNameForSymbol(
		    file, preferences, member, /*origin*/ nullptr,
		    CompletionKindObjectPropertyDeclaration,
		    /*isJsxIdentifierExpected*/ false);
		if (displayName.empty()) {
			continue;
		}
		symbolOriginInfoObjectLiteralMethod* entry =
		    getEntryForObjectLiteralMethodCompletion(
		        ctx, typeChecker, member, enclosingDeclaration, file);
		if (entry == nullptr) {
			continue;
		}
		methods.push_back(objectLiteralMethodSymbol{
		    .symbol = member,
		    .origin = new symbolOriginInfo{
		        .kind = symbolOriginInfoKindObjectLiteralMethod,
		        .data = entry,
		    },
		});
	}
	return methods;
}

// completions.go:2616
bool isObjectLiteralMethodSymbol(Symbol* symbol) {
	return (symbol->flags & (SymbolFlagsProperty | SymbolFlagsMethod)) != 0;
}

// completions.go:2620
std::string LanguageService::printObjectLiteralMethodLabelDetail(
    Node* method, SourceFile* file, NodeFactory* factory) {
	MethodDeclaration* methodDeclaration = method->as<MethodDeclaration>();
	Node* methodSignature = factory->newMethodSignatureDeclaration(
	    /*modifiers*/ nullptr, factory->newIdentifier(""),
	    methodDeclaration->PostfixToken, methodDeclaration->TypeParameters,
	    methodDeclaration->Parameters, methodDeclaration->Type);
	printer::Printer* signaturePrinter = printer::NewPrinter(
	    printer::PrinterOptions{
	        .RemoveComments = true,
	        .OmitTrailingSemicolon = true,
	        .NewLine =
	            getNewLineKind(FormatOptions().NewLineCharacter),
	        .Target = GetProgram()->Options()->GetEmitScriptTarget(),
	    },
	    printer::PrintHandlers{}, /*emitContext*/ nullptr);
	return signaturePrinter->Emit(methodSignature, file);
}

// completions.go:2639
std::pair<memberCompletionEntry*, gostd::Error>
LanguageService::getEntryForMemberCompletion(
    const ContextPtr& ctx, checker::Checker* typeChecker, Symbol* symbol,
    const std::string& name, Node* location, int position,
    Node* contextToken, SourceFile* file) {
	Node* classLikeDeclaration = findAncestor(location, isClassLike);
	if (classLikeDeclaration == nullptr) {
		return {nullptr, nullptr};
	}

	auto [importAdder, err] = createImportAdder(ctx, typeChecker, file);
	if (err != nullptr) {
		return {nullptr, err};
	}

	change::Tracker* changeTracker =
	    new change::Tracker{format::FormatRequestContext{},
	                        GetProgram()->Options(), FormatOptions(),
	                        converters};
	missingMemberFixer* fixer = newMissingMemberFixer(
	    changeTracker, GetProgram(), typeChecker, UserPreferences(),
	    importAdder, locale::fromContext(ctx));

	presentMemberModifiers presentModifiers =
	    getPresentMemberModifiers(contextToken, file, position);
	bool abstract =
	    (presentModifiers.modifiers & ModifierFlagsAbstract) &&
	    (classLikeDeclaration->modifierFlags() & ModifierFlagsAbstract);
	bool isSnippet = clientSupportsItemSnippet(ctx);
	Node* body = changeTracker->nodeFactory->newBlock(
	    changeTracker->nodeFactory->newNodeList({}), /*multiLine*/ true);
	if (isSnippet) {
		body = createSnippetTabStopBody(changeTracker->nodeFactory,
		                                changeTracker->emitContext);
	}

	std::vector<Node*> nodes = fixer->createMemberFromSymbol(
	    symbol, classLikeDeclaration, file, body,
	    preserveOptionalFlagsProperty, abstract);
	std::vector<std::shared_ptr<lsproto::TextEdit>> additionalTextEdits;
	if (importAdder != nullptr && importAdder->HasFixes()) {
		additionalTextEdits =
		    importAdder->Edits().value_or(
		        std::vector<std::shared_ptr<lsproto::TextEdit>>{});
	}
	if (presentModifiers.eraseRange != nullptr) {
		auto edit = std::make_shared<lsproto::TextEdit>();
		edit->Range = *presentModifiers.eraseRange;
		edit->NewText = "";
		additionalTextEdits.push_back(edit);
	}

	ModifierFlags modifiers = ModifierFlagsNone;
	std::vector<Node*> completionNodes;
	completionNodes.reserve(nodes.size());
	for (Node* node : nodes) {
		if (node == nullptr) {
			continue;
		}
		if (completionNodes.empty()) {
			modifiers = node->modifierFlags();
			if (abstract) {
				modifiers = static_cast<ModifierFlags>(
				    modifiers | ModifierFlagsAbstract);
			}
			if (isClassElement(node) &&
			    typeChecker->GetMemberOverrideModifierStatus(
			        classLikeDeclaration, node, symbol) ==
			        checker::MemberOverrideStatus::NeedsOverride) {
				modifiers = static_cast<ModifierFlags>(
				    modifiers | ModifierFlagsOverride);
			}
		}
		completionNodes.push_back(node);
	}

	if (completionNodes.empty()) {
		return {new memberCompletionEntry{
		            .insertText = name,
		            .filterText = name,
		            .isSnippet = isSnippet,
		            .additionalTextEdits = additionalTextEdits,
		        },
		        nullptr};
	}

	ModifierFlags allowedModifiers = static_cast<ModifierFlags>(
	    modifiers | ModifierFlagsOverride | ModifierFlagsPublic);
	if (symbol->flags & SymbolFlagsMethod) {
		allowedModifiers =
		    static_cast<ModifierFlags>(allowedModifiers |
		                               ModifierFlagsAsync);
	} else {
		allowedModifiers =
		    static_cast<ModifierFlags>(allowedModifiers |
		                               ModifierFlagsAmbient |
		                               ModifierFlagsReadonly);
	}

	ModifierFlags allowedAndPresent = static_cast<ModifierFlags>(
	    presentModifiers.modifiers & allowedModifiers);
	if ((presentModifiers.modifiers & ~allowedModifiers) != 0) {
		return {nullptr, nullptr};
	}

	if ((modifiers & ModifierFlagsProtected) &&
	    (allowedAndPresent & ModifierFlagsPublic)) {
		modifiers = static_cast<ModifierFlags>(
		    modifiers & ~ModifierFlagsProtected);
	}

	if (allowedAndPresent != ModifierFlagsNone &&
	    !(allowedAndPresent & ModifierFlagsPublic)) {
		modifiers = static_cast<ModifierFlags>(
		    modifiers & ~ModifierFlagsPublic);
	}

	modifiers =
	    static_cast<ModifierFlags>(modifiers | allowedAndPresent);
	const std::string& newLine = FormatOptions().NewLineCharacter;
	snippetPrinter* snippetPrinter = createSnippetPrinter(
	    printer::PrinterOptions{
	        .RemoveComments = true,
	        .NewLine = getNewLineKind(newLine),
	        .Target = GetProgram()->Options()->GetEmitScriptTarget(),
	    },
	    changeTracker->emitContext);

	Node* decoratedNode = nullptr;
	if (!presentModifiers.decorators.empty()) {
		size_t lastNodeIndex = completionNodes.size() - 1;
		if (canHaveDecorators(completionNodes[lastNodeIndex])) {
			decoratedNode = completionNodes[lastNodeIndex];
		}
	}

	std::vector<std::string> texts;
	texts.reserve(completionNodes.size());
	for (Node* node : completionNodes) {
		node = replaceModifiers(
		    changeTracker->nodeFactory, node,
		    createModifierList(
		        changeTracker->nodeFactory, modifiers,
		        node == decoratedNode ? presentModifiers.decorators
		                              : std::vector<Node*>{}));
		std::string text =
		    snippetPrinter->printAndFormatNodeWithSettings(
		        ctx, node, file,
		        change::GetFormatCodeSettingsForWriting(FormatOptions(),
		                                                file));
		texts.push_back(std::move(text));
	}

	std::string insertText;
	for (size_t i = 0; i < texts.size(); i++) {
		if (i != 0) {
			insertText += newLine;
		}
		insertText += texts[i];
	}
	if (insertText.empty()) {
		return {nullptr, nullptr};
	}

	return {new memberCompletionEntry{
	            .insertText = insertText,
	            .filterText = name,
	            .isSnippet = isSnippet,
	            .additionalTextEdits = additionalTextEdits,
	        },
	        nullptr};
}

// completions.go:2766
presentMemberModifiers LanguageService::getPresentMemberModifiers(
    Node* contextToken, SourceFile* file, int position) {
	if (contextToken == nullptr ||
	    getLineOfPosition(file, position) >
	        getLineOfPosition(file, contextToken->end())) {
		return {};
	}

	ModifierFlags modifiers = ModifierFlagsNone;
	std::vector<Node*> decorators;
	int rangePos = position;
	int rangeEnd = position;

	if (isPropertyDeclaration(contextToken->parent)) {
		Kind contextModifierKind = modifierLikeKind(contextToken);
		if (contextModifierKind == Kind::Unknown) {
			return {};
		}

		std::vector<Node*> modifierNodes =
		    contextToken->parent->modifierNodes();
		if (!modifierNodes.empty()) {
			modifiers = static_cast<ModifierFlags>(
			    modifiers |
			    (NodeFactory::modifiersToFlags(modifierNodes) &
			     ModifierFlagsModifier));
			for (Node* modifier : modifierNodes) {
				if (isDecorator(modifier)) {
					decorators.push_back(modifier);
				}
				rangePos = std::min(
				    rangePos,
				    getTokenPosOfNode(modifier, file,
				                      /*includeJSDoc*/ false));
			}
		}

		ModifierFlags contextModifierFlag =
		    modifierToFlag(contextModifierKind);
		if (!(modifiers & contextModifierFlag)) {
			modifiers = static_cast<ModifierFlags>(
			    modifiers | contextModifierFlag);
			rangePos = std::min(
			    rangePos,
			    astnav::getStartOfNode(contextToken, file,
			                           /*includeJSDoc*/ false));
		}

		if (contextToken->parent->name() != contextToken) {
			rangeEnd = astnav::getStartOfNode(
			    contextToken->parent->name(), file,
			    /*includeJSDoc*/ false);
		}
	}

	lsproto::Range* eraseRange = nullptr;
	if (rangePos < rangeEnd) {
		auto [lspRange, fidelity] =
		    createLspRangeFromBounds(rangePos, rangeEnd, file);
		if (fidelity.IsExact()) {
			eraseRange = new lsproto::Range(lspRange);
		}
	}

	return presentMemberModifiers{
	    .modifiers = modifiers,
	    .decorators = decorators,
	    .eraseRange = eraseRange,
	};
}

// completions.go:2819
Kind modifierLikeKind(Node* node) {
	if (node == nullptr) {
		return Kind::Unknown;
	}
	if (isModifier(node)) {
		return node->kind;
	}
	if (isIdentifier(node)) {
		Kind keywordKind =
		    identifierToKeywordKind(node->as<Identifier>());
		if (keywordKind != Kind::Unknown &&
		    isModifierKind(keywordKind)) {
			return keywordKind;
		}
	}
	return Kind::Unknown;
}

// completions.go:2833
ModifierList* createModifierList(NodeFactory* factory, ModifierFlags flags,
                                 const std::vector<Node*>& decorators) {
	std::vector<Node*> nodes;
	for (Node* decorator : decorators) {
		nodes.push_back(deepCloneNode(*factory, decorator));
	}
	auto created = createModifiersFromModifierFlags(
	    flags, [&](Kind kind) { return factory->newToken(kind); });
	nodes.insert(nodes.end(), created.begin(), created.end());
	if (nodes.empty()) {
		return nullptr;
	}
	return factory->newModifierList(nodes);
}

// completions.go:2845
Node* createSnippetTabStopBody(NodeFactory* factory,
                               printer::EmitContext* emitContext) {
	Node* emptyStatement = factory->newEmptyStatement();
	emitContext->setSnippetElement(
	    emptyStatement,
	    printer::SnippetElement{
	        .Kind = printer::SnippetKind::TabStop,
	        .Order = 0,
	    });
	return factory->newBlock(factory->newNodeList({emptyStatement}),
	                         /*multiLine*/ true);
}

// completions.go:2855
std::pair<autoimport::ImportAdder*, gostd::Error>
LanguageService::createImportAdder(const ContextPtr& ctx,
                                   checker::Checker* typeChecker,
                                   SourceFile* file) {
	if (isDynamicFileName(file->FileName())) {
		return {nullptr, nullptr};
	}
	auto [view, err] = getPreparedAutoImportView(file, typeChecker);
	if (err != nullptr) {
		return {nullptr, err};
	}
	if (view == nullptr) {
		return {nullptr, nullptr};
	}
	return {autoimport::NewImportAdder(gostd::contextBackground(), GetProgram(), typeChecker, file,
	                                 view, FormatOptions(), converters,
	                                 UserPreferences())
	            .release(),
	        nullptr};
}

// completions.go:2871
bool isRecommendedCompletionMatch(Symbol* localSymbol,
                                  Symbol* recommendedCompletion,
                                  checker::Checker* typeChecker) {
	return localSymbol == recommendedCompletion ||
	    ((localSymbol->flags & SymbolFlagsExportValue) &&
	     typeChecker->GetExportSymbolOfSymbol(localSymbol) ==
	         recommendedCompletion);
}

// completions.go:2877 — Ported from vscode.
namespace {
const collections::Set<char32_t>& wordSeparators() {
	static const auto* set = new collections::Set<char32_t>(
	    {'`', '~', '!', '@', '%', '^', '&', '*', '(', ')', '-', '=', '+',
	     '[', '{', ']', '}', '\\', '|', ';', ':', '\'', '"', ',', '.', '<',
	     '>', '/', '?'});
	return *set;
}

// utf8.DecodeLastRuneInString.
std::pair<char32_t, int> decodeLastUtf8Rune(std::string_view s) {
	if (s.empty()) {
		return {kRuneError, 0};
	}
	// Scan back at most 4 bytes to a non-continuation lead byte.
	size_t end = s.size();
	size_t start = end - 1;
	size_t limit = end >= 4 ? end - 4 : 0;
	while (start > limit &&
	       (static_cast<unsigned char>(s[start]) & 0xC0) == 0x80) {
		start--;
	}
	int width = 0;
	char32_t r = decodeUtf8RuneStrict(s.substr(start), &width);
	if (start + static_cast<size_t>(width) != end) {
		return {kRuneError, 1};
	}
	return {r, width};
}

// ast/utilities.go:3274 — ast.IsStringTextContainingNode.
bool isStringTextContainingNode(Node* node) {
	return node->kind == Kind::StringLiteral ||
	       isTemplateLiteralKind(node->kind);
}

// unicode.IsDigit.
bool unicodeIsDigit(char32_t r) { return '0' <= r && r <= '9'; }

// unicode.IsSpace.
bool unicodeIsSpace(char32_t r) {
	if (r <= 0xFF) {
		return (r >= '\t' && r <= '\r') || r == ' ' || r == 0x85 ||
		    r == 0xA0;
	}
	// Unicode Zs category.
	switch (r) {
	case 0x1680:
	case 0x2000:
	case 0x2001:
	case 0x2002:
	case 0x2003:
	case 0x2004:
	case 0x2005:
	case 0x2006:
	case 0x2007:
	case 0x2008:
	case 0x2009:
	case 0x200A:
	case 0x202F:
	case 0x205F:
	case 0x3000:
		return true;
	default:
		return false;
	}
}
} // namespace

// completions.go:2883
// Finds the length and first rune of the word that ends at the given
// position.
// e.g. for "abc def.ghi|jkl", the word length is 3 and the word start is 'g'.
std::pair<int, char32_t> getWordLengthAndStart(SourceFile* sourceFile,
                                               int position) {
	// !!! Port other case of vscode's `DEFAULT_WORD_REGEXP` that covers
	// words that start like numbers, e.g. -123.456abcd.
	std::string_view text =
	    std::string_view(sourceFile->Text()).substr(0, position);
	int totalSize = 0;
	char32_t firstRune = 0;
	for (auto [r, size] = decodeLastUtf8Rune(text); size != 0;
	     std::tie(r, size) = decodeLastUtf8Rune(
	         text.substr(0, text.size() - totalSize))) {
		if (wordSeparators().Has(r) || unicodeIsSpace(r)) {
			break;
		}
		totalSize += size;
		firstRune = r;
	}
	// If word starts with `@`, disregard this first character.
	if (firstRune == '@') {
		totalSize -= 1;
		int w = 0;
		firstRune = decodeUtf8RuneStrict(
		    text.substr(text.size() - totalSize), &w);
	}
	return {totalSize, firstRune};
}

// completions.go:2906
// `["ab c"]` -> `ab c`
// `['ab c']` -> `ab c`
// `[123]` -> `123`
std::string trimElementAccess(const std::string& text) {
	std::string t = text;
	if (t.starts_with("[")) {
		t = t.substr(1);
	}
	if (t.ends_with("]")) {
		t.pop_back();
	}
	if (t.size() >= 2 && t.front() == '\'' && t.back() == '\'') {
		t = t.substr(1, t.size() - 2);
	}
	if (t.size() >= 2 && t.front() == '"' && t.back() == '"') {
		t = t.substr(1, t.size() - 2);
	}
	return t;
}

// completions.go:2919 — Ported from vscode ts extension: `getFilterText`.
std::string getFilterText(SourceFile* file, int position,
                          const std::string& insertText,
                          const std::string& label, char32_t wordStart,
                          const std::string& dotAccessor) {
	// Private field completion, e.g. label `#bar`.
	if (label.starts_with("#")) {
		std::string_view after = std::string_view(label).substr(1);
		if (!insertText.empty()) {
			if (insertText.starts_with("this.#")) {
				std::string_view after2 =
				    std::string_view(insertText).substr(6);
				if (wordStart == '#') {
					// `method() { this.#| }`
					// `method() { #| }`
					return "";
				} else {
					// `method() { this.| }`
					// `method() { | }`
					return std::string(after2);
				}
			}
		} else {
			if (wordStart == '#') {
				// `method() { this.#| }`
				return "";
			} else {
				// `method() { this.| }`
				// `method() { | }`
				return std::string(after);
			}
		}
	}

	// For `this.` completions, generally don't set the filter text since
	// we don't want them to be overly deprioritized.
	// microsoft/vscode#74164
	if (insertText.starts_with("this.")) {
		return "";
	}

	// Handle the case:
	// ```
	// const xyz = { 'ab c': 1 };
	// xyz.ab|
	// ```
	// In which case we want to insert a bracket accessor but should use
	// `.abc` as the filter text instead of the bracketed insert text.
	if (insertText.starts_with("[")) {
		return dotAccessor + trimElementAccess(insertText);
	}

	if (insertText.starts_with("?.")) {
		// Handle this case like the case above:
		// ```
		// const xyz = { 'ab c': 1 } | undefined;
		// xyz.ab|
		// ```
		// filterText should be `.ab c` instead of `?.['ab c']`.
		if (insertText.starts_with("?.[")) {
			return dotAccessor + trimElementAccess(insertText.substr(2));
		} else {
			// ```
			// const xyz = { abc: 1 } | undefined;
			// xyz.ab|
			// ```
			// filterText should be `.abc` instead of `?.abc.
			return dotAccessor + insertText.substr(2);
		}
	}

	// In all other cases, fall back to using the insertText.
	return insertText;
}

// completions.go:2993 — Ported from vscode's `provideCompletionItems`.
std::string getDotAccessor(SourceFile* file, int position) {
	std::string_view text =
	    std::string_view(file->Text()).substr(0, position);
	if (text.ends_with("?.")) {
		return std::string(file->Text().substr(position - 2, 2));
	}
	if (text.ends_with(".")) {
		return std::string(file->Text().substr(position - 1, 1));
	}
	return "";
}

// completions.go:3007
bool strPtrIsEmpty(const std::string* ptr) {
	return ptr == nullptr || *ptr == "";
}

// completions.go:3014
std::string* strPtrTo(const std::string& v) {
	if (v.empty()) {
		return nullptr;
	}
	return new std::string(v);
}

// completions.go:3021
bool* boolToPtr(bool v) {
	if (v) {
		return new bool(true);
	}
	return nullptr;
}

// completions.go:3028
int getLineOfPosition(SourceFile* file, int pos) {
	int line = getECMALineOfPosition(file, pos);
	return line;
}

// completions.go:3033
int getLineEndOfPosition(SourceFile* file, int pos) {
	int line = getLineOfPosition(file, pos);
	auto& lineStarts = getECMALineStarts(file);
	int lastCharPos;
	if (line + 1 >= static_cast<int>(lineStarts.size())) {
		lastCharPos = file->end();
	} else {
		lastCharPos = int(lineStarts[line + 1]) - 1;
	}
	const std::string& fullText = file->Text();
	if (lastCharPos > 0 && lastCharPos < int(fullText.size()) &&
	    fullText[lastCharPos] == '\n' &&
	    fullText[lastCharPos - 1] == '\r') {
		return lastCharPos - 1;
	}
	return lastCharPos;
}

// completions.go:3049
bool isClassLikeMemberCompletion(Symbol* symbol, Node* location,
                                 SourceFile* file) {
	if (isInJSFile(location)) {
		return false;
	}
	// Go: ast.SymbolFlagsClassMember & ast.SymbolFlagsEnumMemberExcludes —
	// EnumMemberExcludes is a positive mask (Value|Type), so the AND keeps
	// the class-member bits (they are all Value-namespace flags). A `~`
	// here would zero memberFlags entirely.
	SymbolFlags memberFlags = static_cast<SymbolFlags>(
	    SymbolFlagsClassMember & SymbolFlagsEnumMemberExcludes);
	return (symbol->flags & memberFlags) &&
	    (isClassLike(location) ||
	     (location->parent != nullptr &&
	      location->parent->parent != nullptr &&
	      isClassElement(location->parent) &&
	      location == location->parent->name() &&
	      lsutil::GetLastToken(location->parent, file) ==
	          location->parent->name() &&
	      isClassLike(location->parent->parent)) ||
	     (location->parent != nullptr && isSyntaxList(location) &&
	      isClassLike(location->parent)));
}

// completions.go:3060
bool symbolAppearsToBeTypeOnly(Symbol* symbol,
                               checker::Checker* typeChecker) {
	SymbolFlags flags = checker::SkipAlias(symbol, typeChecker)
	                        ->combinedLocalAndExportSymbolFlags();
	return !(flags & SymbolFlagsValue) &&
	    (symbol->declarations.empty() ||
	     !isInJSFile(symbol->declarations[0]) ||
	     (flags & SymbolFlagsType));
}

// completions.go:3066
bool shouldIncludeSymbol(Symbol* symbol, completionDataData* data,
                         Node* closestSymbolDeclaration, SourceFile* file,
                         checker::Checker* typeChecker,
                         const CompilerOptions* compilerOptions) {
	SymbolFlags allFlags = symbol->flags;
	Node* location = data->location;
	// export = /**/ here we want to get all meanings, so any symbol is ok
	if (location->parent != nullptr &&
	    isExportAssignment(location->parent)) {
		return true;
	}

	// Filter out variables from their own initializers
	// `const a = /* no 'a' here */`
	if (closestSymbolDeclaration != nullptr &&
	    isVariableDeclaration(closestSymbolDeclaration) &&
	    symbol->valueDeclaration == closestSymbolDeclaration) {
		return false;
	}

	// Filter out current and latter parameters from defaults
	// `function f(a = /* no 'a' and 'b' here */, b) { }` or
	// `function f<T = /* no 'T' and 'T2' here */>(a: T, b: T2) { }`
	Node* symbolDeclaration = nullptr;
	if (symbol->valueDeclaration != nullptr) {
		symbolDeclaration = symbol->valueDeclaration;
	} else if (!symbol->declarations.empty()) {
		symbolDeclaration = symbol->declarations[0];
	}

	if (closestSymbolDeclaration != nullptr &&
	    symbolDeclaration != nullptr) {
		if (isParameterDeclaration(closestSymbolDeclaration) &&
		    isParameterDeclaration(symbolDeclaration)) {
			NodeList* parameters =
			    closestSymbolDeclaration->parent->parameterList();
			if (symbolDeclaration->pos() >=
			        closestSymbolDeclaration->pos() &&
			    symbolDeclaration->pos() < parameters->end()) {
				return false;
			}
		} else if (isTypeParameterDeclaration(
		               closestSymbolDeclaration) &&
		           isTypeParameterDeclaration(symbolDeclaration)) {
			if (closestSymbolDeclaration == symbolDeclaration &&
			    data->contextToken != nullptr &&
			    data->contextToken->kind == Kind::ExtendsKeyword) {
				// filter out the directly self-recursive type parameters
				// `type A<K extends /* no 'K' here*/> = K`
				return false;
			}
			if (isInTypeParameterDefault(data->contextToken) &&
			    !isInferTypeNode(closestSymbolDeclaration->parent)) {
				NodeList* typeParameters =
				    closestSymbolDeclaration->parent
				        ->typeParameterList();
				if (typeParameters != nullptr &&
				    symbolDeclaration->pos() >=
				        closestSymbolDeclaration->pos() &&
				    symbolDeclaration->pos() <
				        typeParameters->end()) {
					return false;
				}
			}
		}
	}

	// External modules can have global export declarations that will be
	// available as global keywords in all scopes. But if the external
	// module already has an explicit export and user only wants to use
	// explicit module imports then the global keywords will be filtered
	// out so auto import suggestions will win in the completion.
	Symbol* symbolOrigin = checker::SkipAlias(symbol, typeChecker);
	// We only want to filter out the global keywords.
	// Auto Imports are not available for scripts so this conditional is
	// always false.
	auto sortIt = data->symbolToSortTextMap.find(getSymbolId(symbol));
	if (file->ExternalModuleIndicator != nullptr &&
	    !tristateIsTrue(compilerOptions->AllowUmdGlobalAccess) &&
	    symbol != symbolOrigin &&
	    sortIt != data->symbolToSortTextMap.end() &&
	    sortIt->second == SortTextGlobalsOrKeywords &&
	    symbol->parent != nullptr &&
	    checker::isExternalModuleSymbol(symbol->parent)) {
		return false;
	}

	allFlags = static_cast<SymbolFlags>(
	    allFlags | symbolOrigin->combinedLocalAndExportSymbolFlags());
	if (symbol->flags & SymbolFlagsAlias) {
		allFlags = static_cast<SymbolFlags>(
		    allFlags | typeChecker->GetSymbolFlags(symbol));
	}

	// import m = /**/ <-- It can only access namespace (if typing import =
	// x. this would get member symbols and not namespace)
	if (isInRightSideOfInternalImportEqualsDeclaration(data->location)) {
		return (allFlags & SymbolFlagsNamespace) != 0;
	}

	if (data->isTypeOnlyLocation) {
		// It's a type, but you can reach it by namespace.type as well.
		return symbolCanBeReferencedAtTypeLocation(
		    symbol, typeChecker, collections::Set<SymbolId>{});
	}

	// expressions are value space (which includes the value namespaces)
	return (allFlags & SymbolFlagsValue) != 0;
}

// completions.go:3158
std::pair<std::string, bool> getCompletionEntryDisplayNameForSymbol(
    SourceFile* file, const lsutil::UserPreferences& preferences,
    Symbol* symbol, symbolOriginInfo* origin, CompletionKind completionKind,
    bool isJsxIdentifierExpected) {
	if (originIsIgnore(origin)) {
		return {"", false};
	}

	std::string name;
	if (originIncludesSymbolName(origin)) {
		name = origin->symbolName();
	} else {
		name = tsc::symbolName(symbol);
	}
	if (name.empty() ||
	    // If the symbol is external module, don't show it in the
	    // completion list
	    // (i.e declare module "http" { const x; } | // <= request
	    // completion here, "http" should not be there)
	    ((symbol->flags & SymbolFlagsModule) && startsWithQuote(name)) ||
	    // If the symbol is the internal name of an ES symbol, it is not a
	    // valid entry. Internal names for ES symbols start with "__@"
	    isKnownSymbol(symbol)) {
		return {"", false};
	}

	LanguageVariant variant = ifElse<LanguageVariant>(
	    isJsxIdentifierExpected, LanguageVariant::JSX,
	    LanguageVariant::Standard);
	// name is a valid identifier or private identifier text
	if (isIdentifierText(name, variant) ||
	    (symbol->valueDeclaration != nullptr &&
	     isPrivateIdentifierClassElementDeclaration(
	         symbol->valueDeclaration))) {
		return {name, false};
	}
	if (symbol->flags & SymbolFlagsAlias) {
		// Allow non-identifier import/export aliases since we can insert
		// them as string literals
		return {name, true};
	}

	switch (completionKind) {
	case CompletionKindMemberLike:
		if (originIsComputedPropertyName(origin)) {
			return {origin->symbolName(), false};
		}
		return {"", false};
	case CompletionKindObjectPropertyDeclaration:
		return {quote(file, preferences, name), false};
	case CompletionKindPropertyAccess:
	case CompletionKindGlobal:
		// For a 'this.' completion it will be in a global context, but may
		// have a non-identifier name.
		// Don't add a completion for a name starting with a space. See
		// https://github.com/Microsoft/TypeScript/pull/20547
		if (!name.empty() && name[0] == ' ') {
			return {"", false};
		}
		return {name, true};
	case CompletionKindNone:
	case CompletionKindString:
		return {name, false};
	default:
		TSC_UNREACHABLE("Unexpected completion kind in "
		                "getCompletionEntryDisplayNameForSymbol");
	}
}

// !!! refactor symbolOriginInfo so that we can tell the difference between
// flags and the kind of data it has
// completions.go:3220
bool originIsIgnore(symbolOriginInfo* origin) {
	return origin != nullptr &&
	    (origin->kind & symbolOriginInfoKindIgnore);
}

// completions.go:3224
bool originIncludesSymbolName(symbolOriginInfo* origin) {
	return originIsComputedPropertyName(origin);
}

// completions.go:3228
bool originIsComputedPropertyName(symbolOriginInfo* origin) {
	return origin != nullptr &&
	    (origin->kind & symbolOriginInfoKindComputedPropertyName);
}

// completions.go:3232
bool originIsObjectLiteralMethod(symbolOriginInfo* origin) {
	return origin != nullptr &&
	    (origin->kind & symbolOriginInfoKindObjectLiteralMethod);
}

// completions.go:3236
bool originIsThisTypeNode(symbolOriginInfo* origin) {
	return origin != nullptr &&
	    (origin->kind & symbolOriginInfoKindThisType);
}

// completions.go:3240
bool originIsTypeOnlyAlias(symbolOriginInfo* origin) {
	return origin != nullptr &&
	    (origin->kind & symbolOriginInfoKindTypeOnlyAlias);
}

// completions.go:3244
bool originIsSymbolMember(symbolOriginInfo* origin) {
	return origin != nullptr &&
	    (origin->kind & symbolOriginInfoKindSymbolMember);
}

// completions.go:3248
bool originIsNullableMember(symbolOriginInfo* origin) {
	return origin != nullptr &&
	    (origin->kind & symbolOriginInfoKindNullable);
}

// completions.go:3252
bool originIsPromise(symbolOriginInfo* origin) {
	return origin != nullptr &&
	    (origin->kind & symbolOriginInfoKindPromise);
}

// completions.go:3256
std::string getSourceFromOrigin(symbolOriginInfo* origin) {
	if (originIsThisTypeNode(origin)) {
		return completionSourceThisProperty;
	}

	if (originIsTypeOnlyAlias(origin)) {
		return completionSourceTypeOnlyAlias;
	}

	return "";
}

// completions.go:3284
// In a scenarion such as `const x = 1 * |`, the context and previous tokens
// are both `*`. In `const x = 1 * o|`, the context token is *, and the
// previous token is `o`. `contextToken` and `previousToken` can both be nil
// if we are at the beginning of the file.
std::pair<Node*, Node*> getRelevantTokens(int position, SourceFile* file) {
	Node* previousToken = astnav::findPrecedingToken(file, position);
	if (previousToken != nullptr && position <= previousToken->end() &&
	    (isMemberName(previousToken) ||
	     isKeywordKind(previousToken->kind))) {
		Node* contextToken =
		    astnav::findPrecedingToken(file, previousToken->pos());
		return {contextToken, previousToken};
	}
	return {previousToken, previousToken};
}

// completions.go:3298
bool isValidTrigger(SourceFile* file,
                    const CompletionsTriggerCharacter& triggerCharacter,
                    Node* contextToken, int position) {
	if (triggerCharacter == "." || triggerCharacter == "@") {
		return true;
	}
	if (triggerCharacter == "\"" || triggerCharacter == "'" ||
	    triggerCharacter == "`") {
		// Only automatically bring up completions if this is an opening
		// quote.
		return contextToken != nullptr &&
		    isStringLiteralOrTemplate(contextToken) &&
		    position == astnav::getStartOfNode(contextToken, file,
		                                       /*includeJSDoc*/ false) +
		                1;
	}
	if (triggerCharacter == "#") {
		return contextToken != nullptr &&
		    isPrivateIdentifier(contextToken) &&
		    getContainingClass(contextToken) != nullptr;
	}
	if (triggerCharacter == "<") {
		// Opening JSX tag
		return contextToken != nullptr &&
		    contextToken->kind == Kind::LessThanToken &&
		    (!isBinaryExpression(contextToken->parent) ||
		     binaryExpressionMayBeOpenTag(
		         contextToken->parent->as<BinaryExpression>()));
	}
	if (triggerCharacter == "/") {
		if (contextToken == nullptr) {
			return false;
		}
		if (isStringLiteralLike(contextToken)) {
			return tryGetImportFromModuleSpecifier(contextToken) !=
			    nullptr;
		}
		return contextToken->kind == Kind::LessThanSlashToken &&
		    isJsxClosingElement(contextToken->parent);
	}
	if (triggerCharacter == " ") {
		return contextToken != nullptr &&
		    contextToken->kind == Kind::ImportKeyword &&
		    contextToken->parent->kind == Kind::SourceFile;
	}
	if (triggerCharacter == "*") {
		return isPotentiallyValidJSDocSnippetCompletionPosition(file,
		                                                      position);
	}
	TSC_UNREACHABLE("Unknown trigger character in isValidTrigger");
}

// completions.go:3323
bool isStringLiteralOrTemplate(Node* node) {
	switch (node->kind) {
	case Kind::StringLiteral:
	case Kind::NoSubstitutionTemplateLiteral:
	case Kind::TemplateExpression:
	case Kind::TaggedTemplateExpression:
		return true;
	default:
		return false;
	}
}

// completions.go:3333
bool binaryExpressionMayBeOpenTag(BinaryExpression* binaryExpression) {
	return nodeIsMissing(binaryExpression->Left);
}

// completions.go:3337
bool isCheckedFile(SourceFile* file,
                   const CompilerOptions* compilerOptions) {
	return !isSourceFileJS(file) ||
	    isCheckJSEnabledForFile(file, compilerOptions);
}

// completions.go:3341
bool isContextTokenValueLocation(Node* contextToken) {
	return contextToken != nullptr &&
	    ((contextToken->kind == Kind::TypeOfKeyword &&
	      (contextToken->parent->kind == Kind::TypeQuery ||
	       isTypeOfExpression(contextToken->parent))) ||
	     (contextToken->kind == Kind::AssertsKeyword &&
	      contextToken->parent->kind == Kind::TypePredicate));
}

// completions.go:3347
bool isPossiblyTypeArgumentPosition(Node* token, SourceFile* sourceFile,
                                    checker::Checker* typeChecker) {
	PossibleTypeArgumentInfo* info =
	    getPossibleTypeArgumentsInfo(token, sourceFile);
	return info != nullptr &&
	    (isPartOfTypeNode(info->called) ||
	     !getPossibleGenericSignatures(info->called, info->nTypeArguments,
	                                   typeChecker)
	          .empty() ||
	     isPossiblyTypeArgumentPosition(info->called, sourceFile,
	                                    typeChecker));
}

// completions.go:3353
bool isContextTokenTypeLocation(Node* contextToken) {
	if (contextToken != nullptr) {
		Kind parentKind = contextToken->parent->kind;
		switch (contextToken->kind) {
		case Kind::ColonToken:
			return parentKind == Kind::PropertyDeclaration ||
			    parentKind == Kind::PropertySignature ||
			    parentKind == Kind::Parameter ||
			    parentKind == Kind::VariableDeclaration ||
			    isFunctionLikeKind(parentKind);
		case Kind::EqualsToken:
			return parentKind == Kind::TypeAliasDeclaration ||
			    parentKind == Kind::TypeParameter;
		case Kind::AsKeyword:
			return parentKind == Kind::AsExpression;
		case Kind::LessThanToken:
			return parentKind == Kind::TypeReference ||
			    parentKind == Kind::TypeAssertionExpression;
		case Kind::ExtendsKeyword:
			return parentKind == Kind::TypeParameter;
		case Kind::SatisfiesKeyword:
			return parentKind == Kind::SatisfiesExpression;
		case Kind::OpenBracketToken:
		case Kind::CommaToken:
			return parentKind == Kind::TupleType;
		default:
			break;
		}
	}
	return false;
}

// completions.go:3380
// True if symbol is a type or a module containing at least one type.
bool symbolCanBeReferencedAtTypeLocation(
    Symbol* symbol, checker::Checker* typeChecker,
    collections::Set<SymbolId> seenModules) {
	// Since an alias can be merged with a local declaration, we need to
	// test both the alias and its target. This code used to just test the
	// result of `skipAlias`, but that would ignore any locally introduced
	// meanings.
	return nonAliasCanBeReferencedAtTypeLocation(symbol, typeChecker,
	                                           seenModules) ||
	    nonAliasCanBeReferencedAtTypeLocation(
	        checker::SkipAlias(
	            ifElse<Symbol*>(symbol->exportSymbol != nullptr,
	                            symbol->exportSymbol, symbol),
	            typeChecker),
	        typeChecker, seenModules);
}

// completions.go:3390
bool nonAliasCanBeReferencedAtTypeLocation(
    Symbol* symbol, checker::Checker* typeChecker,
    collections::Set<SymbolId> seenModules) {
	return (symbol->flags & SymbolFlagsType) ||
	    typeChecker->IsUnknownSymbol(symbol) ||
	    ((symbol->flags & SymbolFlagsModule) &&
	     seenModules.AddIfAbsent(getSymbolId(symbol)) &&
	     someList(
	         typeChecker->GetExportsOfModule(symbol),
	         [&](Symbol* e) {
		         return symbolCanBeReferencedAtTypeLocation(
		             e, typeChecker, seenModules);
	         }));
}

// completions.go:3402
// Gets all properties on a type, but if that type is a union of several
// types, excludes array-like types or callable/constructable types.
std::vector<Symbol*> getPropertiesForCompletion(
    checker::Type* t, checker::Checker* typeChecker) {
	if (t->IsUnion()) {
		return checkEachDefined(
		    typeChecker->GetAllPossiblePropertiesOfTypes(t->types()),
		    "getAllPossiblePropertiesOfTypes() should all be defined.");
	} else {
		return checkEachDefined(
		    typeChecker->GetApparentProperties(t),
		    "getApparentProperties() should all be defined.");
	}
}

// completions.go:3410
// Given 'a.b.c', returns 'a'.
Identifier* getLeftMostName(Node* e) {
	if (isIdentifier(e)) {
		return e->as<Identifier>();
	} else if (isPropertyAccessExpression(e)) {
		return getLeftMostName(e->expression());
	} else {
		return nullptr;
	}
}

// completions.go:3416
Symbol* getFirstSymbolInChain(Symbol* symbol, Node* enclosingDeclaration,
                              checker::Checker* typeChecker) {
	auto chain = typeChecker->GetAccessibleSymbolChain(
	    symbol, enclosingDeclaration, SymbolFlagsAll, /*meaning*/
	    /*useOnlyExternalAliasing*/ false);
	if (!chain.empty()) {
		return chain[0];
	}
	if (symbol->parent != nullptr) {
		if (isModuleSymbol(symbol->parent)) {
			return symbol;
		}
		return getFirstSymbolInChain(symbol->parent, enclosingDeclaration,
		                             typeChecker);
	}
	return nullptr;
}

// completions.go:3436
bool isModuleSymbol(Symbol* symbol) {
	return someList(symbol->declarations, [](Node* decl) {
		return decl->kind == Kind::SourceFile;
	});
}

// completions.go:3441
symbolOriginInfoKind getNullableSymbolOriginInfoKind(
    symbolOriginInfoKind kind, bool insertQuestionDot) {
	if (insertQuestionDot) {
		kind = kind | symbolOriginInfoKindNullable;
	}
	return kind;
}

// completions.go:3447
bool isStaticProperty(Symbol* symbol) {
	return symbol->valueDeclaration != nullptr &&
	    (symbol->valueDeclaration->modifierFlags() &
	     ModifierFlagsStatic) &&
	    isClassLike(symbol->valueDeclaration->parent);
}

// completions.go:3455 — getContextualTypeForConditionalExpression handles
// completion within a conditional expression (ternary operator) by using
// the parent expression to find the contextual type.
checker::Type* getContextualTypeForConditionalExpression(
    Node* conditionalExpr, int position, SourceFile* file,
    checker::Checker* typeChecker) {
	argumentInfoForCompletions* argInfo = getArgumentInfoForCompletions(
	    conditionalExpr, position, file, typeChecker);
	if (argInfo != nullptr) {
		return typeChecker->GetContextualTypeForArgumentAtIndex(
		    argInfo->invocation, argInfo->argumentIndex);
	}
	// Fall through to regular contextual type logic if not in an argument
	checker::Type* contextualType = typeChecker->GetContextualType(
	    conditionalExpr, checker::ContextFlagsIgnoreNodeInferences);
	if (contextualType != nullptr) {
		return contextualType;
	}
	return typeChecker->GetContextualType(conditionalExpr,
	                                      checker::ContextFlagsNone);
}

// completions.go:3470
checker::Type* getContextualType(Node* previousToken, int position,
                                 SourceFile* file,
                                 checker::Checker* typeChecker) {
	Node* parent = previousToken->parent;
	switch (previousToken->kind) {
	case Kind::Identifier:
		return getContextualTypeFromParent(previousToken, typeChecker,
		                                   checker::ContextFlagsNone);
	case Kind::EqualsToken:
		switch (parent->kind) {
		case Kind::VariableDeclaration:
			return typeChecker->GetContextualType(parent->initializer(),
			                                      checker::ContextFlagsNone);
		case Kind::BinaryExpression:
			return typeChecker->GetTypeAtLocation(
			    parent->as<BinaryExpression>()->Left);
		case Kind::JsxAttribute:
			return typeChecker->GetContextualTypeForJsxAttribute(parent);
		default:
			return nullptr;
		}
	case Kind::NewKeyword:
		return typeChecker->GetContextualType(parent,
		                                      checker::ContextFlagsNone);
	case Kind::CaseKeyword: {
		Node* caseClause =
		    ifElse<Node*>(isCaseClause(parent), parent, nullptr);
		if (caseClause != nullptr) {
			return getSwitchedType(caseClause, typeChecker);
		}
		return nullptr;
	}
	case Kind::OpenBraceToken:
		if (isJsxExpression(parent) && !isJsxElement(parent->parent) &&
		    !isJsxFragment(parent->parent)) {
			return typeChecker->GetContextualTypeForJsxAttribute(
			    parent->parent);
		}
		return nullptr;
	case Kind::OpenBracketToken:
		// When completing after `[` in an array literal (e.g.,
		// `[/*here*/]`), we should provide contextual type for the first
		// element
		if (isArrayLiteralExpression(parent)) {
			checker::Type* contextualArrayType =
			    typeChecker->GetContextualType(parent,
			                                   checker::ContextFlagsNone);
			if (contextualArrayType != nullptr) {
				// Get the type for the first element (index 0)
				return typeChecker
				    ->GetContextualTypeForArrayLiteralAtPosition(
				        contextualArrayType, parent, position);
			}
		}
		return nullptr;
	case Kind::CloseBracketToken:
		// When completing after `]` (e.g., `[x]/*here*/`), we should not
		// provide a contextual type for the closing bracket token itself.
		// Without this case, CloseBracketToken would fall through to the
		// default case, and if the parent is an array literal,
		// GetContextualType would try to find the token's index in the
		// array elements (returning -1), leading to an out-of-bounds panic
		// in getContextualTypeForElementExpression.
		return nullptr;
	case Kind::QuestionToken:
		// When completing after `?` in a ternary conditional (e.g.,
		// `foo(a ? /*here*/)`), we need to look at the parent conditional
		// expression to find the contextual type.
		if (isConditionalExpression(parent)) {
			return getContextualTypeForConditionalExpression(
			    parent, position, file, typeChecker);
		}
		return nullptr;
	case Kind::ColonToken:
		// When completing after `:` in a ternary conditional (e.g.,
		// `foo(a ? b : /*here*/)`), we need to look at the parent
		// conditional expression to find the contextual type.
		// Only handle this if parent is ConditionalExpression, otherwise
		// fall through to default (colons are used in other contexts like
		// object literals, type annotations, etc.)
		if (isConditionalExpression(parent)) {
			return getContextualTypeForConditionalExpression(
			    parent, position, file, typeChecker);
		}
		[[fallthrough]];
	case Kind::CommaToken:
		// When completing after `,` in an array literal (e.g.,
		// `[x, /*here*/]`), we should provide contextual type for the
		// element after the comma.
		if (isArrayLiteralExpression(parent)) {
			checker::Type* contextualArrayType =
			    typeChecker->GetContextualType(parent,
			                                   checker::ContextFlagsNone);
			if (contextualArrayType != nullptr) {
				return typeChecker
				    ->GetContextualTypeForArrayLiteralAtPosition(
				        contextualArrayType, parent, position);
			}
			return nullptr;
		}
		break;
	default:
		break;
	}
	// Default case: see if we're in an argument position.
	argumentInfoForCompletions* argInfo = getArgumentInfoForCompletions(
	    previousToken, position, file, typeChecker);
	if (argInfo != nullptr) {
		return typeChecker->GetContextualTypeForArgumentAtIndex(
		    argInfo->invocation, argInfo->argumentIndex);
	} else if (isEqualityOperatorKind(previousToken->kind) &&
	           isBinaryExpression(parent) &&
	           isEqualityOperatorKind(
	               parent->as<BinaryExpression>()->OperatorToken->kind)) {
		// completion at `x ===/**/`
		return typeChecker->GetTypeAtLocation(
		    parent->as<BinaryExpression>()->Left);
	} else {
		checker::Type* contextualType = typeChecker->GetContextualType(
		    previousToken, checker::ContextFlagsIgnoreNodeInferences);
		if (contextualType != nullptr) {
			return contextualType;
		}
		return typeChecker->GetContextualType(previousToken,
		                                      checker::ContextFlagsNone);
	}
}

// completions.go:3556
checker::Type* getSwitchedType(Node* caseClause,
                               checker::Checker* typeChecker) {
	return typeChecker->GetTypeAtLocation(
	    caseClause->parent->parent->expression());
}

// completions.go:3560
bool isEqualityOperatorKind(Kind kind) {
	switch (kind) {
	case Kind::EqualsEqualsEqualsToken:
	case Kind::EqualsEqualsToken:
	case Kind::ExclamationEqualsEqualsToken:
	case Kind::ExclamationEqualsToken:
		return true;
	default:
		return false;
	}
}

// completions.go:3568
// We disregard boolean literals for completion purposes.
bool isLiteral(checker::Type* t) {
	return t->IsStringLiteral() || t->IsNumberLiteral() ||
	    t->IsBigIntLiteral();
}

// completions.go:3572
Symbol* getRecommendedCompletion(Node* previousToken,
                                 checker::Type* contextualType,
                                 checker::Checker* typeChecker) {
	std::vector<checker::Type*> types;
	if (contextualType->IsUnion()) {
		types = contextualType->types();
	} else {
		types = {contextualType};
	}
	// For a union, return the first one with a recommended completion.
	return firstNonNil(types, [&](checker::Type* t) -> Symbol* {
		Symbol* symbol = t->symbol;
		// Don't make a recommended completion for an abstract class.
		if (symbol != nullptr &&
		    (symbol->flags & (SymbolFlagsEnumMember | SymbolFlagsEnum |
		                      SymbolFlagsClass)) &&
		    !isAbstractConstructorSymbol(symbol)) {
			return getFirstSymbolInChain(symbol, previousToken,
			                             typeChecker);
		}
		return nullptr;
	});
}

// completions.go:3593
bool isAbstractConstructorSymbol(Symbol* symbol) {
	if (symbol->flags & SymbolFlagsClass) {
		Node* declaration = getClassLikeDeclarationOfSymbol(symbol);
		return declaration != nullptr &&
		    hasSyntacticModifier(declaration, ModifierFlagsAbstract);
	}
	return false;
}

// completions.go:3601
bool startsWithQuote(const std::string& s) {
	int w = 0;
	char32_t r = decodeUtf8RuneStrict(s, &w);
	return r == '"' || r == '\'';
}

// completions.go:3606
Node* getClosestSymbolDeclaration(Node* contextToken, Node* location) {
	if (contextToken == nullptr) {
		return nullptr;
	}

	Node* closestDeclaration = findAncestorOrQuit(
	    contextToken, [](Node* node) -> FindAncestorResult {
		    if (isFunctionBlock(node) || isArrowFunctionBody(node) ||
		        isBindingPattern(node)) {
			    return FindAncestorResult::Quit;
		    }

		    if ((isParameterDeclaration(node) ||
		         isTypeParameterDeclaration(node)) &&
		        !isIndexSignatureDeclaration(node->parent)) {
			    return FindAncestorResult::True;
		    }
		    return FindAncestorResult::False;
	    });

	if (closestDeclaration == nullptr) {
		closestDeclaration = findAncestorOrQuit(
		    location, [](Node* node) -> FindAncestorResult {
			    if (isFunctionBlock(node) || isArrowFunctionBody(node) ||
			        isBindingPattern(node)) {
				    return FindAncestorResult::Quit;
			    }

			    if (isVariableDeclaration(node)) {
				    return FindAncestorResult::True;
			    }
			    return FindAncestorResult::False;
		    });
	}
	return closestDeclaration;
}

// completions.go:3634
bool isArrowFunctionBody(Node* node) {
	return node->parent != nullptr && isArrowFunction(node->parent) &&
	    (node->parent->body() == node ||
	     // const a = () => /**/;
	     node->kind == Kind::EqualsGreaterThanToken);
}

// completions.go:3641
bool isInTypeParameterDefault(Node* contextToken) {
	if (contextToken == nullptr) {
		return false;
	}

	Node* node = contextToken;
	Node* parent = contextToken->parent;
	while (parent != nullptr) {
		if (isTypeParameterDeclaration(parent)) {
			return parent->as<TypeParameterDeclaration>()
			           ->DefaultType == node ||
			    node->kind == Kind::EqualsToken;
		}
		node = parent;
		parent = parent->parent;
	}

	return false;
}

// completions.go:3659
bool isDeprecated(Symbol* symbol, checker::Checker* typeChecker) {
	auto declarations =
	    checker::SkipAlias(symbol, typeChecker)->declarations;
	return !declarations.empty() &&
	    everyList(declarations, [&](Node* decl) {
		       return typeChecker->IsDeprecatedDeclaration(decl);
	       });
}

// completions.go:3664
lsproto::Range* LanguageService::getReplacementRangeForContextToken(
    SourceFile* file, Node* contextToken, int position) {
	if (contextToken == nullptr) {
		return nullptr;
	}

	// !!! ensure range is single line
	switch (contextToken->kind) {
	case Kind::StringLiteral:
	case Kind::NoSubstitutionTemplateLiteral:
		return createRangeFromStringLiteralLikeContent(
		    file, contextToken, position);
	default: {
		auto [lspRange, fidelity] =
		    createLspRangeFromNode(contextToken, file);
		if (!fidelity.IsExact()) {
			return nullptr;
		}
		return new lsproto::Range(lspRange);
	}
	}
}

// completions.go:3681
lsproto::Range* LanguageService::createRangeFromStringLiteralLikeContent(
    SourceFile* file, Node* node, int position) {
	int replacementEnd = node->end() - 1;
	int nodeStart =
	    astnav::getStartOfNode(node, file, /*includeJSDoc*/ false);
	if (isUnterminatedLiteral(node)) {
		// we return no replacement range only if unterminated string is
		// empty
		if (nodeStart == replacementEnd) {
			return nullptr;
		}
		replacementEnd = std::min(position, node->end());
	}
	auto [lspRange, fidelity] =
	    createLspRangeFromBounds(nodeStart + 1, replacementEnd, file);
	if (!fidelity.IsExact()) {
		return nullptr;
	}
	return new lsproto::Range(lspRange);
}

// completions.go:3710
std::string quotePropertyName(
    SourceFile* file, const lsutil::UserPreferences& preferences,
    const std::string& name) {
	int w = 0;
	char32_t r = decodeUtf8RuneStrict(name, &w);
	if (unicodeIsDigit(r)) {
		return name;
	}
	return quote(file, preferences, name);
}

// completions.go:3719
// Checks whether type is `string & {}`, which is semantically equivalent
// to string but is not reduced by the checker as a special case used for
// supporting string literal completions for string type.
bool isStringAndEmptyAnonymousObjectIntersection(
    checker::Checker* typeChecker, checker::Type* t) {
	if (!t->IsIntersection()) {
		return false;
	}

	return t->types().size() == 2 &&
	    (areIntersectedTypesAvoidingStringReduction(
	         typeChecker, t->types()[0], t->types()[1]) ||
	     areIntersectedTypesAvoidingStringReduction(
	         typeChecker, t->types()[1], t->types()[0]));
}

// completions.go:3730
bool areIntersectedTypesAvoidingStringReduction(
    checker::Checker* typeChecker, checker::Type* t1, checker::Type* t2) {
	return t1->IsString() && typeChecker->IsEmptyAnonymousObjectType(t2);
}

// completions.go:3734
std::string escapeSnippetText(const std::string& text) {
	std::string out = text;
	// strings.ReplaceAll(text, `$`, `\$`)
	size_t pos = 0;
	while ((pos = out.find('$', pos)) != std::string::npos) {
		out.replace(pos, 1, "\\$");
		pos += 2;
	}
	return out;
}

// completions.go:3738
bool isNamedImportsOrExports(Node* node) {
	return isNamedImports(node) || isNamedExports(node);
}

// completions.go:3742
std::string generateIdentifierForArbitraryString(const std::string& text) {
	bool needsUnderscore = false;
	std::string identifier;

	// Convert "(example, text)" into "_example_text_"
	for (size_t pos = 0; pos < text.size();) {
		int size = 0;
		char32_t ch =
		    decodeUtf8RuneStrict(
		        std::string_view(text).substr(pos), &size);
		bool validChar;
		if (pos == 0) {
			validChar = isIdentifierStart(ch);
		} else {
			validChar = isIdentifierPart(ch);
		}
		if (size > 0 && validChar) {
			if (needsUnderscore) {
				identifier += '_';
			}
			identifier += utf8String(ch);
			needsUnderscore = false;
		} else {
			needsUnderscore = true;
		}
		pos += size;
	}

	if (needsUnderscore) {
		identifier += '_';
	}

	// Default to "_" if the provided text was empty
	if (identifier.empty()) {
		return "_";
	}

	return identifier;
}

// completions.go:3781 — Copied from vscode TS extension.
lsproto::CompletionItemKind getCompletionsSymbolKind(
    lsutil::ScriptElementKind kind) {
	switch (kind) {
	case lsutil::ScriptElementKindPrimitiveType:
	case lsutil::ScriptElementKindKeyword:
		return lsproto::CompletionItemKindKeyword;
	case lsutil::ScriptElementKindConstElement:
	case lsutil::ScriptElementKindLetElement:
	case lsutil::ScriptElementKindVariableElement:
	case lsutil::ScriptElementKindLocalVariableElement:
	case lsutil::ScriptElementKindAlias:
	case lsutil::ScriptElementKindParameterElement:
		return lsproto::CompletionItemKindVariable;
	case lsutil::ScriptElementKindMemberVariableElement:
	case lsutil::ScriptElementKindMemberGetAccessorElement:
	case lsutil::ScriptElementKindMemberSetAccessorElement:
		return lsproto::CompletionItemKindField;
	case lsutil::ScriptElementKindFunctionElement:
	case lsutil::ScriptElementKindLocalFunctionElement:
		return lsproto::CompletionItemKindFunction;
	case lsutil::ScriptElementKindMemberFunctionElement:
	case lsutil::ScriptElementKindConstructSignatureElement:
	case lsutil::ScriptElementKindCallSignatureElement:
	case lsutil::ScriptElementKindIndexSignatureElement:
		return lsproto::CompletionItemKindMethod;
	case lsutil::ScriptElementKindEnumElement:
		return lsproto::CompletionItemKindEnum;
	case lsutil::ScriptElementKindEnumMemberElement:
		return lsproto::CompletionItemKindEnumMember;
	case lsutil::ScriptElementKindModuleElement:
	case lsutil::ScriptElementKindExternalModuleName:
		return lsproto::CompletionItemKindModule;
	case lsutil::ScriptElementKindClassElement:
	case lsutil::ScriptElementKindTypeElement:
		return lsproto::CompletionItemKindClass;
	case lsutil::ScriptElementKindInterfaceElement:
		return lsproto::CompletionItemKindInterface;
	case lsutil::ScriptElementKindWarning:
		return lsproto::CompletionItemKindText;
	case lsutil::ScriptElementKindScriptElement:
		return lsproto::CompletionItemKindFile;
	case lsutil::ScriptElementKindDirectory:
		return lsproto::CompletionItemKindFolder;
	case lsutil::ScriptElementKindString:
		return lsproto::CompletionItemKindConstant;
	default:
		return lsproto::CompletionItemKindProperty;
	}
}

// completions.go:3832 — Editors will use the `sortText` and then fall back
// to `name` for sorting, but leave ties in response order. So, it's
// important that we sort those ties in the order we want them displayed if
// it matters. We don't strictly need to sort by name or SortText here
// since clients are going to do it anyway, but we have to do the work of
// comparing them so we can sort those ties appropriately.
int CompareCompletionEntries(lsproto::CompletionItem* a,
                             lsproto::CompletionItem* b) {
	auto compareStrings =
	    stringutil::CompareStringsCaseInsensitiveThenSensitive;
	int result = compareStrings(*a->SortText, *b->SortText);
	if (result == stringutil::ComparisonEqual) {
		result = compareStrings(a->Label, b->Label);
	}
	return result;
}

namespace {
// completions.go:3839
collections::SyncMap<KeywordCompletionFilters,
                     std::vector<lsproto::CompletionItem*>>
    keywordCompletionsCache;

const std::vector<lsproto::CompletionItem*>& allKeywordCompletions() {
	static const std::vector<lsproto::CompletionItem*>* result =
	    []() -> std::vector<lsproto::CompletionItem*>* {
		    auto* r = new std::vector<lsproto::CompletionItem*>();
		    r->reserve((int)KindLastKeyword - (int)KindFirstKeyword + 1);
		    for (int i = (int)KindFirstKeyword; i <= (int)KindLastKeyword;
		         i++) {
			    r->push_back(new lsproto::CompletionItem{
			        .Label = std::string(tokenToString(static_cast<Kind>(i))),
			        .Kind = std::make_shared<lsproto::CompletionItemKind>(
			            lsproto::CompletionItemKindKeyword),
			        .SortText = std::string(SortTextGlobalsOrKeywords),
			    });
		    }
		    return r;
	    }();
	return *result;
}
} // namespace

// completions.go:3852
std::vector<CompletionItem*> cloneItems(
    const std::vector<lsproto::CompletionItem*>& items) {
	if (items.empty()) {
		return {};
	}
	std::vector<CompletionItem*> entries;
	entries.reserve(items.size());
	for (lsproto::CompletionItem* item : items) {
		auto* itemClone = new lsproto::CompletionItem(*item);
		entries.push_back(new CompletionItem{.completionItem = itemClone});
	}
	return entries;
}

// completions.go:3862
std::vector<CompletionItem*> getKeywordCompletions(
    KeywordCompletionFilters keywordFilter, bool filterOutTsOnlyKeywords) {
	if (!filterOutTsOnlyKeywords) {
		return cloneItems(
		    getTypescriptKeywordCompletions(keywordFilter));
	}

	KeywordCompletionFilters index =
	    keywordFilter + KeywordCompletionFiltersLast + 1;
	if (auto [cached, ok] = keywordCompletionsCache.Load(index); ok) {
		return cloneItems(cached);
	}
	std::vector<lsproto::CompletionItem*> result = filterList(
	    getTypescriptKeywordCompletions(keywordFilter),
	    [](lsproto::CompletionItem* ci) {
		    return !isTypeScriptOnlyKeyword(stringToToken(ci->Label));
	    });
	keywordCompletionsCache.Store(index, result);
	return cloneItems(result);
}

// completions.go:3882
std::vector<lsproto::CompletionItem*> getTypescriptKeywordCompletions(
    KeywordCompletionFilters keywordFilter) {
	if (auto [cached, ok] = keywordCompletionsCache.Load(keywordFilter);
	    ok) {
		return cached;
	}
	std::vector<lsproto::CompletionItem*> result = filterList(
	    allKeywordCompletions(), [&](lsproto::CompletionItem* entry) {
		    Kind kind = stringToToken(entry->Label);
		    switch (keywordFilter) {
		    case KeywordCompletionFiltersNone:
			    return false;
		    case KeywordCompletionFiltersAll:
			    return isFunctionLikeBodyKeyword(kind) ||
			        kind == Kind::DeclareKeyword ||
			        kind == Kind::ModuleKeyword ||
			        kind == Kind::TypeKeyword ||
			        kind == Kind::NamespaceKeyword ||
			        kind == Kind::AbstractKeyword ||
			        (isTypeKeyword(kind) &&
			         kind != Kind::UndefinedKeyword);
		    case KeywordCompletionFiltersFunctionLikeBodyKeywords:
			    return isFunctionLikeBodyKeyword(kind);
		    case KeywordCompletionFiltersClassElementKeywords:
			    return isClassMemberCompletionKeyword(kind);
		    case KeywordCompletionFiltersInterfaceElementKeywords:
			    return isInterfaceOrTypeLiteralCompletionKeyword(kind);
		    case KeywordCompletionFiltersConstructorParameterKeywords:
			    return isParameterPropertyModifier(kind);
		    case KeywordCompletionFiltersTypeAssertionKeywords:
			    return isTypeKeyword(kind) || kind == Kind::ConstKeyword;
		    case KeywordCompletionFiltersTypeKeywords:
			    return isTypeKeyword(kind);
		    case KeywordCompletionFiltersTypeKeyword:
			    return kind == Kind::TypeKeyword;
		    default:
			    TSC_UNREACHABLE("Unknown keyword filter in "
			                    "getTypescriptKeywordCompletions");
		    }
	    });

	keywordCompletionsCache.Store(keywordFilter, result);
	return result;
}

// completions.go:3930
bool isTypeScriptOnlyKeyword(Kind kind) {
	switch (kind) {
	case Kind::AbstractKeyword:
	case Kind::AnyKeyword:
	case Kind::BigIntKeyword:
	case Kind::BooleanKeyword:
	case Kind::DeclareKeyword:
	case Kind::EnumKeyword:
	case Kind::GlobalKeyword:
	case Kind::ImplementsKeyword:
	case Kind::InferKeyword:
	case Kind::InterfaceKeyword:
	case Kind::IsKeyword:
	case Kind::KeyOfKeyword:
	case Kind::ModuleKeyword:
	case Kind::NamespaceKeyword:
	case Kind::NeverKeyword:
	case Kind::NumberKeyword:
	case Kind::ObjectKeyword:
	case Kind::OverrideKeyword:
	case Kind::PrivateKeyword:
	case Kind::ProtectedKeyword:
	case Kind::PublicKeyword:
	case Kind::ReadonlyKeyword:
	case Kind::StringKeyword:
	case Kind::SymbolKeyword:
	case Kind::TypeKeyword:
	case Kind::UniqueKeyword:
	case Kind::UnknownKeyword:
		return true;
	default:
		return false;
	}
}

// completions.go:3964
bool isFunctionLikeBodyKeyword(Kind kind) {
	return kind == Kind::AsyncKeyword || kind == Kind::AwaitKeyword ||
	    kind == Kind::UsingKeyword || kind == Kind::AsKeyword ||
	    kind == Kind::SatisfiesKeyword || kind == Kind::TypeKeyword ||
	    (!isContextualKeyword(kind) &&
	     !isClassMemberCompletionKeyword(kind));
}

// completions.go:3973
bool isClassMemberCompletionKeyword(Kind kind) {
	switch (kind) {
	case Kind::AbstractKeyword:
	case Kind::AccessorKeyword:
	case Kind::ConstructorKeyword:
	case Kind::GetKeyword:
	case Kind::SetKeyword:
	case Kind::AsyncKeyword:
	case Kind::DeclareKeyword:
	case Kind::OverrideKeyword:
		return true;
	default:
		return isClassMemberModifier(kind);
	}
}

// completions.go:4014
bool isInterfaceOrTypeLiteralCompletionKeyword(Kind kind) {
	return kind == Kind::ReadonlyKeyword;
}

// completions.go:4018
bool isContextualKeywordInAutoImportableExpressionSpace(
    const std::string& keyword) {
	return keyword == "abstract" || keyword == "async" ||
	    keyword == "await" || keyword == "declare" ||
	    keyword == "module" || keyword == "namespace" ||
	    keyword == "type" || keyword == "satisfies" || keyword == "as";
}

// completions.go:4031
std::vector<lsproto::CompletionItem*> getContextualKeywords(
    SourceFile* file, Node* contextToken, int position) {
	std::vector<lsproto::CompletionItem*> entries;
	// An `AssertClause` can come after an import declaration:
	//  import * from "foo" |
	//  import "foo" |
	// or after a re-export declaration that has a module specifier:
	//  export { foo } from "foo" |
	// Source: https://tc39.es/proposal-import-assertions/
	if (contextToken != nullptr) {
		Node* parent = contextToken->parent;
		int tokenLine = getECMALineOfPosition(file, contextToken->end());
		int currentLine = getECMALineOfPosition(file, position);
		if ((isImportDeclaration(parent) ||
		     (isExportDeclaration(parent) &&
		      parent->moduleSpecifier() != nullptr)) &&
		    contextToken == parent->moduleSpecifier() &&
		    tokenLine == currentLine) {
			entries.push_back(new lsproto::CompletionItem{
			    .Label = std::string(tokenToString(Kind::AssertKeyword)),
			    .Kind = std::make_shared<lsproto::CompletionItemKind>(
			        lsproto::CompletionItemKindKeyword),
			    .SortText = std::string(SortTextGlobalsOrKeywords),
			});
		}
	}
	return entries;
}

// completions.go:4062
std::vector<CompletionItem*> LanguageService::getJSCompletionEntries(
    const ContextPtr& ctx, SourceFile* file, int position,
    collections::Set<std::string>* uniqueNames,
    std::vector<CompletionItem*> sortedEntries) {
	const auto& nameTable = getNameTable(file);
	for (const auto& [name, pos] : nameTable) {
		// Skip identifiers produced only from the current location
		if (pos == position) {
			continue;
		}
		if (!uniqueNames->Has(name) &&
		    isIdentifierText(name, LanguageVariant::Standard)) {
			uniqueNames->Add(name);
			sortedEntries.push_back(new CompletionItem{
			    .completionItem = new lsproto::CompletionItem{
			        .Label = name,
			        .Kind = std::make_shared<lsproto::CompletionItemKind>(
			            lsproto::CompletionItemKindText),
			        .SortText =
			            std::string(SortTextJavascriptIdentifiers),
			        .CommitCharacters = std::make_shared<lsproto::Slice<std::string>>(
			            std::vector<std::string>{}),
			    },
			});
		}
	}
	return sortedEntries;
}

// completions.go:4079
lsproto::Range* LanguageService::getOptionalReplacementSpan(
    Node* location, SourceFile* file) {
	// StringLiteralLike locations are handled separately in
	// stringCompletions.ts
	if (location != nullptr &&
	    (location->kind == Kind::Identifier ||
	     location->kind == Kind::PrivateIdentifier)) {
		int start =
		    astnav::getStartOfNode(location, file,
		                           /*includeJSDoc*/ false);
		auto [lspRange, fidelity] =
		    createLspRangeFromBounds(start, location->end(), file);
		if (fidelity.IsExact()) {
			return new lsproto::Range(lspRange);
		}
	}
	return nullptr;
}

// completions.go:4087
bool isMemberCompletionKind(CompletionKind kind) {
	return kind == CompletionKindObjectPropertyDeclaration ||
	    kind == CompletionKindMemberLike ||
	    kind == CompletionKindPropertyAccess;
}

// completions.go:4093
Node* tryGetFunctionLikeBodyCompletionContainer(Node* contextToken) {
	if (contextToken == nullptr) {
		return nullptr;
	}

	Node* prev = nullptr;
	return findAncestorOrQuit(
	    contextToken, [&](Node* node) -> FindAncestorResult {
		    if (isClassLike(node)) {
			    return FindAncestorResult::Quit;
		    }
		    if (isFunctionLikeDeclaration(node) && prev == node->body()) {
			    return FindAncestorResult::True;
		    }
		    prev = node;
		    return FindAncestorResult::False;
	    });
}

// completions.go:4109
std::pair<bool, std::vector<std::string>>
computeCommitCharactersAndIsNewIdentifier(Node* contextToken,
                                          SourceFile* file, int position) {
	if (contextToken == nullptr) {
		return {false, allCommitCharacters};
	}
	Kind containingNodeKind = contextToken->parent->kind;
	Kind tokenKind = keywordForNode(contextToken);
	// Previous token may have been a keyword that was converted to an
	// identifier.
	switch (tokenKind) {
	case Kind::CommaToken:
		switch (containingNodeKind) {
		// func( a, |
		// new C(a, |
		case Kind::CallExpression:
		case Kind::NewExpression: {
			Node* expression = contextToken->parent->expression();
			// func\n(a, |
			if (getLineOfPosition(file, expression->end()) !=
			    getLineOfPosition(file, position)) {
				return {true, noCommaCommitCharacters};
			}
			return {true, allCommitCharacters};
		}
		// const x = (a, |
		case Kind::BinaryExpression:
			return {true, noCommaCommitCharacters};
		// constructor( a, | /* public, protected, private keywords are
		// allowed here, so show completion */
		// var x: (s: string, list|
		// const obj = { x, |
		case Kind::Constructor:
		case Kind::FunctionType:
		case Kind::ObjectLiteralExpression:
			return {true, emptyCommitCharacters};
		// [a, |
		case Kind::ArrayLiteralExpression:
			return {true, allCommitCharacters};
		default:
			return {false, allCommitCharacters};
		}
	case Kind::OpenParenToken:
		switch (containingNodeKind) {
		// func( |
		// new C(a|
		case Kind::CallExpression:
		case Kind::NewExpression: {
			Node* expression = contextToken->parent->expression();
			// func\n( |
			if (getLineOfPosition(file, expression->end()) !=
			    getLineOfPosition(file, position)) {
				return {true, noCommaCommitCharacters};
			}
			return {true, allCommitCharacters};
		}
		// const x = (a|
		case Kind::ParenthesizedExpression:
			return {true, noCommaCommitCharacters};
		// constructor( |
		// function F(pred: (a| /* this can become an arrow function,
		// where 'a' is the argument */
		case Kind::Constructor:
		case Kind::ParenthesizedType:
			return {true, emptyCommitCharacters};
		default:
			return {false, allCommitCharacters};
		}
	case Kind::OpenBracketToken:
		switch (containingNodeKind) {
		// [ |
		// [ | : string ]
		// [ | : string ]
		// [ |    /* this can become an index signature */
		case Kind::ArrayLiteralExpression:
		case Kind::IndexSignature:
		case Kind::TupleType:
		case Kind::ComputedPropertyName:
			return {true, allCommitCharacters};
		default:
			return {false, allCommitCharacters};
		}
	// module |
	// namespace |
	// import |
	case Kind::ModuleKeyword:
	case Kind::NamespaceKeyword:
	case Kind::ImportKeyword:
		return {true, emptyCommitCharacters};
	case Kind::DotToken:
		switch (containingNodeKind) {
		// module A.|
		case Kind::ModuleDeclaration:
			return {true, emptyCommitCharacters};
		default:
			return {false, allCommitCharacters};
		}
	case Kind::OpenBraceToken:
		switch (containingNodeKind) {
		// class A { |
		// const obj = { |
		case Kind::ClassDeclaration:
		case Kind::ObjectLiteralExpression:
			return {true, emptyCommitCharacters};
		default:
			return {false, allCommitCharacters};
		}
	case Kind::EqualsToken:
		switch (containingNodeKind) {
		// const x = a|
		// x = a|
		case Kind::VariableDeclaration:
		case Kind::BinaryExpression:
			return {true, allCommitCharacters};
		default:
			return {false, allCommitCharacters};
		}
	case Kind::TemplateHead:
		// `aa ${|
		return {containingNodeKind == Kind::TemplateExpression,
		        allCommitCharacters};
	case Kind::TemplateMiddle:
		// `aa ${10} dd ${|
		return {containingNodeKind == Kind::TemplateSpan,
		        allCommitCharacters};
	case Kind::AsyncKeyword:
		// const obj = { async c|()
		// const obj = { async c|
		if (containingNodeKind == Kind::MethodDeclaration ||
		    containingNodeKind == Kind::ShorthandPropertyAssignment) {
			return {true, emptyCommitCharacters};
		}
		return {false, allCommitCharacters};
	case Kind::AsteriskToken:
		// const obj = { * c|
		if (containingNodeKind == Kind::MethodDeclaration) {
			return {true, emptyCommitCharacters};
		}
		return {false, allCommitCharacters};
	default:
		break;
	}

	if (isClassMemberCompletionKeyword(tokenKind)) {
		return {true, emptyCommitCharacters};
	}

	return {false, allCommitCharacters};
}

// completions.go:4201
Kind keywordForNode(Node* node) {
	if (isIdentifier(node)) {
		return identifierToKeywordKind(node->as<Identifier>());
	}
	return node->kind;
}

// completions.go:4208 — Finds the first node that "embraces" the position,
// so that one may accurately aggregate locals from the closest containing
// scope.
Node* getScopeNode(Node* initialToken, int position, SourceFile* file) {
	Node* scope = initialToken;
	while (scope != nullptr &&
	       !positionBelongsToNode(scope, position, file)) {
		scope = scope->parent;
	}
	return scope;
}

// completions.go:4216
bool isSnippetScope(Node* scopeNode) {
	switch (scopeNode->kind) {
	case Kind::SourceFile:
	case Kind::TemplateExpression:
	case Kind::JsxExpression:
	case Kind::Block:
		return true;
	default:
		return isStatement(scopeNode);
	}
}

// completions.go:4228 — Determines if a type is exactly the same type
// resolved by the global 'self', 'global', or 'globalThis'.
bool isProbablyGlobalType(checker::Type* t, SourceFile* file,
                          checker::Checker* typeChecker) {
	// The type of `self` and `window` is the same in lib.dom.d.ts, but
	// `window` does not exist in lib.webworker.d.ts, so checking against
	// `self` is also a check against `window` when it exists.
	Symbol* selfSymbol = typeChecker->GetGlobalSymbol(
	    "self", SymbolFlagsValue, /*diagnostic*/ nullptr);
	if (selfSymbol != nullptr &&
	    typeChecker->GetTypeOfSymbolAtLocation(selfSymbol,
	                                           file->asNode()) == t) {
		return true;
	}
	Symbol* globalSymbol = typeChecker->GetGlobalSymbol(
	    "global", SymbolFlagsValue, /*diagnostic*/ nullptr);
	if (globalSymbol != nullptr &&
	    typeChecker->GetTypeOfSymbolAtLocation(globalSymbol,
	                                           file->asNode()) == t) {
		return true;
	}
	Symbol* globalThisSymbol = typeChecker->GetGlobalSymbol(
	    "globalThis", SymbolFlagsValue, /*diagnostic*/ nullptr);
	if (globalThisSymbol != nullptr &&
	    typeChecker->GetTypeOfSymbolAtLocation(globalThisSymbol,
	                                           file->asNode()) == t) {
		return true;
	}
	return false;
}

// completions.go:4246
Node* tryGetTypeLiteralNode(Node* node) {
	if (node == nullptr) {
		return nullptr;
	}

	Node* parent = node->parent;
	switch (node->kind) {
	case Kind::OpenBraceToken:
		if (isTypeLiteralNode(parent)) {
			return parent;
		}
		break;
	case Kind::SemicolonToken:
	case Kind::CommaToken:
	case Kind::Identifier:
		if (parent->kind == Kind::PropertySignature &&
		    isTypeLiteralNode(parent->parent)) {
			return parent->parent;
		}
		break;
	default:
		break;
	}

	return nullptr;
}

// completions.go:4263
checker::Type* getConstraintOfTypeArgumentProperty(
    Node* node, checker::Checker* typeChecker) {
	if (node == nullptr) {
		return nullptr;
	}

	if (isTypeNode(node)) {
		checker::Type* constraint =
		    typeChecker->GetTypeArgumentConstraint(node);
		if (constraint != nullptr) {
			return constraint;
		}
	}

	checker::Type* t =
	    getConstraintOfTypeArgumentProperty(node->parent, typeChecker);
	if (t == nullptr) {
		return nullptr;
	}

	switch (node->kind) {
	case Kind::PropertySignature: {
		// Try to get the reparsed node first - we may be in JSDoc.
		Node* reparsed = getReparsedNodeForNode(node);
		if (Symbol* symbol = reparsed->symbol(); symbol != nullptr) {
			return typeChecker->GetTypeOfPropertyOfContextualType(
			    t, symbol->name);
		}

		// In some cases, we won't have a corresponding symbol
		// (e.g. JSDoc types that never get re-attached) so we'll use
		// the name as declared by the property as a best-effort.
		std::string name;
		if (tryGetTextOfPropertyName(reparsed->name(), name)) {
			return typeChecker->GetTypeOfPropertyOfContextualType(t,
			                                                    name);
		}

		return nullptr;
	}
	case Kind::ColonToken:
		if (node->parent->kind == Kind::PropertySignature) {
			// The cursor is at a property value location like
			// `Foo<{ x: | }`.
			// `t` already refers to the appropriate property type.
			return t;
		}
		break;
	case Kind::IntersectionType:
	case Kind::TypeLiteral:
	case Kind::UnionType:
		return t;
	case Kind::OpenBracketToken:
		return typeChecker->GetElementTypeOfArrayType(t);
	default:
		break;
	}

	return nullptr;
}

// completions.go:4308
Node* tryGetObjectLikeCompletionContainer(Node* contextToken, int position,
                                          SourceFile* file) {
	if (contextToken == nullptr) {
		return nullptr;
	}

	Node* parent = contextToken->parent;
	switch (contextToken->kind) {
	// const x = { |
	// const x = { a: 0, |
	case Kind::OpenBraceToken:
	case Kind::CommaToken:
		if (isObjectLiteralExpression(parent) ||
		    isObjectBindingPattern(parent)) {
			return parent;
		}
		break;
	case Kind::AsteriskToken:
		if (isMethodDeclaration(parent) &&
		    isObjectLiteralExpression(parent->parent)) {
			return parent->parent;
		}
		break;
	case Kind::AsyncKeyword:
		if (isObjectLiteralExpression(parent->parent)) {
			return parent->parent;
		}
		break;
	case Kind::Identifier:
		if (contextToken->text() == "async" &&
		    isShorthandPropertyAssignment(parent)) {
			return parent->parent;
		} else {
			if (isObjectLiteralExpression(parent->parent) &&
			    (isSpreadAssignment(parent) ||
			     (isShorthandPropertyAssignment(parent) &&
			      getLineOfPosition(file, contextToken->end()) !=
			          getLineOfPosition(file, position)))) {
				return parent->parent;
			}
			Node* ancestorNode =
			    findAncestor(parent, isPropertyAssignment);
			if (ancestorNode != nullptr &&
			    lsutil::GetLastToken(ancestorNode, file) ==
			        contextToken &&
			    isObjectLiteralExpression(ancestorNode->parent)) {
				return ancestorNode->parent;
			}
		}
		break;
	default:
		if (parent->parent != nullptr && parent->parent->parent != nullptr &&
		    (isMethodDeclaration(parent->parent) ||
		     isGetAccessorDeclaration(parent->parent) ||
		     isSetAccessorDeclaration(parent->parent)) &&
		    isObjectLiteralExpression(parent->parent->parent)) {
			return parent->parent->parent;
		}
		if (isSpreadAssignment(parent) &&
		    isObjectLiteralExpression(parent->parent)) {
			return parent->parent;
		}
		Node* ancestorNode = findAncestor(parent, isPropertyAssignment);
		if (contextToken->kind != Kind::ColonToken &&
		    ancestorNode != nullptr &&
		    lsutil::GetLastToken(ancestorNode, file) == contextToken &&
		    isObjectLiteralExpression(ancestorNode->parent)) {
			return ancestorNode->parent;
		}
		break;
	}

	return nullptr;
}

// completions.go:4361
checker::Type* tryGetObjectLiteralContextualType(
    Node* node, checker::Checker* typeChecker) {
	checker::Type* t = typeChecker->GetContextualType(
	    node, checker::ContextFlagsNone);
	if (t != nullptr) {
		return t;
	}

	Node* parent = walkUpParenthesizedExpressions(node->parent);
	if (isBinaryExpression(parent) &&
	    parent->as<BinaryExpression>()->OperatorToken->kind ==
	        Kind::EqualsToken &&
	    node == parent->as<BinaryExpression>()->Left) {
		// Object literal is assignment pattern: ({ | } = x)
		return typeChecker->GetTypeAtLocation(parent);
	}
	if (isExpression(parent)) {
		// f(() => (({ | })));
		return typeChecker->GetContextualType(parent,
		                                      checker::ContextFlagsNone);
	}

	return nullptr;
}

// completions.go:4382
std::vector<Symbol*> getPropertiesForObjectExpression(
    checker::Type* contextualType, checker::Type* completionsType,
    Node* obj, checker::Checker* typeChecker) {
	bool hasCompletionsType =
	    completionsType != nullptr && completionsType != contextualType;
	std::vector<checker::Type*> types;
	if (contextualType->IsUnion()) {
		types = contextualType->types();
	} else {
		types = {contextualType};
	}
	checker::Type* promiseFilteredContextualType =
	    typeChecker->GetUnionType(filterList(
	        types, [&](checker::Type* t) {
		        return typeChecker->GetPromisedTypeOfPromise(t) ==
		            nullptr;
	        }));

	checker::Type* t;
	if (hasCompletionsType &&
	    (completionsType->flags & checker::TypeFlagsAnyOrUnknown) == 0) {
		t = typeChecker->GetUnionType(
		    {promiseFilteredContextualType, completionsType});
	} else {
		t = promiseFilteredContextualType;
	}

	// Filter out members whose only declaration is the object literal
	// itself to avoid self-fulfilling completions like:
	//
	// function f<T>(x: T) {}
	// f({ abc/**/: "" }) // `abc` is a member of `T` but only because it
	// declares itself
	auto hasDeclarationOtherThanSelf = [&](Symbol* member) -> bool {
		if (member->declarations.empty()) {
			return true;
		}
		return someList(member->declarations, [&](Node* decl) {
			return decl->parent != obj;
		});
	};

	std::vector<Symbol*> properties =
	    getApparentProperties(t, obj, typeChecker);
	if (t->IsClass() && containsNonPublicProperties(properties)) {
		return {};
	} else if (hasCompletionsType) {
		return filterList(properties, hasDeclarationOtherThanSelf);
	} else {
		return properties;
	}
}

// completions.go:4428
std::vector<Symbol*> getApparentProperties(
    checker::Type* t, Node* node, checker::Checker* typeChecker) {
	if (!t->IsUnion()) {
		return typeChecker->GetApparentProperties(t);
	}
	return typeChecker->GetAllPossiblePropertiesOfTypes(filterList(
	    t->types(), [&](checker::Type* memberType) {
		    return !(
		        (memberType->flags & checker::TypeFlagsPrimitive) !=
		            0 ||
		        typeChecker->IsArrayLikeType(memberType) ||
		        typeChecker->IsTypeInvalidDueToUnionDiscriminant(
		            memberType, node) ||
		        typeChecker->TypeHasCallOrConstructSignatures(
		            memberType) ||
		        (memberType->IsClass() &&
		         containsNonPublicProperties(
		             typeChecker->GetApparentProperties(memberType))));
	    }));
}

// completions.go:4440
bool containsNonPublicProperties(const std::vector<Symbol*>& props) {
	return someList(props, [](Symbol* p) {
		return (checker::GetDeclarationModifierFlagsFromSymbol(p) &
		        ModifierFlagsNonPublicAccessibilityModifier) != 0;
	});
}

// completions.go:4447 — Filters out members that are already declared in
// the object literal or binding pattern. Also computes the set of existing
// members declared by spread assignment.
std::pair<std::vector<Symbol*>, collections::Set<std::string>>
filterObjectMembersList(
    const std::vector<Symbol*>& contextualMemberSymbols,
    const std::vector<Node*>& existingMembers, SourceFile* file,
    int position, checker::Checker* typeChecker) {
	if (existingMembers.empty()) {
		return {contextualMemberSymbols,
		        collections::Set<std::string>{}};
	}

	collections::Set<std::string> membersDeclaredBySpreadAssignment;
	collections::Set<std::string> existingMemberNames;
	for (Node* member : existingMembers) {
		// Ignore omitted expressions for missing members.
		if (member->kind != Kind::PropertyAssignment &&
		    member->kind != Kind::ShorthandPropertyAssignment &&
		    member->kind != Kind::BindingElement &&
		    member->kind != Kind::MethodDeclaration &&
		    member->kind != Kind::GetAccessor &&
		    member->kind != Kind::SetAccessor &&
		    member->kind != Kind::SpreadAssignment) {
			continue;
		}

		// If this is the current item we are editing right now, do not
		// filter it out.
		if (isCurrentlyEditingNode(member, file, position)) {
			continue;
		}

		std::string existingName;

		if (isSpreadAssignment(member)) {
			setMemberDeclaredBySpreadAssignment(
			    member, &membersDeclaredBySpreadAssignment,
			    typeChecker);
		} else if (isBindingElement(member) &&
		           member->propertyName() != nullptr) {
			// include only identifiers in completion list
			if (member->propertyName()->kind == Kind::Identifier) {
				existingName = member->propertyName()->text();
			}
		} else {
			// TODO: Account for computed property name
			// NOTE: if one only performs this step when m.name is an
			// identifier, things like '__proto__' are not filtered out.
			Node* name = getNameOfDeclaration(member);
			if (name != nullptr && isPropertyNameLiteral(name)) {
				existingName = name->text();
			}
		}

		if (!existingName.empty()) {
			existingMemberNames.Add(existingName);
		}
	}

	std::vector<Symbol*> filteredSymbols =
	    filterList(contextualMemberSymbols, [&](Symbol* m) {
		    return !existingMemberNames.Has(m->name);
	    });

	return {filteredSymbols, membersDeclaredBySpreadAssignment};
}

// completions.go:4508
bool isCurrentlyEditingNode(Node* node, SourceFile* file, int position) {
	int start =
	    astnav::getStartOfNode(node, file, /*includeJSDoc*/ false);
	return start <= position && position <= node->end();
}

// completions.go:4513
void setMemberDeclaredBySpreadAssignment(
    Node* declaration,
    collections::Set<std::string>* members,
    checker::Checker* typeChecker) {
	Node* expression = declaration->expression();
	Symbol* symbol = typeChecker->GetSymbolAtLocation(expression);
	checker::Type* t = nullptr;
	if (symbol != nullptr) {
		t = typeChecker->GetTypeOfSymbolAtLocation(symbol, expression);
	}
	std::vector<Symbol*> properties;
	if (t != nullptr &&
	    (t->flags & checker::TypeFlagsStructuredType) != 0) {
		properties = t->AsStructuredType()->properties;
	}
	for (Symbol* property : properties) {
		members->Add(property->name);
	}
}

// completions.go:4531 — Returns the immediate owning class declaration of
// a context token, on the condition that one exists and that the context
// implies completion should be given.
Node* tryGetConstructorLikeCompletionContainer(Node* contextToken) {
	if (contextToken == nullptr) {
		return nullptr;
	}

	Node* parent = contextToken->parent;
	switch (contextToken->kind) {
	case Kind::OpenParenToken:
	case Kind::CommaToken:
		if (isConstructorDeclaration(parent)) {
			return parent;
		}
		return nullptr;
	default:
		if (isConstructorParameterCompletion(contextToken)) {
			return parent->parent;
		}
		break;
	}
	return nullptr;
}

// completions.go:4548
bool isConstructorParameterCompletion(Node* node) {
	return node->parent != nullptr &&
	    isParameterDeclaration(node->parent) &&
	    isConstructorDeclaration(node->parent->parent) &&
	    (isParameterPropertyModifier(node->kind) ||
	     isDeclarationName(node));
}

// completions.go:4558 — Returns the immediate owning class declaration of
// a context token, on the condition that one exists and that the context
// implies completion should be given.
Node* tryGetObjectTypeDeclarationCompletionContainer(
    SourceFile* file, Node* contextToken, Node* location, int position) {
	// class c { method() { } | method2() { } }
	switch (location->kind) {
	case Kind::SyntaxList:
		if (isObjectTypeDeclaration(location->parent)) {
			return location->parent;
		}
		return nullptr;
	case Kind::EndOfFile: {
		NodeList* stmtList = location->parent->statementList();
		if (stmtList != nullptr && !stmtList->nodes.empty() &&
		    isObjectTypeDeclaration(stmtList->nodes.back())) {
			Node* cls = stmtList->nodes.back();
			if (astnav::findChildOfKind(cls, Kind::CloseBraceToken,
			                            file) == nullptr) {
				return cls;
			}
		}
		break;
	}
	case Kind::PrivateIdentifier:
		if (isPropertyDeclaration(location->parent)) {
			return findAncestor(location, isClassLike);
		}
		break;
	case Kind::Identifier: {
		Kind originalKeywordKind =
		    identifierToKeywordKind(location->as<Identifier>());
		if (originalKeywordKind != Kind::Unknown) {
			return nullptr;
		}
		// class c { public prop = c| }
		if (isPropertyDeclaration(location->parent) &&
		    location->parent->initializer() == location) {
			return nullptr;
		}
		// class c extends React.Component { a: () => 1\n compon| }
		if (isFromObjectTypeDeclaration(location)) {
			return findAncestor(location, isObjectTypeDeclaration);
		}
		break;
	}
	default:
		break;
	}

	if (contextToken == nullptr) {
		return nullptr;
	}

	// class C { blah; constructor/**/ }
	// or
	// class C { blah \n constructor/**/ }
	if (location->kind == Kind::ConstructorKeyword ||
	    (isIdentifier(contextToken) &&
	     isPropertyDeclaration(contextToken->parent) &&
	     isClassLike(location))) {
		return findAncestor(contextToken, isClassLike);
	}

	switch (contextToken->kind) {
	// class c { public prop = | /* global completions */ }
	case Kind::EqualsToken:
		return nullptr;
	// class c {getValue(): number; | }
	// class c { method() { } | }
	case Kind::SemicolonToken:
	case Kind::CloseBraceToken:
		// class c { method() { } b| }
		if (isFromObjectTypeDeclaration(location) &&
		    location->parent->name() == location) {
			return location->parent->parent;
		}
		if (isObjectTypeDeclaration(location)) {
			return location;
		}
		return nullptr;
	// class c { |
	// class c {getValue(): number, | }
	case Kind::OpenBraceToken:
	case Kind::CommaToken:
		if (isObjectTypeDeclaration(contextToken->parent)) {
			return contextToken->parent;
		}
		return nullptr;
	default:
		if (isObjectTypeDeclaration(location)) {
			// class C extends React.Component { a: () => 1\n| }
			// class C { prop = ""\n | }
			if (getLineOfPosition(file, contextToken->end()) !=
			    getLineOfPosition(file, position)) {
				return location;
			}
			auto isValidKeyword = ifElse<bool (*)(Kind)>(
			    isClassLike(contextToken->parent->parent),
			    isClassMemberCompletionKeyword,
			    isInterfaceOrTypeLiteralCompletionKeyword);

			if (isValidKeyword(contextToken->kind) ||
			    contextToken->kind == Kind::AsteriskToken ||
			    (isIdentifier(contextToken) &&
			     isValidKeyword(identifierToKeywordKind(
			         contextToken->as<Identifier>())))) {
				return contextToken->parent->parent;
			}
		}

		return nullptr;
	}
}

// completions.go:4642
bool isFromObjectTypeDeclaration(Node* node) {
	return node->parent != nullptr &&
	    isClassOrTypeElement(node->parent) &&
	    isObjectTypeDeclaration(node->parent->parent);
}

// completions.go:4646 — Filters out completion suggestions for class
// elements.
std::vector<Symbol*> filterClassMembersList(
    const std::vector<Symbol*>& baseSymbols,
    const std::vector<Node*>& existingMembers,
    ModifierFlags classElementModifierFlags, SourceFile* file,
    int position) {
	collections::Set<std::string> existingMemberNames;
	for (Node* member : existingMembers) {
		// Ignore omitted expressions for missing members.
		if (member->kind != Kind::PropertyDeclaration &&
		    member->kind != Kind::MethodDeclaration &&
		    member->kind != Kind::GetAccessor &&
		    member->kind != Kind::SetAccessor) {
			continue;
		}

		// If this is the current item we are editing right now, do not
		// filter it out
		if (isCurrentlyEditingNode(member, file, position)) {
			continue;
		}

		// Don't filter member even if the name matches if it is
		// declared private in the list.
		if ((member->modifierFlags() & ModifierFlagsPrivate) != 0) {
			continue;
		}

		// Do not filter it out if the static presence doesn't match.
		if (isStatic(member) !=
		    ((classElementModifierFlags & ModifierFlagsStatic) != 0)) {
			continue;
		}

		std::string existingName =
		    getPropertyNameForPropertyNameNode(member->name());
		if (!existingName.empty()) {
			existingMemberNames.Add(existingName);
		}
	}

	return filterList(baseSymbols, [&](Symbol* propertySymbol) {
		return !existingMemberNames.Has(symbolName(propertySymbol)) &&
		    !propertySymbol->declarations.empty() &&
		    (checker::GetDeclarationModifierFlagsFromSymbol(
		         propertySymbol) &
		     ModifierFlagsPrivate) == 0 &&
		    !(propertySymbol->valueDeclaration != nullptr &&
		      isPrivateIdentifierClassElementDeclaration(
		          propertySymbol->valueDeclaration));
	});
}

// completions.go:4688
Node* tryGetContainingJsxElement(Node* contextToken, SourceFile* file) {
	if (contextToken == nullptr) {
		return nullptr;
	}

	Node* parent = contextToken->parent;
	switch (contextToken->kind) {
	case Kind::GreaterThanToken:
	case Kind::LessThanSlashToken:
	case Kind::SlashToken:
	case Kind::Identifier:
	case Kind::PropertyAccessExpression:
	case Kind::JsxNamespacedName:
	case Kind::JsxAttributes:
	case Kind::JsxAttribute:
	case Kind::JsxSpreadAttribute:
		if (parent != nullptr &&
		    (parent->kind == Kind::JsxSelfClosingElement ||
		     parent->kind == Kind::JsxOpeningElement)) {
			if (contextToken->kind == Kind::GreaterThanToken) {
				Node* precedingToken = astnav::findPrecedingToken(
				    file, contextToken->pos());
				if (parent->typeArguments().empty() ||
				    (precedingToken != nullptr &&
				     precedingToken->kind ==
				         Kind::SlashToken)) {
					return nullptr;
				}
			}
			return parent;
		} else if (parent != nullptr && isJsxNamespacedName(parent) &&
		           parent->parent != nullptr &&
		           (parent->parent->kind ==
		                Kind::JsxSelfClosingElement ||
		            parent->parent->kind ==
		                Kind::JsxOpeningElement)) {
			return parent->parent;
		} else if (parent != nullptr &&
		           parent->kind == Kind::JsxAttribute) {
			// Currently we parse JsxOpeningLikeElement as:
			//      JsxOpeningLikeElement
			//          attributes: JsxAttributes
			//             properties: NodeArray<JsxAttributeLike>
			return parent->parent->parent;
		}
		break;
	// The context token is the closing } or " of an attribute, which
	// means its parent is a JsxExpression, whose parent is a
	// JsxAttribute, whose parent is a JsxOpeningLikeElement
	case Kind::StringLiteral:
		if (parent != nullptr &&
		    (parent->kind == Kind::JsxAttribute ||
		     parent->kind == Kind::JsxSpreadAttribute)) {
			// Currently we parse JsxOpeningLikeElement as:
			//      JsxOpeningLikeElement
			//          attributes: JsxAttributes
			//             properties: NodeArray<JsxAttributeLike>
			return parent->parent->parent;
		}
		break;
	case Kind::CloseBraceToken:
		if (parent != nullptr && parent->kind == Kind::JsxExpression &&
		    parent->parent != nullptr &&
		    parent->parent->kind == Kind::JsxAttribute) {
			// Currently we parse JsxOpeningLikeElement as:
			//      JsxOpeningLikeElement
			//          attributes: JsxAttributes
			//             properties: NodeArray<JsxAttributeLike>
			//                  each JsxAttribute can have initializer
			//                  as JsxExpression
			return parent->parent->parent->parent;
		}
		if (parent != nullptr &&
		    parent->kind == Kind::JsxSpreadAttribute) {
			// Currently we parse JsxOpeningLikeElement as:
			//      JsxOpeningLikeElement
			//          attributes: JsxAttributes
			//             properties: NodeArray<JsxAttributeLike>
			return parent->parent->parent;
		}
		break;
	default:
		break;
	}

	return nullptr;
}

// completions.go:4751 — Filters out completion suggestions from 'symbols'
// according to existing JSX attributes.
// @returns Symbols to be suggested in a JSX element, barring those whose
// attributes do not occur at the current position and have not otherwise
// been typed.
std::pair<std::vector<Symbol*>, collections::Set<std::string>>
filterJsxAttributes(const std::vector<Symbol*>& symbols,
                    const std::vector<Node*>& attributes, SourceFile* file,
                    int position, checker::Checker* typeChecker) {
	collections::Set<std::string> existingNames;
	collections::Set<std::string> membersDeclaredBySpreadAssignment;
	for (Node* attr : attributes) {
		// If this is the item we are editing right now, do not filter it
		// out.
		if (isCurrentlyEditingNode(attr, file, position)) {
			continue;
		}

		if (attr->kind == Kind::JsxAttribute) {
			existingNames.Add(attr->name()->text());
		} else if (isJsxSpreadAttribute(attr)) {
			setMemberDeclaredBySpreadAssignment(
			    attr, &membersDeclaredBySpreadAssignment, typeChecker);
		}
	}

	return {filterList(symbols,
	                   [&](Symbol* a) {
		                   return !existingNames.Has(a->name);
	                   }),
	        membersDeclaredBySpreadAssignment};
}

// completions.go:4781
bool isTypeKeywordTokenOrIdentifier(Node* node) {
	return isTypeKeywordToken(node) ||
	    (isIdentifier(node) &&
	     identifierToKeywordKind(node->as<Identifier>()) ==
	         Kind::TypeKeyword);
}

// completions.go:4840 — Returns the item defaults for completion items, if
// that capability is supported. Otherwise, if some item default is not
// supported by client, sets that property on each item.
lsproto::CompletionItemDefaults* LanguageService::setItemDefaults(
    const ContextPtr& ctx, int position, SourceFile* file,
    std::vector<CompletionItem*> items,
    std::vector<std::string>* defaultCommitCharacters,
    lsproto::Range* optionalReplacementSpan) {
	lsproto::CompletionItemDefaults* itemDefaults = nullptr;
	if (defaultCommitCharacters != nullptr) {
		bool supportsItemCommitCharacters =
		    clientSupportsItemCommitCharacters(ctx);
		if (clientSupportsDefaultCommitCharacters(ctx) &&
		    supportsItemCommitCharacters) {
			itemDefaults = new lsproto::CompletionItemDefaults{
			    .CommitCharacters = std::make_shared<lsproto::Slice<std::string>>(
			        *defaultCommitCharacters),
			};
		} else if (supportsItemCommitCharacters) {
			for (CompletionItem* item : items) {
				if (item->completionItem->CommitCharacters ==
				    nullptr) {
					item->completionItem->CommitCharacters =
					    std::make_shared<lsproto::Slice<std::string>>(
					        *defaultCommitCharacters);
				}
			}
		}
	}
	if (optionalReplacementSpan != nullptr) {
		// Ported from vscode ts extension.
		auto [end, fidelity] = createLspPosition(position, file);
		if (!fidelity.IsExact()) {
			return itemDefaults;
		}
		lsproto::Range insertRange{
		    .Start = optionalReplacementSpan->Start,
		    .End = end,
		};
		if (clientSupportsDefaultEditRange(ctx)) {
			if (itemDefaults == nullptr) {
				itemDefaults = new lsproto::CompletionItemDefaults();
			}
			itemDefaults->EditRange =
			    std::make_shared<lsproto::RangeOrEditRangeWithInsertReplace>(
			        lsproto::RangeOrEditRangeWithInsertReplace{
			            .EditRangeWithInsertReplace =
			                std::make_shared<lsproto::EditRangeWithInsertReplace>(
			                    lsproto::EditRangeWithInsertReplace{
			                        .Insert = insertRange,
			                        .Replace = *optionalReplacementSpan,
			                    }),
			        });
			for (CompletionItem* item : items) {
				// If `editRange` is set, `insertText` is ignored by the
				// client, so we need to provide `textEdit` instead.
				if (item->completionItem->InsertText.has_value() &&
				    item->completionItem->TextEdit == nullptr) {
					item->completionItem->TextEdit =
					    std::make_shared<lsproto::TextEditOrInsertReplaceEdit>(
					        lsproto::TextEditOrInsertReplaceEdit{
					            .InsertReplaceEdit =
					                std::make_shared<lsproto::InsertReplaceEdit>(
					                    lsproto::InsertReplaceEdit{
					                        .NewText = *item->completionItem
					                                       ->InsertText,
					                        .Insert = insertRange,
					                        .Replace =
					                            *optionalReplacementSpan,
					                    }),
					        });
					item->completionItem->InsertText = std::nullopt;
				}
			}
		} else if (clientSupportsItemInsertReplace(ctx)) {
			for (CompletionItem* item : items) {
				if (item->completionItem->TextEdit == nullptr) {
					std::string newText =
					    item->completionItem->InsertText.has_value()
					        ? *item->completionItem->InsertText
					        : item->completionItem->Label;
					item->completionItem->TextEdit =
					    std::make_shared<lsproto::TextEditOrInsertReplaceEdit>(
					        lsproto::TextEditOrInsertReplaceEdit{
					            .InsertReplaceEdit =
					                std::make_shared<lsproto::InsertReplaceEdit>(
					                    lsproto::InsertReplaceEdit{
					                        .NewText = newText,
					                        .Insert = insertRange,
					                        .Replace =
					                            *optionalReplacementSpan,
					                    }),
					        });
				}
			}
		}
	}

	return itemDefaults;
}

// completions.go:4890
CompletionList* LanguageService::specificKeywordCompletionInfo(
    const ContextPtr& ctx, int position, SourceFile* file,
    std::vector<CompletionItem*> items, bool isNewIdentifierLocation,
    lsproto::Range* optionalReplacementSpan) {
	std::vector<std::string> defaultCommitCharacters =
	    getDefaultCommitCharacters(isNewIdentifierLocation);
	lsproto::CompletionItemDefaults* itemDefaults = setItemDefaults(
	    ctx, position, file, items, &defaultCommitCharacters,
	    optionalReplacementSpan);
	return new CompletionList{
	    .IsIncomplete = false,
	    .ItemDefaults = itemDefaults,
	    .Items = items,
	};
}

// completions.go:4913
CompletionList* LanguageService::getJsxClosingTagCompletion(
    const ContextPtr& ctx, Node* location, SourceFile* file, int position) {
	// We wanna walk up the tree till we find a JSX closing element.
	Node* jsxClosingElement = findAncestorOrQuit(
	    location, [](Node* node) -> FindAncestorResult {
		    switch (node->kind) {
		    case Kind::JsxClosingElement:
			    return FindAncestorResult::True;
		    case Kind::LessThanSlashToken:
		    case Kind::GreaterThanToken:
		    case Kind::Identifier:
		    case Kind::PropertyAccessExpression:
			    return FindAncestorResult::False;
		    default:
			    return FindAncestorResult::Quit;
		    }
	    });

	if (jsxClosingElement == nullptr) {
		return nullptr;
	}

	// In the TypeScript JSX element, if such element is not defined. When
	// users query for completion at closing tag, instead of simply giving
	// unknown value, the completion will return the tag-name of an
	// associated opening-element.
	// For example:
	//     var x = <div> </ /*1*/
	// The completion list at "1" will contain "div>" with type any
	// And at `<div> </ /*1*/ >` (with a closing `>`), the completion list
	// will contain "div".
	// And at property access expressions
	// `<MainComponent.Child> </MainComponent. /*1*/ >` the completion
	// will return full closing tag with an optional replacement span
	// For example:
	//     var x = <MainComponent.Child> </     MainComponent /*1*/  >
	//     var y = <MainComponent.Child> </   /*2*/   MainComponent >
	// the completion list at "1" and "2" will contain
	// "MainComponent.Child" with a replacement span of closing tag name
	bool hasClosingAngleBracket =
	    astnav::findChildOfKind(jsxClosingElement, Kind::GreaterThanToken,
	                            file) != nullptr;
	Node* tagName = jsxClosingElement->parent->as<JsxElement>()
	                    ->OpeningElement->tagName();
	std::string closingTag = getTextOfNode(tagName);
	std::string fullClosingTag =
	    closingTag + (hasClosingAngleBracket ? "" : ">");
	auto [optionalReplacementSpan, fidelity] = createLspRangeFromNode(
	    jsxClosingElement->tagName(), file);
	if (!fidelity.IsExact()) {
		return nullptr;
	}
	std::vector<std::string> defaultCommitCharacters =
	    getDefaultCommitCharacters(/*isNewIdentifierLocation*/ false);

	lsproto::CompletionItem* lspItem = createLSPCompletionItem(
	    ctx,
	    fullClosingTag, /*name*/
	    "",             /*insertText*/
	    "",             /*filterText*/
	    SortTextLocationPriority,
	    lsutil::ScriptElementKindClassElement,
	    lsutil::ScriptElementKindModifierNone, /*kindModifiers*/
	    nullptr,                               /*replacementSpan*/
	    nullptr,                               /*commitCharacters*/
	    nullptr,                               /*labelDetails*/
	    file, position,
	    true,  /*isMemberCompletion*/
	    false, /*isSnippet*/
	    false, /*hasAction*/
	    false, /*preselect*/
	    "",    /*source*/
	    nullptr, /*autoImportEntryData*/ // !!! jsx autoimports
	    nullptr, /*additionalTextEdits*/
	    nullptr  /*detail*/);
	CompletionItem* item = new CompletionItem{
	    .completionItem = lspItem,
	};
	std::vector<CompletionItem*> items{item};
	lsproto::CompletionItemDefaults* itemDefaults = setItemDefaults(
	    ctx, position, file, items, &defaultCommitCharacters,
	    &optionalReplacementSpan);

	return new CompletionList{
	    .IsIncomplete = false,
	    .ItemDefaults = itemDefaults,
	    .Items = items,
	};
}

// completions.go:5023
lsproto::CompletionItem* LanguageService::createLSPCompletionItem(
    const ContextPtr& ctx, const std::string& name,
    const std::string& insertText, const std::string& filterText,
    const SortText& sortText, lsutil::ScriptElementKind elementKind,
    lsutil::ScriptElementKindModifier kindModifiers,
    lsproto::Range* replacementSpan,
    std::vector<std::string>* commitCharacters,
    lsproto::CompletionItemLabelDetails* labelDetails, SourceFile* file,
    int position, bool isMemberCompletion, bool isSnippet, bool hasAction,
    bool preselect, const std::string& source,
    lsproto::AutoImportFix* autoImportFix,
    std::vector<std::shared_ptr<lsproto::TextEdit>>* additionalTextEdits,
    std::string* detail) {
	lsproto::CompletionItemKind kind = getCompletionsSymbolKind(elementKind);
	// Go stores `AutoImportFix` by pointer on the item data; the canonical
	// field is shared_ptr, so copy the caller-owned fix (borrowed).
	auto data = std::make_shared<lsproto::CompletionItemData>(
	    lsproto::CompletionItemData{
	        .FileName = file->OriginalFileName(),
	        .Position = int32_t(position),
	        .SupplementalFileIndex = supplementalFileIndex(file),
	        .Source = source,
	        .Name = name,
	        .AutoImport = autoImportFix != nullptr
	            ? std::make_shared<lsproto::AutoImportFix>(*autoImportFix)
	            : nullptr,
	    });

	// Text edit
	std::shared_ptr<lsproto::TextEditOrInsertReplaceEdit> textEdit;
	if (replacementSpan != nullptr) {
		textEdit = std::make_shared<lsproto::TextEditOrInsertReplaceEdit>(
		    lsproto::TextEditOrInsertReplaceEdit{
		        .TextEdit = std::make_shared<lsproto::TextEdit>(
		            lsproto::TextEdit{
		                .NewText =
		                    insertText.empty() ? name : insertText,
		                .Range = *replacementSpan,
		            }),
		    });
	}

	// Filter text

	// Ported from vscode ts extension.
	std::string nameMut = name;
	std::string insertTextMut = insertText;
	std::string filterTextMut = filterText;
	auto [wordSize, wordStart] = getWordLengthAndStart(file, position);
	std::string dotAccessor = getDotAccessor(file, position - wordSize);
	if (filterTextMut.empty()) {
		filterTextMut =
		    getFilterText(file, position, insertTextMut, nameMut,
		                  wordStart, dotAccessor);
	}

	// Adjustements based on kind modifiers.
	std::shared_ptr<lsproto::Slice<lsproto::CompletionItemTag>> tags;
	// Copied from vscode ts extension: `MyCompletionItem.constructor`.
	if (isMemberCompletion &&
	    (kindModifiers & lsutil::ScriptElementKindModifierOptional) !=
	    lsutil::ScriptElementKindModifierNone) {
		if (insertTextMut.empty()) {
			insertTextMut = nameMut;
		}
		if (filterTextMut.empty() || isSnippet) {
			filterTextMut = nameMut;
		}
		nameMut = nameMut + "?";
	}
	if ((kindModifiers & lsutil::ScriptElementKindModifierDeprecated) !=
	    lsutil::ScriptElementKindModifierNone) {
		tags = std::make_shared<lsproto::Slice<lsproto::CompletionItemTag>>(
		    std::vector<lsproto::CompletionItemTag>{
		        lsproto::CompletionItemTagDeprecated});
	}

	if (hasAction && !source.empty()) {
		// !!! adjust label like vscode does
	}

	// Client assumes plain text by default.
	std::shared_ptr<lsproto::InsertTextFormat> insertTextFormat;
	if (isSnippet) {
		insertTextFormat = std::make_shared<lsproto::InsertTextFormat>(
		    lsproto::InsertTextFormatSnippet);
	}

	return new lsproto::CompletionItem{
	    .Label = nameMut,
	    .LabelDetails = labelDetails != nullptr
	        ? std::make_shared<lsproto::CompletionItemLabelDetails>(
	            *labelDetails)
	        : nullptr,
	    .Kind = std::make_shared<lsproto::CompletionItemKind>(kind),
	    .Tags = tags,
	    .Detail = detail != nullptr ? std::optional<std::string>(*detail)
	                                : std::nullopt,
	    .Preselect = preselect ? std::optional<bool>(true)
	                               : std::nullopt,
	    .SortText = std::string(sortText),
	    .FilterText = !filterTextMut.empty()
	        ? std::optional<std::string>(filterTextMut)
	        : std::nullopt,
	    .InsertText = !insertTextMut.empty()
	        ? std::optional<std::string>(insertTextMut)
	        : std::nullopt,
	    .InsertTextFormat = insertTextFormat,
	    .TextEdit = textEdit,
	    .CommitCharacters = commitCharacters != nullptr
	        ? std::make_shared<lsproto::Slice<std::string>>(*commitCharacters)
	        : nullptr,
	    .AdditionalTextEdits = additionalTextEdits != nullptr
	        ? std::make_shared<lsproto::Slice<std::shared_ptr<lsproto::TextEdit>>>(
	            *additionalTextEdits)
	        : nullptr,
	    .Data = data,
	};
}

// completions.go:5119
CompletionList* LanguageService::getLabelCompletionsAtPosition(
    const ContextPtr& ctx, Node* node, SourceFile* file, int position,
    lsproto::Range* optionalReplacementSpan) {
	std::vector<CompletionItem*> items =
	    getLabelStatementCompletions(ctx, node, file, position);
	if (items.empty()) {
		return nullptr;
	}
	std::vector<std::string> defaultCommitCharacters =
	    getDefaultCommitCharacters(/*isNewIdentifierLocation*/ false);
	lsproto::CompletionItemDefaults* itemDefaults = setItemDefaults(
	    ctx, position, file, items, &defaultCommitCharacters,
	    optionalReplacementSpan);
	return new CompletionList{
	    .IsIncomplete = false,
	    .ItemDefaults = itemDefaults,
	    .Items = items,
	};
}

// completions.go:5145
std::vector<CompletionItem*> LanguageService::getLabelStatementCompletions(
    const ContextPtr& ctx, Node* node, SourceFile* file, int position) {
	collections::Set<std::string> uniques;
	std::vector<CompletionItem*> items;
	Node* current = node;
	while (current != nullptr) {
		if (isFunctionLike(current)) {
			break;
		}
		if (isLabeledStatement(current)) {
			const std::string& name = current->label()->text();
			if (!uniques.Has(name)) {
				uniques.Add(name);
				lsproto::CompletionItem* lspItem = createLSPCompletionItem(
				    ctx,
				    name,
				    "", /*insertText*/
				    "", /*filterText*/
				    SortTextLocationPriority,
				    lsutil::ScriptElementKindLabel,
				    lsutil::ScriptElementKindModifierNone, /*kindModifiers*/
				    nullptr,                             /*replacementSpan*/
				    nullptr, /*commitCharacters*/
				    nullptr, /*labelDetails*/
				    file, position,
				    false, /*isMemberCompletion*/
				    false, /*isSnippet*/
				    false, /*hasAction*/
				    false, /*preselect*/
				    "",    /*source*/
				    nullptr, /*autoImportEntryData*/
				    nullptr, /*additionalTextEdits*/
				    nullptr  /*detail*/);
				items.push_back(new CompletionItem{
				    .completionItem = lspItem,
				});
			}
		}
		current = current->parent;
	}
	return items;
}

// completions.go:5190
bool isCompletionListBlocker(Node* contextToken, Node* previousToken,
                             Node* location, SourceFile* file, int position,
                             checker::Checker* typeChecker) {
	return isInStringOrRegularExpressionOrTemplateLiteral(contextToken,
	                                                      position) ||
	    isSolelyIdentifierDefinitionLocation(contextToken, previousToken,
	                                         file, position, typeChecker) ||
	    isDotOfNumericLiteral(contextToken, file) ||
	    isInJsxText(contextToken, location) ||
	    isBigIntLiteral(contextToken);
}

// completions.go:5204
bool isInStringOrRegularExpressionOrTemplateLiteral(Node* contextToken,
                                                    int position) {
	// To be "in" one of these literals, the position has to be:
	//   1. entirely within the token text.
	//   2. at the end position of an unterminated token.
	//   3. at the end of a regular expression (due to trailing flags like
	// '/foo/g').
	return (isRegularExpressionLiteral(contextToken) ||
	        isStringTextContainingNode(contextToken)) &&
	           contextToken->loc.contains(position) ||
	       (position == contextToken->end() &&
	        (isUnterminatedLiteral(contextToken) ||
	         isRegularExpressionLiteral(contextToken)));
}

// completions.go:5215 — true if we are certain that the currently edited
// location must define a new location; false otherwise.
bool isSolelyIdentifierDefinitionLocation(
    Node* contextToken, Node* previousToken, SourceFile* file, int position,
    checker::Checker* typeChecker) {
	Node* parent = contextToken->parent;
	Kind containingNodeKind = parent->kind;
	switch (contextToken->kind) {
	case Kind::CommaToken:
		return containingNodeKind == Kind::VariableDeclaration ||
		    isVariableDeclarationListButNotTypeArgument(contextToken, file,
		                                                typeChecker) ||
		    containingNodeKind == Kind::VariableStatement ||
		    containingNodeKind == Kind::EnumDeclaration || // enum a { foo, |
		    isFunctionLikeButNotConstructor(containingNodeKind) ||
		    containingNodeKind == Kind::InterfaceDeclaration || // interface A<T, |
		    containingNodeKind == Kind::ArrayBindingPattern || // var [x, y|
		    containingNodeKind == Kind::TypeAliasDeclaration || // type Map, K, |
		    // class A<T, |
		    // var C = class D<T, |
		    (isClassLike(parent) && parent->typeParameterList() != nullptr &&
		     parent->typeParameterList()->end() >= contextToken->pos());
	case Kind::DotToken:
		return containingNodeKind == Kind::ArrayBindingPattern; // var [.|
	case Kind::ColonToken:
		return containingNodeKind == Kind::BindingElement; // var {x :html|
	case Kind::OpenBracketToken:
		return containingNodeKind == Kind::ArrayBindingPattern; // var [x|
	case Kind::OpenParenToken:
		return containingNodeKind == Kind::CatchClause ||
		    isFunctionLikeButNotConstructor(containingNodeKind);
	case Kind::OpenBraceToken:
		return containingNodeKind == Kind::EnumDeclaration; // enum a { |
	case Kind::LessThanToken:
		return containingNodeKind == Kind::ClassDeclaration || // class A< |
		    containingNodeKind == Kind::ClassExpression || // var C = class D< |
		    containingNodeKind == Kind::InterfaceDeclaration || // interface A< |
		    containingNodeKind == Kind::TypeAliasDeclaration || // type List< |
		    isFunctionLikeKind(containingNodeKind);
	case Kind::StaticKeyword:
		return containingNodeKind == Kind::PropertyDeclaration &&
		    !isClassLike(parent->parent);
	case Kind::DotDotDotToken:
		return containingNodeKind == Kind::Parameter ||
		    (parent->parent != nullptr &&
		     parent->parent->kind == Kind::ArrayBindingPattern); // var [...z|
	case Kind::PublicKeyword:
	case Kind::PrivateKeyword:
	case Kind::ProtectedKeyword:
		return containingNodeKind == Kind::Parameter &&
		    !isConstructorDeclaration(parent->parent);
	case Kind::AsKeyword:
		return containingNodeKind == Kind::ImportSpecifier ||
		    containingNodeKind == Kind::ExportSpecifier ||
		    containingNodeKind == Kind::NamespaceImport;
	case Kind::GetKeyword:
	case Kind::SetKeyword:
		return !isFromObjectTypeDeclaration(contextToken);
	case Kind::Identifier:
		if ((containingNodeKind == Kind::ImportSpecifier ||
		     containingNodeKind == Kind::ExportSpecifier) &&
		    contextToken == parent->name() &&
		    contextToken->text() == "type") {
			// import { type | }
			return false;
		}
		{
			Node* ancestorVariableDeclaration =
			    findAncestor(parent, isVariableDeclaration);
			if (ancestorVariableDeclaration != nullptr &&
			    getLineEndOfPosition(file, contextToken->end()) <
			        position) {
				// let a
				// |
				return false;
			}
		}
		break;
	case Kind::ClassKeyword:
	case Kind::EnumKeyword:
	case Kind::InterfaceKeyword:
	case Kind::FunctionKeyword:
	case Kind::VarKeyword:
	case Kind::ImportKeyword:
	case Kind::LetKeyword:
	case Kind::ConstKeyword:
	case Kind::InferKeyword:
		return true;
	case Kind::TypeKeyword:
		// import { type foo| }
		return containingNodeKind != Kind::ImportSpecifier;
	case Kind::AsteriskToken:
		return isFunctionLike(parent) && !isMethodDeclaration(parent);
	default:
		break;
	}

	Kind tokenKind = keywordForNode(contextToken);
	// If the previous token is keyword corresponding to class member
	// completion keyword there will be completion available here
	if (isClassMemberCompletionKeyword(tokenKind) &&
	    isFromObjectTypeDeclaration(contextToken)) {
		return false;
	}

	if (isConstructorParameterCompletion(contextToken)) {
		// constructor parameter completion is available only if
		// - its modifier of the constructor parameter or
		// - its name of the parameter and not being edited
		// eg. constructor(a |<- this shouldnt show completion
		if (!isIdentifier(contextToken) ||
		    isParameterPropertyModifier(tokenKind) ||
		    isCurrentlyEditingNode(contextToken, file, position)) {
			return false;
		}
	}

	// Previous token may have been a keyword that was converted to an
	// identifier.
	switch (keywordForNode(contextToken)) {
	case Kind::AbstractKeyword:
	case Kind::ClassKeyword:
	case Kind::DeclareKeyword:
	case Kind::EnumKeyword:
	case Kind::FunctionKeyword:
	case Kind::InterfaceKeyword:
	case Kind::LetKeyword:
	case Kind::PrivateKeyword:
	case Kind::ProtectedKeyword:
	case Kind::PublicKeyword:
	case Kind::StaticKeyword:
	case Kind::VarKeyword:
		return true;
	case Kind::AsyncKeyword:
		return isPropertyDeclaration(contextToken->parent);
	default:
		break;
	}

	// If we are inside a class declaration, and `constructor` is totally
	// not present, but we request a completion manually at a whitespace...
	Node* ancestorClassLike = findAncestor(parent, isClassLike);
	if (ancestorClassLike != nullptr && contextToken == previousToken &&
	    isPreviousPropertyDeclarationTerminated(contextToken, file,
	                                            position)) {
		// Don't block completions.
		return false;
	}

	Node* ancestorPropertyDeclaration =
	    findAncestor(parent, isPropertyDeclaration);
	// If we are inside a class declaration and typing `constructor` after
	// property declaration...
	if (ancestorPropertyDeclaration != nullptr &&
	    contextToken != previousToken &&
	    isClassLike(previousToken->parent->parent) &&
	    // And the cursor is at the token...
	    position <= previousToken->end()) {
		// If we are sure that the previous property declaration is
		// terminated according to newline or semicolon...
		if (isPreviousPropertyDeclarationTerminated(
		        contextToken, file, previousToken->end())) {
			// Don't block completions.
			return false;
		} else if (contextToken->kind != Kind::EqualsToken &&
		           // Should not block: `class C { blah = c/**/ }`
		           // But should block:
		           // `class C { blah = somewhat c/**/ }` and
		           // `class C { blah: SomeType c/**/ }`
		           (isInitializedProperty(ancestorPropertyDeclaration) ||
		            ancestorPropertyDeclaration->type() != nullptr)) {
			return true;
		}
	}
	if (tokenKind == Kind::ConstKeyword) {
		return true;
	}
	return isDeclarationName(contextToken) &&
	    !isShorthandPropertyAssignment(parent) && !isJsxAttribute(parent) &&
	    // Don't block completions if we're in `class C /**/`,
	    // `interface I /**/` or `<T /**/>` , because we're *past* the end
	    // of the identifier and might want to complete `extends`. If
	    // `contextToken !== previousToken`, this is `class C ex/**/`,
	    // `interface I ex/**/` or `<T ex/**/>`.
	    !((isClassLike(parent) || isInterfaceDeclaration(parent) ||
	       isTypeParameterDeclaration(parent)) &&
	      (contextToken != previousToken ||
	       position > previousToken->end()));
}

// completions.go:5356
bool isVariableDeclarationListButNotTypeArgument(
    Node* node, SourceFile* file, checker::Checker* typeChecker) {
	return node->parent->kind == Kind::VariableDeclarationList &&
	    !isPossiblyTypeArgumentPosition(node, file, typeChecker);
}

// completions.go:5360
bool isFunctionLikeButNotConstructor(Kind kind) {
	return isFunctionLikeKind(kind) && kind != Kind::Constructor;
}

// completions.go:5364
bool isPreviousPropertyDeclarationTerminated(Node* contextToken,
                                             SourceFile* file,
                                             int position) {
	return contextToken->kind != Kind::EqualsToken &&
	    (contextToken->kind == Kind::SemicolonToken ||
	     getLineOfPosition(file, contextToken->end()) !=
	         getLineOfPosition(file, position));
}

// completions.go:5371
bool isDotOfNumericLiteral(Node* contextToken, SourceFile* file) {
	if (contextToken->kind == Kind::NumericLiteral) {
		std::string_view text =
		    std::string_view(file->Text())
		        .substr(contextToken->pos(),
		                contextToken->end() - contextToken->pos());
		auto [r, _] = decodeLastUtf8Rune(text);
		return r == '.';
	}

	return false;
}

// completions.go:5381
bool isInJsxText(Node* contextToken, Node* location) {
	if (contextToken->kind == Kind::JsxText) {
		return true;
	}

	if (contextToken->kind == Kind::GreaterThanToken &&
	    contextToken->parent != nullptr) {
		// <Component<string> /**/ />
		// <Component<string> /**/ ><Component>
		// - contextToken: GreaterThanToken (before cursor)
		// - location: JsxSelfClosingElement or JsxOpeningElement
		// - contextToken.parent === location
		if (location == contextToken->parent &&
		    isJsxOpeningLikeElement(location)) {
			return false;
		}

		if (contextToken->parent->kind == Kind::JsxOpeningElement) {
			// <div>/**/
			// - contextToken: GreaterThanToken (before cursor)
			// - location: JSXElement
			// - different parents (JSXOpeningElement, JSXElement)
			return location->parent->kind != Kind::JsxOpeningElement;
		}

		if (contextToken->parent->kind == Kind::JsxClosingElement ||
		    contextToken->parent->kind == Kind::JsxSelfClosingElement) {
			return contextToken->parent->parent != nullptr &&
			    contextToken->parent->parent->kind == Kind::JsxElement;
		}
	}

	return false;
}

// Canonical capabilities live on a gostd::Context value populated by
// `lsproto::withClientCapabilities`; the ContextPtr chain never carries
// them (the Go server layer that would is unported), so resolved defaults
// apply. ctx is kept in the signatures to mirror Go.
static std::shared_ptr<lsp::lsproto::ResolvedClientCapabilities>
resolvedCaps(const ContextPtr& ctx) {
	(void)ctx;
	return lsp::lsproto::getClientCapabilities(ctx);
}

// completions.go:5417
bool clientSupportsItemLabelDetails(const ContextPtr& ctx) {
	return resolvedCaps(ctx)
	    ->TextDocument.Completion.CompletionItem.LabelDetailsSupport;
}

// completions.go:5421
bool clientSupportsItemSnippet(const ContextPtr& ctx) {
	return resolvedCaps(ctx)
	    ->TextDocument.Completion.CompletionItem.SnippetSupport;
}

// completions.go:5425
bool clientSupportsItemCommitCharacters(const ContextPtr& ctx) {
	return resolvedCaps(ctx)
	    ->TextDocument.Completion.CompletionItem.CommitCharactersSupport;
}

// completions.go:5429
bool clientSupportsItemInsertReplace(const ContextPtr& ctx) {
	return resolvedCaps(ctx)
	    ->TextDocument.Completion.CompletionItem.InsertReplaceSupport;
}

namespace {
bool capsHasItemDefault(const ContextPtr& ctx, const std::string& key) {
	const auto& defaults = resolvedCaps(ctx)
	                           ->TextDocument.Completion.CompletionList
	                           .ItemDefaults;
	return defaults.has_value() &&
	    std::find(defaults->begin(), defaults->end(), key) !=
	        defaults->end();
}
} // namespace

// completions.go:5433
bool clientSupportsDefaultCommitCharacters(const ContextPtr& ctx) {
	return capsHasItemDefault(ctx, "commitCharacters");
}

// completions.go:5443
bool clientSupportsDefaultEditRange(const ContextPtr& ctx) {
	return capsHasItemDefault(ctx, "editRange");
}

// completions.go:5452
argumentInfoForCompletions* getArgumentInfoForCompletions(
    Node* node, int position, SourceFile* file,
    checker::Checker* typeChecker) {
	argumentListInfo* info = getImmediatelyContainingArgumentInfo(
	    node, position, file, typeChecker);
	if (info == nullptr || info->isTypeParameterList ||
	    info->invocation_ == nullptr ||
	    info->invocation_->callInvocation == nullptr) {
		return nullptr;
	}
	return new argumentInfoForCompletions{
	    .invocation = info->invocation_->callInvocation->node,
	    .argumentIndex = info->argumentIndex,
	    .argumentCount = info->argumentCount,
	};
}

// completions.go:5490
std::pair<lsproto::CompletionItem*, gostd::Error>
LanguageService::ResolveCompletionItem(const ContextPtr& ctx,
                                       lsproto::CompletionItem* item,
                                       lsproto::CompletionItemData* data) {
	if (data == nullptr) {
		return {nullptr,
		        gostd::errorf("%s", {"completion item data is nil"})};
	}

	auto [program, file] = tryGetProgramAndFile(data->FileName);
	if (file == nullptr) {
		return {nullptr, gostd::errorf("%s %s",
		                               {"file not found:",
		                                data->FileName})};
	}
	file = sourceFileForSupplementalFileIndex(file,
	                                          data->SupplementalFileIndex);
	if (file == nullptr) {
		return {nullptr,
		        gostd::errorf(
		            "%s %d",
		            {"supplemental source file index not found:",
		             int(*data->SupplementalFileIndex)})};
	}

	auto [checker, done] =
	    program->GetTypeCheckerForFile(ctx, file);
	DoneGuard guard{done};
	return {getCompletionItemDetails(ctx, program, checker,
	                                 int(data->Position), file, item,
	                                 data),
	        nullptr};
}

// completions.go:5513
lsproto::MarkupKind getCompletionDocumentationFormat(
    const ContextPtr& ctx) {
	return lsproto::PreferredMarkupKind(
	    resolvedCaps(ctx)
	        ->TextDocument.Completion.CompletionItem.DocumentationFormat);
}

// completions.go:5517
lsproto::CompletionItem* LanguageService::getCompletionItemDetails(
    const ContextPtr& ctx, compiler::SimpleProgram* program,
    checker::Checker* checker, int position, SourceFile* file,
    lsproto::CompletionItem* item, lsproto::CompletionItemData* data) {
	lsproto::MarkupKind docFormat = getCompletionDocumentationFormat(ctx);
	auto relevantTokens_ = getRelevantTokens(position, file);
	Node* contextToken = relevantTokens_.first;
	Node* previousToken = relevantTokens_.second;
	if (IsInString(file, position, previousToken)) {
		return getStringLiteralCompletionDetails(
		    ctx, checker, item, data->Name, file, position, contextToken,
		    docFormat);
	}

	if (data->AutoImport != nullptr) {
		if (data->IsImportStatementCompletion) {
			return item;
		}
		// Auto-imports in content-mapped files are evaluated eagerly so
		// edits outside of verbatim spans can cause the completion item
		// to be filtered out entirely. Only real files take this code
		// path, so the final Edits() is guaranteed ok.
		autoimport::Fix fix{
		    .AutoImportFix = data->AutoImport.get(),
		};
		auto [edits, description, ok] = fix.Edits(
		    gostd::contextBackground(), file, program->Options(), FormatOptions(), converters,
		    UserPreferences());
		item->AdditionalTextEdits =
		    std::make_shared<lsproto::Slice<std::shared_ptr<lsproto::TextEdit>>>(
		        edits);
		item->Detail = description;
		return item;
	}

	// Compute all the completion symbols again.
	detailsData symbolCompletion = getSymbolCompletionFromItemData(
	    ctx, checker, file, position, data);
	lsutil::UserPreferences preferences = UserPreferences();

	if (symbolCompletion.request != nullptr) {
		completionData request = *symbolCompletion.request;
		if (std::holds_alternative<completionDataJSDocTagName*>(
		        request)) {
			return createSimpleDetails(item, data->Name, docFormat);
		}
		if (std::holds_alternative<completionDataJSDocTag*>(request)) {
			return createSimpleDetails(item, data->Name, docFormat);
		}
		if (std::holds_alternative<completionDataJSDocParameterName*>(
		        request)) {
			return createSimpleDetails(item, data->Name, docFormat);
		}
		if (auto* kw =
		        std::get_if<completionDataKeyword*>(&request)) {
			if (someList((*kw)->keywordCompletions,
			             [&](CompletionItem* c) {
				             return c->completionItem->Label ==
				                 data->Name;
			             })) {
				return createSimpleDetails(item, data->Name,
				                           docFormat);
			}
			return item;
		}
		TSC_UNREACHABLE(
		    "Unexpected completion data type in "
		    "getCompletionItemDetails");
	} else if (symbolCompletion.symbol != nullptr) {
		symbolDetails* details = symbolCompletion.symbol;
		return createCompletionDetailsForSymbol(
		    item, details->symbol, checker, details->location, position,
		    docFormat);
	} else if (symbolCompletion.literal != nullptr) {
		literalValue* literal = symbolCompletion.literal;
		return createSimpleDetails(
		    item,
		    completionNameForLiteral(file, preferences, *literal),
		    docFormat);
	} else if (symbolCompletion.cases) {
		return item;
	} else {
		// Didn't find a symbol with this name.  See if we can find a
		// keyword instead.
		if (someList(allKeywordCompletions(),
		             [&](lsproto::CompletionItem* c) {
			             return c->Label == data->Name;
		             })) {
			return createSimpleDetails(item, data->Name, docFormat);
		}
		return item;
	}
}

// completions.go:5637
detailsData LanguageService::getSymbolCompletionFromItemData(
    const ContextPtr& ctx, checker::Checker* ch, SourceFile* file,
    int position, lsproto::CompletionItemData* itemData) {
	if (itemData->Source ==
	    std::string(completionSourceSwitchCases)) {
		return detailsData{.cases = true};
	}

	auto [completion, err] = getCompletionData(
	    ctx, ch, file, position, UserPreferences(),
	    /*forItemResolve*/ true);
	if (err) {
		TSC_UNREACHABLE("getCompletionData failed in "
		                "getSymbolCompletionFromItemData");
	}

	if (std::holds_alternative<std::monostate>(completion)) {
		return detailsData{};
	}

	auto* dataPtr = std::get_if<completionDataData*>(&completion);
	if (dataPtr == nullptr) {
		return detailsData{.request = new completionData(completion)};
	}

	completionDataData* data = *dataPtr;

	lsutil::UserPreferences preferences = UserPreferences();
	literalValue literal;
	bool literalFound = false;
	for (const literalValue& l : data->literals) {
		if (completionNameForLiteral(file, preferences, l) ==
		    itemData->Name) {
			literal = l;
			literalFound = true;
			break;
		}
	}
	if (literalFound) {
		return detailsData{.literal = new literalValue(literal)};
	}

	// Find the symbol with the matching entry name.
	// We don't need to perform character checks here because we're only
	// comparing the name against 'entryName' (which is known to be good),
	// not building a new completion entry.
	for (size_t index = 0; index < data->symbols.size(); index++) {
		Symbol* symbol = data->symbols[index];
		auto originIt = data->symbolToOriginInfoMap.find(int(index));
		symbolOriginInfo* origin = originIt !=
		            data->symbolToOriginInfoMap.end()
		        ? originIt->second
		        : nullptr;
		auto [displayName, _] = getCompletionEntryDisplayNameForSymbol(
		    file, preferences, symbol, origin, data->completionKind,
		    data->isJsxIdentifierExpected);
		if (displayName == itemData->Name &&
		    ((itemData->Source ==
		          std::string(completionSourceClassMemberSnippet) &&
		      (symbol->flags & SymbolFlagsClassMember) != 0) ||
		     (itemData->Source ==
		          std::string(completionSourceObjectLiteralMethodSnippet) &&
		      (symbol->flags &
		       (SymbolFlagsProperty | SymbolFlagsMethod)) != 0) ||
		     getSourceFromOrigin(origin) == itemData->Source ||
		     itemData->Source ==
		         std::string(
		             completionSourceObjectLiteralMemberWithComma))) {
			return detailsData{
			    .symbol = new symbolDetails{
			        .symbol = symbol,
			        .location = data->location,
			        .origin = origin,
			        .previousToken = data->previousToken,
			        .contextToken = data->contextToken,
			        .jsxInitializer = data->jsxInitializer,
			        .isTypeOnlyLocation = data->isTypeOnlyLocation,
			    },
			};
		}
	}
	return detailsData{};
}

// completions.go:5697
lsproto::CompletionItem* createSimpleDetails(
    lsproto::CompletionItem* item, const std::string& name,
    lsproto::MarkupKind docFormat) {
	return createCompletionDetails(item, name, /*documentation*/ "",
	                               docFormat);
}

// completions.go:5704
lsproto::CompletionItem* createCompletionDetails(
    lsproto::CompletionItem* item, const std::string& detail,
    const std::string& documentation, lsproto::MarkupKind docFormat) {
	// !!! fill in additionalTextEdits from code actions
	if (!item->Detail.has_value() && !detail.empty()) {
		item->Detail = detail;
	}
	if (!documentation.empty()) {
		item->Documentation =
		    std::make_shared<lsproto::StringOrMarkupContent>(
		        lsproto::StringOrMarkupContent{
		            .MarkupContent = std::make_shared<lsproto::MarkupContent>(
		                lsproto::MarkupContent{
		                    .Kind = docFormat,
		                    .Value = documentation,
		                }),
		        });
	}
	return item;
}

// completions.go:5730
lsproto::CompletionItem* LanguageService::createCompletionDetailsForSymbol(
    lsproto::CompletionItem* item, Symbol* symbol,
    checker::Checker* checker, Node* location, int position,
    lsproto::MarkupKind docFormat) {
	auto [quickInfo, documentation, _a, _b] =
	    getQuickInfoAndDocumentationForSymbol(
	        checker, symbol, location, docFormat, nullptr,
	        /*vsCapability*/ false);
	return createCompletionDetails(item, quickInfo, documentation,
	                               docFormat);
}

// completions.go:5743
importStatementCompletionInfo
LanguageService::getImportStatementCompletionInfo(Node* contextToken,
                                                  SourceFile* sourceFile) {
	importStatementCompletionInfo result{};
	Node* candidate = nullptr;
	Node* parent = contextToken->parent;
	if (isImportEqualsDeclaration(parent)) {
		// import Foo |
		// import Foo f|
		Node* lastToken = lsutil::GetLastToken(parent, sourceFile);
		if (contextToken->kind == Kind::Identifier &&
		    lastToken != contextToken) {
			result.keywordCompletion = Kind::FromKeyword;
			result.isKeywordOnlyCompletion = true;
		} else {
			if (contextToken->kind != Kind::TypeKeyword) {
				result.keywordCompletion = Kind::TypeKeyword;
			}
			if (isModuleSpecifierMissingOrEmpty(
			        parent->as<ImportEqualsDeclaration>()
			            ->ModuleReference)) {
				candidate = parent;
			}
		}
	} else if (couldBeTypeOnlyImportSpecifier(parent, contextToken) &&
	           canCompleteFromNamedBindings(parent->parent)) {
		candidate = parent;
	} else if (isNamedImports(parent) || isNamespaceImport(parent)) {
		if (!parent->parent->isTypeOnly() &&
		    (contextToken->kind == Kind::OpenBraceToken ||
		     contextToken->kind == Kind::ImportKeyword ||
		     contextToken->kind == Kind::CommaToken)) {
			result.keywordCompletion = Kind::TypeKeyword;
		}
		if (canCompleteFromNamedBindings(parent)) {
			// At `import { ... } |` or `import * as Foo |`, the only
			// possible completion is `from`
			if (contextToken->kind == Kind::CloseBraceToken ||
			    contextToken->kind == Kind::Identifier) {
				result.isKeywordOnlyCompletion = true;
				result.keywordCompletion = Kind::FromKeyword;
			} else {
				candidate = parent->parent->parent;
			}
		}
	} else if ((isExportDeclaration(parent) &&
	            contextToken->kind == Kind::AsteriskToken) ||
	           (isNamedExports(parent) &&
	            contextToken->kind == Kind::CloseBraceToken)) {
		result.isKeywordOnlyCompletion = true;
		result.keywordCompletion = Kind::FromKeyword;
	} else if (contextToken->kind == Kind::ImportKeyword) {
		if (isSourceFile(parent)) {
			// A lone import keyword with nothing following it does not
			// parse as a statement at all
			result.keywordCompletion = Kind::TypeKeyword;
			candidate = contextToken;
		} else if (isImportDeclaration(parent)) {
			// `import s| from`
			result.keywordCompletion = Kind::TypeKeyword;
			if (isModuleSpecifierMissingOrEmpty(
			        parent->moduleSpecifier())) {
				candidate = parent;
			}
		}
	}

	if (candidate != nullptr) {
		result.isNewIdentifierLocation = true;
		result.replacementSpan =
		    getSingleLineReplacementSpanForImportCompletionNode(candidate);
		result.couldBeTypeOnlyImportSpecifier =
		    couldBeTypeOnlyImportSpecifier(candidate, contextToken);
		if (isImportDeclaration(candidate)) {
			if (Node* importClause = candidate->importClause();
			    importClause != nullptr) {
				result.isTopLevelTypeOnly = importClause->isTypeOnly();
			}
		} else if (candidate->kind == Kind::ImportEqualsDeclaration) {
			result.isTopLevelTypeOnly = candidate->isTypeOnly();
		}
	} else {
		result.isNewIdentifierLocation =
		    result.keywordCompletion == Kind::TypeKeyword;
	}
	return result;
}

// completions.go:5805
lsproto::Range*
LanguageService::getSingleLineReplacementSpanForImportCompletionNode(
    Node* node) {
	// node is ImportDeclaration | ImportEqualsDeclaration |
	// ImportSpecifier | JSDocImportTag | Token<SyntaxKind.ImportKeyword>
	if (Node* ancestor = findAncestor(
	        node, [](Node* n) {
		        return isImportDeclaration(n) ||
		            isImportEqualsDeclaration(n) || isJSDocImportTag(n);
	        });
	    ancestor != nullptr) {
		node = ancestor;
	}
	SourceFile* sourceFile = getSourceFileOfNode(node);
	// Use token position (excluding JSDoc/trivia) instead of node.Pos()
	// to avoid including JSDoc comments
	int tokenPos =
	    getTokenPosOfNode(node, sourceFile, /*includeJSDoc*/ false);
	if (printer::GetLinesBetweenPositions(sourceFile, tokenPos,
	                                      node->end()) == 0) {
		auto [lspRange, fidelity] =
		    createLspRangeFromNode(node, sourceFile);
		if (!fidelity.IsExact()) {
			return nullptr;
		}
		return new lsproto::Range(lspRange);
	}

	if (node->kind == Kind::ImportKeyword ||
	    node->kind == Kind::ImportSpecifier) {
		TSC_UNREACHABLE(
		    "ImportKeyword was necessarily on one line; ImportSpecifier "
		    "was necessarily parented in an ImportDeclaration");
	}

	// Guess which point in the import might actually be a later statement
	// parsed as part of the import during parser recovery - either in the
	// middle of named imports, or the module specifier.
	Node* potentialSplitPoint = nullptr;
	if (node->kind == Kind::ImportDeclaration ||
	    node->kind == Kind::JSDocImportTag) {
		Node* specifier = nullptr;
		if (Node* importClause = node->importClause();
		    importClause != nullptr) {
			specifier = getPotentiallyInvalidImportSpecifier(
			    importClause->as<ImportClause>()->NamedBindings);
		}

		if (specifier != nullptr) {
			potentialSplitPoint = specifier;
		} else {
			potentialSplitPoint = node->moduleSpecifier();
		}
	} else {
		potentialSplitPoint =
		    node->as<ImportEqualsDeclaration>()->ModuleReference;
	}

	TextRange withoutModuleSpecifier{
	    getTokenPosOfNode(lsutil::GetFirstToken(node, sourceFile),
	                      sourceFile, /*includeJSDoc*/ false),
	    potentialSplitPoint->pos()};
	// The module specifier/reference was previously found to be missing,
	// empty, or not a string literal - in this last case, it's likely
	// that statement on a following line was parsed as the module
	// specifier of a partially-typed import, e.g.
	//   import Foo|
	//   interface Blah {}
	// This appears to be a multiline-import, and editors can't replace
	// multiple lines. But if everything but the "module specifier" is on
	// one line, by this point we can assume that the "module specifier"
	// is actually just another statement, and return the single-line
	// range of the import excluding that probable statement.
	if (printer::GetLinesBetweenPositions(sourceFile,
	                                      withoutModuleSpecifier.pos(),
	                                      withoutModuleSpecifier.end()) ==
	    0) {
		auto [lspRange, fidelity] = createLspRangeFromBounds(
		    withoutModuleSpecifier.pos(), withoutModuleSpecifier.end(),
		    sourceFile);
		if (!fidelity.IsExact()) {
			return nullptr;
		}
		return new lsproto::Range(lspRange);
	}
	return nullptr;
}

// completions.go:5849
bool couldBeTypeOnlyImportSpecifier(Node* importSpecifier,
                                    Node* contextToken) {
	return isImportSpecifier(importSpecifier) &&
	    (importSpecifier->isTypeOnly() ||
	     (contextToken == importSpecifier->name() &&
	      isTypeKeywordTokenOrIdentifier(contextToken)));
}

// completions.go:5853
bool canCompleteFromNamedBindings(Node* namedBindings) {
	if (!isModuleSpecifierMissingOrEmpty(
	        namedBindings->parent->parent->moduleSpecifier()) ||
	    namedBindings->parent->name() != nullptr) {
		return false;
	}
	if (isNamedImports(namedBindings)) {
		// We can only complete on named imports if there are no other
		// named imports already, but parser recovery sometimes puts later
		// statements in the named imports list, so we try to only
		// consider the probably-valid ones.
		Node* invalidNamedImport =
		    getPotentiallyInvalidImportSpecifier(namedBindings);
		std::vector<Node*> elements = namedBindings->elements();
		int validImports = int(elements.size());
		if (invalidNamedImport != nullptr) {
			auto it = std::find(elements.begin(), elements.end(),
			                    invalidNamedImport);
			validImports = int(it - elements.begin());
		}

		return validImports < 2 && validImports > -1;
	}
	return true;
}

// completions.go:5911 — Tries to identify the first named import that is
// not really a named import, but rather just parser recovery for a
// situation like:
//
//	import { Foo|
//	interface Bar {}
//
// in which `Foo`, `interface`, and `Bar` are all parsed as import
// specifiers. The caller will also check if this token is on a separate
// line from the rest of the import.
Node* getPotentiallyInvalidImportSpecifier(Node* namedBindings) {
	if (namedBindings == nullptr ||
	    namedBindings->kind != Kind::NamedImports) {
		return nullptr;
	}
	return findIn(namedBindings->elements(), [&](Node* e) {
		return e->propertyName() == nullptr &&
		    lsutil::IsNonContextualKeyword(
		        stringToToken(e->name()->text())) &&
		    astnav::findPrecedingToken(
		        getSourceFileOfNode(namedBindings), e->name()->pos())
		            ->kind != Kind::CommaToken;
	});
}

// completions.go:5925
bool isModuleSpecifierMissingOrEmpty(Node* specifier) {
	if (nodeIsMissing(specifier)) {
		return true;
	}
	Node* node = specifier;
	if (isExternalModuleReference(node)) {
		node = node->expression();
	}
	if (!isStringLiteralLike(node)) {
		return true;
	}
	return node->text().empty();
}

// completions.go:5938
bool hasDocComment(SourceFile* file, int position) {
	Node* token = astnav::getTokenAtPosition(file, position);
	return findAncestor(token, isJSDoc) != nullptr;
}

// completions.go:5943 — Get the corresponding JSDocTag node if the
// position is in a JSDoc comment
Node* getJSDocTagAtPosition(Node* node, int position) {
	return findAncestorOrQuit(node, [position](Node* n) -> FindAncestorResult {
		if (isJSDocTag(n) && n->loc.containsInclusive(position)) {
			return FindAncestorResult::True;
		}
		if (isJSDoc(n)) {
			return FindAncestorResult::Quit;
		}
		return FindAncestorResult::False;
	});
}

// completions.go:5955
Node* tryGetTypeExpressionFromTag(Node* tag) {
	if (isTagWithTypeExpression(tag)) {
		Node* typeExpression = nullptr;
		if (isJSDocTemplateTag(tag)) {
			typeExpression = tag->as<JSDocTemplateTag>()->Constraint;
		} else {
			typeExpression = tag->typeExpression();
		}
		if (typeExpression != nullptr &&
		    typeExpression->kind == Kind::JSDocTypeExpression) {
			return typeExpression;
		}
	}
	if (isJSDocAugmentsTag(tag) || isJSDocImplementsTag(tag)) {
		return tag->className();
	}
	return nullptr;
}

// completions.go:5971
bool isTagWithTypeExpression(Node* tag) {
	switch (tag->kind) {
	case Kind::JSDocParameterTag:
	case Kind::JSDocPropertyTag:
	case Kind::JSDocReturnTag:
	case Kind::JSDocTypeTag:
	case Kind::JSDocTypedefTag:
	case Kind::JSDocThrowsTag:
	case Kind::JSDocSatisfiesTag:
		return true;
	case Kind::JSDocTemplateTag:
		return tag->as<JSDocTemplateTag>()->Constraint != nullptr;
	default:
		return false;
	}
}

// completions.go:5984
CompletionList* LanguageService::jsDocCompletionInfo(
    const ContextPtr& ctx, int position, SourceFile* file,
    std::vector<CompletionItem*> items) {
	std::vector<std::string> defaultCommitCharacters =
	    getDefaultCommitCharacters(/*isNewIdentifierLocation*/ false);
	lsproto::CompletionItemDefaults* itemDefaults =
	    setItemDefaults(ctx, position, file, items,
	                    &defaultCommitCharacters,
	                    /*optionalReplacementSpan*/ nullptr);
	return new CompletionList{
	    .IsIncomplete = false,
	    .ItemDefaults = itemDefaults,
	    .Items = items,
	};
}

namespace {
// completions.go:6001
const std::vector<std::string> jsDocTagNames = {
    "abstract",     "access",         "alias",      "argument",
    "async",        "augments",       "author",     "borrows",
    "callback",     "class",          "classdesc",  "constant",
    "constructor",  "constructs",     "copyright",  "default",
    "deprecated",   "description",    "emits",      "enum",
    "event",        "example",        "exports",    "extends",
    "external",     "field",          "file",       "fileoverview",
    "fires",        "function",       "generator",  "global",
    "hideconstructor", "host",        "ignore",     "implements",
    "import",       "inheritdoc",     "inner",      "instance",
    "interface",    "kind",           "lends",      "license",
    "link",         "linkcode",       "linkplain",  "listens",
    "member",       "memberof",       "method",     "mixes",
    "module",       "name",           "namespace",  "overload",
    "override",     "package",        "param",      "private",
    "prop",         "property",       "protected",  "public",
    "readonly",     "requires",       "returns",    "satisfies",
    "see",          "since",          "static",     "summary",
    "template",     "this",           "throws",     "todo",
    "tutorial",     "type",           "typedef",    "var",
    "variation",    "version",        "virtual",    "yields",
};

// completions.go:6093
const std::vector<lsproto::CompletionItem*>& jsDocTagNameCompletionItems() {
	static const auto* items =
	    []() -> std::vector<lsproto::CompletionItem*>* {
		    auto* r = new std::vector<lsproto::CompletionItem*>();
		    r->reserve(jsDocTagNames.size());
		    for (const std::string& tagName : jsDocTagNames) {
			    r->push_back(new lsproto::CompletionItem{
			        .Label = tagName,
			        .Kind = std::make_shared<lsproto::CompletionItemKind>(
			            lsproto::CompletionItemKindKeyword),
			        .SortText = std::string(SortTextLocationPriority),
			    });
		    }
		    return r;
	    }();
	return *items;
}

// completions.go:6106
const std::vector<lsproto::CompletionItem*>& jsDocTagCompletionItems() {
	static const auto* items =
	    []() -> std::vector<lsproto::CompletionItem*>* {
		    auto* r = new std::vector<lsproto::CompletionItem*>();
		    r->reserve(jsDocTagNames.size());
		    for (const std::string& tagName : jsDocTagNames) {
			    r->push_back(new lsproto::CompletionItem{
			        .Label = "@" + tagName,
			        .Kind = std::make_shared<lsproto::CompletionItemKind>(
			            lsproto::CompletionItemKindKeyword),
			        .SortText = std::string(SortTextLocationPriority),
			    });
		    }
		    return r;
	    }();
	return *items;
}

// printer.cpp:43 getNewLineCharacter — printer's copy is file-local.
std::string getNewLineCharacter(NewLineKind newLine) {
	switch (newLine) {
	case NewLineKind::CarriageReturnLineFeed:
		return "\r\n";
	case NewLineKind::LineFeed:
		return "\n";
	case NewLineKind::None:
	default:
		return "";
	}
}

// strings.Join.
std::string joinStrings(const std::vector<std::string>& parts,
                        const std::string& sep) {
	std::string out;
	for (size_t i = 0; i < parts.size(); i++) {
		if (i != 0) {
			out += sep;
		}
		out += parts[i];
	}
	return out;
}

// core.MapIndex.
template <typename T, typename F>
std::vector<std::string> mapIndexToStrings(const std::vector<T>& list,
                                           F f) {
	std::vector<std::string> result;
	result.reserve(list.size());
	for (size_t i = 0; i < list.size(); i++) {
		result.push_back(f(list[i], int(i)));
	}
	return result;
}
} // namespace

// completions.go:6178
std::vector<CompletionItem*> getJSDocTagNameCompletions() {
	return cloneItems(jsDocTagNameCompletionItems());
}

// completions.go:6182
std::vector<CompletionItem*> getJSDocTagCompletions() {
	return cloneItems(jsDocTagCompletionItems());
}

// completions.go:6186
std::vector<CompletionItem*> getJSDocParameterCompletions(
    const ContextPtr& ctx, SourceFile* file, int position,
    checker::Checker* typeChecker, const CompilerOptions* options,
    const lsutil::UserPreferences& preferences, bool tagNameOnly) {
	Node* currentToken = astnav::getTokenAtPosition(file, position);
	if (!isJSDocTag(currentToken) && !isJSDoc(currentToken)) {
		return {};
	}
	Node* jsDoc = nullptr;
	if (isJSDoc(currentToken)) {
		jsDoc = currentToken;
	} else {
		jsDoc = currentToken->parent;
	}
	if (!isJSDoc(jsDoc)) {
		return {};
	}
	Node* fun = jsDoc->parent;
	if (!isFunctionLike(fun)) {
		return {};
	}

	bool isJS = isSourceFileJS(file);
	// isSnippet := clientSupportsItemSnippet(clientOptions)
	bool isSnippet = false; // !!! need snippet printer
	int paramTagCount = 0;
	std::vector<Node*> tags;
	if (jsDoc->as<JSDoc>()->Tags != nullptr) {
		tags = jsDoc->as<JSDoc>()->Tags->nodes;
	}
	for (Node* tag : tags) {
		if (isJSDocParameterTag(tag) &&
		    astnav::getStartOfNode(tag, file,
		                           /*includeJSDoc*/ false) < position &&
		    isIdentifier(tag->name())) {
			paramTagCount++;
		}
	}
	int paramIndex = -1;
	return mapNonNil(fun->parameters(), [&](Node* param) -> CompletionItem* {
		paramIndex++;
		if (paramIndex < paramTagCount) {
			// This parameter is already annotated.
			return nullptr;
		}
		if (isIdentifier(param->name())) { // Named parameter
			int tabstopCounter = 1;
			std::string paramName = param->name()->text();
			std::string displayText = getJSDocParamAnnotation(
			    paramName, param->initializer(),
			    param->as<ParameterDeclaration>()->DotDotDotToken,
			    isJS,
			    /*isObject*/ false,
			    /*isSnippet*/ false, typeChecker, options,
			    preferences, &tabstopCounter);
			std::string snippetText;
			if (isSnippet) {
				snippetText = getJSDocParamAnnotation(
				    paramName, param->initializer(),
				    param->as<ParameterDeclaration>()
				        ->DotDotDotToken,
				    isJS,
				    /*isObject*/ false,
				    /*isSnippet*/ true, typeChecker, options,
				    preferences, &tabstopCounter);
			}
			if (tagNameOnly) { // Remove `@`
				displayText = displayText.substr(1);
				if (!snippetText.empty()) {
					snippetText = snippetText.substr(1);
				}
			}

			return new CompletionItem{
			    .completionItem = new lsproto::CompletionItem{
			        .Label = displayText,
			        .Kind = std::make_shared<lsproto::CompletionItemKind>(
			            lsproto::CompletionItemKindVariable),
			        .SortText = std::string(SortTextLocationPriority),
			        .InsertText = !snippetText.empty()
		            ? std::optional<std::string>(snippetText)
		            : std::nullopt,
			        .InsertTextFormat =
			            isSnippet
			                ? std::make_shared<lsproto::InsertTextFormat>(
			                      lsproto::InsertTextFormatSnippet)
			                : nullptr,
			    },
			};
		} else if (paramIndex == paramTagCount) {
			// Destructuring parameter; do it positionally
			std::string paramPath =
			    gostd::sprintf("param%d", {paramIndex});
			std::vector<std::string> displayTextResult =
			    generateJSDocParamTagsForDestructuring(
			        paramPath, param->name()->as<BindingPattern>(),
			        param->initializer(),
			        param->as<ParameterDeclaration>()
			            ->DotDotDotToken,
			        isJS,
			        /*isSnippet*/ false, typeChecker, options,
			        preferences);
			std::string snippetText;
			if (isSnippet) {
				std::vector<std::string> snippetTextResult =
				    generateJSDocParamTagsForDestructuring(
				        paramPath,
				        param->name()->as<BindingPattern>(),
				        param->initializer(),
				        param->as<ParameterDeclaration>()
				            ->DotDotDotToken,
				        isJS,
				        /*isSnippet*/ true, typeChecker, options,
				        preferences);
				snippetText = joinStrings(
				    snippetTextResult,
				    getNewLineCharacter(options->NewLine) + "* ");
			}
			std::string displayText = joinStrings(
			    displayTextResult,
			    getNewLineCharacter(options->NewLine) + "* ");
			if (tagNameOnly) { // Remove `@`
				if (displayText.starts_with("@")) {
					displayText = displayText.substr(1);
				}
				if (snippetText.starts_with("@")) {
					snippetText = snippetText.substr(1);
				}
			}
			return new CompletionItem{
			    .completionItem = new lsproto::CompletionItem{
			        .Label = displayText,
			        .Kind = std::make_shared<lsproto::CompletionItemKind>(
			            lsproto::CompletionItemKindVariable),
			        .SortText = std::string(SortTextLocationPriority),
			        .InsertText = !snippetText.empty()
		            ? std::optional<std::string>(snippetText)
		            : std::nullopt,
			        .InsertTextFormat =
			            isSnippet
			                ? std::make_shared<lsproto::InsertTextFormat>(
			                      lsproto::InsertTextFormatSnippet)
			                : nullptr,
			    },
			};
		}
		return nullptr;
	});
}

// completions.go:6348
std::string getJSDocParamAnnotation(
    const std::string& paramName, Node* initializer,
    Node* dotDotDotToken, bool isJS, bool isObject, bool isSnippet,
    checker::Checker* typeChecker, const CompilerOptions* options,
    const lsutil::UserPreferences& preferences, int* tabstopCounter) {
	if (isSnippet) {
		TSC_ASSERT(tabstopCounter != nullptr,
		           "tabstopCounter required for snippet");
	}
	std::string paramNameMut = paramName;
	if (initializer != nullptr) {
		paramNameMut =
		    getJSDocParamNameWithInitializer(paramNameMut, initializer);
	}
	if (isSnippet) {
		paramNameMut = escapeSnippetText(paramNameMut);
	}
	if (isJS) {
		std::string t = "*";
		if (isObject) {
			TSC_ASSERT(dotDotDotToken == nullptr,
			           "Cannot annotate a rest parameter with type "
			           "'object'.");
			t = "object";
		} else {
			if (initializer != nullptr) {
				checker::Type* inferredType =
				    typeChecker->GetTypeAtLocation(
				        initializer->parent);
				if ((inferredType->flags &
				     (checker::TypeFlagsAny |
				      checker::TypeFlagsVoid)) == 0) {
					SourceFile* file =
					    getSourceFileOfNode(initializer);
					lsutil::QuotePreference quotePreference =
					    lsutil::GetQuotePreference(file, preferences);
					nodebuilder::Flags builderFlags = ifElse(
					    quotePreference ==
					        lsutil::QuotePreferenceSingle,
					    nodebuilder::
					        FlagsUseSingleQuotesForStringLiteralType,
					    nodebuilder::FlagsNone);
					Node* typeNode = typeChecker->TypeToTypeNode(
					    inferredType,
					    findAncestor(initializer,
					                 isFunctionLike),
					    builderFlags,
					    /*idToSymbol*/ nullptr);
					if (typeNode != nullptr) {
						printer::EmitContext* emitContext =
						    printer::NewEmitContext();
						// !!! snippet p
						printer::Printer* p = printer::NewPrinter(
						    printer::PrinterOptions{
						        .RemoveComments = true,
						        // !!!
						        // Module: options.Module,
						        // ModuleResolution:
						        // options.ModuleResolution,
						        // Target: options.Target,
						    },
						    printer::PrintHandlers{},
						    emitContext);
						emitContext->setEmitFlags(
						    typeNode, printer::EFSingleLine);
						t = p->Emit(typeNode, file);
					}
				}
			}
			if (isSnippet && t == "*") {
				int tabstop = *tabstopCounter;
				(*tabstopCounter)++;
				t = gostd::sprintf("${%d:%s}", {tabstop, t});
			}
		}
		std::string dotDotDot =
		    (!isObject && dotDotDotToken != nullptr) ? "..." : "";
		std::string description;
		if (isSnippet) {
			int tabstop = *tabstopCounter;
			(*tabstopCounter)++;
			description = gostd::sprintf("${%d}", {tabstop});
		}
		return gostd::sprintf("@param {%s%s} %s %s",
		                      {dotDotDot, t, paramNameMut,
		                       description});
	} else {
		std::string description;
		if (isSnippet) {
			int tabstop = *tabstopCounter;
			(*tabstopCounter)++;
			description = gostd::sprintf("${%d}", {tabstop});
		}
		return gostd::sprintf("@param %s %s",
		                      {paramNameMut, description});
	}
}

// completions.go:6412
std::string getJSDocParamNameWithInitializer(const std::string& paramName,
                                             Node* initializer) {
	std::string initializerText = std::string(
	    trimSpace(getTextOfNode(initializer)));
	if (initializerText.find('\n') != std::string::npos ||
	    initializerText.size() > 80) {
		return gostd::sprintf("[%s]", {paramName});
	}
	return gostd::sprintf("[%s=%s]", {paramName, initializerText});
}

// completions.go:6420
std::vector<std::string> generateJSDocParamTagsForDestructuring(
    const std::string& path, BindingPattern* pattern,
    Node* initializer, Node* dotDotDotToken, bool isJS, bool isSnippet,
    checker::Checker* typeChecker, const CompilerOptions* options,
    const lsutil::UserPreferences& preferences) {
	int tabstopCounter = 1;
	if (!isJS) {
		return {getJSDocParamAnnotation(
		    path, initializer, dotDotDotToken, isJS,
		    /*isObject*/ false, isSnippet, typeChecker, options,
		    preferences, &tabstopCounter)};
	}
	return jsDocParamPatternWorker(path, pattern, initializer,
	                               dotDotDotToken, isJS, isSnippet,
	                               typeChecker, options, preferences,
	                               &tabstopCounter);
}

// completions.go:6453
std::vector<std::string> jsDocParamPatternWorker(
    const std::string& path, BindingPattern* pattern,
    Node* initializer, Node* dotDotDotToken, bool isJS, bool isSnippet,
    checker::Checker* typeChecker, const CompilerOptions* options,
    const lsutil::UserPreferences& preferences, int* counter) {
	if (isObjectBindingPattern(pattern) && dotDotDotToken == nullptr) {
		int childCounter = *counter;
		std::string rootParam = getJSDocParamAnnotation(
		    path, initializer, dotDotDotToken, isJS,
		    /*isObject*/ true, isSnippet, typeChecker, options,
		    preferences, &childCounter);
		std::vector<std::string> childTags;
		for (Node* element : pattern->elements()) {
			std::vector<std::string> elementTags =
			    jsDocParamElementWorker(
			        path, element->as<BindingElement>(), initializer,
			        dotDotDotToken, isJS, isSnippet, typeChecker,
			        options, preferences, &childCounter);
			if (elementTags.empty()) {
				childTags.clear();
				break;
			}
			childTags.insert(childTags.end(), elementTags.begin(),
			                 elementTags.end());
		}
		if (!childTags.empty()) {
			*counter = childCounter;
			std::vector<std::string> result{rootParam};
			result.insert(result.end(), childTags.begin(),
			              childTags.end());
			return result;
		}
	}
	return {getJSDocParamAnnotation(path, initializer, dotDotDotToken,
	                                isJS,
	                                /*isObject*/ false, isSnippet,
	                                typeChecker, options, preferences,
	                                counter)};
}

// completions.go:6510 — Assumes binding element is inside object binding
// pattern. We can't deeply annotate an array binding pattern.
std::vector<std::string> jsDocParamElementWorker(
    const std::string& path, BindingElement* element,
    Node* initializer, Node* dotDotDotToken, bool isJS, bool isSnippet,
    checker::Checker* typeChecker, const CompilerOptions* options,
    const lsutil::UserPreferences& preferences, int* counter) {
	if (isIdentifier(element->name)) { // `{ b }` or `{ b: newB }`
		std::string propertyName;
		if (element->propertyName() != nullptr) {
			tryGetTextOfPropertyName(element->propertyName(),
			                         propertyName);
		} else {
			propertyName = element->name->text();
		}
		if (propertyName.empty()) {
			return {};
		}
		std::string paramName =
		    gostd::sprintf("%s.%s", {path, propertyName});
		return {getJSDocParamAnnotation(
		    paramName, element->initializer(),
		    element->as<BindingElement>()->DotDotDotToken, isJS,
		    /*isObject*/ false, isSnippet, typeChecker, options,
		    preferences, counter)};
	} else if (element->propertyName() !=
	           nullptr) { // `{ b: {...} }` or `{ b: [...] }`
		std::string propertyName;
		tryGetTextOfPropertyName(element->propertyName(), propertyName);
		if (propertyName.empty()) {
			return {};
		}
		return jsDocParamPatternWorker(
		    gostd::sprintf("%s.%s", {path, propertyName}),
		    element->name->as<BindingPattern>(),
		    element->initializer(),
		    element->as<BindingElement>()->DotDotDotToken, isJS,
		    isSnippet, typeChecker, options, preferences, counter);
	}
	return {};
}

// completions.go:6560
std::vector<CompletionItem*> getJSDocParameterNameCompletions(Node* tag) {
	if (!isIdentifier(tag->name())) {
		return {};
	}
	std::string nameThusFar = tag->name()->text();
	Node* jsDoc = tag->parent;
	Node* fn = jsDoc->parent;
	if (!isFunctionLike(fn)) {
		return {};
	}

	std::vector<Node*> tags;
	if (jsDoc->as<JSDoc>()->Tags != nullptr) {
		tags = jsDoc->as<JSDoc>()->Tags->nodes;
	}

	return mapNonNil(fn->parameters(), [&](Node* param) -> CompletionItem* {
		if (!isIdentifier(param->name())) {
			return nullptr;
		}

		std::string name = param->name()->text();
		if (someList(tags,
		             [&](Node* t) {
			             return t != tag->asNode() &&
			                 isJSDocParameterTag(t) &&
			                 isIdentifier(t->name()) &&
			                 t->name()->text() == name;
		             }) ||
		    (!nameThusFar.empty() &&
		     !name.starts_with(nameThusFar))) {
			return nullptr;
		}

		return new CompletionItem{
		    .completionItem = new lsproto::CompletionItem{
		        .Label = name,
		        .Kind = std::make_shared<lsproto::CompletionItemKind>(
		            lsproto::CompletionItemKindVariable),
		        .SortText = std::string(SortTextLocationPriority),
		    },
		};
	});
}

// completions.go:6607
std::pair<lsproto::CompletionItem*, gostd::Error>
LanguageService::getExhaustiveCaseSnippets(
    const ContextPtr& ctx, Node* caseBlock, SourceFile* file, int position,
    const CompilerOptions* options, compiler::SimpleProgram* program,
    checker::Checker* c) {
	std::vector<Node*> clauses =
	    caseBlock->as<CaseBlock>()->Clauses->nodes;
	checker::Type* switchType = c->GetTypeAtLocation(
	    caseBlock->asNode()->parent->expression());
	if (switchType != nullptr && switchType->IsUnion() &&
	    everyList(switchType->types(), isLiteral)) {
		// Collect constant values in existing clauses.
		caseClauseTracker* tracker = newCaseClauseTracker(c, clauses);
		ScriptTarget target = options->GetEmitScriptTarget();
		lsutil::QuotePreference quotePreference =
		    lsutil::GetQuotePreference(file, UserPreferences());
		// Tolerate a nil import adder in untitled files.
		autoimport::ImportAdder* importAdder = nullptr;
		if (!isDynamicFileName(file->FileName())) {
			auto [view, err] = getPreparedAutoImportView(file, c);
			if (err) {
				return {nullptr, err};
			}
			if (view != nullptr) {
				importAdder = autoimport::NewImportAdder(
				    gostd::contextBackground(), program, c, file, view, FormatOptions(),
				    converters, UserPreferences())
				    .release();
			}
		}

		std::vector<Node*> elements;
		NodeFactory factory{NodeFactoryHooks{}};
		for (checker::Type* t : switchType->types()) {
			// Enums
			if (t->IsEnumLiteral()) {
				TSC_ASSERT(t->symbol != nullptr,
				           "An enum member type should have a symbol");
				TSC_ASSERT(t->symbol->parent != nullptr,
				           "An enum member type should have a parent "
				           "symbol (the enum symbol)");
				// Filter existing enums by their values
				checker::LiteralValue enumValue;
				if (t->symbol->valueDeclaration != nullptr) {
					enumValue = c->GetConstantValue(
					    t->symbol->valueDeclaration);
				}
				if (!std::holds_alternative<std::monostate>(
				        enumValue)) {
					if (tracker->hasValue(enumValue)) {
						continue;
					}
					tracker->addValue(enumValue);
				}
				Node* typeNode = autoimport::TypeToAutoImportableTypeNode(
				    c, importAdder, t, caseBlock->asNode(), &factory);
				if (typeNode == nullptr) {
					return {nullptr, nullptr};
				}
				Node* expr = typeNodeToExpression(
				    typeNode, target, quotePreference, &factory);
				if (expr == nullptr) {
					return {nullptr, nullptr};
				}
				elements.push_back(expr);
			} else {
				checker::LiteralValue value =
				    t->AsLiteralType()->value;
				if (tracker->hasValue(value)) {
					continue;
				}
				if (const PseudoBigInt* v =
				        std::get_if<PseudoBigInt>(&value)) {
					Node* bigInt;
					if (v->negative) {
						PseudoBigInt vv = *v;
						vv.negative = false;
						bigInt = factory.newPrefixUnaryExpression(
						    Kind::MinusToken,
						    factory.newBigIntLiteral(
						        vv.string() + "n",
						        TokenFlagsNone));
					} else {
						bigInt = factory.newBigIntLiteral(
						    v->string() + "n", TokenFlagsNone);
					}
					elements.push_back(bigInt);
				} else if (const Number* v =
				               std::get_if<Number>(&value)) {
					Node* number;
					if (v->v < 0) {
						number = factory.newPrefixUnaryExpression(
						    Kind::MinusToken,
						    factory.newNumericLiteral(
						        v->abs().string(),
						        TokenFlagsNone));
					} else {
						number = factory.newNumericLiteral(
						    v->string(), TokenFlagsNone);
					}
					elements.push_back(number);
				} else if (const std::string* v =
				               std::get_if<std::string>(&value)) {
					Node* literal = factory.newStringLiteral(
					    *v,
					    quotePreference ==
					                lsutil::QuotePreferenceSingle
					        ? TokenFlagsSingleQuote
					        : TokenFlagsNone);
					elements.push_back(literal);
				}
			}
		}
		if (elements.empty()) {
			return {nullptr, nullptr};
		}

		std::vector<Node*> newClauses =
		    mapList(elements, [&](Node* element) {
			    return factory.newCaseOrDefaultClause(
			        Kind::CaseClause, element,
			        factory.newNodeList({}));
		    });
		std::string newLineChar = FormatOptions().NewLineCharacter;
		snippetPrinter* printer = createSnippetPrinter(
		    printer::PrinterOptions{
		        .RemoveComments = true,
		        .NewLine = getNewLineKind(newLineChar),
		    },
		    /*emitContext*/ nullptr);
		auto printNode = [&](Node* node) {
			return printer->printAndFormatNode(ctx, node, file);
		};
		std::string insertText = joinStrings(
		    mapIndexToStrings(
		        newClauses, [&](Node* clause, int i) {
			        if (clientSupportsItemSnippet(ctx)) {
				        return gostd::sprintf(
				            "%s$%d",
				            {printNode(clause), i + 1});
			        }
			        return printer->printUnescapedNode(clause);
		        }),
		    newLineChar);

		std::string firstClause =
		    printer->printUnescapedNode(newClauses[0]);
		std::string name = firstClause + " ...";

		std::shared_ptr<lsproto::Slice<std::shared_ptr<lsproto::TextEdit>>>
		    additionalTextEdits;
		if (importAdder != nullptr) {
			auto edits = importAdder->Edits();
			if (edits.has_value() && !edits->empty()) {
				additionalTextEdits = std::make_shared<
				    lsproto::Slice<std::shared_ptr<lsproto::TextEdit>>>(
				    std::move(*edits));
			}
		}

		return {new lsproto::CompletionItem{
		            .Label = name,
		            .Kind = std::make_shared<lsproto::CompletionItemKind>(
		                lsproto::CompletionItemKindSnippet),
		            .SortText =
		                std::string(SortTextGlobalsOrKeywords),
		            .InsertText = !insertText.empty()
		                ? std::optional<std::string>(insertText)
		                : std::nullopt,
		            .AdditionalTextEdits = additionalTextEdits,
		            .InsertTextFormat =
		                clientSupportsItemSnippet(ctx)
		                    ? std::make_shared<lsproto::InsertTextFormat>(
		                          lsproto::InsertTextFormatSnippet)
		                    : nullptr,
		            .Data = std::make_shared<lsproto::CompletionItemData>(
		                lsproto::CompletionItemData{
		                    .FileName = file->OriginalFileName(),
		                    .Position = int32_t(position),
		                    .SupplementalFileIndex =
		                        supplementalFileIndex(file),
		                    .Source =
		                        std::string(completionSourceSwitchCases),
		                    .Name = name,
		                }),
		        },
		        nullptr};
	}
	return {nullptr, nullptr};
}

// completions.go:6706
Node* typeNodeToExpression(Node* typeNode, ScriptTarget target,
                           lsutil::QuotePreference quotePreference,
                           NodeFactory* factory) {
	switch (typeNode->kind) {
	case Kind::TypeReference: {
		Node* typeName =
		    typeNode->as<TypeReferenceNode>()->TypeName;
		return entityNameToExpression(typeName, target, quotePreference,
		                              factory);
	}
	case Kind::IndexedAccessType: {
		Node* objectExpression = typeNodeToExpression(
		    typeNode->as<IndexedAccessTypeNode>()->ObjectType, target,
		    quotePreference, factory);
		Node* indexExpression = typeNodeToExpression(
		    typeNode->as<IndexedAccessTypeNode>()->IndexType, target,
		    quotePreference, factory);
		if (objectExpression != nullptr && indexExpression != nullptr) {
			return factory->newElementAccessExpression(
			    objectExpression, /*questionDotToken*/ nullptr,
			    indexExpression, NodeFlagsNone);
		}
		return nullptr;
	}
	case Kind::LiteralType: {
		Node* literal = typeNode->as<LiteralTypeNode>()->Literal;
		switch (literal->kind) {
		case Kind::StringLiteral:
			return factory->newStringLiteral(
			    literal->text(),
			    quotePreference == lsutil::QuotePreferenceSingle
			        ? TokenFlagsSingleQuote
			        : TokenFlagsNone);
		case Kind::NumericLiteral:
			return factory->newNumericLiteral(
			    literal->text(),
			    literal->as<NumericLiteral>()->TokenFlags);
		default:
			return nullptr;
		}
	}
	case Kind::ParenthesizedType: {
		Node* expr = typeNodeToExpression(
		    typeNode->as<ParenthesizedTypeNode>()->Type, target,
		    quotePreference, factory);
		if (expr == nullptr) {
			return nullptr;
		}
		if (isIdentifier(expr)) {
			return expr;
		}
		return factory->newParenthesizedExpression(expr);
	}
	case Kind::TypeQuery:
		return entityNameToExpression(
		    typeNode->as<TypeQueryNode>()->ExprName, target,
		    quotePreference, factory);
	case Kind::ImportType:
		TSC_ASSERT(
		    false,
		    "We should not get an import type after calling "
		    "'typeToAutoImportableTypeNode'.");
		return nullptr;
	default:
		break;
	}
	return nullptr;
}

// completions.go:6768
Node* entityNameToExpression(Node* entityName, ScriptTarget target,
                             lsutil::QuotePreference quotePreference,
                             NodeFactory* factory) {
	if (isIdentifier(entityName)) {
		return entityName;
	}
	return factory->newPropertyAccessExpression(
	    entityNameToExpression(
	        entityName->as<QualifiedName>()->Left, target, quotePreference,
	        factory),
	    /*questionDotToken*/ nullptr,
	    entityName->as<QualifiedName>()->Right, NodeFlagsNone);
}

// completions.go:6794 — Snippet-escaping version of `printer.printNode`.
std::string snippetPrinter::printNode(Node* node) {
	std::string unescaped = printUnescapedNode(node);
	if (!writer->escapes.empty()) {
		return ApplyBulkEdits(unescaped, writer->escapes);
	}
	return unescaped;
}

// completions.go:6802
std::string snippetPrinter::printUnescapedNode(Node* node) {
	writer->escapes.clear();
	writer->Clear();
	printer_->Write(node, /*sourceFile*/ nullptr, writer,
	                /*sourceMapGenerator*/ nullptr);
	return writer->String();
}

// completions.go:6809
std::string snippetPrinter::printAndFormatNode(const ContextPtr& ctx,
                                               Node* node,
                                               SourceFile* sourceFile) {
	// The Go code threads context.Context through to
	// format.GetFormatCodeSettingsFromContext; our FormatRequestContext
	// is a standalone carrier, so we start from a default one.
	return printAndFormatNodeWithSettings(
	    ctx, node, sourceFile,
	    format::GetFormatCodeSettingsFromContext(
	        format::FormatRequestContext{}));
}

// completions.go:6813
std::string snippetPrinter::printAndFormatNodeWithSettings(
    const ContextPtr& ctx, Node* node, SourceFile* sourceFile,
    lsutil::FormatCodeSettings formatOptions) {
	std::string text = printUnescapedNode(node);
	Node* nodeWithPos =
	    baseWriter->AssignPositionsToNode(node, factory);
	SourceFile* syntheticFile = printer::CreateSyntheticSourceFile(
	    factory, nodeWithPos, text, sourceFile->ParseOptions());
	format::FormatRequestContext formatCtx = format::WithFormatCodeSettings(
	    format::FormatRequestContext{}, formatOptions,
	    formatOptions.NewLineCharacter);
	std::vector<TextChange> changes = format::FormatNodeGivenIndentation(
	    formatCtx, nodeWithPos, syntheticFile,
	    sourceFile->LanguageVariant,
	    /*initialIndentation*/ 0,
	    /*delta*/ 0);

	std::vector<TextChange> allChanges = changes;
	if (!writer->escapes.empty()) {
		allChanges.insert(allChanges.end(), writer->escapes.begin(),
		                  writer->escapes.end());
		std::sort(allChanges.begin(), allChanges.end(),
		          [](const TextChange& a, const TextChange& b) {
			          return compareTextRanges(
			                 static_cast<const TextRange&>(a),
			                 static_cast<const TextRange&>(b)) < 0;
		          });
	}

	return ApplyBulkEdits(syntheticFile->Text(), allChanges);
}

// completions.go:6839
snippetPrinter* createSnippetPrinter(printer::PrinterOptions options,
                                     printer::EmitContext* emitContext) {
	if (emitContext == nullptr) {
		emitContext = printer::NewEmitContext();
	}
	printer::ChangeTrackerWriter* baseWriter =
	    printer::NewChangeTrackerWriter(
	        getNewLineCharacter(options.NewLine), -1);
	printer::Printer* printer = printer::NewPrinter(
	    options, baseWriter->GetPrintHandlers(), emitContext);
	snippetEmitTextWriter* writer = new snippetEmitTextWriter{};
	writer->base = baseWriter;
	return new snippetPrinter{
	    .baseWriter = baseWriter,
	    .emitContext = emitContext,
	    .printer_ = printer,
	    .writer = writer,
	    .factory = emitContext->factory.asNodeFactory(),
	};
}

// completions.go:6860 Write.
void snippetEmitTextWriter::Write(const std::string& s) {
	escapingWrite(s, [&]() { base->Write(s); });
}

// completions.go:6864 WriteComment.
void snippetEmitTextWriter::WriteComment(const std::string& text) {
	escapingWrite(
	    text, [&]() { base->WriteComment(text); });
}

// completions.go:6868 WriteStringLiteral.
void snippetEmitTextWriter::WriteStringLiteral(const std::string& text) {
	escapingWrite(text, [&]() {
		base->WriteStringLiteral(text);
	});
}

// completions.go:6872 WriteParameter.
void snippetEmitTextWriter::WriteParameter(const std::string& text) {
	escapingWrite(text, [&]() {
		base->WriteParameter(text);
	});
}

// completions.go:6876 WriteProperty.
void snippetEmitTextWriter::WriteProperty(const std::string& text) {
	escapingWrite(text, [&]() {
		base->WriteProperty(text);
	});
}

// completions.go:6880 WriteSymbol.
void snippetEmitTextWriter::WriteSymbol(const std::string& text,
                                        Symbol* symbol) {
	escapingWrite(text, [&]() {
		base->WriteSymbol(text, symbol);
	});
}

// completions.go:6886 — The formatter/scanner will have issues with
// snippet-escaped text, so instead of writing the escaped text directly to
// the writer, generate a set of changes that can be applied to the
// unescaped text to escape it post-formatting.
void snippetEmitTextWriter::escapingWrite(
    const std::string& s, const std::function<void()>& write) {
	std::string escaped = escapeSnippetText(s);
	if (escaped != s) {
		int start = GetTextPos();
		write();
		int end = GetTextPos();
		TextChange tc{TextRange{start, end}, escaped};
		escapes.push_back(tc);
	} else {
		write();
	}
}

} // namespace tsc::ls
