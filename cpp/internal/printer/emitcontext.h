// Port of tsc/internal/printer/emitcontext.go + emitflags.go — EmitFlags,
// EmitContext, and the side-table structs the nodebuilder/checker code uses.
// Only the surface the ported code touches is implemented; the rest of the
// EmitContext API arrives with the printer/emitter slices.
#pragma once

#include "internal/ast/ast.h"
#include "internal/core/arena.h"
#include "internal/core/linkstore.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tsc::printer {

// --- EmitFlags (emitflags.go) ------------------------------------------------

using EmitFlags = uint32_t;

inline constexpr EmitFlags EFSingleLine = 1 << 0;
inline constexpr EmitFlags EFMultiLine = 1 << 1;
inline constexpr EmitFlags EFNoLeadingSourceMap = 1 << 2;
inline constexpr EmitFlags EFNoTrailingSourceMap = 1 << 3;
inline constexpr EmitFlags EFNoNestedSourceMaps = 1 << 4;
inline constexpr EmitFlags EFNoTokenLeadingSourceMaps = 1 << 5;
inline constexpr EmitFlags EFNoTokenTrailingSourceMaps = 1 << 6;
inline constexpr EmitFlags EFNoLeadingComments = 1 << 7;
inline constexpr EmitFlags EFNoTrailingComments = 1 << 8;
inline constexpr EmitFlags EFNoNestedComments = 1 << 9;
inline constexpr EmitFlags EFHelperName = 1 << 10;
inline constexpr EmitFlags EFExportName = 1 << 11;
inline constexpr EmitFlags EFLocalName = 1 << 12;
inline constexpr EmitFlags EFIndented = 1 << 13;
inline constexpr EmitFlags EFNoIndentation = 1 << 14;
inline constexpr EmitFlags EFReuseTempVariableScope = 1 << 15;
inline constexpr EmitFlags EFCustomPrologue = 1 << 16;
inline constexpr EmitFlags EFNoAsciiEscaping = 1 << 17;
inline constexpr EmitFlags EFExternalHelpers = 1 << 18;
inline constexpr EmitFlags EFStartOnNewLine = 1 << 19;
inline constexpr EmitFlags EFIndirectCall = 1 << 20;
inline constexpr EmitFlags EFAsyncFunctionBody = 1 << 21;
inline constexpr EmitFlags EFNoLexicalArguments = 1 << 22;
inline constexpr EmitFlags EFTransformPrivateStaticElements = 1 << 23;
inline constexpr EmitFlags EFNoLexicalThis = 1 << 24;

inline constexpr EmitFlags EFNone = 0;
inline constexpr EmitFlags EFNoSourceMap =
	EFNoLeadingSourceMap | EFNoTrailingSourceMap;
inline constexpr EmitFlags EFNoTokenSourceMaps =
	EFNoTokenLeadingSourceMaps | EFNoTokenTrailingSourceMaps;
inline constexpr EmitFlags EFNoComments =
	EFNoLeadingComments | EFNoTrailingComments;

// --- SymbolAccessibility (emitresolver.go) -----------------------------------

enum class SymbolAccessibility : int32_t {
	Accessible = 0,
	NotAccessible = 1,
	CannotBeNamed = 2,
	NotResolved = 3,
};

struct SymbolAccessibilityResult {
	SymbolAccessibility Accessibility = SymbolAccessibility::Accessible;
	std::vector<Node*> AliasesToMakeVisible;
	std::string ErrorSymbolName;
	Node* ErrorNode = nullptr;
	std::string ErrorModuleName;
};

// --- TypeReferenceSerializationKind (printer/emitresolver.go:33) -------------

// Indicates how to serialize the name for a TypeReferenceNode when emitting
// decorator metadata.
enum class TypeReferenceSerializationKind : int32_t {
	// The TypeReferenceNode could not be resolved.
	// The type name should be emitted using a safe fallback.
	Unknown = 0,

	// The TypeReferenceNode resolves to a type with a constructor
	// function that can be reached at runtime (e.g. a `class`
	// declaration or a `var` declaration for the static side
	// of a type, such as the global `Promise` type in lib.d.ts).
	TypeWithConstructSignatureAndValue,

	// The TypeReferenceNode resolves to a Void-like, Nullable, or Never type.
	VoidNullableOrNeverType,

	// The TypeReferenceNode resolves to a Number-like type.
	NumberLikeType,

