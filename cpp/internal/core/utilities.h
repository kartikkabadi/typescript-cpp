// Port of selected free helpers from tsc/internal/core/core.go.
#pragma once

#include <string_view>

#include "internal/core/types.h"
#include "internal/tspath/tspath.h"

namespace tsc {

// ShouldRewriteModuleSpecifier — core.go:724
inline bool shouldRewriteModuleSpecifier(std::string_view specifier,
                                         const CompilerOptions* options) {
	return tristateIsTrue(options->RewriteRelativeImportExtensions) &&
	       tspath::pathIsRelative(specifier) &&
	       !tspath::isDeclarationFileName(specifier) &&
	       tspath::hasTSFileExtension(specifier);
}

} // namespace tsc
