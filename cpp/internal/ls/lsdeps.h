// === slice: ls-coreA (dep declarations) ===
// Declarations for sibling packages that have not landed on this branch:
// ls/lsutil (beyond the format.h block), ls/change, ls/autoimport.
// Types and pure-data constants below are faithful ports; function bodies for
// them are dep-stubs in lsdeps.cpp (TSC_UNREACHABLE("<name> — <slice>")). When
// the owner slices land, replace this file with their real headers.
//
// The lsconv Converters (converters.go) are now the REAL port — templated
// members are defined in this header like Go's generic functions, and the
// SourceFile-specific members are defined in lsdeps.cpp.
#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include <functional>

#include "internal/ast/ast.h"
#include "internal/core/types.h"
#include "internal/format/format.h" // lsutil FormatCodeSettings dep block
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/modulespecifiers/types.h"
#include "internal/printer/emitcontext.h"
#include "internal/printer/printer.h"
#include "internal/spanmap/spanmap.h"
#include "internal/stringutil/stringutil.h" // decodeUtf8RuneStrict
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

// === lsconv — converters.go ===

// converters.go:33 — Go `Script` interface. SourceFile, originalTextScript and
// the project package's overlay implement the five methods; callers pass the
// pointer type for T (`*ast::SourceFile`, `*originalTextScript`, ...).
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

// converters.go:30.
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

	// converters.go:66 ToLSPRange.
	template <Script T>
	std::pair<lsproto::Range, spanmap::Fidelity> ToLSPRange(
	    T script, TextRange textRange);
	// converters.go:78 ToLSPRangeForFeature.
	template <Script T>
	std::pair<lsproto::Range, spanmap::Fidelity> ToLSPRangeForFeature(
	    T script, TextRange textRange, spanmap::Feature feature);
	// converters.go:89 ToLSPPosition.
	template <Script T>
	std::pair<lsproto::Position, spanmap::Fidelity> ToLSPPosition(
	    T script, TextPos position);
	// converters.go:98 ToLSPPositionForFeature.
	template <Script T>
	std::pair<lsproto::Position, spanmap::Fidelity> ToLSPPositionForFeature(
	    T script, TextPos position, spanmap::Feature feature);
	// converters.go:107 ToLSPLocation.
	template <Script T>
	std::pair<lsproto::Location, spanmap::Fidelity> ToLSPLocation(
	    T script, TextRange rng);
	// converters.go:119 ToLSPLocationForFeature.
	template <Script T>
	std::pair<lsproto::Location, spanmap::Fidelity> ToLSPLocationForFeature(
	    T script, TextRange rng, spanmap::Feature feature);

	// converters.go:218 FromLSPPositionForSourceFile.
	std::vector<MappedPosition<SourceFile*>> FromLSPPositionForSourceFile(
	    SourceFile* file, lsproto::Position position, spanmap::Feature feature);
	// converters.go:134 FromLSPRangeForSourceFile.
	std::vector<MappedSpan<SourceFile*>> FromLSPRangeForSourceFile(
	    SourceFile* file, lsproto::Range textRange, spanmap::Feature feature);
	// converters.go:149 FromLSPRangeIntersectingForSourceFile.
	std::vector<MappedSpan<SourceFile*>> FromLSPRangeIntersectingForSourceFile(
	    SourceFile* file, lsproto::Range textRange, spanmap::Feature feature);
	// converters.go:224 FromLSPRangeToOriginal.
	template <Script T>
	TextRange FromLSPRangeToOriginal(T script, lsproto::Range textRange);

