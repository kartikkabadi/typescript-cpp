// Port of tsc/internal/printer/emitcontext.go + emitflags.go +
// generatedidentifierflags.go — EmitFlags, EmitContext, NodeFactory (the
// emit-aware factory), NameGenerator, and the side-table structs the
// nodebuilder/checker code uses.
#pragma once

#include "internal/ast/ast.h"
#include "internal/core/arena.h"
#include "internal/core/linkstore.h"

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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

// --- GeneratedIdentifierFlags (generatedidentifierflags.go) -------------------

using GeneratedIdentifierFlags = uint32_t;

inline constexpr GeneratedIdentifierFlags GeneratedIdentifierFlagsNone = 0;
inline constexpr GeneratedIdentifierFlags GeneratedIdentifierFlagsAuto = 1;
inline constexpr GeneratedIdentifierFlags GeneratedIdentifierFlagsLoop = 2;
inline constexpr GeneratedIdentifierFlags GeneratedIdentifierFlagsUnique = 3;
inline constexpr GeneratedIdentifierFlags GeneratedIdentifierFlagsNode = 4;
inline constexpr GeneratedIdentifierFlags GeneratedIdentifierFlagsKindMask = 7;

inline constexpr GeneratedIdentifierFlags
	GeneratedIdentifierFlagsReservedInNestedScopes = 1 << 3;
inline constexpr GeneratedIdentifierFlags GeneratedIdentifierFlagsOptimistic =
	1 << 4;
inline constexpr GeneratedIdentifierFlags GeneratedIdentifierFlagsFileLevel =
	1 << 5;
inline constexpr GeneratedIdentifierFlags
	GeneratedIdentifierFlagsAllowNameSubstitution = 1 << 6;

inline GeneratedIdentifierFlags
generatedIdentifierFlagsKind(GeneratedIdentifierFlags f) {
	return f & GeneratedIdentifierFlagsKindMask;
}
inline bool generatedIdentifierFlagsIsAuto(GeneratedIdentifierFlags f) {
	return generatedIdentifierFlagsKind(f) == GeneratedIdentifierFlagsAuto;
}
inline bool generatedIdentifierFlagsIsLoop(GeneratedIdentifierFlags f) {
	return generatedIdentifierFlagsKind(f) == GeneratedIdentifierFlagsLoop;
}
inline bool generatedIdentifierFlagsIsUnique(GeneratedIdentifierFlags f) {
	return generatedIdentifierFlagsKind(f) == GeneratedIdentifierFlagsUnique;
}
inline bool generatedIdentifierFlagsIsNode(GeneratedIdentifierFlags f) {
	return generatedIdentifierFlagsKind(f) == GeneratedIdentifierFlagsNode;
}
inline bool
generatedIdentifierFlagsIsReservedInNestedScopes(GeneratedIdentifierFlags f) {
	return (f & GeneratedIdentifierFlagsReservedInNestedScopes) != 0;
}
inline bool generatedIdentifierFlagsIsOptimistic(GeneratedIdentifierFlags f) {
	return (f & GeneratedIdentifierFlagsOptimistic) != 0;
}
inline bool generatedIdentifierFlagsIsFileLevel(GeneratedIdentifierFlags f) {
	return (f & GeneratedIdentifierFlagsFileLevel) != 0;
}
inline bool generatedIdentifierFlagsHasAllowNameSubstitution(
	GeneratedIdentifierFlags f) {
	return (f & GeneratedIdentifierFlagsAllowNameSubstitution) != 0;
}

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

struct AutoGenerateInfo {
	GeneratedIdentifierFlags Flags = 0; // whether to auto-generate the text
	AutoGenerateId Id = 0;              // clones share the same generated name
	std::string Prefix;                 // optional name prefix
	std::string Suffix;                 // optional name suffix
	Node* Node = nullptr;               // node used to generate an identifier
};

// AutoGenerateOptions (emitcontext.go:421).
struct AutoGenerateOptions {
	GeneratedIdentifierFlags Flags = 0;
	std::string Prefix;
	std::string Suffix;
};

// NameOptions (factory.go:485).
struct NameOptions {
	bool AllowComments = false;   // whether comments may be emitted for the name
	bool AllowSourceMaps = false; // whether source maps may be emitted
};

