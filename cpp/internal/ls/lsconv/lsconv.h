// lsconv — dep-decl: converters.go's FileNameToDocumentURI helper, owned by
// the lsconv slice. Converters itself is dep-declared by
// internal/ls/change/change.h (the consumer that already existed); this
// header adds only what autoimport needs beyond it. The real implementation
// lands with the lsconv slice.
#pragma once

#include <string>

#include "internal/ast/ast.h" // tscUnreachable
#include "internal/ls/lsutil/lsutil.h"

namespace tsc::lsconv {

// FileNameToDocumentURI — converters.go:332.
// dep-stub — owned by ls/lsconv.
inline tsc::lsp::lsproto::DocumentUri fileNameToDocumentURI(
    std::string_view fileName) {
	TSC_UNREACHABLE("fileNameToDocumentURI — owned by ls/lsconv");
}

} // namespace tsc::lsconv
