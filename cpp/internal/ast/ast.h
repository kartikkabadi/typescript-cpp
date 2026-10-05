// Port of tsc/internal/ast — Node, NodeList, ModifierList, NodeFactory and the
// small value structs. Node-kind payloads (struct X : Node) and the kind
// dispatch live in nodes_generated.h, which is included at the bottom of this
// file once every type it needs is declared.
#pragma once


#include <cassert>
#include <atomic>
#include <cstdint>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "internal/ast/flags.h"
#include "internal/ast/kind.h"
#include "internal/ast/symbol.h"
#include "internal/core/arena.h"
#include "internal/core/text.h"
#include "internal/core/types.h"
#include "internal/diagnostics/diagnostics.h"

namespace tsc {

[[noreturn]] inline void tscUnreachable(const char* msg) {
	std::fprintf(stderr, "tsc internal error: %s\n", msg);
	std::abort();
}
#define TSC_UNREACHABLE(msg) ::tsc::tscUnreachable(msg)
#define TSC_ASSERT(cond, msg) assert(((void)(msg), (cond)))

struct Node;
struct NodeList;
struct ModifierList;
class NodeFactory;
struct NodeVisitor;
struct NodeVisitorHooks;
struct SourceFile;
struct SourceFileParseOptions;

#include "internal/ast/nodes_fwd_decl.inc"
struct Diagnostic;
struct FlowNode;
struct PositionMap;
struct ContentMapperSourceFileInfo;
struct DeclarationDataRef;
struct ExportableDataRef;
struct LocalsContainerDataRef;
struct FlowNodeDataRef;
struct FunctionLikeDataRef;
struct BodyDataRef;
struct ClassLikeDataRef;
struct LiteralLikeDataRef;
struct TemplateLiteralLikeDataRef;
struct PatternAmbientModule {
	std::string pattern;
	Symbol* symbol = nullptr;
};

using NodeId = uint32_t;
using TokenSyntaxKind = Kind;
using JsxTokenSyntaxKind = Kind;
using KeywordSyntaxKind = Kind;
using ModifierSyntaxKind = Kind;

// ---------------------------------------------------------------------------
// Node
// ---------------------------------------------------------------------------

// Go struct copies (node.Clone) duplicate atomics; give ours a copy ctor so
// generated payload structs remain copyable.
template <class T>
struct CopyableAtomic : std::atomic<T> {
	CopyableAtomic() = default;
	CopyableAtomic(T v) : std::atomic<T>(v) {}
	CopyableAtomic(const CopyableAtomic& o) : std::atomic<T>(o.load()) {}
	CopyableAtomic& operator=(const CopyableAtomic& o) {
		this->store(o.load());
		return *this;
	}
};

struct Node {
	Kind kind = Kind::Unknown;
	NodeFlags flags = NodeFlagsNone;
	TextRange loc;
	Node* parent = nullptr;
	mutable CopyableAtomic<NodeId> id{0};

	TextPos pos() const { return loc.pos(); }
	TextPos end() const { return loc.end(); }
	TextPos len() const { return loc.len(); }
	TextRange posEnd() const { return loc; }

	Node* asNode() { return this; }
	const Node* asNode() const { return this; }

	template <class T>
	T* as() {
		return static_cast<T*>(this);
	}
	template <class T>
	const T* as() const {
		return static_cast<const T*>(this);
	}

	// visit helpers used by the generated forEachChild bodies
	template <class F>
	static bool visitChild(F&& v, Node* c) {
		return c != nullptr && v(c);
	}
	template <class F>
	static bool visitChildList(F&& v, const NodeList* l);
	template <class F>
	static bool visitChildModifiers(F&& v, const ModifierList* l);
	template <class F>
	static bool visitChildNodeList(F&& v, const NodeList* l) {
		return visitChildList(v, l);
	}

	bool contains(Node* descendant) const {
		while (descendant != nullptr) {
			if (descendant == this)
				return true;
			descendant = descendant->parent;
		}
		return false;
	}

	// --- accessors declared here, defined in nodes_generated.h or ast.cpp ---
#include "internal/ast/nodes_node_decl.inc"

	std::vector<Node*> jsDoc(SourceFile* file = nullptr);
	std::vector<Node*> eagerJSDoc(SourceFile* file = nullptr);

	ModifierFlags modifierFlags() const;
	std::vector<Node*> modifierNodes() const;
	std::vector<Node*> decorators() const;
	Symbol* symbol() const;
	Symbol* localSymbol() const;
	SymbolTable* locals() const;
	Node* nextContainer() const;
	std::string text() const;
	std::string rawText() const;
	Node* body() const;
	Node* expression() const;
	Node* type() const;
	Node* initializer() const;
	Node* tagName() const;
	Node* questionToken() const;
	Node* postfixToken() const;
	Node* questionDotToken() const;
	Node* propertyName() const;
	Node* propertyNameOrName() const;
	Node* label() const;
	Node* attributes() const;
	Node* moduleSpecifier() const;
	Node* importClause() const;
	Node* statement() const;
	Node* typeExpression() const;
	Node* className() const;
	NodeList* argumentList() const;
	std::vector<Node*> arguments() const;
	NodeList* typeArgumentList() const;
	std::vector<Node*> typeArguments() const;
	NodeList* typeParameterList() const;
	std::vector<Node*> typeParameters() const;
	NodeList* memberList() const;
	std::vector<Node*> members() const;
	NodeList* statementList() const;
	std::vector<Node*> statements() const;
	bool canHaveStatements() const;
	NodeList* elementList() const;
	std::vector<Node*> elements() const;
	NodeList* propertyList() const;
	std::vector<Node*> properties() const;
	NodeList* commentList() const;
	std::vector<Node*> comments() const;
	NodeList* parameterList() const;
	std::vector<Node*> parameters() const;
	NodeList* children() const;
	bool isTypeOnly() const;

	// mutable setters (Go MutableNode.Set*) — assign the matching child field.
	void setType(Node* t);
	void setExpression(Node* expr);
	void setInitializer(Node* initializer);
};

// ---------------------------------------------------------------------------
// NodeList / ModifierList
// ---------------------------------------------------------------------------

struct NodeList {
	TextRange loc;
	std::vector<Node*> nodes;

	TextPos pos() const { return loc.pos(); }
	TextPos end() const { return loc.end(); }
	bool hasTrailingComma() const {
		return !nodes.empty() && nodes.back()->end() < loc.end();
	}

	// Clone (ast.go): a new NodeList over the same nodes, same Loc.
	NodeList* clone(NodeFactory& f);
};

struct ModifierList : NodeList {
	ModifierFlags ModifierFlags{};

