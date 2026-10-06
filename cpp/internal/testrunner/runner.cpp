// Port of tsc/internal/testrunner/runner.go
#include "internal/testrunner/testrunner.h"

namespace tsc::testrunner {

// runTests — runner.go:10-14.
void runTests(gostd::testing::T* t, std::vector<Runner*> runners) {
	for (auto* runner : runners) {
		runner->RunTests(t);
	}
}

}  // namespace tsc::testrunner
