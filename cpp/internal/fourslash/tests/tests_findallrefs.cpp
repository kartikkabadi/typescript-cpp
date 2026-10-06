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

// findAllRefsBadImport_test.go
static void TestFindAllRefsBadImport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import { /*0*/ab as /*1*/cd } from "doesNotExist";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"0", "1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsBadImport, TestFindAllRefsBadImport);

// findAllRefsNoSubstitutionTemplateLiteralNoCrash1_test.go
static void TestFindAllRefsNoSubstitutionTemplateLiteralNoCrash1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(type Test = ` + "`" + `T/*1*/` + "`" + `;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsNoSubstitutionTemplateLiteralNoCrash1, TestFindAllRefsNoSubstitutionTemplateLiteralNoCrash1);

// findAllRefsDefinition_test.go
static void TestFindAllRefsDefinition(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const /*1*/x = 0;
/*2*/x;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsDefinition, TestFindAllRefsDefinition);

// findAllRefsEnumAsNamespace_test.go
static void TestFindAllRefsEnumAsNamespace(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(/*1*/enum /*2*/E { A }
let e: /*3*/E.A;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsEnumAsNamespace, TestFindAllRefsEnumAsNamespace);

// findAllRefsEnumMember_test.go
static void TestFindAllRefsEnumMember(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(enum E { /*1*/A, B }
const e: E./*2*/A = E./*3*/A;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsEnumMember, TestFindAllRefsEnumMember);

// findAllRefsForStringLiteralTypes_test.go
static void TestFindAllRefsForStringLiteralTypes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(type Options = "/*1*/option 1" | "option 2";
let myOption: Options = "/*2*/option 1";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsForStringLiteralTypes, TestFindAllRefsForStringLiteralTypes);

// findAllRefsImportEquals_test.go
static void TestFindAllRefsImportEquals(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import j = N./**/q;
namespace N { export const q = 0; })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllRefsImportEquals, TestFindAllRefsImportEquals);

// findAllReferencesFilteringMappedTypeProperty_test.go
static void TestFindAllReferencesFilteringMappedTypeProperty(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const obj = { /*1*/a: 1, b: 2 };
const filtered: { [P in keyof typeof obj as P extends 'b' ? never : P]: 0; } = { /*2*/a: 0 };
filtered./*3*/a;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllReferencesFilteringMappedTypeProperty, TestFindAllReferencesFilteringMappedTypeProperty);

// findAllReferencesImportMeta_test.go
static void TestFindAllReferencesImportMeta(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// Haha that's so meta!

let x = import.meta/**/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllReferencesImportMeta, TestFindAllReferencesImportMeta);

// findAllReferencesUndefined_test.go
static void TestFindAllReferencesUndefined(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /a.ts
/**/undefined;

void undefined;
// @Filename: /b.ts
undefined;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestFindAllReferencesUndefined, TestFindAllReferencesUndefined);

}  // namespace
