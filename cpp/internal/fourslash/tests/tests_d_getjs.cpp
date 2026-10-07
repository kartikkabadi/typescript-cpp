// PORTED FROM Go fourslash tests (batch D, fsgen)
#include "internal/fourslash/fourslash.h"
#include "internal/fourslash/goutil.h"
#include "internal/fourslash/test_parser.h"
#include "internal/fourslash/tests/registry.h"
#include "internal/fourslash/tests/util/util.h"
#include "internal/gostd/testing.h"
#include "internal/ls/lsutil/lsutil.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/ls/ls.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/stringtestutil/stringtestutil.h"
#include "internal/testutil/contentmappertest/contentmappertest.h"
#include "internal/json/json.h"
#include "internal/modulespecifiers/types.h"

namespace {

using namespace tsc;
namespace tsu = tsc::fourslash::tests::util;

static std::vector<fourslash::MarkerOrRangeOrName> asMonVec(
	const std::vector<std::any> &v) {
	std::vector<fourslash::MarkerOrRangeOrName> out;
	out.reserve(v.size());
	for (auto &e : v) {
		if (auto *p = std::any_cast<std::shared_ptr<fourslash::Marker>>(&e))
			out.push_back(*p);
		else if (auto *p =
					 std::any_cast<std::shared_ptr<fourslash::RangeMarker>>(&e))
			out.push_back(*p);
		else if (auto *p = std::any_cast<std::string>(&e)) out.push_back(*p);
	}
	return out;
}


static void TestGetJavaScriptSyntacticDiagnostics9(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
public function F() { })TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineNonSuggestionDiagnostics(t);
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptSyntacticDiagnostics9, TestGetJavaScriptSyntacticDiagnostics9);


static void TestGetJavaScriptSyntacticDiagnostics8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
type a = b;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineNonSuggestionDiagnostics(t);
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptSyntacticDiagnostics8, TestGetJavaScriptSyntacticDiagnostics8);


static void TestGetJavaScriptSyntacticDiagnostics7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
namespace M { })TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineNonSuggestionDiagnostics(t);
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptSyntacticDiagnostics7, TestGetJavaScriptSyntacticDiagnostics7);


static void TestGetJavaScriptSyntacticDiagnostics6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
interface I { })TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineNonSuggestionDiagnostics(t);
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptSyntacticDiagnostics6, TestGetJavaScriptSyntacticDiagnostics6);


static void TestGetJavaScriptSyntacticDiagnostics5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
class C implements D { })TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineNonSuggestionDiagnostics(t);
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptSyntacticDiagnostics5, TestGetJavaScriptSyntacticDiagnostics5);


static void TestGetJavaScriptSyntacticDiagnostics4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
public class C { })TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineNonSuggestionDiagnostics(t);
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptSyntacticDiagnostics4, TestGetJavaScriptSyntacticDiagnostics4);


static void TestGetJavaScriptSyntacticDiagnostics3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
class C<T> { })TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineNonSuggestionDiagnostics(t);
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptSyntacticDiagnostics3, TestGetJavaScriptSyntacticDiagnostics3);


static void TestGetJavaScriptSyntacticDiagnostics2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
export = b;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineNonSuggestionDiagnostics(t);
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptSyntacticDiagnostics2, TestGetJavaScriptSyntacticDiagnostics2);


static void TestGetJavaScriptSyntacticDiagnostics24(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
function Person(age) {
    if (age >= 18) {
        this.canVote = true;
    } else {
        this.canVote = 23;
    }
}
let x = new Person(100);
x.canVote/**/;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "(property) Person.canVote: number | boolean", "");
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptSyntacticDiagnostics24, TestGetJavaScriptSyntacticDiagnostics24);


static void TestGetJavaScriptSyntacticDiagnostics23(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
function Person(age) {
    if (age >= 18) {
        this.canVote = true;
    } else {
        this.canVote = false;
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyNonSuggestionDiagnostics(t, {});
		f->VerifyNonSuggestionDiagnostics(t, {});
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptSyntacticDiagnostics23, TestGetJavaScriptSyntacticDiagnostics23);


static void TestGetJavaScriptSyntacticDiagnostics22(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
function foo(...a) {})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyNonSuggestionDiagnostics(t, {});
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptSyntacticDiagnostics22, TestGetJavaScriptSyntacticDiagnostics22);


static void TestGetJavaScriptSyntacticDiagnostics21(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @experimentalDecorators: true
// @Filename: a.js
@internal class C {})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyNonSuggestionDiagnostics(t, {});
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptSyntacticDiagnostics21, TestGetJavaScriptSyntacticDiagnostics21);


