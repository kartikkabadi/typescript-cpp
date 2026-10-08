// lsconv — partial port: linemap.go + FileNameToDocumentURI + the
// diagnostic converters (converters.go). The stateful Converters type's
// canonical definition lives in internal/ls/lsdeps.h (which includes this
// header); its SourceFile member bodies and NewConverters are defined in
// lsdeps.cpp, and the diagnostic converters in lsconv.cpp.
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

// FileNameToDocumentURI (converters.go:332) — std::string_view convenience
// overload; forwards to the real port above.
inline tsc::lsp::lsproto::DocumentUri FileNameToDocumentURI(
    std::string_view fileName) {
	return FileNameToDocumentURI(std::string(fileName));
}

// DiagnosticToLSPPull (converters.go:459) — defined in lsconv.cpp.
class Converters;
lsproto::Diagnostic* DiagnosticToLSPPull(
    gostd::Context ctx, Converters* converters, Diagnostic* diagnostic,
    bool reportStyleChecksAsWarnings);

// ComputeLSPLineStarts (linemap.go:19) — std::string_view convenience
// overload; forwards to the real port above.
inline LSPLineMap* ComputeLSPLineStarts(std::string_view text) {
	return ComputeLSPLineStarts(std::string(text));
}

// DiagnosticToLSPPush (converters.go:469) — defined in lsconv.cpp.
lsproto::Diagnostic* DiagnosticToLSPPush(
    gostd::Context ctx, Converters* converters, Diagnostic* diag);

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

// MappedSpan / MappedPosition and the Converters class — canonical
// definitions live in lsdeps.h (field-promoted spanmap bases, `T Script`
// value member); that header includes this one.

// NewConverters — converters.go:43. Declared here for project/* headers that
// do not include lsdeps.h; also declared (canonically) in lsdeps.h and
// defined in lsdeps.cpp.
Converters* NewConverters(
	tsc::lsp::lsproto::PositionEncodingKind positionEncoding,
	std::function<LSPLineMap*(const std::string&)> getLineMap);


} // namespace tsc::lsconv
