// Ported fourslash tests — batch A. One static void TestX(gostd::testing::T*)
// per Go `func TestX` in tsc/internal/fourslash/tests/*_test.go; each test
// self-registers in the fourslashrunner registry. `defer done()` is modeled
// by TSC_DEFER_FN(done) and `defer testutil.RecoverAndFail` by
// tsc::testutil::withRecoverAndFail (a try/catch: a propagating C++
// exception is the recovered panic; testGoexit passes through).
#include "internal/fourslash/fourslash.h"
#include "internal/fourslash/goutil.h"
#include "internal/fourslash/test_parser.h"
#include "internal/fourslash/tests/registry.h"
#include "internal/fourslash/tests/util/util.h"
#include "internal/gostd/testing.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/testutil/testutil.h"

namespace {
using namespace tsc;
namespace tsu = tsc::fourslash::tests::util;

// getOccurrencesIsDefinitionOfArrowFunction_test.go
static void TestGetOccurrencesIsDefinitionOfArrowFunction(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(/*1*/var /*2*/f = x => x + 1;
/*3*/f(12);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesIsDefinitionOfArrowFunction, TestGetOccurrencesIsDefinitionOfArrowFunction);

// getOccurrencesIsDefinitionOfBindingPattern_test.go
static void TestGetOccurrencesIsDefinitionOfBindingPattern(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const { /*1*/x, y } = { /*2*/x: 1, y: 2 };
const z = /*3*/x;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesIsDefinitionOfBindingPattern, TestGetOccurrencesIsDefinitionOfBindingPattern);

// getOccurrencesIsDefinitionOfNumberNamedProperty_test.go
static void TestGetOccurrencesIsDefinitionOfNumberNamedProperty(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(let o = { /*1*/1: 12 };
let y = o[/*2*/1];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesIsDefinitionOfNumberNamedProperty, TestGetOccurrencesIsDefinitionOfNumberNamedProperty);

// getOccurrencesIsDefinitionOfStringNamedProperty_test.go
static void TestGetOccurrencesIsDefinitionOfStringNamedProperty(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(let o = { /*1*/"/*2*/x": 12 };
let y = o./*3*/x;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesIsDefinitionOfStringNamedProperty, TestGetOccurrencesIsDefinitionOfStringNamedProperty);

// getOccurrencesIsDefinitionOfTypeAlias_test.go
static void TestGetOccurrencesIsDefinitionOfTypeAlias(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(/*1*/type /*2*/Alias= number;
let n: /*3*/Alias = 12;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesIsDefinitionOfTypeAlias, TestGetOccurrencesIsDefinitionOfTypeAlias);

// getOccurrencesIsDefinitionOfFunction_test.go
static void TestGetOccurrencesIsDefinitionOfFunction(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(/*1*/function /*2*/func(x: number) {
}
/*3*/func(x))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesIsDefinitionOfFunction, TestGetOccurrencesIsDefinitionOfFunction);

}  // namespace