static void TestGetJavaScriptSyntacticDiagnostics1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
import a = b;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineNonSuggestionDiagnostics(t);
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptSyntacticDiagnostics1, TestGetJavaScriptSyntacticDiagnostics1);


static void TestGetJavaScriptSyntacticDiagnostics19(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
enum E { })TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineNonSuggestionDiagnostics(t);
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptSyntacticDiagnostics19, TestGetJavaScriptSyntacticDiagnostics19);


static void TestGetJavaScriptSyntacticDiagnostics18(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
class C {
    x; // Regular property declaration allowed
    static y; // static allowed
    public z; // public not allowed
}
// @Filename: b.js
class C {
    x: number; // Types not allowed
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineNonSuggestionDiagnostics(t);
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptSyntacticDiagnostics18, TestGetJavaScriptSyntacticDiagnostics18);


static void TestGetJavaScriptSyntacticDiagnostics17(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
function F(a: number) { })TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineNonSuggestionDiagnostics(t);
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptSyntacticDiagnostics17, TestGetJavaScriptSyntacticDiagnostics17);


static void TestGetJavaScriptSyntacticDiagnostics16(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
function F(p?) { })TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineNonSuggestionDiagnostics(t);
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptSyntacticDiagnostics16, TestGetJavaScriptSyntacticDiagnostics16);


static void TestGetJavaScriptSyntacticDiagnostics15(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
function F(public p) { })TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineNonSuggestionDiagnostics(t);
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptSyntacticDiagnostics15, TestGetJavaScriptSyntacticDiagnostics15);


static void TestGetJavaScriptSyntacticDiagnostics14(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
Foo<number>();)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineNonSuggestionDiagnostics(t);
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptSyntacticDiagnostics14, TestGetJavaScriptSyntacticDiagnostics14);


static void TestGetJavaScriptSyntacticDiagnostics13(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
var v: () => number;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineNonSuggestionDiagnostics(t);
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptSyntacticDiagnostics13, TestGetJavaScriptSyntacticDiagnostics13);


static void TestGetJavaScriptSyntacticDiagnostics12(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
declare var v;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineNonSuggestionDiagnostics(t);
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptSyntacticDiagnostics12, TestGetJavaScriptSyntacticDiagnostics12);


static void TestGetJavaScriptSyntacticDiagnostics11(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
function F(): number { })TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineNonSuggestionDiagnostics(t);
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptSyntacticDiagnostics11, TestGetJavaScriptSyntacticDiagnostics11);


static void TestGetJavaScriptSyntacticDiagnostics10(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
function F<T>() { })TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineNonSuggestionDiagnostics(t);
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptSyntacticDiagnostics10, TestGetJavaScriptSyntacticDiagnostics10);


static void TestGetJavaScriptSyntacticDiagnostics02(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @allowJs: true
// @Filename: b.js
var a = "a";
var b: boolean = true;
function foo(): string { }
var var = "c";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineNonSuggestionDiagnostics(t);
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptSyntacticDiagnostics02, TestGetJavaScriptSyntacticDiagnostics02);


static void TestGetJavaScriptSyntacticDiagnostics01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @allowJs: true
// @Filename: a.js
var ===;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineNonSuggestionDiagnostics(t);
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptSyntacticDiagnostics01, TestGetJavaScriptSyntacticDiagnostics01);


static void TestGetJavaScriptGlobalCompletions1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: Foo.js
function f() {
    // helloWorld leaks from here into the global space?
    if (helloWorld) {
        return 3;
    }
    return 5;
}

hello/**/)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "helloWorld", .SortText = std::string(ls::SortTextJavascriptIdentifiers)})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptGlobalCompletions1, TestGetJavaScriptGlobalCompletions1);