private:
	// The generic FromLSPRange/FromLSPPosition Go methods (converters.go:127
	// and :211) are free templates here — befriend them so they can call the
	// private lsp*ToVirtual helpers below.
	template <typename U>
	friend std::vector<MappedSpan<U>> convertersFromLSPRange(
	    Converters*, const U&, const lsproto::Range&, spanmap::Feature);
	template <typename U>
	friend std::vector<MappedPosition<U>> convertersFromLSPPosition(
	    Converters*, const U&, lsproto::Position, spanmap::Feature);

	// converters.go:177/187 — lspRangeToVirtualForScripts / lspRangeToVirtual.
	template <Script T>
	std::vector<MappedSpan<T>> lspRangeToVirtualForScripts(
	    const std::vector<T>& scripts, lsproto::Range textRange,
	    spanmap::Feature feature);
	template <Script T>
	std::vector<spanmap::MappedSpan> lspRangeToVirtual(
	    T script, lsproto::Range textRange, spanmap::Feature feature);
	// converters.go:239/249 — lspPositionToVirtualForScripts /
	// lspPositionToVirtual.
	template <Script T>
	std::vector<MappedPosition<T>> lspPositionToVirtualForScripts(
	    const std::vector<T>& scripts, lsproto::Position position,
	    spanmap::Feature feature);
	template <Script T>
	std::vector<spanmap::MappedPosition> lspPositionToVirtual(
	    T script, lsproto::Position position, spanmap::Feature feature);
	// converters.go:366/417 — raw coordinate converters. Require
	// non-content-mapped scripts (debug.Assert in Go).
	template <Script T>
	TextPos lineAndCharacterToPosition(T script,
	                                   lsproto::Position lineAndCharacter) const;
	template <Script T>
	lsproto::Position positionToLineAndCharacter(T script,
	                                             TextPos position) const;

	lsproto::PositionEncodingKind positionEncoding_;
	std::function<LSPLineMap*(const std::string&)> getLineMap_;
};

// converters.go:596 — originalTextScript presents a content-mapped file's
// original (untransformed) text as a Script, so that ranges already mapped
// into that text convert to the correct line/character positions.
struct originalTextScript {
	std::string fileName;
	std::string text;

	const std::string& FileName() const { return fileName; }
	const std::string& OriginalFileName() const { return fileName; }
	const std::string& Text() const { return text; }
	const std::string& OriginalText() const { return text; }
	spanmap::SpanMap* SpanMap() const { return nullptr; }
};

// The Go `Script` result of virtualRangeToOriginal / virtualPositionToOriginal:
// either the caller's script unchanged (unmapped) or an originalTextScript
// (mapped). Go returns the Script interface; the C++ port uses a variant.
template <Script T>
using ScriptOrOriginal = std::variant<T, originalTextScript>;

// scriptArg adapts a ScriptOrOriginal arm to the Script pointer the raw
// converters take: pointer arms pass through, an originalTextScript value arm
// yields a pointer to it.
template <typename S>
constexpr auto scriptArg(S&& s) noexcept {
	if constexpr (std::is_pointer_v<std::decay_t<S>>) {
		return s;
	} else {
		return std::addressof(s);
	}
}

// converters.go:261 — virtualRangeToOriginal maps a content mapper's virtual
// range back to its original text. A nil feature bypasses feature filtering
// for diagnostics and edits.
template <Script T>
std::tuple<ScriptOrOriginal<T>, TextRange, spanmap::Fidelity>
virtualRangeToOriginal(T script, TextRange textRange,
                       const spanmap::Feature* feature) {
	if (script->SpanMap() == nullptr) {
		return {script, textRange, spanmap::FidelityExact};
	}
	TextRange mapped;
	spanmap::Fidelity fidelity;
	if (feature == nullptr) {
		auto [m, f] =
		    spanmap::VirtualToOriginalSpan(script->SpanMap(), textRange);
		mapped = m;
		fidelity = f;
	} else {
		auto [m, f] = spanmap::VirtualToOriginalSpanForFeature(
		    script->SpanMap(), textRange, *feature);
		mapped = m;
		fidelity = f;
	}
	return {originalTextScript{script->OriginalFileName(),
	                           script->OriginalText()},
	        mapped, fidelity};
}

// converters.go:276 — virtualPositionToOriginal is the single-position analog
// of virtualRangeToOriginal.
template <Script T>
std::tuple<ScriptOrOriginal<T>, TextPos, spanmap::Fidelity>
virtualPositionToOriginal(T script, TextPos position,
                          const spanmap::Feature* feature) {
	if (script->SpanMap() == nullptr) {
		return {script, position, spanmap::FidelityExact};
	}
	TextPos mapped;
	spanmap::Fidelity fidelity;
	if (feature == nullptr) {
		auto [m, f] =
		    spanmap::VirtualToOriginalPosition(script->SpanMap(), position);
		mapped = m;
		fidelity = f;
	} else {
		auto [m, f] = spanmap::VirtualToOriginalPositionForFeature(
		    script->SpanMap(), position, *feature);
		mapped = m;
		fidelity = f;
	}
	return {originalTextScript{script->OriginalFileName(),
	                           script->OriginalText()},
	        mapped, fidelity};
}

