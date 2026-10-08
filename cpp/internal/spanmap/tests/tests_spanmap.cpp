// tests_spanmap.cpp — port of tsc/internal/spanmap/spanmap_test.go.
#include <optional>
#include <string>
#include <vector>

#include "internal/core/text.h"
#include "internal/gostd/testing.h"
#include "internal/spanmap/spanmap.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
using namespace tsc;
using tsc::spanmap::Fidelity;
using tsc::spanmap::Feature;
using tsc::spanmap::MappedPosition;
using tsc::spanmap::MappedSpan;
using tsc::spanmap::Segment;
using tsc::spanmap::SpanMap;

namespace {

void TestVirtualToOriginalSpanVerbatim(T* t) {
	t->Parallel();

	// Virtual [0,10) is a verbatim copy of original [100,110).
	auto* m = spanmap::New(std::vector<Segment>{
	    {0, 10, 100, 110, spanmap::KindVerbatim, spanmap::FeatureAll},
	});

	auto [got, fidelity] = spanmap::VirtualToOriginalSpan(m, TextRange{3, 7});
	assert::Equal(t, got.pos(), 103);
	assert::Equal(t, got.end(), 107);
	assert::Equal(t, fidelity, spanmap::FidelityExact);
}

void TestVirtualToOriginalSpanAtom(T* t) {
	t->Parallel();

	// Virtual [0,3) is a synthesized gap; [3,14) ("MyComponent") is an atom of
	// the original [60,71).
	auto* m = spanmap::New(std::vector<Segment>{
	    {3, 14, 60, 71, spanmap::KindAtom, spanmap::FeatureAll},
	});

	// A span inside the atom maps to the whole atom span.
	auto [got, fidelity] = spanmap::VirtualToOriginalSpan(m, TextRange{5, 9});
	assert::Equal(t, got.pos(), 60);
	assert::Equal(t, got.end(), 71);
	assert::Equal(t, fidelity, spanmap::FidelityAtom);
}

void TestVirtualAlias(T* t) {
	t->Parallel();

	auto* m = spanmap::New(std::vector<Segment>{
	    {3, 6, 10, 11, spanmap::KindAlias, spanmap::FeatureAll},
	});

	auto [got, fidelity] = spanmap::VirtualToOriginalSpan(m, TextRange{3, 6});
	assert::Equal(t, got, TextRange{10, 11});
	assert::Equal(t, fidelity, spanmap::FidelityAtom);
	auto [alias, ok] = spanmap::AliasForVirtualSpan(m, TextRange{3, 6});
	assert::Assert(t, ok);
	assert::Equal(t, alias.Kind, spanmap::KindAlias);
	auto [a2, partial] = spanmap::AliasForVirtualSpan(m, TextRange{4, 6});
	assert::Assert(t, !partial);

	auto [data, err] = spanmap::Marshal(m);
	assert::Assert(t, !err.has_value());
	auto [decoded, derr] = spanmap::Unmarshal(data);
	assert::Assert(t, !derr.has_value());
	assert::Equal(t, spanmap::Segments(decoded)[0].Kind,
	              spanmap::KindAlias);
}

void TestVirtualToOriginalSpanSynthesizedGap(T* t) {
	t->Parallel();

	// A gap between two verbatim segments is synthesized: it maps to the
	// insertion point (the preceding segment's original end) with no
	// fidelity.
	auto* m = spanmap::New(std::vector<Segment>{
	    {0, 10, 100, 110, spanmap::KindVerbatim, spanmap::FeatureAll},
	    {20, 30, 200, 210, spanmap::KindVerbatim, spanmap::FeatureAll},
	});

	auto [got, fidelity] = spanmap::VirtualToOriginalSpan(m, TextRange{12, 15});
	assert::Equal(t, got.pos(), 110);
	assert::Equal(t, got.end(), 110);
	assert::Equal(t, fidelity, spanmap::FidelityNone);
}

void TestOriginalToVirtualIntersectingSpansAllowsUncoveredEndpoints(T* t) {
	t->Parallel();
	auto* m = spanmap::New(std::vector<Segment>{
	    {10, 20, 100, 110, spanmap::KindVerbatim,
	     spanmap::FeatureSemanticTokens | spanmap::FeatureInlayHints},
	});

	for (Feature feature : {spanmap::FeatureSemanticTokens,
	                        spanmap::FeatureInlayHints}) {
		auto got = spanmap::OriginalToVirtualIntersectingSpans(
		    m, TextRange{90, 120}, feature);
		assert::Equal(t, (int)got.size(), 1);
		assert::Equal(t, got[0].Span, TextRange{10, 20});
		assert::Equal(t, got[0].Fidelity, spanmap::FidelityExact);
	}
}

void TestVirtualToOriginalSpanEmptyIsSynthesized(T* t) {
	t->Parallel();

	// An empty map describes fully synthesized output: everything maps to
	// the start with no fidelity.
	auto* m = spanmap::New({});
	auto [got, fidelity] = spanmap::VirtualToOriginalSpan(m, TextRange{5, 10});
	assert::Equal(t, got.pos(), 0);
	assert::Equal(t, got.end(), 0);
	assert::Equal(t, fidelity, spanmap::FidelityNone);
}

void TestVirtualToOriginalSpanCrossingSegments(T* t) {
	t->Parallel();

	auto* m = spanmap::New(std::vector<Segment>{
	    {0, 10, 100, 110, spanmap::KindVerbatim},
	    {10, 20, 200, 210, spanmap::KindVerbatim},
	});

	auto [got, fidelity] = spanmap::VirtualToOriginalSpan(m, TextRange{5, 15});
	assert::Equal(t, got.pos(), 105);
	assert::Equal(t, got.end(), 205);
	assert::Equal(t, fidelity, spanmap::FidelityApproximate);
}

void TestVirtualToOriginalSpanNilIdentity(T* t) {
	t->Parallel();

	SpanMap* m = nullptr;
	auto [got, fidelity] = spanmap::VirtualToOriginalSpan(m, TextRange{3, 7});
	assert::Equal(t, got.pos(), 3);
	assert::Equal(t, got.end(), 7);
	assert::Equal(t, fidelity, spanmap::FidelityExact);
}

void TestVirtualToOriginalPosition(T* t) {
	t->Parallel();

	// Virtual [0,10) is a verbatim copy of original [100,110); [10,20) is an
	// atom of original [200,210).
	auto* m = spanmap::New(std::vector<Segment>{
	    {0, 10, 100, 110, spanmap::KindVerbatim, spanmap::FeatureAll},
	    {20, 30, 200, 210, spanmap::KindAtom, spanmap::FeatureAll},
	});

	struct TestCase {
		std::string name;
		TextPos pos;
		TextPos want;
		Fidelity fidelity;
	};
	std::vector<TestCase> testCases{
	    {"verbatim interpolates", 3, 103, spanmap::FidelityExact},
	    {"atom maps to its start", 25, 200, spanmap::FidelityAtom},
	    {"gap maps to insertion point", 15, 110, spanmap::FidelityNone},
	};
	for (const auto& tc : testCases) {
		auto rec = tc;
		t->Run(tc.name, [m, rec](T* t) {
			t->Parallel();
			auto [got, fidelity] =
			    spanmap::VirtualToOriginalPosition(m, rec.pos);
			assert::Equal(t, got, rec.want);
			assert::Equal(t, fidelity, rec.fidelity);
			// VirtualToOriginalPosition must agree with
			// VirtualToOriginalSpan on a zero-length range.
			auto [span, spanFidelity] = spanmap::VirtualToOriginalSpan(
			    m, TextRange{(int)rec.pos, (int)rec.pos});
			assert::Equal(t, got, TextPos(span.pos()));
			assert::Equal(t, fidelity, spanFidelity);
		});
	}
}

void TestVirtualToOriginalPositionExact(T* t) {
	t->Parallel();

	auto* m = spanmap::New(std::vector<Segment>{
	    {0, 10, 100, 110, spanmap::KindVerbatim, spanmap::FeatureAll},
	    {10, 20, 110, 120, spanmap::KindAtom, spanmap::FeatureAll},
	    {20, 30, 120, 130, spanmap::KindVerbatim, spanmap::FeatureAll},
	});

	struct TestCase {
		TextPos pos;
		TextPos want;
		bool ok;
	};
	for (const auto& test : std::vector<TestCase>{
	         {5, 105, true},
	         {10, 110, false},
	         {15, 110, false},
	         {20, 120, false},
	         {25, 125, true},
	     }) {
		auto [got, ok] = spanmap::VirtualToOriginalPositionExact(m, test.pos);
		assert::Equal(t, got, test.want);
		assert::Equal(t, ok, test.ok);
	}
}

void TestVirtualToOriginalPositionExactRejectsDiscontinuousBoundary(T* t) {
	t->Parallel();

	auto* m = spanmap::New(std::vector<Segment>{
	    {0, 10, 0, 10, spanmap::KindVerbatim},
	    {10, 20, 100, 110, spanmap::KindVerbatim},
	});

	auto [mapped, ok] = spanmap::VirtualToOriginalPositionExact(m, 10);
	assert::Equal(t, mapped, TextPos(100));
	assert::Assert(t, !ok);
}

void TestZeroLengthSpansAtSegmentEnds(T* t) {
	t->Parallel();

	auto* m = spanmap::New(std::vector<Segment>{
	    {0, 10, 100, 110, spanmap::KindVerbatim, spanmap::FeatureHover},
	    {20, 30, 200, 210, spanmap::KindVerbatim, spanmap::FeatureHover},
	});

	auto [position, fidelity] = spanmap::VirtualToOriginalPosition(m, 30);
	assert::Equal(t, position, TextPos(210));
	assert::Equal(t, fidelity, spanmap::FidelityExact);
	auto [virtualSpan, sfidelity] =
	    spanmap::VirtualToOriginalSpan(m, TextRange{30, 30});
	assert::Equal(t, virtualSpan, TextRange{210, 210});
	assert::Equal(t, sfidelity, spanmap::FidelityExact);

	struct TestCase {
		std::string name;
		int originalEnd;
	};
	for (const auto& test :
	     std::vector<TestCase>{{"before gap", 110}, {"final", 210}}) {
		auto rec = test;
		t->Run(test.name, [m, rec](T* t) {
			t->Parallel();
			auto positions = spanmap::OriginalToVirtualPositions(
			    m, TextPos(rec.originalEnd), spanmap::FeatureHover);
			auto spans = spanmap::OriginalToVirtualSpans(
			    m, TextRange{rec.originalEnd, rec.originalEnd},
			    spanmap::FeatureHover);
			assert::Equal(t, (int)positions.size(), 1);
			assert::Equal(t, (int)spans.size(), 1);
			assert::Equal(t, spans[0].Span,
			              TextRange{(int)positions[0].Position,
			                        (int)positions[0].Position});
			assert::Equal(t, spans[0].Fidelity, positions[0].Fidelity);
		});
	}
}

void TestMapPositionNilIdentity(T* t) {
	t->Parallel();

	SpanMap* m = nullptr;
	auto [got, fidelity] = spanmap::VirtualToOriginalPosition(m, 7);
	assert::Equal(t, got, TextPos(7));
	assert::Equal(t, fidelity, spanmap::FidelityExact);
}

void TestOriginalToVirtualSpanVerbatim(T* t) {
	t->Parallel();

	// Virtual [0,10) is a verbatim copy of original [100,110).
	auto* m = spanmap::New(std::vector<Segment>{
	    {0, 10, 100, 110, spanmap::KindVerbatim, spanmap::FeatureAll},
	});

	auto results =
	    spanmap::OriginalToVirtualSpans(m, TextRange{103, 107},
	                                    spanmap::FeatureAll);
	assert::Equal(t, (int)results.size(), 1);
	assert::Equal(t, results[0].Span.pos(), 3);
	assert::Equal(t, results[0].Span.end(), 7);
	assert::Equal(t, results[0].Fidelity, spanmap::FidelityExact);
}

void TestOriginalToVirtualSpanAtom(T* t) {
	t->Parallel();

	// Virtual [3,14) is an atom of the original [60,71).
	auto* m = spanmap::New(std::vector<Segment>{
	    {3, 14, 60, 71, spanmap::KindAtom, spanmap::FeatureAll},
	});

	// A span inside the original atom maps to the whole virtual span.
	auto results =
	    spanmap::OriginalToVirtualSpans(m, TextRange{63, 67},
	                                    spanmap::FeatureAll);
	assert::Equal(t, (int)results.size(), 1);
	assert::Equal(t, results[0].Span.pos(), 3);
	assert::Equal(t, results[0].Span.end(), 14);
	assert::Equal(t, results[0].Fidelity, spanmap::FidelityAtom);
}

void TestOriginalToVirtualSpanGap(T* t) {
	t->Parallel();

	// An original range with no covering segment has no virtual counterpart.
	auto* m = spanmap::New(std::vector<Segment>{
	    {0, 10, 100, 110, spanmap::KindVerbatim, spanmap::FeatureAll},
	    {20, 30, 200, 210, spanmap::KindVerbatim, spanmap::FeatureAll},
	});

	assert::Equal(t,
	              (int)spanmap::OriginalToVirtualSpans(
	                  m, TextRange{150, 160}, spanmap::FeatureAll)
	                  .size(),
	              0);
}

void TestOriginalToVirtualSpanNilIdentity(T* t) {
	t->Parallel();

	SpanMap* m = nullptr;
	auto results =
	    spanmap::OriginalToVirtualSpans(m, TextRange{3, 7},
	                                    spanmap::FeatureAll);
	assert::Equal(t, (int)results.size(), 1);
	assert::Equal(t, results[0].Span.pos(), 3);
	assert::Equal(t, results[0].Span.end(), 7);
	assert::Equal(t, results[0].Fidelity, spanmap::FidelityExact);
}

void TestOriginalToVirtualPositions(T* t) {
	t->Parallel();

	// Original [100,110) is a verbatim copy of virtual [0,10); [200,210) is
	// an atom of virtual [20,30).
	auto* m = spanmap::New(std::vector<Segment>{
	    {0, 10, 100, 110, spanmap::KindVerbatim, spanmap::FeatureAll},
	    {20, 30, 200, 210, spanmap::KindAtom, spanmap::FeatureAll},
	});

	struct TestCase {
		std::string name;
		TextPos pos;
		TextPos want;
		Fidelity fidelity;
	};
	std::vector<TestCase> testCases{
	    {"verbatim interpolates", 103, 3, spanmap::FidelityExact},
	    {"atom maps to its start", 205, 20, spanmap::FidelityAtom},
	    {"gap has no projection", 150, 0, spanmap::FidelityNone},
	};
	for (const auto& tc : testCases) {
		auto rec = tc;
		t->Run(tc.name, [m, rec](T* t) {
			t->Parallel();
			auto positions = spanmap::OriginalToVirtualPositions(
			    m, rec.pos, spanmap::FeatureAll);
			auto spans = spanmap::OriginalToVirtualSpans(
			    m, TextRange{(int)rec.pos, (int)rec.pos},
			    spanmap::FeatureAll);
			if (rec.fidelity == spanmap::FidelityNone) {
				assert::Equal(t, (int)positions.size(), 0);
				assert::Equal(t, (int)spans.size(), 0);
				return;
			}
			assert::Equal(t, (int)positions.size(), 1);
			assert::Equal(t, positions[0].Position, rec.want);
			assert::Equal(t, positions[0].Fidelity, rec.fidelity);
			assert::Equal(t, (int)spans.size(), 1);
			assert::Equal(t, TextPos(spans[0].Span.pos()), rec.want);
			assert::Equal(t, spans[0].Fidelity, rec.fidelity);
		});
	}
}

void TestOriginalToVirtualPositionsAtEndpoint(T* t) {
	t->Parallel();

	auto* m = spanmap::New(std::vector<Segment>{
	    {2, 5, 10, 13, spanmap::KindVerbatim, spanmap::FeatureCompletion},
	    {8, 11, 13, 16, spanmap::KindVerbatim, spanmap::FeatureCompletion},
	    {20, 23, 30, 35, spanmap::KindAtom, spanmap::FeatureCompletion},
	});

	assert::Assert(t, spanmap::OriginalToVirtualPositions(
	                      m, 13, spanmap::FeatureCompletion) ==
	                      std::vector<MappedPosition>{
	                          {5, spanmap::FidelityExact},
	                          {8, spanmap::FidelityExact},
	                      });
	assert::Assert(t, spanmap::OriginalToVirtualPositions(
	                      m, 35, spanmap::FeatureCompletion) ==
	                      std::vector<MappedPosition>{
	                          {23, spanmap::FidelityAtom},
	                      });

	auto* filtered = spanmap::New(std::vector<Segment>{
	    {20, 23, 10, 13, spanmap::KindVerbatim, spanmap::FeatureHover},
	    {2, 5, 13, 16, spanmap::KindVerbatim, spanmap::FeatureCompletion},
	});
	assert::Assert(t, spanmap::OriginalToVirtualPositions(
	                      filtered, 13, spanmap::FeatureAll) ==
	                      std::vector<MappedPosition>{
	                          {2, spanmap::FidelityExact},
	                          {23, spanmap::FidelityExact},
	                      });
	assert::Assert(t, spanmap::OriginalToVirtualPositions(
	                      filtered, 13, spanmap::FeatureCompletion) ==
	                      std::vector<MappedPosition>{
	                          {2, spanmap::FidelityExact},
	                      });
}

void TestOriginalToVirtualDuplicateGroup(T* t) {
	t->Parallel();

	auto* m = spanmap::New(std::vector<Segment>{
	    {0, 3, 10, 13, spanmap::KindVerbatim, spanmap::FeatureDefinition},
	    {10, 13, 10, 13, spanmap::KindVerbatim, spanmap::FeatureHover},
	    {20, 25, 10, 13, spanmap::KindAtom, spanmap::FeatureDefinition},
	});

	auto semantic =
	    spanmap::OriginalToVirtualPositions(m, 11, spanmap::FeatureHover);
	assert::Equal(t, (int)semantic.size(), 1);
	assert::Equal(t, semantic[0].Position, TextPos(11));
	assert::Equal(t, semantic[0].Fidelity, spanmap::FidelityExact);

	auto navigation = spanmap::OriginalToVirtualPositions(
	    m, 11, spanmap::FeatureDefinition);
	assert::Equal(t, (int)navigation.size(), 2);
	assert::Equal(t, navigation[0].Position, TextPos(1));
	assert::Equal(t, navigation[0].Fidelity, spanmap::FidelityExact);
	assert::Equal(t, navigation[1].Position, TextPos(20));
	assert::Equal(t, navigation[1].Fidelity, spanmap::FidelityAtom);

	auto spans = spanmap::OriginalToVirtualSpans(
	    m, TextRange{10, 13}, spanmap::FeatureDefinition);
	assert::Equal(t, (int)spans.size(), 2);
	assert::Equal(t, spans[0].Span.pos(), 0);
	assert::Equal(t, spans[0].Span.end(), 3);
	assert::Equal(t, spans[1].Span.pos(), 20);
	assert::Equal(t, spans[1].Span.end(), 25);
}

void TestOriginalToVirtualOverlappingSpans(T* t) {
	t->Parallel();

	auto* m = spanmap::New(std::vector<Segment>{
	    {0, 6, 0, 6, spanmap::KindVerbatim, spanmap::FeatureHover},
	    {10, 12, 2, 4, spanmap::KindVerbatim, spanmap::FeatureHover},
	    {20, 24, 3, 7, spanmap::KindVerbatim, spanmap::FeatureHover},
	});

	assert::Assert(t, spanmap::OriginalToVirtualPositions(
	                      m, 3, spanmap::FeatureHover) ==
	                      std::vector<MappedPosition>{
	                          {3, spanmap::FidelityExact},
	                          {11, spanmap::FidelityExact},
	                          {20, spanmap::FidelityExact},
	                      });
	auto spans =
	    spanmap::OriginalToVirtualSpans(m, TextRange{3, 4},
	                                    spanmap::FeatureHover);
	std::vector<MappedSpan> wantSpans{
	    {TextRange{3, 4}, spanmap::FidelityExact},
	    {TextRange{11, 12}, spanmap::FidelityExact},
	    {TextRange{20, 21}, spanmap::FidelityExact},
	};
	assert::Equal(t, (int)spans.size(), (int)wantSpans.size());
	for (size_t i = 0; i < spans.size(); i++) {
		assert::Equal(t, spans[i], wantSpans[i]);
	}
}

void TestOriginalToVirtualPositionFindsEarlyCoveringSegment(T* t) {
	t->Parallel();

	// Binary search lands near [90,95), which does not contain 97. The
	// interval index must still find the earlier [0,100) segment without
	// scanning every segment whose start precedes the query.
	auto* m = spanmap::New(std::vector<Segment>{
	    {0, 100, 0, 100, spanmap::KindVerbatim, spanmap::FeatureHover},
	    {100, 105, 80, 85, spanmap::KindVerbatim, spanmap::FeatureHover},
	    {105, 110, 90, 95, spanmap::KindVerbatim, spanmap::FeatureHover},
	    {110, 113, 100, 103, spanmap::KindVerbatim, spanmap::FeatureHover},
	});

	assert::Assert(t, spanmap::OriginalToVirtualPositions(
	                      m, 97, spanmap::FeatureHover) ==
	                      std::vector<MappedPosition>{
	                          {97, spanmap::FidelityExact},
	                      });
	auto spans =
	    spanmap::OriginalToVirtualSpans(m, TextRange{97, 98},
	                                    spanmap::FeatureHover);
	assert::Equal(t, (int)spans.size(), 1);
	assert::Assert(
	    t, spans[0] ==
	           MappedSpan{TextRange{97, 98}, spanmap::FidelityExact});

	// Point lookup includes both sides of a shared endpoint, including an
	// early interval found through the max-end tree. Nonempty span lookup
	// treats segment ends as exclusive and uses only the right segment.
	assert::Assert(t, spanmap::OriginalToVirtualPositions(
	                      m, 100, spanmap::FeatureHover) ==
	                      std::vector<MappedPosition>{
	                          {100, spanmap::FidelityExact},
	                          {110, spanmap::FidelityExact},
	                      });
	spans = spanmap::OriginalToVirtualSpans(m, TextRange{100, 101},
	                                        spanmap::FeatureHover);
	assert::Equal(t, (int)spans.size(), 1);
	assert::Assert(
	    t, spans[0] ==
	           MappedSpan{TextRange{110, 111}, spanmap::FidelityExact});
}

void TestOriginalToVirtualOverlapFallsBackFromDisabledContainer(T* t) {
	t->Parallel();

	auto* m = spanmap::New(std::vector<Segment>{
	    {0, 6, 0, 6, spanmap::KindVerbatim, spanmap::FeatureDefinition},
	    {10, 13, 0, 3, spanmap::KindVerbatim, spanmap::FeatureHover},
	    {13, 16, 3, 6, spanmap::KindVerbatim, spanmap::FeatureHover},
	});

	auto spans =
	    spanmap::OriginalToVirtualSpans(m, TextRange{1, 5},
	                                    spanmap::FeatureHover);
	assert::Equal(t, (int)spans.size(), 1);
	assert::Assert(
	    t, spans[0] ==
	           MappedSpan{TextRange{11, 15}, spanmap::FidelityApproximate});
}

void TestOriginalToVirtualCrossGroupProjections(T* t) {
	t->Parallel();

	auto* m = spanmap::New(std::vector<Segment>{
	    {0, 2, 0, 2, spanmap::KindVerbatim, spanmap::FeatureHover},
	    {2, 4, 2, 4, spanmap::KindVerbatim, spanmap::FeatureHover},
	    {10, 12, 0, 2, spanmap::KindVerbatim, spanmap::FeatureHover},
	    {12, 14, 2, 4, spanmap::KindVerbatim, spanmap::FeatureHover},
	});

	auto spans =
	    spanmap::OriginalToVirtualSpans(m, TextRange{1, 3},
	                                    spanmap::FeatureHover);
	assert::Equal(t, (int)spans.size(), 2);
	assert::Equal(t, spans[0].Span, TextRange{1, 3});
	assert::Equal(t, spans[1].Span, TextRange{11, 13});
	for (const auto& mapped : spans) {
		assert::Equal(t, mapped.Fidelity, spanmap::FidelityApproximate);
	}
}

void TestOriginalToVirtualExplicitZeroFeatures(T* t) {
	t->Parallel();

	auto* m = spanmap::New(std::vector<Segment>{
	    {0, 3, 10, 13, spanmap::KindVerbatim, spanmap::FeatureNone},
	});

	assert::Equal(t,
	              (int)spanmap::OriginalToVirtualPositions(
	                  m, 11, spanmap::FeatureHover)
	                  .size(),
	              0);
	assert::Equal(t,
	              (int)spanmap::OriginalToVirtualPositions(
	                  m, 11, spanmap::FeatureDefinition)
	                  .size(),
	              0);
	assert::Equal(t,
	              (int)spanmap::OriginalToVirtualSpans(
	                  m, TextRange{10, 13}, spanmap::FeatureHover)
	                  .size(),
	              0);

	auto [data, err] = spanmap::Marshal(m);
	assert::Assert(t, !err.has_value());
	assert::Equal(t, data, std::string("[[0,3,10,3,0,0]]"));
	auto [decoded, derr] = spanmap::Unmarshal(data);
	assert::Assert(t, !derr.has_value());
	auto segments = spanmap::Segments(decoded);
	assert::Equal(t, segments[0].Features, spanmap::FeatureNone);

	auto [legacy, lerr] = spanmap::Unmarshal("[[0,3,10,3,0]]");
	assert::Assert(t, !lerr.has_value());
	assert::Equal(t, spanmap::Segments(legacy)[0].Features,
	              spanmap::FeatureAll);
	assert::Equal(t,
	              (int)spanmap::OriginalToVirtualPositions(
	                  legacy, 11, spanmap::FeatureHover)
	                  .size(),
	              1);
}

void TestFeatureParticipationOriginalAndVirtual(T* t) {
	t->Parallel();
	auto* m = spanmap::New(std::vector<Segment>{
	    {0, 3, 10, 13, spanmap::KindVerbatim, spanmap::FeatureHover},
	    {3, 6, 20, 23, spanmap::KindVerbatim, spanmap::FeatureCompletion},
	});

	assert::Equal(t,
	              (int)spanmap::OriginalToVirtualPositions(
	                  m, 11, spanmap::FeatureHover)
	                  .size(),
	              1);
	assert::Equal(t,
	              (int)spanmap::OriginalToVirtualPositions(
	                  m, 11, spanmap::FeatureCompletion)
	                  .size(),
	              0);

	auto [mapped, fidelity] = spanmap::VirtualToOriginalSpanForFeature(
	    m, TextRange{0, 3}, spanmap::FeatureHover);
	assert::Equal(t, mapped, TextRange{10, 13});
	assert::Equal(t, fidelity, spanmap::FidelityExact);
	auto [m2, fidelity2] = spanmap::VirtualToOriginalSpanForFeature(
	    m, TextRange{0, 3}, spanmap::FeatureCompletion);
	assert::Equal(t, fidelity2, spanmap::FidelityNone);

	// Diagnostics and edit safety use unfiltered geometry and cannot be
	// disabled by feature flags.
	auto [mapped3, fidelity3] =
	    spanmap::VirtualToOriginalSpan(m, TextRange{0, 3});
	assert::Equal(t, mapped3, TextRange{10, 13});
	assert::Equal(t, fidelity3, spanmap::FidelityExact);
}

void TestOriginalToVirtualSpanRoundTrip(T* t) {
	t->Parallel();

	// Original spans are out of order relative to virtual spans, exercising
	// the reverse index.
	auto* m = spanmap::New(std::vector<Segment>{
	    {0, 10, 200, 210, spanmap::KindVerbatim, spanmap::FeatureAll},
	    {10, 20, 100, 110, spanmap::KindVerbatim, spanmap::FeatureAll},
	});

	for (TextRange r : {TextRange{2, 8}, TextRange{12, 18}}) {
		auto [orig, fidelity] = spanmap::VirtualToOriginalSpan(m, r);
		assert::Equal(t, fidelity, spanmap::FidelityExact);
		auto back =
		    spanmap::OriginalToVirtualSpans(m, orig, spanmap::FeatureAll);
		assert::Equal(t, (int)back.size(), 1);
		assert::Equal(t, back[0].Fidelity, spanmap::FidelityExact);
		assert::Equal(t, back[0].Span.pos(), r.pos());
		assert::Equal(t, back[0].Span.end(), r.end());
	}
}

void TestMarshalRoundTrip(T* t) {
	t->Parallel();

	auto* original = spanmap::New(std::vector<Segment>{
	    {3, 14, 60, 71, spanmap::KindAtom},
	    {14, 24, 71, 81, spanmap::KindVerbatim},
	});

	auto [data, err] = spanmap::Marshal(original);
	assert::Assert(t, !err.has_value());
	auto [decoded, derr] = spanmap::Unmarshal(data);
	assert::Assert(t, !derr.has_value());

	for (TextRange r : {TextRange{1, 2}, TextRange{4, 10}, TextRange{16, 20}}) {
		auto [wantRange, wantFidelity] =
		    spanmap::VirtualToOriginalSpan(original, r);
		auto [gotRange, gotFidelity] =
		    spanmap::VirtualToOriginalSpan(decoded, r);
		assert::Equal(t, gotRange, wantRange);
		assert::Equal(t, gotFidelity, wantFidelity);
	}
}

void TestValidate(T* t) {
	t->Parallel();

	const std::string transformed = "const greeting = 1;\n";
	const std::string original = "<x>const greeting = 1;\n</x>";
	int scriptStart = 3; // index of "const" in original

	struct TestCase {
		std::string name;
		std::vector<Segment> segs;
		spanmap::MappingErrorKind wantKind;
		bool wantOK;
	};
	std::vector<TestCase> testCases{
	    {"valid verbatim",
	     {{0, (int)transformed.size(), scriptStart,
	       scriptStart + (int)transformed.size(), spanmap::KindVerbatim}},
	     0,
	     true},
	    {"empty is valid", {}, 0, true},
	    {"gap is allowed",
	     {{3, (int)transformed.size(), 0, 0, spanmap::KindAtom}},
	     0,
	     true},
	    {"overlap",
	     {{0, 10, 0, 0, spanmap::KindAtom},
	      {5, (int)transformed.size(), 0, 0, spanmap::KindAtom}},
	     spanmap::MappingErrorKindOverlap,
	     false},
	    {"original out of bounds",
	     {{0, (int)transformed.size(), 0, (int)original.size() + 10,
	       spanmap::KindAtom}},
	     spanmap::MappingErrorKindOutOfBounds,
	     false},
	    {"verbatim text mismatch",
	     {{0, (int)transformed.size(), 0, (int)transformed.size(),
	       spanmap::KindVerbatim}},
	     spanmap::MappingErrorKindVerbatimMismatch,
	     false},
	    {"unknown kind",
	     {{0, 1, 0, 1, 3}},
	     spanmap::MappingErrorKindKind,
	     false},
	};

	for (const auto& tc : testCases) {
		auto rec = tc;
		t->Run(tc.name, [rec, &transformed, &original](T* t) {
			t->Parallel();
			auto problem =
			    spanmap::Validate(spanmap::New(rec.segs), transformed,
			                      original);
			if (rec.wantOK) {
				assert::Assert(t, !problem.has_value(),
				               "expected valid");
				return;
			}
			assert::Assert(t, problem.has_value(),
			               "expected a problem");
			assert::Equal(t, problem->Kind, rec.wantKind);
		});
	}
}

void TestValidateOriginalOverlapAndFeatures(T* t) {
	t->Parallel();

	struct TestCase {
		std::string name;
		std::vector<Segment> segments;
		spanmap::MappingErrorKind wantKind;
		bool valid;
	};
	std::vector<TestCase> tests{
	    {"identical duplicate group",
	     {{0, 3, 0, 3, spanmap::KindVerbatim, spanmap::FeatureDefinition},
	      {3, 6, 0, 3, spanmap::KindVerbatim, spanmap::FeatureHover}},
	     0,
	     true},
	    {"partial original overlap is valid",
	     {{0, 3, 0, 3, spanmap::KindAtom},
	      {3, 6, 2, 5, spanmap::KindAtom}},
	     0,
	     true},
	    {"nested original overlap is valid",
	     {{0, 5, 0, 5, spanmap::KindAtom}, {5, 6, 1, 4, spanmap::KindAtom}},
	     0,
	     true},
	    {"duplicate without explicit features is tolerant",
	     {{0, 3, 0, 3, spanmap::KindAtom},
	      {3, 6, 0, 3, spanmap::KindAtom, spanmap::FeatureDefinition}},
	     0,
	     true},
	    {"duplicate with shared feature members is tolerant",
	     {{0, 3, 0, 3, spanmap::KindAtom, spanmap::FeatureHover},
	      {3, 6, 0, 3, spanmap::KindAtom,
	       spanmap::FeatureHover | spanmap::FeatureDefinition}},
	     0,
	     true},
	    {"features on sole cover are valid",
	     {{0, 3, 0, 3, spanmap::KindAtom, spanmap::FeatureDefinition}},
	     0,
	     true},
	    {"unknown feature flag",
	     {{0, 3, 0, 3, spanmap::KindAtom, 1 << 22}},
	     spanmap::MappingErrorKindFeature,
	     false},
	};

	for (const auto& test : tests) {
		auto rec = test;
		t->Run(test.name, [rec](T* t) {
			t->Parallel();
			auto problem = spanmap::Validate(spanmap::New(rec.segments),
			                                 "abcabc", "abcdef");
			if (rec.valid) {
				assert::Assert(t, !problem.has_value(),
				               "expected valid");
				return;
			}
			assert::Assert(t, problem.has_value());
			assert::Equal(t, problem->Kind, rec.wantKind);
		});
	}
}

void TestValidateNilIsValid(T* t) {
	t->Parallel();
	SpanMap* m = nullptr;
	assert::Assert(t, !spanmap::Validate(m, "abc", "abc").has_value());
}

} // namespace

