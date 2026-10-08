// Self-registration registry for ported fourslash tests — each tests_*.cpp
// file registers its TestX functions via REGISTER_FOURSLASH_TEST so future
// batches add files without touching a central list.
#pragma once

#include <functional>
#include <vector>

namespace tsc::gostd::testing {
class T;
}

namespace tsc::fourslash::tests {

struct FourslashTestCase {
	const char* name;
	std::function<void(gostd::testing::T*)> fn;
};

std::vector<FourslashTestCase>& fourslashTestRegistry();

}  // namespace tsc::fourslash::tests

#define REGISTER_FOURSLASH_TEST(name, fn)                                                \
	static bool _fsreg_##name =                                                          \
	    (tsc::fourslash::tests::fourslashTestRegistry().push_back({#name, fn}), true)
