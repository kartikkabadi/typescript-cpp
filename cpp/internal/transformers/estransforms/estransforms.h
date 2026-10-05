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

// === slice: async family ===

// newAsyncTransformer — async.go:37
Transformer* newAsyncTransformer(TransformOptions* opts);

// isSimpleParameterList — async.go:951. Shared by async.cpp and forawait.cpp.
bool isSimpleParameterList(const std::vector<Node*>& params);

// newforawaitTransformer — forawait.go:58
Transformer* newforawaitTransformer(TransformOptions* opts);

// newUsingDeclarationTransformer — using.go:19
Transformer* newUsingDeclarationTransformer(TransformOptions* opts);

// Dep-stubs — owned by the namedevaluation slice (esdecorator-cluster);
// stubbed in using.cpp until namedevaluation.cpp lands.
// isNamedEvaluation — namedevaluation.go:85
bool isNamedEvaluation(printer::EmitContext* emitContext, Node* node);
// transformNamedEvaluation — namedevaluation.go:513
Node* transformNamedEvaluation(printer::EmitContext* context, Node* node,
                               bool ignoreEmptyStringLiteral,
                               std::string assignedName);

}  // namespace tsc::transformers::estransforms