// AssignedNameOptions (factory.go:490).
struct AssignedNameOptions {
	bool AllowComments = false;
	bool AllowSourceMaps = false;
	bool IgnoreAssignedName = false;
};

// PrivateIdentifierKind (factory.go:677). Go carries these as string
// constants; C++ keeps an enum and converts at the call site.
enum class PrivateIdentifierKind : int {
	Field,          // "f"
	Method,         // "m"
	Accessor,       // "a"
	Untransformed,  // "untransformed"
};

inline std::string privateIdentifierKindString(PrivateIdentifierKind k) {
	switch (k) {
	case PrivateIdentifierKind::Field:
		return "f";
	case PrivateIdentifierKind::Method:
		return "m";
	case PrivateIdentifierKind::Accessor:
		return "a";
	case PrivateIdentifierKind::Untransformed:
		return "untransformed";
	}
	return {};
}

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
	void clear() {
		elements.clear();
		seen.clear();
	}
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

// Stack — minimal stand-in for core.Stack[T].
template <class T>
struct Stack {
	std::deque<T> items;

	void push(T v) { items.push_back(std::move(v)); }
	T pop() {
		T v = std::move(items.back());
		items.pop_back();
		return v;
	}
	T* peek() { return items.empty() ? nullptr : &items.back(); }
	size_t len() const { return items.size(); }
	bool empty() const { return items.empty(); }
	void clear() { items.clear(); }
};

// slices helpers used by emitcontext.go (core.Splice/Concatenate/Every/
// AppendIfUnique/IfElse live in the core package).
template <class T>
std::vector<T> spliceSlice(const std::vector<T>& slice, int start, int deleteCount,
                           const std::vector<T>& insert) {
	std::vector<T> result;
	result.reserve(slice.size() - deleteCount + insert.size());
	result.insert(result.end(), slice.begin(), slice.begin() + start);
	result.insert(result.end(), insert.begin(), insert.end());
	result.insert(result.end(), slice.begin() + start + deleteCount,
	              slice.end());
	return result;
}

template <class T>
std::vector<T> concatenateSlices(const std::vector<T>& a,
                                 const std::vector<T>& b) {
	std::vector<T> result;
	result.reserve(a.size() + b.size());
	result.insert(result.end(), a.begin(), a.end());
	result.insert(result.end(), b.begin(), b.end());
	return result;
}

template <class T>
void appendIfUnique(std::vector<T>& v, const T& value) {
	if (std::find(v.begin(), v.end(), value) == v.end()) {
		v.push_back(value);
	}
}

struct EmitContext;

// NodeFactory (factory.go:13) — the emit-aware node factory. Embeds
// tsc::NodeFactory (as public base) so all the ast New*/Update* methods are
// promoted, and adds the generated-identifier / emit-helper methods that read
// and write EmitContext side tables.
struct NodeFactory : tsc::NodeFactory {
	EmitContext* emitContext = nullptr;

	NodeFactory() = default;
	explicit NodeFactory(EmitContext* ctx);

	// Convenience upcast (Go: f.AsNodeFactory()).
	tsc::NodeFactory* asNodeFactory() { return this; }

	// generated-identifier family (factory.go)
	Node* newGeneratedIdentifier(GeneratedIdentifierFlags kind,
	                             std::string text, Node* node,
	                             const AutoGenerateOptions& options);
	Node* newTempVariable(const AutoGenerateOptions& options = {});
	Node* newLoopVariable(const AutoGenerateOptions& options = {});
	Node* newUniqueName(std::string text,
	                    const AutoGenerateOptions& options = {});
	Node* newGeneratedNameForNode(Node* node,
	                            const AutoGenerateOptions& options = {});
	Node* newGeneratedPrivateIdentifier(GeneratedIdentifierFlags kind,
	                                    std::string text, Node* node,
	                                    const AutoGenerateOptions& options);
	Node* newUniquePrivateName(std::string text,
	                           const AutoGenerateOptions& options = {});
	Node* newGeneratedPrivateNameForNode(Node* node,
	                                     const AutoGenerateOptions& options = {});
	Node* newStringLiteralFromNode(Node* textSourceNode);