// utf16.RuneLen — utf16.go. Returns -1 for surrogate code points and runes
// outside the valid range, mirroring Go exactly (the port's strict UTF-8
// decoder never produces either, but the faithful helper keeps the Go guard).
inline int32_t utf16RuneLen(char32_t r) {
	if (r < 0xD800 || (r >= 0xE000 && r < 0x10000)) {
		return 1;
	}
	if (r >= 0x10000 && r <= 0x10FFFF) {
		return 2;
	}
	return -1;
}

// converters.go:66 — ToLSPRange.
template <Script T>
std::pair<lsproto::Range, spanmap::Fidelity> Converters::ToLSPRange(
    T script, TextRange textRange) {
	auto [s, rng, fidelity] =
	    virtualRangeToOriginal(script, textRange, nullptr);
	return {lsproto::Range{
	            std::visit(
	                [this, pos = rng.pos()](auto&& sc) {
		                return positionToLineAndCharacter(scriptArg(sc), pos);
	                },
	                s),
	            std::visit(
	                [this, pos = rng.end()](auto&& sc) {
		                return positionToLineAndCharacter(scriptArg(sc), pos);
	                },
	                s)},
	        fidelity};
}

// converters.go:78 — ToLSPRangeForFeature.
template <Script T>
std::pair<lsproto::Range, spanmap::Fidelity> Converters::ToLSPRangeForFeature(
    T script, TextRange textRange, spanmap::Feature feature) {
	auto [s, rng, fidelity] =
	    virtualRangeToOriginal(script, textRange, &feature);
	return {lsproto::Range{
	            std::visit(
	                [this, pos = rng.pos()](auto&& sc) {
		                return positionToLineAndCharacter(scriptArg(sc), pos);
	                },
	                s),
	            std::visit(
	                [this, pos = rng.end()](auto&& sc) {
		                return positionToLineAndCharacter(scriptArg(sc), pos);
	                },
	                s)},
	        fidelity};
}

// converters.go:89 — ToLSPPosition.
template <Script T>
std::pair<lsproto::Position, spanmap::Fidelity> Converters::ToLSPPosition(
    T script, TextPos position) {
	auto [s, pos, fidelity] =
	    virtualPositionToOriginal(script, position, nullptr);
	return {std::visit(
	            [this, p = pos](auto&& sc) {
		            return positionToLineAndCharacter(scriptArg(sc), p);
	            },
	            s),
	        fidelity};
}

// converters.go:98 — ToLSPPositionForFeature.
template <Script T>
std::pair<lsproto::Position, spanmap::Fidelity>
Converters::ToLSPPositionForFeature(T script, TextPos position,
                                    spanmap::Feature feature) {
	auto [s, pos, fidelity] =
	    virtualPositionToOriginal(script, position, &feature);
	return {std::visit(
	            [this, p = pos](auto&& sc) {
		            return positionToLineAndCharacter(scriptArg(sc), p);
	            },
	            s),
	        fidelity};
}

// converters.go:107 — ToLSPLocation.
template <Script T>
std::pair<lsproto::Location, spanmap::Fidelity> Converters::ToLSPLocation(
    T script, TextRange rng) {
	auto [lspRange, fidelity] = ToLSPRange(script, rng);
	return {lsproto::Location{
	            .Uri = FileNameToDocumentURI(script->OriginalFileName()),
	            .Range = lspRange},
	        fidelity};
}

// converters.go:119 — ToLSPLocationForFeature.
template <Script T>
std::pair<lsproto::Location, spanmap::Fidelity>
Converters::ToLSPLocationForFeature(T script, TextRange rng,
                                    spanmap::Feature feature) {
	auto [lspRange, fidelity] = ToLSPRangeForFeature(script, rng, feature);
	return {lsproto::Location{
	            .Uri = FileNameToDocumentURI(script->OriginalFileName()),
	            .Range = lspRange},
	        fidelity};
}

