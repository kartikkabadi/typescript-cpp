// spanmap.cpp — Port of tsc/internal/spanmap/spanmap.go
//
// Bidirectional span-aware mapping between a content mapper's virtual text
// and its original, untransformed source.
//
// Porting notes:
// - Go `m == nil` receiver checks can't live inside C++ member functions: a
//   member call on nullptr is UB and clang folds `this == nullptr` away under
//   -O2 (verified). Every Go method with a nil guard is therefore a free
//   function `spanmap::F(m, ...)` taking the receiver as its first argument;
//   `m->F()` doesn't compile, so Go call sites like `m.VirtualToOriginalSpan(r)`
//   port to `spanmap::VirtualToOriginalSpan(m, r)` and stay nil-safe by
//   construction, exactly matching Go. Go-private helpers stay member
//   functions; they only run once the nil guard has passed.
// - json.Unmarshal/Marshal for the [][]int32 tuple form are implemented by a
//   file-local minimal JSON reader/writer mirroring encoding/json semantics
//   (packagejson.cpp precedent): whitespace-insensitive, `null` decodes to the
//   zero value, numbers must be integral and fit int32, trailing input errors.

#include "internal/spanmap/spanmap.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>

namespace tsc::spanmap {

namespace {

// featureMask — spanmap.go:68
constexpr Feature featureMask = FeatureAll;

// supportsFeature reports whether segment participates in feature.
// spanmap.go:764
bool supportsFeature(const Segment& segment, Feature feature) {
	return (segment.Features & feature) != 0;
}

// clamp confines v to the inclusive interval [lo, hi].
// spanmap.go:769
constexpr TextPos clamp(TextPos v, TextPos lo, TextPos hi) {
	return std::max(lo, std::min(v, hi));
}

// sameOriginalRange reports whether two segments belong to the same duplicate group.
// spanmap.go:647
bool sameOriginalRange(const Segment& left, const Segment& right) {
	return left.OriginalStart == right.OriginalStart && left.OriginalEnd == right.OriginalEnd;
}

// wrapTextPosAdd — Go int32 addition wraps on overflow; keep that behavior for
// the raw, unvalidated tuple arithmetic in Unmarshal (signed overflow is UB in C++).
constexpr TextPos wrapTextPosAdd(TextPos a, TextPos b) {
	return static_cast<TextPos>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

// sortSearch — Go sort.Search: the smallest index in [0, n) at which f is true
// (f must be monotonic), or n.
template <typename F>
int sortSearch(int n, F f) {
	int lo = 0, hi = n;
	while (lo < hi) {
		int mid = lo + (hi - lo) / 2;
		if (!f(mid)) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	return lo;
}

// originalStartProjections maps the inclusive start of an original range through every matching segment.
// Verbatim segments preserve the offset within the segment; atoms map to their virtual start.
// spanmap.go:585
std::vector<TextPos> originalStartProjections(const std::vector<Segment>& segments, TextPos start,
											  Feature feature) {
	std::vector<TextPos> results;
	results.reserve(segments.size());
	for (const auto& segment : segments) {
		if (!supportsFeature(segment, feature)) {
			continue;
		}
		if (segment.Kind == KindVerbatim) {
			results.push_back(clamp(segment.VirtualStart + (start - segment.OriginalStart),
									segment.VirtualStart, segment.VirtualEnd));
		} else {
			results.push_back(segment.VirtualStart);
		}
	}
	return results;
}

// originalEndProjections maps the exclusive end of an original range through every matching segment.
// Verbatim segments preserve that boundary; atoms map to their virtual end.
// spanmap.go:613
std::vector<TextPos> originalEndProjections(const std::vector<Segment>& segments, TextPos end,
											Feature feature) {
	std::vector<TextPos> results;
	results.reserve(segments.size());
	for (const auto& segment : segments) {
		if (!supportsFeature(segment, feature)) {
			continue;
		}
		if (segment.Kind == KindVerbatim) {
			results.push_back(clamp(segment.VirtualStart + (end - segment.OriginalStart),
									segment.VirtualStart, segment.VirtualEnd));
		} else {
			results.push_back(segment.VirtualEnd);
		}
	}
	return results;
}

// originalToVirtualSpansInSegments maps a range fully contained by each segment.
// spanmap.go:629
std::vector<MappedSpan> originalToVirtualSpansInSegments(const std::vector<Segment>& segments,
														 TextPos start, TextPos end, Feature feature) {
	std::vector<MappedSpan> results;
	results.reserve(segments.size());
	for (const auto& segment : segments) {
		if (!supportsFeature(segment, feature)) {
			continue;
		}
		if (segment.Kind == KindVerbatim) {
			TextPos virtualStart = clamp(segment.VirtualStart + (start - segment.OriginalStart),
										 segment.VirtualStart, segment.VirtualEnd);
			TextPos virtualEnd = clamp(segment.VirtualStart + (end - segment.OriginalStart),
									   virtualStart, segment.VirtualEnd);
			results.push_back(MappedSpan{TextRange{virtualStart, virtualEnd}, FidelityExact});
		} else {
			results.push_back(MappedSpan{TextRange{segment.VirtualStart, segment.VirtualEnd},
										 FidelityAtom});
		}
	}
	return results;
}

// int32TupleParser — minimal JSON decoder for the [][]int32 tuple form produced
// by an out-of-process content mapper (json.Unmarshal). Mirrors encoding/json:
// whitespace is insignificant, `null` decodes to the zero value at any level,
// numbers must be integral and fit int32, and trailing input is an error.
struct int32TupleParser {
	std::string_view s;
	size_t pos = 0;
	std::string err;

	bool fail(std::string msg) {
		err = std::move(msg);
		return false;
	}

	void skipWs() {
		while (pos < s.size() &&
			   (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r')) {
			pos++;
		}
	}

	bool consume(char c) {
		skipWs();
		if (pos < s.size() && s[pos] == c) {
			pos++;
			return true;
		}
		return false;
	}

	bool literal(std::string_view lit) {
		if (s.substr(pos, lit.size()) == lit) {
			pos += lit.size();
			return true;
		}
		return false;
	}

	// parseInt32 reads one JSON value into an int32. `null` yields 0 (Go leaves
	// the zero value); numbers must be integral and within int32 range.
	bool parseInt32(int32_t& out) {
		skipWs();
		if (literal("null")) {
			out = 0;
			return true;
		}
		size_t numStart = pos;
		if (pos < s.size() && s[pos] == '-') {
			pos++;
		}
		bool digits = false;
		while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
			pos++;
			digits = true;
		}
		bool isFloat = false;
		if (pos < s.size() && s[pos] == '.') {
			isFloat = true;
			pos++;
			while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
				pos++;
				digits = true;
			}
		}
		if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
			isFloat = true;
			pos++;
			if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) {
				pos++;
			}
			bool expDigits = false;
			while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
				pos++;
				expDigits = true;
			}
			if (!expDigits) {
				return fail("invalid number literal in span map");
			}
		}
		if (!digits) {
			return fail("cannot unmarshal value into span map segment (expected number)");
		}
		std::string lit(s.substr(numStart, pos - numStart));
		if (!isFloat) {
			errno = 0;
			char* endp = nullptr;
			long long v = std::strtoll(lit.c_str(), &endp, 10);
			if (errno == ERANGE || endp == lit.c_str() || *endp != '\0' ||
				v < INT32_MIN || v > INT32_MAX) {
				return fail("cannot unmarshal number " + lit + " into Go value of type int32");
			}
			out = static_cast<int32_t>(v);
			return true;
		}
		char* endp = nullptr;
		double d = std::strtod(lit.c_str(), &endp);
		if (endp == lit.c_str() || !std::isfinite(d) || std::trunc(d) != d || d < INT32_MIN ||
			d > INT32_MAX) {
			return fail("cannot unmarshal number " + lit + " into Go value of type int32");
		}
		out = static_cast<int32_t>(d);
		return true;
	}

