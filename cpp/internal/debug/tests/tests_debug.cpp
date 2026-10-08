// tests_debug.cpp — port of tsc/internal/debug/debug_test.go.
#include <string>

#include "internal/debug/debug.h"
#include "internal/gostd/testing.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
namespace assert = tsc::gotest::assert;
using namespace tsc;

#undef assert

namespace {

void TestFailEmptyReason(T* t) {
	t->Parallel();
	testutil::AssertPanics(t, [] { debug::fail(""); },
	                       std::string("Debug failure."));
}

void TestFailWithReason(T* t) {
	t->Parallel();
	testutil::AssertPanics(t, [] { debug::fail("something went wrong"); },
	                       std::string("Debug failure. something went wrong"));
}

struct mockNode {
	std::string kind;

	std::string KindString() const { return kind; }
};

void TestFailBadSyntaxKindNoMessage(T* t) {
	t->Parallel();
	testutil::AssertPanics(
	    t, [] { debug::failBadSyntaxKind(mockNode{"FooNode"}); },
	    std::string("Debug failure. Unexpected node.\nNode FooNode was "
	                "unexpected."));
}

void TestFailBadSyntaxKindWithMessage(T* t) {
	t->Parallel();
	testutil::AssertPanics(
	    t,
	    [] { debug::failBadSyntaxKind(mockNode{"BarNode"}, "custom message"); },
	    std::string("Debug failure. custom message\nNode BarNode was "
	                "unexpected."));
}

void TestAssertNeverDefaultMessageKindString(T* t) {
	t->Parallel();
	testutil::AssertPanics(t, [] { debug::assertNever(mockNode{"TestNode"}); },
	                       std::string("Debug failure. Illegal value: "
	                                   "TestNode"));
}

void TestAssertNeverCustomMessageKindString(T* t) {
	t->Parallel();
	testutil::AssertPanics(
	    t, [] { debug::assertNever(mockNode{"TestNode"}, "bad value:"); },
	    std::string("Debug failure. bad value: TestNode"));
}

struct mockStringer {
	std::string s;

	std::string String() const { return s; }
};

void TestAssertNeverStringer(T* t) {
	t->Parallel();
	testutil::AssertPanics(t, [] { debug::assertNever(mockStringer{"hello"}); },
	                       std::string("Debug failure. Illegal value: hello"));
}

void TestAssertNeverFallback(T* t) {
	t->Parallel();
	testutil::AssertPanics(t, [] { debug::assertNever(42); },
	                       std::string("Debug failure. Illegal value: 42"));
}

void TestAssertTrue(T* t) {
	t->Parallel();
	debug::assert(true);
}

void TestAssertTrueWithMessage(T* t) {
	t->Parallel();
	debug::assert(true, "this should not trigger");
}

void TestAssertFalseNoMessage(T* t) {
	t->Parallel();
	testutil::AssertPanics(t, [] { debug::assert(false); },
	                       std::string("Debug failure. False expression."));
}

void TestAssertFalseWithMessage(T* t) {
	t->Parallel();
	testutil::AssertPanics(t,
	                       [] { debug::assert(false, "expected x > 0"); },
	                       std::string("Debug failure. False expression: "
	                                   "expected x > 0"));
}

} // namespace

REGISTER_UNIT_TEST("debug.TestFailEmptyReason", TestFailEmptyReason);
REGISTER_UNIT_TEST("debug.TestFailWithReason", TestFailWithReason);
REGISTER_UNIT_TEST("debug.TestFailBadSyntaxKindNoMessage",
                   TestFailBadSyntaxKindNoMessage);
REGISTER_UNIT_TEST("debug.TestFailBadSyntaxKindWithMessage",
                   TestFailBadSyntaxKindWithMessage);
REGISTER_UNIT_TEST("debug.TestAssertNeverDefaultMessageKindString",
                   TestAssertNeverDefaultMessageKindString);
REGISTER_UNIT_TEST("debug.TestAssertNeverCustomMessageKindString",
                   TestAssertNeverCustomMessageKindString);
REGISTER_UNIT_TEST("debug.TestAssertNeverStringer", TestAssertNeverStringer);
REGISTER_UNIT_TEST("debug.TestAssertNeverFallback", TestAssertNeverFallback);
REGISTER_UNIT_TEST("debug.TestAssertTrue", TestAssertTrue);
REGISTER_UNIT_TEST("debug.TestAssertTrueWithMessage",
                   TestAssertTrueWithMessage);
REGISTER_UNIT_TEST("debug.TestAssertFalseNoMessage", TestAssertFalseNoMessage);
REGISTER_UNIT_TEST("debug.TestAssertFalseWithMessage",
                   TestAssertFalseWithMessage);