	// Clone (ast.go): a new ModifierList copying loc/nodes/ModifierFlags.
	ModifierList* clone(NodeFactory& f);
};

template <class F>
bool Node::visitChildList(F&& v, const NodeList* l) {
	if (l == nullptr)
		return false;
	for (Node* c : l->nodes) {
		if (v(c))
			return true;
	}
	return false;
}

template <class F>
bool Node::visitChildModifiers(F&& v, const ModifierList* l) {
	return visitChildList(v, static_cast<const NodeList*>(l));
}

// ---------------------------------------------------------------------------
// Small value structs (mirroring Go field names)
// ---------------------------------------------------------------------------

enum class CommentDirectiveKind : int32_t {
	Unknown = 0,
	ExpectError = 1,
	Ignore = 2,
};

struct CommentRange : TextRange {
	Kind kind = Kind::Unknown;
	bool HasTrailingNewLine = false;
};

struct PragmaArgument : TextRange {
	std::string Name;
	std::string Value;
};

using PragmaKindFlags = uint8_t;
inline constexpr PragmaKindFlags PragmaKindTripleSlashXML = 1 << 0;
inline constexpr PragmaKindFlags PragmaKindSingleLine = 1 << 1;
inline constexpr PragmaKindFlags PragmaKindMultiLine = 1 << 2;
inline constexpr PragmaKindFlags PragmaKindFlagsNone = 0;
inline constexpr PragmaKindFlags PragmaKindAll =
	PragmaKindTripleSlashXML | PragmaKindSingleLine | PragmaKindMultiLine;
inline constexpr PragmaKindFlags PragmaKindDefault = PragmaKindAll;

struct PragmaArgumentSpecification {
	std::string Name;
	bool Optional = false;
	bool CaptureSpan = false;
};
struct PragmaSpecification {
	std::vector<PragmaArgumentSpecification> Args;
	PragmaKindFlags Kind = PragmaKindDefault;
};

struct Pragma : CommentRange {
	std::string Name;
	std::unordered_map<std::string, PragmaArgument> Args;
};

struct FileReference : TextRange {
	std::string FileName;
	ResolutionMode ResolutionMode = ResolutionMode::None;
	bool Preserve = false;
};

struct CommentDirective {
	TextRange Loc;
	CommentDirectiveKind Kind = CommentDirectiveKind::Unknown;
};

struct CheckJsDirective {
	bool Enabled = false;
	CommentRange Range;
};

struct SourceFileMetaData {
	std::string PackageJsonType;
	std::string PackageJsonDirectory;
	ResolutionMode ImpliedNodeFormat = ResolutionMode::None;
};

struct ExternalModuleIndicatorOptions {
	JsxEmit JSX = JsxEmit::None;
	bool Force = false;
};

struct SourceFileParseOptions {
	std::string FileName;
	std::string Path;
	ExternalModuleIndicatorOptions ExternalModuleIndicatorOptions;
};

enum class MappedDiagnosticDirectivePolicy : uint8_t {
	Ignore = 0,
	Expect = 1,
};

struct MappedDiagnosticDirective {
	TextRange OriginalRange;
	TextRange VirtualRange;
	MappedDiagnosticDirectivePolicy Policy =
		MappedDiagnosticDirectivePolicy::Ignore;
	int32_t UnusedCode = 0;
	std::string UnusedMessageText;
	std::string Source;
};

namespace spanmap {
struct SpanMap;
} // namespace spanmap

// ContentMapperSourceFileInfo — ast.go:2618
struct ContentMapperSourceFileInfo {
	std::string ContentMapper;
	std::string TransformIdentity;
	SourceFileParseOptions ParseOptions;
	std::string VirtualFileName;
	std::string OriginalText;
	spanmap::SpanMap* SpanMap = nullptr;
	std::vector<MappedDiagnosticDirective> DiagnosticDirectives;
	std::vector<SourceFile*> SupplementalSourceFiles;
	SourceFile* CanonicalSourceFile = nullptr;
};

struct Uint128 {
	uint64_t lo = 0, hi = 0;
	bool operator==(const Uint128&) const = default;
};

// std::once replacement used for lazily-computed SourceFile fields.
struct OnceFlag {
	template <class F>
	void run(F&& fn) {
		std::call_once(once_, fn);
	}
	std::once_flag once_;
};

struct TokenCacheKey {
	const Node* parent;
	TextRange loc;
	bool operator==(const TokenCacheKey&) const = default;
};

struct TokenCacheKeyHash {
	size_t operator()(const TokenCacheKey& k) const {
		size_t h = reinterpret_cast<uintptr_t>(k.parent);
		h = h * 0x9E3779B97F4A7C15ull ^ static_cast<uint32_t>(k.loc.pos());
		return h * 0x9E3779B97F4A7C15ull ^ static_cast<uint32_t>(k.loc.end());
	}
};

// RepopulateDiagnosticKind (ast/diagnostic.go) — kind of repopulation for a
// diagnostic chain entry recomputed during incremental builds.
enum class RepopulateDiagnosticKind : int32_t {
	None = 0,
	ModeMismatch = 1,
	ModuleNotFound = 2,
};

struct RepopulateDiagnosticInfo {
	RepopulateDiagnosticKind kind = RepopulateDiagnosticKind::None;
	std::string moduleReference;
	ResolutionMode mode = ResolutionModeNone;
	std::string packageName;
};

struct Diagnostic {
	SourceFile* file = nullptr;
	TextRange loc;
	int32_t code = 0;
	DiagnosticCategory category = DiagnosticCategory::Error;
	std::string source; // external-source prefix (e.g. a content mapper's name); empty = "TS"
	const DiagnosticMessage* message = nullptr;
	std::string messageText;
	const char* messageKey = "";
	std::vector<std::string> messageArgs;
	std::vector<Diagnostic*> messageChain;
	std::vector<Diagnostic*> relatedInformation;
	bool reportsUnnecessary = false;
	bool reportsDeprecated = false;
	bool skippedOnNoEmit = false;
	RepopulateDiagnosticInfo* repopulateInfo = nullptr;

	SourceFile* File() const { return file; }
	int Pos() const { return loc.pos(); }
	int End() const { return loc.end(); }
	int Len() const { return loc.len(); }
	TextRange Loc() const { return loc; }
	int32_t Code() const { return code; }
	DiagnosticCategory Category() const { return category; }
	std::string_view Source() const { return source; }
	std::string_view MessageText() const { return messageText; }
	std::string_view MessageKey() const { return messageKey; }
	const std::vector<std::string>& MessageArgs() const { return messageArgs; }
	const std::vector<Diagnostic*>& MessageChain() const { return messageChain; }
	const std::vector<Diagnostic*>& RelatedInformation() const { return relatedInformation; }
	bool ReportsUnnecessary() const { return reportsUnnecessary; }
	bool ReportsDeprecated() const { return reportsDeprecated; }
	bool SkippedOnNoEmit() const { return skippedOnNoEmit; }

	void SetFile(SourceFile* f) { file = f; }
	void SetLocation(TextRange l) { loc = l; }
	void SetCategory(DiagnosticCategory c) { category = c; }
	void SetSkippedOnNoEmit() { skippedOnNoEmit = true; }
	Diagnostic* AddMessageChain(Diagnostic* chain) {
		messageChain.insert(messageChain.begin(), chain);
		return this;
	}
	Diagnostic* SetRelatedInfo(std::vector<Diagnostic*> info) {
		relatedInformation = std::move(info);
		return this;
	}
	Diagnostic* AddRelatedInfo(Diagnostic* info) {
		relatedInformation.push_back(info);
		return this;
	}
	RepopulateDiagnosticInfo* RepopulateInfo() const { return repopulateInfo; }
	void SetRepopulateInfo(RepopulateDiagnosticInfo* info) { repopulateInfo = info; }
};

Diagnostic* newDiagnostic(SourceFile* file, TextRange loc,
                        const DiagnosticMessage* message,
                        const std::vector<std::string>& args = {});
Diagnostic* newDetachedDiagnostic(TextRange loc,
                                  const DiagnosticMessage* message,
                                  const std::vector<std::string>& args = {});
Diagnostic* newDiagnosticFromText(SourceFile* file, TextRange loc,
                                  int32_t code, DiagnosticCategory category,
                                  std::string_view text);

// ---------------------------------------------------------------------------
// NodeFactory
// ---------------------------------------------------------------------------

// NodeFactoryHooks — hooks invoked when the factory produces a node
// (mirrors ast.NodeFactoryHooks).
struct NodeFactoryHooks {
	std::function<void(Node* node)> onCreate;
	std::function<void(Node* node, Node* original)> onUpdate;
	std::function<void(Node* node, Node* original)> onClone;
};

class NodeFactory {
	Arena arena_;
	int32_t nodeCount_ = 0;
	int32_t textCount_ = 0;

public:
	NodeFactoryHooks hooks;