	// parseTuple reads one []int32; `null` yields an empty tuple (which then
	// fails the 5-or-6-length check like Go's nil slice).
	bool parseTuple(std::vector<int32_t>& out) {
		skipWs();
		if (literal("null")) {
			return true;
		}
		if (!consume('[')) {
			return fail("cannot unmarshal value into Go value of type []int32");
		}
		if (consume(']')) {
			return true;
		}
		while (true) {
			int32_t v;
			if (!parseInt32(v)) {
				return false;
			}
			out.push_back(v);
			if (consume(']')) {
				break;
			}
			if (!consume(',')) {
				return fail("expected ',' or ']' after array element");
			}
		}
		return true;
	}

	bool parse(std::vector<std::vector<int32_t>>& out) {
		skipWs();
		if (literal("null")) {
			return true;
		}
		if (!consume('[')) {
			return fail("cannot unmarshal value into Go value of type [][]int32");
		}
		if (!consume(']')) {
			while (true) {
				std::vector<int32_t> t;
				if (!parseTuple(t)) {
					return false;
				}
				out.push_back(std::move(t));
				if (consume(']')) {
					break;
				}
				if (!consume(',')) {
					return fail("expected ',' or ']' after array element");
				}
			}
		}
		skipWs();
		if (pos != s.size()) {
			return fail("invalid character after top-level value");
		}
		return true;
	}
};

} // anonymous namespace