// converters.go:177 — lspRangeToVirtualForScripts.
template <Script T>
std::vector<MappedSpan<T>> Converters::lspRangeToVirtualForScripts(
    const std::vector<T>& scripts, lsproto::Range textRange,
    spanmap::Feature feature) {
	std::vector<MappedSpan<T>> result;
	result.reserve(scripts.size());
	for (const T& script : scripts) {
		for (const auto& mapped : lspRangeToVirtual(script, textRange, feature)) {
			result.push_back(MappedSpan<T>{mapped, script});
		}
	}
	return result;
}

// converters.go:187 — lspRangeToVirtual.
template <Script T>
std::vector<spanmap::MappedSpan> Converters::lspRangeToVirtual(
    T script, lsproto::Range textRange, spanmap::Feature feature) {
	spanmap::SpanMap* spans = script->SpanMap();
	if (spans == nullptr) {
		return {spanmap::MappedSpan{
		    TextRange{
		        lineAndCharacterToPosition(script, textRange.Start),
		        lineAndCharacterToPosition(script, textRange.End)},
		    spanmap::FidelityExact}};
	}
	// A content-mapped script's line map is its original text's, so convert
	// against that text and then map the resulting original range forward
	// into the virtual text.
	originalTextScript original{script->OriginalFileName(),
	                            script->OriginalText()};
	TextRange origRange{
	    lineAndCharacterToPosition(&original, textRange.Start),
	    lineAndCharacterToPosition(&original, textRange.End)};
	return spanmap::OriginalToVirtualSpans(spans, origRange, feature);
}

// converters.go:239 — lspPositionToVirtualForScripts.
template <Script T>
std::vector<MappedPosition<T>> Converters::lspPositionToVirtualForScripts(
    const std::vector<T>& scripts, lsproto::Position position,
    spanmap::Feature feature) {
	std::vector<MappedPosition<T>> result;
	result.reserve(scripts.size());
	for (const T& script : scripts) {
		for (const auto& mapped :
		     lspPositionToVirtual(script, position, feature)) {
			result.push_back(MappedPosition<T>{mapped, script});
		}
	}
	return result;
}

// converters.go:249 — lspPositionToVirtual.
template <Script T>
std::vector<spanmap::MappedPosition> Converters::lspPositionToVirtual(
    T script, lsproto::Position position, spanmap::Feature feature) {
	spanmap::SpanMap* spans = script->SpanMap();
	if (spans == nullptr) {
		return {spanmap::MappedPosition{
		    lineAndCharacterToPosition(script, position),
		    spanmap::FidelityExact}};
	}
	originalTextScript original{script->OriginalFileName(),
	                            script->OriginalText()};
	TextPos origOffset = lineAndCharacterToPosition(&original, position);
	return spanmap::OriginalToVirtualPositions(spans, origOffset, feature);
}

// converters.go:366 — lineAndCharacterToPosition: UTF-8/16 0-indexed line and
// character to UTF-8 offset.
template <Script T>
TextPos Converters::lineAndCharacterToPosition(
    T script, lsproto::Position lineAndCharacter) const {
	// debug.Assert — always-on like Go's panic; a real debug::assert would
	// force debug.h (which #undefs `assert`) onto every TU including this
	// header.
	if (script->SpanMap() != nullptr) {
		tsc::tscUnreachable(
		    "raw coordinate conversion requires a non-content-mapped script");
	}

	auto* lineMap = getLineMap_(script->FileName());

	TextPos line = (TextPos)lineAndCharacter.Line;
	TextPos chr = (TextPos)lineAndCharacter.Character;

	TextPos textLen = (TextPos)script->Text().size();

	// Clamp line to valid range.
	if (line >= (TextPos)lineMap->LineStarts.size()) {
		return textLen;
	}

	TextPos start = lineMap->LineStarts[line];

	// Determine the end of this line (start of next line, or end of text).
	TextPos lineEnd;
	if (line + 1 < (TextPos)lineMap->LineStarts.size()) {
		lineEnd = lineMap->LineStarts[line + 1];
	} else {
		lineEnd = textLen;
	}

	if (lineMap->AsciiOnly || positionEncoding_ == lsproto::PositionEncodingKindUTF8) {
		// max(start, min(start+char, lineEnd)) — uint32 math to mirror Go's
		// wrapping int32 addition on untrusted LSP coordinates.
		TextPos startPlusChar =
		    (TextPos)((uint32_t)start + (uint32_t)chr);
		return std::max(start, std::min(startPlusChar, lineEnd));
	}

	// Scan from line start counting UTF-16 code units to find the byte
	// position. Uses decodeUtf8RuneStrict (Go's utf8.DecodeRuneInString
	// semantics) so invalid UTF-8 bytes advance by their actual size (1)
	// rather than RuneLen(RuneError) == 3. This matches the approach in
	// scanner.ComputePositionOfLineAndUTF16Character.
	TextPos utf16Char = 0;
	TextPos pos = start;
	TextPos end = lineEnd;
	std::string_view text = script->Text();
	while (pos < end) {
		int size;
		char32_t r = decodeUtf8RuneStrict(text.substr(pos), &size);
		TextPos u16Len = (TextPos)utf16RuneLen(r);
		if ((TextPos)((uint32_t)utf16Char + (uint32_t)u16Len) > chr) {
			break;
		}
		utf16Char = (TextPos)((uint32_t)utf16Char + (uint32_t)u16Len);
		pos += size;
	}

	return pos;
}

