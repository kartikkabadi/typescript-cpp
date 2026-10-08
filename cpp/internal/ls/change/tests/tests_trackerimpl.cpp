// Port of tsc/internal/ls/change/trackerimpl_test.go.
#include "internal/gostd/testing.h"
#include "internal/ls/change/change.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
using namespace tsc;
namespace lsproto = tsc::lsp::lsproto;
namespace change = tsc::ls::change;

static void TestTextEditsConflictAtSameInsertionPointAcrossProjections(T* t) {
	t->Parallel();

	lsproto::Position position{.Line = 1, .Character = 2};
	lsproto::TextEdit a{
	    .Range = lsproto::Range{.Start = position, .End = position},
	    .NewText = "a"};
	lsproto::TextEdit b{
	    .Range = lsproto::Range{.Start = position, .End = position},
	    .NewText = "b"};

	if (!change::textEditsConflict(a, b, true)) {
		t->Fatal({"different insertions at the same position across "
		          "projections should conflict"});
	}
	if (change::textEditsConflict(a, b, false)) {
		t->Fatal({"insertions at the same position within one projection "
		          "should retain their existing ordering"});
	}
}
REGISTER_UNIT_TEST("ls/change.TestTextEditsConflictAtSameInsertionPointAcrossProjections",
                   TestTextEditsConflictAtSameInsertionPointAcrossProjections);