	NodeFactory() = default;
	explicit NodeFactory(NodeFactoryHooks h) : hooks(std::move(h)) {}
	NodeFactory(const NodeFactory&) = delete;
	NodeFactory& operator=(const NodeFactory&) = delete;

	Arena& arena() { return arena_; }
	int32_t nodeCount() const { return nodeCount_; }
	int32_t textCount() const { return textCount_; }

	template <class T>
	T* newData() {
		return arena_.alloc<T>();
	}

	Node* newNode(Kind kind, Node* data) {
		data->kind = kind;
		nodeCount_++;
		if (hooks.onCreate) {
			hooks.onCreate(data);
		}
		return data;
	}

	NodeList* newNodeList(std::vector<Node*> children) {
		auto* l = arena_.alloc<NodeList>();
		l->loc = TextRange::undefined();
		l->nodes = std::move(children);
		return l;
	}

	ModifierList* newModifierList(std::vector<Node*> children) {
		auto* l = arena_.alloc<ModifierList>();
		l->loc = TextRange::undefined();
		l->nodes = std::move(children);
		l->ModifierFlags = modifiersToFlags(l->nodes);
		return l;
	}

	CommentRange newCommentRange(Kind kind, int pos, int end,
	                             bool hasTrailingNewLine) {
		CommentRange cr;
		cr.pos_ = pos;
		cr.end_ = end;
		cr.kind = kind;
		cr.HasTrailingNewLine = hasTrailingNewLine;
		return cr;
	}

	static ModifierFlags modifiersToFlags(const std::vector<Node*>& modifiers);

	void releaseArenas() { arena_.clear(); }

	// DeepCloneReparse clones a subtree for JSDoc reparsing: children are
	// recursively cloned, parents re-threaded, and NodeFlagsReparsed set.
	Node* deepCloneReparse(Node* node);
	ModifierList* deepCloneReparseModifiers(ModifierList* modifiers);

#include "internal/ast/nodes_factory_decl.inc"
};

// NodeList::Clone / ModifierList::Clone (ast.go).
inline NodeList* NodeList::clone(NodeFactory& f) {
	NodeList* result = f.newNodeList(nodes);
	result->loc = loc;
	return result;
}

inline ModifierList* ModifierList::clone(NodeFactory& f) {
	auto* res = f.arena().alloc<ModifierList>();
	res->loc = loc;
	res->nodes = nodes;
	res->ModifierFlags = ModifierFlags;
	return res;
}

// updateNode / cloneNode (ast.go): shared tail of Update*/Clone — copy the
// original's Flags/Loc onto the new node and run the factory hooks.
inline Node* updateNode(Node* updated, Node* original,
                        const NodeFactoryHooks& hooks) {
	if (updated != original) {
		updated->flags = original->flags;
		updated->loc = original->loc;
		if (hooks.onUpdate) {
			hooks.onUpdate(updated, original);
		}
	}
	return updated;
}

inline Node* cloneNode(Node* updated, Node* original,
                       const NodeFactoryHooks& hooks) {
	updateNode(updated, original, hooks);
	if (updated != original && hooks.onClone) {
		hooks.onClone(updated, original);
	}
	return updated;
}

// ---------------------------------------------------------------------------
// subtree-facts helpers (free functions mirroring subtreefacts.go)
// ---------------------------------------------------------------------------

SubtreeFacts propagateSubtreeFacts(const Node* child);
SubtreeFacts propagateEraseableSyntaxSubtreeFacts(const Node* child);
SubtreeFacts propagateEraseableSyntaxListSubtreeFacts(const NodeList* children);
SubtreeFacts propagateNodeListSubtreeFacts(
	const NodeList* children, SubtreeFacts (*propagate)(const Node*));
SubtreeFacts propagateModifierListSubtreeFacts(const ModifierList* children);
SubtreeFacts propagateObjectBindingElementSubtreeFacts(const Node* child);
SubtreeFacts propagateBindingElementSubtreeFacts(const Node* child);
bool containsObjectRestOrSpread(const Node* node);
bool isThisIdentifier(const Node* node);
bool isAssignmentPattern(const Node* node);

// utilities.go
inline bool positionIsSynthesized(int pos) { return pos < 0; }
bool nodeIsMissing(const Node* node);
inline bool nodeIsPresent(const Node* node) { return !nodeIsMissing(node); }
bool nodeIsSynthesized(const Node* node);
inline bool rangeIsSynthesized(const TextRange& loc) {
	return positionIsSynthesized(loc.pos()) || positionIsSynthesized(loc.end());
}
const SourceFile* getSourceFileOfNode(const Node* node);
inline SourceFile* getSourceFileOfNode(Node* node) {
	return const_cast<SourceFile*>(
		getSourceFileOfNode(static_cast<const Node*>(node)));
}
inline bool isJSDocNode(const Node* node) {
	return node->kind >= KindFirstJSDocNode && node->kind <= KindLastJSDocNode;
}
// Registered by the parser package; resolves lazy JSDoc on demand.
extern std::vector<Node*> (*parseJSDocForNode)(SourceFile*, Node*);

enum class JSDeclarationKind {
	None,
	ModuleExports,
	ExportsProperty,
	ThisProperty,
	Property,
	ObjectDefinePropertyValue,
	ObjectDefinePropertyExports,
};
JSDeclarationKind getAssignmentDeclarationKind(Node* node);
bool isAssignmentExpression(Node* node, bool excludeCompoundAssignment);
Node* getRightMostAssignedExpression(Node* node);
bool hasSamePropertyAccessName(Node* node1, Node* node2);
void setParentInChildren(Node* node);

// utilities.go — name-of-declaration resolution.
bool isTypeNodeKind(Kind kind);
inline bool isTypeNode(const Node* node) { return isTypeNodeKind(node->kind); }
bool isDeclarationName(Node* name);
bool isDeclaration(Node* node);                                   // utilities.go:1302
bool isDeclarationNameOrImportPropertyName(Node* name);           // utilities.go:1315
bool isLiteralComputedPropertyDeclarationName(Node* node);        // utilities.go:1324
bool isImportOrExportSpecifier(Node* node);                       // utilities.go:1351
bool isExternalModuleImportEqualsDeclaration(Node* node);         // utilities.go:1330
Node* getExternalModuleImportEqualsDeclarationExpression(Node* node);  // utilities.go:3287
bool isBindableObjectDefinePropertyCall(Node* node);              // utilities.go:1581
Node* tryGetClassImplementingOrExtendingHeritageClauseElement(Node* node,
                                                              bool* isImplements);  // utilities.go:1445
Node* tryGetClassExtendingExpressionWithTypeArguments(Node* node);  // utilities.go:1434
bool isExpressionWithTypeArgumentsInClassExtendsClause(Node* node); // utilities.go:1430
bool isClassOrInterfaceLike(Node* node);                          // utilities.go:535
bool isJSDocNameReferenceContext(Node* node);                     // utilities.go:4058
bool isThisInTypeQuery(Node* node);                               // utilities.go:3032
bool isRightSideOfQualifiedNameOrPropertyAccess(Node* node);      // utilities.go:3735
int compareNodePositions(Node* a, Node* b);                       // utilities.go:3791
Node* getReparsedNodeForNode(Node* node);                         // utilities.go:4548
Node* getJSDocRoot(Node* node);                                   // utilities.go:4066
Node* getJSDocHost(Node* node);                                   // utilities.go:4072
Node* getHostSignatureFromJSDoc(Node* node);                      // utilities.go:4082
inline bool isParseTreeNode(const Node* node) {
	return !(node->flags & NodeFlagsSynthesized);
}
bool isStringLiteralLike(Node* node);
bool isOptionalChain(Node* node);
bool isOptionalChainRoot(Node* node);
bool isOutermostOptionalChain(Node* node);
Node* getQuestionDotToken(Node* node);
bool isSignedNumericLiteral(Node* node);
bool isStringOrNumericLiteralLike(Node* node);
Node* skipParentheses(Node* node);
bool isAccessExpression(Node* node);
bool isEntityNameExpression(Node* node);
bool isInJSFile(Node* node);
bool isExportsIdentifier(Node* node);
bool isModuleIdentifier(Node* node);
Node* getElementOrPropertyAccessName(Node* node);
bool isModuleExportsAccessExpression(Node* node);
Node* getNameOfDeclaration(Node* declaration);
Node* getNonAssignedNameOfDeclaration(Node* declaration);
Node* tryGetPropertyNameOfBindingOrAssignmentElement(
	Node* bindingElement);
Node* getAssignedName(Node* node);

// utilities.go:3866,3879,3891,4046
std::vector<Node*> getElementsOfBindingOrAssignmentPattern(Node* pattern);
bool isLiteralExpression(Node* node);
bool isSuperProperty(Node* node);
bool isSuperCall(Node* node);
bool isEmptyObjectLiteral(Node* expression);
bool isEmptyArrayLiteral(Node* expression);
bool isDeclarationBindingElement(Node* bindingElement);
Node* getTargetOfBindingOrAssignmentElement(Node* bindingElement);
Node* getRestIndicatorOfBindingOrAssignmentElement(Node* bindingElement);

template <class T>
constexpr T ifElse(bool c, T a, T b) {
	return c ? a : b;
}

inline bool isKeyword(Kind k) {
	return k >= KindFirstKeyword && k <= KindLastKeyword;
}

// utilities.go / parseoptions.go — module & import helpers.
bool hasSyntacticModifier(Node* node, ModifierFlags flags);
bool isAmbientModule(Node* node);
bool isGlobalScopeAugmentation(Node* node);
bool isExternalModule(SourceFile* file);
bool isAnyImportSyntax(Node* node);
bool isImportNode(Node* node);
bool isAnyImportOrReExport(Node* node);
Node* getExternalModuleName(Node* node);
bool isLiteralImportTypeNode(Node* node);
bool isRequireCall(Node* node, bool requireStringLiteralLikeArgument);
bool isImportCall(Node* node);
bool isImportMeta(Node* node);
Node* getFirstIdentifier(Node* node);
bool isExternalModuleAugmentation(Node* node);
// utilities.go:3590
bool isLateVisibilityPaintedStatement(Node* node);
bool isEmittableImport(Node* node);
Node* getModuleSpecifierOfBareOrAccessedRequire(Node* node);
std::pair<ResolutionMode, bool> getResolutionModeOverride(
    Node* attributes,
    const std::function<bool(Node*, const DiagnosticMessage*)>& grammarErrorOnNode);
bool hasResolutionModeOverride(Node* node);
bool isResolutionModeOverrideHost(Node* node);
bool isJsxOpeningLikeElement(Node* node);
// utilities.go:3834 — GetSemanticJsxChildren.
std::vector<Node*> getSemanticJsxChildren(const std::vector<Node*>& children);
Node* getNodeAtPosition(SourceFile* file, int position, bool includeJSDoc);
void setImportsOfSourceFile(SourceFile* file, std::vector<Node*> imports);
bool forEachDynamicImportOrRequireCall(
    SourceFile* file, bool includeTypeSpaceImports,
    bool requireStringLiteralLikeArgument,
    const std::function<bool(Node* node, Node* argument)>& cb);
void setExternalModuleIndicator(SourceFile* file,
                                const ExternalModuleIndicatorOptions& opts);

}  // namespace tsc

