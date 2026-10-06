// lsproto — dep-stubs for the ls-autoimport slice. Bodies panic like the
// unported sibling-slice functions they stand in for.
#include "internal/lsp/lsproto/lsproto.h"

#include "internal/ast/ast.h" // tscUnreachable

namespace tsc::lsproto {

// === dep stubs — removed when owner slice lands ===

std::string documentUriFileName(const DocumentUri&) {
	TSC_UNREACHABLE("documentUriFileName — owned by lsp");
}

tspath::Path documentUriPath(const DocumentUri&, bool) {
	TSC_UNREACHABLE("documentUriPath — owned by lsp");
}

} // namespace tsc::lsproto
