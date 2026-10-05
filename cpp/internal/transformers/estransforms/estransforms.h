// Port of tsc/internal/transformers/estransforms — shared helpers used across
// the ES transforms (utilities.go) plus small package-level helpers lifted
// from async.go that utilities.go references.
#pragma once

#include "internal/transformers/transformers.h"

namespace tsc::transformers::estransforms {

// convertClassDeclarationToClassExpression — utilities.go:10
Node* convertClassDeclarationToClassExpression(printer::EmitContext* emitContext,
                                               Node* node);

// createNotNullCondition — utilities.go:23
Node* createNotNullCondition(printer::EmitContext* emitContext, Node* left,
                             Node* right, bool invert);

// superAccessState — utilities.go:55. Tracks super property/element accesses
// and super property assignments within async function or async generator
// bodies. Shared by asyncTransformer and forawaitTransformer.
struct superAccessState {
	printer::NodeFactory* factory = nullptr;

	// Keeps track of property names accessed on super (`super.x`) within
	// async functions. nullptr = tracking disabled (Go *OrderedSet nil).
	printer::OrderedSet<std::string>* capturedSuperProperties = nullptr;
	// Whether the async function contains an element access on super
	// (`super[x]`).
	bool hasSuperElementAccess = false;
	bool hasSuperPropertyAssignment = false;

	Node* superBinding = nullptr;
	Node* superIndexBinding = nullptr;
	NodeVisitor* superAccessVisitor = nullptr;

	// initSuperAccessVisitor — utilities.go:69
	void initSuperAccessVisitor(printer::EmitContext* emitContext,
	                            printer::NodeFactory* factory);

	// visitSuperAccessNode — utilities.go:77. Walks the async/generator body
	// and replaces super property/element accesses with _super/_superIndex
	// references.
	Node* visitSuperAccessNode(Node* node);

	// substituteSuperAccessesInBody — utilities.go:111
	Node* substituteSuperAccessesInBody(Node* body);

	// substituteCallExpressionWithSuperAccess — utilities.go:116
	Node* substituteCallExpressionWithSuperAccess(Node* call,
	                                              NodeVisitor* visitor);

	// createSuperElementAccessInAsyncMethod — utilities.go:158
	Node* createSuperElementAccessInAsyncMethod(Node* argumentExpression);

	// createSuperAccessVariableStatement — utilities.go:182
	Node* createSuperAccessVariableStatement();

	// trackSuperAccess — utilities.go:251
	void trackSuperAccess(Node* node);
};

// assignmentTargetContainsSuperProperty — async.go:899
bool assignmentTargetContainsSuperProperty(Node* node);

// isUpdateExpression — async.go:931. Checks if a prefix/postfix unary
// expression is ++ or --.
bool isUpdateExpression(Node* node);

// createAccessorPropertyBackingField — utilities.go:280
Node* createAccessorPropertyBackingField(printer::NodeFactory* f, Node* node,
                                         ModifierList* modifiers,
                                         Node* initializer);

// === slice: esdecorator cluster ===

// classthis.go
// Gets whether a node is a `static {}` block containing only a single
// assignment of the static `this` to the `_classThis` (or similar) variable
// stored in the `classthis` property of the block's `EmitNode`.
bool isClassThisAssignmentBlock(printer::EmitContext* emitContext, Node* node);

// namedevaluation.go
bool isClassNamedEvaluationHelperBlock(printer::EmitContext* emitContext,
                                       Node* node);
bool classHasExplicitlyAssignedName(printer::EmitContext* emitContext,
                                    Node* node);
bool classHasDeclaredOrExplicitlyAssignedName(printer::EmitContext* emitContext,
                                              Node* node);
bool isNamedEvaluation(printer::EmitContext* emitContext, Node* node);
bool isNamedEvaluationAnd(printer::EmitContext* emitContext, Node* node,
                          const std::function<bool(Node*)>& cb);
Node* injectClassNamedEvaluationHelperBlockIfMissing(
    printer::EmitContext* emitContext, Node* node, Node* assignedName,
    Node* thisExpression);
Node* transformNamedEvaluation(printer::EmitContext* emitContext, Node* node,
                               bool ignoreEmptyStringLiteral,
                               const std::string& assignedName);

// esdecorator.go
Transformer* newESDecoratorTransformer(TransformOptions* opts);

// === dep stubs — removed when owner slice lands ===
// classfields.go — owned by the classfields slice
bool classHasClassThisAssignment(printer::EmitContext* emitContext, Node* node);
BinaryExpression* findComputedPropertyNameCacheAssignment(
    printer::EmitContext* emitContext, Node* name);
Node* expandPreOrPostfixIncrementOrDecrementExpression(
    printer::NodeFactory* f, printer::EmitContext* emitContext, Node* node,
    Node* expression, Node* resultVariable);

}  // namespace tsc::transformers::estransforms