// Error describes the invalid mapping and the coordinate at which it was detected.
// spanmap.go:166
std::string MappingError::Error() const {
	switch (Kind) {
	case MappingErrorKindOverlap:
		return "content mapper position mappings overlap or are out of order near virtual offset " +
			std::to_string(VirtualPos);
	case MappingErrorKindOutOfBounds:
		return "content mapper position mapping points outside the original content at original offset " +
			std::to_string(OriginalPos);
	case MappingErrorKindVerbatimMismatch:
		return "content mapper verbatim mapping does not match the original content at virtual offset " +
			std::to_string(VirtualPos) + ", original offset " + std::to_string(OriginalPos);
	case MappingErrorKindKind:
		return "content mapper position mapping has an invalid kind at virtual offset " +
			std::to_string(VirtualPos);
	case MappingErrorKindFeature:
		return "content mapper position mappings have invalid features near original offset " +
			std::to_string(OriginalPos);
	default:
		return "content mapper produced an invalid position mapping";
	}
}

// Validate enforces the content-mapper span map contract against the virtual and original text: the
// segments must be ordered and disjoint in virtual space and stay within the virtual text, every
// original span must lie within the original text, and every verbatim segment's text must match the
// original exactly. Gaps are allowed (they map as synthesized) and an empty map is valid. It returns the
// first violation found, or nil if the map is valid.
// spanmap.go:188
std::optional<MappingError> Validate(const SpanMap* m, std::string_view virtualText,
								   std::string_view original) {
	if (m == nullptr) {
		return std::nullopt;
	}
	TextPos virtualLen = static_cast<TextPos>(virtualText.size());
	TextPos origLen = static_cast<TextPos>(original.size());
	TextPos previousVirtualEnd = 0;
	for (const auto& s : m->segments) {
		if (s.VirtualStart < previousVirtualEnd || s.VirtualEnd < s.VirtualStart ||
			s.VirtualEnd > virtualLen) {
			return MappingError{MappingErrorKindOverlap, s.VirtualStart, 0};
		}
		previousVirtualEnd = s.VirtualEnd;
		if (s.OriginalStart < 0 || s.OriginalEnd < s.OriginalStart || s.OriginalEnd > origLen) {
			return MappingError{MappingErrorKindOutOfBounds, s.VirtualStart, s.OriginalEnd};
		}
		if (s.Kind != KindVerbatim && s.Kind != KindAtom && s.Kind != KindAlias) {
			return MappingError{MappingErrorKindKind, s.VirtualStart, s.OriginalStart};
		}
		if (s.Kind == KindVerbatim) {
			if (s.VirtualEnd - s.VirtualStart != s.OriginalEnd - s.OriginalStart ||
				virtualText.substr(static_cast<size_t>(s.VirtualStart),
								   static_cast<size_t>(s.VirtualEnd - s.VirtualStart)) !=
					original.substr(static_cast<size_t>(s.OriginalStart),
									static_cast<size_t>(s.OriginalEnd - s.OriginalStart))) {
				return MappingError{MappingErrorKindVerbatimMismatch, s.VirtualStart, s.OriginalStart};
			}
		}
		if ((s.Features & ~featureMask) != 0) {
			return MappingError{MappingErrorKindFeature, s.VirtualStart, s.OriginalStart};
		}
	}
	return std::nullopt;
}

// New builds a SpanMap from segments, sorted by virtual start. Segments describe only the parts of the
// virtual text that correspond to the original; anything not covered maps as synthesized.
// spanmap.go:222
SpanMap* New(std::vector<Segment> segments) {
	std::vector<Segment> sorted = std::move(segments);
	std::sort(sorted.begin(), sorted.end(), [](const Segment& a, const Segment& b) {
		return a.VirtualStart < b.VirtualStart;
	});
	auto* m = new SpanMap();
	m->segments = std::move(sorted);
	return m;
}

// Segments returns the map's segments ordered by virtual start.
// spanmap.go:231
std::vector<Segment> Segments(const SpanMap* m) {
	if (m == nullptr) {
		return {};
	}
	return m->segments;
}

