// Self-registration registry for ported Go *_test.go unit tests — each
// tests_*.cpp file registers its TestX functions via REGISTER_UNIT_TEST so
// ported test files drop in without touching a central list. Mirrors
// internal/fourslash/tests/registry.h.
//
// Registered names should be package-qualified for uniqueness, e.g.
//   REGISTER_UNIT_TEST("tsoptions.TestParseCommandLine", fn)
#pragma once

#include <functional>
#include <vector>

namespace tsc::gostd::testing {
class T;
}

namespace tsc::testutil::unittests {

struct UnitTestCase {
	const char* name;
	std::function<void(gostd::testing::T*)> fn;
};

std::vector<UnitTestCase>& unitTestRegistry();

}  // namespace tsc::testutil::unittests

// Two-level indirection so __LINE__ expands before token pasting.
#define _UT_CAT2(a, b) a##b
#define _UT_CAT(a, b) _UT_CAT2(a, b)
#define REGISTER_UNIT_TEST(name, fn)                                                     \
	static bool _UT_CAT(_utreg_, __LINE__) =                                           \
	    (tsc::testutil::unittests::unitTestRegistry().push_back({name, fn}), true)