REGISTER_UNIT_TEST("spanmap.TestVirtualToOriginalSpanVerbatim",
                   TestVirtualToOriginalSpanVerbatim);
REGISTER_UNIT_TEST("spanmap.TestVirtualToOriginalSpanAtom",
                   TestVirtualToOriginalSpanAtom);
REGISTER_UNIT_TEST("spanmap.TestVirtualAlias", TestVirtualAlias);
REGISTER_UNIT_TEST("spanmap.TestVirtualToOriginalSpanSynthesizedGap",
                   TestVirtualToOriginalSpanSynthesizedGap);
REGISTER_UNIT_TEST(
    "spanmap.TestOriginalToVirtualIntersectingSpansAllowsUncoveredEndpoints",
    TestOriginalToVirtualIntersectingSpansAllowsUncoveredEndpoints);
REGISTER_UNIT_TEST("spanmap.TestVirtualToOriginalSpanEmptyIsSynthesized",
                   TestVirtualToOriginalSpanEmptyIsSynthesized);
REGISTER_UNIT_TEST("spanmap.TestVirtualToOriginalSpanCrossingSegments",
                   TestVirtualToOriginalSpanCrossingSegments);
REGISTER_UNIT_TEST("spanmap.TestVirtualToOriginalSpanNilIdentity",
                   TestVirtualToOriginalSpanNilIdentity);