	// common tokens/operators (factory.go)
	Node* newThisExpression();
	Node* newTrueExpression();
	Node* newFalseExpression();
	Node* newCommaExpression(Node* left, Node* right);
	Node* newAssignmentExpression(Node* left, Node* right);
	Node* newLogicalORExpression(Node* left, Node* right);
	Node* newLogicalANDExpression(Node* left, Node* right);
	Node* newStrictEqualityExpression(Node* left, Node* right);
	Node* newStrictInequalityExpression(Node* left, Node* right);

	// compound nodes / utilities (factory.go)
	Node* newVoidZeroExpression();
	Node* inlineExpressions(const std::vector<Node*>& expressions);
	Node* createExpressionFromEntityName(Node* node);
	Node* restoreEnclosingLabel(Node* node, Node* outermostLabeledStatement);
	Node* createForOfBindingStatement(Node* node, Node* boundValue);
	Node* newTypeCheck(Node* value, const std::string& tag);
	Node* newMethodCall(Node* object, Node* methodName,
	                    std::vector<Node*> argumentsList);
	Node* newGlobalMethodCall(const std::string& globalObjectName,
	                          const std::string& methodName,
	                          std::vector<Node*> argumentsList);
	Node* newFunctionCallCall(Node* target, Node* thisArg,
	                          std::vector<Node*> argumentsList);
	Node* newArraySliceCall(Node* array, int start);
	bool isIgnorableParen(Node* node);
	Node* updateOuterExpression(Node* outerExpression, Node* expression);
	Node* restoreOuterExpressions(Node* outerExpression, Node* innerExpression,
	                              OuterExpressionKinds kinds);
	std::vector<Node*> ensureUseStrict(std::vector<Node*> statements);
	std::pair<std::vector<Node*>, std::vector<Node*>>
	splitStandardPrologue(const std::vector<Node*>& source);
	std::pair<std::vector<Node*>, std::vector<Node*>>
	splitCustomPrologue(const std::vector<Node*>& source);

	// declaration names (factory.go)
	Node* getName(Node* node, EmitFlags emitFlags,
	              const AssignedNameOptions& opts);
	Node* getLocalName(Node* node, const AssignedNameOptions& opts = {});
	Node* getExportName(Node* node, const AssignedNameOptions& opts = {});
	Node* getDeclarationName(Node* node, const NameOptions& opts = {});
	Node* getNamespaceMemberName(Node* ns, Node* name, const NameOptions& opts);
	Node* getExternalModuleOrNamespaceExportName(Node* ns, Node* node,
	                                           bool allowComments,
	                                           bool allowSourceMaps);

