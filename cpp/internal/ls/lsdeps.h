// === slice: ls-coreA (dep declarations) ===
// Declarations for sibling packages that have not landed on this branch:
// ls/lsutil (beyond the format.h block), ls/lsconv, ls/change, ls/autoimport.
// Types and pure-data constants below are faithful ports; function bodies are
// dep-stubs in lsdeps.cpp (TSC_UNREACHABLE("<name> — <slice>")). When the
// owner slices land, replace this file with their real headers.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <functional>
#include <tuple>

#include "internal/ast/ast.h"
#include "internal/core/types.h"
#include "internal/format/format.h" // lsutil FormatCodeSettings dep block
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/modulespecifiers/types.h"
#include "internal/printer/emitcontext.h"
#include "internal/printer/printer.h"
#include "internal/spanmap/spanmap.h"
#include "internal/ls/lsconv/lsconv.h" // LSPLineMap, FileNameToDocumentURI

namespace tsc::checker {
class Checker;
struct Type;
} // namespace tsc::checker
namespace tsc::compiler {
class SimpleProgram;
} // namespace tsc::compiler

namespace tsc::ls::change {
struct Tracker;
} // namespace tsc::ls::change


namespace tsc::lsconv {

// === lsconv decls — dep-stubs (bodies in lsdeps.cpp) ===

// converters.go:33 — Go `Script` interface. SourceFile already implements all
// five methods; callers pass `*ast::SourceFile` for T.
template <typename T>
concept Script = requires(T t) {
	{ t->FileName() };
	{ t->OriginalFileName() };
	{ t->Text() };
	{ t->SpanMap() };
	{ t->OriginalText() };
};

// converters.go:25 — Go field promotion = C++ public base.
template <Script T>
struct MappedSpan : spanmap::MappedSpan {
	T Script;
};

// converters.go:30
template <Script T>
struct MappedPosition : spanmap::MappedPosition {
	T Script;
};

// converters.go:21 — `type Converters struct`.
class Converters {
public:
	Converters(lsproto::PositionEncodingKind positionEncoding,
	           std::function<LSPLineMap*(const std::string&)> getLineMap)
	    : positionEncoding_(std::move(positionEncoding)),
	      getLineMap_(std::move(getLineMap)) {}

	// converters.go:59 ToLSPRange.
	template <Script T>
	std::pair<lsproto::Range, spanmap::Fidelity> ToLSPRange(
	    T script, TextRange textRange);
	// converters.go:70 ToLSPRangeForFeature.
	template <Script T>
	std::pair<lsproto::Range, spanmap::Fidelity> ToLSPRangeForFeature(
	    T script, TextRange textRange, spanmap::Feature feature);
	// converters.go:83 ToLSPPosition.
	template <Script T>
	std::pair<lsproto::Position, spanmap::Fidelity> ToLSPPosition(
	    T script, TextPos position);
	// converters.go:93 ToLSPPositionForFeature.
	template <Script T>
	std::pair<lsproto::Position, spanmap::Fidelity> ToLSPPositionForFeature(
	    T script, TextPos position, spanmap::Feature feature);
	// converters.go:105 ToLSPLocation.
	template <Script T>
	std::pair<lsproto::Location, spanmap::Fidelity> ToLSPLocation(
	    T script, TextRange rng);
	// converters.go:118 ToLSPLocationForFeature.
	template <Script T>
	std::pair<lsproto::Location, spanmap::Fidelity> ToLSPLocationForFeature(
	    T script, TextRange rng, spanmap::Feature feature);

	// converters.go:202 FromLSPPositionForSourceFile.
	std::vector<MappedPosition<SourceFile*>> FromLSPPositionForSourceFile(
	    SourceFile* file, lsproto::Position position, spanmap::Feature feature);
	// converters.go:133 FromLSPRangeForSourceFile.
	std::vector<MappedSpan<SourceFile*>> FromLSPRangeForSourceFile(
	    SourceFile* file, lsproto::Range textRange, spanmap::Feature feature);
	// converters.go:151 FromLSPRangeIntersectingForSourceFile.
	std::vector<MappedSpan<SourceFile*>> FromLSPRangeIntersectingForSourceFile(
	    SourceFile* file, lsproto::Range textRange, spanmap::Feature feature);
	// converters.go:220 FromLSPRangeToOriginal.
	TextRange FromLSPRangeToOriginal(SourceFile* script,
	                                 lsproto::Range textRange);

private:
	lsproto::PositionEncodingKind positionEncoding_;
	std::function<LSPLineMap*(const std::string&)> getLineMap_;
};

// converters.go:43 NewConverters.
Converters* NewConverters(
    lsproto::PositionEncodingKind positionEncoding,
    std::function<LSPLineMap*(const std::string&)> getLineMap);

} // namespace tsc::lsconv