REGISTER_UNIT_TEST("spanmap.TestVirtualToOriginalPosition",
                   TestVirtualToOriginalPosition);
REGISTER_UNIT_TEST("spanmap.TestVirtualToOriginalPositionExact",
                   TestVirtualToOriginalPositionExact);
REGISTER_UNIT_TEST(
    "spanmap.TestVirtualToOriginalPositionExactRejectsDiscontinuousBoundary",
    TestVirtualToOriginalPositionExactRejectsDiscontinuousBoundary);
REGISTER_UNIT_TEST("spanmap.TestZeroLengthSpansAtSegmentEnds",
                   TestZeroLengthSpansAtSegmentEnds);
REGISTER_UNIT_TEST("spanmap.TestMapPositionNilIdentity",
                   TestMapPositionNilIdentity);
REGISTER_UNIT_TEST("spanmap.TestOriginalToVirtualSpanVerbatim",
                   TestOriginalToVirtualSpanVerbatim);
REGISTER_UNIT_TEST("spanmap.TestOriginalToVirtualSpanAtom",
                   TestOriginalToVirtualSpanAtom);
REGISTER_UNIT_TEST("spanmap.TestOriginalToVirtualSpanGap",
                   TestOriginalToVirtualSpanGap);
REGISTER_UNIT_TEST("spanmap.TestOriginalToVirtualSpanNilIdentity",
                   TestOriginalToVirtualSpanNilIdentity);