	// emit helpers (factory.go)
	Node* newUnscopedHelperName(const std::string& name);
	Node* newDecorateHelper(std::vector<Node*> decoratorExpressions, Node* target,
	                        Node* memberName, Node* descriptor);
	Node* newMetadataHelper(const std::string& metadataKey, Node* metadataValue);
	Node* newParamHelper(Node* expression, int parameterOffset,
	                     TextRange location);
	Node* newAddDisposableResourceHelper(Node* envBinding, Node* value,
	                                     bool async_);
	Node* newDisposeResourcesHelper(Node* envBinding);
	Node* newClassPrivateFieldGetHelper(Node* receiver, Node* state,
	                                  PrivateIdentifierKind kind, Node* fn);
	Node* newClassPrivateFieldSetHelper(Node* receiver, Node* state, Node* value,
	                                  PrivateIdentifierKind kind, Node* fn);
	Node* newClassPrivateFieldInHelper(Node* state, Node* receiver);
	Node* newObjectDefinePropertyCall(Node* target, Node* name,
	                                  Node* descriptor);
	Node* newReflectGetCall(Node* target, Node* propertyKey, Node* receiver);
	Node* newReflectSetCall(Node* target, Node* propertyKey, Node* value,
	                        Node* receiver);
	Node* newFunctionBindCall(Node* target, Node* thisArg,
	                          std::vector<Node*> argumentsList);
	Node* newImmediatelyInvokedArrowFunction(std::vector<Node*> statements);
	Node* newExportDefault(Node* expression);
	Node* newExternalModuleExport(Node* name);
	Node* newAssignHelper(std::vector<Node*> attributesSegments,
	                      ScriptTarget scriptTarget);
	Node* newRestHelper(Node* value, const std::vector<Node*>& elements,
	                    std::vector<Node*> computedTempVariables,
	                    TextRange location);
	Node* newAwaitHelper(Node* expression);
	Node* newAsyncGeneratorHelper(Node* generatorFunc, bool hasLexicalThis);
	Node* newAsyncDelegatorHelper(Node* expression);
	Node* newAsyncValuesHelper(Node* expression);
	Node* newAwaiterHelper(bool hasLexicalThis, Node* argumentsExpression,
	                       NodeList* parameters, Node* body);
	Node* newESDecorateClassContextObject(Node* nameExpr, Node* metadata);
	Node* newESDecorateClassElementAccessGetMethod(bool nameComputed,
	                                             Node* nameExpr);
	Node* newESDecorateClassElementAccessSetMethod(bool nameComputed,
	                                             Node* nameExpr);
	Node* newESDecorateClassElementAccessHasMethod(bool nameComputed,
	                                             Node* nameExpr);
	Node* newESDecorateClassElementAccessObject(bool nameComputed, Node* nameExpr,
	                                            bool hasGet, bool hasSet);
	Node* newESDecorateClassElementContextObject(
		const std::string& kind, bool nameComputed, Node* nameExpr,
		bool isStatic, bool isPrivate, bool hasGet, bool hasSet,
		Node* metadata);
	Node* newESDecorateHelper(Node* ctor, Node* descriptorIn, Node* decorators,
	                          Node* contextIn, Node* initializers,
	                          Node* extraInitializers);
	Node* newRunInitializersHelper(Node* thisArg, Node* initializers,
	                               Node* value);
	Node* newTemplateObjectHelper(Node* cookedArray, Node* rawArray);
	Node* newPropKeyHelper(Node* expr);
	Node* newSetFunctionNameHelper(Node* fn, Node* name,
	                               const std::string& prefix);
	Node* newImportDefaultHelper(Node* expression);
	Node* newImportStarHelper(Node* expression);
	Node* newExportStarHelper(Node* moduleExpression, Node* exportsExpression);
	Node* newAssignmentTargetWrapper(Node* paramName, Node* expression);
	Node* newRewriteRelativeImportExtensionsHelper(Node* firstArgument,
	                                             bool preserveJsx);
};

// EmitContext — side-table information used during transformation that can be
// read by the printer to customize emit (emitcontext.go).
struct EmitContext {
	NodeFactory factory;
	Arena emitNodesArena;
	LinkStore<Node*, emitNode> emitNodes{&emitNodesArena};
	std::unordered_map<Node*, AutoGenerateInfo> autoGenerate; // key: MemberName
	std::unordered_map<Node*, Node*> textSource;  // key: StringLiteralNode
	std::unordered_map<Node*, Node*> original_;
	std::unordered_map<Node*, Node*> assignedName; // value: Expression
	std::unordered_map<Node*, Node*> classThis;    // value: IdentifierNode
	Stack<varScope> varScopeStack;
	Stack<varScope> letScopeStack;
	OrderedSet<EmitHelper*> emitHelpers;
	// NodeVisitors created through newNodeVisitor are owned here (Go GC analog).
	std::vector<std::unique_ptr<NodeVisitor>> visitors;

	EmitContext() : factory(this) {}
	EmitContext(const EmitContext&) = delete;
	EmitContext& operator=(const EmitContext&) = delete;

	void reset();

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

	// Go's private `emitContext` field on Printer is lowercase; C++ code uses
	// it directly.

	NodeVisitor* newNodeVisitor(std::function<Node*(Node*)> visit);

