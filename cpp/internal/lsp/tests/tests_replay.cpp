// Port of tsc/internal/lsp/replay_test.go — upstream-skipped.
//
// Go: TestReplay skips unless -replay=<file> was passed
// (`if replay == nil || *replay == ""` → t.Skip("no replay file
// specified")). This port has no replay flag plumbed through the test
// runner, so the test is registered as an unconditional SKIP with the
// upstream reason — matching the Go oracle run, which reports SKIP.
#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

namespace {

using tsc::gostd::testing::T;

void TestReplay(T* t) {
	t->Parallel();
	t->Skip({"no replay file specified"});
}
REGISTER_UNIT_TEST("lsp.TestReplay", TestReplay);

}  // namespace

// testmain_test.go — contains only TestMain (no runnable tests upstream).
// baseline.Track() coverage tracking is a no-op in the C++ runner.
