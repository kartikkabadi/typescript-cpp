// checker_services2.cpp — port of tsc/internal/checker/services.go: the
// services-facing API surface (GetSymbolsInScope … IsLibTypeForHoverVerbosity).
// The services tail of checker.go (32070-32660) lives in checker_services.cpp —
// this file is services.go only.
//
// Callees owned by slices that have not landed are declared in
// `// === slice: services2 ===` in checker.h and stubbed once at the bottom of
// this file with `TSC_UNREACHABLE("<name> — <slice> dep")`; each stub is
// deleted when its owner's real definition merges.

#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "internal/astnav/tokens.h"
#include "internal/checker/checker.h"
#include "internal/checker/mapper.h"
#include "internal/checker/types.h"
#include "internal/evaluator/evaluator.h"
#include "internal/printer/emitcontext.h"
#include "internal/scanner/scanner.h"

namespace tsc::checker {

// Cross-TU free functions with canonical defs in sibling checker files (no
// header decl).
bool canHaveLocals(Node* node);                              // utilities.go — checker_utilities.cpp
bool isCallLikeExpression(Node* node);                       // utilities.go:2998 — checker_expressions_b.cpp
bool isTypeReferenceType(Node* node);                        // utilities.go — checker_typenodes.cpp
SymbolTable createSymbolTable(const std::vector<Symbol*>& symbols); // utilities.go — checker_typenodes.cpp

namespace {

// ---------------------------------------------------------------------------
// core.* helpers — per-translation-unit copies (PORTING.md)
// ---------------------------------------------------------------------------

// core.OrElse — returns the first non-nil value.
template <class T>
T orElse(T a, T b) {
	return a != nullptr ? a : b;
}

// core.Some
template <class T, class Pred>
bool some(const std::vector<T>& v, Pred pred) {
	for (const T& e : v) {
		if (pred(e)) {
			return true;
		}
	}
	return false;
}

// core.Every
template <class T, class Pred>
bool every(const std::vector<T>& v, Pred pred) {
	for (const T& e : v) {
		if (!pred(e)) {
			return false;
		}
	}
	return true;
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

// core.Deduplicate — order-preserving unique by identity.
template <class T>
std::vector<T> deduplicate(const std::vector<T>& v) {
	std::unordered_set<T> seen;
	std::vector<T> out;
	for (const T& e : v) {
		if (seen.insert(e).second) {
			out.push_back(e);
		}
	}
	return out;
}

// core.FirstOrNil
template <class T>
T* firstOrNil(const std::vector<T*>& v) {
	return v.empty() ? nullptr : v.front();
}

// slices.Index
template <class T>
int indexOf(const std::vector<T>& v, T elem) {
	auto it = std::find(v.begin(), v.end(), elem);
	return it == v.end() ? -1 : static_cast<int>(it - v.begin());
}

// ---------------------------------------------------------------------------
// checker.go / utilities.go file-local replicas (per PORTING.md these helpers
// are tiny and copied per translation unit rather than shared).
// ---------------------------------------------------------------------------

// checker.go:25876 — isUnitType
bool isUnitType(Type* t) {
	return (t->flags & TypeFlagsUnit) != 0;
}

// checker.go:25859 — isLiteralType
bool isLiteralType(Type* t) {
	if ((t->flags & TypeFlagsBoolean) != 0) {
		return true;
	}
	if ((t->flags & TypeFlagsUnion) != 0) {
		if ((t->flags & TypeFlagsEnumLiteral) != 0) {
			return true;
		}
		return every(t->types(), isUnitType);
	}
	return isUnitType(t);
}

// checker.go:23946 — isTupleType
bool isTupleType(Type* t) {
	return (t->objectFlags & ObjectFlagsReference) != 0 &&
		   (t->Target()->objectFlags & ObjectFlagsTuple) != 0;
}

// utilities.go:286 — IsTypeAny
bool IsTypeAny(Type* t) {
	return t != nullptr && (t->flags & TypeFlagsAny) != 0;
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

// utilities.go:3017 — IsCallLikeOrFunctionLikeExpression
bool isCallLikeOrFunctionLikeExpression(Node* node) {
	return isCallLikeExpression(node) ||
		   isFunctionExpressionOrArrowFunction(node);
}

// utilities.go:3063 — HasTypeArguments
bool hasTypeArguments(Node* node) {
	switch (node->kind) {
	case Kind::CallExpression:
	case Kind::NewExpression:
	case Kind::TaggedTemplateExpression:
	case Kind::TypeReference:
	case Kind::ExpressionWithTypeArguments:
	case Kind::ImportType:
	case Kind::TypeQuery:
	case Kind::JsxOpeningElement:
	case Kind::JsxSelfClosingElement:
		return true;
	}
	return false;
}

// services.go:633 — isExportSpecifierAlias
bool isExportSpecifierAlias(Identifier* referenceLocation,
							ExportSpecifier* exportSpecifier) {
	TSC_ASSERT(exportSpecifier->PropertyName == static_cast<Node*>(referenceLocation) ||
				   exportSpecifier->name == static_cast<Node*>(referenceLocation),
			   "referenceLocation is not export specifier name or property name");
	Node* propertyName = exportSpecifier->PropertyName;
	if (propertyName != nullptr) {
		// Given `export { foo as bar } [from "someModule"]`: It's an alias at
		// `foo`, but at `bar` it's a new symbol.
		return propertyName == referenceLocation;
	}
	// `export { foo } from "foo"` is a re-export.
	// `export { foo };` is not a re-export, it creates an alias for the local
	// variable `foo`.
	return exportSpecifier->parent->parent->moduleSpecifier() == nullptr;
}

// services.go:646 — getPossibleSymbolReferenceNodes
std::vector<Node*> getPossibleSymbolReferenceNodes(SourceFile* sourceFile,
												   const std::string& symbolName,
												   Node* container);

// services.go:655 — getPossibleSymbolReferencePositions
std::vector<int> getPossibleSymbolReferencePositions(SourceFile* sourceFile,
													 const std::string& symbolName,
													 Node* container) {
	std::vector<int> positions;

	// TODO: Cache symbol existence for files to save text search
	// Also, need to make this work for unicode escapes.

	// Be resilient in the face of a symbol with no name or zero length name
	if (symbolName.empty()) {
		return positions;
	}

	const std::string& text = sourceFile->text;
	int sourceLength = static_cast<int>(text.size());
	int symbolNameLength = static_cast<int>(symbolName.size());

	if (container == nullptr) {
		container = sourceFile;
	}

	// strings.Index(text[container.Pos():], symbolName) — RELATIVE index for
	// the first match, which the loop below then treats as absolute (Go quirk
	// ported faithfully).
	int position;
	{
		size_t found = text.find(symbolName, static_cast<size_t>(container->pos()));
		position = found == std::string::npos
					   ? -1
					   : static_cast<int>(found - static_cast<size_t>(container->pos()));
	}
	int endPos = container->end();
	while (position >= 0 && position < endPos) {
		// We found a match.  Make sure it's not part of a larger word (i.e. the
		// char before and after it have to be a non-identifier char).
		int endPosition = position + symbolNameLength;

		if ((position == 0 ||
			 !isIdentifierPart(static_cast<char32_t>(
				 static_cast<unsigned char>(text[position - 1])))) &&
			(endPosition == sourceLength ||
			 !isIdentifierPart(static_cast<char32_t>(
				 static_cast<unsigned char>(text[endPosition]))))) {
			// Found a real match.  Keep searching.
			positions.push_back(position);
		}
		int startIndex = position + symbolNameLength + 1;
		if (startIndex > sourceLength) {
			break;
		}
		// strings.Index(text[startIndex:], symbolName) returns an index
		// relative to startIndex; position = startIndex + foundIndex is the
		// absolute index, i.e. the value text.find itself returns.
		size_t foundIndex = text.find(symbolName, static_cast<size_t>(startIndex));
		if (foundIndex != std::string::npos) {
			position = static_cast<int>(foundIndex);
		} else {
			break;
		}
	}

	return positions;
}

std::vector<Node*> getPossibleSymbolReferenceNodes(SourceFile* sourceFile,
												   const std::string& symbolName,
												   Node* container) {
	return mapNonNil(
		getPossibleSymbolReferencePositions(sourceFile, symbolName, container),
		[sourceFile](int pos) -> Node* {
			if (Node* referenceLocation =
				    astnav::getTouchingPropertyName(sourceFile, pos);
				referenceLocation != sourceFile) {
				return referenceLocation;
			}
			return nullptr;
		});
}

// services.go:981 — knownGenericTypeNames
const std::unordered_set<std::string> knownGenericTypeNames = {
	"Array",
	"ArrayLike",
	"ReadonlyArray",
	"Promise",
	"PromiseLike",
	"Iterable",
	"IterableIterator",
	"AsyncIterable",
	"Set",
	"WeakSet",
	"ReadonlySet",
	"Map",
	"WeakMap",
	"ReadonlyMap",
	"Partial",
	"Required",
	"Readonly",
	"Pick",
	"Omit",
	"NonNullable",
};

// services.go:1004 — isKnownGenericTypeName
bool isKnownGenericTypeName(const std::string& name) {
	return knownGenericTypeNames.count(name) != 0;
}

// jsx.go:42 — JsxNames. Only fields services.go touches are used here, but the
// full replica is kept so later slices can move to the canonical def.
struct JsxNamesStruct {
	const std::string JSX{"JSX"};
	const std::string IntrinsicElements{"IntrinsicElements"};
	const std::string ElementClass{"ElementClass"};
	const std::string ElementAttributesPropertyNameContainer{
		"ElementAttributesProperty"};
	const std::string ElementChildrenAttributeNameContainer{
		"ElementChildrenAttribute"};
	const std::string Element{"Element"};
	const std::string ElementType{"ElementType"};
	const std::string IntrinsicAttributes{"IntrinsicAttributes"};
	const std::string IntrinsicClassAttributes{"IntrinsicClassAttributes"};
	const std::string LibraryManagedAttributes{"LibraryManagedAttributes"};
};
const JsxNamesStruct JsxNames{};

}  // namespace

// ---------------------------------------------------------------------------
// services.go
// ---------------------------------------------------------------------------

std::vector<Symbol*> Checker::GetSymbolsInScope(Node* location,
											  SymbolFlags meaning) {
	return getSymbolsInScope(location, meaning);
}

std::vector<Symbol*> Checker::getSymbolsInScope(Node* location,
											  SymbolFlags meaning) {
	if ((location->flags & NodeFlagsInWithStatement) != 0) {
		// We cannot answer semantic questions within a with block, do not
		// proceed any further
		return {};
	}

	SymbolTable symbols;
	bool isStaticSymbol = false;

	// Copy the given symbol into symbol tables if the symbol has the given
	// meaning and it doesn't already exists in the symbol table.
	auto copySymbol = [&symbols](Symbol* symbol, SymbolFlags meaning) {
		if ((symbol->combinedLocalAndExportSymbolFlags() & meaning) != 0) {
			const std::string& id = symbol->name;
			// We will copy all symbol regardless of its reserved name because
			// symbolsToArray will check whether the key is a reserved name and
			// it will not copy symbol with reserved name to the array
			if (symbols.find(id) == symbols.end()) {
				symbols[id] = symbol;
			}
		}
	};

	auto copySymbols = [&copySymbol](const SymbolTable& source,
									 SymbolFlags meaning) {
		if (meaning != 0) {
			for (const auto& [_, symbol] : source) {
				copySymbol(symbol, meaning);
			}
		}
	};

	auto copyLocallyVisibleExportSymbols =
		[&copySymbol](const SymbolTable& source, SymbolFlags meaning) {
			if (meaning != 0) {
				for (const auto& [_, symbol] : source) {
					// Similar condition as in `resolveNameHelper`
					if (getDeclarationOfKind(symbol, Kind::ExportSpecifier) ==
							nullptr &&
						getDeclarationOfKind(symbol, Kind::NamespaceExport) ==
							nullptr &&
						symbol->name != InternalSymbolNameDefault) {
						copySymbol(symbol, meaning);
					}
				}
			}
		};

	auto populateSymbols = [&]() {
		Node* lastLocation = nullptr;
		for (; location != nullptr;) {
			if (isModuleDeclaration(location) &&
				location->as<ModuleDeclaration>()->Attributes != nullptr &&
				lastLocation == location->as<ModuleDeclaration>()->Attributes) {
				// Module declaration is not in scope inside its attributes.
				lastLocation = location;
				location = location->parent;
				continue;
			}

			if (canHaveLocals(location) && location->locals() != nullptr &&
				!isGlobalSourceFile(location)) {
				copySymbols(*location->locals(), meaning);
			}

			switch (location->kind) {
			case Kind::SourceFile:
				if (!isExternalModule(location->as<SourceFile>())) {
					break;
				}
				[[fallthrough]];
			case Kind::ModuleDeclaration:
				copyLocallyVisibleExportSymbols(
					getSymbolOfDeclaration(location)->exports,
					meaning & SymbolFlagsModuleMember);
				break;
			case Kind::EnumDeclaration:
				copySymbols(getSymbolOfDeclaration(location)->exports,
							meaning & SymbolFlagsEnumMember);
				break;
			case Kind::ClassExpression: {
				Node* className = location->name();
				if (className != nullptr) {
					copySymbol(location->symbol(), meaning);
				}
				// this fall-through is necessary because we would like to
				// handle type parameter inside class expression similar to how
				// we handle it in classDeclaration and interface Declaration.
				[[fallthrough]];
			}
			case Kind::ClassDeclaration:
			case Kind::InterfaceDeclaration:
				// If we didn't come from static member of class or interface,
				// add the type parameters into the symbol table
				// (type parameters of classDeclaration/classExpression and
				// interface are in member property of the symbol.
				// Note: that the memberFlags come from previous iteration.
				if (!isStaticSymbol) {
					copySymbols(
						getMembersOfSymbol(getSymbolOfDeclaration(location)),
						meaning & SymbolFlagsType);
				}
				break;
			case Kind::FunctionExpression:
				if (Node* funcName = location->name(); funcName != nullptr) {
					copySymbol(location->symbol(), meaning);
				}
				break;
			}

			if (introducesArgumentsExoticObject(location)) {
				copySymbol(argumentsSymbol, meaning);
			}

			isStaticSymbol = isStatic(location);
			lastLocation = location;
			location = location->parent;
		}

		copySymbols(globals, meaning);
	};

	populateSymbols();

	symbols.erase(InternalSymbolNameThis);  // Not a symbol, a keyword
	return symbolsToArray(symbols);
}

std::vector<Symbol*> Checker::GetExportsOfModule(Symbol* symbol) {
	return symbolsToArray(getExportsOfModule(symbol));
}

void Checker::ForEachExportAndPropertyOfModule(
	Symbol* moduleSymbol,
	const std::function<void(Symbol*, const std::string&)>& cb) {
	SymbolTable exports = getExportsOfModule(moduleSymbol);
	for (const auto& [key, exportedSymbol] : exports) {
		if (!isReservedMemberName(key)) {
			cb(exportedSymbol, key);
		}
	}

	Symbol* exportEquals =
		resolveExternalModuleSymbol(moduleSymbol, false /*dontResolveAlias*/);
	if (exportEquals == moduleSymbol) {
		return;
	}

	Type* typeOfSymbol = getTypeOfSymbol(exportEquals);
	if (!shouldTreatPropertiesOfExternalModuleAsExports(typeOfSymbol)) {
		return;
	}

	// forEachPropertyOfType
	Type* reducedType = getReducedApparentType(typeOfSymbol);
	if ((reducedType->flags & TypeFlagsStructuredType) == 0) {
		return;
	}
	for (const auto& [name, symbol] :
		 resolveStructuredTypeMembers(reducedType)->members) {
		if (isNamedMember(symbol, name)) {
			cb(symbol, name);
		}
	}
}

bool Checker::IsValidPropertyAccess(Node* node,
									const std::string& propertyName) {
	return isValidPropertyAccess(node, propertyName);
}

bool Checker::isValidPropertyAccess(Node* node,
									const std::string& propertyName) {
	switch (node->kind) {
	case Kind::PropertyAccessExpression:
		return isValidPropertyAccessWithType(
			node, node->expression()->kind == Kind::SuperKeyword, propertyName,
			getWidenedType(checkExpression(node->expression())));
	case Kind::QualifiedName:
		return isValidPropertyAccessWithType(
			node, false /*isSuper*/, propertyName,
			getWidenedType(checkExpression(node->as<QualifiedName>()->Left)));
	case Kind::ImportType:
		return isValidPropertyAccessWithType(node, false /*isSuper*/,
											 propertyName,
											 getTypeFromTypeNode(node));
	}
	// panic("Unexpected node kind in isValidPropertyAccess: " + node.Kind.String())
	::tsc::tscUnreachable(
		("Unexpected node kind in isValidPropertyAccess: " +
		 std::string(kindToString(node->kind)))
			.c_str());
}

bool Checker::isValidPropertyAccessWithType(Node* node, bool isSuper,
											const std::string& propertyName,
											Type* t) {
	// Short-circuiting for improved performance.
	if (IsTypeAny(t)) {
		return true;
	}

	Symbol* prop = getPropertyOfType(t, propertyName);
	return prop != nullptr &&
		   isPropertyAccessible(node, isSuper, false /*isWrite*/, t, prop);
}

// Checks if an existing property access is valid for completions purposes.
// node: a property access-like node where we want to check if we can access a
// property. This node does not need to be an access of the property we are
// checking. e.g. in completions, this node will often be an incomplete
// property access node, as in `foo.`. Besides providing a location (i.e.
// scope) used to check property accessibility, we use this node for computing
// whether this is a `super` property access.
// type: the type whose property we are checking.
// property: the accessed property's symbol.
bool Checker::IsValidPropertyAccessForCompletions(Node* node, Type* t,
												  Symbol* property) {
	return isPropertyAccessible(
		node,
		node->kind == Kind::PropertyAccessExpression &&
			node->expression()->kind == Kind::SuperKeyword,
		false, /*isWrite*/
		t, property);
	// Previously we validated the 'this' type of methods but this adversely
	// affected performance. See #31377 for more context.
}

std::vector<Symbol*>
Checker::GetAllPossiblePropertiesOfTypes(std::vector<Type*> types) {
	Type* unionType = getUnionType(types);
	if ((unionType->flags & TypeFlagsUnion) == 0) {
		return getAugmentedPropertiesOfType(unionType);
	}

	SymbolTable props;
	for (Type* memberType : types) {
		std::vector<Symbol*> augmentedProps =
			getAugmentedPropertiesOfType(memberType);
		for (Symbol* p : augmentedProps) {
			if (props.find(p->name) == props.end()) {
				Symbol* prop = createUnionOrIntersectionProperty(
					unionType, p->name,
					false /*skipObjectFunctionPropertyAugment*/);
				// May be undefined if the property is private
				if (prop != nullptr) {
					props[p->name] = prop;
				}
			}
		}
	}
	std::vector<Symbol*> result;
	result.reserve(props.size());
	for (const auto& [_, sym] : props) {
		result.push_back(sym);
	}
	return result;
}

bool Checker::IsUnknownSymbol(Symbol* symbol) {
	return symbol == unknownSymbol;
}

bool Checker::IsUndefinedSymbol(Symbol* symbol) {
	return symbol == undefinedSymbol;
}

// (deduped: IsArgumentsSymbol defined in checker_jsdoc.cpp)

// Originally from services.ts
Type* Checker::GetNonOptionalType(Type* t) {
	return removeOptionalTypeMarker(t);
}

Type* Checker::GetStringIndexType(Type* t) {
	return getIndexTypeOfType(t, stringType);
}

Type* Checker::GetNumberIndexType(Type* t) {
	return getIndexTypeOfType(t, numberType);
}

Type* Checker::GetElementTypeOfArrayType(Type* t) {
	return getElementTypeOfArrayType(t);
}

std::vector<Signature*> Checker::GetCallSignatures(Type* t) {
	return getSignaturesOfType(t, SignatureKind::Call);
}

std::vector<Signature*> Checker::GetConstructSignatures(Type* t) {
	return getSignaturesOfType(t, SignatureKind::Construct);
}

std::vector<Symbol*> Checker::GetApparentProperties(Type* t) {
	return getAugmentedPropertiesOfType(t);
}

std::vector<Symbol*> Checker::getAugmentedPropertiesOfType(Type* t) {
	t = getApparentType(t);
	SymbolTable propsByName = createSymbolTable(getPropertiesOfType(t));
	Type* functionType = nullptr;
	if (!getSignaturesOfType(t, SignatureKind::Call).empty()) {
		functionType = globalCallableFunctionType;
	} else if (!getSignaturesOfType(t, SignatureKind::Construct).empty()) {
		functionType = globalNewableFunctionType;
	}

	// (createSymbolTable returns a value type here, so the Go nil-table branch
	// is unreachable.)
	if (functionType != nullptr) {
		for (Symbol* p : getPropertiesOfType(functionType)) {
			if (propsByName.find(p->name) == propsByName.end()) {
				propsByName[p->name] = p;
			}
		}
	}
	return getNamedMembers(propsByName, nullptr);
}

Symbol* Checker::TryGetMemberInModuleExportsAndProperties(
	const std::string& memberName, Symbol* moduleSymbol) {
	Symbol* symbol = TryGetMemberInModuleExports(memberName, moduleSymbol);
	if (symbol != nullptr) {
		return symbol;
	}

	Symbol* exportEquals =
		resolveExternalModuleSymbol(moduleSymbol, false /*dontResolveAlias*/);
	if (exportEquals == moduleSymbol) {
		return nullptr;
	}

	Type* t = getTypeOfSymbol(exportEquals);
	if (shouldTreatPropertiesOfExternalModuleAsExports(t)) {
		return getPropertyOfType(t, memberName);
	}
	return nullptr;
}

Symbol* Checker::TryGetMemberInModuleExports(const std::string& memberName,
											 Symbol* moduleSymbol) {
	SymbolTable symbolTable = getExportsOfModule(moduleSymbol);
	auto it = symbolTable.find(memberName);
	return it != symbolTable.end() ? it->second : nullptr;
}

bool Checker::shouldTreatPropertiesOfExternalModuleAsExports(
	Type* resolvedExternalModuleType) {
	return (resolvedExternalModuleType->flags & TypeFlagsPrimitive) == 0 ||
		   (resolvedExternalModuleType->objectFlags & ObjectFlagsClass) != 0 ||
		   // `isArrayOrTupleLikeType` is too expensive to use in this
		   // auto-imports hot path.
		   isArrayType(resolvedExternalModuleType) ||
		   isTupleType(resolvedExternalModuleType);
}

Type* Checker::GetContextualType(Node* node, ContextFlags contextFlags) {
	if ((contextFlags & ContextFlagsIgnoreNodeInferences) != 0) {
		return runWithInferenceBlockedFromSourceNode<Type*>(
			node, [this, node, contextFlags]() -> Type* {
				return getContextualType(node, contextFlags);
			});
	}
	return getContextualType(node, contextFlags);
}

template <typename T>
T Checker::runWithInferenceBlockedFromSourceNode(
	Node* node, const std::function<T()>& fn) {
	Node* containingCall = findAncestor(node, isCallLikeExpression);
	if (containingCall != nullptr) {
		Node* toMarkSkip = node;
		for (;;) {
			skipDirectInferenceNodes.insert(toMarkSkip);
			toMarkSkip = toMarkSkip->parent;
			if (toMarkSkip == nullptr || toMarkSkip == containingCall) {
				break;
			}
		}
	}

	isInferencePartiallyBlocked = true;
	T result = runWithoutResolvedSignatureCaching<T>(node, fn);
	isInferencePartiallyBlocked = false;

	skipDirectInferenceNodes.clear();
	return result;
}

std::pair<Signature*, std::vector<Signature*>>
GetResolvedSignatureForSignatureHelp(Node* node, int argumentCount,
									 Checker* c) {
	return c->runWithoutResolvedSignatureCaching<
		std::pair<Signature*, std::vector<Signature*>>>(node, [c, node,
															 argumentCount]() {
		return c->getResolvedSignatureWorker(node, CheckModeIsForSignatureHelp,
											 argumentCount);
	});
}

template <typename T>
T Checker::runWithoutResolvedSignatureCaching(
	Node* node, const std::function<T()>& fn) {
	Node* ancestorNode =
		findAncestor(node, isCallLikeOrFunctionLikeExpression);
	if (ancestorNode != nullptr) {
		std::unordered_map<SignatureLinks*, Signature*> cachedResolvedSignatures;
		std::unordered_map<ValueSymbolLinks*, Type*> cachedTypes;
		for (; ancestorNode != nullptr;
			 ancestorNode = findAncestor(
				 ancestorNode->parent, isCallLikeOrFunctionLikeExpression)) {
			SignatureLinks* links = signatureLinks.Get(ancestorNode);
			cachedResolvedSignatures[links] = links->resolvedSignature;
			links->resolvedSignature = nullptr;
			if (isFunctionExpressionOrArrowFunction(ancestorNode)) {
				ValueSymbolLinks* symbolLinks =
					valueSymbolLinks.Get(getSymbolOfDeclaration(ancestorNode));
				Type* resolvedType = symbolLinks->resolvedType;
				cachedTypes[symbolLinks] = resolvedType;
				symbolLinks->resolvedType = nullptr;
			}
		}
		T result = fn();
		for (const auto& [links, resolvedSignature] :
			 cachedResolvedSignatures) {
			links->resolvedSignature = resolvedSignature;
		}
		for (const auto& [symbolLinks, resolvedType] : cachedTypes) {
			symbolLinks->resolvedType = resolvedType;
		}
		return result;
	}
	return fn();
}

Symbol* Checker::SkipAlias(Symbol* symbol) {
	if ((symbol->flags & SymbolFlagsAlias) != 0) {
		return GetAliasedSymbol(symbol);
	}
	return symbol;
}

std::vector<Symbol*> Checker::GetRootSymbols(Symbol* symbol) {
	std::vector<Symbol*> roots = getImmediateRootSymbols(symbol);
	if (roots.empty()) {
		return {symbol};
	}
	std::vector<Symbol*> result;
	for (Symbol* root : roots) {
		std::vector<Symbol*> sub = GetRootSymbols(root);
		result.insert(result.end(), sub.begin(), sub.end());
	}
	return result;
}

Symbol* Checker::GetMappedTypeSymbolOfProperty(Symbol* symbol) {
	if (ValueSymbolLinks* valueLinks = valueSymbolLinks.TryGet(symbol);
		valueLinks != nullptr) {
		return valueLinks->containingType->symbol;
	}
	return nullptr;
}

std::vector<Symbol*> Checker::getImmediateRootSymbols(Symbol* symbol) {
	if ((symbol->checkFlags & CheckFlagsSynthetic) != 0) {
		return mapNonNil(
			valueSymbolLinks.Get(symbol)->containingType->types(),
			[this, symbol](Type* t) -> Symbol* {
				return getPropertyOfType(t, symbol->name);
			});
	}
	if ((symbol->flags & SymbolFlagsTransient) != 0) {
		if (spreadLinks.Has(symbol)) {
			Symbol* leftSpread = spreadLinks.Get(symbol)->leftSpread;
			Symbol* rightSpread = spreadLinks.Get(symbol)->rightSpread;
			if (leftSpread != nullptr) {
				return {leftSpread, rightSpread};
			}
		}
		if (mappedSymbolLinks.Has(symbol)) {
			Symbol* syntheticOrigin =
				mappedSymbolLinks.Get(symbol)->syntheticOrigin;
			if (syntheticOrigin != nullptr) {
				return {syntheticOrigin};
			}
		}
		Symbol* target = tryGetTarget(symbol);
		if (target != nullptr) {
			return {target};
		}
	}
	return {};
}

Symbol* Checker::tryGetTarget(Symbol* symbol) {
	Symbol* target = nullptr;
	Symbol* next = symbol;
	for (;;) {
		if (valueSymbolLinks.Has(next)) {
			next = valueSymbolLinks.Get(next)->target;
		} else if (exportTypeLinks.Has(next)) {
			next = exportTypeLinks.Get(next)->target;
		} else {
			next = nullptr;
		}
		if (next == nullptr) {
			break;
		}
		target = next;
	}
	return target;
}

Symbol* Checker::GetExportSymbolOfSymbol(Symbol* symbol) {
	return getMergedSymbol(orElse(symbol->exportSymbol, symbol));
}

Symbol* Checker::GetExportSpecifierLocalTargetSymbol(Node* node) {
	// node should be ExportSpecifier | Identifier
	switch (node->kind) {
	case Kind::ExportSpecifier:
		if (node->parent->parent->moduleSpecifier() != nullptr) {
			return getExternalModuleMember(node->parent->parent, node,
										 false /*dontResolveAlias*/);
		}
		{
			Node* name = node->propertyNameOrName();
			if (name->kind == Kind::StringLiteral) {
				// Skip for invalid syntax like this: export { "x" }
				return nullptr;
			}
			return resolveEntityName(
				name,
				SymbolFlagsValue | SymbolFlagsType | SymbolFlagsNamespace |
					SymbolFlagsAlias,
				true /*ignoreErrors*/, false, nullptr);
		}
	case Kind::Identifier:
		return resolveEntityName(
			node,
			SymbolFlagsValue | SymbolFlagsType | SymbolFlagsNamespace |
				SymbolFlagsAlias,
			true /*ignoreErrors*/, false, nullptr);
	}
	TSC_UNREACHABLE(
		"Unhandled case in getExportSpecifierLocalTargetSymbol, node should be "
		"ExportSpecifier | Identifier");
}

Symbol* Checker::GetShorthandAssignmentValueSymbol(Node* location) {
	if (location != nullptr &&
		location->kind == Kind::ShorthandPropertyAssignment) {
		return resolveEntityName(location->name(),
								 SymbolFlagsValue | SymbolFlagsAlias,
								 true /*ignoreErrors*/, false, nullptr);
	}
	return nullptr;
}

// Get symbols that represent parameter-property-declaration as parameter and
// as property declaration.
std::pair<Symbol*, Symbol*>
Checker::GetSymbolsOfParameterPropertyDeclaration(
	Node* parameter, const std::string& parameterName) {
	Node* constructorDeclaration = parameter->parent;
	Node* classDeclaration = parameter->parent->parent;

	SymbolTable emptyLocals;
	SymbolTable& ctorLocals = constructorDeclaration->locals() != nullptr
								  ? *constructorDeclaration->locals()
								  : emptyLocals;
	Symbol* parameterSymbol =
		getSymbol(ctorLocals, parameterName, SymbolFlagsValue);
	SymbolTable memberSymbols = getMembersOfSymbol(classDeclaration->symbol());
	Symbol* propertySymbol =
		getSymbol(memberSymbols, parameterName, SymbolFlagsValue);

	if (parameterSymbol != nullptr && propertySymbol != nullptr) {
		return {parameterSymbol, propertySymbol};
	}

	TSC_UNREACHABLE(
		"There should exist two symbols, one as property declaration and one as "
		"parameter declaration");
}

// IsDeclarationUsed checks if an import declaration identifier is used in the
// source file. This is primarily used for organizing imports to determine
// which imports can be removed.
bool Checker::IsDeclarationUsed(SourceFile* sourceFile, Identifier* identifier,
								bool jsxElementsPresent,
								bool jsxModeNeedsExplicitImport) {
	if (jsxElementsPresent && jsxModeNeedsExplicitImport) {
		std::string jsxNamespace = getJsxNamespace(sourceFile);
		std::string jsxFragmentFactory = GetJsxFragmentFactory(sourceFile);
		const std::string& identifierText = identifier->Text;
		if (identifierText == jsxNamespace) {
			return true;
		}
		if (!jsxFragmentFactory.empty() && identifierText == jsxFragmentFactory) {
			return true;
		}
	}

	Symbol* symbol = GetSymbolAtLocation(identifier);
	if (symbol == nullptr) {
		return true;
	}

	return IsSymbolReferencedInFile(sourceFile, identifier, symbol);
}

// IsSymbolReferencedInFile checks if a symbol is referenced in the source file
// (besides its definition). This is used as a quick check for whether a symbol
// is used at all in a file.
bool Checker::IsSymbolReferencedInFile(SourceFile* sourceFile,
									   Identifier* definition,
									   Symbol* symbol) {
	const std::string& identifierText = definition->Text;
	for (Node* token :
		 getPossibleSymbolReferenceNodes(sourceFile, identifierText,
										 sourceFile)) {
		if (!isIdentifier(token)) {
			continue;
		}
		Identifier* id = token->as<Identifier>();
		if (id == definition || id->Text != identifierText) {
			continue;
		}
		Symbol* refSymbol = GetSymbolAtLocation(token);
		if (refSymbol == symbol) {
			return true;
		}
		if (token->parent != nullptr &&
			token->parent->kind == Kind::ShorthandPropertyAssignment) {
			Symbol* shorthandSymbol =
				GetShorthandAssignmentValueSymbol(token->parent);
			if (shorthandSymbol == symbol) {
				return true;
			}
		}
		if (token->parent != nullptr && isExportSpecifier(token->parent)) {
			Symbol* localSymbol = getLocalSymbolForExportSpecifier(
				token->as<Identifier>(), refSymbol,
				token->parent->as<ExportSpecifier>());
			if (localSymbol == symbol) {
				return true;
			}
		}
	}
	return false;
}

// GetReferencesToSymbolInFile returns all identifier nodes in the file that
// reference the given symbol.
std::vector<Node*>
Checker::GetReferencesToSymbolInFile(SourceFile* sourceFile, Symbol* symbol) {
	const std::string& identifierText = symbol->name;
	std::vector<Node*> result;
	for (Node* token :
		 getPossibleSymbolReferenceNodes(sourceFile, identifierText,
										 sourceFile)) {
		if (!isIdentifier(token)) {
			continue;
		}
		Identifier* id = token->as<Identifier>();
		if (id->Text != identifierText) {
			continue;
		}
		Symbol* refSymbol = GetSymbolAtLocation(token);
		if (refSymbol == symbol) {
			result.push_back(token);
			continue;
		}
		if (token->parent != nullptr &&
			token->parent->kind == Kind::ShorthandPropertyAssignment) {
			Symbol* shorthandSymbol =
				GetShorthandAssignmentValueSymbol(token->parent);
			if (shorthandSymbol == symbol) {
				result.push_back(token);
				continue;
			}
		}
		if (token->parent != nullptr && isExportSpecifier(token->parent)) {
			Symbol* localSymbol = getLocalSymbolForExportSpecifier(
				token->as<Identifier>(), refSymbol,
				token->parent->as<ExportSpecifier>());
			if (localSymbol == symbol) {
				result.push_back(token);
				continue;
			}
		}
	}
	return result;
}

Symbol* Checker::getLocalSymbolForExportSpecifier(
	Identifier* referenceLocation, Symbol* referenceSymbol,
	ExportSpecifier* exportSpecifier) {
	if (isExportSpecifierAlias(referenceLocation, exportSpecifier)) {
		if (Symbol* symbol =
				GetExportSpecifierLocalTargetSymbol(exportSpecifier);
			symbol != nullptr) {
			return symbol;
		}
	}
	return referenceSymbol;
}

Type* Checker::GetTypeArgumentConstraint(Node* node) {
	if (!isTypeNode(node)) {
		return nullptr;
	}
	return getTypeArgumentConstraint(node);
}

// getUninstantiatedSignatures gets generic signatures from the
// function's/constructor's type.
std::vector<Signature*> Checker::getUninstantiatedSignatures(Node* node) {
	switch (node->kind) {
	case Kind::CallExpression:
	case Kind::Decorator:
		return getSignaturesOfType(getTypeOfExpression(node->expression()),
								   SignatureKind::Call);
	case Kind::NewExpression:
		return getSignaturesOfType(getTypeOfExpression(node->expression()),
								   SignatureKind::Construct);
	case Kind::JsxSelfClosingElement:
	case Kind::JsxOpeningElement:
		if (isJsxIntrinsicTagName(node->tagName())) {
			return {};
		}
		return getSignaturesOfType(getTypeOfExpression(node->tagName()),
								   SignatureKind::Call);
	case Kind::TaggedTemplateExpression:
		return getSignaturesOfType(
			getTypeOfExpression(node->as<TaggedTemplateExpression>()->Tag),
			SignatureKind::Call);
	case Kind::BinaryExpression:
	case Kind::JsxOpeningFragment:
		return {};
	}
	return {};
}

Type* Checker::getTypeParameterConstraintForPositionAcrossSignatures(
	std::vector<Signature*> signatures, int position) {
	std::vector<Type*> relevantConstraints;
	for (Signature* signature : signatures) {
		if (position >= static_cast<int>(signature->typeParameters.size())) {
			continue;
		}
		Type* relevantTypeParameter = signature->typeParameters[position];
		Type* relevantConstraint =
			getConstraintOfTypeParameter(relevantTypeParameter);
		if (relevantConstraint != nullptr) {
			relevantConstraints.push_back(relevantConstraint);
		}
	}
	return getUnionType(relevantConstraints);
}

Type* Checker::getTypeArgumentConstraint(Node* node) {
	int typeArgumentPosition = -1;
	if (hasTypeArguments(node->parent)) {
		std::vector<Node*> typeArgs = node->parent->typeArguments();
		for (size_t i = 0; i < typeArgs.size(); i++) {
			if (typeArgs[i] == node) {
				typeArgumentPosition = static_cast<int>(i);
				break;
			}
		}
	}

	if (typeArgumentPosition >= 0) {
		// The node could be a type argument of a call, a `new` expression, a
		// decorator, an instantiation expression, or a generic type
		// instantiation.

		if (isCallLikeExpression(node->parent)) {
			return getTypeParameterConstraintForPositionAcrossSignatures(
				getUninstantiatedSignatures(node->parent), typeArgumentPosition);
		}

		if (isDecorator(node->parent->parent)) {
			return getTypeParameterConstraintForPositionAcrossSignatures(
				getUninstantiatedSignatures(node->parent->parent),
				typeArgumentPosition);
		}

		if (isExpressionWithTypeArguments(node->parent) &&
			isExpressionStatement(node->parent->parent)) {
			Type* uninstantiatedType =
				checkExpression(node->parent->expression());

			Type* callConstraint =
				getTypeParameterConstraintForPositionAcrossSignatures(
					getSignaturesOfType(uninstantiatedType, SignatureKind::Call),
					typeArgumentPosition);
			Type* constructConstraint =
				getTypeParameterConstraintForPositionAcrossSignatures(
					getSignaturesOfType(uninstantiatedType,
										SignatureKind::Construct),
					typeArgumentPosition);

			// An instantiation expression instantiates both call and construct
			// signatures, so if both exist type arguments must be assignable to
			// both constraints.
			if ((constructConstraint->flags & TypeFlagsNever) != 0) {
				return callConstraint;
			}
			if ((callConstraint->flags & TypeFlagsNever) != 0) {
				return constructConstraint;
			}
			return getIntersectionType({callConstraint, constructConstraint});
		}

		if (isTypeReferenceType(node->parent)) {
			std::vector<Type*> typeParameters =
				getTypeParametersForTypeReferenceOrImport(node->parent);
			if (typeParameters.empty()) {
				return nullptr;
			}
			if (typeArgumentPosition >=
				static_cast<int>(typeParameters.size())) {
				return nullptr;
			}
			Type* relevantTypeParameter =
				typeParameters[typeArgumentPosition];
			Type* constraint =
				getConstraintOfTypeParameter(relevantTypeParameter);
			if (constraint != nullptr) {
				return instantiateType(
					constraint,
					newTypeMapper(
						typeParameters,
						getEffectiveTypeArguments(node->parent,
												  typeParameters)));
			}
		}
	}
	return nullptr;
}

bool Checker::IsTypeInvalidDueToUnionDiscriminant(Type* contextualType,
												  Node* obj) {
	std::vector<Node*> properties = obj->properties();
	return some(properties, [&](Node* property) -> bool {
		Type* nameType = nullptr;
		Node* propertyName = property->name();
		if (propertyName != nullptr) {
			if (isJsxNamespacedName(propertyName)) {
				nameType = getStringLiteralType(propertyName->text());
			} else {
				nameType = getLiteralTypeFromPropertyName(propertyName);
			}
		}
		std::string name;
		if (nameType != nullptr && isTypeUsableAsPropertyName(nameType)) {
			name = getPropertyNameFromType(nameType);
		}
		Type* expected = nullptr;
		if (!name.empty()) {
			expected = getTypeOfPropertyOfType(contextualType, name);
		}
		return expected != nullptr && isLiteralType(expected) &&
			   !isTypeAssignableTo(getTypeOfNode(property), expected);
	});
}

// Unlike `getExportsOfModule`, this includes properties of an `export =`
// value.
std::vector<Symbol*>
Checker::GetExportsAndPropertiesOfModule(Symbol* moduleSymbol) {
	std::vector<Symbol*> exports = getExportsOfModuleAsArray(moduleSymbol);
	Symbol* exportEquals =
		resolveExternalModuleSymbol(moduleSymbol, false /*dontResolveAlias*/);
	if (exportEquals != moduleSymbol) {
		Type* t = getTypeOfSymbol(exportEquals);
		if (shouldTreatPropertiesOfExternalModuleAsExports(t)) {
			std::vector<Symbol*> props = getPropertiesOfType(t);
			exports.insert(exports.end(), props.begin(), props.end());
		}
	}
	return exports;
}

std::vector<Symbol*>
Checker::getExportsOfModuleAsArray(Symbol* moduleSymbol) {
	return symbolsToArray(getExportsOfModule(moduleSymbol));
}

// Returns all the properties of the Jsx.IntrinsicElements interface.
std::vector<Symbol*> Checker::GetJsxIntrinsicTagNamesAt(Node* location) {
	Type* intrinsics = getJsxType(JsxNames.IntrinsicElements, location);
	if (intrinsics == nullptr) {
		return {};
	}
	return GetPropertiesOfType(intrinsics);
}

Type* Checker::GetContextualTypeForJsxAttribute(Node* attribute) {
	return getContextualTypeForJsxAttribute(attribute, ContextFlagsNone);
}

LiteralValue Checker::GetConstantValue(Node* node) {
	if (node->kind == Kind::EnumMember) {
		return getEnumMemberValue(node).Value;
	}

	if (symbolNodeLinks.Get(node)->resolvedSymbol == nullptr) {
		checkExpressionCached(node);  // ensure cached resolved symbol is set
	}
	Symbol* symbol = symbolNodeLinks.Get(node)->resolvedSymbol;
	if (symbol == nullptr && isEntityNameExpression(node)) {
		symbol = resolveEntityName(node, SymbolFlagsValue,
								   true,  /*ignoreErrors*/
								   false, /*dontResolveAlias*/
								   nullptr /*location*/);
	}
	if (symbol != nullptr &&
		(symbol->flags & SymbolFlagsEnumMember) != 0) {
		// inline property\index accesses only for const enums
		Node* member = symbol->valueDeclaration;
		if (isEnumConst(member->parent)) {
			return getEnumMemberValue(member).Value;
		}
	}

	return std::monostate{};
}

std::pair<Signature*, std::vector<Signature*>>
Checker::getResolvedSignatureWorker(Node* node, CheckMode checkMode,
									int argumentCount) {
	printer::EmitContext emitContext;
	Node* parsedNode = emitContext.parseNode(node);
	apparentArgumentCount = &argumentCount;
	std::vector<Signature*> candidatesOutArray;
	Signature* res = nullptr;
	if (parsedNode != nullptr) {
		res = getResolvedSignature(parsedNode, &candidatesOutArray, checkMode);
	}
	apparentArgumentCount = nullptr;
	return {res, candidatesOutArray};
}

std::vector<Signature*>
Checker::GetCandidateSignaturesForStringLiteralCompletions(
	Node* call, Node* editingArgument) {
	// first, get candidates when inference is blocked from the source node.
	std::vector<Signature*> candidates =
		runWithInferenceBlockedFromSourceNode<std::vector<Signature*>>(
			editingArgument, [this, call]() -> std::vector<Signature*> {
				return getResolvedSignatureWorker(call, CheckModeNormal, 0)
					.second;
			});
	std::unordered_set<Signature*> candidatesSet(candidates.begin(),
												 candidates.end());

	// next, get candidates where the source node is considered for inference.
	std::vector<Signature*> otherCandidates =
		runWithoutResolvedSignatureCaching<std::vector<Signature*>>(
			editingArgument, [this, call]() -> std::vector<Signature*> {
				return getResolvedSignatureWorker(call, CheckModeNormal, 0)
					.second;
			});

	for (Signature* candidate : otherCandidates) {
		if (candidatesSet.count(candidate) != 0) {
			continue;
		}
		candidates.push_back(candidate);
	}

	return candidates;
}

// GetTypeAtPosition returns the type of a parameter at a given index in a
// signature.
Type* Checker::GetTypeAtPosition(Signature* s, int pos) {
	return getTypeAtPosition(s, pos);
}

Type* Checker::GetTypeParameterAtPosition(Signature* s, int pos) {
	Type* t = getTypeAtPosition(s, pos);
	if (t->IsIndex() && isThisTypeParameter(t->AsIndexType()->target)) {
		Type* constraint = getBaseConstraintOfType(t->AsIndexType()->target);
		if (constraint != nullptr) {
			return getIndexType(constraint);
		}
	}
	return t;
}

// GetContextualTypeForArrayLiteralAtPosition returns the contextual type for
// an element at the given position in an array with the given contextual type.
Type* Checker::GetContextualTypeForArrayLiteralAtPosition(
	Type* contextualArrayType, Node* arrayLiteral, int position) {
	if (contextualArrayType == nullptr) {
		return nullptr;
	}
	int firstSpreadIndex = -1, lastSpreadIndex = -1;
	int elementIndex = 0;
	std::vector<Node*> elements = arrayLiteral->elements();
	for (size_t i = 0; i < elements.size(); i++) {
		Node* elem = elements[i];
		if (elem->pos() < position) {
			elementIndex++;
		}
		if (isSpreadElement(elem)) {
			if (firstSpreadIndex == -1) {
				firstSpreadIndex = static_cast<int>(i);
			}
			lastSpreadIndex = static_cast<int>(i);
		}
	}
	// The array may be incomplete, so we don't know its final length.
	return getContextualTypeForElementExpression(
		contextualArrayType, elementIndex, -1 /*length*/, firstSpreadIndex,
		lastSpreadIndex);
}

Type* Checker::GetFirstTypeArgumentFromKnownType(Type* t) {
	if ((t->objectFlags & ObjectFlagsReference) != 0 && t->symbol != nullptr &&
		isKnownGenericTypeName(t->symbol->name)) {
		Symbol* symbol =
			getGlobalSymbol(t->symbol->name, SymbolFlagsType, nullptr);
		if (symbol != nullptr && symbol == t->Target()->symbol) {
			return firstOrNil(getTypeArguments(t));
		}
	}
	if (t->alias != nullptr && isKnownGenericTypeName(t->alias->symbol->name)) {
		Symbol* symbol =
			getGlobalSymbol(t->alias->symbol->name, SymbolFlagsType, nullptr);
		if (symbol != nullptr && symbol == t->alias->symbol) {
			return firstOrNil(t->alias->typeArguments);
		}
	}
	return nullptr;
}

// Gets all symbols for one property. Does not get symbols for every property.
std::vector<Symbol*> Checker::GetPropertySymbolsFromContextualType(
	Node* node, Type* contextualType, bool unionSymbolOk) {
	std::string name = getTextOfPropertyName(node->name());
	if (name.empty()) {
		return {};
	}
	if ((contextualType->flags & TypeFlagsUnion) == 0) {
		if (Symbol* symbol = getPropertyOfType(contextualType, name);
			symbol != nullptr) {
			return {symbol};
		}
		return {};
	}
	std::vector<Type*> filteredTypes = contextualType->types();
	if (isObjectLiteralExpression(node->parent) ||
		isJsxAttributes(node->parent)) {
		filteredTypes = filter(filteredTypes, [&](Type* t) {
			return !IsTypeInvalidDueToUnionDiscriminant(t, node->parent);
		});
	}
	std::vector<Symbol*> discriminatedPropertySymbols =
		mapNonNil(filteredTypes, [&](Type* t) -> Symbol* {
			return getPropertyOfType(t, name);
		});
	if (unionSymbolOk &&
		(discriminatedPropertySymbols.empty() ||
		 discriminatedPropertySymbols.size() ==
			 contextualType->types().size())) {
		if (Symbol* symbol = getPropertyOfType(contextualType, name);
			symbol != nullptr) {
			return {symbol};
		}
	}
	if (filteredTypes.empty() && discriminatedPropertySymbols.empty()) {
		// Bad discriminant -- do again without discriminating
		return mapNonNil(contextualType->types(), [&](Type* t) -> Symbol* {
			return getPropertyOfType(t, name);
		});
	}
	// by eliminating duplicates we might even end up with a single symbol
	// that helps with displaying better quick infos on properties of union
	// types
	return deduplicate(discriminatedPropertySymbols);
}

// Gets the property symbol corresponding to the property in destructuring
// assignment 'property1' from
//
//	for ( { property1: a } of elems) {
//	}
//
// 'property1' at location 'a' from:
//
//	[a] = [ property1, property2 ]
Symbol* Checker::GetPropertySymbolOfDestructuringAssignment(Node* location) {
	if (isArrayLiteralOrObjectLiteralDestructuringPattern(
			location->parent->parent)) {
		// Get the type of the object or array literal and then look for
		// property of given name in the type
		if (Type* typeOfObjectLiteral =
				getTypeOfAssignmentPattern(location->parent->parent);
			typeOfObjectLiteral != nullptr) {
			return getPropertyOfType(typeOfObjectLiteral, location->text());
		}
	}
	return nullptr;
}

// Gets the type of object literal or array literal of destructuring
// assignment. { a } from
//
//	for ( { a } of elems) {
//	}
//
// [ a ] from
//
//	[a] = [ some array ...]
Type* Checker::getTypeOfAssignmentPattern(Node* expr) {
	// If this is from "for of"
	//     for ( { a } of elems) {
	//     }
	if (isForOfStatement(expr->parent)) {
		Type* iteratedType = checkRightHandSideOfForOf(expr->parent);
		return checkDestructuringAssignment(
			expr, orElse(iteratedType, errorType), CheckModeNormal, false);
	}
	// If this is from "for" initializer
	//     for ({a } = elems[0];.....) { }
	if (isBinaryExpression(expr->parent)) {
		Type* iteratedType =
			getTypeOfExpression(expr->parent->as<BinaryExpression>()->Right);
		return checkDestructuringAssignment(
			expr, orElse(iteratedType, errorType), CheckModeNormal, false);
	}
	// If this is from nested object binding pattern
	//     for ({ skills: { primary, secondary } } = multiRobot, i = 0; i < 1;
	//     i++) {
	if (isPropertyAssignment(expr->parent)) {
		Node* node = expr->parent->parent;
		Type* typeOfParentObjectLiteral =
			orElse(getTypeOfAssignmentPattern(node), errorType);
		int propertyIndex = indexOf(node->properties(), expr->parent);
		return checkObjectLiteralDestructuringPropertyAssignment(
			node, typeOfParentObjectLiteral, propertyIndex, nullptr, false);
	}
	// Array literal assignment - array destructuring pattern
	Node* node = expr->parent;
	//    [{ property1: p1, property2 }] = elems;
	Type* typeOfArrayLiteral =
		orElse(getTypeOfAssignmentPattern(node), errorType);
	Type* elementType =
		orElse(checkIteratedTypeOrElementType(IterationUseDestructuring,
											  typeOfArrayLiteral, undefinedType,
											  expr->parent),
			   errorType);
	return checkArrayLiteralDestructuringElementAssignment(
		node, typeOfArrayLiteral, indexOf(node->elements(), expr), elementType,
		CheckModeNormal);
}

Signature* Checker::GetSignatureFromDeclaration(Node* node) {
	return getSignatureFromDeclaration(node);
}

// IsLibSymbolForHoverVerbosity returns true if a symbol is declared in a lib
// file.
bool Checker::IsLibSymbolForHoverVerbosity(Symbol* symbol) {
	if (symbol == nullptr) {
		return false;
	}
	for (Node* decl : symbol->declarations) {
		SourceFile* sf = getSourceFileOfNode(decl);
		if (sf != nullptr && program->IsSourceFileDefaultLibrary(sf->Path())) {
			return true;
		}
	}
	return false;
}

// IsLibTypeForHoverVerbosity returns true if a type is declared in a lib file.
// Don't expand types like Array or Promise, instead treating them as opaque.
bool Checker::IsLibTypeForHoverVerbosity(Type* t) {
	Symbol* symbol;
	if ((t->objectFlags & ObjectFlagsReference) != 0) {
		symbol = t->Target()->symbol;
	} else {
		symbol = t->symbol;
	}
	if (IsLibSymbolForHoverVerbosity(symbol)) {
		return true;
	}
	return isTupleType(t);
}

// ---------------------------------------------------------------------------
}  // namespace tsc::checker