	// --- environment tracking (emitcontext.go) ---
	void startVariableEnvironment();
	std::vector<Node*> endVariableEnvironment();
	NodeList* endAndMergeVariableEnvironmentList(NodeList* statements);
	std::vector<Node*> endAndMergeVariableEnvironment(
		const std::vector<Node*>& statements);
	void addVariableDeclaration(Node* name);
	void addHoistedFunctionDeclaration(Node* node);
	void startLexicalEnvironment();
	std::vector<Node*> endLexicalEnvironment();
	NodeList* endAndMergeLexicalEnvironmentList(NodeList* statements);
	std::vector<Node*> endAndMergeLexicalEnvironment(
		const std::vector<Node*>& statements);
	void addLexicalDeclaration(Node* name);
	NodeList* mergeEnvironmentList(NodeList* statements,
	                             const std::vector<Node*>& declarations);
	std::vector<Node*> mergeEnvironment(
		const std::vector<Node*>& statements,
		const std::vector<Node*>& declarations);
	std::pair<std::vector<Node*>, bool> mergeEnvironmentImpl(
		const std::vector<Node*>& statements,
		const std::vector<Node*>& declarations);
	bool isCustomPrologue(Node* node);
	bool isHoistedFunction(Node* node);
	bool isHoistedVariableStatement(Node* node);

	// --- name generation (emitcontext.go) ---
	bool hasAutoGenerateInfo(Node* node) const;
	AutoGenerateInfo* getAutoGenerateInfo(Node* name);
	const AutoGenerateInfo* getAutoGenerateInfo(Node* name) const;
	Node* getNodeForGeneratedName(Node* name);
	Node* getNodeForGeneratedNameWorker(Node* node, AutoGenerateId autoGenerateId);

	// --- original node tracking (emitcontext.go) ---
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

	Node* parseNode(Node* node) const {
		node = mostOriginal(node);
		if (node != nullptr && isParseTreeNode(node)) {
			return node;
		}
		return nullptr;
	}

	bool isFileLevelUniqueName(
		SourceFile* sourceFile, const std::string& name,
		const std::function<bool(const std::string&)>& hasGlobalName);

	// --- emit-related data (emitcontext.go) ---
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

	SnippetElement* snippetElement(Node* node) {
		if (emitNode* en = emitNodes.TryGet(node)) {
			return en->snippetElement;
		}
		return nullptr;
	}

	void setSnippetElement(Node* node, const SnippetElement& el) {
		emitNodes.Get(node)->snippetElement = new SnippetElement(el);
	}

	TextRange commentRange(Node* node) {
		if (emitNode* en = emitNodes.TryGet(node);
		    en != nullptr && (en->flags & hasCommentRange) != 0) {
			return en->commentRange;
		}
		return node->loc;
	}

	void setCommentRange(Node* node, TextRange loc) {
		emitNode* en = emitNodes.Get(node);
		en->commentRange = loc;
		en->flags |= hasCommentRange;
	}

	void assignCommentRange(Node* to, Node* from) {
		setCommentRange(to, commentRange(from));
	}

	TextRange sourceMapRange(Node* node) {
		if (emitNode* en = emitNodes.TryGet(node);
		    en != nullptr && (en->flags & hasSourceMapRange) != 0) {
			return en->sourceMapRange;
		}
		return node->loc;
	}

	void setSourceMapRange(Node* node, TextRange loc) {
		emitNode* en = emitNodes.Get(node);
		en->sourceMapRange = loc;
		en->flags |= hasSourceMapRange;
	}

	void assignSourceMapRange(Node* to, Node* from) {
		setSourceMapRange(to, sourceMapRange(from));
	}

	void assignCommentAndSourceMapRanges(Node* to, Node* from) {
		emitNode* en = emitNodes.Get(to);
		TextRange cr = commentRange(from);
		TextRange smr = sourceMapRange(from);
		en->commentRange = cr;
		en->sourceMapRange = smr;
		en->flags |= hasCommentRange | hasSourceMapRange;
	}

	std::optional<TextRange> tokenSourceMapRange(Node* node, Kind kind) {
		if (emitNode* en = emitNodes.TryGet(node);
		    en != nullptr && !en->tokenSourceMapRanges.empty()) {
			if (auto it = en->tokenSourceMapRanges.find(kind);
			    it != en->tokenSourceMapRanges.end()) {
				return it->second;
			}
		}
		return std::nullopt;
	}

	void setTokenSourceMapRange(Node* node, Kind kind, TextRange loc) {
		emitNodes.Get(node)->tokenSourceMapRanges[kind] = loc;
	}

	Node* assignedNameOf(Node* node) {
		auto it = assignedName.find(node);
		return it != assignedName.end() ? it->second : nullptr;
	}

