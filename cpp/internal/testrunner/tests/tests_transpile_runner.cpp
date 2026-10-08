// tests_transpile_runner.cpp — port of
// tsc/internal/testrunner/transpile_runner_test.go.
#include "internal/gostd/testing.h"
#include "internal/testrunner/testrunner.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;

namespace {

void TestTranspile(T* t) {
	t->Parallel();
	tsc::testrunner::RunTranspileTests(t);
}
REGISTER_UNIT_TEST("testrunner.TestTranspile", TestTranspile);

}  // namespace