	// The TypeReferenceNode resolves to a BigInt-like type.
	BigIntLikeType,

	// The TypeReferenceNode resolves to a String-like type.
	StringLikeType,

	// The TypeReferenceNode resolves to a Boolean-like type.
	BooleanType,

	// The TypeReferenceNode resolves to an Array-like type.
	ArrayLikeType,

	// The TypeReferenceNode resolves to the ESSymbol type.
	ESSymbolType,

	// The TypeReferenceNode resolved to the global Promise constructor symbol.
	Promise,

	// The TypeReferenceNode resolves to a Function type or a type with call
	// signatures.
	TypeWithCallSignature,

	// The TypeReferenceNode resolves to any other type.
	ObjectType,
};

// --- side-table value types (emitcontext.go / helpers.go) --------------------

using AutoGenerateId = uint32_t;
using GeneratedIdentifierFlags = uint32_t;

struct AutoGenerateInfo {
	GeneratedIdentifierFlags Flags = 0; // whether to auto-generate the text
	AutoGenerateId Id = 0;              // clones share the same generated name
	std::string Prefix;                 // optional name prefix
	std::string Suffix;                 // optional name suffix
	Node* Node = nullptr;               // node used to generate an identifier
};

using Priority = int32_t;

struct EmitHelper {
	std::string Name;
	bool Scoped = false;
	std::string Text;
	std::function<std::string(const std::function<std::string(std::string)>&)>
		TextCallback;
	const Priority* Priority = nullptr;
	std::vector<EmitHelper*> Dependencies;
	std::string ImportName;
};

using emitNodeFlags = uint32_t;
inline constexpr emitNodeFlags hasCommentRange = 1 << 0;
inline constexpr emitNodeFlags hasSourceMapRange = 1 << 1;

enum class SnippetKind : int32_t { TabStop = 0 };

struct SnippetElement {
	SnippetKind Kind = SnippetKind::TabStop;
	int Order = 0;
};

struct SynthesizedComment {
	Kind Kind = Kind::Unknown;
	TextRange Loc;
	bool HasLeadingNewLine = false;
	bool HasTrailingNewLine = false;
	std::string Text;
};

// emitNode — per-node emit side-table entry (emitcontext.go).
struct emitNode {
	emitNodeFlags flags = 0;
	EmitFlags emitFlags = 0;
	TextRange commentRange;
	TextRange sourceMapRange;
	std::unordered_map<tsc::Kind, TextRange> tokenSourceMapRanges;
	std::vector<EmitHelper*> helpers;
	Node* externalHelpersModuleName = nullptr; // *IdentifierNode in Go
	std::vector<SynthesizedComment> leadingComments;
	std::vector<SynthesizedComment> trailingComments;
	Node* typeNode = nullptr; // *TypeNode in Go
	SnippetElement* snippetElement = nullptr;

	// NOTE: mirrors (*emitNode).copyFrom — leadingComments, trailingComments
	// and typeNode are intentionally not copied.
	void copyFrom(const emitNode& source) {
		flags = source.flags;
		emitFlags = source.emitFlags;
		commentRange = source.commentRange;
		sourceMapRange = source.sourceMapRange;
		tokenSourceMapRanges = source.tokenSourceMapRanges;
		helpers = source.helpers;
		externalHelpersModuleName = source.externalHelpersModuleName;
		if (source.snippetElement != nullptr) {
			snippetElement = new SnippetElement(*source.snippetElement);
		}
	}
};

// OrderedSet — minimal stand-in for collections.OrderedSet (insert order +
// dedup by key equality).
template <class T>
struct OrderedSet {
	std::vector<T> elements;
	std::unordered_set<T> seen;

	bool add(const T& v) {
		if (seen.insert(v).second) {
			elements.push_back(v);
			return true;
		}
		return false;
	}
	bool has(const T& v) const { return seen.count(v) != 0; }
	size_t size() const { return elements.size(); }
};

// environmentFlags (emitcontext.go).
using environmentFlags = int32_t;
inline constexpr environmentFlags environmentFlagsNone = 0;
inline constexpr environmentFlags environmentFlagsInParameters = 1 << 0;
inline constexpr environmentFlags environmentFlagsVariablesHoistedInParameters =
	1 << 1;