	Node* textSourceOf(Node* node) {
		auto it = textSource.find(node);
		return it != textSource.end() ? it->second : nullptr;
	}

	void setAssignedName(Node* node, Node* name) { assignedName[node] = name; }

	Node* classThisOf(Node* node) {
		auto it = classThis.find(node);
		return it != classThis.end() ? it->second : nullptr;
	}

	void setClassThis(Node* node, Node* classThisNode) {
		classThis[node] = classThisNode;
	}

	void requestEmitHelper(EmitHelper* helper);
	std::vector<EmitHelper*> readEmitHelpers();
	void addEmitHelper(Node* node, EmitHelper* helper);
	void moveEmitHelpers(Node* source, Node* target,
	                     const std::function<bool(EmitHelper*)>& predicate);
	std::vector<EmitHelper*> getEmitHelpers(Node* node);
	Node* getExternalHelpersModuleName(SourceFile* node);
	void setExternalHelpersModuleName(SourceFile* node, Node* name);
	bool hasRecordedExternalHelpers(SourceFile* node);
	bool isCallToHelper(Node* firstSegment, const std::string& helperName);

	// --- visitor hooks (emitcontext.go) ---
	NodeList* visitVariableEnvironment(NodeList* nodes, NodeVisitor* visitor);
	NodeList* visitParameters(NodeList* nodes, NodeVisitor* visitor);
	NodeList* addDefaultValueAssignmentsIfNeeded(NodeList* nodeList);
	Node* addDefaultValueAssignmentIfNeeded(ParameterDeclaration* parameter);
	Node* addDefaultValueAssignmentForBindingPattern(
		ParameterDeclaration* parameter);
	Node* addDefaultValueAssignmentForInitializer(
		ParameterDeclaration* parameter, Node* name, Node* initializer);
	void addInitializationStatement(Node* node);
	Node* convertToFunctionBlock(Node* node, bool multiLine);
	Node* visitFunctionBody(Node* node, NodeVisitor* visitor);
	Node* visitIterationBody(Node* body, NodeVisitor* visitor);
	Node* visitEmbeddedStatement(Node* node, NodeVisitor* visitor);

	// --- synthesized comments (emitcontext.go) ---
	Node* setSyntheticLeadingComments(
		Node* node, std::vector<SynthesizedComment> comments);
	Node* addSyntheticLeadingComment(Node* node, Kind kind, std::string text,
	                               bool hasTrailingNewLine);
	std::vector<SynthesizedComment> getSyntheticLeadingComments(Node* node);
	Node* setSyntheticTrailingComments(
		Node* node, std::vector<SynthesizedComment> comments);
	Node* addSyntheticTrailingComment(Node* node, Kind kind, std::string text,
	                                bool hasTrailingNewLine);
	std::vector<SynthesizedComment> getSyntheticTrailingComments(Node* node);

	void setTypeNode(Node* node, Node* typeNode);
	Node* getTypeNode(Node* node);
	Node* newNotEmittedStatement(Node* node);

	// nodebuilder.go — e.Factory.ReleaseArenas()
	// Go's Factory.ReleaseArenas frees only arena memory the GC can prove
	// unreachable — nodes still referenced from caches (e.g. NodeBuilderLinks
	// serializedTypes) stay alive. Our bump arena cannot distinguish, so the
	// faithful equivalent frees nothing: arena blocks stay live for the
	// context's lifetime, which also keeps node addresses unique so no
	// node-keyed map (original_, emitNodes, autoGenerate) can see a stale-key
	// collision. It must NOT reset() the context maps: Go's emitNodes map
	// persists across calls, and cached serializedTypes TypeNodes rely on
	// their emit flags (e.g. EFSingleLine) surviving between typeToString
	// calls.
	void releaseArenas() {}
};

// NewEmitContext (emitcontext.go:45).
inline EmitContext* NewEmitContext() { return new EmitContext(); }

// nextAutoGenerateId (emitcontext.go:427) — atomic counter; returns the new
// value like atomic.AddUint32.
uint32_t nextAutoGenerateId();

// GetEmitContext (emitcontext.go:57) — pooled; the returned function resets and
// releases the context.
std::pair<EmitContext*, std::function<void()>> GetEmitContext();