namespace std {
template <>
struct hash<tsc::TokenCacheKey> : tsc::TokenCacheKeyHash {};
}  // namespace std

// Node-kind payloads + generated member definitions.
#include "internal/ast/nodes_generated.h"

// NodeVisitor machinery (visitor.go); needs the complete node structs, so it
// comes last. The generated clone/visitEachChild dispatch bodies live at the
// end of visitor.h (nodes_visitor_generated.h).
#include "internal/ast/visitor.h"

namespace tsc {

inline SubtreeFacts propagateSubtreeFacts(const Node* child) {
	if (child == nullptr)
		return SubtreeFactsNone;
	return child->propagateSubtreeFacts();
}
inline SubtreeFacts propagateEraseableSyntaxSubtreeFacts(const Node* child) {
	return child != nullptr ? SubtreeContainsTypeScript : SubtreeFactsNone;
}
inline SubtreeFacts propagateEraseableSyntaxListSubtreeFacts(const NodeList* children) {
	return children != nullptr ? SubtreeContainsTypeScript : SubtreeFactsNone;
}
inline SubtreeFacts propagateNodeListSubtreeFacts(
	const NodeList* children, SubtreeFacts (*propagate)(const Node*)) {
	if (children == nullptr)
		return SubtreeFactsNone;
	SubtreeFacts facts = SubtreeFactsNone;
	for (Node* c : children->nodes)
		facts |= propagate(c);
	return facts;
}
inline SubtreeFacts propagateModifierListSubtreeFacts(const ModifierList* children) {
	if (children == nullptr)
		return SubtreeFactsNone;
	return propagateNodeListSubtreeFacts(children, propagateSubtreeFacts);
}
inline SubtreeFacts propagateObjectBindingElementSubtreeFacts(const Node* child) {
	SubtreeFacts facts = propagateSubtreeFacts(child);
	if (facts & SubtreeContainsRestOrSpread) {
		facts &= ~SubtreeContainsRestOrSpread;
		facts |= SubtreeContainsObjectRestOrSpread |
		         SubtreeContainsESObjectRestOrSpread;
	}
	return facts;
}
inline SubtreeFacts propagateBindingElementSubtreeFacts(const Node* child) {
	return propagateSubtreeFacts(child) & ~SubtreeContainsRestOrSpread;
}

// ---------------------------------------------------------------------------
// Kind / node classification utilities (ast_generated.go, utilities.go)
// ---------------------------------------------------------------------------

inline bool isModifierKind(Kind kind) {
	switch (kind) {
	case Kind::AbstractKeyword:
	case Kind::AccessorKeyword:
	case Kind::AsyncKeyword:
	case Kind::ConstKeyword:
	case Kind::DeclareKeyword:
	case Kind::DefaultKeyword:
	case Kind::ExportKeyword:
	case Kind::InKeyword:
	case Kind::PrivateKeyword:
	case Kind::ProtectedKeyword:
	case Kind::PublicKeyword:
	case Kind::ReadonlyKeyword:
	case Kind::OutKeyword:
	case Kind::OverrideKeyword:
	case Kind::StaticKeyword:
		return true;
	default:
		break;
	}
	return false;
}

inline bool isAssignmentOperator(Kind kind) {
	switch (kind) {
	case Kind::EqualsToken:
	case Kind::PlusEqualsToken:
	case Kind::MinusEqualsToken:
	case Kind::AsteriskAsteriskEqualsToken:
	case Kind::AsteriskEqualsToken:
	case Kind::SlashEqualsToken:
	case Kind::PercentEqualsToken:
	case Kind::AmpersandEqualsToken:
	case Kind::BarEqualsToken:
	case Kind::CaretEqualsToken:
	case Kind::LessThanLessThanEqualsToken:
	case Kind::GreaterThanGreaterThanGreaterThanEqualsToken:
	case Kind::GreaterThanGreaterThanEqualsToken:
	case Kind::BarBarEqualsToken:
	case Kind::AmpersandAmpersandEqualsToken:
	case Kind::QuestionQuestionEqualsToken:
		return true;
	default:
		break;
	}
	return false;
}

inline bool isModifier(Node* node) { return isModifierKind(node->kind); }
inline bool isModifierLike(Node* node) {
	return isModifier(node) || isDecorator(node);
}
inline bool isQuestionToken(Node* node) {
	return node != nullptr && node->kind == Kind::QuestionToken;
}

inline bool isFunctionLikeDeclarationKind(Kind kind) {
	switch (kind) {
	case Kind::FunctionDeclaration:
	case Kind::MethodDeclaration:
	case Kind::Constructor:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::FunctionExpression:
	case Kind::ArrowFunction:
		return true;
	default:
		break;
	}
	return false;
}

inline bool isFunctionLikeDeclaration(Node* node) {
	return node != nullptr && isFunctionLikeDeclarationKind(node->kind);
}

inline bool isFunctionLikeKind(Kind kind) {
	switch (kind) {
	case Kind::MethodSignature:
	case Kind::CallSignature:
	case Kind::JSDocSignature:
	case Kind::ConstructSignature:
	case Kind::IndexSignature:
	case Kind::FunctionType:
	case Kind::ConstructorType:
		return true;
	default:
		break;
	}
	return isFunctionLikeDeclarationKind(kind);
}

inline bool isFunctionLike(Node* node) {
	return node != nullptr && isFunctionLikeKind(node->kind);
}

// OuterExpressionKinds (utilities.go:OEK*)
using OuterExpressionKinds = uint32_t;
inline constexpr OuterExpressionKinds OEKParentheses = 1 << 0;
inline constexpr OuterExpressionKinds OEKTypeAssertions = 1 << 1;
inline constexpr OuterExpressionKinds OEKNonNullAssertions = 1 << 2;
inline constexpr OuterExpressionKinds OEKPartiallyEmittedExpressions = 1 << 3;
inline constexpr OuterExpressionKinds OEKExpressionsWithTypeArguments = 1 << 4;
inline constexpr OuterExpressionKinds OEKSatisfies = 1 << 5;
inline constexpr OuterExpressionKinds OEKExcludeJSDocTypeAssertion = 1 << 6;
inline constexpr OuterExpressionKinds OEKAssignments = 1 << 7;
inline constexpr OuterExpressionKinds OEKComma = 1 << 8;
inline constexpr OuterExpressionKinds OEKAssertions =
	OEKTypeAssertions | OEKNonNullAssertions | OEKSatisfies;
inline constexpr OuterExpressionKinds OEKAll =
	OEKParentheses | OEKAssertions | OEKPartiallyEmittedExpressions |
	OEKExpressionsWithTypeArguments;
inline constexpr OuterExpressionKinds
	OEKAllExceptAssertionsOrExpressionsWithTypeArguments =
		OEKAll & ~OEKAssertions & ~OEKExpressionsWithTypeArguments;
inline constexpr OuterExpressionKinds OEKExpressionTypePassthrough =
	OEKParentheses | OEKAssignments | OEKComma;

inline bool isJSDocTypeAssertion(Node* node) {
	if (node == nullptr || !isParenthesizedExpression(node) ||
	    !isInJSFile(node)) {
		return false;
	}
	Node* expr = node->expression();
	return isAsExpression(expr) && expr->type() != nullptr &&
	       (expr->type()->flags & NodeFlagsReparsed) != 0;
}

// Determines whether node is an "outer expression" of the provided kinds
inline bool isOuterExpression(Node* node, OuterExpressionKinds kinds) {
	switch (node->kind) {
	case Kind::ParenthesizedExpression:
		return (kinds & OEKParentheses) != 0 &&
		       !((kinds & OEKExcludeJSDocTypeAssertion) != 0 &&
		         isJSDocTypeAssertion(node));
	case Kind::TypeAssertionExpression:
	case Kind::AsExpression:
		return (kinds & OEKTypeAssertions) != 0;
	case Kind::SatisfiesExpression:
		return (kinds & (OEKExpressionsWithTypeArguments | OEKSatisfies)) !=
		       0;
	case Kind::ExpressionWithTypeArguments:
		return (kinds & OEKExpressionsWithTypeArguments) != 0;
	case Kind::NonNullExpression:
		return (kinds & OEKNonNullAssertions) != 0;
	case Kind::PartiallyEmittedExpression:
		return (kinds & OEKPartiallyEmittedExpressions) != 0;
	case Kind::BinaryExpression:
		switch (node->as<BinaryExpression>()->OperatorToken->kind) {
		case Kind::EqualsToken:
			return (kinds & OEKAssignments) != 0;
		case Kind::CommaToken:
			return (kinds & OEKComma) != 0;
		default:
			break;
		}
		break;
	default:
		break;
	}
	return false;
}

// Descends into an expression, skipping past "outer expressions" of the
// provided kinds
inline Node* skipOuterExpressions(Node* node, OuterExpressionKinds kinds) {
	while (isOuterExpression(node, kinds)) {
		if (isBinaryExpression(node)) {
			node = node->as<BinaryExpression>()->Right;
		} else {
			node = node->expression();
		}
	}
	return node;
}

inline Node* skipParentheses(Node* node) {
	return skipOuterExpressions(node, OEKParentheses);
}

inline Node* skipTypeParentheses(Node* node) {
	while (isParenthesizedTypeNode(node)) {
		node = node->type();
	}
	return node;
}

inline Node* skipPartiallyEmittedExpressions(Node* node) {
	return skipOuterExpressions(node, OEKPartiallyEmittedExpressions);
}

inline bool isLeftHandSideExpressionKind(Kind kind) {
	switch (kind) {
	case Kind::PropertyAccessExpression:
	case Kind::ElementAccessExpression:
	case Kind::NewExpression:
	case Kind::CallExpression:
	case Kind::JsxElement:
	case Kind::JsxSelfClosingElement:
	case Kind::JsxFragment:
	case Kind::TaggedTemplateExpression:
	case Kind::ArrayLiteralExpression:
	case Kind::ParenthesizedExpression:
	case Kind::ObjectLiteralExpression:
	case Kind::ClassExpression:
	case Kind::FunctionExpression:
	case Kind::Identifier:
	case Kind::PrivateIdentifier:
	case Kind::RegularExpressionLiteral:
	case Kind::NumericLiteral:
	case Kind::BigIntLiteral:
	case Kind::StringLiteral:
	case Kind::NoSubstitutionTemplateLiteral:
	case Kind::TemplateExpression:
	case Kind::FalseKeyword:
	case Kind::NullKeyword:
	case Kind::ThisKeyword:
	case Kind::TrueKeyword:
	case Kind::SuperKeyword:
	case Kind::NonNullExpression:
	case Kind::ExpressionWithTypeArguments:
	case Kind::MetaProperty:
	case Kind::ImportKeyword:
	case Kind::MissingDeclaration:
		return true;
	default:
		break;
	}
	return false;
}

// Determines whether a node is a LeftHandSideExpression based only on its
// kind.
inline bool isLeftHandSideExpression(Node* node) {
	return isLeftHandSideExpressionKind(
		skipPartiallyEmittedExpressions(node)->kind);
}

// ast/utilities.go:415
inline bool isUnaryExpressionKind(Kind kind) {
	switch (kind) {
	case Kind::PrefixUnaryExpression:
	case Kind::PostfixUnaryExpression:
	case Kind::DeleteExpression:
	case Kind::TypeOfExpression:
	case Kind::VoidExpression:
	case Kind::AwaitExpression:
	case Kind::TypeAssertionExpression:
		return true;
	}
	return isLeftHandSideExpressionKind(kind);
}

// ast/utilities.go:427
inline bool isUnaryExpression(Node* node) {
	return isUnaryExpressionKind(skipPartiallyEmittedExpressions(node)->kind);
}

// ast/utilities.go:434
inline bool isExpressionKind(Kind kind) {
	switch (kind) {
	case Kind::ConditionalExpression:
	case Kind::YieldExpression:
	case Kind::ArrowFunction:
	case Kind::BinaryExpression:
	case Kind::SpreadElement:
	case Kind::AsExpression:
	case Kind::OmittedExpression:
	case Kind::PartiallyEmittedExpression:
	case Kind::SatisfiesExpression:
		return true;
	}
	return isUnaryExpressionKind(kind);
}

// Determines whether a node is an expression based only on its kind.
// ast/utilities.go:451
inline bool isExpression(Node* node) {
	return isExpressionKind(skipPartiallyEmittedExpressions(node)->kind);
}

inline ModifierFlags modifierToFlag(Kind token) {
	switch (token) {
	case Kind::StaticKeyword:
		return ModifierFlagsStatic;
	case Kind::PublicKeyword:
		return ModifierFlagsPublic;
	case Kind::ProtectedKeyword:
		return ModifierFlagsProtected;
	case Kind::PrivateKeyword:
		return ModifierFlagsPrivate;
	case Kind::AbstractKeyword:
		return ModifierFlagsAbstract;
	case Kind::AccessorKeyword:
		return ModifierFlagsAccessor;
	case Kind::ExportKeyword:
		return ModifierFlagsExport;
	case Kind::DeclareKeyword:
		return ModifierFlagsAmbient;
	case Kind::ConstKeyword:
		return ModifierFlagsConst;
	case Kind::DefaultKeyword:
		return ModifierFlagsDefault;
	case Kind::AsyncKeyword:
		return ModifierFlagsAsync;
	case Kind::ReadonlyKeyword:
		return ModifierFlagsReadonly;
	case Kind::OverrideKeyword:
		return ModifierFlagsOverride;
	case Kind::InKeyword:
		return ModifierFlagsIn;
	case Kind::OutKeyword:
		return ModifierFlagsOut;
	case Kind::Decorator:
		return ModifierFlagsDecorator;
	default:
		break;
	}
	return ModifierFlagsNone;
}

inline bool isParameterPropertyModifier(Kind kind) {
	return (modifierToFlag(kind) & ModifierFlagsParameterPropertyModifier) !=
	       0;
}

inline bool isClassMemberModifier(Kind token) {
	return isParameterPropertyModifier(token) ||
	       token == Kind::StaticKeyword || token == Kind::OverrideKeyword ||
	       token == Kind::AccessorKeyword;
}

inline bool canHaveIllegalDecorators(Node* node) {
	switch (node->kind) {
	case Kind::PropertyAssignment:
	case Kind::ShorthandPropertyAssignment:
	case Kind::FunctionDeclaration:
	case Kind::Constructor:
	case Kind::IndexSignature:
	case Kind::ClassStaticBlockDeclaration:
	case Kind::MissingDeclaration:
	case Kind::VariableStatement:
	case Kind::InterfaceDeclaration:
	case Kind::TypeAliasDeclaration:
	case Kind::EnumDeclaration:
	case Kind::ModuleDeclaration:
	case Kind::ImportEqualsDeclaration:
	case Kind::ImportDeclaration:
	case Kind::JSImportDeclaration:
	case Kind::NamespaceExportDeclaration:
	case Kind::ExportDeclaration:
	case Kind::ExportAssignment:
		return true;
	default:
		break;
	}
	return false;
}

inline bool canHaveDecorators(Node* node) {
	switch (node->kind) {
	case Kind::Parameter:
	case Kind::PropertyDeclaration:
	case Kind::MethodDeclaration:
	case Kind::GetAccessor:
	case Kind::SetAccessor:
	case Kind::ClassExpression:
	case Kind::ClassDeclaration:
		return true;
	default:
		break;
	}
	return false;
}

inline bool tagNamesAreEquivalent(Node* lhs, Node* rhs) {
	if (lhs->kind != rhs->kind) {
		return false;
	}
	switch (lhs->kind) {
	case Kind::Identifier:
		return lhs->text() == rhs->text();
	case Kind::ThisKeyword:
		return true;
	case Kind::JsxNamespacedName:
		return lhs->as<JsxNamespacedName>()->Namespace->text() ==
		           rhs->as<JsxNamespacedName>()->Namespace->text() &&
		       lhs->as<JsxNamespacedName>()->name->text() ==
		           rhs->as<JsxNamespacedName>()->name->text();
	case Kind::PropertyAccessExpression:
		return lhs->as<PropertyAccessExpression>()->name->text() ==
		           rhs->as<PropertyAccessExpression>()->name->text() &&
		       tagNamesAreEquivalent(lhs->expression(), rhs->expression());
	default:
		break;
	}
	TSC_UNREACHABLE("Unhandled case in tagNamesAreEquivalent");
}

// --- Binder-facing utilities (utilities.cpp) ---

NodeId getNodeId(Node* node);
SymbolId getSymbolId(Symbol* symbol);
SymbolTable& getMembers(Symbol* symbol);
SymbolTable& getExports(Symbol* symbol);
SymbolTable& getLocals(Node* container);
Node* getRootDeclaration(Node* node);
NodeFlags getCombinedNodeFlags(Node* node);
ModifierFlags getCombinedModifierFlags(Node* node);
bool isDeclarationStatementKind(Kind kind);
bool isStatementKindButNotDeclarationKind(Kind kind);
bool isBlockStatement(Node* node);
bool isStatement(Node* node);
bool isDeclarationStatement(Node* node);
bool isClassElement(Node* node);
bool isClassLike(Node* node);
bool isFunctionExpressionOrArrowFunction(Node* node);
bool isObjectLiteralOrClassExpressionMethodOrAccessor(Node* node);
bool isObjectLiteralMethod(Node* node);
bool isEnumConst(Node* node);
bool isAutoAccessorPropertyDeclaration(Node* node);
bool isStatic(Node* node);
Node* getThisContainer(Node* node, bool includeArrowFunctions,
                       bool includeClassComputedPropertyName);
bool isInTopLevelContext(Node* node);
bool isIdentifierName(Node* node);
Node* findAncestor(Node* node, const std::function<bool(Node*)>& callback);
Node* findAncestorKind(Node* node, Kind kind);
enum class FindAncestorResult : int32_t { False = 0, True, Quit };
inline FindAncestorResult toFindAncestorResult(bool b) {
	return b ? FindAncestorResult::True : FindAncestorResult::False;
}
Node* findAncestorOrQuit(
	Node* node, const std::function<FindAncestorResult(Node*)>& callback);
bool isNodeDescendantOf(Node* node, Node* ancestor);
bool isFunctionLikeOrClassStaticBlockDeclaration(Node* node);
FunctionFlags getFunctionFlags(Node* node);
bool isComputedNonLiteralName(Node* name);
std::string getTextOfPropertyName(Node* name);
bool tryGetTextOfPropertyName(Node* name, std::string& out);
inline bool isExclamationToken(Node* node) {
	return node != nullptr && node->kind == Kind::ExclamationToken;
}
inline bool isThisKeyword(Node* node) {
	return node != nullptr && node->kind == Kind::ThisKeyword;
}
inline bool isTypeQuery(Node* node) { return node->kind == Kind::TypeQuery; }
inline bool isInfinityOrNaNString(std::string_view name) {
	return name == "Infinity" || name == "-Infinity" || name == "NaN";
}
bool isBlockScope(Node* node, Node* parentNode);
Node* getEnclosingBlockScopeContainer(Node* node);
inline bool isAccessor(Node* node) {
	return node->kind == Kind::GetAccessor || node->kind == Kind::SetAccessor;
}
inline bool isEntityName(Node* node) {
	return node->kind == Kind::Identifier || node->kind == Kind::QualifiedName;
}
// utilities.go:2158 — EntityNameToString
std::string EntityNameToString(
	Node* name, const std::function<std::string(const Node*)>& getTextOfNode);
Node* getHeritageClause(Node* node, Kind kind);
std::vector<Node*> getHeritageElements(Node* node, Kind kind);
inline std::vector<Node*> getExtendsHeritageClauseElements(Node* node) {
	return getHeritageElements(node, Kind::ExtendsKeyword);
}
// ast/utilities.go:3131 — GetClassExtendsHeritageElement
inline Node* getClassExtendsHeritageElement(Node* node) {
	auto elements = getHeritageElements(node, Kind::ExtendsKeyword);
	return elements.empty() ? nullptr : elements.front();
}
// ast/utilities.go:1147 — IsFunctionOrModuleBlock
inline bool isFunctionOrModuleBlock(Node* node) {
	return isSourceFile(node) || isModuleBlock(node) ||
	       (isBlock(node) && isFunctionLike(node->parent));
}
// ast/utilities.go:845 — WalkUpParenthesizedExpressions
inline Node* walkUpParenthesizedExpressions(Node* node) {
	while (node != nullptr && node->kind == Kind::ParenthesizedExpression) {
		node = node->parent;
	}
	return node;
}
Node* getHeritageClauseElementName(Node* node);
bool isPrologueDirective(Node* node);
bool isDottedName(Node* node);
bool isPushOrUnshiftIdentifier(Node* node);
bool isDestructuringAssignment(Node* node);
bool isExpressionOfOptionalChainRoot(Node* node);
bool isNullishCoalesce(Node* node);
bool isDynamicName(Node* name);
bool hasDynamicName(Node* declaration);
bool isPropertyNameLiteral(Node* node);
// utilities.go:279 — IsPropertyName (Identifier|PrivateIdentifier|String|
// Numeric|ComputedPropertyName).
inline bool isPropertyName(Node* node) {
	switch (node->kind) {
	case Kind::Identifier:
	case Kind::PrivateIdentifier:
	case Kind::StringLiteral:
	case Kind::NumericLiteral:
	case Kind::ComputedPropertyName:
		return true;
	}
	return false;
}

Node* getPropertyNameForPropertyOrComputedPropertyName(Node* name);
bool isLogicalOrCoalescingBinaryOperator(Kind token);
bool isLogicalOrCoalescingBinaryExpression(Node* expr);
bool isLogicalExpression(Node* node);
bool isLogicalOrCoalescingAssignmentOperator(Kind token);
bool isLogicalOrCoalescingAssignmentExpression(Node* expr);
bool isBooleanLiteral(Node* node);
Node* getImmediatelyInvokedFunctionExpression(Node* fn);
bool isExpandoInitializer(Node* declaration, Node* initializer);
bool isVariableDeclarationInitializedToRequire(Node* node);
bool isAliasSymbolDeclaration(Node* node);
SymbolTable& getSymbolTable(SymbolTable& data);
Diagnostic* newDiagnosticChain(Diagnostic* chain, const DiagnosticMessage* message,
							   const std::vector<std::string>& args = {});
Node* getImportAttributes(Node* node);
bool hasImportAttributes(Node* node);
bool isPartOfTypeOnlyImportOrExportDeclaration(Node* node);
bool isVariableDeclarationInitializedWithRequireHelper(Node* node,
                                                       bool allowAccessedRequire);
bool isVariableDeclarationInitializedToBareOrAccessedRequire(Node* node);
bool isPotentiallyExecutableNode(Node* node);
bool isCatchClauseVariableDeclarationOrBindingElement(Node* declaration);
bool isBlockOrCatchScoped(Node* declaration);
bool isPartOfParameterDeclaration(Node* node);
bool isParameterPropertyDeclaration(Node* node, Node* parent);
bool isAsyncFunction(Node* node);
bool isBindingPattern(Node* node);
bool isForInOrOfStatement(Node* node);
Node* getAssignmentTarget(Node* node);
bool isAssignmentTarget(Node* node);
Node* skipPartiallyEmittedExpressions(Node* node);
bool isLeftHandSideExpression(Node* node);
std::pair<std::string, bool> tryGetAmbientModuleNameFromSymbolName(
	const std::string& s);
bool isAmbientModuleSymbolName(const std::string& s);
bool isExternalOrCommonJSModule(SourceFile* file);
bool isNonLocalAlias(Symbol* symbol, SymbolFlags excludes);
bool isPlainJSFile(SourceFile* file, Tristate checkJs);
bool nodeKindIs(Node* node, Kind k1);
bool nodeKindIs(Node* node, Kind k1, Kind k2);
bool nodeKindIs(Node* node, Kind k1, Kind k2, Kind k3);
bool nodeKindIs(Node* node, std::initializer_list<Kind> kinds);
bool isJsonSourceFile(SourceFile* file);
bool nodeHasName(Node* statement, Node* id);
ModuleInstanceState getModuleInstanceState(Node* node);
bool isInstantiatedModule(Node* node, bool preserveConstEnums);
bool canHaveSymbol(Node* node);
bool hasQuestionToken(Node* node);
bool hasAccessorModifier(Node* node);
bool hasStaticModifier(Node* node);
bool isPartOfTypeQuery(Node* node);
Node* getContainingClass(Node* node);
bool isModuleAugmentationExternal(Node* node);
bool isImplicitlyExportedJSDocDeclaration(Node* node);
bool moduleExportNameIsDefault(Node* node);
bool expressionIsAlias(Node* node);
bool isMethodOrAccessor(Node* node);
bool isPrivateIdentifierClassElementDeclaration(Node* node);
Node* getLeftmostAccessExpression(Node* expr);
bool isLogicalBinaryOperator(Kind token);
bool isModuleOrEnumDeclaration(Node* node);
bool isGlobalSourceFile(Node* node);
bool isConstTypeReference(Node* node);
bool isConstAssertion(Node* node);
Node* getDeclarationOfKind(Symbol* symbol, Kind kind);
Node* findConstructorDeclaration(Node* node);

// utilities.go — ported with the checker bootstrap slice.
bool isTypeDeclaration(Node* node);
bool isTypeDeclarationName(Node* name);
bool isTypeOnlyImportDeclaration(Node* node);
bool isTypeOnlyImportOrExportDeclaration(Node* node);
bool isExclusivelyTypeOnlyImportOrExport(Node* node);
bool isValidTypeOnlyAliasUseSite(Node* useSite);
bool isShorthandPropertyNameUseSite(Node* useSite);
bool isExpressionNode(Node* node);
bool isInExpressionContext(Node* node);
bool isPartOfTypeNode(Node* node);
bool isJsxTagName(Node* node);
bool isJSDocLinkLike(Node* node);
bool isAssertionExpression(Node* node);
bool isPropertyAccessOrQualifiedName(Node* node);
bool hasDecorators(Node* node);
bool nodeCanBeDecorated(bool useLegacyDecorators, Node* node, Node* parent,
                        Node* grandparent);
bool nodeIsDecorated(bool useLegacyDecorators, Node* node, Node* parent,
                     Node* grandparent);
bool nodeOrChildIsDecorated(bool useLegacyDecorators, Node* node, Node* parent,
                            Node* grandparent);
bool childIsDecorated(bool useLegacyDecorators, Node* node, Node* parent);
Node* getFirstConstructorWithBody(Node* node);
Node* getThisParameter(Node* signature);
bool isThisParameter(Node* node);
bool classOrConstructorParameterIsDecorated(bool useLegacyDecorators,
                                            Node* node);
bool classElementOrClassElementParameterIsDecorated(bool useLegacyDecorators,
                                                    Node* node, Node* parent);
struct AllAccessorDeclarations {
	Node* firstAccessor{};
	Node* secondAccessor{};
	Node* setAccessor{};
	Node* getAccessor{};
};
AllAccessorDeclarations getAllAccessorDeclarationsForDeclaration(
	Node* accessor, const std::vector<Node*>& declarationsOfSymbol);
AllAccessorDeclarations getAllAccessorDeclarations(
	const std::vector<Node*>& parentDeclarations, Node* accessor);
std::string getPropertyNameForPropertyNameNode(Node* name);
bool hasAbstractModifier(Node* node);
bool hasAmbientModifier(Node* node);
bool isModuleWithStringLiteralName(Node* node);

// === slice: grammarchecks — utilities.go helpers ===
bool isCommaExpression(Node* node);
bool isCommaSequence(Node* node);
bool isIterationStatement(Node* node, bool lookInLabeledStatements);
bool isStringLiteralLikeType(Node* node);
Node* walkUpParenthesizedTypes(Node* node);
bool canHaveModifiers(Node* node);
bool canHaveIllegalModifiers(Node* node);
bool hasModifier(Node* node, ModifierFlags flags);
Node* getContainingFunction(Node* node);
bool isEffectiveExternalModule(SourceFile* node, const CompilerOptions* compilerOptions);
bool isBindableStaticElementAccessExpression(Node* node, bool excludeThisKeyword);
bool isLiteralLikeElementAccess(Node* node);
bool isBindableStaticAccessExpression(Node* node, bool excludeThisKeyword);
bool isBindableStaticNameExpression(Node* node, bool excludeThisKeyword);
bool isPrototypeAccess(Node* node);
bool isNameOfHeritageClauseTypeReference(Node* node);
bool isCallOrNewExpression(Node* node);
bool isExpandoPropertyDeclaration(Node* node);

// === slice: program === — utilities.go/parseoptions.go format + pragma helpers
bool isSourceFileJS(SourceFile* file);
bool isCheckJSEnabledForFile(SourceFile* sourceFile,
                             const CompilerOptions* compilerOptions);
bool shouldTransformImportCall(std::string_view fileName,
                               const CompilerOptions* options,
                               ModuleKind impliedNodeFormatForEmit);
ModuleKind getImpliedNodeFormatForFile(std::string_view path,
                                       std::string_view packageJsonType);
ResolutionMode getImpliedNodeFormatForEmitWorker(
    std::string_view fileName, ModuleKind emitModuleKind,
    const SourceFileMetaData& sourceFileMetaData);
ModuleKind getEmitModuleFormatOfFileWorker(
    std::string_view fileName, const CompilerOptions* options,
    const SourceFileMetaData& sourceFileMetaData);
ExternalModuleIndicatorOptions getExternalModuleIndicatorOptions(
    std::string_view fileName, const CompilerOptions* options,
    const SourceFileMetaData& metadata);
ExternalModuleIndicatorOptions getExternalModuleIndicatorOptions(
    const SourceFileMetaData& metadata);
const Pragma* getPragmaFromSourceFile(const SourceFile* file,
                                      std::string_view name);
std::string getPragmaArgument(const Pragma* pragma, std::string_view name);
std::string getJSXImplicitImportBase(const CompilerOptions* compilerOptions,
                                     SourceFile* file);
std::string getJSXRuntimeImport(std::string_view base,
                                const CompilerOptions* options);

// === slice: printer === — kind/var/literal helpers needed by the emitter
// (ast_generated.go IsKeywordKind/IsPunctuationKind/IsLiteralKind +
// utilities.go).
inline bool isKeywordKind(Kind kind) {
	return kind >= KindFirstKeyword && kind <= KindLastKeyword;
}

inline bool isPunctuationKind(Kind kind) {
	return kind >= KindFirstPunctuation && kind <= KindLastPunctuation;
}

inline bool isLiteralKind(Kind kind) {
	return kind >= KindFirstLiteralToken && kind <= KindLastLiteralToken;
}

inline bool isJSDocKind(Kind kind) {
	return kind >= KindFirstJSDocNode && kind <= KindLastJSDocNode;
}

inline bool isTemplateLiteralKind(Kind kind) {
	return kind >= KindFirstTemplateToken && kind <= KindLastTemplateToken;
}

inline bool isMemberName(Node* node) {
	return node->kind == Kind::Identifier || node->kind == Kind::PrivateIdentifier;
}

inline bool isTypeElement(Node* node) {
	switch (node->kind) {
		case Kind::ConstructSignature:
		case Kind::CallSignature:
		case Kind::PropertySignature:
		case Kind::MethodSignature:
		case Kind::IndexSignature:
		case Kind::GetAccessor:
		case Kind::SetAccessor:
		case Kind::NotEmittedTypeElement:
			return true;
	}
	return false;
}

inline bool isVarAwaitUsing(Node* node) {
	return (getCombinedNodeFlags(node) & NodeFlagsBlockScoped) ==
	       NodeFlagsAwaitUsing;
}

inline bool isVarUsing(Node* node) {
	return (getCombinedNodeFlags(node) & NodeFlagsBlockScoped) ==
	       NodeFlagsUsing;
}

inline bool isVarConst(Node* node) {
	return (getCombinedNodeFlags(node) & NodeFlagsBlockScoped) ==
	       NodeFlagsConst;
}

inline bool isVarConstLike(Node* node) {
	switch (getCombinedNodeFlags(node) & NodeFlagsBlockScoped) {
		case NodeFlagsConst:
		case NodeFlagsUsing:
		case NodeFlagsAwaitUsing:
			return true;
	}
	return false;
}

inline bool isVarLet(Node* node) {
	return (getCombinedNodeFlags(node) & NodeFlagsBlockScoped) == NodeFlagsLet;
}

inline bool isInJsonFile(Node* node) {
	return (node->flags & NodeFlagsJsonFile) != 0;
}

inline bool isJsxChild(Node* node) {
	switch (node->kind) {
		case Kind::JsxElement:
		case Kind::JsxExpression:
		case Kind::JsxSelfClosingElement:
		case Kind::JsxText:
		case Kind::JsxFragment:
			return true;
	}
	return false;
}

inline bool isUnterminatedLiteral(Node* node) {
	return (isLiteralKind(node->kind) &&
	        (*node->literalLikeData().tokenFlags & TokenFlagsUnterminated) != 0) ||
	       (isTemplateLiteralKind(node->kind) &&
	        (*node->templateLiteralLikeData().templateFlags &
	         TokenFlagsUnterminated) != 0);
}

// SourceFile::HasIdentifier (ast.go:2674) — whether `name` appears as an
// identifier/literal text anywhere in the file. Collects on first call.
bool sourceFileHasIdentifier(SourceFile* file, const std::string& name);

}  // namespace tsc
