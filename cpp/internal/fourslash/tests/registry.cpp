#include "internal/fourslash/tests/registry.h"

namespace tsc::fourslash::tests {

std::vector<FourslashTestCase>& fourslashTestRegistry() {
	static std::vector<FourslashTestCase> registry;
	return registry;
}

}  // namespace tsc::fourslash::tests
