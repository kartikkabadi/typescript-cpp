// lsconv — partial port: linemap.go + FileNameToDocumentURI (converters.go)
// landed with the api slice. The stateful Converters type is dep-declared by
// internal/ls/change/change.h (the consumer that already existed); the lsp
// slice owns the real package and will replace these decls when it lands.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "internal/gostd/gostd.h"
#include "internal/lsp/lsproto/lsproto.h"

namespace tsc {
struct Diagnostic;
}

namespace tsc::lsconv {

// LSPLineStarts / LSPLineMap (linemap.go:11-16).
using LSPLineStarts = std::vector<int32_t>; // core.TextPos

struct LSPLineMap {
	LSPLineStarts LineStarts;
	bool AsciiOnly = true; // TODO(jakebailey): collect ascii-only info per line

	// ComputeIndexOfLineStart (linemap.go:56).
	int ComputeIndexOfLineStart(int32_t targetPos) const;
};

// ComputeLSPLineStarts (linemap.go:19) — line starts considering only
// "\n", "\r", "\r\n" as line breaks, plus an ASCII-only flag.
LSPLineMap* ComputeLSPLineStarts(const std::string& text);

// FileNameToDocumentURI (converters.go:332).
lsproto::DocumentUri FileNameToDocumentURI(const std::string& fileName);

// DiagnosticToLSPPull (converters.go:459) — dep-stub: the full port lands
// with the lsconv slice (needs the unported localize machinery).
class Converters;
lsproto::Diagnostic* DiagnosticToLSPPull(
    gostd::Context ctx, Converters* converters, Diagnostic* diagnostic,
    bool reportStyleChecksAsWarnings);

} // namespace tsc::lsconv
