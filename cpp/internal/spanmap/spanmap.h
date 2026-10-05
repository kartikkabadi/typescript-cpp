// spanmap.h — dep-stub for tsc/internal/spanmap, owned by the spanmap slice.
// Only the member emitter.cpp calls is declared; the real SpanMap struct and
// the rest of the package land with the spanmap port.
//
// === slice: contentmapper === — the contentmapper port additionally declares
// the surface it consumes (Segment/Kind/Feature, MappingError, New/Unmarshal,
// Segments, Validate); the bodies are dep-implemented in
// cpp/internal/contentmapper/hostimpl.cpp and replaced when the spanmap slice
// lands.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "internal/core/text.h"
#include "internal/gostd/gostd.h"

namespace tsc::spanmap {

// Kind — spanmap.go:22. How positions inside a segment relate the virtual
// span to the original span.
enum class Kind : int32_t {
	// KindVerbatim segments are length-preserving; interior positions map 1:1.
	KindVerbatim = 0,
	// KindAtom maps a virtual span to an original span as a whole.
	KindAtom = 1,
	// KindAlias has atom geometry and asserts the spans name the same entity.
	KindAlias = 2,
};

// Feature — spanmap.go:41. Which language-service operations may use a
// segment.
enum Feature : int32_t {
	FeatureHover = 1 << 0,
	FeatureSignatureHelp = 1 << 1,
	FeatureCompletion = 1 << 2,
	FeatureDefinition = 1 << 3,
	FeatureTypeDefinition = 1 << 4,
	FeatureImplementation = 1 << 5,
	FeatureReferences = 1 << 6,
	FeatureDocumentHighlights = 1 << 7,
	FeatureRename = 1 << 8,
	FeatureCallHierarchy = 1 << 9,
	FeatureCodeActions = 1 << 10,
	FeatureFormatting = 1 << 11,
	FeatureInlayHints = 1 << 12,
	FeatureSemanticTokens = 1 << 13,
	FeatureFoldingRanges = 1 << 14,
	FeatureSelectionRanges = 1 << 15,
	FeatureLinkedEditing = 1 << 16,
	FeatureAutoInsert = 1 << 17,
	FeatureDocumentSymbols = 1 << 18,
	FeatureCodeLens = 1 << 19,
	FeatureNone = 0,
	FeatureAll = (FeatureCodeLens << 1) - 1,
};

inline constexpr int32_t featureMask = FeatureAll;

// Segment — spanmap.go:105.
struct Segment {
	TextPos VirtualStart = 0;
	TextPos VirtualEnd = 0;
	TextPos OriginalStart = 0;
	TextPos OriginalEnd = 0;
	Kind Kind = Kind::KindVerbatim;
	Feature Features = FeatureAll;
};

// MappingErrorKind — spanmap.go:140.
enum class MappingErrorKind : int32_t {
	// segments overlap, run backwards, or extend past the virtual text.
	Overlap = 0,
	// a segment's original span lies outside the original text.
	OutOfBounds = 1,
	// a verbatim segment's virtual and original text differ.
	VerbatimMismatch = 2,
	// a segment uses an unsupported mapping kind.
	Kind = 3,
	// a feature annotation contains unsupported flags.
	Feature = 4,
};

// MappingError — spanmap.go:159. One span map validation failure.
struct MappingError : gostd::ErrObj {
	MappingErrorKind Kind = MappingErrorKind::Overlap;
	TextPos VirtualPos = 0;
	TextPos OriginalPos = 0;

	MappingError() = default;
	MappingError(MappingErrorKind kind, TextPos virtualPos,
	             TextPos originalPos)
	    : Kind(kind), VirtualPos(virtualPos), OriginalPos(originalPos) {}
	std::string Error() const override;
};

// SpanMap — spanmap.go:129. A sparse, ordered set of segments over a content
// mapper's virtual text.
struct SpanMap {
	std::vector<Segment> segments;

	// spanmap.go:330 VirtualToOriginalPositionExact — dep stub, owned by the
	// spanmap slice.
	std::pair<TextPos, bool> VirtualToOriginalPositionExact(
	    TextPos /*pos*/) {
		TSC_UNREACHABLE(
		    "spanmap::SpanMap::VirtualToOriginalPositionExact — spanmap slice");
	}

	// Validate enforces the content-mapper span map contract — spanmap.go:188.
	// Returns the first violation found, or nullptr if the map is valid.
	MappingError* Validate(std::string_view virtualText,
	                       std::string_view originalText);

	// Segments returns the map's segments ordered by virtual start —
	// spanmap.go:231.
	std::vector<Segment> Segments() const { return segments; }
};

// New builds a SpanMap from segments, sorted by virtual start —
// spanmap.go:222.
SpanMap* New(std::vector<Segment> segments);

// Unmarshal decodes a SpanMap from the JSON tuple form produced by an
// out-of-process content mapper — spanmap.go:776.
std::pair<SpanMap*, gostd::Error> Unmarshal(const std::string& data);

} // namespace tsc::spanmap
