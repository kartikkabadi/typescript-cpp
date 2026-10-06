// === slice: lsconv ===
// converters.go — minimal lsconv declarations for the ls slice port.
// The sibling child package (lsconv slice) owns the real implementation;
// all method bodies here are dep-stubs.
#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "internal/core/types.h"
#include "internal/gostd/gostd.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/spanmap/spanmap.h"

namespace tsc {
struct SourceFile;
struct Diagnostic;
namespace lsconv {

struct LSPLineMap; // converters.go — owned by the lsconv slice

// Script — converters.go:44. Any type with the five accessors qualifies
// (SourceFile, and the ls-internal `script` wrapper in source_map.cpp).
template <typename T>
concept Script = requires(T* t) {
	{ t->FileName() } -> std::convertible_to<std::string>;
	{ t->OriginalFileName() } -> std::convertible_to<std::string>;
	{ t->Text() } -> std::convertible_to<std::string>;
	{ t->SpanMap() } -> std::convertible_to<spanmap::SpanMap*>;
	{ t->OriginalText() } -> std::convertible_to<std::string>;
};

// MappedSpan / MappedPosition — converters.go:31/36.
template <Script S>
struct MappedSpan : spanmap::MappedSpan {
	S* Script_ = nullptr;
};
template <Script S>
struct MappedPosition : spanmap::MappedPosition {
	S* Script_ = nullptr;
};

// Converters — converters.go:25.
struct Converters {
	// Methods — converters.go. Owned by the lsconv slice; dep-stubbed.
	template <Script S>
	std::pair<lsproto::Range, spanmap::Fidelity> ToLSPRange(S* script, TextRange textRange);
	template <Script S>
	std::pair<lsproto::Range, spanmap::Fidelity> ToLSPRangeForFeature(
	    S* script, TextRange textRange, spanmap::Feature feature);
	template <Script S>
	std::pair<lsproto::Position, spanmap::Fidelity> ToLSPPosition(
	    S* script, TextPos position);
	template <Script S>
	std::pair<lsproto::Position, spanmap::Fidelity> ToLSPPositionForFeature(
	    S* script, TextPos position, spanmap::Feature feature);
	template <Script S>
	std::pair<lsproto::Location, spanmap::Fidelity> ToLSPLocation(
	    S* script, TextRange rng);
	template <Script S>
	std::pair<lsproto::Location, spanmap::Fidelity> ToLSPLocationForFeature(
	    S* script, TextRange rng, spanmap::Feature feature);
	std::vector<MappedSpan<SourceFile>> FromLSPRangeForSourceFile(
	    SourceFile* file, lsproto::Range textRange,
	    spanmap::Feature feature);
	std::vector<MappedPosition<SourceFile>> FromLSPPositionForSourceFile(
	    SourceFile* file, lsproto::Position position,
	    spanmap::Feature feature);
};

// DiagnosticToLSPPull — converters.go:458 (dep-stub; lsconv slice owns).
lsproto::Diagnostic* DiagnosticToLSPPull(
    const gostd::Context& ctx, Converters* converters,
    tsc::Diagnostic* diagnostic, bool reportStyleChecksAsWarnings);

// NewConverters — converters.go:52.
Converters* NewConverters(
    lsproto::PositionEncodingKind positionEncoding,
    std::function<LSPLineMap*(const std::string& fileName)> getLineMap);

// FileNameToDocumentURI — converters.go:332.
lsproto::DocumentUri FileNameToDocumentURI(const std::string& fileName);

} // namespace lsconv
} // namespace tsc