// varScope — hoisting state tracked while visiting (emitcontext.go).
struct varScope {
	std::vector<Node*> variables;   // *VariableDeclarationNode
	std::vector<Node*> functions;   // *FunctionDeclarationNode
	environmentFlags flags = environmentFlagsNone;
	std::vector<Node*> initializationStatements;
};

// EmitContext — side-table information used during transformation that can be
// read by the printer to customize emit (emitcontext.go).
//
// NOTE: Go stores `Factory *printer.NodeFactory` (a type embedding
// ast.NodeFactory with emit-aware factory methods). The emit-aware methods are
// not ported yet, so we keep the ast.NodeFactory value directly; take
// `&ctx.factory` where a *NodeFactory is needed.
struct EmitContext {
	NodeFactory factory; // hooks bound to this context (see ctor)
	Arena emitNodesArena;
	LinkStore<Node*, emitNode> emitNodes{&emitNodesArena};
	std::unordered_map<Node*, AutoGenerateInfo> autoGenerate; // key: MemberName
	std::unordered_map<Node*, Node*> textSource;  // key: StringLiteralNode
	std::unordered_map<Node*, Node*> original_;
	std::unordered_map<Node*, Node*> assignedName; // value: Expression
	std::unordered_map<Node*, Node*> classThis;    // value: IdentifierNode
	std::vector<varScope> varScopeStack;
	std::vector<varScope> letScopeStack;
	OrderedSet<EmitHelper*> emitHelpers;

	EmitContext() {
		factory.hooks.onCreate = [this](Node* node) { onCreate(node); };
		factory.hooks.onUpdate = [this](Node* updated, Node* original) {
			onUpdate(updated, original);
		};
		factory.hooks.onClone = [this](Node* updated, Node* original) {
			onClone(updated, original);
		};
	}
	EmitContext(const EmitContext&) = delete;
	EmitContext& operator=(const EmitContext&) = delete;

	void onCreate(Node* node) { node->flags |= NodeFlagsSynthesized; }
	void onUpdate(Node* updated, Node* original) {
		setOriginal(updated, original);
	}
	void onClone(Node* updated, Node* original) {
		setOriginal(updated, original);
		if (isIdentifier(updated) || isPrivateIdentifier(updated)) {
			if (auto it = autoGenerate.find(original);
			    it != autoGenerate.end()) {
				autoGenerate[updated] = it->second;
			}
		}
	}

	// Sets the original node for a given node (strada: setOriginalNode).
	void setOriginal(Node* node, Node* original) {
		setOriginalEx(node, original, false);
	}

	void unsetOriginal(Node* node) { original_.erase(node); }

	void setOriginalEx(Node* node, Node* original, bool allowOverwrite) {
		if (original == nullptr) {
			TSC_UNREACHABLE("Original cannot be nil.");
		}
		auto it = original_.find(node);
		if (it == original_.end()) {
			original_[node] = original;
			if (emitNode* en = emitNodes.TryGet(original)) {
				emitNodes.Get(node)->copyFrom(*en);
			}
		} else if (!allowOverwrite && it->second != original) {
			TSC_UNREACHABLE("Original node already set.");
		} else if (allowOverwrite) {
			original_[node] = original;
		}
	}

	// Gets the original node for a given node (strada: node.original).
	Node* original(Node* node) const {
		auto it = original_.find(node);
		return it != original_.end() ? it->second : nullptr;
	}

	// Gets the most original node associated with this node by walking
	// Original pointers (strada: getOriginalNode).
	Node* mostOriginal(Node* node) const {
		if (node != nullptr) {
			Node* original = this->original(node);
			while (original != nullptr) {
				node = original;
				original = this->original(node);
			}
		}
		return node;
	}

	// Gets the original parse tree node for a given node
	// (strada: getParseTreeNode).
	Node* parseNode(Node* node) const {
		node = mostOriginal(node);
		if (node != nullptr && isParseTreeNode(node)) {
			return node;
		}
		return nullptr;
	}

	EmitFlags emitFlags(Node* node) {
		if (emitNode* en = emitNodes.TryGet(node)) {
			return en->emitFlags;
		}
		return EFNone;
	}

	void setEmitFlags(Node* node, EmitFlags flags) {
		emitNodes.Get(node)->emitFlags = flags;
	}

	void addEmitFlags(Node* node, EmitFlags flags) {
		emitNodes.Get(node)->emitFlags |= flags;
	}
};

}  // namespace tsc::printer
