// lsconv — dep-decl for the ls-autoimport slice: the LSP converters struct
// (converters.go) that flows through change::Tracker, and the
// FileNameToDocumentURI helper. Conversion behavior is owned by the lsconv
// slice — functions are dep-stubs.
#pragma once

#include <string>

#include "internal/ast/ast.h" // tscUnreachable
#include "internal/lsp/lsproto/lsproto.h"

namespace tsc::lsconv {

// === dep decls for ls-autoimport — owned by ls/lsconv ===

// Converters — converters.go:23. Opaque to ls-autoimport (only stored and
// passed to dep-stubbed functions).
struct Converters {};

// FileNameToDocumentURI — converters.go:332.
// dep-stub — owned by ls/lsconv.
inline lsproto::DocumentUri fileNameToDocumentURI(std::string_view fileName) {
	TSC_UNREACHABLE("fileNameToDocumentURI — owned by ls/lsconv");
}

} // namespace tsc::lsconv
