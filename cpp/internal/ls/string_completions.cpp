// Port of tsc/internal/ls/string_completions.go (2247 Go lines) — ls-coreA
// slice. See PORTING.md for conventions.
#include "internal/ls/ls.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "internal/astnav/tokens.h"
#include "internal/collections/collections.h"
#include "internal/compiler/program.h"
#include "internal/module/util.h"
#include "internal/stringutil/stringutil.h"
#include "internal/tspath/tspath.h"
#include "internal/tsoptions/tsoptions.h"

namespace tsc::ls {

namespace {

// ---------------------------------------------------------------------------
// File-local replicas of helpers owned by other packages/slices (per
// PORTING.md). Collapses when the owning slice lands a shared version.
// ---------------------------------------------------------------------------

// core.Map.
template <class R, class F>
auto mapList(R&& list, F f)
    -> std::vector<std::invoke_result_t<F, std::decay_t<std::ranges::range_value_t<R>>>> {
	std::vector<std::invoke_result_t<F, std::decay_t<std::ranges::range_value_t<R>>>> out;
	out.reserve(list.size());
	for (auto& v : list) {
		out.push_back(f(v));
	}
	return out;
}

// core.Filter.
template <class T, class F>
std::vector<T> filterList(const std::vector<T>& list, F f) {
	std::vector<T> out;
	for (const T& v : list) {
		if (f(v)) out.push_back(v);
	}
	return out;
}

// core.FlatMap.
template <class T, class F>
auto flatMap(const std::vector<T>& list, F f)
    -> std::vector<typename std::invoke_result_t<F, const T&>::value_type> {
	std::vector<typename std::invoke_result_t<F, const T&>::value_type> out;
	for (const T& v : list) {
		auto r = f(v);
		out.insert(out.end(), std::make_move_iterator(r.begin()),
		           std::make_move_iterator(r.end()));
	}
	return out;
}

// core.Flatten.
template <class T>
std::vector<T> flattenExt(const std::vector<std::vector<T>>& lists) {
	std::vector<T> out;
	for (const auto& l : lists) {
		out.insert(out.end(), l.begin(), l.end());
	}
	return out;
}

// slices.Contains.
template <class T, class U>
bool containsElem(const std::vector<T>& list, const U& v) {
	return std::find(list.begin(), list.end(), v) != list.end();
}

// maps.Values.
template <class K, class V>
std::vector<V> mapValues(const std::unordered_map<K, V>& m) {
	std::vector<V> out;
	out.reserve(m.size());
	for (const auto& [k, v] : m) {
		out.push_back(v);
	}
	return out;
}

// core.FindIn.
template <class T, class F>
T* findIn(const std::vector<T*>& list, F f) {
	for (T* v : list) {
		if (f(v)) return v;
	}
	return nullptr;
}

// checker_expressions_b.cpp — isCallLikeExpression (file-local there too).
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

// ast/utilities.go:3621 GetNonAugmentationDeclaration — canonical defn lives
// in ast/utilities.cpp (tsc::); unqualified calls resolve there.

// stringutil/util.go:222 — stringutil.StripQuotes.
std::string stripQuotes(const std::string& name) {
	if (name.size() < 2) {
		return name;
	}
	int w1 = 0, w2 = 0;
	char32_t firstChar = decodeUtf8Rune(name, &w1);
	char32_t lastChar = decodeLastUtf8Rune(name, &w2);
	if (firstChar == lastChar &&
	    (firstChar == '\'' || firstChar == '"' || firstChar == '`')) {
		return name.substr(1, name.size() - 2);
	}
	return name;
}

// strings.TrimLeft (unicode whitespace only — scanner's IsWhiteSpaceLike).
std::string trimLeftSpace(std::string_view s) {
	size_t i = 0;
	while (i < s.size()) {
		int w = 0;
		char32_t ch = decodeUtf8Rune(s.substr(i), &w);
		if (!isWhiteSpaceLike(ch)) break;
		i += w;
	}
	return std::string(s.substr(i));
}

} // namespace

// string_completions.go:57 getStringLiteralCompletions.
CompletionList* LanguageService::getStringLiteralCompletions(
    const ContextPtr& ctx, SourceFile* file, int position,
    Node* contextToken, checker::Checker* typeChecker,
    const CompilerOptions* compilerOptions, bool includeSymbols) {
	if (isInReferenceComment(file, position)) {
		pathCompletions* completion =
		    getTripleSlashReferenceCompletions(file, position, GetProgram(),
		                                       typeChecker);
		return convertPathCompletions(ctx, completion, file, position);
	}
	if (IsInString(file, position, contextToken)) {
		if (contextToken == nullptr || !isStringLiteralLike(contextToken)) {
			return nullptr;
		}
		stringLiteralCompletions* entries = getStringLiteralCompletionEntries(
		    ctx, file, contextToken, position, typeChecker);
		return convertStringLiteralCompletions(ctx, entries, file,
		                                       typeChecker, compilerOptions,
		                                       contextToken, position,
		                                       includeSymbols);
	}
	return nullptr;
}

// string_completions.go:95 convertStringLiteralCompletions.
CompletionList* LanguageService::convertStringLiteralCompletions(
    const ContextPtr& ctx, stringLiteralCompletions* completion,
    SourceFile* file, checker::Checker* typeChecker,
    const CompilerOptions* options, Node* contextToken, int position,
    bool includeSymbols) {
	if (completion == nullptr) {
		return nullptr;
	}

	lsproto::Range* optionalReplacementRange =
	    createRangeFromStringLiteralLikeContent(file, contextToken, position);
	if (completion->fromPaths != nullptr) {
		return convertPathCompletions(ctx, completion->fromPaths, file,
		                              position);
	}
	if (completion->fromProperties != nullptr) {
		completionsFromProperties* fromProperties = completion->fromProperties;
		auto* data = new completionDataData();
		data->symbols = fromProperties->symbols;
		data->completionKind = CompletionKindString;
		data->isNewIdentifierLocation = fromProperties->hasIndexSignature;
		data->location = file->asNode();
		data->contextToken = contextToken;
		auto&& [uniques, items, err] = getCompletionEntriesFromSymbols(
		    ctx, typeChecker, data,
		    /*replacementToken*/ contextToken, position, file, options,
		    /*includeSymbols*/ includeSymbols);
		(void)uniques;
		if (err != nullptr) {
			TSC_UNREACHABLE(err->Error().c_str());
		}
		std::vector<std::string> defaultCommitCharacters =
		    getDefaultCommitCharacters(fromProperties->hasIndexSignature);
		lsproto::CompletionItemDefaults* itemDefaults =
		    setItemDefaults(ctx, position, file, items,
		                    &defaultCommitCharacters,
		                    optionalReplacementRange);
		auto* list = new CompletionList();
		list->IsIncomplete = false;
		list->ItemDefaults = itemDefaults;
		list->Items = items;
		return list;
	}
	if (completion->fromTypes != nullptr) {
		completionsFromTypes* fromTypes = completion->fromTypes;
		printer::QuoteChar quoteChar;
		if (contextToken->kind == Kind::NoSubstitutionTemplateLiteral) {
			quoteChar = printer::QuoteCharBacktick;
		} else if (contextToken->text().starts_with('\'')) {
			quoteChar = printer::QuoteCharSingleQuote;
		} else {
			quoteChar = printer::QuoteCharDoubleQuote;
		}
		std::vector<CompletionItem*> items = mapList(
		    fromTypes->types, [&](checker::StringLiteralType* t) {
			    std::string name = printer::EscapeString(
			        std::get<std::string>(t->AsLiteralType()->value),
			        quoteChar);
			    lsproto::CompletionItem* lspItem = createLSPCompletionItem(
			        ctx, name,
			        /*insertText*/ "", /*filterText*/ "",
			        SortTextLocationPriority,
			        lsutil::ScriptElementKindString,
			        lsutil::ScriptElementKindModifierNone,
			        getReplacementRangeForContextToken(file, contextToken,
			                                           position),
			        /*commitCharacters*/ nullptr, /*labelDetails*/ nullptr,
			        file, position,
			        /*isMemberCompletion*/ false, /*isSnippet*/ false,
			        /*hasAction*/ false, /*preselect*/ false,
			        /*source*/ "", /*autoImportFix*/ nullptr,
			        /*additionalTextEdits*/ nullptr, /*detail*/ nullptr);
			    auto* item = new CompletionItem();
			    item->completionItem = lspItem;
			    return item;
		    });
		std::vector<std::string> defaultCommitCharacters =
		    getDefaultCommitCharacters(fromTypes->isNewIdentifier);
		lsproto::CompletionItemDefaults* itemDefaults = setItemDefaults(
		    ctx, position, file, items, &defaultCommitCharacters,
		    /*optionalReplacementSpan*/ nullptr);
		auto* list = new CompletionList();
		list->IsIncomplete = false;
		list->ItemDefaults = itemDefaults;
		list->Items = items;
		return list;
	}
	return nullptr;
}

// string_completions.go:206 convertPathCompletions.
CompletionList* LanguageService::convertPathCompletions(
    const ContextPtr& ctx, pathCompletions* completion, SourceFile* file,
    int position) {
	if (completion == nullptr) {
		return nullptr;
	}
	// The user may type in a path that doesn't yet exist, creating a "new
	// identifier" with respect to the collection of identifiers the server
	// is aware of.
	bool isNewIdentifierLocation = true;
	std::vector<std::string> defaultCommitCharacters =
	    getDefaultCommitCharacters(isNewIdentifierLocation);
	std::vector<CompletionItem*> items =
	    mapList(completion->entries, [&](pathCompletion* pathCompletion) {
		    std::string detail = pathCompletion->name;
		    if (!detail.ends_with(pathCompletion->extension)) {
			    detail += pathCompletion->extension;
		    }
		    lsproto::CompletionItem* lspItem = createLSPCompletionItem(
		        ctx, pathCompletion->name,
		        /*insertText*/ "", /*filterText*/ "",
		        SortTextLocationPriority, pathCompletion->kind,
		        kindModifiersFromExtension(pathCompletion->extension),
		        completion->replacementSpan,
		        /*commitCharacters*/ nullptr, /*labelDetails*/ nullptr, file,
		        position,
		        /*isMemberCompletion*/ false, /*isSnippet*/ false,
		        /*hasAction*/ false, /*preselect*/ false,
		        /*source*/ "", /*autoImportFix*/ nullptr,
		        /*additionalTextEdits*/ nullptr, &detail);
		    auto* item = new CompletionItem();
		    item->completionItem = lspItem;
		    return item;
	    });
	lsproto::CompletionItemDefaults* itemDefaults =
	    setItemDefaults(ctx, position, file, items, &defaultCommitCharacters,
	                    /*optionalReplacementSpan*/ nullptr);
	auto* list = new CompletionList();
	list->IsIncomplete = false;
	list->ItemDefaults = itemDefaults;
	list->Items = items;
	return list;
}

// string_completions.go:263 getStringLiteralCompletionEntries.
stringLiteralCompletions*
LanguageService::getStringLiteralCompletionEntries(
    const ContextPtr& ctx, SourceFile* file, Node* node, int position,
    checker::Checker* typeChecker) {
	Node* parent = walkUpParentheses(node->parent);
	switch (parent->kind) {
	case Kind::LiteralType: {
		Node* grandparent = walkUpParentheses(parent->parent);
		if (grandparent->kind == Kind::ImportType) {
			return getStringLiteralCompletionsFromModuleNames(
			    file, node, GetProgram(), typeChecker);
		}
		return fromUnionableLiteralType(grandparent, parent, position,
		                                typeChecker);
	}
	case Kind::PropertyAssignment: {
		if (isObjectLiteralExpression(parent->parent) &&
		    parent->name() == node) {
			// Get quoted name of properties of the object literal
			// expression
			// i.e. interface ConfigFiles {
			//          'jspm:dev': string
			//      }
			//      let files: ConfigFiles = {
			//          '/*completion position*/'
			//      }
			//
			//      function foo(c: ConfigFiles) {}
			//      foo({
			//          '/*completion position*/'
			//      });
			auto* result = new stringLiteralCompletions();
			result->fromProperties = stringLiteralCompletionsForObjectLiteral(
			    typeChecker, parent->parent);
			return result;
		}
		if (findAncestor(parent->parent, [](Node* n) {
			    return isCallLikeExpression(n);
		    }) != nullptr) {
			auto* uniques = new collections::Set<std::string>();
			std::vector<checker::StringLiteralType*> stringLiteralTypes =
			    getStringLiteralTypes(
			        typeChecker->GetContextualType(
			            node, checker::ContextFlagsNone),
			        uniques, typeChecker);
			std::vector<checker::StringLiteralType*> rest =
			    getStringLiteralTypes(
			        typeChecker->GetContextualType(
			            node, checker::ContextFlagsIgnoreNodeInferences),
			        uniques, typeChecker);
			stringLiteralTypes.insert(stringLiteralTypes.end(),
			                          rest.begin(), rest.end());
			return toStringLiteralCompletionsFromTypes(stringLiteralTypes);
		}
		auto* result = new stringLiteralCompletions();
		result->fromTypes = fromContextualType(
		    checker::ContextFlagsNone, node, typeChecker);
		return result;
	}
	case Kind::ElementAccessExpression: {
		Node* expression = parent->expression();
		Node* argumentExpression =
		    parent->as<ElementAccessExpression>()->ArgumentExpression;
		if (node == skipParentheses(argumentExpression)) {
			// Get all names of properties on the expression
			// i.e. interface A {
			//      'prop1': string
			// }
			// let a: A;
			// a['/*completion position*/']
			checker::Type* t = typeChecker->GetTypeAtLocation(expression);
			auto* result = new stringLiteralCompletions();
			result->fromProperties =
			    stringLiteralCompletionsFromProperties(t, typeChecker);
			return result;
		}
		return nullptr;
	}
	case Kind::CallExpression:
	case Kind::NewExpression:
	case Kind::JsxAttribute:
		if (!isRequireCallArgument(node) && !isImportCall(parent)) {
			Node* argumentNode;
			if (parent->kind == Kind::JsxAttribute) {
				argumentNode = parent->parent;
			} else {
				argumentNode = node;
			}
			argumentInfoForCompletions* argumentInfo =
			    getArgumentInfoForCompletions(argumentNode, position, file,
			                                  typeChecker);
			// Get string literal completions from specialized signatures
			// of the target
			// i.e. declare function f(a: 'A');
			// f("/*completion position*/")
			if (argumentInfo == nullptr) {
				return nullptr;
			}

			completionsFromTypes* result =
			    getStringLiteralCompletionsFromSignature(
			        argumentInfo->invocation, node, argumentInfo,
			        typeChecker);
			auto* completions = new stringLiteralCompletions();
			if (result != nullptr) {
				completions->fromTypes = result;
				return completions;
			}
			completions->fromTypes = fromContextualType(
			    checker::ContextFlagsNone, node, typeChecker);
			return completions;
		}
		[[fallthrough]]; // is `require("")` or `require(""` or `import("")`
	case Kind::ImportDeclaration:
	case Kind::ExportDeclaration:
	case Kind::ExternalModuleReference:
	case Kind::JSDocImportTag:
		// Get all known external module names or complete a path to a
		// module
		// i.e. import * as ns from "/*completion position*/";
		//      var y = import("/*completion position*/");
		//      import x = require("/*completion position*/");
		//      var y = require("/*completion position*/");
		//      export * from "/*completion position*/";
		return getStringLiteralCompletionsFromModuleNames(file, node,
		                                                GetProgram(),
		                                                typeChecker);
	case Kind::CaseClause: {
		caseClauseTracker* tracker = newCaseClauseTracker(
		    typeChecker, parent->parent->as<CaseBlock>()->Clauses->nodes);
		completionsFromTypes* contextualTypes = fromContextualType(
		    checker::ContextFlagsIgnoreNodeInferences, node, typeChecker);
		if (contextualTypes == nullptr) {
			return nullptr;
		}
		std::vector<checker::StringLiteralType*> literals = filterList(
		    contextualTypes->types, [&](checker::StringLiteralType* t) {
			    return !tracker->hasValue(t->AsLiteralType()->value);
		    });
		auto* fromTypes = new completionsFromTypes();
		fromTypes->types = literals;
		fromTypes->isNewIdentifier = false;
		auto* result = new stringLiteralCompletions();
		result->fromTypes = fromTypes;
		return result;
	}
	case Kind::ImportSpecifier:
	case Kind::ExportSpecifier: {
		// Complete string aliases in `import { "|" } from` and
		// `export { "|" } from`
		Node* specifier = parent;
		if (Node* propertyName = specifier->propertyName();
		    propertyName != nullptr && node != propertyName) {
			return nullptr; // Don't complete in `export { "..." as "|" }
			                // from`
		}
		Node* namedImportsOrExports = specifier->parent;
		Node* moduleSpecifier;
		if (namedImportsOrExports->kind == Kind::NamedImports) {
			moduleSpecifier = namedImportsOrExports->parent->parent;
		} else {
			moduleSpecifier = namedImportsOrExports->parent;
		}
		if (moduleSpecifier == nullptr) {
			return nullptr;
		}
		Symbol* moduleSpecifierSymbol =
		    typeChecker->GetSymbolAtLocation(moduleSpecifier);
		if (moduleSpecifierSymbol == nullptr) {
			return nullptr;
		}
		std::vector<Symbol*> exports_ =
		    typeChecker->GetExportsAndPropertiesOfModule(
		        moduleSpecifierSymbol);
		collections::Set<std::string> existing =
		    collections::NewSetFromItems<std::string>();
		for (const std::string& n : mapList(
		         namedImportsOrExports->elements(), [](Node* n) {
			         return std::string(n->propertyNameOrName()->text());
		         })) {
			existing.Add(n);
		}
		std::vector<Symbol*> uniques =
		    filterList(exports_, [&](Symbol* e) {
			    return e->name != InternalSymbolNameDefault &&
			           !existing.Has(e->name);
		    });
		auto* fromProperties = new completionsFromProperties();
		fromProperties->symbols = uniques;
		fromProperties->hasIndexSignature = false;
		auto* result = new stringLiteralCompletions();
		result->fromProperties = fromProperties;
		return result;
	}
	case Kind::BinaryExpression: {
		if (parent->as<BinaryExpression>()->OperatorToken->kind ==
		    Kind::InKeyword) {
			checker::Type* t = typeChecker->GetTypeAtLocation(
			    parent->as<BinaryExpression>()->Right);
			std::vector<Symbol*> properties =
			    getPropertiesForCompletion(t, typeChecker);
			auto* fromProperties = new completionsFromProperties();
			fromProperties->symbols = filterList(
			    properties, [](Symbol* s) {
				    return s->valueDeclaration == nullptr ||
				           !isPrivateIdentifierClassElementDeclaration(
				               s->valueDeclaration);
			    });
			fromProperties->hasIndexSignature = false;
			auto* result = new stringLiteralCompletions();
			result->fromProperties = fromProperties;
			return result;
		}
		auto* result = new stringLiteralCompletions();
		result->fromTypes = fromContextualType(
		    checker::ContextFlagsNone, node, typeChecker);
		return result;
	}
	default: {
		completionsFromTypes* result = fromContextualType(
		    checker::ContextFlagsIgnoreNodeInferences, node,
		    typeChecker);
		if (result != nullptr) {
			auto* completions = new stringLiteralCompletions();
			completions->fromTypes = result;
			return completions;
		}
		auto* completions = new stringLiteralCompletions();
		completions->fromTypes = fromContextualType(
		    checker::ContextFlagsNone, node, typeChecker);
		return completions;
	}
	}
}

// string_completions.go:440 fromContextualType.
completionsFromTypes* fromContextualType(checker::ContextFlags contextFlags,
                                         Node* node,
                                         checker::Checker* typeChecker) {
	// Get completion for string literal from string literal type
	// i.e. var x: "hi" | "hello" = "/*completion position*/"
	return toCompletionsFromTypes(
	    getStringLiteralTypes(
	        getContextualTypeFromParent(node, typeChecker, contextFlags),
	        nullptr, typeChecker));
}

// string_completions.go:446 toCompletionsFromTypes.
completionsFromTypes* toCompletionsFromTypes(
    const std::vector<checker::StringLiteralType*>& types) {
	if (types.empty()) {
		return nullptr;
	}
	auto* result = new completionsFromTypes();
	result->types = types;
	result->isNewIdentifier = false;
	return result;
}

// string_completions.go:455 toStringLiteralCompletionsFromTypes.
stringLiteralCompletions* toStringLiteralCompletionsFromTypes(
    const std::vector<checker::StringLiteralType*>& types) {
	completionsFromTypes* result = toCompletionsFromTypes(types);
	if (result == nullptr) {
		return nullptr;
	}
	auto* completions = new stringLiteralCompletions();
	completions->fromTypes = result;
	return completions;
}

// string_completions.go:465 fromUnionableLiteralType.
stringLiteralCompletions* fromUnionableLiteralType(
    Node* grandparent, Node* parent, int position,
    checker::Checker* typeChecker) {
	switch (grandparent->kind) {
	case Kind::CallExpression:
	case Kind::ExpressionWithTypeArguments:
	case Kind::JsxOpeningElement:
	case Kind::JsxSelfClosingElement:
	case Kind::NewExpression:
	case Kind::TaggedTemplateExpression:
	case Kind::TypeReference: {
		Node* typeArgument = findAncestor(
		    parent, [&](Node* n) { return n->parent == grandparent; });
		if (typeArgument != nullptr) {
			checker::Type* t =
			    typeChecker->GetTypeArgumentConstraint(typeArgument);
			auto* fromTypes = new completionsFromTypes();
			fromTypes->types =
			    getStringLiteralTypes(t, nullptr, typeChecker);
			fromTypes->isNewIdentifier = false;
			auto* result = new stringLiteralCompletions();
			result->fromTypes = fromTypes;
			return result;
		}
		return nullptr;
	}
	case Kind::IndexedAccessType: {
		// Get all apparent property names
		// i.e. interface Foo {
		//          foo: string;
		//          bar: string;
		//      }
		//      let x: Foo["/*completion position*/"]
		Node* indexType =
		    grandparent->as<IndexedAccessTypeNode>()->IndexType;
		Node* objectType =
		    grandparent->as<IndexedAccessTypeNode>()->ObjectType;
		if (!indexType->loc.containsInclusive(position)) {
			return nullptr;
		}
		checker::Type* t = typeChecker->GetTypeFromTypeNode(objectType);
		auto* result = new stringLiteralCompletions();
		result->fromProperties =
		    stringLiteralCompletionsFromProperties(t, typeChecker);
		return result;
	}
	case Kind::UnionType: {
		stringLiteralCompletions* result = fromUnionableLiteralType(
		    walkUpParentheses(grandparent->parent), parent, position,
		    typeChecker);
		if (result == nullptr) {
			return nullptr;
		}
		std::vector<std::string> alreadyUsedTypes =
		    getAlreadyUsedTypesInStringLiteralUnion(grandparent, parent);
		if (result->fromProperties != nullptr) {
			completionsFromProperties* fromProperties =
			    result->fromProperties;
			auto* filtered = new completionsFromProperties();
			filtered->symbols =
			    filterList(fromProperties->symbols, [&](Symbol* s) {
				    return !containsElem(alreadyUsedTypes,
				                         s->name);
			    });
			filtered->hasIndexSignature =
			    fromProperties->hasIndexSignature;
			auto* out = new stringLiteralCompletions();
			out->fromProperties = filtered;
			return out;
		}
		if (result->fromTypes != nullptr) {
			completionsFromTypes* fromTypes = result->fromTypes;
			auto* filtered = new completionsFromTypes();
			filtered->types = filterList(
			    fromTypes->types, [&](checker::StringLiteralType* t) {
				    return !containsElem(
				        alreadyUsedTypes,
				        std::get<std::string>(
				            t->AsLiteralType()->value));
			    });
			filtered->isNewIdentifier = false;
			auto* out = new stringLiteralCompletions();
			out->fromTypes = filtered;
			return out;
		}
		return nullptr;
	}
	case Kind::PropertySignature: {
		auto* fromTypes = new completionsFromTypes();
		fromTypes->types = getStringLiteralTypes(
		    getConstraintOfTypeArgumentProperty(grandparent, typeChecker),
		    nullptr, typeChecker);
		fromTypes->isNewIdentifier = false;
		auto* result = new stringLiteralCompletions();
		result->fromTypes = fromTypes;
		return result;
	}
	default:
		return nullptr;
	}
}

// string_completions.go:566 stringLiteralCompletionsForObjectLiteral.
completionsFromProperties* stringLiteralCompletionsForObjectLiteral(
    checker::Checker* typeChecker, Node* objectLiteralExpression) {
	checker::Type* contextualType = typeChecker->GetContextualType(
	    objectLiteralExpression, checker::ContextFlagsNone);
	if (contextualType == nullptr) {
		return nullptr;
	}

	checker::Type* completionsType = typeChecker->GetContextualType(
	    objectLiteralExpression, checker::ContextFlagsIgnoreNodeInferences);
	std::vector<Symbol*> symbols = getPropertiesForObjectExpression(
	    contextualType, completionsType, objectLiteralExpression,
	    typeChecker);

	auto* result = new completionsFromProperties();
	result->symbols = symbols;
	result->hasIndexSignature =
	    hasIndexSignature(contextualType, typeChecker);
	return result;
}

// string_completions.go:585 stringLiteralCompletionsFromProperties.
completionsFromProperties* stringLiteralCompletionsFromProperties(
    checker::Type* t, checker::Checker* typeChecker) {
	auto* result = new completionsFromProperties();
	result->symbols = filterList(
	    typeChecker->GetApparentProperties(t), [](Symbol* s) {
		    return !(s->valueDeclaration != nullptr &&
		             isPrivateIdentifierClassElementDeclaration(
		                 s->valueDeclaration));
	    });
	result->hasIndexSignature = hasIndexSignature(t, typeChecker);
	return result;
}

// string_completions.go:587 getStringLiteralCompletionsFromModuleNames.
stringLiteralCompletions*
LanguageService::getStringLiteralCompletionsFromModuleNames(
    SourceFile* file, Node* node, compiler::SimpleProgram* program,
    checker::Checker* typeChecker) {
	int textStart =
	    astnav::getStartOfNode(node, file, /*includeJSDoc*/ false) + 1;
	auto&& [replacementSpan, ok] = pathCompletionReplacementSpan(
	    file, getDirectoryFragmentRange(node->text(), textStart));
	if (!ok) {
		return nullptr;
	}
	std::vector<moduleCompletionNameAndKind> nameAndKinds =
	    getStringLiteralCompletionsFromModuleNamesWorker(file, node, program,
		                                             typeChecker);
	auto* fromPaths = new pathCompletions();
	fromPaths->entries = toPathCompletions(nameAndKinds);
	fromPaths->replacementSpan = replacementSpan;
	auto* result = new stringLiteralCompletions();
	result->fromPaths = fromPaths;
	return result;
}

// string_completions.go:612 toPathCompletions.
std::vector<pathCompletion*> toPathCompletions(
    const std::vector<moduleCompletionNameAndKind>& names) {
	return mapList(names, [](const moduleCompletionNameAndKind& nameAndKind) {
		auto* result = new pathCompletion();
		result->name = nameAndKind.name;
		result->kind = moduletToScriptElementKind(nameAndKind.kind);
		result->extension = nameAndKind.extension;
		return result;
	});
}

// string_completions.go:622 pathCompletionReplacementSpan.
std::pair<lsproto::Range*, bool>
LanguageService::pathCompletionReplacementSpan(SourceFile* file,
                                               TextRange* textRange) {
	if (textRange == nullptr) {
		return {nullptr, true};
	}
	auto&& [lspRange, fidelity] =
	    createLspRangeFromBounds(textRange->pos(), textRange->end(), file);
	if (!fidelity.IsExact()) {
		return {nullptr, false};
	}
	return {new lsproto::Range(lspRange), true};
}

// string_completions.go:633 moduletToScriptElementKind.
lsutil::ScriptElementKind moduletToScriptElementKind(
    moduleCompletionKind kind) {
	switch (kind) {
	case moduleCompletionKindDirectory:
		return lsutil::ScriptElementKindDirectory;
	case moduleCompletionKindFile:
		return lsutil::ScriptElementKindScriptElement;
	case moduleCompletionKindExternalModuleName:
		return lsutil::ScriptElementKindExternalModuleName;
	}
	TSC_UNREACHABLE("Unknown moduleCompletionKind");
}

// string_completions.go:645 isAnyDirectorySeparator.
bool isAnyDirectorySeparator(char32_t r) { return r == '/' || r == '\\'; }

// string_completions.go:650 getDirectoryFragmentRange — replace everything
// after the last directory separator that appears.
TextRange* getDirectoryFragmentRange(const std::string& text, int textStart) {
	size_t index = text.find_last_of("/\\");
	int offset = 0;
	if (index != std::string::npos) {
		offset = (int)index + 1;
	}
	int length = (int)text.size() - offset;
	if (length == 0) {
		return nullptr;
	}
	return new TextRange{textStart + offset, textStart + offset + length};
}

// string_completions.go:663 getStringLiteralCompletionsFromModuleNamesWorker.
std::vector<moduleCompletionNameAndKind>
LanguageService::getStringLiteralCompletionsFromModuleNamesWorker(
    SourceFile* file, Node* node, compiler::SimpleProgram* program,
    checker::Checker* typeChecker) {
	std::string literalValue = tspath::normalizeSlashes(node->text());
	ResolutionMode mode = ResolutionModeNone;
	if (isStringLiteralLike(node)) {
		mode = program->GetModeForUsageLocation(file, node);
	}

	tspath::Path scriptPath = file->Path();
	std::string scriptDirectory = tspath::getDirectoryPath(scriptPath);
	const CompilerOptions* options = program->Options();
	extensionOptions* extensionOptions =
	    getExtensionOptions(options, referenceKind::ModuleSpecifier, file,
	                        mode, typeChecker);

	if (isPathRelativeToScript(literalValue) ||
	    (options->Paths.empty() &&
	     (tspath::isRootedDiskPath(literalValue) ||
	      tspath::isUrl(literalValue)))) {
		return getCompletionEntriesForRelativeModules(
		    literalValue, scriptDirectory, program, scriptPath,
		    extensionOptions);
	}
	return getCompletionEntriesForNonRelativeModules(
	    literalValue, scriptDirectory, mode, program, typeChecker,
	    extensionOptions);
}

// Check all of the declared modules and those in node modules. Possible
// sources of modules:
//
//	Modules that are found by the type checker
//	Modules found via patterns from "paths" compiler option
//	Modules from node_modules (i.e. those listed in package.json)
//	    This includes all files that are found in node_modules/moduleName/
//	    with acceptable file extensions

// string_completions.go:707 getCompletionEntriesForNonRelativeModules.
std::vector<moduleCompletionNameAndKind>
LanguageService::getCompletionEntriesForNonRelativeModules(
    const std::string& fragment, const std::string& scriptPath,
    ResolutionMode mode, compiler::SimpleProgram* program,
    checker::Checker* typeChecker, extensionOptions* extensionOptions) {
	const CompilerOptions* compilerOptions = program->Options();
	const auto& paths = compilerOptions->Paths;

	auto* result = new moduleCompletionNameAndKindSet();
	ModuleResolutionKind moduleResolution =
	    compilerOptions->GetModuleResolutionKind();

	if (!paths.empty()) {
		std::string absolute = compilerOptions->GetPathsBasePath(
		    program->GetCurrentDirectory());
		addCompletionEntriesFromPaths(result, program, fragment, absolute,
		                              extensionOptions, &paths);
	}

	std::string fragmentDirectory = getFragmentDirectory(fragment);
	for (const std::string& ambientName : getAmbientModuleCompletions(
	         fragment, fragmentDirectory, typeChecker)) {
		moduleCompletionNameAndKind entry;
		entry.name = ambientName;
		entry.kind = moduleCompletionKindExternalModuleName;
		result->add(entry);
	}

	getCompletionEntriesFromTypings(program, scriptPath, fragmentDirectory,
	                                extensionOptions, result);

	if (moduleResolutionUsesNodeModules(moduleResolution)) {
		// If looking for a global package name, don't just include
		// everything in `node_modules` because that includes dependencies'
		// own dependencies.
		// (But do if we didn't find anything, e.g. 'package.json' missing.)
		bool foundGlobal = false;
		if (fragmentDirectory.empty()) {
			for (const std::string& moduleName :
			     enumerateNodeModulesVisibleToScript(scriptPath)) {
				moduleCompletionNameAndKind moduleResult;
				moduleResult.name = moduleName;
				moduleResult.kind =
				    moduleCompletionKindExternalModuleName;
				if (result->names.find(moduleResult.name) ==
				    result->names.end()) {
					foundGlobal = true;
					result->add(moduleResult);
				}
			}
		}
		if (!foundGlobal) {
			bool resolvePackageJsonExports =
			    compilerOptions->GetResolvePackageJsonExports();
			bool resolvePackageJsonImports =
			    compilerOptions->GetResolvePackageJsonImports();
			bool seenPackageScope = false;
			std::vector<std::string> conditions =
			    module::GetConditions(*compilerOptions, mode);

			// Returns true if the search should stop.
			std::function<bool(packagejson::ExportsOrImports*,
			                   const std::string&, const std::string&, bool,
			                   bool)>
			    exportsOrImportsLookup =
			        [&](packagejson::ExportsOrImports* lookupTable,
			            const std::string& fragment,
			            const std::string& baseDirectory, bool isExports,
			            bool isImports) -> bool {
				if (lookupTable == nullptr ||
				    lookupTable->type !=
				        packagejson::JSONValueType::Object) {
					return lookupTable != nullptr &&
					       lookupTable->type !=
					           packagejson::JSONValueType::
					               NotPresent;
				}
				const std::vector<std::string>& keys =
				    lookupTable->AsObject()->Keys();
				addCompletionEntriesFromPathsOrExportsOrImports(
				    result, program, isExports, isImports, fragment,
				    baseDirectory, extensionOptions, keys,
				    [&](const std::string& key)
				        -> std::vector<std::string> {
					    auto&& [keyValue, ok] =
					        lookupTable->AsObject()->Get(key);
					    if (!ok) {
						    return {};
					    }
					    std::string pattern =
					        getPatternFromFirstMatchingCondition(
					            keyValue, conditions);
					    if (pattern.empty()) {
						    return {};
					    }
					    if (key.ends_with('/') &&
					        pattern.ends_with('/')) {
						    return {pattern + "*"};
					    }
					    return {pattern};
				    },
				    [](const std::string& a, const std::string& b) {
					    return module::ComparePatternKeys(a, b);
				    });
				return true;
			};

			auto importsLookup = [&](const std::string& directory) {
				if (resolvePackageJsonImports && !seenPackageScope) {
					std::string packageFile = tspath::combinePaths(
					    directory, {"package.json"});
					std::shared_ptr<packagejson::InfoCacheEntry>
					    packageJsonInfo =
					        program->GetPackageJsonInfo(packageFile);
					if (packageJsonInfo != nullptr &&
					    packageJsonInfo->Exists()) {
						seenPackageScope = true;
						exportsOrImportsLookup(
						    &packageJsonInfo->Contents->Imports,
						    fragment, directory,
						    /*isExports*/ false,
						    /*isImports*/ true);
					}
				}
			};

			std::function<std::pair<std::monostate, bool>(
			    std::string_view)>
			    ancestorLookup = [&](std::string_view ancestor)
			    -> std::pair<std::monostate, bool> {
				std::string nodeModules = tspath::combinePaths(
				    ancestor, {"node_modules"});
				if (host->DirectoryExists(nodeModules)) {
					getCompletionEntriesForDirectoryFragment(
					    fragment, nodeModules, extensionOptions,
					    program,
					    /*moduleSpecifierIsRelative*/ false, "",
					    result);
				}
				importsLookup(std::string(ancestor));
				return {std::monostate{}, false};
			};

			if (!fragmentDirectory.empty() && resolvePackageJsonExports) {
				auto nodeModulesDirectoryOrImportsLookup =
				    ancestorLookup;
				ancestorLookup = [&](std::string_view ancestor)
				    -> std::pair<std::monostate, bool> {
					std::vector<std::string> components =
					    tspath::getPathComponents(fragment, "");
					components.erase(
					    components.begin()); // shift off empty root
					if (components.empty()) {
						nodeModulesDirectoryOrImportsLookup(ancestor);
						return {std::monostate{}, false};
					}
					std::string packagePath = components[0];
					components.erase(components.begin());
					if (packagePath.starts_with('@')) {
						if (components.empty()) {
							nodeModulesDirectoryOrImportsLookup(
							    ancestor);
							return {std::monostate{}, false};
						}
						std::string subName = components[0];
						components.erase(components.begin());
						packagePath = tspath::combinePaths(
						    packagePath, {subName});
					}
					if (resolvePackageJsonImports &&
					    packagePath.starts_with('#')) {
						importsLookup(std::string(ancestor));
						return {std::monostate{}, false};
					}
					std::string packageDirectory =
					    tspath::combinePaths(
					        ancestor,
					        {"node_modules", packagePath});
					std::string packageFile = tspath::combinePaths(
					    packageDirectory, {"package.json"});
					std::shared_ptr<packagejson::InfoCacheEntry>
					    packageJsonInfo =
					        program->GetPackageJsonInfo(packageFile);
					if (packageJsonInfo != nullptr &&
					    packageJsonInfo->Exists()) {
						std::string fragmentSubpath;
						for (size_t i = 0; i < components.size();
						     i++) {
							if (i) fragmentSubpath += '/';
							fragmentSubpath += components[i];
						}
						if (!components.empty() &&
						    tspath::hasTrailingDirectorySeparator(
						        fragment)) {
							fragmentSubpath += '/';
						}
						if (exportsOrImportsLookup(
						        &packageJsonInfo->Contents->Exports,
						        fragmentSubpath, packageDirectory,
						        /*isExports*/ true,
						        /*isImports*/ false)) {
							return {std::monostate{}, false};
						}
					}
					nodeModulesDirectoryOrImportsLookup(ancestor);
					return {std::monostate{}, false};
				};
			}

			std::string globalCacheLocation =
			    program->GetGlobalTypingsCacheLocation();
			tspath::forEachAncestorDirectoryStoppingAtGlobalCache<
			    std::monostate>(globalCacheLocation, scriptPath,
			                    ancestorLookup);
		}
	}

	return mapValues(result->names);
}

// string_completions.go:875 getFragmentDirectory.
std::string getFragmentDirectory(const std::string& fragment) {
	if (!containsSlash(fragment)) {
		return "";
	}
	if (tspath::hasTrailingDirectorySeparator(fragment)) {
		return fragment;
	}
	return tspath::getDirectoryPath(fragment);
}

// string_completions.go:885 getPatternFromFirstMatchingCondition.
std::string getPatternFromFirstMatchingCondition(
    packagejson::ExportsOrImports* target,
    const std::vector<std::string>& conditions) {
	if (target->type == packagejson::JSONValueType::String) {
		return target->AsString();
	}
	if (target->type == packagejson::JSONValueType::Object) {
		collections::OrderedMap<std::string, packagejson::ExportsOrImports>*
		    obj = target->AsObject();
		for (const std::string& condition : obj->Keys()) {
			if (condition == "default" ||
			    containsElem(conditions, condition) ||
			    (containsElem(conditions, std::string("types")) &&
			     module::IsApplicableVersionedTypesKey(condition))) {
				auto&& [pattern, ok] = obj->Get(condition);
				if (ok) {
					return getPatternFromFirstMatchingCondition(
					    pattern, conditions);
				}
			}
		}
	}
	return "";
}

// string_completions.go:904 getAmbientModuleCompletions.
std::vector<std::string> getAmbientModuleCompletions(
    const std::string& fragment, const std::string& fragmentDirectory,
    checker::Checker* typeChecker) {
	std::vector<Symbol*> ambientModules = typeChecker->GetAmbientModules();
	std::vector<std::string> nonRelativeModuleNames;
	for (Symbol* sym : ambientModules) {
		std::string moduleName = getAmbientModuleName(sym);
		if (moduleName.starts_with(fragment) &&
		    moduleName.find('*') == std::string::npos) {
			nonRelativeModuleNames.push_back(moduleName);
		}
	}

	if (!fragmentDirectory.empty()) {
		std::string moduleNameWithSeparator =
		    tspath::ensureTrailingDirectorySeparator(fragmentDirectory);
		for (std::string& moduleName : nonRelativeModuleNames) {
			if (moduleName.starts_with(moduleNameWithSeparator)) {
				moduleName =
				    moduleName.substr(moduleNameWithSeparator.size());
			}
		}
	}
	return nonRelativeModuleNames;
}

// string_completions.go:923 getAmbientModuleName.
std::string getAmbientModuleName(Symbol* symbol) {
	Node* declaration = getNonAugmentationDeclaration(symbol);
	if (declaration != nullptr &&
	    isModuleWithStringLiteralName(declaration)) {
		return std::string(declaration->name()->text());
	}
	return stripQuotes(symbol->name);
}

// string_completions.go:931 getCompletionEntriesFromTypings.
void LanguageService::getCompletionEntriesFromTypings(
    compiler::SimpleProgram* program, const std::string& scriptPath,
    const std::string& fragmentDirectory,
    extensionOptions* extensionOptions,
    moduleCompletionNameAndKindSet* result) {
	const CompilerOptions* options = program->Options();
	std::unordered_map<std::string, bool> seen;

	auto&& [typeRoots, _] =
	    options->GetEffectiveTypeRoots(program->GetCurrentDirectory());
	(void)_;

	for (const std::string& root : typeRoots) {
		getCompletionEntriesFromTypingsDirectories(
		    root, options, fragmentDirectory, extensionOptions, program,
		    &seen, result);
	}

	std::string globalCacheLocation =
	    program->GetGlobalTypingsCacheLocation();
	tspath::forEachAncestorDirectoryStoppingAtGlobalCache<std::monostate>(
	    globalCacheLocation, scriptPath,
	    [&](std::string_view directory) -> std::pair<std::monostate, bool> {
		    std::string typesDir = tspath::combinePaths(
		        directory, {"node_modules/@types"});
		    getCompletionEntriesFromTypingsDirectories(
		        typesDir, options, fragmentDirectory, extensionOptions,
		        program, &seen, result);
		    return {std::monostate{}, false};
	    });
}

// string_completions.go:955 getCompletionEntriesFromTypingsDirectories.
void LanguageService::getCompletionEntriesFromTypingsDirectories(
    const std::string& directory, const CompilerOptions* options,
    const std::string& fragmentDirectory,
    extensionOptions* extensionOptions, compiler::SimpleProgram* program,
    std::unordered_map<std::string, bool>* seen,
    moduleCompletionNameAndKindSet* result) {
	if (!host->DirectoryExists(directory)) {
		return;
	}

	for (const std::string& typeDirectoryName :
	     GetDirectories(directory)) {
		std::string packageName =
		    module::UnmangleScopedPackageName(typeDirectoryName);
		if (!options->Types.empty() &&
		    !containsElem(options->Types, packageName)) {
			continue;
		}

		if (fragmentDirectory.empty()) {
			if (!(*seen)[packageName]) {
				moduleCompletionNameAndKind entry;
				entry.name = packageName;
				entry.kind = moduleCompletionKindExternalModuleName;
				result->add(entry);
				(*seen)[packageName] = true;
			}
		} else {
			std::string baseDirectory = tspath::combinePaths(
			    directory, {typeDirectoryName});
			std::string* remainingFragment = tryRemoveDirectoryPrefix(
			    fragmentDirectory, packageName,
			    program->UseCaseSensitiveFileNames());
			if (remainingFragment != nullptr) {
				getCompletionEntriesForDirectoryFragment(
				    *remainingFragment, baseDirectory,
				    extensionOptions, program,
				    /*moduleSpecifierIsRelative*/ false, "",
				    result);
			}
		}
	}
}

// string_completions.go:1000 tryRemoveDirectoryPrefix.
std::string* tryRemoveDirectoryPrefix(const std::string& path,
                                      const std::string& prefix,
                                      bool useCaseSensitiveFileNames) {
	auto&& [withoutPrefix, ok] = tspath::trimFilePathPrefix(
	    path, prefix, useCaseSensitiveFileNames);
	if (!ok) {
		return nullptr;
	}
	if (withoutPrefix.starts_with('/') || withoutPrefix.starts_with('\\')) {
		withoutPrefix = withoutPrefix.substr(1);
	}
	return new std::string(std::move(withoutPrefix));
}

// string_completions.go:1011 enumerateNodeModulesVisibleToScript.
std::vector<std::string>
LanguageService::enumerateNodeModulesVisibleToScript(
    const std::string& scriptPath) {
	std::vector<std::string> result;
	std::string globalCacheLocation =
	    program->GetGlobalTypingsCacheLocation();

	tspath::forEachAncestorDirectoryStoppingAtGlobalCache<std::monostate>(
	    globalCacheLocation, scriptPath,
	    [&](std::string_view directory) -> std::pair<std::monostate, bool> {
		    std::string packageJsonPath = tspath::combinePaths(
		        directory, {"package.json"});
		    std::shared_ptr<packagejson::InfoCacheEntry> packageJsonInfo =
		        program->GetPackageJsonInfo(packageJsonPath);
		    if (packageJsonInfo != nullptr && packageJsonInfo->Exists() &&
		        packageJsonInfo->Contents != nullptr) {
			    packageJsonInfo->Contents->RangeDependencies(
			        [&](const std::string& name, const std::string& version,
			            const std::string& dependencyField) -> bool {
				        (void)version;
				        (void)dependencyField;
				        if (!name.starts_with("@types/")) {
					        result.push_back(name);
				        }
				        return true;
			        });
		    }
		    return {std::monostate{}, false};
	    });

	return result;
}

// string_completions.go:1032 getExtensionOptions.
extensionOptions* LanguageService::getExtensionOptions(
    const CompilerOptions* options, referenceKind refKind, SourceFile* file,
    ResolutionMode mode, checker::Checker* typeChecker) {
	std::vector<std::string> extensionsToSearch =
	    getSupportedExtensionsForModuleResolution(
	        options, GetProgram()->CommandLine()->ContentMapperExtensions(),
	        typeChecker);

	auto* result = new extensionOptions();
	result->extensionsToSearch = extensionsToSearch;
	result->referenceKind = refKind;
	result->importingSourceFile = file;
	result->endingPreference =
	    UserPreferences().ImportModuleSpecifierEnding;
	result->resolutionMode = mode;
	return result;
}

// string_completions.go:1050 getSupportedExtensionsForModuleResolution.
std::vector<std::string> getSupportedExtensionsForModuleResolution(
    const CompilerOptions* options,
    const std::vector<std::string>& extraExtensions,
    checker::Checker* typeChecker) {
	// file extensions from ambient modules declarations e.g. *.css
	std::vector<std::string> extensions;
	if (typeChecker != nullptr) {
		std::vector<Symbol*> ambientModules =
		    typeChecker->GetAmbientModules();
		for (Symbol* module : ambientModules) {
			std::string name = getAmbientModuleName(module);
			if (!name.starts_with("*.") ||
			    name.find('/') != std::string::npos) {
				continue;
			}
			extensions.push_back(name.substr(1));
		}
	}
	const std::vector<std::vector<std::string_view>>* supportedExtensions =
	    tsoptions::GetSupportedExtensions(options, extraExtensions);
	for (const auto& ext : *supportedExtensions) {
		for (std::string_view e : ext) {
			extensions.emplace_back(e);
		}
	}
	ModuleResolutionKind moduleResolution =
	    options->GetModuleResolutionKind();
	if (moduleResolutionUsesNodeModules(moduleResolution)) {
		// [][]string{extensions}
		std::vector<std::vector<std::string_view>> wrapped;
		wrapped.emplace_back();
		for (const std::string& e : extensions) {
			wrapped.back().emplace_back(e);
		}
		const std::vector<std::vector<std::string_view>>* withJson =
		    tsoptions::GetSupportedExtensionsWithJsonIfResolveJsonModule(
		        options, &wrapped);
		// core.Flatten
		std::vector<std::string> out;
		for (const auto& group : *withJson) {
			for (std::string_view e : group) {
				out.emplace_back(e);
			}
		}
		return out;
	}
	return extensions;
}

// string_completions.go:1074 moduleResolutionUsesNodeModules.
bool moduleResolutionUsesNodeModules(ModuleResolutionKind moduleResolution) {
	return (moduleResolution >= ModuleResolutionKind::Node16 &&
	        moduleResolution <= ModuleResolutionKind::NodeNext) ||
	       moduleResolution == ModuleResolutionKind::Bundler;
}

// string_completions.go:1080 isPathRelativeToScript — returns true if the
// path is explicitly relative (i.e. relative to . or ..).
bool isPathRelativeToScript(const std::string& path) {
	return path.starts_with("./") || path.starts_with("../");
}

// string_completions.go:1084 getCompletionEntriesForRelativeModules.
std::vector<moduleCompletionNameAndKind>
LanguageService::getCompletionEntriesForRelativeModules(
    const std::string& literalValue, const std::string& scriptDirectory,
    compiler::SimpleProgram* program, tspath::Path scriptPath,
    extensionOptions* extensionOptions) {
	const CompilerOptions* options = program->Options();
	if (!options->RootDirs.empty()) {
		return getCompletionEntriesForDirectoryFragmentWithRootDirs(
		    options->RootDirs, literalValue, scriptDirectory, program,
		    scriptPath, extensionOptions);
	}
	moduleCompletionNameAndKindSet* result =
	    getCompletionEntriesForDirectoryFragment(
	        literalValue, scriptDirectory, extensionOptions, program,
	        /*moduleSpecifierIsRelative*/ true, scriptPath,
	        new moduleCompletionNameAndKindSet());
	return mapValues(result->names);
}

// string_completions.go:1115
// getCompletionEntriesForDirectoryFragmentWithRootDirs.
std::vector<moduleCompletionNameAndKind>
LanguageService::getCompletionEntriesForDirectoryFragmentWithRootDirs(
    const std::vector<std::string>& rootDirs, const std::string& fragment,
    const std::string& scriptDirectory, compiler::SimpleProgram* program,
    const std::string& exclude, extensionOptions* extensionOptions) {
	const CompilerOptions* options = program->Options();
	std::string basePath;
	if (!options->Project.empty()) {
		basePath = options->Project;
	} else {
		basePath = program->GetCurrentDirectory();
	}
	bool ignoreCase = !program->UseCaseSensitiveFileNames();
	std::vector<std::string> baseDirectories = getBaseDirectoriesFromRootDirs(
	    rootDirs, basePath, scriptDirectory, ignoreCase);

	std::vector<moduleCompletionNameAndKind> allCompletions;
	for (const std::string& baseDirectory : baseDirectories) {
		moduleCompletionNameAndKindSet* result =
		    getCompletionEntriesForDirectoryFragment(
		        fragment, baseDirectory, extensionOptions, program,
		        /*moduleSpecifierIsRelative*/ true, exclude,
		        new moduleCompletionNameAndKindSet());
		for (auto& [name, entry] : result->names) {
			(void)name;
			allCompletions.push_back(entry);
		}
	}

	// Deduplicate based on name, kind, and extension
	return deduplicateModuleCompletions(allCompletions);
}

// string_completions.go:1155 getBaseDirectoriesFromRootDirs — takes a
// script path and returns paths for all potential folders that could be
// merged with its containing folder via the "rootDirs" compiler option.
std::vector<std::string> getBaseDirectoriesFromRootDirs(
    const std::vector<std::string>& rootDirs, const std::string& basePath,
    const std::string& scriptDirectory, bool ignoreCase) {
	// Make all paths absolute/normalized if they are not already
	std::vector<std::string> normalizedRootDirs(rootDirs.size());
	for (size_t i = 0; i < rootDirs.size(); i++) {
		std::string normalizedPath;
		if (tspath::isRootedDiskPath(rootDirs[i])) {
			normalizedPath = rootDirs[i];
		} else {
			normalizedPath =
			    tspath::combinePaths(basePath, {rootDirs[i]});
		}
		normalizedRootDirs[i] = tspath::ensureTrailingDirectorySeparator(
		    tspath::normalizePath(normalizedPath));
	}

	// Determine the path to the directory containing the script relative
	// to the root directory it is contained within
	std::string relativeDirectory;
	tspath::ComparePathsOptions comparePathsOptions;
	comparePathsOptions.useCaseSensitiveFileNames = !ignoreCase;
	comparePathsOptions.currentDirectory = basePath;
	for (const std::string& rootDirectory : normalizedRootDirs) {
		if (tspath::containsPath(rootDirectory, scriptDirectory,
		                         comparePathsOptions)) {
			if (rootDirectory.size() > scriptDirectory.size()) {
				relativeDirectory = "";
			} else {
				relativeDirectory =
				    scriptDirectory.substr(rootDirectory.size());
			}
			break;
		}
	}

	// Now find a path for each potential directory that is to be merged
	// with the one containing the script
	std::vector<std::string> directories;
	for (const std::string& rootDirectory : normalizedRootDirs) {
		directories.push_back(
		    std::string(tspath::removeTrailingDirectorySeparator(
		        tspath::combinePaths(rootDirectory,
		                             {relativeDirectory}))));
	}
	directories.push_back(std::string(
	    tspath::removeTrailingDirectorySeparator(scriptDirectory)));

	return deduplicateStrings(directories);
}

// string_completions.go:1195 deduplicateStrings.
std::vector<std::string> deduplicateStrings(
    const std::vector<std::string>& slice) {
	if (slice.size() <= 1) {
		return slice;
	}
	std::unordered_set<std::string> seen;
	std::vector<std::string> result;
	for (const std::string& s : slice) {
		if (seen.insert(s).second) {
			result.push_back(s);
		}
	}
	return result;
}

namespace {
// deduplicateModuleCompletions map key (Go anonymous `struct`).
struct moduleCompletionKey {
	std::string name;
	moduleCompletionKind kind;
	std::string extension;
	bool operator==(const moduleCompletionKey&) const = default;
};
struct moduleCompletionKeyHash {
	size_t operator()(const moduleCompletionKey& k) const {
		size_t h = std::hash<std::string>{}(k.name);
		h = h * 31 + std::hash<int32_t>{}(k.kind);
		return h * 31 + std::hash<std::string>{}(k.extension);
	}
};
} // namespace

// string_completions.go:1210 deduplicateModuleCompletions.
std::vector<moduleCompletionNameAndKind> deduplicateModuleCompletions(
    const std::vector<moduleCompletionNameAndKind>& completions) {
	if (completions.size() <= 1) {
		return completions;
	}
	std::unordered_set<moduleCompletionKey, moduleCompletionKeyHash> seen;
	std::vector<moduleCompletionNameAndKind> result;
	for (const moduleCompletionNameAndKind& c : completions) {
		moduleCompletionKey k{c.name, c.kind, c.extension};
		if (seen.insert(k).second) {
			result.push_back(c);
		}
	}
	return result;
}

// string_completions.go:1251 moduleCompletionNameAndKindSet.add.
void moduleCompletionNameAndKindSet::add(
    const moduleCompletionNameAndKind& entry) {
	auto it = names.find(entry.name);
	if (it == names.end() || it->second.kind < entry.kind) {
		names[entry.name] = entry;
	}
}
// string_completions.go:1272 — Given a path ending at a directory, gets
// the completions for the path.
moduleCompletionNameAndKindSet*
LanguageService::getCompletionEntriesForDirectoryFragment(
    const std::string& fragmentIn, const std::string& scriptDirectory,
    extensionOptions* extensionOptions, compiler::SimpleProgram* program,
    bool moduleSpecifierIsRelative, const std::string& exclude,
    moduleCompletionNameAndKindSet* result) {
	std::string fragment = tspath::normalizeSlashes(fragmentIn);

	// Remove the basename from the path.
	// We don't use the basename to filter completions: the client is
	// responsible for that filtering.
	if (!tspath::hasTrailingDirectorySeparator(fragment)) {
		fragment = std::string(tspath::getDirectoryPath(fragment));
	}

	if (fragment.empty()) {
		fragment = ".";
	}

	fragment = tspath::ensureTrailingDirectorySeparator(fragment);

	std::string baseDirectory =
	    tspath::resolvePath(scriptDirectory, {fragment});
	if (!moduleSpecifierIsRelative) {
		// Check for a version redirect.
		std::string packageJsonDirectory =
		    program->GetNearestAncestorDirectoryWithPackageJson(
		        baseDirectory);
		if (!packageJsonDirectory.empty()) {
			std::string packageJsonPath = tspath::combinePaths(
			    packageJsonDirectory, {"package.json"});
			auto packageJsonInfo =
			    program->GetPackageJsonInfo(packageJsonPath);
			if (packageJsonInfo != nullptr &&
			    packageJsonInfo->Contents != nullptr &&
			    packageJsonInfo->Contents->TypesVersions.type ==
			        packagejson::JSONValueType::Object) {
				packagejson::VersionPaths* versionPaths =
				    packageJsonInfo->Contents->GetVersionPaths(
				        nullptr);
				auto* pathsMap = versionPaths->GetPaths();
				std::vector<std::pair<std::string, std::vector<std::string>>>
				    pathsVec;
				if (pathsMap != nullptr) {
					for (const std::string& key : pathsMap->Keys()) {
						auto&& [v, ok] = pathsMap->Get(key);
						if (ok) pathsVec.emplace_back(key, *v);
					}
				}
				auto* paths = &pathsVec;
				if (paths != nullptr && !paths->empty()) {
					std::string pathInPackage = baseDirectory.substr(
					    tspath::ensureTrailingDirectorySeparator(
					        packageJsonDirectory)
					        .size());
					if (addCompletionEntriesFromPaths(
					        result, program, pathInPackage,
					        packageJsonDirectory,
					        extensionOptions, paths)) {
						// One of the `versionPaths` was
						// matched, which will block
						// relative resolution to files and
						// folders from here.
						// All reachable paths given the
						// pattern match are already added.
						return result;
					}
				}
			}
		}
	}

	if (!host->DirectoryExists(baseDirectory)) {
		return result;
	}

	// Enumerate all available files.
	std::vector<std::string> files = ReadDirectory(
	    baseDirectory, extensionOptions->extensionsToSearch,
	    /*include*/ {"./*"});

	for (const std::string& filePath : files) {
		if (tspath::comparePaths(
		        exclude, filePath,
		        tspath::ComparePathsOptions{
		            .useCaseSensitiveFileNames =
		                program->UseCaseSensitiveFileNames(),
		            .currentDirectory =
		                program->GetCurrentDirectory(),
		        }) == 0) {
			continue; // Avoid self-imports
		}

		auto [name, extension] = getFilenameWithExtensionOption(
		    std::string(tspath::getBaseFileName(filePath)), program,
		    extensionOptions,
		    /*isExportsOrImportsWildcard*/ false);
		result->add(moduleCompletionNameAndKind{
		    .name = name,
		    .kind = moduleCompletionKindFile,
		    .extension = extension,
		});
	}

	// Get folder completion as well.
	std::vector<std::string> directories = GetDirectories(baseDirectory);

	for (const std::string& directory : directories) {
		std::string directoryName =
		    std::string(tspath::getBaseFileName(directory));
		if (directoryName != "@types") {
			result->add(moduleCompletionNameAndKind{
			    .name = directoryName,
			    .kind = moduleCompletionKindDirectory,
			});
		}
	}

	return result;
}

// string_completions.go:1373 — Returns true if `fragment` was a match for
// any `paths` (which should indicate whether any other path completions
// should be offered).
bool LanguageService::addCompletionEntriesFromPaths(
    moduleCompletionNameAndKindSet* result, compiler::SimpleProgram* program,
    const std::string& fragment, const std::string& baseDirectory,
    extensionOptions* extensionOptions,
    const std::vector<std::pair<std::string, std::vector<std::string>>>*
        paths) {
	auto getPatternsForKeys = [&](const std::string& key) {
		for (const auto& [k, v] : *paths) {
			if (k == key) {
				return v;
			}
		}
		return std::vector<std::string>{};
	};
	auto comparePaths = [](const std::string& a, const std::string& b) {
		Pattern patternA = tryParsePattern(a);
		Pattern patternB = tryParsePattern(b);
		int lengthA = int(a.size());
		if (patternA.starIndex != -1) {
			lengthA = patternA.starIndex;
		}
		int lengthB = int(b.size());
		if (patternB.starIndex != -1) {
			lengthB = patternB.starIndex;
		}
		return (lengthB < lengthA)   ? -1
		       : (lengthB > lengthA) ? 1
		                             : 0;
	};
	std::vector<std::string> keys =
	    mapList(*paths,
	            [](const std::pair<std::string, std::vector<std::string>>& p) {
		            return p.first;
	                         });
	return addCompletionEntriesFromPathsOrExportsOrImports(
	    result, program, /*isExports*/ false, /*isImports*/ false,
	    fragment, baseDirectory, extensionOptions, keys,
	    getPatternsForKeys, comparePaths);
}

// string_completions.go:1413 — Returns true if `fragment` was a match for
// any `paths` (which should indicate whether any other path completions
// should be offered).
bool LanguageService::addCompletionEntriesFromPathsOrExportsOrImports(
    moduleCompletionNameAndKindSet* result, compiler::SimpleProgram* program,
    bool isExports, bool isImports, const std::string& fragment,
    const std::string& baseDirectory, extensionOptions* extensionOptions,
    const std::vector<std::string>& keys,
    const std::function<std::vector<std::string>(const std::string&)>&
        getPatternsForKey,
    const std::function<int(const std::string&, const std::string&)>&
        comparePaths) {
	struct pathResult {
		std::vector<moduleCompletionNameAndKind> results;
		bool matched;
	};
	std::vector<pathResult> pathResults;
	std::unique_ptr<std::string> matchedPath;
	for (const std::string& key : keys) {
		if (key == ".") {
			continue;
		}
		std::string normalizedKey =
		    key.starts_with("./") ? key.substr(2)
		                          : key; // Remove leading "./"
		if ((isExports || isImports) &&
		    key.ends_with("/")) { // Normalize trailing "/" to "/*"
			normalizedKey = normalizedKey + "*";
		}
		std::vector<std::string> patterns = getPatternsForKey(key);
		if (!patterns.empty()) {
			Pattern pathPattern = tryParsePattern(normalizedKey);
			if (!pathPattern.isValid()) {
				continue;
			}
			bool isMatch = pathPattern.matches(fragment);
			bool isLongestMatch = false;
			if (isMatch) {
				if (matchedPath == nullptr) {
					isLongestMatch = true;
				} else {
					isLongestMatch =
					    comparePaths(normalizedKey,
					                 *matchedPath) < 0;
				}
			}
			if (isLongestMatch) {
				// If this is a higher priority match than anything
				// we've seen so far, previous results from matches
				// are invalid, e.g.
				// for `import {} from "some-package/|"` with a
				// typesVersions:
				// {
				//   "bar/*": ["bar/*"], // <-- 1. We add 'bar', but
				//   'bar/*' doesn't match yet.
				//   "*": ["dist/*"],    // <-- 2. We match here and
				//   add files from dist. 'bar' is still ok because
				//   it didn't come from a match.
				//   "foo/*": ["foo/*"]  // <-- 3. We matched '*'
				//   earlier and added results from dist, but if
				//   'foo/*' also matched,
				// } results in dist would not be visible. 'bar' still
				//   stands because it didn't come from a match.
				//                                 This is especially
				//                                 important if `dist/foo`
				//                                 is a folder, because if
				//                                 we fail to clear
				//                                 results added by the
				//                                 '*' match, after typing
				//                                 `"some-package/foo/|"`
				//                                 we would get file
				//                                 results from both
				//                                 ./dist/foo and ./foo,
				//                                 when only the latter
				//                                 will actually be
				//                                 resolvable.
				//                                 See
				//                                 pathCompletionsTypesVersionsWildcard6.ts.
				matchedPath =
				    std::make_unique<std::string>(normalizedKey);
				pathResults = filterList(
				    pathResults,
				    [](const pathResult& pr) { return !pr.matched; });
			}
			if (pathPattern.starIndex == -1 || matchedPath == nullptr ||
			    comparePaths(normalizedKey, *matchedPath) <= 0) {
				pathResults.push_back(pathResult{
				    .results = getCompletionsForPathMapping(
				        normalizedKey, patterns, fragment,
				        baseDirectory, isExports, isImports,
				        extensionOptions, program),
				    .matched = isMatch,
				});
			}
		}
	}

	for (const pathResult& pr : pathResults) {
		for (const moduleCompletionNameAndKind& res : pr.results) {
			result->add(res);
		}
	}

	return matchedPath != nullptr;
}

// string_completions.go:1500 getCompletionsForPathMapping.
std::vector<moduleCompletionNameAndKind>
LanguageService::getCompletionsForPathMapping(
    const std::string& path, const std::vector<std::string>& patterns,
    const std::string& fragment, const std::string& packageDirectory,
    bool isExports, bool isImports, extensionOptions* extensionOptions,
    compiler::SimpleProgram* program) {
	std::string fragmentDirectory = getFragmentDirectory(fragment);
	if (!fragmentDirectory.empty()) {
		fragmentDirectory = tspath::ensureTrailingDirectorySeparator(
		    fragmentDirectory);
	}
	auto justPathMappingName =
	    [&](const std::string& name, moduleCompletionKind kind,
	        const std::string& extension)
	        -> std::vector<moduleCompletionNameAndKind> {
		if (name.starts_with(fragment)) {
			std::string n = std::string(
			    tspath::removeTrailingDirectorySeparator(name));
			if (!fragmentDirectory.empty() &&
			    n.starts_with(fragmentDirectory)) {
				n = n.substr(fragmentDirectory.size());
			}
			return {moduleCompletionNameAndKind{
			    .name = n,
			    .kind = kind,
			    .extension = extension,
			}};
		}
		return {};
	};

	Pattern parsedPath = tryParsePattern(path);
	if (!parsedPath.isValid()) {
		return {};
	}
	// No stars in the pattern.
	if (parsedPath.starIndex == -1) {
		// For a path mapping "foo": ["/x/y/z.ts"], add "foo" itself as a
		// completion.
		std::string pattern = patterns.empty() ? "" : patterns.front();
		std::string extension = getFileExtension(pattern);
		return justPathMappingName(path, moduleCompletionKindFile,
		                           extension);
	}

	std::string pathPrefix =
	    parsedPath.text.substr(0, parsedPath.starIndex);
	std::string pathSuffix =
	    parsedPath.text.substr(parsedPath.starIndex + 1);
	if (!fragment.starts_with(pathPrefix)) {
		// Fragment doesn't match the path mapping prefix at all:
		// we cannot extend it via this path.
		if (!pathPrefix.starts_with(fragment)) {
			return {};
		}
		bool starIsFullPathComponent = path.ends_with("/*");
		if (starIsFullPathComponent) {
			return justPathMappingName(pathPrefix,
			                           moduleCompletionKindDirectory,
			                           /*extension*/ "");
		}
		// If path is e.g. `foo/bar/*`, and fragment is `foo/b`, then
		// remaining directory prefix is `bar/`,
		std::string remainingDirectoryPrefix =
		    pathPrefix.substr(fragmentDirectory.size());
		std::vector<moduleCompletionNameAndKind> completions;
		for (const std::string& pattern : patterns) {
			std::vector<moduleCompletionNameAndKind> modules =
			    getModulesForPathsPattern(
			        /*fragment*/ "", packageDirectory, pattern,
			        isExports, isImports, extensionOptions, program);
			for (auto& mod : modules) {
				mod.name = remainingDirectoryPrefix + mod.name +
				    (mod.kind == moduleCompletionKindFile
				         ? pathSuffix
				         : "");
			}
			completions.insert(completions.end(), modules.begin(),
			                   modules.end());
		}
		return completions;
	}
	std::string remainingFragment = fragment.substr(pathPrefix.size());
	std::string remainingDirectoryFragment;
	if (!fragmentDirectory.starts_with(pathPrefix)) {
		remainingDirectoryFragment =
		    pathPrefix.substr(fragmentDirectory.size());
	}
	return flatMap(
	    patterns, [&](const std::string& pattern) {
		    std::vector<moduleCompletionNameAndKind> modules =
		        getModulesForPathsPattern(
		            remainingFragment, packageDirectory, pattern,
		            isExports, isImports, extensionOptions, program);
		    for (auto& mod : modules) {
			    mod.name = remainingDirectoryFragment + mod.name +
			        (mod.kind == moduleCompletionKindFile
			             ? pathSuffix
			             : "");
		    }
		    return modules;
	    });
}

// string_completions.go:1598 getFileExtension.
std::string getFileExtension(const std::string& fileName) {
	std::string_view extension = tspath::tryGetExtensionFromPath(fileName);
	if (extension.empty()) {
		extension = tspath::getAnyExtensionFromPath(
		    fileName, /*extensions*/ nullptr, /*ignoreCase*/ false);
	}
	return std::string(extension);
}

// string_completions.go:1611 — The input fragment is relative to the path
// pattern's prefix:
// e.g. if path = "bar/_*/baz", and fragment = "bar/_dir", then fragment is
// "dir".
// The names are relative to the path pattern's prefix and fragment
// directory :
// e.g. if path = "bar/_*/baz", and fragment = "bar/_dir/a", and we find
// result "abd", the result should be interpreted as "bar/_dir/abd".
std::vector<moduleCompletionNameAndKind>
LanguageService::getModulesForPathsPattern(
    const std::string& fragment, const std::string& packageDirectory,
    const std::string& pattern, bool isExports, bool isImports,
    extensionOptions* extensionOptions, compiler::SimpleProgram* program) {
	Pattern parsed = tryParsePattern(pattern);
	if (!parsed.isValid() || parsed.starIndex == -1) {
		return {};
	}

	std::string prefix = parsed.text.substr(0, parsed.starIndex);
	std::string suffix = parsed.text.substr(parsed.starIndex + 1);

	// The prefix has two effective parts: the directory path and the
	// base component after the filepath that is not a full directory
	// component. For example: directory/path/of/prefix/base*
	std::string normalizedPrefix = tspath::resolvePath(prefix, {});
	std::string normalizedPrefixDirectory;
	std::string normalizedPrefixBase;
	if (tspath::hasTrailingDirectorySeparator(prefix)) {
		normalizedPrefixDirectory = normalizedPrefix;
		normalizedPrefixBase = "";
	} else {
		normalizedPrefixDirectory =
		    std::string(tspath::getDirectoryPath(normalizedPrefix));
		normalizedPrefixBase =
		    std::string(tspath::getBaseFileName(normalizedPrefix));
	}

	bool fragmentHasPath = containsSlash(fragment);
	std::string fragmentDirectory;
	if (fragmentHasPath) {
		if (tspath::hasTrailingDirectorySeparator(fragment)) {
			fragmentDirectory = fragment;
		} else {
			fragmentDirectory =
			    std::string(tspath::getDirectoryPath(fragment));
		}
	}

	const CompilerOptions* options = program->Options();
	bool ignoreCase = !program->UseCaseSensitiveFileNames();
	std::string outDir = options->OutDir;
	std::string declarationDir = options->DeclarationDir;

	// Try and expand the prefix to include any path from the fragment so
	// that we can limit the readDirectory call
	std::string expandedPrefixDirectory;
	if (fragmentHasPath) {
		expandedPrefixDirectory = tspath::combinePaths(
		    normalizedPrefixDirectory,
		    {normalizedPrefixBase + fragmentDirectory});
	} else {
		expandedPrefixDirectory = normalizedPrefixDirectory;
	}
	// Need to normalize after combining: If we combinePaths("a",
	// "../b"), we want "b" and not "a/../b".
	std::string baseDirectory = tspath::normalizePath(
	    tspath::combinePaths(packageDirectory, {expandedPrefixDirectory}));

	std::string possibleInputBaseDirectoryForOutDir;
	std::string possibleInputBaseDirectoryForDeclarationDir;
	if (isImports) {
		if (!outDir.empty()) {
			possibleInputBaseDirectoryForOutDir =
			    getPossibleOriginalInputPathWithoutChangingExt(
			        baseDirectory, ignoreCase, outDir,
			        [&]() {
				        return program->CommonSourceDirectory();
			        });
		}
		if (!declarationDir.empty()) {
			possibleInputBaseDirectoryForDeclarationDir =
			    getPossibleOriginalInputPathWithoutChangingExt(
			        baseDirectory, ignoreCase, declarationDir,
			        [&]() {
				        return program->CommonSourceDirectory();
			        });
		}
	}

	std::string normalizedSuffix = tspath::normalizePath(suffix);

	std::string declarationExtension;
	std::vector<std::string> inputExtensions;
	if (!normalizedSuffix.empty()) {
		declarationExtension =
		    tspath::getDeclarationEmitExtensionForPath(
		        "_" + normalizedSuffix);
		inputExtensions =
		    tspath::getPossibleOriginalInputExtensionForExtension(
		        "_" + normalizedSuffix);
	}

	std::vector<std::string> matchingSuffixes;
	if (!declarationExtension.empty()) {
		matchingSuffixes.push_back(std::string(tspath::changeExtension(
		    normalizedSuffix, declarationExtension)));
	}
	for (std::string_view ext : inputExtensions) {
		matchingSuffixes.push_back(
		    std::string(tspath::changeExtension(normalizedSuffix, ext)));
	}
	matchingSuffixes.push_back(normalizedSuffix);

	// If we have a suffix, then we read the directory all the way down to
	// avoid returning completions for directories that don't contain
	// files that would match the suffix. A previous comment here was
	// concerned about the case where `normalizedSuffix` includes a `?`
	// character, which should be interpreted literally, but will match
	// any single character as part of the `include` pattern in
	// `tryReadDirectory`. This is not a problem, because (in the
	// extremely unusual circumstance where the suffix has a `?` in it) a
	// `?` interpreted as "any character" can only return *too many*
	// results as compared to the literal interpretation, so we can filter
	// those superfluous results out via `trimPrefixAndSuffix` as we've
	// always done.
	std::vector<std::string> includeGlobs;
	if (!normalizedSuffix.empty()) {
		for (const std::string& suffixStr : matchingSuffixes) {
			includeGlobs.push_back("**/*" + suffixStr);
		}
	} else {
		includeGlobs = {"./*"};
	}

	bool isExportsOrImportsWildcard = (isExports || isImports) &&
	    pattern.ends_with("/*");

	auto trimPrefixAndSuffix = [&](const std::string& path,
	                               const std::string& prefixStr) {
		for (const std::string& suffixStr : matchingSuffixes) {
			std::optional<std::string> inner = withoutStartAndEnd(
			    tspath::normalizePath(path), prefixStr, suffixStr);
			if (!inner.has_value()) {
				continue;
			}
			return removeLeadingDirectorySeparator(*inner);
		}
		return std::string();
	};

	auto getMatchesWithPrefix = [&](const std::string& directory) {
		std::string completePrefix;
		if (fragmentHasPath) {
			completePrefix = directory;
		} else {
			completePrefix =
			    tspath::ensureTrailingDirectorySeparator(directory) +
			    normalizedPrefixBase;
		}

		std::vector<std::string> matches = ReadDirectory(
		    directory, extensionOptions->extensionsToSearch,
		    includeGlobs);

		std::vector<moduleCompletionNameAndKind> result;
		for (const std::string& match : matches) {
			std::string trimmedWithPattern =
			    trimPrefixAndSuffix(match, completePrefix);
			if (!trimmedWithPattern.empty()) {
				if (containsSlash(trimmedWithPattern)) {
					std::vector<std::string> pathComponents =
					    tspath::getPathComponents(
					        removeLeadingDirectorySeparator(
					            trimmedWithPattern),
					        "");
					if (pathComponents.size() > 1) {
						result.push_back(
						    moduleCompletionNameAndKind{
						        .name = pathComponents[1],
						        .kind =
						            moduleCompletionKindDirectory,
						    });
					}
				} else {
					auto [name, extension] =
					    getFilenameWithExtensionOption(
					        trimmedWithPattern, program,
					        extensionOptions,
					        isExportsOrImportsWildcard);
					if (extension.empty()) {
						extension = getFileExtension(match);
					}
					result.push_back(
					    moduleCompletionNameAndKind{
					        .name = name,
					        .kind = moduleCompletionKindFile,
					        .extension = extension,
					    });
				}
			}
		}
		return result;
	};

	auto getDirectoryMatches = [&](const std::string& directoryName) {
		std::vector<std::string> directories =
		    GetDirectories(directoryName);
		std::vector<moduleCompletionNameAndKind> result;
		for (const std::string& dir : directories) {
			if (dir != "node_modules") {
				result.push_back(moduleCompletionNameAndKind{
				    .name = dir,
				    .kind = moduleCompletionKindDirectory,
				});
			}
		}
		return result;
	};

	std::vector<moduleCompletionNameAndKind> matches;
	std::vector<moduleCompletionNameAndKind> more =
	    getMatchesWithPrefix(baseDirectory);
	matches.insert(matches.end(), more.begin(), more.end());

	if (!possibleInputBaseDirectoryForOutDir.empty()) {
		more = getMatchesWithPrefix(
		    possibleInputBaseDirectoryForOutDir);
		matches.insert(matches.end(), more.begin(), more.end());
	}
	if (!possibleInputBaseDirectoryForDeclarationDir.empty()) {
		more = getMatchesWithPrefix(
		    possibleInputBaseDirectoryForDeclarationDir);
		matches.insert(matches.end(), more.begin(), more.end());
	}

	// If we had a suffix, we already recursively searched for all
	// possible files that could match it and returned the directories
	// leading to those files. Otherwise, assume any directory could have
	// something valid to import.
	if (normalizedSuffix.empty()) {
		more = getDirectoryMatches(baseDirectory);
		matches.insert(matches.end(), more.begin(), more.end());
		if (!possibleInputBaseDirectoryForOutDir.empty()) {
			more = getDirectoryMatches(
			    possibleInputBaseDirectoryForOutDir);
			matches.insert(matches.end(), more.begin(), more.end());
		}
		if (!possibleInputBaseDirectoryForDeclarationDir.empty()) {
			more = getDirectoryMatches(
			    possibleInputBaseDirectoryForDeclarationDir);
			matches.insert(matches.end(), more.begin(), more.end());
		}
	}

	return matches;
}

// string_completions.go:1822 containsSlash.
bool containsSlash(const std::string& fragment) {
	return fragment.find('/') != std::string::npos;
}

// string_completions.go:1826 withoutStartAndEnd.
std::optional<std::string> withoutStartAndEnd(const std::string& s,
                                              const std::string& start,
                                              const std::string& end) {
	if (s.starts_with(start) && s.ends_with(end) &&
	    s.size() >= start.size() + end.size()) {
		return s.substr(start.size(), s.size() - end.size());
	}
	return std::nullopt;
}

// string_completions.go:1834 removeLeadingDirectorySeparator.
std::string removeLeadingDirectorySeparator(const std::string& path) {
	return path.starts_with("/") ? path.substr(1) : path;
}

// string_completions.go:1838
// getPossibleOriginalInputPathWithoutChangingExt.
std::string getPossibleOriginalInputPathWithoutChangingExt(
    const std::string& filePath, bool ignoreCase,
    const std::string& outputDir,
    const std::function<std::string()>& getCommonSourceDirectory) {
	if (!outputDir.empty()) {
		return tspath::resolvePath(
		    getCommonSourceDirectory(),
		    {tspath::getRelativePathFromDirectory(
		        outputDir, filePath,
		        tspath::ComparePathsOptions{
		            .useCaseSensitiveFileNames = !ignoreCase,
		        })});
	}
	return filePath;
}

// string_completions.go:1855 getFilenameWithExtensionOption.
std::pair<std::string, std::string> getFilenameWithExtensionOption(
    const std::string& name, compiler::SimpleProgram* program,
    extensionOptions* extensionOptions, bool isExportsOrImportsWildcard) {
	std::string nonJSResult =
	    modulespecifiers::TryGetRealFileNameForNonJSDeclarationFileName(
	        name);
	if (!nonJSResult.empty()) {
		return {nonJSResult,
		        std::string(
		            tspath::tryGetExtensionFromPath(nonJSResult))};
	}
	if (extensionOptions->referenceKind == referenceKind::FileName) {
		return {name, std::string(tspath::tryGetExtensionFromPath(name))};
	}

	std::vector<modulespecifiers::ModuleSpecifierEnding> allowedEndings =
	    modulespecifiers::GetAllowedEndingsInPreferredOrder(
	        modulespecifiers::UserPreferences{
	            .ImportModuleSpecifierEnding =
	                extensionOptions->endingPreference,
	        },
	        program, program->Options(),
	        extensionOptions->importingSourceFile,
	        /*oldImportSpecifier*/ "", extensionOptions->resolutionMode);

	if (isExportsOrImportsWildcard) {
		// If we're completing `import {} from "foo/|"` and subpaths are
		// available via `"exports": { "./*": "./src/*" }`, the
		// completion must be a (potentially extension-swapped) file
		// name. Dropping extensions and index files is not allowed.
		allowedEndings = filterList(
		    allowedEndings, [](modulespecifiers::ModuleSpecifierEnding e) {
			    return e != modulespecifiers::ModuleSpecifierEnding::Minimal &&
			        e != modulespecifiers::ModuleSpecifierEnding::Index;
		    });
	}

	if (!allowedEndings.empty() &&
	    allowedEndings[0] ==
	        modulespecifiers::ModuleSpecifierEnding::TsExtension) {
		if (tspath::fileExtensionIsOneOf(
		        name, tspath::supportedTSImplementationExtensions)) {
			return {name,
			        std::string(tspath::tryGetExtensionFromPath(name))};
		}
		std::string_view outputExtension =
		    module::TryGetJSExtensionForFile(name, *program->Options());
		if (!outputExtension.empty()) {
			return {std::string(tspath::changeExtension(
			            name, outputExtension)),
			        std::string(outputExtension)};
		}
		return {name, std::string(tspath::tryGetExtensionFromPath(name))};
	}

	if (!isExportsOrImportsWildcard && !allowedEndings.empty() &&
	    (allowedEndings[0] ==
	         modulespecifiers::ModuleSpecifierEnding::Minimal ||
	     allowedEndings[0] ==
	         modulespecifiers::ModuleSpecifierEnding::Index) &&
	    tspath::fileExtensionIsOneOf(
	        name,
	        {tspath::extensionJs, tspath::extensionJsx,
	         tspath::extensionTs, tspath::extensionTsx,
	         tspath::extensionDts})) {
		return {std::string(tspath::removeFileExtension(name)),
		        std::string(tspath::tryGetExtensionFromPath(name))};
	}

	std::string_view outputExtension =
	    module::TryGetJSExtensionForFile(name, *program->Options());
	if (!outputExtension.empty()) {
		return {std::string(tspath::changeExtension(name,
		                                          outputExtension)),
		        std::string(outputExtension)};
	}
	return {name, std::string(tspath::tryGetExtensionFromPath(name))};
}

// string_completions.go:1911 walkUpParentheses.
Node* walkUpParentheses(Node* node) {
	switch (node->kind) {
	case Kind::ParenthesizedType:
		return walkUpParenthesizedTypes(node);
	case Kind::ParenthesizedExpression:
		return walkUpParenthesizedExpressions(node);
	default:
		return node;
	}
}

// string_completions.go:1922 getStringLiteralTypes.
std::vector<checker::StringLiteralType*> getStringLiteralTypes(
    checker::Type* t, collections::Set<std::string>* uniques,
    checker::Checker* typeChecker) {
	if (t == nullptr) {
		return {};
	}
	if (uniques == nullptr) {
		uniques = new collections::Set<std::string>{};
	}
	t = skipConstraint(t, typeChecker);
	if (t->IsUnion()) {
		std::vector<checker::StringLiteralType*> types;
		for (checker::Type* elementType : t->types()) {
			std::vector<checker::StringLiteralType*> inner =
			    getStringLiteralTypes(elementType, uniques,
			                          typeChecker);
			types.insert(types.end(), inner.begin(), inner.end());
		}
		return types;
	}
	if (t->IsStringLiteral() && !t->IsEnumLiteral() &&
	    uniques->AddIfAbsent(
	        std::get<std::string>(t->AsLiteralType()->value))) {
		return {t};
	}
	return {};
}

// string_completions.go:1943 getAlreadyUsedTypesInStringLiteralUnion.
std::vector<std::string> getAlreadyUsedTypesInStringLiteralUnion(
    Node* union_, Node* current) {
	NodeList* typesList = union_->as<UnionTypeNode>()->Types;
	if (typesList == nullptr) {
		return {};
	}
	std::vector<std::string> values;
	for (Node* typeNode : typesList->nodes) {
		if (typeNode != current && isLiteralTypeNode(typeNode) &&
		    isStringLiteral(
		        typeNode->as<LiteralTypeNode>()->Literal)) {
			values.push_back(std::string(
			    typeNode->as<LiteralTypeNode>()->Literal->text()));
		}
	}
	return values;
}

// string_completions.go:1958 hasIndexSignature.
bool hasIndexSignature(checker::Type* t, checker::Checker* typeChecker) {
	return typeChecker->GetStringIndexType(t) != nullptr ||
	    typeChecker->GetNumberIndexType(t) != nullptr;
}

// string_completions.go:1966 — Matches
//
//	require(""
//	require("")
bool isRequireCallArgument(Node* node) {
	return isCallExpression(node->parent) &&
	    !node->parent->arguments().empty() &&
	    node->parent->arguments()[0] == node &&
	    isIdentifier(node->parent->expression()) &&
	    node->parent->expression()->text() == "require";
}

// string_completions.go:1971 kindModifiersFromExtension.
lsutil::ScriptElementKindModifier kindModifiersFromExtension(
    const std::string& extension) {
	if (extension == tspath::extensionDts) {
		return lsutil::ScriptElementKindModifierDts;
	}
	if (extension == tspath::extensionJs) {
		return lsutil::ScriptElementKindModifierJs;
	}
	if (extension == tspath::extensionJson) {
		return lsutil::ScriptElementKindModifierJson;
	}
	if (extension == tspath::extensionJsx) {
		return lsutil::ScriptElementKindModifierJsx;
	}
	if (extension == tspath::extensionTs) {
		return lsutil::ScriptElementKindModifierTs;
	}
	if (extension == tspath::extensionTsx) {
		return lsutil::ScriptElementKindModifierTsx;
	}
	if (extension == tspath::extensionDmts) {
		return lsutil::ScriptElementKindModifierDmts;
	}
	if (extension == tspath::extensionMjs) {
		return lsutil::ScriptElementKindModifierMjs;
	}
	if (extension == tspath::extensionMts) {
		return lsutil::ScriptElementKindModifierMts;
	}
	if (extension == tspath::extensionDcts) {
		return lsutil::ScriptElementKindModifierDcts;
	}
	if (extension == tspath::extensionCjs) {
		return lsutil::ScriptElementKindModifierCjs;
	}
	if (extension == tspath::extensionCts) {
		return lsutil::ScriptElementKindModifierCts;
	}
	if (extension == tspath::extensionTsBuildInfo) {
		TSC_UNREACHABLE("Extension .tsbuildinfo is unsupported.");
	}
	return lsutil::ScriptElementKindModifierNone;
}

// string_completions.go:2004 getStringLiteralCompletionsFromSignature.
completionsFromTypes* getStringLiteralCompletionsFromSignature(
    Node* call, Node* arg, argumentInfoForCompletions* argumentInfo,
    checker::Checker* typeChecker) {
	bool isNewIdentifier = false;
	collections::Set<std::string> uniques;
	Node* editingArgument;
	if (isJsxOpeningLikeElement(call)) {
		editingArgument = findAncestor(arg->parent, isJsxAttribute);
		if (editingArgument == nullptr) {
			TSC_UNREACHABLE("Expected jsx opening-like element to have "
			                "a jsx attribute as ancestor.");
		}
	} else {
		editingArgument = arg;
	}
	std::vector<checker::Signature*> candidates =
	    typeChecker->GetCandidateSignaturesForStringLiteralCompletions(
	        call, editingArgument);
	std::vector<checker::StringLiteralType*> types;
	for (checker::Signature* candidate : candidates) {
		if (!(candidate->flags &
		      checker::SignatureFlagsHasRestParameter) &&
		    argumentInfo->argumentCount >
		        int(candidate->parameters.size())) {
			continue;
		}
		checker::Type* t = typeChecker->GetTypeParameterAtPosition(
		    candidate, argumentInfo->argumentIndex);
		if (isJsxOpeningLikeElement(call)) {
			checker::Type* propType =
			    typeChecker->GetTypeOfPropertyOfType(
			        t,
			        std::string(
			            editingArgument->as<JsxAttribute>()
			                ->name
			                ->text()));
			if (propType != nullptr) {
				t = propType;
			}
		}
		isNewIdentifier = isNewIdentifier || t->IsString();
		std::vector<checker::StringLiteralType*> inner =
		    getStringLiteralTypes(t, &uniques, typeChecker);
		types.insert(types.end(), inner.begin(), inner.end());
	}
	if (!types.empty()) {
		return new completionsFromTypes{
		    .types = types,
		    .isNewIdentifier = isNewIdentifier,
		};
	}
	return nullptr;
}

// string_completions.go:2046 getStringLiteralCompletionDetails.
lsproto::CompletionItem* LanguageService::getStringLiteralCompletionDetails(
    const ContextPtr& ctx, checker::Checker* checker,
    lsproto::CompletionItem* item, const std::string& name,
    SourceFile* file, int position, Node* contextToken,
    lsproto::MarkupKind docFormat) {
	if (contextToken == nullptr ||
	    !isStringLiteralLike(contextToken)) {
		return item;
	}
	stringLiteralCompletions* completions =
	    getStringLiteralCompletionEntries(ctx, file, contextToken,
	                                      position, checker);
	if (completions == nullptr) {
		return item;
	}
	return stringLiteralCompletionDetails(item, name, contextToken,
	                                      position, completions, file,
	                                      checker, docFormat);
}

// string_completions.go:2072 stringLiteralCompletionDetails.
lsproto::CompletionItem* LanguageService::stringLiteralCompletionDetails(
    lsproto::CompletionItem* item, const std::string& name, Node* location,
    int position, stringLiteralCompletions* completion, SourceFile* file,
    checker::Checker* checker, lsproto::MarkupKind docFormat) {
	if (completion->fromPaths != nullptr) {
		// Path completions have eagerly-resolved details so the client
		// can show an accurate icon for items of file kind based on the
		// file extension provided in the item detail.
		return item;
	}
	if (completion->fromProperties != nullptr) {
		completionsFromProperties* properties =
		    completion->fromProperties;
		for (Symbol* symbol : properties->symbols) {
			if (symbol->name == name) {
				return createCompletionDetailsForSymbol(
				    item, symbol, checker, location, position,
				    docFormat);
			}
		}
	}
	if (completion->fromTypes != nullptr) {
		completionsFromTypes* types = completion->fromTypes;
		for (checker::StringLiteralType* t : types->types) {
			if (std::get<std::string>(t->AsLiteralType()->value) ==
			    name) {
				return createCompletionDetails(
				    item, name, /*documentation*/ "",
				    docFormat);
			}
		}
	}
	return item;
}

// string_completions.go:2105 isInReferenceComment.
bool isInReferenceComment(SourceFile* file, int position) {
	CommentRange* commentRange = isInComment(
	    file, position, astnav::getTokenAtPosition(file, position));
	if (commentRange == nullptr) {
		return false;
	}
	std::string_view commentText = std::string_view(file->Text()).substr(
	    commentRange->pos(), commentRange->end() - commentRange->pos());
	return hasTripleSlashPrefix(commentText);
}

// string_completions.go:2114 hasTripleSlashPrefix.
bool hasTripleSlashPrefix(std::string_view commentText) {
	return commentText.starts_with("///") &&
	    trimLeftSpace(commentText.substr(3)).starts_with("<");
}

// string_completions.go:2134 — Matches a triple slash reference directive
// with an incomplete string literal for its path. Used to determine if the
// caret is currently within the string literal and capture the literal
// fragment for completions.
// For example, this matches
//
// /// <reference path="fragment
//
// but not
//
// /// <reference path="fragment"
//
// Returns (prefix, kind, toComplete, ok) where:
//   - prefix is everything up to and including the opening quote
//   - kind is either "path" or "types"
//   - toComplete is the fragment after the opening quote
//   - ok indicates whether the match was successful
std::optional<tripleSlashDirectiveResult> parseTripleSlashDirectiveFragment(
    std::string_view text) {
	std::string_view rest = text;
	if (!rest.starts_with("///")) {
		return std::nullopt;
	}

	rest = rest.substr(3);
	rest = trimLeftSpace(rest);

	// <reference
	if (!rest.starts_with("<reference")) {
		return std::nullopt;
	}
	rest = rest.substr(std::string_view("<reference").size());

	if (rest.empty() ||
	    !isWhiteSpaceLike(char32_t(rest[0]))) {
		return std::nullopt;
	}
	rest = trimLeftSpace(rest);

	// path or types
	std::string kind;
	if (rest.starts_with("path")) {
		kind = "path";
		rest = rest.substr(4);
	} else if (rest.starts_with("types")) {
		kind = "types";
		rest = rest.substr(5);
	} else {
		return std::nullopt;
	}

	// Skip optional whitespace, then must have "="
	rest = trimLeftSpace(rest);
	if (!rest.starts_with("=")) {
		return std::nullopt;
	}
	rest = rest.substr(1);

	// Skip optional whitespace, then must have opening quote (' or ")
	rest = trimLeftSpace(rest);
	if (rest.empty() || (rest[0] != '\'' && rest[0] != '"')) {
		return std::nullopt;
	}
	rest = rest.substr(1);

	// The toComplete part is everything after the opening quote
	if (rest.find_first_of("'\"") != std::string_view::npos) {
		return std::nullopt;
	}
	return tripleSlashDirectiveResult{
	    .prefix = std::string(text.substr(0, text.size() - rest.size())),
	    .kind = kind,
	    .toComplete = std::string(rest),
	};
}

// string_completions.go:2188 getTripleSlashReferenceCompletions.
pathCompletions* LanguageService::getTripleSlashReferenceCompletions(
    SourceFile* file, int position, compiler::SimpleProgram* program,
    checker::Checker* checker) {
	const CompilerOptions* compilerOptions = program->Options();
	Node* token = astnav::getTokenAtPosition(file, position);
	std::vector<CommentRange> commentRanges;
	NodeFactory factory{NodeFactoryHooks{}};
	getLeadingCommentRanges(file->Text(), token->pos(),
	                                 [&](const CommentRange& r) {
		                                 commentRanges.push_back(r);
		                                 return true;
	                                 });

	const CommentRange* foundRange = nullptr;
	for (const CommentRange& commentRange : commentRanges) {
		if (position >= commentRange.pos() &&
		    position <= commentRange.end()) {
			foundRange = &commentRange;
			break;
		}
	}
	if (foundRange == nullptr) {
		return nullptr;
	}

	std::string text = std::string(
	    std::string_view(file->Text())
	        .substr(foundRange->pos(), position - foundRange->pos()));
	auto parsed = parseTripleSlashDirectiveFragment(text);
	if (!parsed.has_value()) {
		return nullptr;
	}
	auto [replacementSpan, ok] = pathCompletionReplacementSpan(
	    file,
	    getDirectoryFragmentRange(
	        parsed->toComplete,
	        foundRange->pos() + int(parsed->prefix.size())));
	if (!ok) {
		return nullptr;
	}

	std::string scriptPath =
	    std::string(tspath::getDirectoryPath(file->Path()));

	std::vector<moduleCompletionNameAndKind> names;
	if (parsed->kind == "path") {
		extensionOptions* extOptions = getExtensionOptions(
		    compilerOptions, referenceKind::FileName, file,
		    ResolutionModeNone, /*checker*/ nullptr);
		moduleCompletionNameAndKindSet* result =
		    getCompletionEntriesForDirectoryFragment(
		        parsed->toComplete, scriptPath, extOptions, program,
		        /*moduleSpecifierIsRelative*/ true,
		        std::string(file->Path()),
		        new moduleCompletionNameAndKindSet{});
		names = mapValues(result->names);
	} else if (parsed->kind == "types") {
		extensionOptions* extOptions = getExtensionOptions(
		    compilerOptions, referenceKind::ModuleSpecifier, file,
		    ResolutionModeNone, /*checker*/ nullptr);
		auto* result = new moduleCompletionNameAndKindSet{};
		getCompletionEntriesFromTypings(
		    program, scriptPath, getFragmentDirectory(parsed->toComplete),
		    extOptions, result);
		names = mapValues(result->names);
	}

	return new pathCompletions{
	    .entries = toPathCompletions(names),
	    .replacementSpan = replacementSpan,
	};
}

} // namespace tsc::ls