// VirtualToOriginalSpan maps a virtual range to an original range, along with the fidelity of the result. A virtual
// range that lies entirely in a gap between segments (or in an empty map) is synthesized: it maps to the
// insertion point in the original with FidelityNone. A nil SpanMap maps identically.
// spanmap.go:241
std::pair<TextRange, Fidelity> VirtualToOriginalSpan(const SpanMap* m, TextRange r) {
	if (m == nullptr) {
		return {r, FidelityExact};
	}
	TextPos virtualStart = r.pos();
	TextPos virtualEnd = std::max(r.end(), virtualStart);
	if (virtualStart == virtualEnd) {
		auto [position, fidelity] = VirtualToOriginalPosition(m, virtualStart);
		return {TextRange{position, position}, fidelity};
	}

	auto [startIdx, startIn] = m->segmentIndexAt(virtualStart);
	TextPos endProbe = virtualEnd - 1;
	auto [endIdx, endIn] = m->segmentIndexAt(endProbe);

	if (startIdx == endIdx && startIn == endIn) {
		if (startIn) {
			const Segment& seg = m->segments[startIdx];
			if (seg.Kind == KindVerbatim) {
				TextPos origStart =
					clamp(seg.OriginalStart + (virtualStart - seg.VirtualStart), seg.OriginalStart,
						  seg.OriginalEnd);
				TextPos origEnd = clamp(seg.OriginalStart + (virtualEnd - seg.VirtualStart), origStart,
										seg.OriginalEnd);
				return {TextRange{origStart, origEnd}, FidelityExact};
			}
			return {TextRange{seg.OriginalStart, seg.OriginalEnd}, FidelityAtom};
		}
		// Entirely within a single synthesized gap.
		TextPos pos = m->insertionPoint(startIdx);
		return {TextRange{pos, pos}, FidelityNone};
	}

	TextPos origStart = m->mapLow(virtualStart, startIdx, startIn);
	TextPos origEnd = std::max(m->mapHigh(virtualEnd, endIdx, endIn), origStart);
	return {TextRange{origStart, origEnd}, FidelityApproximate};
}

// VirtualToOriginalSpanForFeature maps r only when every virtual position in the non-empty range is
// covered by contiguous segments participating in feature. A zero-length range requires its containing
// segment to participate. Diagnostics and edit write-back intentionally use VirtualToOriginalSpan instead.
// spanmap.go:279
std::pair<TextRange, Fidelity> VirtualToOriginalSpanForFeature(const SpanMap* m, TextRange r,
															   Feature feature) {
	auto [mapped, fidelity] = VirtualToOriginalSpan(m, r);
	if (m == nullptr || m->virtualSpanSupportsFeature(r, feature)) {
		return {mapped, fidelity};
	}
	return {mapped, FidelityNone};
}

// spanmap.go:287
bool SpanMap::virtualSpanSupportsFeature(TextRange r, Feature feature) const {
	TextPos start = r.pos();
	TextPos end = std::max(r.end(), start);
	if (start == end) {
		auto [index, inside] = segmentIndexAt(start);
		return inside && supportsFeature(segments[index], feature);
	}
	auto [index, inside] = segmentIndexAt(start);
	if (!inside) {
		return false;
	}
	TextPos coveredThrough = start;
	while (index < static_cast<int>(segments.size()) && coveredThrough < end) {
		const Segment& segment = segments[index];
		if (segment.VirtualStart > coveredThrough || segment.VirtualEnd <= coveredThrough ||
			!supportsFeature(segment, feature)) {
			return false;
		}
		coveredThrough = segment.VirtualEnd;
		index++;
	}
	return coveredThrough >= end;
}

// VirtualToOriginalPosition maps a single virtual position to the corresponding original position, along with the
// fidelity of the result. It is the single-position analog of VirtualToOriginalSpan: a position in a gap (or in an empty
// map) is synthesized and maps to the insertion point with FidelityNone. A nil SpanMap maps identically.
// spanmap.go:313
std::pair<TextPos, Fidelity> VirtualToOriginalPosition(const SpanMap* m, TextPos pos) {
	if (m == nullptr) {
		return {pos, FidelityExact};
	}
	auto [idx, in] = m->segmentIndexAt(pos);
	if (!in) {
		return {m->insertionPoint(idx), FidelityNone};
	}
	const Segment& seg = m->segments[idx];
	if (seg.Kind == KindVerbatim) {
		return {clamp(seg.OriginalStart + (pos - seg.VirtualStart), seg.OriginalStart, seg.OriginalEnd),
				FidelityExact};
	}
	return {seg.OriginalStart, FidelityAtom};
}