// converters.go:417 — positionToLineAndCharacter: UTF-8 offset to UTF-8/16
// 0-indexed line and character.
template <Script T>
lsproto::Position Converters::positionToLineAndCharacter(
    T script, TextPos position) const {
	// debug.Assert — see the note in lineAndCharacterToPosition above.
	if (script->SpanMap() != nullptr) {
		tsc::tscUnreachable(
		    "raw coordinate conversion requires a non-content-mapped script");
	}

	position =
	    std::max<TextPos>(0, std::min<TextPos>(position, (TextPos)script->Text().size()));

	auto* lineMap = getLineMap_(script->FileName());

	// slices.BinarySearch + not-found adjust + clamp — the same computation
	// as computeLineOfPosition, ported as LSPLineMap::ComputeIndexOfLineStart.
	int line = lineMap->ComputeIndexOfLineStart(position);

	// The current line ranges from lineMap.LineStarts[line] (or 0) to
	// lineMap.LineStarts[line+1] (or len(text)).
	TextPos start = lineMap->LineStarts[line];

	TextPos character;
	if (lineMap->AsciiOnly || positionEncoding_ == lsproto::PositionEncodingKindUTF8) {
		character = position - start;
	} else {
		// We need to rescan the text as UTF-16 to find the character offset.
		// `for _, r := range text[start:position]` — Go's range decode turns
		// invalid bytes into RuneError advancing 1 byte; decodeUtf8RuneStrict
		// reproduces that.
		character = 0;
		std::string_view text = script->Text();
		for (TextPos p = start; p < position;) {
			int size;
			char32_t r = decodeUtf8RuneStrict(text.substr(p), &size);
			character = (TextPos)((uint32_t)character + (uint32_t)utf16RuneLen(r));
			p += size;
		}
	}

	return lsproto::Position{(uint32_t)line, (uint32_t)character};
}

// Converters.FromLSPRange — converters.go:127.
template <typename T>
std::vector<MappedSpan<T>> convertersFromLSPRange(
    Converters* c, const T& script, const lsproto::Range& textRange,
    spanmap::Feature feature) {
	return c->lspRangeToVirtualForScripts(std::vector<T>{script}, textRange,
	                                      feature);
}

// Converters.FromLSPPosition — converters.go:211.
template <typename T>
std::vector<MappedPosition<T>> convertersFromLSPPosition(
    Converters* c, const T& script, lsproto::Position position,
    spanmap::Feature feature) {
	return c->lspPositionToVirtualForScripts(std::vector<T>{script}, position,
	                                         feature);
}

// converters.go:224 — FromLSPRangeToOriginal.
template <Script T>
TextRange Converters::FromLSPRangeToOriginal(T script,
                                             lsproto::Range textRange) {
	originalTextScript original{script->OriginalFileName(),
	                            script->OriginalText()};
	return TextRange{lineAndCharacterToPosition(&original, textRange.Start),
	                 lineAndCharacterToPosition(&original, textRange.End)};
}

// converters.go:52 NewConverters.
Converters* NewConverters(
    lsproto::PositionEncodingKind positionEncoding,
    std::function<LSPLineMap*(const std::string&)> getLineMap);

} // namespace tsc::lsconv



