// spanmap.h — Port of tsc/internal/spanmap/spanmap.go
//
// Package spanmap provides bidirectional span-aware mapping between a content mapper's virtual text
// and its original, untransformed source. Unlike a source map, which records
// point correspondences and leaves spans and "no origin" implicit, a SpanMap records explicit segments
// for the parts of the virtual text that correspond to the original; positions not covered by any
// segment are synthesized (virtual content with no original counterpart). All positions are absolute
// offsets (core.TextPos), matching the compiler's TextRange model.
//
// Keep this in sync with spanMap.ts.
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "internal/core/text.h"

namespace tsc::spanmap {

// Kind describes how positions inside a segment relate the virtual span to the original span.
// spanmap.go:22
using Kind = int32_t;
inline constexpr Kind KindVerbatim = 0; // length-preserving; interior positions map 1:1
inline constexpr Kind KindAtom = 1;     // span maps as a whole; positions clamp to endpoints
inline constexpr Kind KindAlias = 2;    // atom geometry + same logical entity assertion

// Feature selects which language-service operations may use a segment. Diagnostics are intentionally not
// represented: diagnostics on virtual text may not opt out of reporting. Text edits additionally require exact
// verbatim geometry regardless of feature participation.
// spanmap.go:41
using Feature = int32_t;
inline constexpr Feature FeatureHover = 1 << 0;
inline constexpr Feature FeatureSignatureHelp = 1 << 1;
inline constexpr Feature FeatureCompletion = 1 << 2;
inline constexpr Feature FeatureDefinition = 1 << 3;
inline constexpr Feature FeatureTypeDefinition = 1 << 4;
inline constexpr Feature FeatureImplementation = 1 << 5;
inline constexpr Feature FeatureReferences = 1 << 6;
inline constexpr Feature FeatureDocumentHighlights = 1 << 7;
inline constexpr Feature FeatureRename = 1 << 8;
inline constexpr Feature FeatureCallHierarchy = 1 << 9;
inline constexpr Feature FeatureCodeActions = 1 << 10;
inline constexpr Feature FeatureFormatting = 1 << 11;
inline constexpr Feature FeatureInlayHints = 1 << 12;
inline constexpr Feature FeatureSemanticTokens = 1 << 13;
inline constexpr Feature FeatureFoldingRanges = 1 << 14;
inline constexpr Feature FeatureSelectionRanges = 1 << 15;
inline constexpr Feature FeatureLinkedEditing = 1 << 16;
inline constexpr Feature FeatureAutoInsert = 1 << 17;
inline constexpr Feature FeatureDocumentSymbols = 1 << 18;
inline constexpr Feature FeatureCodeLens = 1 << 19;
inline constexpr Feature FeatureNone = 0;
inline constexpr Feature FeatureAll = (FeatureCodeLens << 1) - 1;

// Fidelity describes how faithfully a mapped span reflects the original.
// spanmap.go:71. A struct (not an enum) so the Go method syntax
// `fidelity.IsExact()` ports unchanged.
struct Fidelity {
	int32_t v = 0;

	// IsExact reports whether the mapping was fully faithful — the input fell within a single verbatim span —
	// so the result maps 1:1 and can host a text edit written back to the original.
	// spanmap.go:86
	constexpr bool IsExact() const { return v == 0; }

	// IsSingleSegment reports whether the input fell within one segment, verbatim or atom, so the result is a
	// concrete location rather than a best-effort approximation across boundaries or a synthesized gap.
	// spanmap.go:92
	constexpr bool IsSingleSegment() const { return v == 0 || v == 1; }

	// IsNone reports whether the input had no original counterpart, meaning the mapped result is a synthesized
	// gap that does not correspond to any location in the original text.
	// spanmap.go:98
	constexpr bool IsNone() const { return v == 3; }

	constexpr bool operator==(const Fidelity&) const = default;
};

// spanmap.go:74-81
inline constexpr Fidelity FidelityExact{0};       // fell entirely within a single verbatim segment
inline constexpr Fidelity FidelityAtom{1};        // fell within a single atom segment
inline constexpr Fidelity FidelityApproximate{2}; // crossed segment boundaries; endpoints mapped and clamped
inline constexpr Fidelity FidelityNone{3};        // no original counterpart (entirely synthesized)

// Segment maps the half-open virtual range [VirtualStart, VirtualEnd) to the half-open original range
// [OriginalStart, OriginalEnd). Features controls language-service participation; diagnostics and exact edit mapping
// deliberately bypass it.
// spanmap.go:105
struct Segment {
	TextPos VirtualStart;
	TextPos VirtualEnd;
	TextPos OriginalStart;
	TextPos OriginalEnd;
	Kind Kind;
	Feature Features;
};

// MappedPosition is one virtual projection of an original position and its mapping fidelity.
// spanmap.go:115
struct MappedPosition {
	TextPos Position;
	Fidelity Fidelity;

	bool operator==(const MappedPosition&) const = default;
};

// MappedSpan is one virtual projection of an original range and its mapping fidelity.
// spanmap.go:121
struct MappedSpan {
	TextRange Span;
	Fidelity Fidelity;

