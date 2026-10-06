// findallreferences.cpp — port of tsc/internal/ls/findallreferences.go (2767
// lines). Find-all-references / rename-locations / implementations: the core
// ref-search algorithm, module-reference collection, and the LSP response
// conversions.

#include "internal/ls/ls.h"

#include <algorithm>
#include <deque>

#include "internal/ast/ast.h"
#include "internal/astnav/tokens.h"
#include "internal/binder/nameresolver.h"
#include "internal/checker/checker.h"
#include "internal/collections/collections.h"
#include "internal/compiler/program.h"
#include "internal/core/text.h"
#include "internal/debug/debug.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/ls/lsconv/lsconv.h"
#include "internal/printer/printer.h"
#include "internal/scanner/scanner.h"
#include "internal/spanmap/spanmap.h"
#include "internal/tspath/tspath.h"

namespace tsc::ls {

// doneGuard — RAII for the `done` release callback returned by
// GetTypeCheckerForFileExclusive (Go `defer done()`).
struct doneGuard {
	std::function<void()> done;
	~doneGuard() {
		if (done) {
			done();
		}
	}
};

// === file-local replicas — helpers owned by ast/core that this repo's ast
// package does not expose ===
namespace {

// --- core.go collection helpers ---
template <typename T, typename F>
std::vector<T> filter(const std::vector<T>& v, F pred) {
	std::vector<T> out;
	for (auto& x : v) {
		if (pred(x)) {
			out.push_back(x);
		}
	}
	return out;
}

template <typename T, typename F>
auto mapSlice(const std::vector<T>& v, F f)
    -> std::vector<std::invoke_result_t<F, T>> {
	std::vector<std::invoke_result_t<F, T>> out;
	out.reserve(v.size());
	for (auto& x : v) {
		out.push_back(f(x));
	}
	return out;
}

template <typename T, typename F>
auto flatMap(const std::vector<T>& v, F f)
    -> std::vector<typename std::invoke_result_t<F, T>::value_type> {
	std::vector<typename std::invoke_result_t<F, T>::value_type> out;
	for (auto& x : v) {
		auto r = f(x);
		out.insert(out.end(), r.begin(), r.end());
	}
	return out;
}

template <typename T, typename F>
auto mapNonNil(const std::vector<T>& v, F f)
    -> std::vector<std::invoke_result_t<F, T>> {
	std::vector<std::invoke_result_t<F, T>> out;
	for (auto& x : v) {
		if (auto r = f(x); r != nullptr) {
			out.push_back(r);
		}
	}
	return out;
}

template <typename T, typename F>
auto firstNonNil(const std::vector<T>& v, F f)
    -> std::invoke_result_t<F, T> {
	for (auto& x : v) {
		if (auto r = f(x); r != nullptr) {
			return r;
		}
	}
	return {};
}

template <typename T, typename F>
T find(const std::vector<T>& v, F pred) {
	for (auto& x : v) {
		if (pred(x)) {
			return x;
		}
	}
	return T{};
}

template <typename T, typename F>
int findIndex(const std::vector<T>& v, F pred) {
	for (size_t i = 0; i < v.size(); i++) {
		if (pred(v[i])) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

template <typename T, typename F>
bool some(const std::vector<T>& v, F pred) {
	for (auto& x : v) {
		if (pred(x)) {
			return true;
		}
	}
	return false;
}

template <typename T>
bool contains(const std::vector<T>& v, const T& x) {
	return std::find(v.begin(), v.end(), x) != v.end();
}

template <typename T>
int indexOf(const std::vector<T>& v, const T& x) {
	auto it = std::find(v.begin(), v.end(), x);
	return it == v.end() ? -1 : static_cast<int>(it - v.begin());
}

template <typename T>
T orElse(T a, T b) {
	return a ? a : b;
}

template <typename T>
T coalesce(T a, T b) {
	return a ? a : b;
}

// onceValue — sync.OnceValue replica: memoizes a nil-able position* result.
inline std::function<position*()> onceValue(std::function<position*()> f) {
	return [f = std::move(f), called = false,
	        cached = static_cast<position*>(nullptr)]() mutable -> position* {
		if (!called) {
			cached = f();
			called = true;
		}
		return cached;
	};
}

// --- ast replicas (utilities.go bodies this port's ast package lacks) ---

// isJSDocTag — utilities.go:2134.
bool isJSDocTag(Node* node) {
	return node->kind >= KindFirstJSDocTagNode && node->kind <= KindLastJSDocTagNode;
}

// isTagName — utilities.go:4525.
bool isTagName(Node* node) {
	return node->parent != nullptr && isJSDocTag(node->parent) &&
	       node->parent->tagName() == node;
}

// isArgumentOfElementAccessExpression — utilities.go:4539.
bool isArgumentOfElementAccessExpression(Node* node) {
	return node != nullptr && node->parent != nullptr &&
	       node->parent->kind == Kind::ElementAccessExpression &&
	       node->parent->as<ElementAccessExpression>()->ArgumentExpression ==
	           node;
}

// literalIsName — utilities.go:4533.
bool literalIsName(Node* node) {
	return isDeclarationName(node) ||
	       node->parent->kind == Kind::ExternalModuleReference ||
	       isArgumentOfElementAccessExpression(node) ||
	       isLiteralComputedPropertyDeclarationName(node);
}

// getNameTable — ast.go:2850 (SourceFile.GetNameTable).
const std::unordered_map<std::string, int32_t>& getNameTable(
    SourceFile* file) {
	file->nameTableOnce.run([file] {
		std::unordered_map<std::string, int32_t> nameTable;
		nameTable.reserve(file->IdentifierCount);

		std::function<bool(Node*)> walk = [&](Node* node) -> bool {
			if ((isIdentifier(node) && !isTagName(node) &&
			     !node->text().empty()) ||
			    (isStringOrNumericLiteralLike(node) && literalIsName(node)) ||
			    isPrivateIdentifier(node)) {
				const std::string& text = node->text();
				if (nameTable.find(text) != nameTable.end()) {
					nameTable[text] = -1;
				} else {
					nameTable[text] = node->pos();
				}
			}

			node->forEachChild(walk);
			for (auto* jsdoc : node->jsDoc(file)) {
				jsdoc->forEachChild(walk);
			}
			return false;
		};
		file->forEachChild(walk);

		file->nameTable = std::move(nameTable);
	});
	return file->nameTable;
}

// hasInitializer — utilities.go:3086.
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

// forEachReturnStatement — utilities.go:1157.
bool forEachReturnStatement(Node* body,
                            const std::function<bool(Node*)>& visitor) {
	std::function<bool(Node*)> traverse = [&](Node* node) -> bool {
		switch (node->kind) {
		case Kind::ReturnStatement:
			return visitor(node);
		case Kind::CaseBlock:
		case Kind::Block:
		case Kind::IfStatement:
		case Kind::DoStatement:
		case Kind::WhileStatement:
		case Kind::ForStatement:
		case Kind::ForInStatement:
		case Kind::ForOfStatement:
		case Kind::WithStatement:
		case Kind::SwitchStatement:
		case Kind::CaseClause:
		case Kind::DefaultClause:
		case Kind::LabeledStatement:
		case Kind::TryStatement:
		case Kind::CatchClause:
			return node->forEachChild(traverse);
		}
		return false;
	};
	return traverse(body);
}

// getSuperContainer — utilities.go:1864.
Node* getSuperContainer(Node* node, bool stopOnFunctions) {
	for (node = node->parent; node != nullptr; node = node->parent) {
		switch (node->kind) {
		case Kind::ComputedPropertyName:
			node = node->parent;
			break;
		case Kind::FunctionDeclaration:
		case Kind::FunctionExpression:
		case Kind::ArrowFunction:
			if (!stopOnFunctions) {
				continue;
			}
			return node;
		case Kind::PropertyDeclaration:
		case Kind::PropertySignature:
		case Kind::MethodDeclaration:
		case Kind::MethodSignature:
		case Kind::Constructor:
		case Kind::GetAccessor:
		case Kind::SetAccessor:
		case Kind::ClassStaticBlockDeclaration:
			return node;
		case Kind::Decorator:
			// Decorators are always applied outside of the body of a class or
			// method.
			if (node->parent->kind == Kind::Parameter &&
			    isClassElement(node->parent->parent)) {
				node = node->parent->parent;
			} else if (isClassElement(node->parent)) {
				node = node->parent;
			}
			break;
		}
	}
	return nullptr;
}

// isRightSideOfPropertyAccess — utilities.go.
bool isRightSideOfPropertyAccess(Node* node) {
	return node->parent != nullptr &&
	       node->parent->kind == Kind::PropertyAccessExpression &&
	       node->parent->as<PropertyAccessExpression>()->name == node;
}

// isArgumentExpressionOfElementAccess — utilities.go.
bool isArgumentExpressionOfElementAccess(Node* node) {
	return node->parent != nullptr &&
	       node->parent->kind == Kind::ElementAccessExpression &&
	       node->parent->as<ElementAccessExpression>()->ArgumentExpression ==
	           node;
}

// climbPastPropertyAccess — utilities.go:3654.
Node* climbPastPropertyAccess(Node* node) {
	if (isRightSideOfPropertyAccess(node)) {
		return node->parent;
	}
	return node;
}

// climbPastPropertyOrElementAccess — utilities.go:3659.
Node* climbPastPropertyOrElementAccess(Node* node) {
	if (isRightSideOfPropertyAccess(node) ||
	    isArgumentExpressionOfElementAccess(node)) {
		return node->parent;
	}
	return node;
}

// selectExpressionOfCallOrNewExpressionOrDecorator — utilities.go:3666.
Node* selectExpressionOfCallOrNewExpressionOrDecorator(Node* node) {
	if (isCallExpression(node) || isNewExpression(node) || isDecorator(node)) {
		return node->expression();
	}
	return nullptr;
}

// isCalleeWorker — utilities.go.
bool isCalleeWorker(
    Node* node, const std::function<bool(Node*)>& pred,
    const std::function<Node*(Node*)>& calleeSelector,
    bool includeElementAccess, bool skipPastOuterExpressions_) {
	Node* target = nullptr;
	if (includeElementAccess) {
		target = climbPastPropertyOrElementAccess(node);
	} else {
		target = climbPastPropertyAccess(node);
	}
	if (skipPastOuterExpressions_) {
		// Only skip outer expressions if the target is actually an expression
		// node
		if (isExpression(target)) {
			target = skipOuterExpressions(target, OEKAll);
		}
	}
	return target != nullptr && target->parent != nullptr &&
	       pred(target->parent) && calleeSelector(target->parent) == target;
}

// isCallExpressionTarget / isNewExpressionTarget — utilities.go:3689/3693.
bool isCallExpressionTarget(Node* node, bool includeElementAccess,
                            bool skipPastOuterExpressions_) {
	return isCalleeWorker(
	    node, [](Node* n) { return isCallExpression(n); },
	    selectExpressionOfCallOrNewExpressionOrDecorator, includeElementAccess,
	    skipPastOuterExpressions_);
}
bool isNewExpressionTarget(Node* node, bool includeElementAccess,
                           bool skipPastOuterExpressions_) {
	return isCalleeWorker(
	    node, [](Node* n) { return isNewExpression(n); },
	    selectExpressionOfCallOrNewExpressionOrDecorator, includeElementAccess,
	    skipPastOuterExpressions_);
}

// tryGetImportFromModuleSpecifier — utilities.go:4203.
Node* tryGetImportFromModuleSpecifier(Node* node) {
	switch (node->parent->kind) {
	case Kind::ImportDeclaration:
	case Kind::JSImportDeclaration:
	case Kind::ExportDeclaration:
		return node->parent;
	case Kind::ExternalModuleReference:
		return node->parent->parent;
	case Kind::CallExpression:
		if (isImportCall(node->parent) ||
		    isRequireCall(node->parent,
		                  /*requireStringLiteralLikeArgument*/ false)) {
			return node->parent;
		}
		return nullptr;
	case Kind::LiteralType:
		if (!isStringLiteral(node)) {
			return nullptr;
		}
		if (isImportTypeNode(node->parent->parent)) {
			return node->parent->parent;
		}
		return nullptr;
	}
	return nullptr;
}

// getMeaningFromDeclaration — utilities.go:2250.
SemanticMeaning getMeaningFromDeclaration(Node* node) {
	switch (node->kind) {
	case Kind::VariableDeclaration:
		return SemanticMeaningValue;
	case Kind::Parameter:
	case Kind::BindingElement:
	case Kind::PropertyDeclaration:
	case Kind::PropertySignature:
	case Kind::PropertyAssignment:
	case Kind::ShorthandPropertyAssignment:
	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
	case Kind::Constructor:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
	case Kind::ArrowFunction:
	case Kind::CatchClause:
	case Kind::JsxAttribute:
		return SemanticMeaningValue;

	case Kind::TypeParameter:
	case Kind::InterfaceDeclaration:
	case Kind::TypeAliasDeclaration:
	case Kind::JSTypeAliasDeclaration:
	case Kind::TypeLiteral:
		return SemanticMeaningType;
	case Kind::EnumMember:
	case Kind::ClassDeclaration:
		return SemanticMeaningValue | SemanticMeaningType;

	case Kind::ModuleDeclaration:
		if (isAmbientModule(node)) {
			return SemanticMeaningNamespace | SemanticMeaningValue;
		} else if (getModuleInstanceState(node) ==
		           ModuleInstanceState::Instantiated) {
			return SemanticMeaningNamespace | SemanticMeaningValue;
		} else {
			return SemanticMeaningNamespace;
		}

	case Kind::EnumDeclaration:
	case Kind::NamedImports:
	case Kind::ImportSpecifier:
	case Kind::ImportEqualsDeclaration:
	case Kind::ImportDeclaration:
	case Kind::JSImportDeclaration:
	case Kind::ExportAssignment:
	case Kind::ExportDeclaration:
		return SemanticMeaningAll;

	// An external module can be a Value
	case Kind::SourceFile:
		return SemanticMeaningNamespace | SemanticMeaningValue;
	}

	return SemanticMeaningAll;
}

} // namespace

// === forward declarations — file-local fns defined later ===
ReferenceEntry* newNodeEntry(Node* node);
Node* getContextNodeForNodeEntry(Node* node);
Node* getContextNode(Node* node);
TextRange getRangeOfNode(Node* node, SourceFile* sourceFile, Node* endNode);
bool isDefinitionVisible(checker::EmitResolver* emitResolver,
                         Node* declaration);
bool isDeclarationOfSymbol(Node* node, Symbol* target);
std::vector<SymbolAndEntries*> getReferencedSymbolsSpecial(
    Node* node, const std::vector<SourceFile*>& sourceFiles);
std::vector<SymbolAndEntries*> getLabelReferencesInNode(Node* container,
                                                      Node* targetLabel);
std::vector<SymbolAndEntries*> getReferencesForThisKeyword(
    Node* thisOrSuperKeyword, const std::vector<SourceFile*>& sourceFiles);
std::vector<SymbolAndEntries*> getReferencesForSuperKeyword(
    Node* superKeyword);
std::vector<SymbolAndEntries*> getAllReferencesForImportMeta(
    const std::vector<SourceFile*>& sourceFiles);
std::vector<SymbolAndEntries*> getAllReferencesForKeyword(
    const std::vector<SourceFile*>& sourceFiles, Kind keywordKind,
    bool filterReadOnlyTypeOperator);
std::vector<Node*> getPossibleSymbolReferenceNodes(
    SourceFile* sourceFile, const std::string& symbolName, Node* container);
std::vector<int> getPossibleSymbolReferencePositions(
    SourceFile* sourceFile, const std::string& symbolName, Node* container);
Node* findFirstJsxNode(Node* root);
std::vector<ReferenceEntry*> getReferencesForNonModule(
    SourceFile* referencedFile, compiler::SimpleProgram* program);
Symbol* getMergedAliasedSymbolOfNamespaceExportDeclaration(
    Node* node, Symbol* symbol, checker::Checker* checker);
std::string getSpecialSearchKind(Node* node);
std::vector<HeritageClauseElement*> getAllSuperTypeNodes(Node* node);
struct refState;
refState* newState(const gostd::Context& ctx,
                   compiler::SimpleProgram* program,
                   const std::vector<SourceFile*>& sourceFiles,
                   collections::Set<std::string>* sourceFilesSet, Node* node,
                   checker::Checker* checker, SemanticMeaning searchMeaning,
                   const refOptions& options);
std::vector<SymbolAndEntries*> getReferencedSymbolsForSymbol(
    const gostd::Context& ctx, compiler::SimpleProgram* program,
    Symbol* originalSymbol, Node* node,
    const std::vector<SourceFile*>& sourceFiles,
    collections::Set<std::string>* sourceFilesSet, checker::Checker* checker,
    const refOptions& options);
bool isStringLiteralPropertyReference(Node* node, checker::Checker* checker);
void getReferenceEntriesForShorthandPropertyAssignment(
    Node* node, checker::Checker* checker,
    const std::function<void(Node*)>& addReference);
Node* tryGetClassByExtendingIdentifier(Node* node);
void findOwnConstructorReferences(
    Symbol* classSymbol, SourceFile* sourceFile,
    const std::function<void(Node*)>& addNode);
void findSuperConstructorAccesses(
    Node* classDeclaration, const std::function<void(Node*)>& addNode);
void forEachDescendantOfKind(Node* node, Kind kind,
                             const std::function<void(Node*)>& action);

// === shared types — method definitions ===

// NewSymbolAndEntries — findallreferences.go:60.
SymbolAndEntries* NewSymbolAndEntries(DefinitionKind kind, Node* node,
                                      Symbol* symbol,
                                      std::vector<ReferenceEntry*> references) {
	return new SymbolAndEntries{
	    new Definition{kind, symbol, node, nullptr}, references};
}

// SymbolAndEntries.DefinitionNode — findallreferences.go:130.
Node* SymbolAndEntries::DefinitionNode() const {
	if (definition == nullptr) {
		return nullptr;
	}
	if (definition->node != nullptr) {
		return definition->node;
	}
	if (definition->symbol != nullptr &&
	    !definition->symbol->declarations.empty()) {
		return definition->symbol->declarations[0];
	}
	return nullptr;
}

// SymbolAndEntries.DefinitionSymbol — findallreferences.go:143.
Symbol* SymbolAndEntries::DefinitionSymbol() const {
	if (definition == nullptr) {
		return nullptr;
	}
	return definition->symbol;
}

// SymbolAndEntries.canUseDefinitionSymbol — findallreferences.go:150.
bool SymbolAndEntries::canUseDefinitionSymbol() const {
	if (definition == nullptr) {
		return false;
	}

	switch (definition->Kind) {
	case definitionKindSymbol:
	case definitionKindThis:
		return definition->symbol != nullptr;
	case definitionKindTripleSlashReference:
		// !!! TODO : need to find file reference instead?
		// May need to return true to indicate this to be file search instead and
		// might need to do for import stuff as well For now
		return false;
	default:
		return false;
	}
}

// === functions on (*ls) ===

// getRangeOfEntry — findallreferences.go:168.
lsproto::Range LanguageService::getRangeOfEntry(ReferenceEntry* entry) {
	return resolveEntry(entry)->lspRange->Range_;
}

// getRangeOfEntryForFeature — findallreferences.go:172.
std::pair<lsproto::Range, bool> LanguageService::getRangeOfEntryForFeature(
    ReferenceEntry* entry, spanmap::Feature feature) {
	auto [location, ok] = getLocationOfEntryForFeature(entry, feature);
	return {location.Range_, ok};
}

// getFileNameOfEntry — findallreferences.go:177.
lsproto::DocumentUri LanguageService::getFileNameOfEntry(
    ReferenceEntry* entry) {
	return resolveEntry(entry)->lspRange->Uri;
}

// getLocationOfEntryForFeature — findallreferences.go:181.
std::pair<lsproto::Location, bool>
LanguageService::getLocationOfEntryForFeature(ReferenceEntry* entry,
                                            spanmap::Feature feature) {
	resolveEntrySource(entry);
	auto [location, fidelity] = sourceFileRangeToLSPLocationForFeature(
	    entry->sourceFile, *entry->textRange, feature);
	return {location, fidelity.IsSingleSegment()};
}

// resolveEntrySource — findallreferences.go:187.
void LanguageService::resolveEntrySource(ReferenceEntry* entry) {
	if (entry->sourceFile == nullptr) {
		TSC_ASSERT(entry->node != nullptr,
		           "reference entry must have a node or source file");
		entry->sourceFile = getSourceFileOfNode(entry->node);
	}
	if (entry->textRange == nullptr) {
		TextRange textRange =
		    getRangeOfNode(entry->node, entry->sourceFile, /*endNode*/ nullptr);
		entry->textRange = new TextRange(textRange);
	}
}

// resolveEntry — findallreferences.go:198.
ReferenceEntry* LanguageService::resolveEntry(ReferenceEntry* entry) {
	resolveEntrySource(entry);
	if (entry->lspRange == nullptr) {
		auto [location, fidelity] =
		    sourceFileRangeToLSPLocation(entry->sourceFile, *entry->textRange);
		entry->lspRange = new lsproto::Location(location);
		entry->unmappable = !fidelity.IsSingleSegment();
	}
	return entry;
}

// newNodeEntryWithKind — findallreferences.go:208.
ReferenceEntry* newNodeEntryWithKind(Node* node, entryKind kind) {
	ReferenceEntry* e = newNodeEntry(node);
	e->kind = kind;
	return e;
}

// newNodeEntry — findallreferences.go:214.
ReferenceEntry* newNodeEntry(Node* node) {
	// creates nodeEntry with `kind == entryKindNode`
	return new ReferenceEntry{entryKindNode, orElse(node->name(), node),
	                          getContextNodeForNodeEntry(node)};
}

// getContextNodeForNodeEntry — findallreferences.go:223.
Node* getContextNodeForNodeEntry(Node* node) {
	if (isDeclaration(node)) {
		return getContextNode(node);
	}

	if (node->parent == nullptr) {
		return nullptr;
	}

	if (!isDeclaration(node->parent) && !isExportAssignment(node->parent)) {
		// Special property assignment in javascript
		if (isInJSFile(node)) {
			// !!! jsdoc: check if branch still needed
			Node* binaryExpression = nullptr;
			if (isBinaryExpression(node->parent)) {
				binaryExpression = node->parent;
			} else if (isAccessExpression(node->parent) &&
			           isBinaryExpression(node->parent->parent) &&
			           node->parent->parent->as<BinaryExpression>()->Left ==
			               node->parent) {
				binaryExpression = node->parent->parent;
			}
			if (binaryExpression != nullptr &&
			    getAssignmentDeclarationKind(binaryExpression) !=
			        JSDeclarationKind::None) {
				return getContextNode(binaryExpression);
			}
		}

		// Jsx Tags
		switch (node->parent->kind) {
		case Kind::JsxOpeningElement:
		case Kind::JsxClosingElement:
			return node->parent->parent;
		case Kind::JsxSelfClosingElement:
		case Kind::LabeledStatement:
		case Kind::BreakStatement:
		case Kind::ContinueStatement:
			return node->parent;
		case Kind::StringLiteral:
		case Kind::NoSubstitutionTemplateLiteral:
			if (Node* validImport = tryGetImportFromModuleSpecifier(node);
			    validImport != nullptr) {
				Node* declOrStatement =
				    findAncestor(validImport, [&](Node*) {
					    return isDeclaration(node) || isStatement(node) ||
					           isJSDocTag(node);
				    });
				if (isDeclaration(declOrStatement)) {
					return getContextNode(declOrStatement);
				}
				return declOrStatement;
			}
		}

		// Handle computed property name
		Node* propertyName = findAncestor(node, isComputedPropertyName);
		if (propertyName != nullptr) {
			return getContextNode(propertyName->parent);
		}
		return nullptr;
	}

	if (node->parent->name() == node || // node is name of declaration, use
	                                  // parent
	    node->parent->kind == Kind::Constructor ||
	    node->parent->kind == Kind::ExportAssignment ||
	    // Property name of the import export specifier or binding pattern, use
	    // parent
	    ((isImportOrExportSpecifier(node->parent) ||
	      node->parent->kind == Kind::BindingElement) &&
	     node->parent->propertyName() == node) ||
	    // Is default export
	    (node->kind == Kind::DefaultKeyword &&
	     hasSyntacticModifier(node->parent, ModifierFlagsExportDefault))) {
		return getContextNode(node->parent);
	}

	return nullptr;
}

// getContextNode — findallreferences.go:286.
Node* getContextNode(Node* node) {
	if (node == nullptr) {
		return nullptr;
	}
	switch (node->kind) {
	case Kind::VariableDeclaration:
		if (!isVariableDeclarationList(node->parent) ||
		    node->parent->as<VariableDeclarationList>()
		            ->Declarations->nodes.size() != 1) {
			return node;
		} else if (isVariableStatement(node->parent->parent)) {
			return node->parent->parent;
		} else if (isForInOrOfStatement(node->parent->parent)) {
			return getContextNode(node->parent->parent);
		}
		return node->parent;

	case Kind::BindingElement:
		return getContextNode(node->parent->parent);

	case Kind::ImportSpecifier:
		return node->parent->parent->parent;

	case Kind::ExportSpecifier:
	case Kind::NamespaceImport:
		return node->parent->parent;

	case Kind::ImportClause:
	case Kind::NamespaceExport:
		return node->parent;

	case Kind::BinaryExpression:
		return ifElse(node->parent->kind == Kind::ExpressionStatement,
		              node->parent, node);

	case Kind::ForOfStatement:
	case Kind::ForInStatement:
		// !!! not implemented
		return nullptr;

	case Kind::PropertyAssignment:
	case Kind::ShorthandPropertyAssignment:
		if (isArrayLiteralOrObjectLiteralDestructuringPattern(node->parent)) {
			return getContextNode(findAncestor(node->parent, [](Node* n) {
				return n->kind == Kind::BinaryExpression ||
				       isForInOrOfStatement(n);
			}));
		}
		return node;
	case Kind::SwitchStatement:
		// !!! not implemented
		return nullptr;
	default:
		return node;
	}
}

// getRangeOfNode — findallreferences.go:335.
TextRange getRangeOfNode(Node* node, SourceFile* sourceFile, Node* endNode) {
	if (sourceFile == nullptr) {
		sourceFile = getSourceFileOfNode(node);
	}
	int start = getTokenPosOfNode(node, sourceFile,
	                              /*includeJsDoc*/ false);
	int end = ifElse(endNode != nullptr, endNode, node)->end();
	if (isStringLiteralLike(node) && (end - start) > 2) {
		if (endNode != nullptr) {
			TSC_UNREACHABLE("endNode is not nil for stringLiteralLike");
		}
		start += 1;
		end -= 1;
	}
	if (endNode != nullptr && endNode->kind == Kind::CaseBlock) {
		end = endNode->pos();
	}
	return TextRange{start, end};
}

// isValidReferencePosition — findallreferences.go:354.
bool isValidReferencePosition(Node* node, const std::string& searchSymbolName) {
	switch (node->kind) {
	case Kind::PrivateIdentifier:
		// !!!
		// if (isJSDocMemberName(node.Parent)) {
		// 	return true;
		// }
		return node->text().size() == searchSymbolName.size();
	case Kind::Identifier:
		return node->text().size() == searchSymbolName.size();
	case Kind::NoSubstitutionTemplateLiteral:
	case Kind::StringLiteral:
		return node->text().size() == searchSymbolName.size() &&
		       (isLiteralNameOfPropertyDeclarationOrIndexAccess(node) ||
		        isNameOfModuleDeclaration(node) ||
		        isExpressionOfExternalModuleImportEqualsDeclaration(node) ||
		        (isCallExpression(node->parent) &&
		         isBindableObjectDefinePropertyCall(node->parent) &&
		         node->parent->arguments()[1] == node) ||
		        isImportOrExportSpecifier(node->parent));
	case Kind::NumericLiteral:
		return isLiteralNameOfPropertyDeclarationOrIndexAccess(node) &&
		       node->text().size() == searchSymbolName.size();
	case Kind::DefaultKeyword:
		return std::string("default").size() == searchSymbolName.size();
	}
	return false;
}

// isForRenameWithPrefixAndSuffixText — findallreferences.go:378.
bool isForRenameWithPrefixAndSuffixText(const refOptions& options) {
	return options.use == referenceUseRename && options.useAliasesForRename;
}

// skipPastExportOrImportSpecifierOrUnion — findallreferences.go:382.
Symbol* skipPastExportOrImportSpecifierOrUnion(
    Symbol* symbol, Node* node, checker::Checker* checker,
    bool useLocalSymbolForExportSpecifier) {
	if (node == nullptr) {
		return nullptr;
	}
	Node* parent = node->parent;
	if (parent->kind == Kind::ExportSpecifier &&
	    useLocalSymbolForExportSpecifier) {
		return getLocalSymbolForExportSpecifier(
		    node, symbol, parent->as<ExportSpecifier>(), checker);
	}
	// If the symbol is declared as part of a declaration like
	// `{ type: "a" } | { type: "b" }`, use the property on the union type to get
	// more references.
	return firstNonNil(symbol->declarations, [&](Node* decl) -> Symbol* {
		if (decl->parent == nullptr) {
			// Ignore UMD module and global merge and CJS module end exports
			// symbols
			if (symbol->flags &
			    (SymbolFlagsTransient | SymbolFlagsModuleExports)) {
				return nullptr;
			}
			// Assertions for GH#21814. We should be handling SourceFile symbols
			// in `getReferencedSymbolsForModule` instead of getting here.
			TSC_UNREACHABLE(
			    ("Unexpected symbol at " +
			     std::string(kindToString(node->kind)) + ": " +
			     symbol->name)
			        .c_str());
		}
		if (decl->parent->kind == Kind::TypeLiteral &&
		    decl->parent->parent->kind == Kind::UnionType) {
			return checker->GetPropertyOfType(
			    checker->GetTypeFromTypeNode(decl->parent->parent),
			    symbol->name);
		}
		return nullptr;
	});
}

// getSymbolScope — findallreferences.go:407.
Node* getSymbolScope(Symbol* symbol) {
	// If this is the symbol of a named function expression or named class
	// expression, then named references are limited to its own scope.
	Node* valueDeclaration = symbol->valueDeclaration;
	if (valueDeclaration != nullptr &&
	    (valueDeclaration->kind == Kind::FunctionExpression ||
	     valueDeclaration->kind == Kind::ClassExpression)) {
		return valueDeclaration;
	}

	if (symbol->declarations.empty()) {
		return nullptr;
	}

	std::vector<Node*> declarations = symbol->declarations;
	// If this is private property or method, the scope is the containing class
	if (symbol->flags & (SymbolFlagsProperty | SymbolFlagsMethod)) {
		Node* privateDeclaration = find(declarations, [](Node* d) {
			return hasModifier(d, ModifierFlagsPrivate) ||
			       isPrivateIdentifierClassElementDeclaration(d);
		});
		if (privateDeclaration != nullptr) {
			return findAncestorKind(privateDeclaration,
			                        Kind::ClassDeclaration);
		}
		// Else this is a public property and could be accessed from anywhere.
		return nullptr;
	}

	// If symbol is of object binding pattern element without property name we
	// would want to look for property too and that could be anywhere
	if (some(declarations, isObjectBindingElementWithoutPropertyName)) {
		return nullptr;
	}

	/*
		If the symbol has a parent, it's globally visible unless:
		- It's a private property (handled above).
		- It's a type parameter.
		- The parent is an external module: then we should only search in the
		  module (and recurse on the export later).
		- But if the parent has `export as namespace`, the symbol is globally
		  visible through that namespace.
	*/
	bool exposedByParent =
	    symbol->parent != nullptr &&
	    !(symbol->flags & SymbolFlagsTypeParameter);
	if (exposedByParent &&
	    !(checker::isExternalModuleSymbol(symbol->parent) &&
	      !isSourceFileWithGlobalExports(
	          symbol->parent->valueDeclaration))) {
		return nullptr;
	}

	Node* scope = nullptr;
	for (auto* declaration : declarations) {
		Node* container = getContainerNode(declaration);
		if (scope != nullptr && scope != container) {
			// Different declarations have different containers, bail out
			return nullptr;
		}

		if (container == nullptr ||
		    (container->kind == Kind::SourceFile &&
		     !isExternalOrCommonJSModule(container->as<SourceFile>()))) {
			// This is a global variable and not an external module, any
			// declaration defined within this scope is visible outside the file
			return nullptr;
		}

		scope = container;
	}

	// If symbol.parent, this means we are in an export of an external module.
	// (Otherwise we would have returned `undefined` above.) For an export of a
	// module, we may be in a declaration file, and it may be accessed
	// elsewhere. E.g.:
	//     declare module "a" { export type T = number; }
	//     declare module "b" { import { T } from "a"; export const x: T; }
	// So we must search the whole source file. (Because we will mark the source
	// file as seen, we won't return to it when searching for imports.)
	if (exposedByParent) {
		return getSourceFileOfNode(scope)->asNode();
	}
	return scope; // TODO: GH#18217
}

// === functions on (*ls) ===

// getFileAndStartPosFromDeclaration — findallreferences.go:496.
std::pair<SourceFile*, TextPos> getFileAndStartPosFromDeclaration(
    Node* declaration) {
	SourceFile* file = getSourceFileOfNode(declaration);
	Node* name = orElse(getNameOfDeclaration(declaration), declaration);
	TextRange textRange = getRangeOfNode(name, file, /*endNode*/ nullptr);
	return {file, TextPos(textRange.pos())};
}

// getNonLocalDefinition — findallreferences.go:504.
nonLocalDefinition* LanguageService::getNonLocalDefinition(
    const gostd::Context& ctx, SymbolAndEntries* entry) {
	if (!entry->canUseDefinitionSymbol()) {
		return nullptr;
	}

	compiler::SimpleProgram* program = GetProgram();
	auto [checker, done] = program->GetTypeCheckerForFileExclusive(nullptr);
	doneGuard doneGuard_{done};
	checker::EmitResolver* emitResolver = checker->GetEmitResolver();
	for (auto* d : entry->definition->symbol->declarations) {
		if (isDefinitionVisible(emitResolver, d)) {
			auto [file, sp] = getFileAndStartPosFromDeclaration(d);
			TextPos startPos = sp;
			std::string fileName = file->FileName();
			auto [lspPosition, fidelity] =
			    converters->ToLSPPosition(file, startPos);
			if (fidelity.IsNone()) {
				continue;
			}
			auto* nld = new nonLocalDefinition{};
			nld->uri = lsconv::FileNameToDocumentURI(fileName);
			nld->pos = lspPosition;
			nld->GetSourcePosition = onceValue([this, fileName, startPos]() {
				sourcemap::DocumentPosition* mapped =
				    tryGetSourcePosition(fileName, startPos);
				if (mapped != nullptr) {
					auto [mappedPosition, mappedFidelity] =
					    converters->ToLSPPosition(
					        getScript(mapped->FileName),
					        TextPos(mapped->Pos));
					if (mappedFidelity.IsNone()) {
						return static_cast<position*>(nullptr);
					}
					auto* p = new position{};
					p->uri = lsconv::FileNameToDocumentURI(mapped->FileName);
					p->pos = mappedPosition;
					return p;
				}
				return static_cast<position*>(nullptr);
			});
			nld->GetGeneratedPosition =
			    onceValue([this, fileName, startPos]() {
				    sourcemap::DocumentPosition* mapped =
				        tryGetGeneratedPosition(fileName, startPos);
				    if (mapped != nullptr) {
					    auto [mappedPosition, mappedFidelity] =
					        converters->ToLSPPosition(
					            getScript(mapped->FileName),
					            TextPos(mapped->Pos));
					    if (mappedFidelity.IsNone()) {
						    return static_cast<position*>(nullptr);
					    }
					    auto* p = new position{};
					    p->uri =
					        lsconv::FileNameToDocumentURI(mapped->FileName);
					    p->pos = mappedPosition;
					    return p;
				    }
				    return static_cast<position*>(nullptr);
			    });
			return nld;
		}
	}
	return nullptr;
}

// isDefinitionVisible — findallreferences.go:561.
// This is special handling to determine if we should load up more projects and
// find location in other projects. By default arrows (and such other ast
// kinds) are not visible as declaration emitter doesnt need them. But we want
// to handle them specially so that they are visible if their parent is
// visible.
bool isDefinitionVisible(checker::EmitResolver* emitResolver,
                         Node* declaration) {
	if (emitResolver->IsDeclarationVisible(declaration)) {
		return true;
	}
	if (declaration->parent == nullptr) {
		return false;
	}

	// Variable initializers are visible if variable is visible
	if (hasInitializer(declaration->parent) &&
	    declaration->parent->initializer() == declaration) {
		return isDefinitionVisible(emitResolver, declaration->parent);
	}

	// Handle some exceptions here like arrow function, members of class and
	// object literal expression which are technically not visible but we want
	// the definition to be determined by its parent
	switch (declaration->kind) {
	case Kind::PropertyDeclaration:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::MethodDeclaration:
		// Private/protected properties/methods are not visible
		if (hasModifier(declaration, ModifierFlagsPrivate) ||
		    isPrivateIdentifier(declaration->name())) {
			return false;
		}
		// Public properties/methods are visible if its parents are visible, so:
		// falls through
		[[fallthrough]];
	case Kind::Constructor:
	case Kind::PropertyAssignment:
	case Kind::ShorthandPropertyAssignment:
	case Kind::ObjectLiteralExpression:
	case Kind::ClassExpression:
	case Kind::ArrowFunction:
	case Kind::FunctionExpression:
		return isDefinitionVisible(emitResolver, declaration->parent);
	default:
		return false;
	}
}

// forEachOriginalDefinitionLocation — findallreferences.go:600.
void LanguageService::forEachOriginalDefinitionLocation(
    const gostd::Context& ctx, SymbolAndEntries* entry,
    const std::function<void(lsproto::DocumentUri, lsproto::Position)>& cb) {
	if (!entry->canUseDefinitionSymbol()) {
		return;
	}

	compiler::SimpleProgram* program = GetProgram();
	for (auto* d : entry->definition->symbol->declarations) {
		auto [file, startPos] = getFileAndStartPosFromDeclaration(d);
		std::string fileName = file->FileName();
		if (tspath::isDeclarationFileName(fileName)) {
			// Map to ts position
			sourcemap::DocumentPosition* mapped =
			    tryGetSourcePosition(file->FileName(), startPos);
			if (mapped != nullptr) {
				auto [lspPosition, fidelity] = converters->ToLSPPosition(
				    getScript(mapped->FileName), TextPos(mapped->Pos));
				if (!fidelity.IsNone()) {
					cb(lsconv::FileNameToDocumentURI(mapped->FileName),
					   lspPosition);
				}
			}
		} else if (program->IsSourceFromProjectReference(
		               toPath(fileName))) {
			auto [lspPosition, fidelity] =
			    converters->ToLSPPosition(file, startPos);
			if (!fidelity.IsNone()) {
				cb(lsconv::FileNameToDocumentURI(fileName), lspPosition);
			}
		}
	}
}

// provideSymbolsAndEntries — findallreferences.go:644.
std::pair<SymbolAndEntriesData, bool> LanguageService::provideSymbolsAndEntries(
    const gostd::Context& ctx, lsproto::DocumentUri uri,
    lsproto::Position documentPosition, bool isRename, bool implementations) {
	// `findReferencedSymbols` except only computes the information needed to
	// return reference locations
	auto [program, sourceFile] = getProgramAndFile(uri);
	spanmap::Feature feature = spanmap::FeatureReferences;
	if (implementations) {
		feature = spanmap::FeatureImplementation;
	} else if (isRename) {
		feature = spanmap::FeatureRename;
	}
	auto positions = converters->FromLSPPositionForSourceFile(
	    sourceFile, documentPosition, feature);
	if (positions.empty()) {
		return {SymbolAndEntriesData{}, false};
	}
	SymbolAndEntriesData combined;
	bool ok = false;
	for (auto& mapped : positions) {
		if (!mapped.Fidelity.IsSingleSegment()) {
			continue;
		}
		auto [data, found] = provideSymbolsAndEntriesAtPosition(
		    ctx, program, mapped.Script_, int(mapped.Position), isRename,
		    implementations);
		if (!found) {
			continue;
		}
		if (!ok) {
			combined.OriginalNode = data.OriginalNode;
			combined.Position = data.Position;
			ok = true;
		}
		combined.SymbolsAndEntries.insert(combined.SymbolsAndEntries.end(),
		                                  data.SymbolsAndEntries.begin(),
		                                  data.SymbolsAndEntries.end());
	}
	return {combined, ok};
}

// provideSymbolsAndEntriesAtPosition — findallreferences.go:677.
std::pair<SymbolAndEntriesData, bool>
LanguageService::provideSymbolsAndEntriesAtPosition(
    const gostd::Context& ctx, compiler::SimpleProgram* program,
    SourceFile* sourceFile, int position, bool isRename,
    bool implementations) {
	Node* node = astnav::getTouchingPropertyName(sourceFile, position);
	if (isRename) {
		// Adjust modifier/keyword nodes to the declaration name, matching
		// Strada's findRenameLocations.
		node = getAdjustedLocation(node, /*forRename*/ true, sourceFile);
	}
	if ((isRename && !nodeIsEligibleForRename(node)) ||
	    (implementations && isSourceFile(node))) {
		return {SymbolAndEntriesData{node, {}, position}, false};
	}

	auto entries = getSymbolAndEntries(ctx, position, node, program, isRename,
	                                   implementations);
	if (!implementations) {
		return {SymbolAndEntriesData{node, entries, position}, true};
	}

	std::vector<SymbolAndEntries*> implementationEntries;
	std::deque<ReferenceEntry*> queue;
	collections::SyncSet<Node*> seenNodes;
	collections::SyncSet<Symbol*> seenDefinitions;
	auto addToQueue =
	    [&](std::vector<SymbolAndEntries*> symbolAndEntries) {
		    for (auto* s : symbolAndEntries) {
			    std::vector<ReferenceEntry*> newReferences;
			    for (auto* ref : s->references) {
				    if (seenNodes.AddIfAbsent(ref->node)) {
					    queue.push_back(ref);
					    newReferences.push_back(ref);
				    }
			    }
			    if (!newReferences.empty() || s->definition == nullptr ||
			        seenDefinitions.AddIfAbsent(s->definition->symbol)) {
				    implementationEntries.push_back(new SymbolAndEntries{
				        s->definition, newReferences});
			    }
		    }
	    };

	addToQueue(entries);
	while (!queue.empty()) {
		if (gostd::ctxErr(ctx) != nullptr) {
			return {SymbolAndEntriesData{}, false};
		}

		ReferenceEntry* entry = queue.front();
		queue.pop_front();
		if (entry->node != nullptr) {
			addToQueue(getSymbolAndEntries(ctx, entry->node->pos(), entry->node,
			                               program, isRename,
			                               implementations));
		}
	}
	return {SymbolAndEntriesData{node, implementationEntries, position}, true};
}

// getSymbolAndEntries — findallreferences.go:726.
std::vector<SymbolAndEntries*> LanguageService::getSymbolAndEntries(
    const gostd::Context& ctx, int position, Node* node,
    compiler::SimpleProgram* program, bool isRename, bool implementations) {
	refOptions options;
	if (!isRename) {
		options.use = referenceUseReferences;
		if (implementations) {
			options.implementations = true;
		}
	} else {
		options.use = referenceUseRename;
		options.useAliasesForRename =
		    tristateIsTrueOrUnknown(UserPreferences().UseAliasesForRename);
	}
	return getReferencedSymbolsForNode(ctx, position, node, program,
	                                 program->GetSourceFiles(), options);
}

// ProvideReferences — findallreferences.go:747.
std::pair<lsproto::ReferencesResponse, gostd::Error>
LanguageService::ProvideReferences(const gostd::Context& ctx,
                                   lsproto::ReferenceParams* params,
                                   CrossProjectOrchestrator* orchestrator) {
	return handleCrossProject<lsproto::ReferenceParams,
	                          lsproto::ReferencesResponse>(
	    ctx, params, orchestrator,
	    [](LanguageService* l, const gostd::Context& c,
	       lsproto::ReferenceParams* p, SymbolAndEntriesData d,
	       symbolEntryTransformOptions o) {
		    return l->symbolAndEntriesToReferences(c, p, d, o);
	    },
	    combineReferences, /*isRename*/ false, /*implementations*/ false,
	    symbolEntryTransformOptions{}, /*defaultProjectData*/ nullptr);
}

// provideReferencesFromData — findallreferences.go:761.
std::pair<lsproto::ReferencesResponse, gostd::Error>
LanguageService::provideReferencesFromData(
    const gostd::Context& ctx, lsproto::ReferenceParams* params,
    CrossProjectOrchestrator* orchestrator, SymbolAndEntriesData data) {
	return handleCrossProject<lsproto::ReferenceParams,
	                          lsproto::ReferencesResponse>(
	    ctx, params, orchestrator,
	    [](LanguageService* l, const gostd::Context& c,
	       lsproto::ReferenceParams* p, SymbolAndEntriesData d,
	       symbolEntryTransformOptions o) {
		    return l->symbolAndEntriesToReferences(c, p, d, o);
	    },
	    combineReferences, /*isRename*/ false, /*implementations*/ false,
	    symbolEntryTransformOptions{}, &data);
}

// ProvideVSReferences — findallreferences.go:775.
std::pair<lsproto::VSReferencesResponse, gostd::Error>
LanguageService::ProvideVSReferences(const gostd::Context& ctx,
                                     lsproto::ReferenceParams* params,
                                     CrossProjectOrchestrator* orchestrator) {
	return handleCrossProject<lsproto::ReferenceParams,
	                          lsproto::VSReferencesResponse>(
	    ctx, params, orchestrator,
	    [](LanguageService* l, const gostd::Context& c,
	       lsproto::ReferenceParams* p, SymbolAndEntriesData d,
	       symbolEntryTransformOptions o) {
		    return l->symbolAndEntriesToVSReferences(c, p, d, o);
	    },
	    combineVSReferences, /*isRename*/ false, /*implementations*/ false,
	    symbolEntryTransformOptions{}, /*defaultProjectData*/ nullptr);
}

// symbolAndEntriesToReferences — findallreferences.go:789.
std::pair<lsproto::ReferencesResponse, gostd::Error>
LanguageService::symbolAndEntriesToReferences(
    const gostd::Context& ctx, lsproto::ReferenceParams* params,
    SymbolAndEntriesData data, symbolEntryTransformOptions options) {
	// `findReferencedSymbols` except only computes the information needed to
	// return reference locations
	std::vector<lsproto::Location> locations;
	collections::Set<lsproto::Location> seenLocations;
	for (auto* symbol : data.SymbolsAndEntries) {
		auto symbolLocations = convertSymbolAndEntriesToLocations(
		    symbol, params->Context->IncludeDeclaration,
		    spanmap::FeatureReferences);
		locations = combineLocationArray<lsproto::Location>(
		    std::move(locations), &symbolLocations, &seenLocations);
	}
	return {lsproto::LocationsOrNull{&locations}, nullptr};
}

// symbolAndEntriesToVSReferences — findallreferences.go:800.
std::pair<lsproto::VSReferencesResponse, gostd::Error>
LanguageService::symbolAndEntriesToVSReferences(
    const gostd::Context& ctx, lsproto::ReferenceParams* params,
    SymbolAndEntriesData data, symbolEntryTransformOptions options) {
	const auto* caps = lsproto::GetClientCapabilities(ctx);
	bool vsCapability = caps->VSSupportsVisualStudioExtensions;
	std::vector<lsproto::VSReferenceItem*> items;
	int32_t id = 0;
	std::string projectName = projectID->String();

	for (auto* s : data.SymbolsAndEntries) {
		if (s->definition == nullptr) {
			continue;
		}

		// Convert definition to info
		auto* defInfo = definitionToReferencedSymbolDefinitionInfo(
		    ctx, s->definition, data.OriginalNode, vsCapability,
		    spanmap::FeatureReferences);
		if (defInfo == nullptr) {
			continue;
		}

		// Create the definition item
		int32_t definitionId = id;
		std::string emptyStr;
		auto* defItem = new lsproto::VSReferenceItem{
		    /*VSId*/ definitionId,
		    /*VSDefinitionId*/ nullptr,
		    /*VSKind*/ new std::vector<lsproto::VSReferenceKind>{
		        lsproto::VSReferenceKindUnknown},
		    /*VSLocation*/ defInfo->location,
		    /*VSDefinitionText*/ defInfo->displayText,
		    /*VSProjectName*/ new std::string(projectName),
		    /*VSContainingType*/ new std::string(emptyStr)};
		items.push_back(defItem);
		id++;

		// Create reference items grouped under the definition
		for (auto* ref : s->references) {
			// Skip the declaration itself (already represented by the
			// definition item)
			if (s->definition->symbol != nullptr &&
			    isDeclarationOfSymbol(ref->node, s->definition->symbol)) {
				continue;
			}

			auto [refLocation, ok] = getLocationOfEntryForFeature(
			    ref, spanmap::FeatureReferences);
			if (!ok) {
				continue;
			}

			// Determine read/write kind
			lsproto::VSReferenceKind kind = lsproto::VSReferenceKindRead;
			if (ref->kind != entryKindRange && ref->node != nullptr &&
			    isWriteAccessForReference(ref->node)) {
				kind = lsproto::VSReferenceKindWrite;
			}

			auto* refItem = new lsproto::VSReferenceItem{
			    /*VSId*/ id,
			    /*VSDefinitionId*/ new int32_t(definitionId),
			    /*VSKind*/ new std::vector<lsproto::VSReferenceKind>{kind},
			    /*VSLocation*/ refLocation};
			refItem->VSProjectName = new std::string(projectName);
			items.push_back(refItem);
			id++;
		}
	}

	return {lsproto::VSReferencesResponse{
	            new lsproto::VSReferenceItems(items)},
	        nullptr};
}

// definitionToReferencedSymbolDefinitionInfo — findallreferences.go:873.
// converts a Definition to display info
referencedSymbolDefinitionInfo*
LanguageService::definitionToReferencedSymbolDefinitionInfo(
    const gostd::Context& ctx, Definition* def, Node* originalNode,
    bool vsCapability, spanmap::Feature feature) {
	switch (def->Kind) {
	case definitionKindSymbol: {
		Symbol* symbol = def->symbol;
		if (symbol == nullptr) {
			return nullptr;
		}
		// Get display parts
		auto* element = getDefinitionKindAndDisplayParts(ctx, symbol,
		                                               originalNode,
		                                               vsCapability);

		// Get the definition node
		Node* node = nullptr;
		if (!symbol->declarations.empty()) {
			Node* decl = symbol->declarations[0];
			node = orElse(decl->name(), decl);
		} else {
			node = originalNode;
		}

		auto [loc, ok] = getLocationOfEntryForFeature(
		    new ReferenceEntry{entryKindNode, node}, feature);
		if (!ok) {
			return nullptr;
		}
		return new referencedSymbolDefinitionInfo{node, loc, element};
	}

	case definitionKindLabel: {
		Node* node = def->node;
		if (node == nullptr) {
			return nullptr;
		}
		auto [loc, ok] = getLocationOfEntryForFeature(
		    new ReferenceEntry{entryKindNode, node}, feature);
		if (!ok) {
			return nullptr;
		}
		return new referencedSymbolDefinitionInfo{
		    node, loc,
		    new lsproto::VSClassifiedTextElement{
		        /*Runs*/ {new lsproto::VSClassifiedTextRun{
		            /*ClassificationTypeName*/
		            lsproto::ClassificationTypeText, /*Text*/
		            node->text()}}}};
	}

	case definitionKindKeyword: {
		Node* node = def->node;
		if (node == nullptr) {
			return nullptr;
		}
		std::string name = std::string(tokenToString(node->kind));
		auto [loc, ok] = getLocationOfEntryForFeature(
		    new ReferenceEntry{entryKindNode, node}, feature);
		if (!ok) {
			return nullptr;
		}
		return new referencedSymbolDefinitionInfo{
		    node, loc,
		    new lsproto::VSClassifiedTextElement{
		        /*Runs*/ {new lsproto::VSClassifiedTextRun{
		            /*ClassificationTypeName*/
		            lsproto::ClassificationTypeKeyword, /*Text*/ name}}}};
	}

	case definitionKindThis: {
		Node* node = def->node;
		if (node == nullptr) {
			return nullptr;
		}
		Symbol* symbol = def->symbol;
		if (symbol == nullptr) {
			return nullptr;
		}
		auto* element =
		    getDefinitionKindAndDisplayParts(ctx, symbol, node, vsCapability);
		auto [loc, ok] = getLocationOfEntryForFeature(
		    new ReferenceEntry{entryKindNode, node}, feature);
		if (!ok) {
			return nullptr;
		}
		return new referencedSymbolDefinitionInfo{node, loc, element};
	}

	case definitionKindString: {
		Node* node = def->node;
		if (node == nullptr) {
			return nullptr;
		}
		auto [loc, ok] = getLocationOfEntryForFeature(
		    new ReferenceEntry{entryKindNode, node}, feature);
		if (!ok) {
			return nullptr;
		}
		return new referencedSymbolDefinitionInfo{
		    node, loc,
		    new lsproto::VSClassifiedTextElement{
		        /*Runs*/ {new lsproto::VSClassifiedTextRun{
		            /*ClassificationTypeName*/
		            lsproto::ClassificationTypeStringLiteral, /*Text*/
		            node->text()}}}};
	}

	case definitionKindTripleSlashReference: {
		if (def->tripleSlashFileRef == nullptr ||
		    def->tripleSlashFileRef->file == nullptr) {
			return nullptr;
		}
		Node* node = def->tripleSlashFileRef->file->asNode();
		auto [loc, ok] = getLocationOfEntryForFeature(
		    new ReferenceEntry{entryKindNode, node}, feature);
		if (!ok) {
			return nullptr;
		}
		return new referencedSymbolDefinitionInfo{
		    node, loc,
		    new lsproto::VSClassifiedTextElement{
		        /*Runs*/ {new lsproto::VSClassifiedTextRun{
		            /*ClassificationTypeName*/
		            lsproto::ClassificationTypeStringLiteral,
		            /*Text*/ "\"" +
		                def->tripleSlashFileRef->reference->FileName +
		                "\""}}}};
	}

	default:
		return nullptr;
	}
}

// getDefinitionKindAndDisplayParts — findallreferences.go:997.
// returns the classified display text for a symbol definition.
lsproto::VSClassifiedTextElement*
LanguageService::getDefinitionKindAndDisplayParts(
    const gostd::Context& ctx, Symbol* symbol, Node* originalNode,
    bool vsCapability) {
	compiler::SimpleProgram* program = GetProgram();
	auto [c, done] = program->GetTypeCheckerForFileExclusive(nullptr);
	doneGuard doneGuard_{done};

	SemanticMeaning meaning = getIntersectingMeaningFromDeclarations(
	    originalNode, symbol, SemanticMeaningAll);

	auto info = getQuickInfoAndDeclarationAtLocation(
	    c, symbol, originalNode, nullptr, vsCapability, meaning);

	if (vsCapability) {
		return new lsproto::VSClassifiedTextElement{
		    info.displayParts->GetRuns()};
	}
	// Fallback: single unclassified run with the full text
	std::string text = info.displayParts->String();
	return new lsproto::VSClassifiedTextElement{
	    /*Runs*/ {new lsproto::VSClassifiedTextRun{
	        /*ClassificationTypeName*/
	        lsproto::ClassificationTypeText, /*Text*/ text}}};
}

// ProvideImplementations — findallreferences.go:1016.
std::pair<lsproto::ImplementationResponse, gostd::Error>
LanguageService::ProvideImplementations(
    const gostd::Context& ctx, lsproto::ImplementationParams* params,
    CrossProjectOrchestrator* orchestrator) {
	return provideImplementationsEx(ctx, params, symbolEntryTransformOptions{},
	                                orchestrator);
}

// provideImplementationsEx — findallreferences.go:1020.
std::pair<lsproto::ImplementationResponse, gostd::Error>
LanguageService::provideImplementationsEx(
    const gostd::Context& ctx, lsproto::ImplementationParams* params,
    symbolEntryTransformOptions options,
    CrossProjectOrchestrator* orchestrator) {
	return handleCrossProject<lsproto::ImplementationParams,
	                          lsproto::ImplementationResponse>(
	    ctx, params, orchestrator,
	    [](LanguageService* l, const gostd::Context& c,
	       lsproto::ImplementationParams* p, SymbolAndEntriesData d,
	       symbolEntryTransformOptions o) {
		    return l->symbolAndEntriesToImplementations(c, p, d, o);
	    },
	    combineImplementations, /*isRename*/ false, /*implementations*/ true,
	    options, /*defaultProjectData*/ nullptr);
}

// provideImplementationsFromData — findallreferences.go:1034.
std::pair<lsproto::ImplementationResponse, gostd::Error>
LanguageService::provideImplementationsFromData(
    const gostd::Context& ctx, lsproto::ImplementationParams* params,
    symbolEntryTransformOptions options,
    CrossProjectOrchestrator* orchestrator, SymbolAndEntriesData data) {
	return handleCrossProject<lsproto::ImplementationParams,
	                          lsproto::ImplementationResponse>(
	    ctx, params, orchestrator,
	    [](LanguageService* l, const gostd::Context& c,
	       lsproto::ImplementationParams* p, SymbolAndEntriesData d,
	       symbolEntryTransformOptions o) {
		    return l->symbolAndEntriesToImplementations(c, p, d, o);
	    },
	    combineImplementations, /*isRename*/ false, /*implementations*/ true,
	    options, &data);
}

// symbolAndEntriesToImplementations — findallreferences.go:1048.
std::pair<lsproto::ImplementationResponse, gostd::Error>
LanguageService::symbolAndEntriesToImplementations(
    const gostd::Context& ctx, lsproto::ImplementationParams* params,
    SymbolAndEntriesData data, symbolEntryTransformOptions options) {
	collections::SyncSet<Node*> seenNodes;
	std::vector<ReferenceEntry*> entries;
	for (auto* entry : data.SymbolsAndEntries) {
		for (auto* ref : entry->references) {
			if (seenNodes.AddIfAbsent(ref->node) &&
			    (!options.dropOriginNodes ||
			     !ref->node->loc.containsInclusive(data.Position))) {
				entries.push_back(ref);
			}
		}
	}

	if (!options.requireLocationsResult &&
	    lsproto::GetClientCapabilities(ctx)
	        ->TextDocument.Implementation.LinkSupport) {
		auto links = convertEntriesToLocationLinks(
		    entries, spanmap::FeatureImplementation);
		return {lsproto::LocationOrLocationsOrDefinitionLinksOrNull{
		            nullptr, nullptr, &links},
		        nullptr};
	}
	auto locations = convertEntriesToLocations(
	    entries, spanmap::FeatureImplementation);
	return {lsproto::LocationOrLocationsOrDefinitionLinksOrNull{
	            nullptr, &locations},
	        nullptr};
}

// == functions for conversions ==

// convertSymbolAndEntriesToLocations — findallreferences.go:1067.
std::vector<lsproto::Location>
LanguageService::convertSymbolAndEntriesToLocations(
    SymbolAndEntries* s, bool includeDeclarations,
    spanmap::Feature feature) {
	std::vector<ReferenceEntry*> references = s->references;

	// !!! includeDeclarations
	if (!includeDeclarations && s->definition != nullptr) {
		Symbol* defSymbol = s->definition->symbol;
		references = filter(references, [&](ReferenceEntry* entry) {
			return !isDeclarationOfSymbol(entry->node, defSymbol);
		});
	}

	return convertEntriesToLocations(references, feature);
}

// isDeclarationOfSymbol — findallreferences.go:1080.
bool isDeclarationOfSymbol(Node* node, Symbol* target) {
	if (node == nullptr || target == nullptr) {
		return false;
	}

	Node* source = nullptr;
	if (Node* decl = getDeclarationFromName(node); decl != nullptr) {
		source = decl;
	} else if (node->kind == Kind::DefaultKeyword) {
		source = node->parent;
	} else if (isLiteralComputedPropertyDeclarationName(node)) {
		source = node->parent->parent;
	} else if (node->kind == Kind::ConstructorKeyword &&
	           isConstructorDeclaration(node->parent)) {
		source = node->parent->parent;
	}

	// !!!
	// const commonjsSource = source && isBinaryExpression(source) ? source.left
	// as unknown as Declaration : undefined;

	return source != nullptr &&
	       some(target->declarations,
	            [&](Node* decl) { return decl == source; });
}

// convertEntriesToLocations — findallreferences.go:1102.
std::vector<lsproto::Location> LanguageService::convertEntriesToLocations(
    const std::vector<ReferenceEntry*>& entries, spanmap::Feature feature) {
	std::vector<lsproto::Location> locations;
	locations.reserve(entries.size());
	for (auto* entry : entries) {
		auto [location, ok] = getLocationOfEntryForFeature(entry, feature);
		if (ok) {
			locations.push_back(location);
		}
	}
	return locations;
}

// convertEntriesToLocationLinks — findallreferences.go:1111.
std::vector<lsproto::LocationLink*>
LanguageService::convertEntriesToLocationLinks(
    const std::vector<ReferenceEntry*>& entries, spanmap::Feature feature) {
	std::vector<lsproto::LocationLink*> links;
	links.reserve(entries.size());
	for (auto* entry : entries) {

		// Get the selection range (the actual reference)
		auto [loc, ok] = getLocationOfEntryForFeature(entry, feature);
		if (!ok) {
			continue;
		}
		lsproto::Range targetSelectionRange = loc.Range_;
		lsproto::Range targetRange = targetSelectionRange;

		// For entries with nodes, compute ranges directly from the node
		if (entry->node != nullptr) {
			// Get the context range (broader scope including declaration
			// context)
			TextRange* contextTextRange = toContextRange(
			    entry->textRange, entry->sourceFile, entry->context);
			if (contextTextRange != nullptr) {
				auto [contextLocation, fidelity] =
				    sourceFileRangeToLSPLocationForFeature(
				        entry->sourceFile, *contextTextRange, feature);
				if (!fidelity.IsNone() &&
				    contextLocation.Uri == loc.Uri) {
					targetRange = contextLocation.Range_;
				}
			}
		}

		links.push_back(new lsproto::LocationLink{
		    /*OriginSelectionRange*/ nullptr,
		    /*TargetUri*/ lsconv::FileNameToDocumentURI(
		        entry->sourceFile->OriginalFileName()),
		    /*TargetRange*/ targetRange,
		    /*TargetSelectionRange*/ targetSelectionRange});
	}
	return links;
}

// mergeReferences — findallreferences.go:1141.
std::vector<SymbolAndEntries*> LanguageService::mergeReferences(
    compiler::SimpleProgram* program,
    const std::vector<std::vector<SymbolAndEntries*>>& referencesToMerge) {
	std::vector<SymbolAndEntries*> result;
	auto getSourceFileIndexOfEntry = [&](ReferenceEntry* entry) {
		resolveEntrySource(entry);
		return indexOf(program->SourceFiles(), entry->sourceFile);
	};

	for (auto references : referencesToMerge) {
		if (references.empty()) {
			continue;
		}
		if (result.empty()) {
			result = references;
			continue;
		}
		for (auto* entry : references) {
			if (entry->definition == nullptr ||
			    entry->definition->Kind != definitionKindSymbol) {
				result.push_back(entry);
				continue;
			}
			Symbol* symbol = entry->definition->symbol;
			int refIndex = findIndex(result, [&](SymbolAndEntries* ref) {
				return ref->definition != nullptr &&
				       ref->definition->Kind == definitionKindSymbol &&
				       ref->definition->symbol == symbol;
			});
			if (refIndex == -1) {
				result.push_back(entry);
				continue;
			}

			SymbolAndEntries* reference = result[refIndex];
			std::vector<ReferenceEntry*> sortedRefs = reference->references;
			sortedRefs.insert(sortedRefs.end(), entry->references.begin(),
			                  entry->references.end());
			std::stable_sort(
			    sortedRefs.begin(), sortedRefs.end(),
			    [&](ReferenceEntry* entry1, ReferenceEntry* entry2) {
				    int entry1File = getSourceFileIndexOfEntry(entry1);
				    int entry2File = getSourceFileIndexOfEntry(entry2);
				    if (entry1File != entry2File) {
					    return entry1File < entry2File;
				    }

				    return lsproto::CompareRanges(
				               getRangeOfEntry(entry1),
				               getRangeOfEntry(entry2)) < 0;
			    });
			result[refIndex] = new SymbolAndEntries{reference->definition,
			                                        sortedRefs};
		}
	}
	return result;
}

// GetReferencedSymbolsForNode — findallreferences.go:1198.
// Returns all referenced symbols and their reference entries for the given
// node across the provided source files.
std::vector<SymbolAndEntries*> LanguageService::GetReferencedSymbolsForNode(
    const gostd::Context& ctx, int position, Node* node,
    const std::vector<SourceFile*>& sourceFiles) {
	refOptions options;
	options.use = referenceUseReferences;
	return getReferencedSymbolsForNode(ctx, position, node, program,
	                                 sourceFiles, options);
}

// GetSignatureUsages — findallreferences.go:1216.
// Returns all usages of a signature declaration as name-call pairs. For each
// reference to the signature's name, it returns the reference node and the
// call expression it appears in (nil if the reference is not in a call
// position).
std::vector<SignatureUsage> LanguageService::GetSignatureUsages(
    const gostd::Context& ctx, Node* signatureDecl) {
	Node* name = signatureDecl->name();
	if (name == nullptr || !isIdentifier(name)) {
		return {};
	}

	auto sourceFiles = program->GetSourceFiles();
	auto entries =
	    GetReferencedSymbolsForNode(ctx, name->pos(), name, sourceFiles);

	// Collect all declaration name nodes for the target symbol so we can
	// filter them out — the caller wants usages, not declarations.
	std::unordered_map<Node*, bool> declNames;
	for (auto* entry : entries) {
		if (entry->definition != nullptr &&
		    entry->definition->symbol != nullptr) {
			for (auto* decl : entry->definition->symbol->declarations) {
				if (Node* n = decl->name(); n != nullptr) {
					declNames[n] = true;
				}
			}
		}
	}

	std::vector<SignatureUsage> result;
	for (auto* entry : entries) {
		for (auto* ref : entry->References()) {
			if (!ref->IsNodeEntry()) {
				continue;
			}
			Node* node = ref->Node_();
			if (node == nullptr || declNames.count(node)) {
				continue;
			}

			Node* called = climbPastPropertyAccess(node);

			Node* callExpr = nullptr;
			if (called->parent != nullptr &&
			    isCallExpression(called->parent) &&
			    called->parent->expression() == called) {
				callExpr = called->parent;
			}

			result.push_back(SignatureUsage{node, callExpr});
		}
	}
	return result;
}

// === functions for find all ref implementation ===

// getReferencedSymbolsForNode — findallreferences.go:1265.
std::vector<SymbolAndEntries*> LanguageService::getReferencedSymbolsForNode(
    const gostd::Context& ctx, int position, Node* node,
    compiler::SimpleProgram* program,
    const std::vector<SourceFile*>& sourceFiles, refOptions options) {
	// !!! cancellationToken
	collections::Set<std::string> sourceFilesSet;
	for (auto* file : sourceFiles) {
		sourceFilesSet.Add(file->FileName());
	}

	if (options.use == referenceUseReferences ||
	    options.use == referenceUseRename) {
		node = getAdjustedLocation(node, options.use == referenceUseRename,
		                           getSourceFileOfNode(node));
	}

	auto [checker, done] = program->GetTypeCheckerForFileExclusive(nullptr);
	doneGuard doneGuard_{done};

	if (node->kind == Kind::SourceFile) {
		refInfo* resolvedRef =
		    getReferenceAtPosition(node->as<SourceFile>(), position, program);
		if (resolvedRef == nullptr || resolvedRef->file == nullptr) {
			return {};
		}

		if (Symbol* moduleSymbol = checker->GetMergedSymbol(
		        resolvedRef->file->Symbol);
		    moduleSymbol != nullptr) {
			return getReferencedSymbolsForModule(
			    ctx, program, moduleSymbol,
			    /*excludeImportTypeOfExportEquals*/ false, sourceFiles,
			    &sourceFilesSet);
		}

		// !!! not implemented
		// fileIncludeReasons := program.getFileIncludeReasons();
		// if (!fileIncludeReasons) {
		// 	return nil
		// }
		return {new SymbolAndEntries{
		    new Definition{definitionKindTripleSlashReference, nullptr,
		                   nullptr,
		                   new tripleSlashDefinition{resolvedRef->reference,
		                                             nullptr}},
		    getReferencesForNonModule(resolvedRef->file,
		                              program /*fileIncludeReasons,*/)}};
	}

	if (!options.implementations) {
		// !!! cancellationToken
		if (auto special = getReferencedSymbolsSpecial(node, sourceFiles);
		    !special.empty()) {
			return special;
		}
	}

	// constructors should use the class symbol, detected by name, if present
	Symbol* symbol = checker->GetSymbolAtLocation(ifElse(
	    node->kind == Kind::Constructor && node->parent->name() != nullptr,
	    node->parent->name(), node));
	// Could not find a symbol e.g. unknown identifier
	if (symbol == nullptr) {
		// String literal might be a property (and thus have a symbol), so do
		// this here rather than in getReferencedSymbolsSpecial.
		if (!options.implementations && isStringLiteralLike(node)) {
			if (isModuleSpecifierLike(node)) {
				// !!! not implemented
				// fileIncludeReasons := program.GetFileIncludeReasons()
				// if referencedFile :=
				// program.GetResolvedModuleFromModuleSpecifier(node, nil
				// /*sourceFile*/); referencedFile != nil { return
				// []*SymbolAndEntries{{ definition: &Definition{Kind:
				// definitionKindString, node: node}, references:
				// getReferencesForNonModule(referencedFile, program
				// /*fileIncludeReasons,*/), }} }
				// Fall through to string literal references. This is not very
				// likely to return anything useful, but I guess it's better
				// than nothing, and there's an existing test that expects this
				// to happen (fourslash/cases/untypedModuleImport.ts).
			}
			return getReferencesForStringLiteral(ctx, node, sourceFiles,
			                                     checker);
		}
		return {};
	}

	if (symbol->name == InternalSymbolNameExportEquals) {
		if (symbol->parent == nullptr) {
			return {};
		}
		return getReferencedSymbolsForModule(
		    ctx, program, symbol->parent,
		    /*excludeImportTypeOfExportEquals*/ false, sourceFiles,
		    &sourceFilesSet);
	}

	auto moduleReferences = getReferencedSymbolsForModuleIfDeclaredBySourceFile(
	    ctx, symbol, program, sourceFiles, checker, options, &sourceFilesSet);
	if (!moduleReferences.empty() &&
	    !(symbol->flags & SymbolFlagsTransient)) {
		return moduleReferences;
	}

	Symbol* aliasedSymbol =
	    getMergedAliasedSymbolOfNamespaceExportDeclaration(node, symbol,
	                                                       checker);
	auto moduleReferencesOfExportTarget =
	    getReferencedSymbolsForModuleIfDeclaredBySourceFile(
	        ctx, aliasedSymbol, program, sourceFiles, checker, options,
	        &sourceFilesSet);

	auto references = getReferencedSymbolsForSymbol(
	    ctx, program, symbol, node, sourceFiles, &sourceFilesSet, checker,
	    options);
	return mergeReferences(program, {moduleReferences, references,
	                                 moduleReferencesOfExportTarget});
}

// getReferencesForStringLiteral — findallreferences.go:1340.
std::vector<SymbolAndEntries*> LanguageService::getReferencesForStringLiteral(
    const gostd::Context& ctx, Node* node,
    const std::vector<SourceFile*>& sourceFiles, checker::Checker* checker) {
	checker::Type* t =
	    getContextualTypeFromParentOrAncestorTypeNode(node, checker);
	std::string nodeText = node->text();
	auto references = flatMap(
	    sourceFiles, [&](SourceFile* sourceFile) -> std::vector<ReferenceEntry*> {
		    if (gostd::ctxErr(ctx) != nullptr) {
			    return {};
		    }
		    std::vector<ReferenceEntry*> entries;
		    auto possibleReferences = getPossibleSymbolReferenceNodes(
		        sourceFile, nodeText, /*container*/ nullptr);
		    for (auto* ref : possibleReferences) {
			    if (isStringLiteralLike(ref) && ref->text() == nodeText) {
				    if (t != nullptr) {
					    checker::Type* refType =
					        getContextualTypeFromParentOrAncestorTypeNode(
					            ref, checker);
					    if (t != checker->GetStringType() &&
					        (t == refType ||
					         isStringLiteralPropertyReference(ref,
					                                          checker))) {
						    entries.push_back(newNodeEntryWithKind(
						        ref, entryKindStringLiteral));
					    }
				    } else {
					    if (tsc::isNoSubstitutionTemplateLiteral(ref) &&
					        !printer::RangeIsOnSingleLine(ref->loc,
					                                      sourceFile)) {
						    continue;
					    }
					    entries.push_back(newNodeEntryWithKind(
					        ref, entryKindStringLiteral));
				    }
			    }
		    }
		    return entries;
	    });

	return {new SymbolAndEntries{
	    new Definition{definitionKindString, nullptr, node, nullptr},
	    references}};
}

// isStringLiteralPropertyReference — findallreferences.go:1372.
bool isStringLiteralPropertyReference(Node* node, checker::Checker* checker) {
	if (isPropertySignatureDeclaration(node->parent)) {
		return checker->GetPropertyOfType(
		           checker->GetTypeAtLocation(node->parent->parent),
		           node->text()) != nullptr;
	}
	return false;
}

// getReferencedSymbolsForModuleIfDeclaredBySourceFile —
// findallreferences.go:1381.
std::vector<SymbolAndEntries*>
LanguageService::getReferencedSymbolsForModuleIfDeclaredBySourceFile(
    const gostd::Context& ctx, Symbol* symbol, compiler::SimpleProgram* program,
    const std::vector<SourceFile*>& sourceFiles, checker::Checker* checker,
    refOptions options,
    collections::Set<std::string>* sourceFilesSet) {
	std::string moduleSourceFileName;
	if (symbol == nullptr ||
	    !((symbol->flags & SymbolFlagsModule) &&
	      !symbol->declarations.empty())) {
		return {};
	}
	if (Node* moduleSourceFile =
	        find(symbol->declarations,
	             [](Node* d) { return isSourceFile(d); });
	    moduleSourceFile != nullptr) {
		moduleSourceFileName =
		    moduleSourceFile->as<SourceFile>()->FileName();
	} else {
		return {};
	}
	auto exportIt = symbol->exports.find(InternalSymbolNameExportEquals);
	Symbol* exportEquals =
	    exportIt != symbol->exports.end() ? exportIt->second : nullptr;
	// If exportEquals != nil, we're about to add references to `import("mod")`
	// anyway, so don't double-count them.
	auto moduleReferences = getReferencedSymbolsForModule(
	    ctx, program, symbol, exportEquals != nullptr, sourceFiles,
	    sourceFilesSet);
	if (exportEquals == nullptr || !(exportEquals->flags & SymbolFlagsAlias) ||
	    !sourceFilesSet->Has(moduleSourceFileName)) {
		return moduleReferences;
	}
	symbol = checker->ResolveAlias(exportEquals).first;
	return mergeReferences(
	    program, {moduleReferences,
	              getReferencedSymbolsForSymbol(ctx, program, symbol, /*node*/
	                                            nullptr, sourceFiles,
	                                            sourceFilesSet, checker,
	                                            options)});
}

// getReferencedSymbolsSpecial — findallreferences.go:1402.
std::vector<SymbolAndEntries*> getReferencedSymbolsSpecial(
    Node* node, const std::vector<SourceFile*>& sourceFiles) {
	if (isTypeKeyword(node->kind)) {
		// A void expression (i.e., `void foo()`) is not special, but the `void`
		// type is.
		if (node->kind == Kind::VoidKeyword &&
		    node->parent->kind == Kind::VoidExpression) {
			return {};
		}

		// A modifier readonly (like on a property declaration) is not special;
		// a readonly type keyword (like `readonly string[]`) is.
		if (node->kind == Kind::ReadonlyKeyword &&
		    !isReadonlyTypeOperator(node)) {
			return {};
		}
		// Likewise, when we *are* looking for a special keyword, make sure we
		// *don't* include readonly member modifiers.
		return getAllReferencesForKeyword(
		    sourceFiles, node->kind,
		    // cancellationToken,
		    node->kind == Kind::ReadonlyKeyword);
	}

	if (isImportMeta(node->parent) && node->parent->name() == node) {
		return getAllReferencesForImportMeta(sourceFiles);
	}

	if (node->kind == Kind::StaticKeyword &&
	    node->parent->kind == Kind::ClassStaticBlockDeclaration) {
		return {new SymbolAndEntries{
		    new Definition{definitionKindKeyword, nullptr, node, nullptr},
		    {newNodeEntry(node)}}};
	}

	// Labels
	if (isJumpStatementTarget(node)) {
		// if we have a label definition, look within its statement for
		// references, if not, then the label is undefined and we have no
		// results..
		if (Node* labelDefinition = getTargetLabel(node->parent, node->text());
		    labelDefinition != nullptr) {
			return getLabelReferencesInNode(labelDefinition->parent,
			                              labelDefinition);
		}
		return {};
	}

	if (isLabelOfLabeledStatement(node)) {
		// it is a label definition and not a target, search within the parent
		// labeledStatement
		return getLabelReferencesInNode(node->parent, node);
	}

	if (isThis(node)) {
		return getReferencesForThisKeyword(node, sourceFiles);
	}

	if (node->kind == Kind::SuperKeyword) {
		return getReferencesForSuperKeyword(node);
	}

	return {};
}

// getLabelReferencesInNode — findallreferences.go:1442.
std::vector<SymbolAndEntries*> getLabelReferencesInNode(Node* container,
                                                      Node* targetLabel) {
	SourceFile* sourceFile = getSourceFileOfNode(container);
	std::string labelName = targetLabel->text();
	auto references = mapNonNil(
	    getPossibleSymbolReferenceNodes(sourceFile, labelName, container),
	    [&](Node* node) -> ReferenceEntry* {
		    // Only pick labels that are either the target label, or have a
		    // target that is the target label
		    if (node == targetLabel ||
		        (isJumpStatementTarget(node) &&
		         getTargetLabel(node, labelName) == targetLabel)) {
			    return newNodeEntry(node);
		    }
		    return nullptr;
	    });
	return {NewSymbolAndEntries(definitionKindLabel, targetLabel, nullptr,
	                            references)};
}

// getReferencesForThisKeyword — findallreferences.go:1454.
std::vector<SymbolAndEntries*> getReferencesForThisKeyword(
    Node* thisOrSuperKeyword, const std::vector<SourceFile*>& sourceFiles) {
	Node* searchSpaceNode =
	    getThisContainer(thisOrSuperKeyword,
	                     /*includeArrowFunctions*/ false,
	                     /*includeClassComputedPropertyName*/ false);

	// Whether 'this' occurs in a static context within a class.
	ModifierFlags staticFlag = ModifierFlagsStatic;
	auto isParameterName = [](Node* node) {
		return node->kind == Kind::Identifier &&
		       node->parent->kind == Kind::Parameter &&
		       node->parent->name() == node;
	};

	switch (searchSpaceNode->kind) {
	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
	case Kind::PropertyDeclaration:
	case Kind::PropertySignature:
	case Kind::Constructor:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
		if ((searchSpaceNode->kind == Kind::MethodDeclaration ||
		     searchSpaceNode->kind == Kind::MethodSignature) &&
		    isObjectLiteralMethod(searchSpaceNode)) {
			staticFlag &= searchSpaceNode->modifierFlags();
			searchSpaceNode = searchSpaceNode->parent; // re-assign to be the
			                                         // owning object literals
			break;
		}
		staticFlag &= searchSpaceNode->modifierFlags();
		searchSpaceNode = searchSpaceNode->parent; // re-assign to be the owning
		                                         // class
		break;
	case Kind::SourceFile:
		if (isExternalModule(searchSpaceNode->as<SourceFile>()) ||
		    isParameterName(thisOrSuperKeyword)) {
			return {};
		}
		break;
	case Kind::FunctionDeclaration:
	case Kind::FunctionExpression:
		// Computed properties in classes are not handled here because
		// references to this are illegal, so there is no point finding
		// references to them.
		break;
	default:
		return {};
	}

	std::vector<SourceFile*> filesToSearch = sourceFiles;
	if (searchSpaceNode->kind != Kind::SourceFile) {
		filesToSearch = {getSourceFileOfNode(searchSpaceNode)};
	}
	auto references = mapSlice(
	    flatMap(filesToSearch, [&](SourceFile* sourceFile) {
		    // cancellationToken.throwIfCancellationRequested();
		    return filter(
		        getPossibleSymbolReferenceNodes(
		            sourceFile, "this",
		            ifElse(searchSpaceNode->kind == Kind::SourceFile,
		                   sourceFile->asNode(), searchSpaceNode)),
		        [&](Node* node) {
			        if (!isThis(node)) {
				        return false;
			        }
			        Node* container = getThisContainer(
			            node, /*includeArrowFunctions*/ false,
			            /*includeClassComputedPropertyName*/ false);
			        if (!canHaveSymbol(container)) {
				        return false;
			        }
			        switch (searchSpaceNode->kind) {
			        case Kind::FunctionExpression:
			        case Kind::FunctionDeclaration:
				        return searchSpaceNode->symbol() ==
				               container->symbol();
			        case Kind::MethodDeclaration:
			        case Kind::MethodSignature:
				        return isObjectLiteralMethod(searchSpaceNode) &&
				               searchSpaceNode->symbol() ==
				                   container->symbol();
			        case Kind::ClassExpression:
			        case Kind::ClassDeclaration:
			        case Kind::ObjectLiteralExpression:
				        // Make sure the container belongs to the same
				        // class/object literals and has the appropriate
				        // static modifier from the original container.
				        return container->parent != nullptr &&
				               canHaveSymbol(container->parent) &&
				               searchSpaceNode->symbol() ==
				                   container->parent->symbol() &&
				               isStatic(container) ==
				                   (staticFlag != ModifierFlagsNone);
			        case Kind::SourceFile:
				        return container->kind == Kind::SourceFile &&
				               !isExternalModule(
				                   container->as<SourceFile>()) &&
				               !isParameterName(node);
			        }
			        return false;
		        });
	    }),
	    [](Node* n) { return static_cast<ReferenceEntry*>(newNodeEntry(n)); });

	Node* thisParameter =
	    firstNonNil(references, [](ReferenceEntry* ref) -> Node* {
		    if (ref->node->parent->kind == Kind::Parameter) {
			    return ref->node;
		    }
		    return nullptr;
	    });
	if (thisParameter == nullptr) {
		thisParameter = thisOrSuperKeyword;
	}
	return {NewSymbolAndEntries(definitionKindThis, thisParameter,
	                            searchSpaceNode->symbol(), references)};
}

// getReferencesForSuperKeyword — findallreferences.go:1515.
std::vector<SymbolAndEntries*> getReferencesForSuperKeyword(
    Node* superKeyword) {
	Node* searchSpaceNode =
	    getSuperContainer(superKeyword, /*stopOnFunctions*/ false);
	if (searchSpaceNode == nullptr) {
		return {};
	}
	// Whether 'super' occurs in a static context within a class.
	ModifierFlags staticFlag = ModifierFlagsStatic;

	switch (searchSpaceNode->kind) {
	case Kind::PropertyDeclaration:
	case Kind::PropertySignature:
	case Kind::MethodDeclaration:
	case Kind::MethodSignature:
	case Kind::Constructor:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
		staticFlag &= searchSpaceNode->modifierFlags();
		searchSpaceNode = searchSpaceNode->parent; // re-assign to be the owning
		                                         // class
		break;
	default:
		return {};
	}

	SourceFile* sourceFile = getSourceFileOfNode(searchSpaceNode);
	auto references = mapNonNil(
	    getPossibleSymbolReferenceNodes(sourceFile, "super", searchSpaceNode),
	    [&](Node* node) -> ReferenceEntry* {
		    if (node->kind != Kind::SuperKeyword) {
			    return nullptr;
		    }

		    Node* container =
		        getSuperContainer(node, /*stopOnFunctions*/ false);

		    // If we have a 'super' container, we must have an enclosing class.
		    // Now make sure the owning class is the same as the search-space
		    // and has the same static qualifier as the original 'super's
		    // owner.
		    if (container != nullptr &&
		        isStatic(container) == (staticFlag != ModifierFlagsNone) &&
		        container->parent->symbol() == searchSpaceNode->symbol()) {
			    return newNodeEntry(node);
		    }
		    return nullptr;
	    });

	return {NewSymbolAndEntries(definitionKindSymbol, nullptr,
	                            searchSpaceNode->symbol(), references)};
}

// getAllReferencesForImportMeta — findallreferences.go:1548.
std::vector<SymbolAndEntries*> getAllReferencesForImportMeta(
    const std::vector<SourceFile*>& sourceFiles) {
	auto references = flatMap(
	    sourceFiles, [&](SourceFile* sourceFile) -> std::vector<ReferenceEntry*> {
		    return mapNonNil(
		        getPossibleSymbolReferenceNodes(sourceFile, "meta",
		                                        sourceFile->asNode()),
		        [](Node* node) -> ReferenceEntry* {
			        Node* parent = node->parent;
			        if (isImportMeta(parent)) {
				        return newNodeEntry(parent);
			        }
			        return nullptr;
		        });
	    });
	if (references.empty()) {
		return {};
	}
	return {new SymbolAndEntries{
	    new Definition{definitionKindKeyword, nullptr, references[0]->node,
	                   nullptr},
	    references}};
}

// getAllReferencesForKeyword — findallreferences.go:1564.
std::vector<SymbolAndEntries*> getAllReferencesForKeyword(
    const std::vector<SourceFile*>& sourceFiles, Kind keywordKind,
    bool filterReadOnlyTypeOperator) {
	// references is a list of NodeEntry
	std::string keywordText = std::string(tokenToString(keywordKind));
	auto references = flatMap(
	    sourceFiles, [&](SourceFile* sourceFile) -> std::vector<ReferenceEntry*> {
		    // cancellationToken.throwIfCancellationRequested();
		    return mapNonNil(
		        getPossibleSymbolReferenceNodes(sourceFile, keywordText,
		                                        sourceFile->asNode()),
		        [&](Node* referenceLocation) -> ReferenceEntry* {
			        if (referenceLocation->kind == keywordKind &&
			            (!filterReadOnlyTypeOperator ||
			             isReadonlyTypeOperator(referenceLocation))) {
				        return newNodeEntry(referenceLocation);
			        }
			        return nullptr;
		        });
	    });
	if (references.empty()) {
		return {};
	}
	return {NewSymbolAndEntries(definitionKindKeyword, references[0]->node,
	                            nullptr, references)};
}

// getPossibleSymbolReferenceNodes — findallreferences.go:1637.
std::vector<Node*> getPossibleSymbolReferenceNodes(SourceFile* sourceFile,
                                                 const std::string& symbolName,
                                                 Node* container) {
	return mapNonNil(
	    getPossibleSymbolReferencePositions(sourceFile, symbolName, container),
	    [&](int pos) -> Node* {
		    if (Node* referenceLocation =
		            astnav::getTouchingPropertyName(sourceFile, pos);
		        referenceLocation != sourceFile->asNode()) {
			    return referenceLocation;
		    }
		    return nullptr;
	    });
}

// getPossibleSymbolReferencePositions — findallreferences.go:1646.
std::vector<int> getPossibleSymbolReferencePositions(
    SourceFile* sourceFile, const std::string& symbolName, Node* container) {
	std::vector<int> positions;

	/// TODO: Cache symbol existence for files to save text search
	// Also, need to make this work for unicode escapes.

	// Be resilient in the face of a symbol with no name or zero length name
	if (symbolName.empty()) {
		return positions;
	}

	std::string_view text = sourceFile->Text();
	int sourceLength = (int)text.size();
	int symbolNameLength = (int)symbolName.size();

	if (container == nullptr) {
		container = sourceFile->asNode();
	}

	// Go: strings.Index(text[container.Pos():], symbolName) — index relative to
	// the suffix slice (faithful, including for containers where pos > 0).
	auto findInText = [&](int start) -> int {
		if (start > sourceLength) {
			return -1;
		}
		auto idx = text.substr(start).find(symbolName);
		return idx == std::string_view::npos ? -1 : int(idx);
	};
	int position = findInText(container->pos());
	int endPos = container->end();
	while (position >= 0 && position < endPos) {
		// We found a match.  Make sure it's not part of a larger word (i.e. the
		// char before and after it have to be a non-identifier char).
		int endPosition = position + symbolNameLength;

		if ((position == 0 || !isIdentifierPart(text[position - 1])) &&
		    (endPosition == sourceLength ||
		     !isIdentifierPart(text[endPosition]))) {
			// Found a real match.  Keep searching.
			positions.push_back(position);
		}
		int startIndex = position + symbolNameLength + 1;
		if (startIndex > sourceLength) {
			break;
		}
		int foundIndex = findInText(startIndex);
		if (foundIndex != -1) {
			position = startIndex + foundIndex;
		} else {
			break;
		}
	}

	return positions;
}

// findFirstJsxNode — findallreferences.go:1692.
// recursively searches for the first JSX element, self-closing element, or
// fragment
Node* findFirstJsxNode(Node* root) {
	std::function<Node*(Node*)> visit = [&](Node* node) -> Node* {
		// Check if this is a JSX node we're looking for
		switch (node->kind) {
		case Kind::JsxElement:
		case Kind::JsxSelfClosingElement:
		case Kind::JsxFragment:
			return node;
		default:
			break;
		}

		// Skip subtree if it doesn't contain JSX
		if (!(node->subtreeFacts() & SubtreeContainsJsx)) {
			return nullptr;
		}

		// Traverse children to find JSX node
		Node* result = nullptr;
		node->forEachChild([&](Node* child) {
			result = visit(child);
			return result != nullptr; // Stop if found
		});
		return result;
	};

	return visit(root);
}

// getReferencesForNonModule — findallreferences.go:1718.
std::vector<ReferenceEntry*> getReferencesForNonModule(
    SourceFile* referencedFile, compiler::SimpleProgram* program) {
	// !!! not implemented
	return {};
}

// getMergedAliasedSymbolOfNamespaceExportDeclaration —
// findallreferences.go:1723.
Symbol* getMergedAliasedSymbolOfNamespaceExportDeclaration(
    Node* node, Symbol* symbol, checker::Checker* checker) {
	if (node->parent != nullptr &&
	    node->parent->kind == Kind::NamespaceExportDeclaration) {
		if (auto [aliasedSymbol, ok] = checker->ResolveAlias(symbol); ok) {
			Symbol* targetSymbol = checker->GetMergedSymbol(aliasedSymbol);
			if (aliasedSymbol != targetSymbol) {
				return targetSymbol;
			}
		}
	}
	return nullptr;
}

// getReferencedSymbolsForModule — findallreferences.go:1735.
std::vector<SymbolAndEntries*>
LanguageService::getReferencedSymbolsForModule(
    const gostd::Context& ctx, compiler::SimpleProgram* program,
    Symbol* symbol, bool excludeImportTypeOfExportEquals,
    const std::vector<SourceFile*>& sourceFiles,
    collections::Set<std::string>* sourceFilesSet) {
	TSC_ASSERT(symbol->valueDeclaration != nullptr, "");

	auto [checker, done] = program->GetTypeCheckerForFileExclusive(nullptr);
	doneGuard doneGuard_{done};

	auto moduleRefs =
	    findModuleReferences(program, sourceFiles, symbol, checker);
	auto references = mapNonNil(moduleRefs, [&](const ModuleReference& reference)
	                              -> ReferenceEntry* {
		switch (reference.kind) {
		case ModuleReferenceKindImport: {
			Node* parent = reference.literal->parent;
			if (isLiteralTypeNode(parent)) {
				Node* importType = parent->parent;
				if (isImportTypeNode(importType)) {
					auto* importTypeNode = importType->as<ImportTypeNode>();
					if (excludeImportTypeOfExportEquals &&
					    importTypeNode->Qualifier == nullptr) {
						return nullptr;
					}
				}
			}
			// import("foo") with no qualifier will reference the `export =` of
			// the module, which may be referenced anyway.
			return newNodeEntry(reference.literal);
		}
		case ModuleReferenceKindImplicit: {
			// For implicit references (e.g., JSX runtime imports), return the
			// first JSX node, the first statement, or the whole file
			Node* rangeNode = nullptr;

			// Skip the JSX search for tslib imports
			if (reference.literal->text() != "tslib") {
				rangeNode =
				    findFirstJsxNode(reference.referencingFile->asNode());
			}

			if (rangeNode == nullptr) {
				if (reference.referencingFile->Statements != nullptr &&
				    !reference.referencingFile->Statements->nodes.empty()) {
					rangeNode =
					    reference.referencingFile->Statements->nodes[0];
				} else {
					rangeNode = reference.referencingFile->asNode();
				}
			}
			return newNodeEntry(rangeNode);
		}
		case ModuleReferenceKindReference:
			return new ReferenceEntry{
			    /*kind*/ entryKindRange, /*node*/ nullptr,
			    /*context*/ nullptr,
			    /*sourceFile*/ reference.referencingFile,
			    /*textRange*/ reference.ref};
		}
		return nullptr;
	});

	// Add references to the module declarations themselves
	if (!symbol->declarations.empty()) {
		for (auto* decl : symbol->declarations) {
			switch (decl->kind) {
			case Kind::SourceFile:
				// Don't include the source file itself. (This may not be ideal
				// behavior, but awkward to include an entire file as a
				// reference.)
				continue;
			case Kind::ModuleDeclaration:
				if (sourceFilesSet->Has(
				        getSourceFileOfNode(decl)->FileName())) {
					references.push_back(
					    newNodeEntry(decl->as<ModuleDeclaration>()->name));
				}
				break;
			default:
				// This may be merged with something (e.g. a class merged with
				// a namespace).
				continue;
			}
		}
	}

	// Handle export equals declarations
	auto exportedIt = symbol->exports.find(InternalSymbolNameExportEquals);
	Symbol* exported =
	    exportedIt != symbol->exports.end() ? exportedIt->second : nullptr;
	if (exported != nullptr && !exported->declarations.empty()) {
		for (auto* decl : exported->declarations) {
			SourceFile* sourceFile = getSourceFileOfNode(decl);
			if (sourceFilesSet->Has(sourceFile->FileName())) {
				Node* node = nullptr;
				// At `module.exports = ...`, reference node is `module`
				if (isBinaryExpression(decl) &&
				    isPropertyAccessExpression(
				        decl->as<BinaryExpression>()->Left)) {
					node = decl->as<BinaryExpression>()
					           ->Left->expression();
				} else if (isExportAssignment(decl)) {
					// Find the export keyword
					node = astnav::findChildOfKind(
					    decl, Kind::ExportKeyword, sourceFile);
					TSC_ASSERT(node != nullptr,
					           "Expected to find export keyword");
				} else {
					node = getNameOfDeclaration(decl);
					if (node == nullptr) {
						node = decl;
					}
				}
				references.push_back(newNodeEntry(node));
			}
		}
	}

	if (!references.empty()) {
		return {new SymbolAndEntries{
		    new Definition{definitionKindSymbol, symbol}, references}};
	}
	return {};
}

// -- Core algorithm for find all references --

// getSpecialSearchKind — findallreferences.go:1838.
std::string getSpecialSearchKind(Node* node) {
	if (node == nullptr) {
		return "none";
	}
	switch (node->kind) {
	case Kind::Constructor:
	case Kind::ConstructorKeyword:
		return "constructor";
	case Kind::Identifier:
		if (isClassLike(node->parent)) {
			TSC_ASSERT(node->parent->name() == node, "");
			return "class";
		}
		[[fallthrough]];
	default:
		return "none";
	}
}

// === refSearch / refState — findallreferences.go:1888 ===
// Symbol that is currently being searched for. This will be replaced if we
// find an alias for the symbol.
struct refSearch {
	// If coming from an export, we will not recursively search for the
	// imported symbol (since that's where we came from).
	ImpExpKind comingFrom = ImpExpKindUnknown; // import, export

	Symbol* symbol = nullptr;
	std::string text;
	std::string escapedText;

	// Only set if `options.implementations` is true. These are the symbols
	// checked to get the implementations of a property access.
	std::vector<Symbol*> parents;

	std::vector<Symbol*> allSearchSymbols;

	// Whether a symbol is in the search set. Do not compare directly to
	// `symbol` because there may be related symbols to search for. See
	// `populateSearchSymbolSet`.
	std::function<bool(Symbol*)> includes;
};

// inheritKey — findallreferences.go:1906.
using inheritKey = std::pair<Symbol*, Symbol*>;

// refState — findallreferences.go:1911.
struct refState {
	std::vector<SourceFile*> sourceFiles;
	collections::Set<std::string>* sourceFilesSet;
	std::string specialSearchKind; // "none", "constructor", or "class"
	checker::Checker* checker;
	gostd::Context ctx;
	compiler::SimpleProgram* program;
	SemanticMeaning searchMeaning;
	refOptions options;
	std::vector<SymbolAndEntries*> result;
	std::map<inheritKey, bool> inheritsFromCache;
	collections::SyncSet<Node*> seenContainingTypeReferences; // node seen
	collections::SyncSet<Node*> seenReExportRHS; // node seen tracker
	ImportTracker importTracker;
	std::unordered_map<Symbol*, SymbolAndEntries*> symbolToReferences;
	std::unordered_map<SourceFile*, collections::SyncSet<Symbol*>*>
	    sourceFileToSeenSymbols;

	bool includesSourceFile(SourceFile* sourceFile);
	ImportsResult* getImportSearches(Symbol* exportSymbol,
	                                 ExportInfo* exportInfo);
	refSearch* createSearch(Node* location, Symbol* symbol,
	                        ImpExpKind comingFrom, const std::string& text,
	                        std::vector<Symbol*> allSearchSymbols);
	std::function<void(Node*, entryKind)> referenceAdder(
	    Symbol* searchSymbol);
	void addReference(Node* referenceLocation, Symbol* symbol, entryKind kind);
	void addImplementationReferences(
	    Node* refNode, const std::function<void(Node*)>& addRef);
	void getReferencesInContainerOrFiles(Symbol* symbol, refSearch* search);
	void getReferencesInSourceFile(SourceFile* sourceFile, refSearch* search,
	                               bool addReferencesHere);
	void getReferencesInContainer(Node* container, SourceFile* sourceFile,
	                              refSearch* search, bool addReferencesHere);
	bool markSearchedSymbols(SourceFile* sourceFile,
	                         std::vector<Symbol*> symbols);
	void getReferencesAtLocation(SourceFile* sourceFile, int position,
	                             refSearch* search, bool addReferencesHere);
	void addConstructorReferences(Node* referenceLocation, Symbol* symbol,
	                              refSearch* search, bool addReferencesHere);
	void addClassStaticThisReferences(Node* referenceLocation, Symbol* symbol,
	                                  refSearch* search,
	                                  bool addReferencesHere);
	void findInheritedConstructorReferences(Node* classDeclaration);
	void getImportOrExportReferences(Node* referenceLocation,
	                                 Symbol* referenceSymbol,
	                                 refSearch* search);
	bool markSeenReExportRHS(Node* node);
	void getReferencesAtExportSpecifier(Node* referenceLocation,
	                                    Symbol* referenceSymbol,
	                                    ExportSpecifier* exportSpecifier,
	                                    refSearch* search,
	                                    bool addReferencesHere,
	                                    bool alwaysGetReferences);
	void searchForImportedSymbol(Symbol* symbol);
	void searchForImportsOfExport(Node* exportLocation, Symbol* exportSymbol,
	                              ExportInfo* exportInfo);
	bool shouldAddSingleReference(Node* singleRef);
	bool hasMatchingMeaning(Node* referenceLocation);
	void getReferenceForShorthandProperty(Symbol* referenceSymbol,
	                                      refSearch* search);
	std::vector<Symbol*> populateSearchSymbolSet(
	    Symbol* symbol, Node* location, bool isForRename,
	    bool providePrefixAndSuffixText, bool implementations);
	Symbol* getRelatedSymbol(refSearch* search, Symbol* referenceSymbol,
	                         Node* referenceLocation, entryKind* kindOut);
	void forEachRelatedSymbol(
	    Symbol* symbol, Node* location,
	    bool isForRenamePopulateSearchSymbolSet,
	    bool onlyIncludeBindingElementAtReferenceLocation,
	    const std::function<Symbol*(Symbol*, Symbol*, Symbol*)>& cbSymbol,
	    const std::function<bool(Symbol*)>& allowBaseTypes,
	    Symbol** resultOut, entryKind* kindOut);
	void searchForName(SourceFile* sourceFile, refSearch* search);
	bool explicitlyInheritsFrom(Symbol* symbol, Symbol* parent);
};

// newState — findallreferences.go:1929.
refState* newState(const gostd::Context& ctx,
                   compiler::SimpleProgram* program,
                   const std::vector<SourceFile*>& sourceFiles,
                   collections::Set<std::string>* sourceFilesSet, Node* node,
                   checker::Checker* checker, SemanticMeaning searchMeaning,
                   const refOptions& options) {
	auto* s = new refState();
	s->sourceFiles = sourceFiles;
	s->sourceFilesSet = sourceFilesSet;
	s->specialSearchKind = getSpecialSearchKind(node);
	s->checker = checker;
	s->ctx = ctx;
	s->program = program;
	s->searchMeaning = searchMeaning;
	s->options = options;
	return s;
}

// refState.includesSourceFile — findallreferences.go:1945.
bool refState::includesSourceFile(SourceFile* sourceFile) {
	return sourceFilesSet->Has(sourceFile->FileName());
}

// refState.getImportSearches — findallreferences.go:1949.
ImportsResult* refState::getImportSearches(Symbol* exportSymbol,
                                         ExportInfo* exportInfo) {
	if (importTracker == nullptr) {
		importTracker = createImportTracker(ctx, program, sourceFiles,
		                                    sourceFilesSet, checker);
	}
	return importTracker(exportSymbol, exportInfo,
	                     options.use == referenceUseRename);
}

// refState.createSearch — findallreferences.go:1957.
// @param allSearchSymbols set of additional symbols for use by `includes`
refSearch* refState::createSearch(Node* location, Symbol* symbol,
                                  ImpExpKind comingFrom,
                                  const std::string& text,
                                  std::vector<Symbol*> allSearchSymbols) {
	// Note: if this is an external module symbol, the name doesn't include
	// quotes. Note: getLocalSymbolForExportDefault handles `export default
	// class C {}`, but not `export default C` or `export { C as default }`.
	// The other two forms seem to be handled downstream (e.g. in
	// `skipPastExportOrImportSpecifier`), so special-casing the first form
	// here appears to be intentional).
	std::string searchText = text;
	if (searchText.empty()) {
		Symbol* s = binder::getLocalSymbolForExportDefault(symbol);
		if (s == nullptr) {
			s = getNonModuleSymbolOfMergedModuleSymbol(symbol);
			if (s == nullptr) {
				s = symbol;
			}
		}
		std::string symbolName = tsc::symbolName(s);
		if (auto [moduleName, ok] =
		        tryGetAmbientModuleNameFromSymbolName(symbolName);
		    ok) {
			searchText = moduleName;
		} else {
			searchText = symbolName;
		}
	}
	if (allSearchSymbols.empty()) {
		allSearchSymbols = {symbol};
	}
	auto* search = new refSearch();
	search->symbol = symbol;
	search->comingFrom = comingFrom;
	search->text = searchText;
	search->escapedText = searchText;
	search->allSearchSymbols = allSearchSymbols;
	search->includes = [allSearchSymbols](Symbol* sym) {
		return contains(allSearchSymbols, sym);
	};
	if (options.implementations && location != nullptr) {
		search->parents =
		    getParentSymbolsOfPropertyAccess(location, symbol, checker);
	}
	return search;
}

// refState.referenceAdder — findallreferences.go:1994.
std::function<void(Node*, entryKind)> refState::referenceAdder(
    Symbol* searchSymbol) {
	SymbolAndEntries* symbolAndEntries = symbolToReferences.count(searchSymbol)
	                                         ? symbolToReferences[searchSymbol]
	                                         : nullptr;
	if (symbolAndEntries == nullptr) {
		symbolAndEntries = NewSymbolAndEntries(definitionKindSymbol, nullptr,
		                                       searchSymbol, {});
		symbolToReferences[searchSymbol] = symbolAndEntries;
		result.push_back(symbolAndEntries);
	}
	return [symbolAndEntries](Node* node, entryKind kind) {
		symbolAndEntries->references.push_back(
		    newNodeEntryWithKind(node, kind));
	};
}

// refState.addReference — findallreferences.go:2006.
void refState::addReference(Node* referenceLocation, Symbol* symbol,
                            entryKind kind) {
	// if rename symbol from default export anonymous function, for example
	// `export default function() {}`, we do not need to add reference
	if (options.use == referenceUseRename &&
	    referenceLocation->kind == Kind::DefaultKeyword) {
		return;
	}

	auto addRef = referenceAdder(symbol);
	if (options.implementations) {
		addImplementationReferences(referenceLocation,
		                            [&](Node* n) { addRef(n, kind); });
	} else {
		addRef(referenceLocation, kind);
	}
}

// getReferenceEntriesForShorthandPropertyAssignment —
// findallreferences.go:2020.
void getReferenceEntriesForShorthandPropertyAssignment(
    Node* node, checker::Checker* checker,
    const std::function<void(Node*)>& addReference) {
	Symbol* refSymbol = checker->GetSymbolAtLocation(node);
	if (refSymbol == nullptr || refSymbol->valueDeclaration == nullptr) {
		return;
	}
	Symbol* shorthandSymbol = checker->GetShorthandAssignmentValueSymbol(
	    refSymbol->valueDeclaration);
	if (shorthandSymbol != nullptr && !shorthandSymbol->declarations.empty()) {
		for (auto* declaration : shorthandSymbol->declarations) {
			if (getMeaningFromDeclaration(declaration) &
			    SemanticMeaningValue) {
				addReference(declaration);
			}
		}
	}
}

// tryGetClassByExtendingIdentifier — findallreferences.go:2039.
Node* tryGetClassByExtendingIdentifier(Node* node) {
	return tryGetClassExtendingExpressionWithTypeArguments(
	    climbPastPropertyAccess(node)->parent);
}

// getClassConstructorSymbol — findallreferences.go:2043.
Symbol* getClassConstructorSymbol(Symbol* classSymbol) {
	if (classSymbol->members.empty()) {
		return nullptr;
	}
	auto it = classSymbol->members.find(InternalSymbolNameConstructor);
	return it != classSymbol->members.end() ? it->second : nullptr;
}

// hasOwnConstructor — findallreferences.go:2050.
bool hasOwnConstructor(Node* classDeclaration) {
	return getClassConstructorSymbol(classDeclaration->symbol()) != nullptr;
}

// findOwnConstructorReferences — findallreferences.go:2054.
void findOwnConstructorReferences(
    Symbol* classSymbol, SourceFile* sourceFile,
    const std::function<void(Node*)>& addNode) {
	Symbol* constructorSymbol = getClassConstructorSymbol(classSymbol);
	if (constructorSymbol != nullptr &&
	    !constructorSymbol->declarations.empty()) {
		for (auto* decl : constructorSymbol->declarations) {
			if (decl->kind == Kind::Constructor) {
				if (Node* ctrKeyword = astnav::findChildOfKind(
				        decl, Kind::ConstructorKeyword, sourceFile);
				    ctrKeyword != nullptr) {
					addNode(ctrKeyword);
				}
			}
		}
	}

	if (!classSymbol->exports.empty()) {
		for (auto& [_, member] : classSymbol->exports) {
			Node* decl = member->valueDeclaration;
			if (decl != nullptr && decl->kind == Kind::MethodDeclaration) {
				Node* body = decl->body();
				if (body != nullptr) {
					forEachDescendantOfKind(body, Kind::ThisKeyword,
					                        [&](Node* thisKeyword) {
						                        if (isNewExpressionTarget(
						                            thisKeyword, false,
						                            false)) {
							                        addNode(thisKeyword);
						                        }
					                        });
				}
			}
		}
	}
}

// findSuperConstructorAccesses — findallreferences.go:2083.
void findSuperConstructorAccesses(
    Node* classDeclaration, const std::function<void(Node*)>& addNode) {
	Symbol* constructorSymbol =
	    getClassConstructorSymbol(classDeclaration->symbol());
	if (constructorSymbol == nullptr ||
	    constructorSymbol->declarations.empty()) {
		return;
	}

	for (auto* decl : constructorSymbol->declarations) {
		if (decl->kind == Kind::Constructor) {
			Node* body = decl->body();
			if (body != nullptr) {
				forEachDescendantOfKind(body, Kind::SuperKeyword,
				                        [&](Node* node) {
					                        if (isCallExpressionTarget(
					                            node, false, false)) {
						                        addNode(node);
					                        }
				                        });
			}
		}
	}
}

// forEachDescendantOfKind — findallreferences.go:2103.
void forEachDescendantOfKind(Node* node, Kind kind,
                             const std::function<void(Node*)>& action) {
	node->forEachChild([&](Node* child) {
		if (child->kind == kind) {
			action(child);
		}
		forEachDescendantOfKind(child, kind, action);
		return false;
	});
}

// refState.addImplementationReferences — findallreferences.go:2113.
void refState::addImplementationReferences(
    Node* refNode, const std::function<void(Node*)>& addRef) {
	// Check if we found a function/propertyAssignment/method with an
	// implementation or initializer
	if (isDeclarationName(refNode) && isImplementation(refNode->parent)) {
		addRef(refNode);
		return;
	}

	if (refNode->kind != Kind::Identifier) {
		return;
	}

	if (refNode->parent->kind == Kind::ShorthandPropertyAssignment) {
		// Go ahead and dereference the shorthand assignment by going to its
		// definition
		getReferenceEntriesForShorthandPropertyAssignment(refNode, checker,
		                                                  addRef);
	}

	// Check if the node is within an extends or implements clause
	if (Node* containingNode = getContainingNodeIfInHeritageClause(refNode);
	    containingNode != nullptr) {
		addRef(containingNode);
		return;
	}

	// If we got a type reference, try and see if the reference applies to any
	// expressions that can implement an interface. Find the first node whose
	// parent isn't a type node -- i.e., the highest type node.
	Node* typeNode = findAncestor(refNode, [](Node* a) {
		return !isQualifiedName(a->parent) && !isTypeNode(a->parent) &&
		       !isTypeElement(a->parent);
	});

	if (typeNode == nullptr || typeNode->parent->type() == nullptr) {
		return;
	}

	Node* typeHavingNode = typeNode->parent;
	if (typeHavingNode->type() == typeNode &&
	    seenContainingTypeReferences.AddIfAbsent(typeHavingNode)) {
		auto addIfImplementation = [&](Node* e) {
			if (isImplementationExpression(e)) {
				addRef(e);
			}
		};
		if (hasInitializer(typeHavingNode)) {
			addIfImplementation(typeHavingNode->initializer());
		} else if (isFunctionLike(typeHavingNode) &&
		           typeHavingNode->body() != nullptr) {
			Node* body = typeHavingNode->body();
			if (body->kind == Kind::Block) {
				forEachReturnStatement(body, [&](Node* returnStatement) {
					if (Node* expr = returnStatement->expression();
					    expr != nullptr) {
						addIfImplementation(expr);
					}
					return false;
				});
			} else {
				addIfImplementation(body);
			}
		} else if (isAssertionExpression(typeHavingNode) ||
		           isSatisfiesExpression(typeHavingNode)) {
			addIfImplementation(typeHavingNode->expression());
		}
	}
}

// refState.getReferencesInContainerOrFiles — findallreferences.go:2173.
void refState::getReferencesInContainerOrFiles(Symbol* symbol,
                                             refSearch* search) {
	// Try to get the smallest valid scope that we can limit our search to;
	// otherwise we'll need to search globally (i.e. include each file).
	if (Node* scope = getSymbolScope(symbol); scope != nullptr) {
		bool addReferencesHere =
		    scope->kind != Kind::SourceFile ||
		    contains(sourceFiles, scope->as<SourceFile>());
		getReferencesInContainer(scope, getSourceFileOfNode(scope), search,
		                         addReferencesHere);
	} else {
		// Global search
		for (auto* sourceFile : sourceFiles) {
			// state.cancellationToken.throwIfCancellationRequested();
			searchForName(sourceFile, search);
		}
	}
}

// refState.getReferencesInSourceFile — findallreferences.go:2188.
void refState::getReferencesInSourceFile(SourceFile* sourceFile,
                                         refSearch* search,
                                         bool addReferencesHere) {
	// state.cancellationToken.throwIfCancellationRequested();
	getReferencesInContainer(sourceFile->asNode(), sourceFile, search,
	                         addReferencesHere);
}

// refState.getReferencesInContainer — findallreferences.go:2193.
void refState::getReferencesInContainer(Node* container,
                                        SourceFile* sourceFile,
                                        refSearch* search,
                                        bool addReferencesHere) {
	// Search within node "container" for references for a search value, where
	// the search value is defined as a tuple of (searchSymbol, searchText,
	// searchLocation, and searchMeaning).
	if (!markSearchedSymbols(sourceFile, search->allSearchSymbols)) {
		return;
	}

	for (int position : getPossibleSymbolReferencePositions(
	         sourceFile, search->text, container)) {
		getReferencesAtLocation(sourceFile, position, search,
		                        addReferencesHere);
	}
}

// refState.markSearchedSymbols — findallreferences.go:2206.
bool refState::markSearchedSymbols(SourceFile* sourceFile,
                                   std::vector<Symbol*> symbols) {
	collections::SyncSet<Symbol*>* seenSymbols =
	    sourceFileToSeenSymbols.count(sourceFile)
	        ? sourceFileToSeenSymbols[sourceFile]
	        : nullptr;
	if (seenSymbols == nullptr) {
		seenSymbols = new collections::SyncSet<Symbol*>();
		sourceFileToSeenSymbols[sourceFile] = seenSymbols;
	}
	bool anyNewSymbols = false;
	for (auto* sym : symbols) {
		if (seenSymbols->AddIfAbsent(sym)) {
			anyNewSymbols = true;
		}
	}
	return anyNewSymbols;
}

// refState.getReferencesAtLocation — findallreferences.go:2221.
void refState::getReferencesAtLocation(SourceFile* sourceFile, int position,
                                       refSearch* search,
                                       bool addReferencesHere) {
	Node* referenceLocation =
	    astnav::getTouchingPropertyName(sourceFile, position);

	if (!isValidReferencePosition(referenceLocation, search->text)) {
		// This wasn't the start of a token.  Check to see if it might be a
		// match in a comment or string if that's what the caller is asking
		// for.

		// !!! not implemented
		// if (!state.options.implementations && (state.options.findInStrings &&
		// isInString(sourceFile, position) || state.options.findInComments &&
		// isInNonReferenceComment(sourceFile, position))) { ... }

		return;
	}

	if (!(getMeaningFromLocation(referenceLocation) & searchMeaning)) {
		return;
	}

	Symbol* referenceSymbol = checker->GetSymbolAtLocation(referenceLocation);
	if (referenceSymbol == nullptr) {
		return;
	}

	Node* parent = referenceLocation->parent;
	if (parent->kind == Kind::ImportSpecifier &&
	    parent->propertyName() == referenceLocation) {
		// This is added through `singleReferences` in ImportsResult. If we
		// happen to see it again, don't add it again.
		return;
	}

	if (parent->kind == Kind::ExportSpecifier) {
		getReferencesAtExportSpecifier(
		    referenceLocation, referenceSymbol,
		    parent->as<ExportSpecifier>(), search, addReferencesHere,
		    /*alwaysGetReferences*/ false);
		return;
	}

	entryKind relatedSymbolKind = entryKindNone;
	Symbol* relatedSymbol = getRelatedSymbol(search, referenceSymbol,
	                                         referenceLocation,
	                                         &relatedSymbolKind);
	if (relatedSymbol == nullptr) {
		getReferenceForShorthandProperty(referenceSymbol, search);
		return;
	}

	if (specialSearchKind == "none") {
		if (addReferencesHere) {
			addReference(referenceLocation, relatedSymbol,
			             relatedSymbolKind);
		}
	} else if (specialSearchKind == "constructor") {
		addConstructorReferences(referenceLocation, relatedSymbol, search,
		                         addReferencesHere);
	} else if (specialSearchKind == "class") {
		addClassStaticThisReferences(referenceLocation, relatedSymbol,
		                             search, addReferencesHere);
	}

	// Use the parent symbol if the location is commonjs require syntax on
	// javascript files only.
	if (isInJSFile(referenceLocation) &&
	    referenceLocation->parent->kind == Kind::BindingElement &&
	    isVariableDeclarationInitializedToBareOrAccessedRequire(
	        referenceLocation->parent->parent->parent)) {
		referenceSymbol = referenceLocation->parent->symbol();
		// The parent will not have a symbol if it's an
		// ObjectBindingPattern (when destructuring is used).  In this case,
		// just skip it, since the bound identifiers are not an alias of the
		// import.
		if (referenceSymbol == nullptr) {
			return;
		}
	}

	getImportOrExportReferences(referenceLocation, referenceSymbol, search);
}

// refState.addConstructorReferences — findallreferences.go:2292.
void refState::addConstructorReferences(Node* referenceLocation,
                                        Symbol* symbol, refSearch* search,
                                        bool addReferencesHere) {
	if (isNewExpressionTarget(referenceLocation, false, false) &&
	    addReferencesHere) {
		addReference(referenceLocation, symbol, entryKindNode);
	}

	auto pusher = [&]() { return referenceAdder(search->symbol); };

	if (isClassLike(referenceLocation->parent)) {
		// This is the class declaration containing the constructor.
		SourceFile* sourceFile = getSourceFileOfNode(referenceLocation);
		findOwnConstructorReferences(search->symbol, sourceFile,
		                             [&](Node* n) {
			                             pusher()(n, entryKindNode);
		                             });
	} else {
		// If this class appears in `extends C`, then the extending class'
		// "super" calls are references.
		if (Node* classExtending =
		        tryGetClassByExtendingIdentifier(referenceLocation);
		    classExtending != nullptr) {
			findSuperConstructorAccesses(classExtending, [&](Node* n) {
				pusher()(n, entryKindNode);
			});
			findInheritedConstructorReferences(classExtending);
		}
	}
}

// refState.addClassStaticThisReferences — findallreferences.go:2318.
void refState::addClassStaticThisReferences(Node* referenceLocation,
                                            Symbol* symbol, refSearch* search,
                                            bool addReferencesHere) {
	if (addReferencesHere) {
		addReference(referenceLocation, symbol, entryKindNode);
	}

	Node* classLike = referenceLocation->parent;
	if (options.use == referenceUseRename || !isClassLike(classLike)) {
		return;
	}

	auto addRef = referenceAdder(search->symbol);
	auto members = classLike->members();
	for (auto* member : members) {
		if (!(isMethodOrAccessor(member) && hasStaticModifier(member))) {
			continue;
		}
		Node* body = member->body();
		if (body != nullptr) {
			std::function<void(Node*)> cb = [&](Node* node) {
				if (node->kind == Kind::ThisKeyword) {
					addRef(node, entryKindNode);
				} else if (!isFunctionLike(node) && !isClassLike(node)) {
					node->forEachChild([&](Node* child) {
						cb(child);
						return false;
					});
				}
			};
			cb(body);
		}
	}
}

// refState.findInheritedConstructorReferences — findallreferences.go:2355.
void refState::findInheritedConstructorReferences(Node* classDeclaration) {
	if (hasOwnConstructor(classDeclaration)) {
		return;
	}
	Symbol* classSymbol = classDeclaration->symbol();
	refSearch* search =
	    createSearch(nullptr, classSymbol, ImpExpKindUnknown, "", {});
	getReferencesInContainerOrFiles(classSymbol, search);
}

// refState.getImportOrExportReferences — findallreferences.go:2364.
void refState::getImportOrExportReferences(Node* referenceLocation,
                                           Symbol* referenceSymbol,
                                           refSearch* search) {
	ImportExportSymbol* importOrExport = getImportOrExportSymbol(
	    referenceLocation, referenceSymbol, checker,
	    search->comingFrom == ImpExpKindExport);
	if (importOrExport == nullptr) {
		return;
	}
	if (importOrExport->kind == ImpExpKindImport) {
		if (!isForRenameWithPrefixAndSuffixText(options)) {
			searchForImportedSymbol(importOrExport->symbol);
		}
	} else {
		searchForImportsOfExport(referenceLocation, importOrExport->symbol,
		                         importOrExport->exportInfo);
	}
}

// refState.markSeenReExportRHS — findallreferences.go:2378.
bool refState::markSeenReExportRHS(Node* node) {
	return seenReExportRHS.AddIfAbsent(node);
}

// refState.getReferencesAtExportSpecifier — findallreferences.go:2382.
void refState::getReferencesAtExportSpecifier(Node* referenceLocation,
                                              Symbol* referenceSymbol,
                                              ExportSpecifier* exportSpecifier,
                                              refSearch* search,
                                              bool addReferencesHere,
                                              bool alwaysGetReferences) {
	TSC_ASSERT(
	    !alwaysGetReferences || options.useAliasesForRename,
	    "If alwaysGetReferences is true, then prefix/suffix text must be enabled");

	auto* exportDeclaration =
	    exportSpecifier->parent->parent->as<ExportDeclaration>();
	Node* propertyName = exportSpecifier->PropertyName;
	Node* name = exportSpecifier->name;
	Symbol* localSymbol = getLocalSymbolForExportSpecifier(
	    referenceLocation, referenceSymbol, exportSpecifier, checker);

	if (!alwaysGetReferences && !search->includes(localSymbol)) {
		return;
	}

	auto addRef = [&]() {
		if (addReferencesHere) {
			addReference(referenceLocation, localSymbol, entryKindNode);
		}
	};

	if (propertyName == nullptr) {
		// Don't rename at `export { default } from "m";`. (but do continue to
		// search for imports of the re-export)
		if (!(options.use == referenceUseRename &&
		      moduleExportNameIsDefault(name))) {
			addRef();
		}
	} else if (referenceLocation == propertyName->asNode()) {
		// For `export { foo as bar } from "baz"`, "`foo`" will be added from
		// the singleReferences for import searches of the original export.
		// For `export { foo as bar };`, where `foo` is a local, so add it now.
		if (exportDeclaration->ModuleSpecifier == nullptr) {
			addRef();
		}

		if (addReferencesHere && options.use != referenceUseRename &&
		    markSeenReExportRHS(name)) {
			Symbol* exportSymbol = exportSpecifier->Symbol;
			TSC_ASSERT(exportSymbol != nullptr,
			           "exportSpecifier.Symbol() should not be nil");
			addReference(name, exportSymbol, entryKindNode);
		}
	} else {
		if (markSeenReExportRHS(referenceLocation)) {
			addRef();
		}
	}

	// For `export { foo as bar }`, rename `foo`, but not `bar`.
	if (!isForRenameWithPrefixAndSuffixText(options) ||
	    alwaysGetReferences) {
		bool isDefaultExport = moduleExportNameIsDefault(referenceLocation) ||
		                       moduleExportNameIsDefault(exportSpecifier->name);
		ExportKind exportKind = ExportKindNamed;
		if (isDefaultExport) {
			exportKind = ExportKindDefault;
		}
		Symbol* exportSymbol = exportSpecifier->Symbol;
		TSC_ASSERT(exportSymbol != nullptr,
		           "exportSpecifier.Symbol() should not be nil");
		ExportInfo* exportInfo =
		    getExportInfo(exportSymbol, exportKind, checker);
		if (exportInfo != nullptr) {
			searchForImportsOfExport(referenceLocation, exportSymbol,
			                         exportInfo);
		}
	}

	// At `export { x } from "foo"`, also search for the imported symbol
	// `"foo".x`.
	if (search->comingFrom != ImpExpKindExport &&
	    exportDeclaration->ModuleSpecifier != nullptr &&
	    propertyName == nullptr &&
	    !isForRenameWithPrefixAndSuffixText(options)) {
		Symbol* imported =
		    checker->GetExportSpecifierLocalTargetSymbol(
		        exportSpecifier->asNode());
		if (imported != nullptr) {
			searchForImportedSymbol(imported);
		}
	}
}

// refState.searchForImportedSymbol — findallreferences.go:2455.
// Go to the symbol we imported from and find references for it.
void refState::searchForImportedSymbol(Symbol* symbol) {
	for (auto* declaration : symbol->declarations) {
		SourceFile* exportingFile = getSourceFileOfNode(declaration);
		// Need to search in the file even if it's not in the search-file set,
		// because it might export the symbol.
		getReferencesInSourceFile(
		    exportingFile,
		    createSearch(declaration, symbol, ImpExpKindImport, "", {}),
		    includesSourceFile(exportingFile));
	}
}

// refState.searchForImportsOfExport — findallreferences.go:2464.
// Search for all imports of a given exported symbol using
// `State.getImportSearches`.
void refState::searchForImportsOfExport(Node* exportLocation,
                                        Symbol* exportSymbol,
                                        ExportInfo* exportInfo) {
	ImportsResult* r = getImportSearches(exportSymbol, exportInfo);

	// For `import { foo as bar }` just add the reference to `foo`, and don't
	// otherwise search in the file.
	if (!r->singleReferences.empty()) {
		auto addRef = referenceAdder(exportSymbol);
		for (auto* singleRef : r->singleReferences) {
			if (shouldAddSingleReference(singleRef)) {
				addRef(singleRef, entryKindNode);
			}
		}
	}

	// For each import, find all references to that import in its source file.
	for (auto& i : r->importSearches) {
		getReferencesInSourceFile(
		    getSourceFileOfNode(i.importLocation),
		    createSearch(i.importLocation, i.importSymbol, ImpExpKindExport,
		                 "", {}),
		    /*addReferencesHere*/ true);
	}

	if (!r->indirectUsers.empty()) {
		refSearch* indirectSearch = nullptr;
		switch (exportInfo->exportKind) {
		case ExportKindNamed:
			indirectSearch = createSearch(exportLocation, exportSymbol,
			                              ImpExpKindExport, "", {});
			break;
		case ExportKindDefault:
			// Search for a property access to '.default'. This can't be
			// renamed.
			if (options.use != referenceUseRename) {
				indirectSearch =
				    createSearch(exportLocation, exportSymbol,
				                 ImpExpKindExport, "default", {});
			}
			break;
		}
		if (indirectSearch != nullptr) {
			for (auto* indirectUser : r->indirectUsers) {
				searchForName(indirectUser, indirectSearch);
			}
		}
	}
}

// refState.shouldAddSingleReference — findallreferences.go:2501.
bool refState::shouldAddSingleReference(Node* singleRef) {
	if (!hasMatchingMeaning(singleRef)) {
		return false;
	}
	if (options.use != referenceUseRename) {
		return true;
	}
	// Don't rename an import type `import("./module-name")` when renaming
	// `name` in `export = name;`
	if (!isIdentifier(singleRef) &&
	    !isImportOrExportSpecifier(singleRef->parent)) {
		return false;
	}
	// At `default` in `import { default as x }` or `export { default as x }`,
	// do add a reference, but do not rename.
	return !(isImportOrExportSpecifier(singleRef->parent) &&
	         moduleExportNameIsDefault(singleRef));
}

// refState.hasMatchingMeaning — findallreferences.go:2516.
bool refState::hasMatchingMeaning(Node* referenceLocation) {
	return getMeaningFromLocation(referenceLocation) & searchMeaning;
}

// refState.getReferenceForShorthandProperty — findallreferences.go:2520.
void refState::getReferenceForShorthandProperty(Symbol* referenceSymbol,
                                              refSearch* search) {
	if (referenceSymbol->flags & SymbolFlagsTransient ||
	    referenceSymbol->valueDeclaration == nullptr) {
		return;
	}
	Symbol* shorthandValueSymbol =
	    checker->GetShorthandAssignmentValueSymbol(
	        referenceSymbol->valueDeclaration);
	Node* name = getNameOfDeclaration(referenceSymbol->valueDeclaration);

	// Because in short-hand property assignment, an identifier which stored as
	// name of the short-hand property assignment has two meanings: property
	// name and property value. Therefore when we do findAllReference at the
	// position where an identifier is declared, the language service should
	// return the position of the variable declaration as well as the position
	// in short-hand property assignment excluding property accessing.
	// However, if we do findAllReference at the position of property
	// accessing, the referenceEntry of such position will be handled in the
	// first case.
	if (name != nullptr && search->includes(shorthandValueSymbol)) {
		addReference(name, shorthandValueSymbol, entryKindNode);
	}
}

// === search ===

// refState.populateSearchSymbolSet — findallreferences.go:2538.
std::vector<Symbol*> refState::populateSearchSymbolSet(
    Symbol* symbol, Node* location, bool isForRename,
    bool providePrefixAndSuffixText, bool implementations) {
	if (location == nullptr) {
		return {symbol};
	}
	std::vector<Symbol*> result;
	Symbol* resultSym = nullptr;
	entryKind kind = entryKindNone;
	forEachRelatedSymbol(
	    symbol, location, isForRename,
	    !(isForRename && providePrefixAndSuffixText),
	    [&](Symbol* sym, Symbol* root, Symbol* base) -> Symbol* {
		    // static method/property and instance method/property might have
		    // the same name. Only include static or only include instance.
		    if (base != nullptr) {
			    if (isStaticSymbol(symbol) != isStaticSymbol(base)) {
				    base = nullptr;
			    }
		    }
		    result.push_back(orElse(base, orElse(root, sym)));
		    return nullptr;
	    },
	    // when try to find implementation, implementations is true, and not
	    // allowed to find base class
	    /*allowBaseTypes*/ [&](Symbol*) { return !implementations; },
	    &resultSym, &kind);
	return result;
}

// refState.getRelatedSymbol — findallreferences.go:2563.
Symbol* refState::getRelatedSymbol(refSearch* search,
                                   Symbol* referenceSymbol,
                                   Node* referenceLocation,
                                   entryKind* kindOut) {
	Symbol* result = nullptr;
	forEachRelatedSymbol(
	    referenceSymbol, referenceLocation,
	    /*isForRenamePopulateSearchSymbolSet*/ false,
	    /*onlyIncludeBindingElementAtReferenceLocation*/
	    options.use != referenceUseRename || options.useAliasesForRename,
	    [&](Symbol* sym, Symbol* rootSymbol,
	        Symbol* baseSymbol) -> Symbol* {
		    // check whether the symbol used to search itself is just the
		    // searched one.
		    if (baseSymbol != nullptr) {
			    // static method/property and instance method/property might
			    // have the same name. Only check static or only check
			    // instance.
			    if (isStaticSymbol(referenceSymbol) !=
			        isStaticSymbol(baseSymbol)) {
				    baseSymbol = nullptr;
			    }
		    }
		    Symbol* searchSym =
		        coalesce(baseSymbol, coalesce(rootSymbol, sym));
		    if (searchSym != nullptr && search->includes(searchSym)) {
			    if (rootSymbol != nullptr &&
			        !(sym->checkFlags & CheckFlagsSynthetic)) {
				    return rootSymbol;
			    }
			    return sym;
		    }
		    // For a base type, use the symbol for the derived type. For a
		    // synthetic (e.g. union) property, use the union symbol.
		    return nullptr;
	    },
	    [&](Symbol* rootSymbol) {
		    return !(!search->parents.empty() &&
		             !some(search->parents, [&](Symbol* parent) {
			             return explicitlyInheritsFrom(rootSymbol->parent,
			                                           parent);
		             }));
	    },
	    &result, kindOut);
	return result;
}

// refState.forEachRelatedSymbol — findallreferences.go:2595.
void refState::forEachRelatedSymbol(
    Symbol* symbol, Node* location, bool isForRenamePopulateSearchSymbolSet,
    bool onlyIncludeBindingElementAtReferenceLocation,
    const std::function<Symbol*(Symbol*, Symbol*, Symbol*)>& cbSymbol,
    const std::function<bool(Symbol*)>& allowBaseTypes, Symbol** resultOut,
    entryKind* kindOut) {
	*resultOut = nullptr;
	*kindOut = entryKindNone;
	auto fromRoot = [&](Symbol* sym) -> Symbol* {
		// If this is a union property:
		//   - In populateSearchSymbolsSet we will add all the symbols from all
		//     its source symbols in all unioned types.
		//   - In findRelatedSymbol, we will just use the union symbol if any
		//     source symbol is included in the search.
		// If the symbol is an instantiation from a another symbol (e.g.
		// widened symbol):
		//   - In populateSearchSymbolsSet, add the root the list
		//   - In findRelatedSymbol, return the source symbol if that is in the
		//     search. (Do not return the instantiation symbol.)
		for (auto* rootSymbol : checker->GetRootSymbols(sym)) {
			if (Symbol* r = cbSymbol(sym, rootSymbol,
			                         /*baseSymbol*/ nullptr)) {
				return r;
			}
			// Add symbol of properties/methods of the same name in base
			// classes and implemented interfaces definitions
			if (rootSymbol->parent != nullptr &&
			    rootSymbol->parent->flags &
			        (SymbolFlagsClass | SymbolFlagsInterface) &&
			    allowBaseTypes(rootSymbol)) {
				Symbol* r = getPropertySymbolsFromBaseTypes(
				    rootSymbol->parent, rootSymbol->name, checker,
				    [&](Symbol* base) {
					    return cbSymbol(sym, rootSymbol, base);
				    });
				if (r != nullptr) {
					return r;
				}
			}
		}
		return nullptr;
	};

	if (Node* containingObjectLiteralElement =
	        getContainingObjectLiteralElement(location);
	    containingObjectLiteralElement != nullptr) {
		/* Because in short-hand property assignment, location has two meaning
		 * : property name and as value of the property When we do
		 * findAllReference at the position of the short-hand property
		 * assignment, we would want to have references to position of
		 * property name and variable declaration of the identifier. Like in
		 * below example, when querying for all references for an identifier
		 * 'name', of the property assignment, the language service should
		 * show both 'name' in 'obj' and 'name' in variable declaration
		 *      const name = "Foo";
		 *      const obj = { name };
		 * In order to do that, we will populate the search set with the value
		 * symbol of the identifier as a value of the property assignment so
		 * that when matching with potential reference symbol, both symbols
		 * from property declaration and variable declaration will be included
		 * correctly.
		 */
		Symbol* shorthandValueSymbol =
		    checker->GetShorthandAssignmentValueSymbol(location->parent);
		// gets the local symbol
		if (shorthandValueSymbol != nullptr &&
		    isForRenamePopulateSearchSymbolSet) {
			// When renaming 'x' in `const o = { x }`, just rename the local
			// variable, not the property.
			*resultOut = cbSymbol(shorthandValueSymbol,
			                      /*rootSymbol*/ nullptr,
			                      /*baseSymbol*/ nullptr);
			*kindOut = entryKindSearchedLocalFoundProperty;
			return;
		}
		// If the location is in a context sensitive location (i.e. in an
		// object literal) try to get a contextual type for it, and add the
		// property symbol from the contextual type to the search set
		if (checker::Type* contextualType = checker->GetContextualType(
		        containingObjectLiteralElement->parent,
		        checker::ContextFlagsNone);
		    contextualType != nullptr) {
			auto symbols = checker->GetPropertySymbolsFromContextualType(
			    containingObjectLiteralElement, contextualType,
			    /*unionSymbolOk*/ true);
			for (auto* sym : symbols) {
				if (Symbol* res = fromRoot(sym); res != nullptr) {
					*resultOut = res;
					*kindOut = entryKindSearchedPropertyFoundLocal;
					return;
				}
			}
		}
		// If the location is name of property symbol from object literal
		// destructuring pattern Search the property symbol
		//      for ( { property: p2 } of elems) { }
		if (Symbol* propertySymbol =
		        checker->GetPropertySymbolOfDestructuringAssignment(location);
		    propertySymbol != nullptr) {
			if (Symbol* res = cbSymbol(propertySymbol,
			                           /*rootSymbol*/ nullptr,
			                           /*baseSymbol*/ nullptr)) {
				*resultOut = res;
				*kindOut = entryKindSearchedPropertyFoundLocal;
				return;
			}
		}
		if (shorthandValueSymbol != nullptr) {
			if (Symbol* res = cbSymbol(shorthandValueSymbol,
			                           /*rootSymbol*/ nullptr,
			                           /*baseSymbol*/ nullptr)) {
				*resultOut = res;
				*kindOut = entryKindSearchedLocalFoundProperty;
				return;
			}
		}
	}

	if (Symbol* aliasedSymbol =
	        getMergedAliasedSymbolOfNamespaceExportDeclaration(location,
	                                                         symbol, checker);
	    aliasedSymbol != nullptr) {
		// In case of UMD module and global merging, search for global as well
		if (Symbol* res = cbSymbol(aliasedSymbol, /*rootSymbol*/ nullptr,
		                           /*baseSymbol*/ nullptr)) {
			*resultOut = res;
			*kindOut = entryKindNode;
			return;
		}
	}

	if (Symbol* res = fromRoot(symbol); res != nullptr) {
		*resultOut = res;
		*kindOut = entryKindNode;
		return;
	}

	if (symbol->valueDeclaration != nullptr &&
	    isParameterPropertyDeclaration(symbol->valueDeclaration,
	                                   symbol->valueDeclaration->parent)) {
		auto [paramProp1, paramProp2] =
		    checker->GetSymbolsOfParameterPropertyDeclaration(
		        symbol->valueDeclaration, symbol->name);
		TSC_ASSERT(
		    paramProp1->flags & SymbolFlagsFunctionScopedVariable &&
		        paramProp2->flags & SymbolFlagsClassMember,
		    "GetSymbolsOfParameterPropertyDeclaration must return "
		    "(parameter, member) pair");
		*resultOut = fromRoot(
		    ifElse(symbol->flags & SymbolFlagsFunctionScopedVariable,
		           paramProp2, paramProp1));
		*kindOut = entryKindNode;
		return;
	}

	if (Node* exportSpecifier =
	        getDeclarationOfKind(symbol, Kind::ExportSpecifier);
	    exportSpecifier != nullptr &&
	    (!isForRenamePopulateSearchSymbolSet ||
	     exportSpecifier->propertyName() == nullptr)) {
		if (Symbol* localSymbol =
		        checker->GetExportSpecifierLocalTargetSymbol(
		            exportSpecifier);
		    localSymbol != nullptr) {
			if (Symbol* res = cbSymbol(localSymbol,
			                           /*rootSymbol*/ nullptr,
			                           /*baseSymbol*/ nullptr)) {
				*resultOut = res;
				*kindOut = entryKindNode;
				return;
			}
		}
	}

	// symbolAtLocation for a binding element is the local symbol. See if the
	// search symbol is the property. Don't do this when populating search set
	// for a rename when prefix and suffix text will be provided -- just
	// rename the local.
	if (!isForRenamePopulateSearchSymbolSet) {
		Symbol* bindingElementPropertySymbol = nullptr;
		if (onlyIncludeBindingElementAtReferenceLocation) {
			if (!isObjectBindingElementWithoutPropertyName(
			        location->parent)) {
				return;
			}
			bindingElementPropertySymbol =
			    getPropertySymbolFromBindingElement(checker,
			                                        location->parent);
		} else {
			bindingElementPropertySymbol =
			    getPropertySymbolOfObjectBindingPatternWithoutPropertyName(
			        symbol, checker);
		}
		if (bindingElementPropertySymbol == nullptr) {
			return;
		}
		*resultOut = fromRoot(bindingElementPropertySymbol);
		*kindOut = entryKindSearchedPropertyFoundLocal;
		return;
	}

	TSC_ASSERT(isForRenamePopulateSearchSymbolSet, "");

	// due to the above assert and the arguments at the uses of this
	// function, (onlyIncludeBindingElementAtReferenceLocation <=>
	// !providePrefixAndSuffixTextForRename) holds
	bool includeOriginalSymbolOfBindingElement =
	    onlyIncludeBindingElementAtReferenceLocation;

	if (includeOriginalSymbolOfBindingElement) {
		if (Symbol* bindingElementPropertySymbol =
		        getPropertySymbolOfObjectBindingPatternWithoutPropertyName(
		            symbol, checker);
		    bindingElementPropertySymbol != nullptr) {
			*resultOut = fromRoot(bindingElementPropertySymbol);
			*kindOut = entryKindSearchedPropertyFoundLocal;
			return;
		}
	}
}

// refState.searchForName — findallreferences.go:2732.
// Search for all occurrences of an identifier in a source file (and filter
// out the ones that match).
void refState::searchForName(SourceFile* sourceFile, refSearch* search) {
	if (getNameTable(sourceFile).count(search->escapedText)) {
		getReferencesInSourceFile(sourceFile, search,
		                          /*addReferencesHere*/ true);
	}
}

// refState.explicitlyInheritsFrom — findallreferences.go:2738.
bool refState::explicitlyInheritsFrom(Symbol* symbol, Symbol* parent) {
	if (symbol == parent) {
		return true;
	}

	// Check cache first
	inheritKey key{symbol, parent};
	if (auto it = inheritsFromCache.find(key); it != inheritsFromCache.end()) {
		return it->second;
	}

	// Set to false initially to prevent infinite recursion
	inheritsFromCache[key] = false;

	if (symbol->declarations.empty()) {
		return false;
	}

	bool inherits = some(symbol->declarations, [&](Node* declaration) {
		auto superTypeNodes = getAllSuperTypeNodes(declaration);
		return some(superTypeNodes, [&](HeritageClauseElement* typeReference) {
			checker::Type* typ = checker->GetTypeAtLocation(
			    reinterpret_cast<Node*>(typeReference));
			return typ != nullptr && typ->symbol != nullptr &&
			       explicitlyInheritsFrom(typ->symbol, parent);
		});
	});

	// Update cache with the actual result
	inheritsFromCache[key] = inherits;
	return inherits;
}

// getReferencedSymbolsForSymbol — findallreferences.go:1856.
std::vector<SymbolAndEntries*> getReferencedSymbolsForSymbol(
    const gostd::Context& ctx, compiler::SimpleProgram* program,
    Symbol* originalSymbol, Node* node,
    const std::vector<SourceFile*>& sourceFiles,
    collections::Set<std::string>* sourceFilesSet, checker::Checker* checker,
    const refOptions& options) {
	// Core find-all-references algorithm for a normal symbol.

	Symbol* symbol = coalesce(
	    skipPastExportOrImportSpecifierOrUnion(
	        originalSymbol, node, checker,
	        /*useLocalSymbolForExportSpecifier*/
	        !isForRenameWithPrefixAndSuffixText(options)),
	    originalSymbol);

	// Compute the meaning from the location and the symbol it references
	SemanticMeaning searchMeaning = SemanticMeaningAll;
	if (options.use != referenceUseRename) {
		searchMeaning = getIntersectingMeaningFromDeclarations(
		    node, symbol, SemanticMeaningAll);
	}
	refState* state = newState(ctx, program, sourceFiles, sourceFilesSet, node,
	                         checker, searchMeaning, options);

	Node* exportSpecifier = nullptr;
	if (isForRenameWithPrefixAndSuffixText(options) &&
	    !symbol->declarations.empty()) {
		exportSpecifier =
		    find(symbol->declarations,
		         [](Node* d) { return isExportSpecifier(d); });
	}
	if (exportSpecifier != nullptr) {
		// When renaming at an export specifier, rename the export and not the
		// thing being exported.
		state->getReferencesAtExportSpecifier(
		    exportSpecifier->name(), symbol,
		    exportSpecifier->as<ExportSpecifier>(),
		    state->createSearch(node, originalSymbol,
		                        ImpExpKindUnknown /*comingFrom*/, "", {}),
		    /*addReferencesHere*/ true, /*alwaysGetReferences*/ true);
	} else if (node != nullptr && node->kind == Kind::DefaultKeyword &&
	           symbol->name == InternalSymbolNameDefault &&
	           symbol->parent != nullptr) {
		state->addReference(node, symbol, entryKindNode);
		state->searchForImportsOfExport(
		    node, symbol,
		    new ExportInfo{symbol->parent, ExportKindDefault});
	} else {
		refSearch* search = state->createSearch(
		    node, symbol, ImpExpKindUnknown /*comingFrom*/, "",
		    state->populateSearchSymbolSet(
		        symbol, node, options.use == referenceUseRename,
		        options.useAliasesForRename, options.implementations));
		state->getReferencesInContainerOrFiles(symbol, search);
	}

	return state->result;
}


// === dep stubs — removed when owner slice lands ===

// getAdjustedLocation — utilities.go (ls-coreA).
Node* getAdjustedLocation(Node* node, bool forRename, SourceFile* sourceFile) {
	TSC_UNREACHABLE("getAdjustedLocation — owned by ls-coreA");
}
Node* getAdjustedRenameLocation(Node* node) {
	TSC_UNREACHABLE("getAdjustedRenameLocation — owned by ls-coreA");
}

// nodeIsEligibleForRename — rename.go (ls-coreC).
bool nodeIsEligibleForRename(Node* node) {
	TSC_UNREACHABLE("nodeIsEligibleForRename — owned by ls-coreC");
}

// getContextualTypeFromParentOrAncestorTypeNode — utilities.go (ls-coreA).
checker::Type* getContextualTypeFromParentOrAncestorTypeNode(
    Node* node, checker::Checker* checker) {
	TSC_UNREACHABLE(
	    "getContextualTypeFromParentOrAncestorTypeNode — owned by ls-coreA");
}

// getMeaningFromLocation — utilities.go:807 (ls-coreA).
SemanticMeaning getMeaningFromLocation(Node* node) {
	TSC_UNREACHABLE("getMeaningFromLocation — owned by ls-coreA");
}

// getIntersectingMeaningFromDeclarations — utilities.go:881 (ls-coreA).
SemanticMeaning getIntersectingMeaningFromDeclarations(
    Node* node, Symbol* symbol, SemanticMeaning defaultMeaning) {
	TSC_UNREACHABLE(
	    "getIntersectingMeaningFromDeclarations — owned by ls-coreA");
}

// getContainingObjectLiteralElement — utilities.go:1261 (ls-coreA).
Node* getContainingObjectLiteralElement(Node* node) {
	TSC_UNREACHABLE(
	    "getContainingObjectLiteralElement — owned by ls-coreA");
}

// isImplementation / isImplementationExpression — utilities.go:396/409
// (ls-coreA).
bool isImplementation(Node* node) {
	TSC_UNREACHABLE("isImplementation — owned by ls-coreA");
}
bool isImplementationExpression(Node* node) {
	TSC_UNREACHABLE("isImplementationExpression — owned by ls-coreA");
}

// getContainingNodeIfInHeritageClause — utilities.go:436 (ls-coreA).
Node* getContainingNodeIfInHeritageClause(Node* node) {
	TSC_UNREACHABLE("getContainingNodeIfInHeritageClause — owned by ls-coreA");
}

// getPropertySymbolsFromBaseTypes — utilities.go:963 (ls-coreA).
Symbol* getPropertySymbolsFromBaseTypes(
    Symbol* symbol, const std::string& propertyName, checker::Checker* checker,
    const std::function<Symbol*(Symbol*)>& cb) {
	TSC_UNREACHABLE("getPropertySymbolsFromBaseTypes — owned by ls-coreA");
}

// getParentSymbolsOfPropertyAccess — utilities.go:935 (ls-coreA).
std::vector<Symbol*> getParentSymbolsOfPropertyAccess(
    Node* location, Symbol* symbol, checker::Checker* ch) {
	TSC_UNREACHABLE("getParentSymbolsOfPropertyAccess — owned by ls-coreA");
}

// getNonModuleSymbolOfMergedModuleSymbol — utilities.go:62 (ls-coreA).
Symbol* getNonModuleSymbolOfMergedModuleSymbol(Symbol* symbol) {
	TSC_UNREACHABLE(
	    "getNonModuleSymbolOfMergedModuleSymbol — owned by ls-coreA");
}

// getPropertySymbolFromBindingElement — utilities.go:996 (ls-coreA).
Symbol* getPropertySymbolFromBindingElement(checker::Checker* checker,
                                            Node* bindingElement) {
	TSC_UNREACHABLE(
	    "getPropertySymbolFromBindingElement — owned by ls-coreA");
}

// getContainerNode — utilities.go:448 (ls-coreA).
Node* getContainerNode(Node* node) {
	TSC_UNREACHABLE("getContainerNode — owned by ls-coreA");
}

// isTypeKeyword — utilities.go:345 (ls-coreA).
bool isTypeKeyword(Kind kind) {
	TSC_UNREACHABLE("isTypeKeyword — owned by ls-coreA");
}

// isReadonlyTypeOperator — utilities.go:420 (ls-coreA).
bool isReadonlyTypeOperator(Node* node) {
	TSC_UNREACHABLE("isReadonlyTypeOperator — owned by ls-coreA");
}

// isJumpStatementTarget / isLabelOfLabeledStatement / getTargetLabel —
// utilities.go:424/428/1011 (ls-coreA).
bool isJumpStatementTarget(Node* node) {
	TSC_UNREACHABLE("isJumpStatementTarget — owned by ls-coreA");
}
bool isLabelOfLabeledStatement(Node* node) {
	TSC_UNREACHABLE("isLabelOfLabeledStatement — owned by ls-coreA");
}
Node* getTargetLabel(Node* referenceNode, const std::string& labelName) {
	TSC_UNREACHABLE("getTargetLabel — owned by ls-coreA");
}

// isThis — utilities.go:240 (ls-coreA).
bool isThis(Node* node) {
	TSC_UNREACHABLE("isThis — owned by ls-coreA");
}

// isLiteralNameOfPropertyDeclarationOrIndexAccess /
// isNameOfModuleDeclaration /
// isExpressionOfExternalModuleImportEqualsDeclaration —
// utilities.go:353/191/198 (ls-coreA).
bool isLiteralNameOfPropertyDeclarationOrIndexAccess(Node* node) {
	TSC_UNREACHABLE(
	    "isLiteralNameOfPropertyDeclarationOrIndexAccess — owned by ls-coreA");
}
bool isNameOfModuleDeclaration(Node* node) {
	TSC_UNREACHABLE("isNameOfModuleDeclaration — owned by ls-coreA");
}
bool isExpressionOfExternalModuleImportEqualsDeclaration(Node* node) {
	TSC_UNREACHABLE(
	    "isExpressionOfExternalModuleImportEqualsDeclaration — owned by "
	    "ls-coreA");
}

// isObjectBindingElementWithoutPropertyName — utilities.go:377 (ls-coreA).
bool isObjectBindingElementWithoutPropertyName(Node* bindingElement) {
	TSC_UNREACHABLE(
	    "isObjectBindingElementWithoutPropertyName — owned by ls-coreA");
}

// isStaticSymbol — utilities.go:388 (ls-coreA).
bool isStaticSymbol(Symbol* symbol) {
	TSC_UNREACHABLE("isStaticSymbol — owned by ls-coreA");
}

// getReferenceAtPosition — utilities.go:1312 (ls-coreA).
refInfo* getReferenceAtPosition(SourceFile* sourceFile, int position,
                                compiler::SimpleProgram* program) {
	TSC_UNREACHABLE("getReferenceAtPosition — owned by ls-coreA");
}

// getLocalSymbolForExportSpecifier — utilities.go:73 (ls-coreA).
Symbol* getLocalSymbolForExportSpecifier(Node* referenceLocation,
                                         Symbol* referenceSymbol,
                                         ExportSpecifier* exportSpecifier,
                                         checker::Checker* ch) {
	TSC_UNREACHABLE("getLocalSymbolForExportSpecifier — owned by ls-coreA");
}

// toContextRange — utilities.go:1300 (ls-coreA).
TextRange* toContextRange(TextRange* textRange, SourceFile* contextFile,
                          Node* context) {
	TSC_UNREACHABLE("toContextRange — owned by ls-coreA");
}

// isModuleSpecifierLike — utilities.go:48 (ls-coreA).
bool isModuleSpecifierLike(Node* node) {
	TSC_UNREACHABLE("isModuleSpecifierLike — owned by ls-coreA");
}

// getAllSuperTypeNodes — utilities.go:922 (ls-coreA).
std::vector<HeritageClauseElement*> getAllSuperTypeNodes(Node* node) {
	TSC_UNREACHABLE("getAllSuperTypeNodes — owned by ls-coreA");
}

// getQuickInfoAndDeclarationAtLocation — hover.go:426 (ls-coreC).
symbolDisplayInfo getQuickInfoAndDeclarationAtLocation(
    checker::Checker* c, Symbol* symbol, Node* node,
    checker::VerbosityContext* vc, bool vsCapability,
    SemanticMeaning meaning) {
	TSC_UNREACHABLE("getQuickInfoAndDeclarationAtLocation — owned by ls-coreC");
}

} // namespace tsc::ls
