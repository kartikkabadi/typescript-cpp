// tests_pattern.cpp — port of tsc/internal/core/pattern_test.go.
#include "internal/core/pattern.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;

namespace {

void TestPatternOverlappingMatch(T* t) {
	t->Parallel();

	auto p = tsc::tryParsePattern("ab*ab");
	if (p.matches("ab")) {
		t->Errorf("expected 'ab' not to match 'ab*ab'", {});
	}
	if (!p.matches("abXab")) {
		t->Errorf("expected 'abXab' to match 'ab*ab'", {});
	}
	if (auto got = p.matchedText("abXab"); got != "X") {
		t->Errorf("MatchedText = %q, want %q", {got, std::string("X")});
	}
	if (!p.matches("abab")) {
		t->Errorf("expected 'abab' to match 'ab*ab'", {});
	}
	if (auto got = p.matchedText("abab"); !got.empty()) {
		t->Errorf("MatchedText = %q, want empty", {got});
	}
}

} // namespace

REGISTER_UNIT_TEST("core.TestPatternOverlappingMatch",
                   TestPatternOverlappingMatch);
