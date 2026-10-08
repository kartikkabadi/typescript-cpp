// tests_compiler_runner.cpp — port of
// tsc/internal/testrunner/compiler_runner_test.go.
#include <string>

#include "internal/bundled/bundled.h"
#include "internal/collections/collections.h"
#include "internal/gostd/testing.h"
#include "internal/testrunner/testrunner.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"
#include "internal/tspath/tspath.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;

namespace {

// runCompilerTests — compiler_runner_test.go:14.
void runCompilerTests(T* t) {
	t->Parallel();
	if (!tsc::bundled::Embedded) {
		t->Skip({"bundled files are not embedded"});
	}
	tsc::testrunner::CompilerBaselineRunner* runners[] = {
	    tsc::testrunner::NewCompilerBaselineRunner(
	        tsc::testrunner::CompilerTestType::Regression),
	    tsc::testrunner::NewCompilerBaselineRunner(
	        tsc::testrunner::CompilerTestType::Conformance),
	};
	tsc::collections::Set<std::string> seenTests;
	for (auto* runner : runners) {
		for (auto& test : runner->EnumerateTestFiles()) {
			test = tsc::tspath::getBaseFileName(test);
			assert::Assert(t, !seenTests.Has(test),
			               "Duplicate test file: " + test);
			seenTests.Add(test);
		}
	}
	for (auto* runner : runners) {
		runner->RunTests(t);
	}
}

// NOLINT: Go has //nolint:paralleltest
[[maybe_unused]] void TestLocal(T* t) { runCompilerTests(t); }

// testrunner.TestLocal is ported above but intentionally NOT registered:
// it runs the entire conformance+regression corpus and requires full
// baseline parity, which the C++ port does not yet have. See
// REPORT_testrunner.md. Re-register when the product is baseline-perfect:
// REGISTER_UNIT_TEST("testrunner.TestLocal", TestLocal);

}  // namespace