// --- NameGenerator (namegenerator.go) -----------------------------------------

// tempFlags — tracks count of temp variables and a few dedicated names
// (namegenerator.go:13).
using tempFlags = uint32_t;
inline constexpr tempFlags tempFlagsAuto = 0x00000000;   // no preferred name
inline constexpr tempFlags tempFlagsCountMask = 0x0FFFFFFF; // counter
inline constexpr tempFlags tempFlags_i = 0x10000000;     // '_i' preference flag

// nameGenerationScope — linked list; `next` is the enclosing scope
// (namegenerator.go:33).
struct nameGenerationScope {
	nameGenerationScope* next = nullptr;
	tempFlags tempFlags_ = tempFlagsAuto;
	std::unordered_map<std::string, tempFlags> formattedNameTempFlags;
	std::unordered_set<std::string> reservedNames;
};

struct NameGenerator {
	EmitContext* Context = nullptr;
	// callback for Printer.isFileLevelUniqueNameInCurrentFile
	std::function<bool(const std::string&, bool)>
		IsFileLevelUniqueNameInCurrentFile;
	// callback for Printer.getTextOfNode
	std::function<std::string(Node*)> GetTextOfNode;

	std::unordered_map<NodeId, std::string> nodeIdToGeneratedName;
	std::unordered_map<NodeId, std::string> nodeIdToGeneratedPrivateName;
	std::unordered_map<AutoGenerateId, std::string>
		autoGeneratedIdToGeneratedName;
	nameGenerationScope* nameScope = nullptr;
	nameGenerationScope* privateNameScope = nullptr;
	std::unordered_set<std::string> generatedNames;
	// owned scope nodes (scopes form linked lists; the generator owns them)
	std::deque<nameGenerationScope> scopePool;

	void PushScope(bool reuseTempVariableScope);
	void PopScope(bool reuseTempVariableScope);
	nameGenerationScope** getScope(bool privateName);
	tempFlags getTempFlags(bool privateName);
	void setTempFlags(bool privateName, tempFlags flags);
	tempFlags getTempFlagsForFormattedName(bool privateName,
	                                       const std::string& formattedNameKey);
	void setTempFlagsForFormattedName(bool privateName,
	                                  const std::string& formattedNameKey,
	                                  tempFlags flags);
	void reserveName(const std::string& name, bool privateName, bool scoped,
	                 bool temp);
	std::string GenerateName(Node* name); // *ast.MemberName
	std::string generateNameForNodeCached(Node* node, bool privateName,
	                                      GeneratedIdentifierFlags flags,
	                                      const std::string& prefix,
	                                      const std::string& suffix);
	std::string generateNameForNode(Node* node, bool privateName,
	                                GeneratedIdentifierFlags flags,
	                                const std::string& prefix,
	                                const std::string& suffix);
	std::string generateNameForModuleOrEnum(Node* node);
	std::string generateNameForImportOrExportDeclaration(Node* node);
	std::string generateNameForExportDefault();
	std::string generateNameForClassExpression();
	std::string generateNameForMethodOrAccessor(Node* node, bool privateName,
	                                            const std::string& prefix,
	                                            const std::string& suffix);
	std::string makeName(Node* name);
	std::string makeTempVariableName(tempFlags flags,
	                                 bool reservedInNestedScopes,
	                                 bool privateName, const std::string& prefix,
	                                 const std::string& suffix);
	std::string makeUniqueName(
		const std::string& baseName,
		const std::function<bool(const std::string&, bool)>& checkFn,
		bool optimistic, bool scoped, bool privateName,
		const std::string& prefix, const std::string& suffix);
	std::string MakeFileLevelOptimisticUniqueName(const std::string& name);
	bool checkUniqueName(
		const std::string& name, bool privateName,
		const std::function<bool(const std::string&, bool)>& checkFn);
	bool isUniqueName(const std::string& name, bool privateName);
	bool isReservedName(const std::string& name, bool privateName);
};

// namegenerator.go:355
Node* nextContainer(Node* node);
// namegenerator.go:363
bool isUniqueLocalName(const std::string& name, Node* container);

} // namespace tsc::printer
