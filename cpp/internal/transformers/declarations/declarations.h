#pragma once
// transformers/declarations/transform.go — DeclarationEmitHost interfaces and
// the declarationTransformer's public surface. The concrete transform is
// ported separately; this header carries the types the emit path needs.

#include <string>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/checker/checker.h"
#include "internal/transformers/transformers.h"

namespace tsc::declarations {

// transform.go:28 OutputPaths
struct OutputPaths {
	virtual ~OutputPaths() = default;
	virtual std::string DeclarationFilePath() = 0;
	virtual std::string JsFilePath() = 0;
};

// transform.go:34 DeclarationEmitHost — Go embeds
// modulespecifiers.ModuleSpecifierGenerationHost; the C++ concrete host is
// checker::Program (the same mapping used by modulespecifiers).
struct DeclarationEmitHost : checker::Program {
	virtual SourceFile* GetSourceFileFromReference(SourceFile* origin,
	                                               FileReference* ref) = 0;
	virtual OutputPaths* GetOutputPathsFor(SourceFile* file,
	                                       bool forceDtsPaths) = 0;
	virtual bool SourceFileMayBeEmitted(SourceFile* file,
	                                    bool forceDtsEmit) = 0;
	virtual ModifierFlags GetEffectiveDeclarationFlags(
	    Node* node, ModifierFlags flags) = 0;
	virtual printer::EmitResolver* GetEmitResolver() = 0;
};

// transform.go:57/63 DeclarationTransformer — public surface used by the
// emit path. The concrete transform lives in transform.cpp (owned by the
// declarations slice).
struct DeclarationTransformer : transformers::Transformer {
	virtual ~DeclarationTransformer() = default;
	virtual SourceFile* TransformSourceFile(SourceFile* file) = 0;
	virtual std::vector<Diagnostic*> GetDiagnostics() = 0;
};

// transform.go:103 NewDeclarationTransformer
DeclarationTransformer* NewDeclarationTransformer(
    DeclarationEmitHost* host, printer::EmitContext* emitContext,
    const CompilerOptions* options, std::string_view declarationFilePath,
    std::string_view declarationMapPath);

// supplementalreferences.go:19 NewSupplementalReferencesTransformer —
// returns *SupplementalReferencesTransformer in Go; it satisfies the
// declarationTransformer interface, so the iface pointer is the return type.
DeclarationTransformer* NewSupplementalReferencesTransformer(
    DeclarationEmitHost* host, SourceFile* sourceFile,
    std::string_view declarationFilePath, bool forceDeclarationPaths);

} // namespace tsc::declarations
