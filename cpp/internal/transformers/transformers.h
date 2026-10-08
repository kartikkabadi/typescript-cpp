// Port of tsc/internal/transformers — the root-layer transformer framework:
// transformer.go (Transformer, chainedTransformer), chain.go (TransformOptions,
// TransformerFactory, Chain), modifiervisitor.go (modifierVisitor,
// ExtractModifiers), utilities.go (shared transform helpers), and
// destructuring.go (the binding/assignment pattern flattener).
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/ast/visitor.h"
#include "internal/binder/referenceresolver.h"
#include "internal/core/types.h"
#include "internal/printer/emitcontext.h"

namespace tsc {

namespace checker {
struct EmitResolver;
}

namespace transformers {

// ---------------------------------------------------------------------------
// transformer.go
// ---------------------------------------------------------------------------

// Transformer — transformer.go:8
struct Transformer {
	virtual ~Transformer() = default;

	// NewTransformer — transformer.go:14. `panic` → TSC_UNREACHABLE.
	Transformer* newTransformer(std::function<Node*(Node*)> visit,
	                            printer::EmitContext* emitContext);

	printer::EmitContext* emitContext() const { return emitContext_; }
	NodeVisitor* visitor() const { return visitor_; }
	printer::NodeFactory* factory() const { return factory_; }

	// TransformSourceFile — transformer.go:39
	SourceFile* transformSourceFile(SourceFile* file) {
		return visitor_->visitSourceFile(file);
	}

private:
	printer::EmitContext* emitContext_ = nullptr;
	printer::NodeFactory* factory_ = nullptr;
	NodeVisitor* visitor_ = nullptr;
};

// ---------------------------------------------------------------------------
// chain.go
// ---------------------------------------------------------------------------

// TransformOptions — chain.go:26
struct TransformOptions {
	printer::EmitContext* Context = nullptr;
	const CompilerOptions* CompilerOptions = nullptr;
	binder::ReferenceResolver* Resolver = nullptr;
	checker::EmitResolver* EmitResolver = nullptr;
	std::function<ModuleKind(SourceFile*)> GetEmitModuleFormatOfFile;
};

// TransformerFactory — chain.go:34. The returned Transformer* is owned by the
// caller (Go GC analog: callers that keep it long-term should wrap it).
using TransformerFactory =
	std::function<Transformer*(TransformOptions* opt)>;

// Chain — chain.go:38. Chains transforms in left-to-right order, running them
// one at a time in order (as opposed to interleaved at each node). The
// resulting combined transform only operates on SourceFile nodes.
TransformerFactory chain(std::vector<TransformerFactory> transforms);

// ---------------------------------------------------------------------------
// modifiervisitor.go
// ---------------------------------------------------------------------------

// ExtractModifiers — modifiervisitor.go:21
ModifierList* extractModifiers(printer::EmitContext* emitContext,
                               ModifierList* modifiers,
                               ModifierFlags allowed);

// ---------------------------------------------------------------------------
// utilities.go
// ---------------------------------------------------------------------------

// utilities.go:12-25
bool isGeneratedIdentifier(printer::EmitContext* emitContext, Node* name);
bool isHelperName(printer::EmitContext* emitContext, Node* name);
bool isLocalName(printer::EmitContext* emitContext, Node* name);
bool isExportName(printer::EmitContext* emitContext, Node* name);

// IsIdentifierReference — utilities.go:28
bool isIdentifierReference(Node* name, Node* parent);

// ConvertBindingPatternToAssignmentPattern — utilities.go:169
Node* convertBindingPatternToAssignmentPattern(
	printer::EmitContext* emitContext, Node* element);

// ConvertVariableDeclarationToAssignmentExpression — utilities.go:213
Node* convertVariableDeclarationToAssignmentExpression(
	printer::EmitContext* emitContext, Node* element);

// SingleOrMany — utilities.go:224
Node* singleOrMany(std::vector<Node*> nodes, printer::NodeFactory* factory);

// IsSimpleCopiableExpression — utilities.go:241
bool isSimpleCopiableExpression(Node* expression);

// IsOriginalNodeSingleLine — utilities.go:248
bool isOriginalNodeSingleLine(printer::EmitContext* emitContext, Node* node);

// IsSimpleInlineableExpression — utilities.go:270
bool isSimpleInlineableExpression(Node* expression);

// FindSuperStatementIndexPath — utilities.go:275. Finds a path of indices to a
// statement containing a `super()` call.
std::vector<int> findSuperStatementIndexPath(std::vector<Node*> statements,
                                             int start);

// GetSuperCallFromStatement — utilities.go:296. Extracts the super() call
// expression from an expression statement, if any.
Node* getSuperCallFromStatement(Node* statement);

// MoveRangePastModifiers — utilities.go:308
TextRange moveRangePastModifiers(Node* node);

// MoveRangePastDecorators — utilities.go:325
TextRange moveRangePastDecorators(Node* node);

// GetNonAssignmentOperatorForCompoundAssignment — utilities.go:341
Kind getNonAssignmentOperatorForCompoundAssignment(Kind kind);

// ---------------------------------------------------------------------------
// destructuring.go
// ---------------------------------------------------------------------------

// FlattenLevel — destructuring.go:12
enum class FlattenLevel : int {
	All = 0,        // Fully decompose all patterns into individual assignments/bindings
	ObjectRest = 1, // Only decompose patterns containing object rest elements
};

// CreateAssignmentCallback — destructuring.go:22. When provided, the target
// will always be an Identifier; the callback can wrap the assignment with
// additional logic (e.g. export expressions in CJS modules or namespace
// member assignments).
using CreateAssignmentCallback =
	std::function<Node*(Node* name, Node* value, TextRange* location)>;

// FlattenDestructuringAssignment — destructuring.go:27. Flattens a
// destructuring assignment expression (VariableDeclaration |
// DestructuringAssignment) into a sequence of individual property/element
// access assignments.
Node* flattenDestructuringAssignment(Transformer* tx, Node* node,
                                     bool needsValue, FlattenLevel level,
                                     CreateAssignmentCallback createAssignmentCallback);

// FlattenDestructuringBinding — destructuring.go:57. Flattens a binding
// pattern in a variable declaration or parameter (VariableDeclaration |
// ParameterDeclaration | BindingElement) into individual variable
// declarations. Returns a single VariableDeclaration, a SyntaxList of
// declarations, or nullptr.
Node* flattenDestructuringBinding(Transformer* tx, Node* node, Node* rval,
                                  FlattenLevel level, bool hoistTempVariables,
                                  bool skipInitializer);

// BindingOrAssignmentElementAssignsToName — destructuring.go:420
bool bindingOrAssignmentElementAssignsToName(Node* element, std::string_view name);

// BindingOrAssignmentElementContainsNonLiteralComputedName —
// destructuring.go:444
bool bindingOrAssignmentElementContainsNonLiteralComputedName(Node* element);

// GetInitializerOfBindingOrAssignmentElement — destructuring.go:459
Node* getInitializerOfBindingOrAssignmentElement(Node* bindingElement);

// isObjectBindingOrAssignmentPattern — destructuring.go:485
bool isObjectBindingOrAssignmentPattern(Node* node);

// isArrayBindingOrAssignmentPattern — destructuring.go:489
bool isArrayBindingOrAssignmentPattern(Node* node);

// isSimpleBindingOrAssignmentElement — destructuring.go:493
bool isSimpleBindingOrAssignmentElement(Node* element);

}  // namespace transformers
}  // namespace tsc