// VirtualToOriginalPositionExact maps a position only when it is unambiguously in verbatim content.
// A boundary touching an atom is rejected because the same virtual position can describe either side.
// spanmap.go:330
std::pair<TextPos, bool> VirtualToOriginalPositionExact(const SpanMap* m, TextPos pos) {
	auto [mapped, fidelity] = VirtualToOriginalPosition(m, pos);
	if (fidelity != FidelityExact || m == nullptr) {
		return {mapped, fidelity == FidelityExact};
	}
	auto [index, inside] = m->segmentIndexAt(pos);
	if (!inside || m->segments[index].Kind != KindVerbatim) {
		return {mapped, false};
	}
	if (index > 0) {
		const Segment& previous = m->segments[index - 1];
		if (previous.VirtualEnd == pos &&
			(previous.Kind != KindVerbatim || previous.OriginalEnd != m->segments[index].OriginalStart)) {
			return {mapped, false};
		}
	}
	return {mapped, true};
}

// VirtualToOriginalPositionForFeature maps pos only when its virtual segment participates in feature.
// Diagnostics and edit write-back intentionally use VirtualToOriginalPosition instead.
// spanmap.go:350
std::pair<TextPos, Fidelity> VirtualToOriginalPositionForFeature(const SpanMap* m, TextPos pos,
																		Feature feature) {
	auto [mapped, fidelity] = VirtualToOriginalPosition(m, pos);
	if (m == nullptr) {
		return {mapped, fidelity};
	}
	auto [index, inside] = m->segmentIndexAt(pos);
	if (!inside || !supportsFeature(m->segments[index], feature)) {
		return {mapped, FidelityNone};
	}
	return {mapped, fidelity};
}

// AliasForVirtualSpan returns the alias segment exactly covering r. Partial overlap does not qualify:
// diagnostic text may be substituted only when the diagnostic identifies the complete virtual alias.
// spanmap.go:364
std::pair<Segment, bool> AliasForVirtualSpan(const SpanMap* m, TextRange r) {
	if (m == nullptr) {
		return {Segment{}, false};
	}
	auto [index, inside] = m->segmentIndexAt(r.pos());
	if (!inside) {
		return {Segment{}, false};
	}
	const Segment& segment = m->segments[index];
	return {segment, segment.Kind == KindAlias && r.pos() == segment.VirtualStart &&
						r.end() == segment.VirtualEnd};
}

// segmentIndexAt returns the index of the segment containing pos and true, or, when pos lies in a gap,
// the index of the segment immediately before pos (-1 if none) and false.
// spanmap.go:378
std::pair<int, bool> SpanMap::segmentIndexAt(TextPos pos) const {
	auto it = std::lower_bound(segments.begin(), segments.end(), pos,
							   [](const Segment& s, TextPos p) { return s.VirtualStart < p; });
	int idx = static_cast<int>(it - segments.begin());
	if (it != segments.end() && it->VirtualStart == pos) {
		return {idx, true};
	}
	int prev = idx - 1;
	if (prev >= 0 && (pos < segments[prev].VirtualEnd ||
					  (prev == static_cast<int>(segments.size()) - 1 &&
					   pos == segments[prev].VirtualEnd))) {
		return {prev, true};
	}
	return {prev, false};
}

// insertionPoint returns the original offset where synthesized content following segment prev sits: the
// original end of that segment, or 0 before the first segment.
// spanmap.go:394
TextPos SpanMap::insertionPoint(int prev) const {
	if (prev < 0) {
		return 0;
	}
	return segments[prev].OriginalEnd;
}

// mapLow maps a virtual lower range boundary to original coordinates. A boundary in a synthesized
// gap uses that gap's insertion point; an atom uses its original start.
// spanmap.go:403
TextPos SpanMap::mapLow(TextPos pos, int idx, bool in) const {
	if (!in) {
		return insertionPoint(idx);
	}
	const Segment& seg = segments[idx];
	if (seg.Kind == KindVerbatim) {
		return clamp(seg.OriginalStart + (pos - seg.VirtualStart), seg.OriginalStart, seg.OriginalEnd);
	}
	return seg.OriginalStart;
}

// mapHigh maps a virtual upper range boundary to original coordinates. A boundary in a synthesized
// gap uses that gap's insertion point; an atom uses its original end.
// spanmap.go:415
TextPos SpanMap::mapHigh(TextPos pos, int idx, bool in) const {
	if (!in) {
		return insertionPoint(idx);
	}
	const Segment& seg = segments[idx];
	if (seg.Kind == KindVerbatim) {
		return clamp(seg.OriginalStart + (pos - seg.VirtualStart), seg.OriginalStart, seg.OriginalEnd);
	}
	return seg.OriginalEnd;
}

