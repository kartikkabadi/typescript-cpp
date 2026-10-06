// lsconv — partial port: linemap.go + FileNameToDocumentURI (converters.go)
// landed with the api slice. The stateful Converters type is dep-declared by
// internal/ls/change/change.h (the consumer that already existed); the lsp
// slice owns the real package and will replace these decls when it lands.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "internal/lsp/lsproto/lsproto.h"
#include "internal/ast/ast.h" // tscUnreachable
#include "internal/core/text.h" // TextRange
#include "internal/spanmap/spanmap.h" // spanmap::Fidelity, spanmap::Feature
#include "internal/gostd/gostd.h" // gostd::Context
#include "internal/ls/lsutil/lsutil.h"

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

// LSPLineMap — linemap.go:14. dep-stub — owned by ls/lsconv.
struct LSPLineMap;

// DiagnosticToLSPPush — owned by ls/lsconv. dep-stub.
class Converters;
inline tsc::lsp::lsproto::Diagnostic* DiagnosticToLSPPush(
    const gostd::Context& ctx, Converters* converters, Diagnostic* diag) {
	TSC_UNREACHABLE("DiagnosticToLSPPush — owned by ls/lsconv");
}

// === dep decls for project — owned by ls/lsconv ===

// LanguageKindToScriptKind — converters.go:290.
inline ScriptKind LanguageKindToScriptKind(
	const tsc::lsp::lsproto::LanguageKind& languageID) {
	if (languageID == "typescript") {
		return ScriptKind::TS;
	}
	if (languageID == "typescriptreact") {
		return ScriptKind::TSX;
	}
	if (languageID == "javascript") {
		return ScriptKind::JS;
	}
	if (languageID == "javascriptreact") {
		return ScriptKind::JSX;
	}
	if (languageID == "json") {
		return ScriptKind::JSON;
	}
	return ScriptKind::Unknown;
}

// MappedSpan / MappedPosition — canonical definitions live in
// lsdeps.h (field-promoted spanmap bases, `T Script` value member);
// that header includes this one.

// NewConverters — converters.go:52. dep-stub — owned by ls/lsconv.
inline Converters* NewConverters(
	tsc::lsp::lsproto::PositionEncodingKind positionEncoding,
	std::function<LSPLineMap*(const std::string&)> getLineMap) {
	TSC_UNREACHABLE("NewConverters — owned by ls/lsconv");
}


} // namespace tsc::lsconv
