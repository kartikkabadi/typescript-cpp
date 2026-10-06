// === dep decls — owned by lsp (ls/lsconv) ===
// Decls the api slice needs from tsc/internal/ls/lsconv (linemap.go,
// converters.go, uri.go). Pure helpers (LSPLineMap, ComputeLSPLineStarts,
// FileNameToDocumentURI) are ported faithfully; the stateful Converters type
// is an opaque pointer the api only forwards to autoimport. The lsp slice
// should replace this file when it lands.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "internal/lsp/lsproto/lsproto.h"

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

// Converters (converters.go:25) — opaque to the api slice; owned by lsp.
struct Converters {};

} // namespace tsc::lsconv