// OriginalToVirtualPositions returns every virtual projection of an original position whose segment
// participates in feature. Segment ends are inclusive for point mapping, so a position shared by adjacent
// original spans returns projections from both sides. Results are ordered by virtual position. It returns
// no results for an uncovered position or when all touching segments reject feature. A nil SpanMap maps identically.
// spanmap.go:431
std::vector<MappedPosition> OriginalToVirtualPositions(SpanMap* m, TextPos pos, Feature feature) {
	if (m == nullptr) {
		return {MappedPosition{pos, FidelityExact}};
	}
	auto groups = m->origIndex()->segmentGroupsAtOriginalPosition(pos);
	if (groups.empty()) {
		return {};
	}
	std::vector<MappedPosition> results;
	for (const auto& group : groups) {
		for (const auto& segment : group.segments) {
			if (!supportsFeature(segment, feature)) {
				continue;
			}
			MappedPosition mapped{0, FidelityAtom};
			if (segment.Kind == KindVerbatim) {
				mapped.Position = clamp(segment.VirtualStart + (pos - segment.OriginalStart),
										segment.VirtualStart, segment.VirtualEnd);
				mapped.Fidelity = FidelityExact;
			} else if (group.atEnd) {
				mapped.Position = segment.VirtualEnd;
			} else {
				mapped.Position = segment.VirtualStart;
			}
			if (std::find(results.begin(), results.end(), mapped) == results.end()) {
				results.push_back(mapped);
			}
		}
	}
	std::sort(results.begin(), results.end(), [](const MappedPosition& a, const MappedPosition& b) {
		return a.Position < b.Position;
	});
	return results;
}

// OriginalToVirtualSpans returns every feature-compatible virtual projection of an original range.
// A range contained by one or more segments produces one exact or atom result per matching segment.
//
// A range that starts in one group and ends in another can have several possible virtual ranges. For
// example, suppose two original segments are each copied twice into the virtual text:
//
//	original:   [ A ][ B ]
//	               [---)       range from inside A to inside B
//
//	virtual:    [ A ][ B ]      [ A ][ B ]
//	               ^   ^          ^   ^
//	             start end      start end
//	               1   3          11  13
//
// The map says that the range may start at 1 or 11 and end at 3 or 13, but it does not say which copy of A
// belongs with which copy of B. We choose the smallest range around each possible location, producing [1,3)
// and [11,13). We do not return [1,13), because it contains both smaller candidates and would include code
// that may be unrelated to the original range. These cross-group results have approximate fidelity.
// If either boundary is uncovered or disabled for feature, there are no results. A nil SpanMap maps identically.
// spanmap.go:482
std::vector<MappedSpan> OriginalToVirtualSpans(SpanMap* m, TextRange r, Feature feature) {
	if (m == nullptr) {
		return {MappedSpan{r, FidelityExact}};
	}
	TextPos start = r.pos();
	TextPos end = std::max(r.end(), start);
	if (start == end) {
		// core.Map(m.OriginalToVirtualPositions(start, feature), ...)
		auto positions = OriginalToVirtualPositions(m, start, feature);
		std::vector<MappedSpan> spans;
		spans.reserve(positions.size());
		for (const auto& position : positions) {
			spans.push_back(
				MappedSpan{TextRange{position.Position, position.Position}, position.Fidelity});
		}
		return spans;
	}
	TextPos lastCharacter = end - 1;
	originalIndex* index = m->origIndex();
	auto [startSegments, startInside] = index->segmentsAtOriginalPosition(start);
	auto [endSegments, endInside] = index->segmentsAtOriginalPosition(lastCharacter);
	if (!startInside || !endInside) {
		return {};
	}
	std::vector<Segment> containing;
	for (const auto& segment : startSegments) {
		if (end <= segment.OriginalEnd) {
			containing.push_back(segment);
		}
	}
	if (!containing.empty()) {
		std::vector<MappedSpan> results =
			originalToVirtualSpansInSegments(containing, start, end, feature);
		if (!results.empty()) {
			std::sort(results.begin(), results.end(), [](const MappedSpan& a, const MappedSpan& b) {
				return a.Span.pos() < b.Span.pos();
			});
			return results;
		}
	}
	std::vector<TextPos> starts = originalStartProjections(startSegments, start, feature);
	std::vector<TextPos> ends = originalEndProjections(endSegments, end, feature);
	if (starts.empty() || ends.empty()) {
		return {};
	}
	std::sort(starts.begin(), starts.end());
	std::sort(ends.begin(), ends.end());
	std::vector<MappedSpan> results;
	results.reserve(std::min(starts.size(), ends.size()));
	for (size_t i = 0; i < starts.size(); i++) {
		TextPos virtualStart = starts[i];
		auto endIt = std::lower_bound(ends.begin(), ends.end(), virtualStart);
		size_t endIndex = static_cast<size_t>(endIt - ends.begin());
		if (endIndex == ends.size() || (i + 1 < starts.size() && starts[i + 1] <= ends[endIndex])) {
			continue;
		}
		results.push_back(
			MappedSpan{TextRange{virtualStart, ends[endIndex]}, FidelityApproximate});
	}
	return results;
}

