// Port of tsc/internal/printer/emitresolver.go — the EmitResolver interface
// surface seen by transformers and the printer. Only the methods needed by
// landed slices are declared so far; extend as more slices consume it.
// The concrete implementation is tsc::checker::EmitResolver (checker.h),
// produced by checker::newEmitResolver.
#pragma once

#include "internal/ast/ast.h"
#include "internal/binder/referenceresolver.h"

namespace tsc {

// EmitResolver — emitresolver.go:76. Go's interface embeds
// binder.ReferenceResolver; mirrored via inheritance.
struct EmitResolver : binder::ReferenceResolver {
	// JSX Emit
	virtual Node* GetJsxFactoryEntity(Node* location) = 0;
	virtual Node* GetJsxFragmentFactoryEntity(Node* location) = 0;
	virtual void SetReferencedImportDeclaration(Node* node, Node* ref) = 0;
};

}  // namespace tsc