	bool operator==(const MappedSpan&) const = default;
};

// Validation failures. A content mapper is required to provide a valid span map; these describe the
// ways a map can be malformed, so the compiler can attribute the failure to the mapper precisely and
// point the mapper's author at the offending location.
// spanmap.go:140
using MappingErrorKind = int;
inline constexpr MappingErrorKind MappingErrorKindOverlap = 0;
inline constexpr MappingErrorKind MappingErrorKindOutOfBounds = 1;
inline constexpr MappingErrorKind MappingErrorKindVerbatimMismatch = 2;
inline constexpr MappingErrorKind MappingErrorKindKind = 3;
inline constexpr MappingErrorKind MappingErrorKindFeature = 4;

// MappingError describes a single span map validation failure, including the offsets involved so the mapper's
// author can locate it. VirtualPos is an offset into the virtual text; OriginalPos is an offset into the
// original content. Either may be unused (zero) depending on Kind.
// spanmap.go:159
struct MappingError {
	MappingErrorKind Kind;
	TextPos VirtualPos;
	TextPos OriginalPos;

	// Error describes the invalid mapping and the coordinate at which it was detected.
	// spanmap.go:166
	std::string Error() const;
};

struct originalIndex;

// SpanMap is a sparse, ordered set of segments over a content mapper's virtual text. Segments do not
// need to cover the whole text: any virtual position not inside a segment is synthesized (it has no
// original counterpart). An empty SpanMap therefore describes fully synthesized virtual text.
//
// The Go methods on *SpanMap are nil-tolerant (`m == nil` is legal and handled inside the body), and
// Go callers rely on that — `file.SpanMap().VirtualToOriginalSpan(...)` runs on nil maps throughout
// the diagnostics paths. C++ cannot express that: a member call on nullptr is UB and clang folds
// `this == nullptr` away under -O2 (verified). Instead, every Go method is a free function
// `spanmap::F(m, ...)` taking the receiver as its first parameter; `m->F()` does not compile, so
// every call site is nil-safe by construction, exactly matching Go semantics.
// spanmap.go:129
struct SpanMap {
  private:
	// Go-private fields.
	std::vector<Segment> segments;

	// origOnce guards lazy construction of the interval index used for original-to-virtual lookups.
	std::once_flag origOnce;
	originalIndex* origIndex_ = nullptr;

	// Go-private methods. They run only on non-null receivers (the free functions nil-guard first).
	// spanmap.go:287
	bool virtualSpanSupportsFeature(TextRange r, Feature feature) const;

	// segmentIndexAt returns the index of the segment containing pos and true, or, when pos lies in a gap,
	// the index of the segment immediately before pos (-1 if none) and false.
	// spanmap.go:378
	std::pair<int, bool> segmentIndexAt(TextPos pos) const;

	// insertionPoint returns the original offset where synthesized content following segment prev sits.
	// spanmap.go:394
	TextPos insertionPoint(int prev) const;

	// mapLow maps a virtual lower range boundary to original coordinates.
	// spanmap.go:403
	TextPos mapLow(TextPos pos, int idx, bool in) const;

	// mapHigh maps a virtual upper range boundary to original coordinates.
	// spanmap.go:415
	TextPos mapHigh(TextPos pos, int idx, bool in) const;

	// origIndex builds the immutable original-text interval index on first use.
	// spanmap.go:663
	originalIndex* origIndex();

