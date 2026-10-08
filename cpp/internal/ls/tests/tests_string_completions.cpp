// Port of tsc/internal/ls/string_completions_test.go.
#include <string>

#include "internal/gostd/testing.h"
#include "internal/ls/ls.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
using namespace tsc::ls;
using namespace tsc;

// TestTryRemoveDirectoryPrefixCaseFoldingShrinksPrefix — reproduces a panic
// that used to occur when tryRemoveDirectoryPrefix confirmed a
// case-insensitive directory match via GetCanonicalFileName, then sliced the
// raw (non-canonicalized) path using the raw byte length of prefix. Each
// Kelvin sign 'K' below case-folds to the single-byte 'k', so the raw
// prefix is longer in bytes (15) than path (12), even though path's
// canonical form is case-insensitively prefixed by prefix's canonical form.
// Slicing path[len(prefix):] used to panic with "slice bounds out of range
// [15:12]"; tryRemoveDirectoryPrefix must instead trim by rune count via
// tspath.TrimFilePathPrefix.
static void TestTryRemoveDirectoryPrefixCaseFoldingShrinksPrefix(T* t) {
	t->Parallel();

	std::string prefix = "/a/\xE2\x84\xAA\xE2\x84\xAA\xE2\x84\xAA\xE2\x84\xAA"; // 'K' x4 (Kelvin sign)
	std::string path = "/a/kkkk/x.ts";
	auto* actual = tryRemoveDirectoryPrefix(path, prefix,
	                                      false /*useCaseSensitiveFileNames*/);
	if (actual == nullptr) {
		t->Fatal({"expected a non-nil result"});
	}
	gotest::assert::Equal(t, *actual, std::string("x.ts"));
}
REGISTER_UNIT_TEST("ls.TestTryRemoveDirectoryPrefixCaseFoldingShrinksPrefix",
                   TestTryRemoveDirectoryPrefixCaseFoldingShrinksPrefix);