// OriginalToVirtualIntersectingSpans maps every feature-enabled segment intersection with r.
// Unlike OriginalToVirtualSpans, uncovered range endpoints do not suppress covered interior segments.
// spanmap.go:539
std::vector<MappedSpan> OriginalToVirtualIntersectingSpans(SpanMap* m, TextRange r, Feature feature) {
	if (m == nullptr) {
		return {MappedSpan{r, FidelityExact}};
	}
	if (r.pos() == r.end()) {
		return OriginalToVirtualSpans(m, r, feature);
	}
	std::vector<MappedSpan> results;
	for (const auto& segment : m->segments) {
		if (!supportsFeature(segment, feature)) {
			continue;
		}
		TextPos start = std::max(r.pos(), segment.OriginalStart);
		TextPos end = std::min(r.end(), segment.OriginalEnd);
		if (start >= end) {
			continue;
		}
		if (segment.Kind == KindVerbatim) {
			results.push_back(MappedSpan{
				TextRange{static_cast<TextPos>(segment.VirtualStart + (start - segment.OriginalStart)),
						  static_cast<TextPos>(segment.VirtualStart + (end - segment.OriginalStart))},
				FidelityExact});
		} else {
			results.push_back(
				MappedSpan{TextRange{segment.VirtualStart, segment.VirtualEnd}, FidelityAtom});
		}
	}
	return results;
}

// origIndex builds the immutable original-text interval index on first use. Sorting dominates the O(n) tree
// construction, so the first lookup remains O(n log n); later point lookups visit only tree branches that can
// contain a match.
// spanmap.go:663
originalIndex* SpanMap::origIndex() {
	std::call_once(origOnce, [this] {
		std::vector<Segment> sorted = segments;
		std::sort(sorted.begin(), sorted.end(), [](const Segment& a, const Segment& b) {
			if (a.OriginalStart != b.OriginalStart) {
				return a.OriginalStart < b.OriginalStart;
			}
			if (a.OriginalEnd != b.OriginalEnd) {
				return a.OriginalEnd < b.OriginalEnd;
			}
			return a.VirtualStart < b.VirtualStart;
		});
		int leafCount = 1;
		while (leafCount < static_cast<int>(sorted.size())) {
			leafCount *= 2;
		}
		std::vector<TextPos> maxEnds(2 * static_cast<size_t>(leafCount));
		for (size_t i = 0; i < sorted.size(); i++) {
			maxEnds[static_cast<size_t>(leafCount) + i] = sorted[i].OriginalEnd;
		}
		for (int i = leafCount - 1; i > 0; i--) {
			maxEnds[i] = std::max(maxEnds[2 * i], maxEnds[2 * i + 1]);
		}
		origIndex_ = new originalIndex{std::move(sorted), leafCount, std::move(maxEnds)};
	});
	return origIndex_;
}

// segmentsAtOriginalPosition returns every mapping segment containing the original-text position pos.
// Segment ends are exclusive; a segment start, including a zero-length segment, is considered contained.
// spanmap.go:693
std::pair<std::vector<Segment>, bool> originalIndex::segmentsAtOriginalPosition(TextPos pos) const {
	// Query intervals that contain pos strictly before their exclusive end. Segments starting exactly at pos
	// are appended separately so zero-length segments are included without preventing maxEnd <= pos pruning.
	int n = static_cast<int>(segments.size());
	int start = sortSearch(n, [this, pos](int index) { return segments[index].OriginalStart >= pos; });
	std::vector<Segment> results = segmentsEndingAfterPosition(start, pos);
	int end = sortSearch(n, [this, pos](int index) { return segments[index].OriginalStart > pos; });
	results.insert(results.end(), segments.begin() + start, segments.begin() + end);
	return {results, !results.empty()};
}

// segmentsEndingAfterPosition returns segments among [0, limit) whose OriginalEnd is greater than pos.
// spanmap.go:704
std::vector<Segment> originalIndex::segmentsEndingAfterPosition(int limit, TextPos pos) const {
	std::vector<Segment> results;
	collectSegmentsEndingAtOrAfter(1, 0, leafCount, limit, pos, false, results);
	return results;
}

