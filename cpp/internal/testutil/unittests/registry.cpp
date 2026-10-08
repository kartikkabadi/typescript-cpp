#include "internal/testutil/unittests/registry.h"

namespace tsc::testutil::unittests {

std::vector<UnitTestCase>& unitTestRegistry() {
	static std::vector<UnitTestCase> registry;
	return registry;
}

}  // namespace tsc::testutil::unittests