REGISTER_UNIT_TEST("spanmap.TestOriginalToVirtualPositions",
                   TestOriginalToVirtualPositions);
REGISTER_UNIT_TEST("spanmap.TestOriginalToVirtualPositionsAtEndpoint",
                   TestOriginalToVirtualPositionsAtEndpoint);
REGISTER_UNIT_TEST("spanmap.TestOriginalToVirtualDuplicateGroup",
                   TestOriginalToVirtualDuplicateGroup);
REGISTER_UNIT_TEST("spanmap.TestOriginalToVirtualOverlappingSpans",
                   TestOriginalToVirtualOverlappingSpans);
REGISTER_UNIT_TEST(
    "spanmap.TestOriginalToVirtualPositionFindsEarlyCoveringSegment",
    TestOriginalToVirtualPositionFindsEarlyCoveringSegment);
REGISTER_UNIT_TEST(
    "spanmap.TestOriginalToVirtualOverlapFallsBackFromDisabledContainer",
    TestOriginalToVirtualOverlapFallsBackFromDisabledContainer);
REGISTER_UNIT_TEST("spanmap.TestOriginalToVirtualCrossGroupProjections",
                   TestOriginalToVirtualCrossGroupProjections);
REGISTER_UNIT_TEST("spanmap.TestOriginalToVirtualExplicitZeroFeatures",
                   TestOriginalToVirtualExplicitZeroFeatures);
REGISTER_UNIT_TEST("spanmap.TestFeatureParticipationOriginalAndVirtual",
                   TestFeatureParticipationOriginalAndVirtual);
REGISTER_UNIT_TEST("spanmap.TestOriginalToVirtualSpanRoundTrip",
                   TestOriginalToVirtualSpanRoundTrip);
REGISTER_UNIT_TEST("spanmap.TestMarshalRoundTrip", TestMarshalRoundTrip);
REGISTER_UNIT_TEST("spanmap.TestValidate", TestValidate);
REGISTER_UNIT_TEST("spanmap.TestValidateOriginalOverlapAndFeatures",
                   TestValidateOriginalOverlapAndFeatures);
REGISTER_UNIT_TEST("spanmap.TestValidateNilIsValid", TestValidateNilIsValid);