	// The free functions below are the public API (Go methods, receiver as first arg).
	friend std::optional<MappingError> Validate(const SpanMap*, std::string_view, std::string_view);
	friend std::vector<Segment> Segments(const SpanMap*);
	friend std::pair<TextRange, Fidelity> VirtualToOriginalSpan(const SpanMap*, TextRange);
	friend std::pair<TextRange, Fidelity> VirtualToOriginalSpanForFeature(const SpanMap*, TextRange,
																		Feature);
	friend std::pair<TextPos, Fidelity> VirtualToOriginalPosition(const SpanMap*, TextPos);
	friend std::pair<TextPos, bool> VirtualToOriginalPositionExact(const SpanMap*, TextPos);
	friend std::pair<TextPos, Fidelity> VirtualToOriginalPositionForFeature(const SpanMap*, TextPos,
																		  Feature);
	friend std::pair<Segment, bool> AliasForVirtualSpan(const SpanMap*, TextRange);
	friend std::vector<MappedPosition> OriginalToVirtualPositions(SpanMap*, TextPos, Feature);
	friend std::vector<MappedSpan> OriginalToVirtualSpans(SpanMap*, TextRange, Feature);
	friend std::vector<MappedSpan> OriginalToVirtualIntersectingSpans(SpanMap*, TextRange, Feature);
	friend std::pair<std::string, std::optional<std::string>> Marshal(const SpanMap*);
	friend SpanMap* New(std::vector<Segment>);
};

// Validate enforces the content-mapper span map contract against the virtual and original text.
// It returns the first violation found, or std::nullopt if the map is valid. A nil map is valid.
// spanmap.go:188
std::optional<MappingError> Validate(const SpanMap* m, std::string_view virtualText,
								   std::string_view original);

// Segments returns the map's segments ordered by virtual start (empty for a nil map).
// spanmap.go:231
std::vector<Segment> Segments(const SpanMap* m);

// VirtualToOriginalSpan maps a virtual range to an original range, along with the fidelity of the result.
// A nil map maps identically.
// spanmap.go:241
std::pair<TextRange, Fidelity> VirtualToOriginalSpan(const SpanMap* m, TextRange r);

// VirtualToOriginalSpanForFeature maps r only when every virtual position in the non-empty range is
// covered by contiguous segments participating in feature.
// spanmap.go:279
std::pair<TextRange, Fidelity> VirtualToOriginalSpanForFeature(const SpanMap* m, TextRange r,
															   Feature feature);

// VirtualToOriginalPosition maps a single virtual position to the corresponding original position.
// A nil map maps identically.
// spanmap.go:313
std::pair<TextPos, Fidelity> VirtualToOriginalPosition(const SpanMap* m, TextPos pos);

// VirtualToOriginalPositionExact maps a position only when it is unambiguously in verbatim content.
// spanmap.go:330
std::pair<TextPos, bool> VirtualToOriginalPositionExact(const SpanMap* m, TextPos pos);

// VirtualToOriginalPositionForFeature maps pos only when its virtual segment participates in feature.
// spanmap.go:350
std::pair<TextPos, Fidelity> VirtualToOriginalPositionForFeature(const SpanMap* m, TextPos pos,
																 Feature feature);

// AliasForVirtualSpan returns the alias segment exactly covering r.
// spanmap.go:364
std::pair<Segment, bool> AliasForVirtualSpan(const SpanMap* m, TextRange r);

// OriginalToVirtualPositions returns every virtual projection of an original position whose segment
// participates in feature, ordered by virtual position.
// spanmap.go:431
std::vector<MappedPosition> OriginalToVirtualPositions(SpanMap* m, TextPos pos, Feature feature);

// OriginalToVirtualSpans returns every feature-compatible virtual projection of an original range.
// spanmap.go:482
std::vector<MappedSpan> OriginalToVirtualSpans(SpanMap* m, TextRange r, Feature feature);

// OriginalToVirtualIntersectingSpans maps every feature-enabled segment intersection with r.
// spanmap.go:539
std::vector<MappedSpan> OriginalToVirtualIntersectingSpans(SpanMap* m, TextRange r, Feature feature);

// Marshal encodes a SpanMap into the JSON tuple form. FeatureAll uses the backward-compatible
// five-element tuple; every other feature mask is emitted as a sixth element.
// spanmap.go:803
std::pair<std::string, std::optional<std::string>> Marshal(const SpanMap* m);

// spanmap.go:725
struct segmentGroupAtOriginalPosition {
	std::vector<Segment> segments;
	bool atEnd;
};

// originalIndex stores segments in original-text order and a complete binary tree whose leaves correspond
// to those segments. Each internal node stores the maximum OriginalEnd below it, allowing point lookups to
// discard a whole subtree when none of its segments can reach the queried position.
// spanmap.go:654
struct originalIndex {
	std::vector<Segment> segments;
	int leafCount;
	std::vector<TextPos> maxEnds;

	// segmentsAtOriginalPosition returns every mapping segment containing the original-text position pos.
	// Segment ends are exclusive; a segment start, including a zero-length segment, is considered contained.
	// spanmap.go:693
	std::pair<std::vector<Segment>, bool> segmentsAtOriginalPosition(TextPos pos) const;

	// segmentsEndingAfterPosition returns segments among [0, limit) whose OriginalEnd is greater than pos.
	// spanmap.go:704
	std::vector<Segment> segmentsEndingAfterPosition(int limit, TextPos pos) const;

	// collectSegmentsEndingAtOrAfter walks the flat max-end tree left-to-right, preserving original-text order.
	// spanmap.go:712
	void collectSegmentsEndingAtOrAfter(int node, int start, int end, int limit, TextPos pos,
										bool includeEnd, std::vector<Segment>& results) const;

	// segmentGroupsAtOriginalPosition returns every group of equal-range mapping segments containing or touching
	// the original-text position pos. Segment ends are included for point mapping.
	// spanmap.go:741
	std::vector<segmentGroupAtOriginalPosition> segmentGroupsAtOriginalPosition(TextPos pos) const;
};

// New builds a SpanMap from segments, sorted by virtual start. Segments describe only the parts of the
// virtual text that correspond to the original; anything not covered maps as synthesized.
// spanmap.go:222
SpanMap* New(std::vector<Segment> segments);

// Unmarshal decodes a SpanMap from the JSON tuple form produced by an out-of-process content mapper.
// Five-element tuples omit features and are normalized to FeatureAll; six-element tuples preserve the
// explicit feature mask, including FeatureNone. The second return is the Go `error` (nullopt == nil).
// spanmap.go:776
std::pair<SpanMap*, std::optional<std::string>> Unmarshal(std::string_view data);

} // namespace tsc::spanmap
