// tests_test_case_parser.cpp — port of
// tsc/internal/testrunner/test_case_parser_test.go.
#include <string>

#include "internal/gostd/testing.h"
#include "internal/testrunner/testrunner.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;

namespace {

void TestMakeUnitsFromTest(T* t) {
	t->Parallel();
	std::string code =
	    "// @strict: true\n"
	    "// @noEmit: true\n"
	    "// @filename: firstFile.ts\n"
	    "function foo() { return \"a\"; }\n"
	    "// normal comment\n"
	    "// @filename: secondFile.ts\n"
	    "// some other comment\n"
	    "function bar() { return \"b\"; }";
	tsc::testrunner::testUnit testUnit1{
	    .content = "function foo() { return \"a\"; }\n// normal comment",
	    .name = "firstFile.ts",
	};
	tsc::testrunner::testUnit testUnit2{
	    .content = "// some other comment\nfunction bar() { return \"b\"; }",
	    .name = "secondFile.ts",
	};
	// cmp.AllowUnexported + DeepEqual on the Go side == a field-by-field
	// comparison here: testUnitData (content/name per unit), tsConfig,
	// tsConfigFileUnitData, symlinks.
	tsc::testrunner::testCaseContent got =
	    tsc::testrunner::makeUnitsFromTest(code, "simpleTest.ts");
	assert::Equal(t, got.testUnitData.size(), size_t{2});
	assert::Equal(t, got.testUnitData[0]->content, testUnit1.content);
	assert::Equal(t, got.testUnitData[0]->name, testUnit1.name);
	assert::Equal(t, got.testUnitData[1]->content, testUnit2.content);
	assert::Equal(t, got.testUnitData[1]->name, testUnit2.name);
	assert::Equal(t, got.tsConfig == nullptr, true);
	assert::Equal(t, got.tsConfigFileUnitData == nullptr, true);
	assert::Equal(t, got.symlinks.empty(), true);
}
REGISTER_UNIT_TEST("testrunner.TestMakeUnitsFromTest", TestMakeUnitsFromTest);

}  // namespace
