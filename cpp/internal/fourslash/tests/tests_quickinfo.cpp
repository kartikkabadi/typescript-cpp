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

// quickInfoForConstDeclaration_test.go
static void TestQuickInfoForConstDeclaration(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const /**/c = 0 ;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "const c: 0", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForConstDeclaration, TestQuickInfoForConstDeclaration);

// quickInfoForConstTypeReference_test.go
static void TestQuickInfoForConstTypeReference(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS("" as /**/const;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNotQuickInfoExists(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForConstTypeReference, TestQuickInfoForConstTypeReference);

// quickInfoForNamedTupleMember_test.go
static void TestQuickInfoForNamedTupleMember(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(type foo = [/**/x: string];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "string", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForNamedTupleMember, TestQuickInfoForNamedTupleMember);

// quickInfoFunctionCheckType_test.go
static void TestQuickInfoFunctionCheckType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(export type /**/Tail<T extends any[]> = ((...t: T) => void) extends (h: any, ...rest: infer R) => void ? R : never;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "type Tail<T extends any[]> = ((...t: T) => void) extends (h: any, ...rest: infer R) => void ? R : never", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoFunctionCheckType, TestQuickInfoFunctionCheckType);

// quickInfoNamedTupleMembers_test.go
static void TestQuickInfoNamedTupleMembers(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(export type /*1*/Segment = [length: number, count: number];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "type Segment = [length: number, count: number]", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoNamedTupleMembers, TestQuickInfoNamedTupleMembers);

// quickInfoRecursiveObjectLiteral_test.go
static void TestQuickInfoRecursiveObjectLiteral(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(var a = { f: /**/a)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "var a: any", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoRecursiveObjectLiteral, TestQuickInfoRecursiveObjectLiteral);

// quickInfoCircularInstantiationExpression_test.go
static void TestQuickInfoCircularInstantiationExpression(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(declare function foo<T>(t: T): typeof foo<T>;
/**/foo("");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoCircularInstantiationExpression, TestQuickInfoCircularInstantiationExpression);

// quickInfoDisplayPartsClassDefaultAnonymous_test.go
static void TestQuickInfoDisplayPartsClassDefaultAnonymous(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(/*1*/export /*2*/default /*3*/class /*4*/ {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsClassDefaultAnonymous, TestQuickInfoDisplayPartsClassDefaultAnonymous);

// quickInfoDisplayPartsClassDefaultNamed_test.go
static void TestQuickInfoDisplayPartsClassDefaultNamed(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(/*1*/export /*2*/default /*3*/class /*4*/C /*5*/ {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsClassDefaultNamed, TestQuickInfoDisplayPartsClassDefaultNamed);

// quickInfoDisplayPartsClassIncomplete_test.go
static void TestQuickInfoDisplayPartsClassIncomplete(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(/*1*/class /*2*/ {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsClassIncomplete, TestQuickInfoDisplayPartsClassIncomplete);

// quickInfoDisplayPartsTypeParameterInTypeAlias_test.go
static void TestQuickInfoDisplayPartsTypeParameterInTypeAlias(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(type /*0*/List</*1*/T> = /*2*/T[]
type /*3*/List2</*4*/T extends string> = /*5*/T[];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsTypeParameterInTypeAlias, TestQuickInfoDisplayPartsTypeParameterInTypeAlias);

// quickInfoDisplayPartsTypeParameterInFunctionLikeInTypeAlias_test.go
static void TestQuickInfoDisplayPartsTypeParameterInFunctionLikeInTypeAlias(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(type MixinCtor<A> = new () => /*0*/A & { constructor: MixinCtor</*1*/A> };
type MixinCtor<A> = new () => A & { constructor: { constructor: MixinCtor</*2*/A> } };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsTypeParameterInFunctionLikeInTypeAlias, TestQuickInfoDisplayPartsTypeParameterInFunctionLikeInTypeAlias);

}  // namespace
