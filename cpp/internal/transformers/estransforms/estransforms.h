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

// === slice: classfields ===

// newClassFieldsTransformer — classfields.go:140. Wired into GetESTransformer
// by definitions.go (esDecoratorAndClassFields chain).
Transformer* newClassFieldsTransformer(TransformOptions* opts);

// Dep-stub declarations — owned by the esdecorator-cluster slice
// (namedevaluation.go + classthis.go). classfields.cpp provides
// TSC_UNREACHABLE bodies until that slice lands; on merge the real
// implementations replace them.

// transformNamedEvaluation — namedevaluation.go:513
Node* transformNamedEvaluation(printer::EmitContext* context, Node* node,
                               bool ignoreEmptyStringLiteral,
                               std::string assignedName);
// isNamedEvaluationAnd — namedevaluation.go:89. `cb` is the anonymous-function
// predicate (classfields passes its anonymous-class check).
bool isNamedEvaluationAnd(printer::EmitContext* emitContext, Node* node,
                          const std::function<bool(Node*)>& cb);
// isClassThisAssignmentBlock — classthis.go:10
bool isClassThisAssignmentBlock(printer::EmitContext* emitContext, Node* node);
// isClassNamedEvaluationHelperBlock — namedevaluation.go:16
bool isClassNamedEvaluationHelperBlock(printer::EmitContext* emitContext,
                                       Node* node);
// classHasExplicitlyAssignedName — namedevaluation.go:38
bool classHasExplicitlyAssignedName(printer::EmitContext* emitContext,
                                    Node* node);

// === end slice: classfields ===

}  // namespace tsc::transformers::estransforms
