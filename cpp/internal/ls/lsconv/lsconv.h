// lsconv — dep-decl: converters.go's FileNameToDocumentURI helper, owned by
// the lsconv slice. Converters itself is dep-declared by
// internal/ls/change/change.h (the consumer that already existed); this
// header adds only what autoimport needs beyond it. The real implementation
// lands with the lsconv slice.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "internal/ast/ast.h" // tscUnreachable
#include "internal/core/text.h" // TextRange
#include "internal/spanmap/spanmap.h" // spanmap::Fidelity, spanmap::Feature
#include "internal/gostd/gostd.h" // gostd::Context
#include "internal/ls/lsutil/lsutil.h"

namespace tsc::lsconv {

// FileNameToDocumentURI — converters.go:332.
// dep-stub — owned by ls/lsconv.
inline tsc::lsp::lsproto::DocumentUri fileNameToDocumentURI(
    std::string_view fileName) {
	TSC_UNREACHABLE("fileNameToDocumentURI — owned by ls/lsconv");
}

// FileNameToDocumentURI — converters.go:332 (exported spelling used by the
// project slice). dep-stub — owned by ls/lsconv.
inline tsc::lsp::lsproto::DocumentUri FileNameToDocumentURI(
    std::string_view fileName) {
	TSC_UNREACHABLE("FileNameToDocumentURI — owned by ls/lsconv");
}

// LSPLineMap — linemap.go:14. dep-stub — owned by ls/lsconv.
struct LSPLineMap;

// ComputeLSPLineStarts — linemap.go:19. dep-stub — owned by ls/lsconv.
inline LSPLineMap* ComputeLSPLineStarts(std::string_view text) {
	TSC_UNREACHABLE("ComputeLSPLineStarts — owned by ls/lsconv");
}

// DiagnosticToLSPPush — owned by ls/lsconv. dep-stub.
struct Converters;
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

// MappedSpan — converters.go:30 (fields flattened from the embedded
// spanmap.MappedSpan). Canonical decl; change.h re-uses this.
template <class T>
struct MappedSpan {
	T* Script;
	tsc::TextRange Span;
	tsc::spanmap::Fidelity Fidelity;
};

// MappedPosition — converters.go:35 (embedded spanmap.MappedPosition
// flattened the same way).
template <class T>
struct MappedPosition {
	T* Script;
	tsc::TextPos Position;
	tsc::spanmap::Fidelity Fidelity;
};

// NewConverters — converters.go:52. dep-stub — owned by ls/lsconv.
inline Converters* NewConverters(
	tsc::lsp::lsproto::PositionEncodingKind positionEncoding,
	std::function<LSPLineMap*(const std::string&)> getLineMap) {
	TSC_UNREACHABLE("NewConverters — owned by ls/lsconv");
}

// Converters.FromLSPRange — converters.go:127. dep-stub — owned by
// ls/lsconv.
template <typename T>
std::vector<MappedSpan<T>> convertersFromLSPRange(
	Converters* c, const T& script,
	const tsc::lsp::lsproto::Range& textRange, spanmap::Feature feature) {
	TSC_UNREACHABLE("Converters.FromLSPRange — owned by ls/lsconv");
}

} // namespace tsc::lsconv
