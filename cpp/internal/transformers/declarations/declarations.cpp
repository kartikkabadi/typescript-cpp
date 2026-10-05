// declarations.cpp — dep-stubs for the declarations slice factories.
// DELETE this file when the declarations port lands (it provides the real
// NewDeclarationTransformer/NewSupplementalReferencesTransformer).

#include "internal/transformers/declarations/declarations.h"

namespace tsc::declarations {

// transform.go:103 — dep stub, owned by the declarations slice.
DeclarationTransformer* NewDeclarationTransformer(
    DeclarationEmitHost* /*host*/, printer::EmitContext* /*emitContext*/,
    const CompilerOptions* /*options*/,
    std::string_view /*declarationFilePath*/,
    std::string_view /*declarationMapPath*/) {
	TSC_UNREACHABLE(
	    "NewDeclarationTransformer — owned by declarations slice");
}

// supplementalreferences.go:19 — dep stub, owned by the declarations slice.
DeclarationTransformer* NewSupplementalReferencesTransformer(
    DeclarationEmitHost* /*host*/, SourceFile* /*sourceFile*/,
    std::string_view /*declarationFilePath*/,
    bool /*forceDeclarationPaths*/) {
	TSC_UNREACHABLE(
	    "NewSupplementalReferencesTransformer — owned by declarations slice");
}

} // namespace tsc::declarations