// collectSegmentsEndingAtOrAfter walks the flat max-end tree left-to-right, preserving original-text order.
// Nodes beyond limit or whose maximum end cannot reach pos are discarded without visiting their leaves.
// spanmap.go:712
void originalIndex::collectSegmentsEndingAtOrAfter(int node, int start, int end, int limit, TextPos pos,
												   bool includeEnd,
												   std::vector<Segment>& results) const {
	if (start >= limit || maxEnds[node] < pos || (!includeEnd && maxEnds[node] == pos)) {
		return;
	}
	if (end - start == 1) {
		results.push_back(segments[start]);
		return;
	}
	int middle = start + (end - start) / 2;
	collectSegmentsEndingAtOrAfter(2 * node, start, middle, limit, pos, includeEnd, results);
	collectSegmentsEndingAtOrAfter(2 * node + 1, middle, end, limit, pos, includeEnd, results);
}

// segmentGroupsAtOriginalPosition returns every group of equal-range mapping segments containing or touching
// the original-text position pos. Segment ends are included for point mapping.
//
// At a shared boundary, segments ending at pos and segments starting there form separate groups:
//
//	original:  [--- A ---)[--- B ---)
//	                      ^ pos
//
//	virtual:   [ A1 ) [ A2 )    [ B1 ) [ B2 )
//	             left group       right group
//	             atEnd: true      atEnd: false
// spanmap.go:741
std::vector<segmentGroupAtOriginalPosition>
originalIndex::segmentGroupsAtOriginalPosition(TextPos pos) const {
	int limit = sortSearch(static_cast<int>(segments.size()),
						   [this, pos](int index) { return segments[index].OriginalStart > pos; });
	std::vector<Segment> segs;
	collectSegmentsEndingAtOrAfter(1, 0, leafCount, limit, pos, true, segs);
	std::vector<segmentGroupAtOriginalPosition> groups;
	for (size_t start = 0; start < segs.size();) {
		size_t end = start + 1;
		while (end < segs.size() && sameOriginalRange(segs[start], segs[end])) {
			end++;
		}
		const Segment& segment = segs[start];
		if (pos <= segment.OriginalEnd) {
			groups.push_back(segmentGroupAtOriginalPosition{
				std::vector<Segment>(segs.begin() + start, segs.begin() + end),
				pos == segment.OriginalEnd && pos != segment.OriginalStart});
		}
		start = end;
	}
	return groups;
}

// Unmarshal decodes a SpanMap from the JSON tuple form produced by an out-of-process content mapper.
// Five-element tuples omit features and are normalized to FeatureAll; six-element tuples preserve the
// explicit feature mask, including FeatureNone.
// spanmap.go:776
std::pair<SpanMap*, std::optional<std::string>> Unmarshal(std::string_view data) {
	std::vector<std::vector<int32_t>> tuples;
	{
		int32TupleParser parser;
		parser.s = data;
		if (!parser.parse(tuples)) {
			return {nullptr, std::move(parser.err)};
		}
	}
	std::vector<Segment> segments(tuples.size());
	for (size_t i = 0; i < tuples.size(); i++) {
		const auto& t = tuples[i];
		if (t.size() != 5 && t.size() != 6) {
			return {nullptr, "span map segment " + std::to_string(i) +
								": expected 5 or 6 values, got " + std::to_string(t.size())};
		}
		segments[i] = Segment{
			TextPos(t[0]),
			wrapTextPosAdd(t[0], t[1]),
			TextPos(t[2]),
			wrapTextPosAdd(t[2], t[3]),
			Kind(t[4]),
			FeatureAll,
		};
		if (t.size() == 6) {
			segments[i].Features = Feature(t[5]);
		}
	}
	return {New(std::move(segments)), std::nullopt};
}

// Marshal encodes a SpanMap into the JSON tuple form. FeatureAll uses the backward-compatible five-element
// tuple; every other feature mask is emitted as a sixth element.
// spanmap.go:803
std::pair<std::string, std::optional<std::string>> Marshal(const SpanMap* m) {
	// json.Marshal of [][]int32 cannot fail — the error return stays faithful to the Go signature.
	std::string out;
	out += '[';
	for (size_t i = 0; i < m->segments.size(); i++) {
		const Segment& s = m->segments[i];
		if (i != 0) {
			out += ',';
		}
		out += '[';
		out += std::to_string(s.VirtualStart);
		out += ',';
		out += std::to_string(s.VirtualEnd - s.VirtualStart);
		out += ',';
		out += std::to_string(s.OriginalStart);
		out += ',';
		out += std::to_string(s.OriginalEnd - s.OriginalStart);
		out += ',';
		out += std::to_string(int32_t(s.Kind));
		if (s.Features != FeatureAll) {
			out += ',';
			out += std::to_string(s.Features);
		}
		out += ']';
	}
	out += ']';
	return {out, std::nullopt};
}

} // namespace tsc::spanmap