static void TestGetJavaScriptCompletions_tsCheck(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
// @ts-check
interface I { a: number; b: number; }
interface J { b: number; c: number; }
declare const ij: I | J;
ij./**/)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {"b"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptCompletions_tsCheck, TestGetJavaScriptCompletions_tsCheck);


static void TestGetJavaScriptCompletions9(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: Foo.js
/**
 * @type {function(new:number)}
 */
var v;
new v()./**/)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "toExponential", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptCompletions9, TestGetJavaScriptCompletions9);


static void TestGetJavaScriptCompletions8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: Foo.js
/**
 * @type {function(): number}
 */
var v;
v()./**/)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "toExponential", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptCompletions8, TestGetJavaScriptCompletions8);


static void TestGetJavaScriptCompletions5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: Foo.js
/**
 * @template T
 * @param {T} a
 * @return {T} */
function foo(a) { }
let x = foo;
foo(1)./**/)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "toExponential", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptCompletions5, TestGetJavaScriptCompletions5);


static void TestGetJavaScriptCompletions4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: Foo.js
/** @return {number} */
function foo(a,b) { }
foo(1,2)./**/)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "toExponential", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptCompletions4, TestGetJavaScriptCompletions4);


static void TestGetJavaScriptCompletions3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: Foo.js
/** @type {Array.<number>} */
var v;
v./**/)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "concat", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptCompletions3, TestGetJavaScriptCompletions3);


static void TestGetJavaScriptCompletions2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: Foo.js
/** @type {(number|string)} */
var v;
v./**/)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "valueOf", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptCompletions2, TestGetJavaScriptCompletions2);


static void TestGetJavaScriptCompletions22(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: file.js
const abc = {};
({./*1*/});)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->Insert(t, ".");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptCompletions22, TestGetJavaScriptCompletions22);


static void TestGetJavaScriptCompletions21(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: file.js
class Prv {
    #privatething = 1;
    notSoPrivate = 1;
}
new Prv()['[|/**/|]'];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "notSoPrivate", .TextEdit = std::make_shared<lsproto::TextEditOrInsertReplaceEdit>(lsproto::TextEditOrInsertReplaceEdit{.TextEdit = std::make_shared<lsproto::TextEdit>(lsproto::TextEdit{.NewText = "notSoPrivate", .Range = f->Ranges()[0]->LSRange})})})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptCompletions21, TestGetJavaScriptCompletions21);


static void TestGetJavaScriptCompletions20(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @allowNonTsExtensions: true
// @Filename: file.js
/**
 * A person
 * @constructor
 * @param {string} name - The name of the person.
 * @param {number} age - The age of the person.
 */
function Person(name, age) {
    this.name = name;
    this.age = age;
}


Person.getName = 10;
Person.getNa/**/ = 10;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = tsu::CompletionFunctionMembersWithPrototypePlus(std::vector<fourslash::CompletionsExpectedItem>{"getName", "getNa", std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "Person", .SortText = std::string(ls::SortTextJavascriptIdentifiers)}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "name", .SortText = std::string(ls::SortTextJavascriptIdentifiers)}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "age", .SortText = std::string(ls::SortTextJavascriptIdentifiers)})})})}));
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptCompletions20, TestGetJavaScriptCompletions20);


static void TestGetJavaScriptCompletions1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: Foo.js
/** @type {number} */
var v;
v./**/)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "toExponential", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptCompletions1, TestGetJavaScriptCompletions1);


static void TestGetJavaScriptCompletions19(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: file.js
function fn() {
	if (foo) {
		return 0;
	} else {
		return '0';
	}
}
let x = fn();
if(typeof x === 'string') {
	x/*str*/
} else {
	x/*num*/
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "str");
		f->Insert(t, ".");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "substring", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)})}})}));
		f->GoToMarker(t, "num");
		f->Insert(t, ".");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "toFixed", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptCompletions19, TestGetJavaScriptCompletions19);


static void TestGetJavaScriptCompletions18(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: file.js
/**
  * @param {number} a
  * @param {string} b
*/
exports.foo = function(a, b) {
	a/*a*/;
	b/*b*/
};)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "a");
		f->Insert(t, ".");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "toFixed", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)})}})}));
		f->GoToMarker(t, "b");
		f->Insert(t, ".");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "substring", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptCompletions18, TestGetJavaScriptCompletions18);


