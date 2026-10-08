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

// signatureHelpWithUnknown_test.go
static void TestSignatureHelpWithUnknown(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(eval(\/*1*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpWithUnknown, TestSignatureHelpWithUnknown);

// signatureHelpNegativeTests2_test.go
static void TestSignatureHelpNegativeTests2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(class clsOverload { constructor(); constructor(test: string); constructor(test?: string) { } }
var x = new clsOverload/*beforeOpenParen*/()/*afterCloseParen*/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoSignatureHelpForMarkers(t, {"beforeOpenParen", "afterCloseParen"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpNegativeTests2, TestSignatureHelpNegativeTests2);

// signatureHelpOnDeclaration_test.go
static void TestSignatureHelpOnDeclaration(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function f</**/
x)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoSignatureHelpForMarkers(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpOnDeclaration, TestSignatureHelpOnDeclaration);

// signatureHelpSkippedArgs1_test.go
static void TestSignatureHelpSkippedArgs1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function fn(a: number, b: number, c: number) {}
fn(/*1*/, /*2*/, /*3*/, /*4*/, /*5*/);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpSkippedArgs1, TestSignatureHelpSkippedArgs1);

// signatureHelpAnonymousType_test.go
static void TestSignatureHelpAnonymousType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const comparers: Array<(a: any, b: any) => boolean> = [];

comparers.push((a,/**/ b) => true);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpAnonymousType, TestSignatureHelpAnonymousType);

// signatureHelpInference_test.go
static void TestSignatureHelpInference(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(declare function f<T extends string>(a: T, b: T, c: T): void;
f("x", /**/);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "f(a: \"x\", b: \"x\", c: \"x\"): void", .ParameterCount = 3, .ParameterName = "b", .ParameterSpan = "b: \"x\""});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpInference, TestSignatureHelpInference);

// signatureHelpNegativeTests_test.go
static void TestSignatureHelpNegativeTests(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//inside a comment foo(/*insideComment*/
cl/*invalidContext*/ass InvalidSignatureHelpLocation { }
InvalidSignatureHelpLocation(/*validContext*/);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoSignatureHelpForMarkers(t, {"insideComment", "invalidContext", "validContext"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpNegativeTests, TestSignatureHelpNegativeTests);

// signatureHelpOptionalCall_test.go
static void TestSignatureHelpOptionalCall(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function fnTest(str: string, num: number) { }
fnTest?.(/*1*/);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "fnTest(str: string, num: number): void", .ParameterCount = 2, .ParameterName = "str", .ParameterSpan = "str: string"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpOptionalCall, TestSignatureHelpOptionalCall);

}  // namespace
