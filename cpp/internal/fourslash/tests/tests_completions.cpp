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

// completions01_test.go
static void TestCompletions01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @lib: es5
var x: string[] = [];
x.forEach(function (y) { y/*1*/
x.forEach(y => y/*2*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "1");
		f->Insert(t, ".");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"trim"}})}));
		f->Insert(t, "});");
		f->GoToMarker(t, "2");
		f->Insert(t, ".");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"trim"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestCompletions01, TestCompletions01);

// completionsBigIntShowNoCompletions_test.go
static void TestCompletionsBigIntShowNoCompletions(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(declare const SSL_OP_SSLEAY_080_CLIENT_DH_BUG: number
const foo = 0n/*1*/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestCompletionsBigIntShowNoCompletions, TestCompletionsBigIntShowNoCompletions);

// completionsAfterAsyncInObjectLiteral_test.go
static void TestCompletionsAfterAsyncInObjectLiteral(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const x: { m(): Promise<void> } = { async /**/ };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{"m"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestCompletionsAfterAsyncInObjectLiteral, TestCompletionsAfterAsyncInObjectLiteral);

// completionsBeforeRestArg1_test.go
static void TestCompletionsBeforeRestArg1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"});
		const std::string content = R"TS(// @target: esnext
// @lib: esnext
const layers = Object.assign({}, /*1*/...[]);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = tsu::CompletionGlobals})}));
	});
}
REGISTER_FOURSLASH_TEST(TestCompletionsBeforeRestArg1, TestCompletionsBeforeRestArg1);

// completionsNonExistentImport_test.go
static void TestCompletionsNonExistentImport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import { NonExistentType } from "non-existent-module";
let foo: /**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"NonExistentType"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestCompletionsNonExistentImport, TestCompletionsNonExistentImport);

// completionsDiscriminatedUnion_test.go
static void TestCompletionsDiscriminatedUnion(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(interface A { kind: "a"; a: number; }
interface B { kind: "b"; b: number; }
const c: A | B = { kind: "a", /**/ };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{"a"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestCompletionsDiscriminatedUnion, TestCompletionsDiscriminatedUnion);

// completionsOptionalMethod_test.go
static void TestCompletionsOptionalMethod(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @strictNullChecks: true
declare const x: { m?(): void };
x./**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{"m?"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestCompletionsOptionalMethod, TestCompletionsOptionalMethod);

// completionsUnion_test.go
static void TestCompletionsUnion(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(interface I { x: number; }
interface Many<T> extends ReadonlyArray<T> { extra: number; }
class C { private priv: number; }
const x: I | I[] | Many<string> | C = { /**/ };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{"x"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestCompletionsUnion, TestCompletionsUnion);

// completionsWithGenericStringLiteral_test.go
static void TestCompletionsWithGenericStringLiteral(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @strict: true
declare function get<T, K extends keyof T>(obj: T, key: K): T[K];
get({ hello: 123, world: 456 }, "/**/");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{"hello", "world"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestCompletionsWithGenericStringLiteral, TestCompletionsWithGenericStringLiteral);

// completionsRecursiveNamespace_test.go
static void TestCompletionsRecursiveNamespace(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(declare namespace N {
    export import M = N;
}
type T = N./**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestCompletionsRecursiveNamespace, TestCompletionsRecursiveNamespace);

}  // namespace