static void TestGetJavaScriptCompletions16(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: file.js
"use strict";

class Something {

    /**
     * @param {number} a
     */
    constructor(a, b) {
        a/*body*/
    }

    /**
     * @param {number} a
     */
    method(a) {
        a/*method*/
    }
}
let x = new Something(/*sig*/);)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "body");
		f->Insert(t, ".");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "toFixed", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)})}})}));
		f->Backspace(t, 1);
		f->GoToMarker(t, "sig");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "Something(a: number, b: any): Something"});
		f->GoToMarker(t, "method");
		f->Insert(t, ".");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "toFixed", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptCompletions16, TestGetJavaScriptCompletions16);


static void TestGetJavaScriptCompletions15(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: refFile1.ts
export var V = 1;
// @Filename: refFile2.ts
export var V = "123"
// @Filename: refFile3.ts
export var V = "123"
// @Filename: main.js
import ref1 = require("./refFile1");
var ref2 = require("./refFile2");
ref1.V./*1*/;
ref2.V./*2*/;
var v = { x: require("./refFile3") };
v.x./*3*/;
v.x.V./*4*/;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {"toExponential"}})}));
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {"toLowerCase"}})}));
		f->VerifyCompletions(t, "3", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {"V", std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "ref1", .SortText = std::string(ls::SortTextJavascriptIdentifiers)}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "ref2", .SortText = std::string(ls::SortTextJavascriptIdentifiers)}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "require", .SortText = std::string(ls::SortTextJavascriptIdentifiers)}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "v", .SortText = std::string(ls::SortTextJavascriptIdentifiers)}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "x", .SortText = std::string(ls::SortTextJavascriptIdentifiers)})}})}));
		f->VerifyCompletions(t, "4", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {"toLowerCase"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptCompletions15, TestGetJavaScriptCompletions15);


static void TestGetJavaScriptCompletions14(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: file1.js
interface Number {
    toExponential(fractionDigits?: number): string;
}
var x = 1;
x./*1*/)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "toExponential", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptCompletions14, TestGetJavaScriptCompletions14);


static void TestGetJavaScriptCompletions13(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: file1.js
var file1Identifier = 1;
interface Foo { FooProp: number };
// @Filename: file2.js
var file2Identifier1 = 2;
var file2Identifier2 = 2;
/*1*/
file2Identifier2./*2*/)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {"file2Identifier1", "file2Identifier2", std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "file1Identifier", .SortText = std::string(ls::SortTextGlobalsOrKeywords)})}, .Excludes = {"FooProp"}})}));
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "file2Identifier1", .SortText = std::string(ls::SortTextJavascriptIdentifiers)}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "file2Identifier2", .SortText = std::string(ls::SortTextJavascriptIdentifiers)})}, .Excludes = {"file1Identifier", "FooProp"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptCompletions13, TestGetJavaScriptCompletions13);


static void TestGetJavaScriptCompletions12(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: Foo.js
/**
 * @param {number} input
 * @param {string} currency
 * @returns {number}
 */
var convert = function(input, currency) {
    switch(currency./*1*/) {
            case "USD":
            input./*2*/;
            case "EUR":
                return "" + rateToUsd.EUR;
            case "CNY":
                return {} + rateToUsd.CNY;
    }
}
convert(1, "")./*3*/
/**
 * @param {number} x
 */
var test1 = function(x) { return x./*4*/ }, test2 = function(a) { return a./*5*/ };)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "charCodeAt", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)})}})}));
		f->VerifyCompletions(t, std::vector<std::string>{"2", "3", "4"}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "toExponential", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)})}})}));
		f->VerifyCompletions(t, "5", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "test1", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindText), .SortText = std::string(ls::SortTextJavascriptIdentifiers)})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptCompletions12, TestGetJavaScriptCompletions12);


static void TestGetJavaScriptCompletions11(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: Foo.js
/** @type {number|string} */
var v;
v./**/)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "toExponential", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "charCodeAt", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptCompletions11, TestGetJavaScriptCompletions11);


static void TestGetJavaScriptCompletions10(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: Foo.js
/**
 * @type {function(this:number)}
 */
function f() { this./**/ })TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "toExponential", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptCompletions10, TestGetJavaScriptCompletions10);
} // namespace
