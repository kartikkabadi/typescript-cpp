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


static void TestJSDocSnippetCompletionForFunction(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*completion*/ */
function abcdef(x, y) { }
)TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = true;
		auto __fsp1 = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "completion");
		f->Insert(t, "/**");
		auto list = f->GetCompletions(t, nullptr);
		tsc::gotest::assert::Assert(t, list != nullptr);
		tsc::gotest::assert::Equal(t, (int)list->Items->size(), 1);
		auto item = (*list->Items)[0];
		tsc::gotest::assert::Equal(t, item->Label, "/** */");
		fourslash::assertDeepEqual(t, item->Kind, std::make_shared<std::decay_t<decltype(lsproto::CompletionItemKindText)>>(lsproto::CompletionItemKindText), "DeepEqual mismatch");
		fourslash::assertDeepEqual(t, item->Detail, std::make_shared<std::string>("JSDoc comment"), "DeepEqual mismatch");
		fourslash::assertDeepEqual(t, item->SortText, std::make_shared<std::string>("\x00"), "DeepEqual mismatch");
		fourslash::assertDeepEqual(t, item->CommitCharacters, std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), "DeepEqual mismatch");
		fourslash::assertDeepEqual(t, item->InsertTextFormat, std::make_shared<std::decay_t<decltype(lsproto::InsertTextFormatSnippet)>>(lsproto::InsertTextFormatSnippet), "DeepEqual mismatch");
		tsc::gotest::assert::Assert(t, item->TextEdit != nullptr);
		tsc::gotest::assert::Assert(t, item->TextEdit->InsertReplaceEdit != nullptr);
		tsc::gotest::assert::Equal(t, item->TextEdit->InsertReplaceEdit->NewText, R"TS(/**
 * $0
 * @param x ${1}
 * @param y ${2}
 */)TS");
		fourslash::assertDeepEqual(t, item->TextEdit->InsertReplaceEdit->Insert, lsproto::Range{.Start = lsproto::Position{.Line = 0, .Character = 0}, .End = lsproto::Position{.Line = 0, .Character = 6}}, "DeepEqual mismatch");
		fourslash::assertDeepEqual(t, item->TextEdit->InsertReplaceEdit->Replace, lsproto::Range{.Start = lsproto::Position{.Line = 0, .Character = 0}, .End = lsproto::Position{.Line = 0, .Character = 6}}, "DeepEqual mismatch");
	});
}
REGISTER_FOURSLASH_TEST(TestJSDocSnippetCompletionForFunction, TestJSDocSnippetCompletionForFunction);

static void TestJSDocSnippetCompletionForReturn(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*completion*/ */
function abcdef(x) { return x; }
)TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = true;
		auto __fsp1 = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "completion");
		f->Insert(t, "/**");
		auto list = f->GetCompletions(t, nullptr);
		tsc::gotest::assert::Assert(t, list != nullptr);
		tsc::gotest::assert::Equal(t, (int)list->Items->size(), 1);
		tsc::gotest::assert::Equal(t, (*list->Items)[0]->TextEdit->InsertReplaceEdit->NewText, R"TS(/**
 * $0
 * @param x ${1}
 * @returns ${2}
 */)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestJSDocSnippetCompletionForReturn, TestJSDocSnippetCompletionForReturn);

static void TestJSDocSnippetCompletionPreservesCRLF(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*completion*/ */
function abcdef(x) { return x; }
)TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = true;
		auto __fsp1 = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "completion");
		f->Insert(t, "/**");
		auto userPreferences = lsutil::NewDefaultUserPreferences();
		userPreferences.FormatCodeSettings.NewLineCharacter = R"TS(
)TS";
		f->Configure(t, userPreferences);
		auto list = f->GetCompletions(t, nullptr);
		tsc::gotest::assert::Assert(t, list != nullptr);
		tsc::gotest::assert::Equal(t, (int)list->Items->size(), 1);
		tsc::gotest::assert::Equal(t, (*list->Items)[0]->TextEdit->InsertReplaceEdit->NewText, R"TS(/**
 * $0
 * @param x ${1}
 * @returns ${2}
 */)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestJSDocSnippetCompletionPreservesCRLF, TestJSDocSnippetCompletionPreservesCRLF);

static void TestJSDocSnippetCompletionRespectsGenerateReturnPreference(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*completion*/ */
function abcdef(x) { return x; }
)TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = true;
		auto __fsp1 = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "completion");
		f->Insert(t, "/**");
		auto userPreferences = lsutil::NewDefaultUserPreferences();
		userPreferences.GenerateReturnInDocTemplate = Tristate::False;
		auto list = f->GetCompletions(t, std::make_shared<lsutil::UserPreferences>(userPreferences));
		tsc::gotest::assert::Assert(t, list != nullptr);
		tsc::gotest::assert::Equal(t, (int)list->Items->size(), 1);
		tsc::gotest::assert::Equal(t, (*list->Items)[0]->TextEdit->InsertReplaceEdit->NewText, R"TS(/**
 * $0
 * @param x ${1}
 */)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestJSDocSnippetCompletionRespectsGenerateReturnPreference, TestJSDocSnippetCompletionRespectsGenerateReturnPreference);

static void TestJSDocSnippetCompletionRespectsEnabledPreference(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*completion*/ */
function abcdef(x) { }
)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "completion");
		f->Insert(t, "/**");
		auto userPreferences = lsutil::NewDefaultUserPreferences();
		userPreferences.EnableJSDocCompletions = Tristate::False;
		auto list = f->GetCompletions(t, std::make_shared<lsutil::UserPreferences>(userPreferences));
		if (list != nullptr) {
			for (auto item : *list->Items) {
				tsc::gotest::assert::Assert(t, item->Label != "/** */");
			}
		}
	});
}
REGISTER_FOURSLASH_TEST(TestJSDocSnippetCompletionRespectsEnabledPreference, TestJSDocSnippetCompletionRespectsEnabledPreference);

static void TestJSDocSnippetCompletionForClass(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*completion*/
class C {
}
)TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->TextDocument->Completion->CompletionItem->SnippetSupport = true;
		auto __fsp1 = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "completion");
		f->Insert(t, "/**");
		auto list = f->GetCompletions(t, nullptr);
		tsc::gotest::assert::Assert(t, list != nullptr);
		tsc::gotest::assert::Equal(t, (int)list->Items->size(), 1);
		tsc::gotest::assert::Equal(t, (*list->Items)[0]->TextEdit->InsertReplaceEdit->NewText, R"TS(/**
 * $0
 */)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestJSDocSnippetCompletionForClass, TestJSDocSnippetCompletionForClass);

static void TestJSDocSnippetCompletionNotInNonEmptyComment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** text /*completion*/ */
function abcdef(x) { }
)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "completion", nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestJSDocSnippetCompletionNotInNonEmptyComment, TestJSDocSnippetCompletionNotInNonEmptyComment);


static void TestJsdocTypedefTag(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @allowNonTsExtensions: true
// @Filename: jsdocCompletion_typedef.js
/** @typedef {(string | number)} NumberLike */

/**
 * @typedef Animal - think Giraffes
 * @type {Object}
 * @property {string} animalName
 * @property {number} animalAge
 */

/**
 * @typedef {Object} Person
 * @property {string} personName
 * @property {number} personAge
 */

/**
 * @typedef {Object}
 * @property {string} catName
 * @property {number} catAge
 */
var Cat;

/** @typedef {{ dogName: string, dogAge: number }} */
var Dog;

/** @type {NumberLike} */
var numberLike; numberLike./*numberLike*/

/** @type {Person} */
var p;p./*person*/;
p.personName./*personName*/;
p.personAge./*personAge*/;

/** @type {/*AnimalType*/Animal} */
var a;a./*animal*/;
a.animalName./*animalName*/;
a.animalAge./*animalAge*/;

/** @type {Cat} */
var c;c./*cat*/;
c.catName./*catName*/;
c.catAge./*catAge*/;

/** @type {Dog} */
var d;d./*dog*/;
d.dogName./*dogName*/;
d.dogAge./*dogAge*/;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyCompletions(t, "numberLike", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {"charAt", "toExponential"}})}));
		f->VerifyCompletions(t, "person", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {"personName", "personAge"}})}));
		f->VerifyCompletions(t, "personName", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {"charAt"}})}));
		f->VerifyCompletions(t, "personAge", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {"toExponential"}})}));
		f->VerifyCompletions(t, "animal", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {"animalName", "animalAge"}})}));
		f->VerifyCompletions(t, "animalName", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {"charAt"}})}));
		f->VerifyCompletions(t, "animalAge", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {"toExponential"}})}));
		f->VerifyCompletions(t, "dog", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {"dogName", "dogAge"}})}));
		f->VerifyCompletions(t, "dogName", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {"charAt"}})}));
		f->VerifyCompletions(t, "dogAge", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {"toExponential"}})}));
		f->VerifyCompletions(t, "cat", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {"catName", "catAge"}})}));
		f->VerifyCompletions(t, "catName", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {"charAt"}})}));
		f->VerifyCompletions(t, "catAge", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {"toExponential"}})}));
		f->VerifyQuickInfoAt(t, "AnimalType", R"TS(type Animal = {
    animalName: string;
    animalAge: number;
})TS", "- think Giraffes");
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocTypedefTag, TestJsdocTypedefTag);


static void TestJsdocTypedefTagTypeExpressionCompletion(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
interface I {
    age: number;
}
 class Foo {
     property1: string;
     constructor(value: number) { this.property1 = "hello"; }
     static method1() {}
     method3(): number { return 3; }
     /**
      * @param {string} foo A value.
      * @returns {number} Another value
      * @mytag
      */
     method4(foo: string) { return 3; }
 }
 namespace Foo.Namespace { export interface SomeType { age2: number } }
 /**
  * @type { /*type1*/Foo./*typeFooMember*/Namespace./*NamespaceMember*/SomeType }
  */
var x;
/*globalValue*/
x./*valueMemberOfSomeType*/
var x1: Foo;
x1./*valueMemberOfFooInstance*/;
Foo./*valueMemberOfFoo*/;
 /**
  * @type { {/*propertyName*/ageX: number} }
  */
var y;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "type1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "Foo", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass)}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "I", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface)})}, .Excludes = {"Namespace", "SomeType", "x", "x1", "y", "method1", "property1", "method3", "method4", "foo"}})}));
		f->VerifyCompletions(t, "typeFooMember", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "Namespace", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindModule)})}})}));
		f->VerifyCompletions(t, "NamespaceMember", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "SomeType", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindInterface)})}})}));
		f->VerifyCompletions(t, "globalValue", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "Foo", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindClass)}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "x", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable)}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "x1", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable)}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "y", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindVariable)})}, .Excludes = {"I", "Namespace", "SomeType", "method1", "property1", "method3", "method4", "foo"}})}));
		f->VerifyCompletions(t, "valueMemberOfSomeType", nullptr);
		f->VerifyCompletions(t, "valueMemberOfFooInstance", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "method3", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "method4", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "property1", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindField)})}})}));
		f->VerifyCompletions(t, "valueMemberOfFoo", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = tsu::CompletionFunctionMembersPlus(std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "method1", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod), .SortText = std::string(ls::SortTextLocalDeclarationPriority)}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "prototype", .SortText = std::string(ls::SortTextLocationPriority)})})})}));
		f->VerifyCompletions(t, "propertyName", nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocTypedefTagTypeExpressionCompletion, TestJsdocTypedefTagTypeExpressionCompletion);



static void TestJsdocTypedefTagSemanticMeaning1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
/** @typedef {number} */
/*1*/const /*2*/T = 1;
/** @type {/*3*/T} */
const n = /*4*/T;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocTypedefTagSemanticMeaning1, TestJsdocTypedefTagSemanticMeaning1);


static void TestJsdocTypedefTagSemanticMeaning0(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
/** /*1*/@typedef {number} /*2*/T */
/*3*/const /*4*/T = 1;
/** @type {/*5*/T} */
const n = /*6*/T;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5", "6"});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocTypedefTagSemanticMeaning0, TestJsdocTypedefTagSemanticMeaning0);


static void TestJsdocTypedefTagRename04(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @allowNonTsExtensions: true
// @Filename: jsDocTypedef_form2.js

function test1() {
   /** @typedef {(string | number)} NumberLike */

   /** @type {/*1*/NumberLike} */
   var numberLike;
}
function test2() {
   /** @typedef {(string | number)} NumberLike2 */

   /** @type {NumberLike2} */
   var n/*2*/umberLike2;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "2");
		f->VerifyQuickInfoExists(t);
		f->GoToMarker(t, "1");
		f->Insert(t, "111");
		f->GoToMarker(t, "2");
		f->VerifyQuickInfoExists(t);
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocTypedefTagRename04, TestJsdocTypedefTagRename04);


static void TestJsdocTypedefTagRename03(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @allowNonTsExtensions: true
// @Filename: jsDocTypedef_form3.js

/**
 * [|@typedef /*1*/[|{| "contextRangeIndex": 0 |}Person|]
 * @type {Object}
 * @property {number} age
 * @property {string} name
 |]*/

/** @type {/*2*/[|Person|]} */
var person;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToFile(t, "jsDocTypedef_form3.js");
		f->VerifyBaselineRename(t, nullptr, asMonVec(tsu::ToAny(f->GetRangesByText()->Get("Person"))));
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocTypedefTagRename03, TestJsdocTypedefTagRename03);




static void TestJsdocTypedefTagNavigateTo(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @allowNonTsExtensions: true
// @Filename: jsDocTypedef_form2.js

/** @typedef {(string | number)} NumberLike */
/** @typedef {(string | number | string[])} */
var NumberLike2;

/** @type {/*1*/NumberLike} */
var numberLike;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocTypedefTagNavigateTo, TestJsdocTypedefTagNavigateTo);


static void TestJsdocTypedefTagNamespace(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @allowNonTsExtensions: true
// @Filename: jsdocCompletion_typedef.js
/**
 * @typedef {string | number} T.NumberLike
 * @typedef {{age: number}} T.People
 * @typedef {string | number} T.O.Q.NumberLike
 * @type {T.NumberLike}
 */
var x; x./*1*/;
/** @type {T.O.Q.NumberLike} */
var x1; x1./*2*/;
/** @type {T.People} */
var x1; x1./*3*/;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyCompletions(t, std::vector<std::string>{"1", "3"}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {"charAt", "toExponential"}})}));
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "age", .SortText = std::string(ls::SortTextJavascriptIdentifiers)})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocTypedefTagNamespace, TestJsdocTypedefTagNamespace);


static void TestJsdocTypedefTagGoToDefinition(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @allowNonTsExtensions: true
// @Filename: jsdocCompletion_typedef.js
/**
 * @typedef {Object} Person
 * @property {string} /*1*/personName
 * @property {number} personAge
 */

/**
 * @typedef {{ /*2*/animalName: string, animalAge: number }} Animal
 */

/** @type {Person} */
var person; person.[|personName/*3*/|]

/** @type {Animal} */
var animal; animal.[|animalName/*4*/|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineGoToDefinition(t, true, {"3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocTypedefTagGoToDefinition, TestJsdocTypedefTagGoToDefinition);


static void TestJsdocTypedefTag2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @allowNonTsExtensions: true
// @Filename: jsdocCompletion_typedef.js
/**
 * @typedef {Object} A.B.MyType
 * @property {string} yes
 */
function foo() {}
/**
 * @param {A.B.MyType} my2
 */
function a(my2) {
    my2.yes./*1*/
}
/**
 * @param {MyType} my2
 */
function b(my2) {
    my2.yes./*2*/
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {"charAt"}})}));
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Excludes = {"charAt"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocTypedefTag2, TestJsdocTypedefTag2);


static void TestJsdocTypedefTag1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es2015
// @allowNonTsExtensions: true
// @Filename: jsdocCompletion_typedef.js
/**
 * @typedef {Object} MyType
 * @property {string} yes
 */
function foo() { }
/**
 * @param {MyType} my
 */
function a(my) {
    my.yes./*1*/
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {"charAt"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocTypedefTag1, TestJsdocTypedefTag1);


static void TestJsdocThrowsTag_rename(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class /**/E extends Error {}
/**
 * @throws {E}
 */
function f() {})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocThrowsTag_rename, TestJsdocThrowsTag_rename);


static void TestJsdocThrowsTag_findAllReferences(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class /**/E extends Error {}
/**
 * @throws {E}
 */
function f() {})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocThrowsTag_findAllReferences, TestJsdocThrowsTag_findAllReferences);


static void TestJsdocThrowsTagCompletion(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
/**
 * @throws {/**/} description
 */
function fn() {})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = tsu::CompletionGlobalTypes})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocThrowsTagCompletion, TestJsdocThrowsTagCompletion);


static void TestJsdocTemplateTagCompletion(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
/**
 * @template {/**/} T
 * @typedef {Object} Foo
 * @property {T} foo
 */)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = tsu::CompletionGlobalTypes})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocTemplateTagCompletion, TestJsdocTemplateTagCompletion);


static void TestJsdocTemplatePrototypeCompletions(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @checkJs: true
// @filename: index.js
https://github.com/microsoft/TypeScript/issues/11492
/** @constructor */
function Foo() {}
/**
 * @template T
 * @param {T} bar
 * @returns {T}
 */
Foo.prototype.foo = function (bar) {};
new Foo().foo({ id: 1234 })./**/)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {"id"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocTemplatePrototypeCompletions, TestJsdocTemplatePrototypeCompletions);


static void TestJsdocSatisfiesTagRename(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noEmit: true
// @allowJS: true
// @checkJs: true
// @filename: /a.js
/**
 * @typedef {Object} T
 * @property {number} a
 */

/** @satisfies {/**/T} comment */
const foo = { a: 1 };)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocSatisfiesTagRename, TestJsdocSatisfiesTagRename);


static void TestJsdocSatisfiesTagFindAllReferences(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noEmit: true
// @allowJS: true
// @checkJs: true
// @filename: /a.js
/**
 * @typedef {Object} T
 * @property {number} a
 */

/** @satisfies {/**/T} comment */
const foo = { a: 1 };)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocSatisfiesTagFindAllReferences, TestJsdocSatisfiesTagFindAllReferences);


static void TestJsdocSatisfiesTagCompletion2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noEmit: true
// @allowJS: true
// @checkJs: true
// @filename: /a.js
/**
 * @/**/
 */
const t = { a: 1 };)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {"satisfies"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocSatisfiesTagCompletion2, TestJsdocSatisfiesTagCompletion2);


static void TestJsdocSatisfiesTagCompletion1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @noEmit: true
// @allowJS: true
// @checkJs: true
// @filename: /a.js
/**
 * @satisfies {/**/}
 */
const t = { a: 1 };)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = tsu::CompletionGlobalTypes})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocSatisfiesTagCompletion1, TestJsdocSatisfiesTagCompletion1);


static void TestJsdocReturnsTag(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: dummy.js
/**
 * Find an item
 * @template T
 * @param {T[]} l
 * @param {T} x
 * @returns {?T}  The names of the found item(s).
 */
function find(l, x) {
}
find(''/**/);)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocReturnsTag, TestJsdocReturnsTag);


static void TestJsdocReturnsTagVS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: dummy.js
/**
 * Find an item
 * @template T
 * @param {T[]} l
 * @param {T} x
 * @returns {?T}  The names of the found item(s).
 */
function find(l, x) {
}
find(''/**/);)TS";
		auto __fsp1 = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = true}), content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocReturnsTagVS, TestJsdocReturnsTagVS);


static void TestJsdocPropertyTagCompletion(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
/**
 * @typedef {Object} Foo
 * @property {/**/}
 */)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = tsu::CompletionGlobalTypes})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocPropertyTagCompletion, TestJsdocPropertyTagCompletion);


static void TestJsdocPropTagCompletion(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/**
 * @typedef Foo
 * @pr/**/
 */)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {"prop"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocPropTagCompletion, TestJsdocPropTagCompletion);


static void TestJsdocParameterNameCompletion(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/**
 * @param /*0*/
 */
function f(foo, bar) {}
/**
 * @param foo
 * @param /*1*/
 */
function g(foo, bar) {}
/**
 * @param can/*2*/
 * @param cantaloupe
 */
function h(cat, canary, canoodle, cantaloupe, zebra) {}
/**
 * @param /*3*/ {string} /*4*/
 */
function i(foo, bar) {})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, std::vector<std::string>{"0", "3", "4"}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {"bar", "foo"}})}));
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {"bar"}})}));
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {"canary", "canoodle"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocParameterNameCompletion, TestJsdocParameterNameCompletion);


static void TestJsdocParam_suggestion1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a.ts
/**
 * @param options - whatever
 * @param options.zone - equally bad
 */
declare function bad(options: any): void

/**
 * @param {number} obtuse
 */
function worse(): void {
    arguments
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToFile(t, "a.ts");
		f->VerifySuggestionDiagnostics(t, {});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocParam_suggestion1, TestJsdocParam_suggestion1);


static void TestJsdocParamTagSpecialKeywords(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @allowNonTsExtensions: true
// @Filename: test.js
/**
 * @param {string} type
 */
function test(type) {
    type./**/
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {"charAt"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocParamTagSpecialKeywords, TestJsdocParamTagSpecialKeywords);


static void TestJsdocOverloadTagCompletion(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJS: true
// @checkJs: true
// @filename: /a.js
/**
 * @/**/
 */)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {"overload"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocOverloadTagCompletion, TestJsdocOverloadTagCompletion);


static void TestJsdocOnInheritedMembers2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @filename: /a.js
/** @template T */
class A {
    /** Method documentation. */
    method() {}
}

/** @extends {A<number>} */
const B = class extends A {
    method() {}
}

const b = new B();
b.method/**/;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocOnInheritedMembers2, TestJsdocOnInheritedMembers2);


static void TestJsdocOnInheritedMembers1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @filename: /a.js
/** @template T */
class A {
    /** Method documentation. */
    method() {}
}

/** @extends {A<number>} */
class B extends A {
    method() {}
}

const b = new B();
b.method/**/;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocOnInheritedMembers1, TestJsdocOnInheritedMembers1);


static void TestJsdocNullableUnion(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @checkJs: true
// @Filename: Foo.js
/**
 * @param {never | {x: string}} p1
 * @param {undefined | {y: number}} p2
 * @param {null | {z: boolean}} p3
 * @returns {void} nothing
 */
function f(p1, p2, p3) {
    p1./*1*/;
    p2./*2*/;
    p3./*3*/;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {"x"}})}));
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {"y"}})}));
		f->VerifyCompletions(t, "3", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = {"z"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocNullableUnion, TestJsdocNullableUnion);


static void TestJsdocLink_rename1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface A/**/ {}
/**
 * {@link A()} is ok
 */
declare const a: A)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocLink_rename1, TestJsdocLink_rename1);


static void TestJsdocLink_findAllReferences1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface A/**/ {}
/**
 * {@link A()} is ok
 */
declare const a: A)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocLink_findAllReferences1, TestJsdocLink_findAllReferences1);


static void TestJsdocLink6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: /a.ts
export default function A() { }
export function B() { };
// @Filename: /b.ts
import A, { B } from "./a";
/**
 * {@link A}
 * {@link B}
 */
export default function /**/f() { })TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocLink6, TestJsdocLink6);


static void TestJsdocLink5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function g() { }
/**
 * {@link g()} {@link g() } {@link g ()} {@link g () 0} {@link g()1} {@link g() 2}
 * {@link u()} {@link u() } {@link u ()} {@link u () 0} {@link u()1} {@link u() 2}
 */
function f(x) {
}
f/*3*/())TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocLink5, TestJsdocLink5);


static void TestJsdocLink4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare class I {
  /** {@link I} */
  bar/*1*/(): void
}
/** {@link I} */
var n/*2*/ = 1
/**
 * A real, very serious {@link I to an interface}. Right there.
 * @param x one {@link Pos here too}
 */
function f(x) {
}
f/*3*/()
type Pos = [number, number])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocLink4, TestJsdocLink4);


static void TestJsdocLink3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /jsdocLink3.ts
export class C {
}
// @Filename: /module1.ts
import { C } from './jsdocLink3'
/**
 * {@link C}
 * @wat Makes a {@link C}. A default one.
 * {@link C()}
 * {@link C|postfix text}
 * {@link unformatted postfix text}
 * @see {@link C} its great
 */
function /**/CC() {
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocLink3, TestJsdocLink3);


static void TestJsdocLink2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: jsdocLink2.ts
class C {
}
// @Filename: script.ts
/**
 * {@link C}
 * @wat Makes a {@link C}. A default one.
 * {@link C()}
 * {@link C|postfix text}
 * {@link unformatted postfix text}
 * @see {@link C} its great
 */
function /**/CC() {
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocLink2, TestJsdocLink2);


static void TestJsdocLink1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
}
/**
 * {@link C}
 * @wat Makes a {@link C}. A default one.
 * {@link C()}
 * {@link C|postfix text}
 * {@link unformatted postfix text}
 * @see {@link C} its great
 */
function /**/CC() {
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocLink1, TestJsdocLink1);


static void TestJsdocImportTagCompletion1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJS: true
// @checkJs: true
// @filename: /a.js
/**
 * @/**/
 */)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {"import"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocImportTagCompletion1, TestJsdocImportTagCompletion1);


static void TestJsdocImplementsTagCompletion(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
/** @implements {/**/} */
class A {})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = tsu::CompletionGlobalTypesPlus(std::vector<fourslash::CompletionsExpectedItem>{"A"})})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocImplementsTagCompletion, TestJsdocImplementsTagCompletion);


static void TestJsdocExtendsTagCompletion(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
/** @extends {/**/} */
class A {})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = tsu::CompletionGlobalTypesPlus(std::vector<fourslash::CompletionsExpectedItem>{"A"})})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocExtendsTagCompletion, TestJsdocExtendsTagCompletion);


static void TestJsdocDeprecated_suggestion9(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: first.ts
export class logger { }
// @Filename: second.ts
import { logger } from './first';
new logger())TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToFile(t, "second.ts");
		f->VerifyNoErrors(t);
		f->VerifySuggestionDiagnostics(t, {});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocDeprecated_suggestion9, TestJsdocDeprecated_suggestion9);


static void TestJsdocDeprecated_suggestion8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: first.ts
/** @deprecated */
export declare function tap<T>(next: null): void;
export declare function tap<T>(next: T): T;
// @Filename: second.ts
import { tap } from './first';
tap)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToFile(t, "second.ts");
		f->VerifyNoErrors(t);
		f->VerifySuggestionDiagnostics(t, {});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocDeprecated_suggestion8, TestJsdocDeprecated_suggestion8);


static void TestJsdocDeprecated_suggestion7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(enum Direction {
    Left = -1,
    Right = 1,
}
type T = Direction.Left
/** @deprecated */
const x = 1
type x = string
var y: x = 'hi')TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifySuggestionDiagnostics(t, {});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocDeprecated_suggestion7, TestJsdocDeprecated_suggestion7);


static void TestJsdocDeprecated_suggestion6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a.tsx
/** @deprecated */
type Props = {}
/** @deprecated */
const Component = (props: [|Props|]) => props && <div />;
<[|Component|] old="old" new="new" />
/** @deprecated */
type Options = {}
/** @deprecated */
const deprecatedFunction = (options: [|Options|]) => { options }
[|deprecatedFunction|]({});)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToFile(t, "a.tsx");
		f->VerifySuggestionDiagnostics(t, {std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'Props' is deprecated.")}, .Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Range = f->Ranges()[0]->LSRange, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated})}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'Component' is deprecated.")}, .Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Range = f->Ranges()[1]->LSRange, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated})}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'Options' is deprecated.")}, .Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Range = f->Ranges()[2]->LSRange, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated})}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'deprecatedFunction' is deprecated.")}, .Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Range = f->Ranges()[3]->LSRange, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated})})});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocDeprecated_suggestion6, TestJsdocDeprecated_suggestion6);


static void TestJsdocDeprecated_suggestion5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @checkJs: true
// @allowJs: true
// @Filename: jsdocDeprecated_suggestion5.js
/** @typedef {{ email: string, nickName?: string }} U2 */
/** @type {U2} */
const u2 = { email: "" }
/**
 * @callback K
 * @param {any} ctx
 * @return {void}
 */
/** @type {K} */
const cc = _k => {}
/** @enum {number} */
const DOOM = { e: 1, m: 1 }
/** @type {DOOM} */
const kneeDeep = DOOM.e)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifySuggestionDiagnostics(t, {});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocDeprecated_suggestion5, TestJsdocDeprecated_suggestion5);


static void TestJsdocDeprecated_suggestion4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @jsx: preserve
// @filename: a.tsx
interface Props {
    /** @deprecated */
    x: number
    y: number
}
function A(props: Props) {
    return <div>{props.y}</div>
}
function B() {
    return <A [|x|]={1} y={1} />
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToFile(t, "a.tsx");
		f->VerifySuggestionDiagnostics(t, {std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'x' is deprecated.")}, .Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Range = f->Ranges()[0]->LSRange, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated})})});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocDeprecated_suggestion4, TestJsdocDeprecated_suggestion4);


static void TestJsdocDeprecated_suggestion3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface RequestOptions {
    /** @deprecated use signal instead */
    timeout?: number;
}
declare function request(url: string, opts: RequestOptions): void;

request("/api", { [|timeout|]: 5000 });
declare const opts: RequestOptions;
opts.[|timeout|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifySuggestionDiagnostics(t, {std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'timeout' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[0]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'timeout' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[1]->LSRange})});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocDeprecated_suggestion3, TestJsdocDeprecated_suggestion3);


static void TestJsdocDeprecated_suggestion2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// overloads
declare function foo(a: string): number;
/** @deprecated */
declare function foo(): undefined;
declare function foo (a?: string): number | undefined;
[|foo|]();
foo('');
foo;
/** @deprecated */
declare function bar(): number;
[|bar|]();
[|bar|];
/** @deprecated */
declare function baz(): number;
/** @deprecated */
declare function baz(): number | undefined;
[|baz|]();
[|baz|];
interface Foo {
    /** @deprecated */
    (): void
    (a: number): void
}
declare const f: Foo;
[|f|]();
f(1);
interface T {
    createElement(): void
    /** @deprecated */
    createElement(tag: 'xmp'): void;
}
declare const t: T;
t.createElement();
t.[|createElement|]('xmp');
declare class C {
    /** @deprecated */
    constructor ();
    constructor(v: string)
}
C;
const c = new [|C|]();
interface Ca {
    /** @deprecated */
    (): void
    new (): void
}
interface Cb {
    (): void
    /** @deprecated */
    new (): string
}
declare const ca: Ca;
declare const cb: Cb;
ca;
cb;
[|ca|]();
cb();
new ca();
new [|cb|]();)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifySuggestionDiagnostics(t, {std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("The signature '(): undefined' of 'foo' is deprecated.")}, .Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6387))}), .Range = f->Ranges()[0]->LSRange, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated})}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("The signature '(): number' of 'bar' is deprecated.")}, .Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6387))}), .Range = f->Ranges()[1]->LSRange, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated})}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'bar' is deprecated.")}, .Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Range = f->Ranges()[2]->LSRange, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated})}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("The signature '(): number' of 'baz' is deprecated.")}, .Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6387))}), .Range = f->Ranges()[3]->LSRange, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated})}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'baz' is deprecated.")}, .Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Range = f->Ranges()[4]->LSRange, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated})}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("The signature '(): void' of 'f' is deprecated.")}, .Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6387))}), .Range = f->Ranges()[5]->LSRange, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated})}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>(R"TS(The signature '(tag: "xmp"): void' of 't.createElement' is deprecated.)TS")}, .Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6387))}), .Range = f->Ranges()[6]->LSRange, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated})}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("The signature 'new (): C' of 'C' is deprecated.")}, .Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6387))}), .Range = f->Ranges()[7]->LSRange, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated})}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("The signature '(): void' of 'ca' is deprecated.")}, .Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6387))}), .Range = f->Ranges()[8]->LSRange, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated})}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("The signature 'new (): string' of 'cb' is deprecated.")}, .Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6387))}), .Range = f->Ranges()[9]->LSRange, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated})})});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocDeprecated_suggestion2, TestJsdocDeprecated_suggestion2);


static void TestJsdocDeprecated_suggestion22(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: /a.ts
const foo: {
    /**
	 * @deprecated
	 */
	(a: string, b: string): string;
	(a: string, b: number): string;
} = (a: string, b: string | number) => a + b;

[|foo|](1, 1);)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifySuggestionDiagnostics(t, {});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocDeprecated_suggestion22, TestJsdocDeprecated_suggestion22);


static void TestJsdocDeprecated_suggestion21(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: esnext
// @filename: /a.ts
export const a = 1;
export const b = 1;
// @filename: /b.ts
export {
    /** @deprecated a is deprecated */
    a
} from "./a";
// @filename: /c.ts
export {
    a
} from "./b";
// @filename: /d.ts
import * as _ from "./c";
_.[|a|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToFile(t, "/d.ts");
		f->VerifySuggestionDiagnostics(t, {std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'a' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[0]->LSRange})});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocDeprecated_suggestion21, TestJsdocDeprecated_suggestion21);


static void TestJsdocDeprecated_suggestion20(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: esnext
// @filename: /a.ts
export default function a() {}
// @filename: /b.ts
import _a from "./a";
export {
	/** @deprecated a is deprecated */
	_a as a,
};
/** @deprecated b is deprecated */
export const b = (): void => {};
// @filename: /c.ts
import * as _ from "./b";

_.[|a|]()
_.[|b|]())TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToFile(t, "/c.ts");
		f->VerifySuggestionDiagnostics(t, {std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'a' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[0]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'b' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[1]->LSRange})});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocDeprecated_suggestion20, TestJsdocDeprecated_suggestion20);


static void TestJsdocDeprecated_suggestion1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @experimentalDecorators: true
// @Filename: a.ts
export namespace foo {
    /** @deprecated */
    export function faff () { }
    [|faff|]()
}
const [|a|] = foo.[|faff|]()
foo[[|"faff"|]]
const { [|faff|] } = foo
[|faff|]()
/** @deprecated */
export function bar () {
    foo?.[|faff|]()
}
foo?.[[|"faff"|]]?.()
[|bar|]();
/** @deprecated */
export interface Foo {
    /** @deprecated */
    zzz: number
}
/** @deprecated */
export type QW = [|Foo|][[|"zzz"|]]
export type WQ = [|QW|]
class C {
    /** @deprecated */
    constructor() {
    }
    /** @deprecated */
    m() { }
}
/** @deprecated */
class D {
    constructor() {
    }
}
var c = new [|C|]()
c.[|m|]()
c.[|m|]
new [|D|]()
C
[|D|]
// @Filename: j.tsx
type Props = { someProp?: any }
declare var props: Props
/** @deprecated */
function Compi(_props: Props) {
    return <div></div>
}
[|Compi|];
<[|Compi|] />;
<[|Compi|] {...props}><div></div></[|Compi|]>;
/** @deprecated */
function ttf(_x: unknown) {
}
[|ttf|]``
[|ttf|]
/** @deprecated */
function dec(_c: unknown) { }
[|dec|]
@[|dec|]
class K { }
// @Filename: b.ts
// imports and aliases
import * as f from './a';
import { [|bar|], [|QW|] } from './a';
f.[|bar|]();
f.foo.[|faff|]();
[|bar|]();
type Z = [|QW|];
type A = f.[|Foo|];
type B = f.[|QW|];
type C = f.WQ;
type [|O|] = Z | A | B | C;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToFile(t, "a.ts");
		f->VerifySuggestionDiagnostics(t, {std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6387))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("The signature '(): void' of 'faff' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[0]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6133))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'a' is declared but its value is never read.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagUnnecessary}), .Range = f->Ranges()[1]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6387))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("The signature '(): void' of 'foo.faff' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[2]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'faff' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[3]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'faff' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[4]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6387))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("The signature '(): void' of 'faff' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[5]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6387))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("The signature '(): void' of 'foo.faff' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[6]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6387))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("The signature '(): void' of 'foo.faff' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[7]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6387))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("The signature '(): void' of 'bar' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[8]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'Foo' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[9]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'zzz' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[10]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'QW' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[11]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6387))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("The signature 'new (): C' of 'C' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[12]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6387))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("The signature '(): void' of 'c.m' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[13]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'m' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[14]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'D' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[15]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'D' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[16]->LSRange})});
		f->GoToFile(t, "j.tsx");
		f->VerifySuggestionDiagnostics(t, {std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'Compi' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[17]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6387))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("The signature '(_props: Props): any' of 'Compi' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[18]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6387))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("The signature '(_props: Props): any' of 'Compi' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[19]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'Compi' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[20]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6387))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("The signature '(_x: unknown): void' of 'ttf' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[21]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'ttf' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[22]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'dec' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[23]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6387))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("The signature '(_c: unknown): void' of 'dec' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[24]->LSRange})});
		f->GoToFile(t, "b.ts");
		f->VerifySuggestionDiagnostics(t, {std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'bar' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[25]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'QW' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[26]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6387))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("The signature '(): void' of 'f.bar' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[27]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6387))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("The signature '(): void' of 'f.foo.faff' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[28]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6387))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("The signature '(): void' of 'bar' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[29]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'QW' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[30]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'Foo' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[31]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'QW' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[32]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6196))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'O' is declared but never used.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagUnnecessary}), .Range = f->Ranges()[33]->LSRange})});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocDeprecated_suggestion1, TestJsdocDeprecated_suggestion1);


static void TestJsdocDeprecated_suggestion19(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    x: number;
    y: number;
}
interface I {
    /** @deprecated  */
    x: number;
}
const foo: I = { [|x|]: 1, y: 1 };
foo.[|x|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifySuggestionDiagnostics(t, {std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'x' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[0]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'x' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[1]->LSRange})});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocDeprecated_suggestion19, TestJsdocDeprecated_suggestion19);


static void TestJsdocDeprecated_suggestion18(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @jsx: preserve
// @filename: foo.tsx
interface Props {
    /** @deprecated  */
    x: number;
    y: number;
}
function A(props: Props) {
    return <div>{props.y}</div>
}
function B() {
    return <A [|x|]={1} [|y|]={1} />
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifySuggestionDiagnostics(t, {std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'x' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[0]->LSRange})});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocDeprecated_suggestion18, TestJsdocDeprecated_suggestion18);


static void TestJsdocDeprecated_suggestion17(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: foo.ts
interface Foo {
    /** @deprecated */
    [k: string]: any;
    /** @deprecated please use `.y` instead  */
    x: number;
    y: number;
}
function f(foo: Foo) {
    foo.[|x|];
    foo.[|y|];
    foo.[|z|];
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifySuggestionDiagnostics(t, {std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'x' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[0]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'z' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[2]->LSRange})});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocDeprecated_suggestion17, TestJsdocDeprecated_suggestion17);


static void TestJsdocDeprecated_suggestion16(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: esnext
// @filename: /a.ts
const a = 1;
const b = 1;
export { a, /** @deprecated b is deprecated */ b }
// @filename: /b.ts
import { [|b|] } from "./a";
[|b|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.ts");
		f->VerifySuggestionDiagnostics(t, {std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'b' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[0]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'b' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[1]->LSRange})});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocDeprecated_suggestion16, TestJsdocDeprecated_suggestion16);


static void TestJsdocDeprecated_suggestion15(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: esnext
// @filename: /a.ts
export const a = 1;
export const b = 1;
// @filename: /b.ts
export {
    /** @deprecated a is deprecated */
    a
} from "./a";
// @filename: /c.ts
export {
    a
} from "./b";
// @filename: /d.ts
import { [|a|] } from "./c";
[|a|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToFile(t, "/d.ts");
		f->VerifySuggestionDiagnostics(t, {std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'a' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[0]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'a' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[1]->LSRange})});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocDeprecated_suggestion15, TestJsdocDeprecated_suggestion15);


static void TestJsdocDeprecated_suggestion14(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: esnext
// @filename: /a.ts
export const a = 1;
export const b = 1;
// @filename: /b.ts
export {
    /** @deprecated a is deprecated */
    a
} from "./a";
// @filename: /c.ts
import { [|a|] } from "./b";
[|a|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToFile(t, "/c.ts");
		f->VerifySuggestionDiagnostics(t, {std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'a' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[0]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'a' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[1]->LSRange})});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocDeprecated_suggestion14, TestJsdocDeprecated_suggestion14);


static void TestJsdocDeprecated_suggestion13(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: foo.ts
/**
 * @deprecated
 */
function foo() {};

class Foo {
    constructor(fn: () => void) {
        fn();
    }
}
new Foo([|foo|]);)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToFile(t, "foo.ts");
		f->VerifySuggestionDiagnostics(t, {std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'foo' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[0]->LSRange})});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocDeprecated_suggestion13, TestJsdocDeprecated_suggestion13);


static void TestJsdocDeprecated_suggestion12(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: foo.ts
/**
 * @deprecated
 */
function foo() {};
function bar(fn: () => void) {
    fn();
}
bar([|foo|]);)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToFile(t, "foo.ts");
		f->VerifySuggestionDiagnostics(t, {std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'foo' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[0]->LSRange})});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocDeprecated_suggestion12, TestJsdocDeprecated_suggestion12);


static void TestJsdocDeprecated_suggestion11(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: /foo.ts
/** @deprecated */
export function foo() {}
// @filename: /test.ts
import { [|foo|] } from "./foo";
[|foo|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToFile(t, "/test.ts");
		f->VerifySuggestionDiagnostics(t, {std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'foo' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[0]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'foo' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[1]->LSRange})});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocDeprecated_suggestion11, TestJsdocDeprecated_suggestion11);


static void TestJsdocDeprecated_suggestion10(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: foo.ts
export namespace foo {
    /** @deprecated */
    export const bar = 1;
    [|bar|];
}
foo.[|bar|];
foo[[|"bar"|]];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToFile(t, "foo.ts");
		f->VerifySuggestionDiagnostics(t, {std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'bar' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[0]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'bar' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[1]->LSRange}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6385))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'bar' is deprecated.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagDeprecated}), .Range = f->Ranges()[2]->LSRange})});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocDeprecated_suggestion10, TestJsdocDeprecated_suggestion10);


static void TestJsdocCallbackTag(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @strict: false
// @allowNonTsExtensions: true
// @Filename: jsdocCallbackTag.js
/**
 * @callback FooHandler - A kind of magic
 * @param {string} eventName - So many words
 * @param eventName2 {number | string} - Silence is golden
 * @param eventName3 - Osterreich mos def
 * @return {number} - DIVEKICK
 */
/**
 * @type {FooHa/*8*/ndler} callback
 */
var t/*1*/;

/**
 * @callback FooHandler2 - What, another one?
 * @param {string=} eventName - it keeps happening
 * @param {string} [eventName2] - i WARNED you dog
 */
/**
 * @type {FooH/*3*/andler2} callback
 */
var t2/*2*/;
t(/*4*/"!", /*5*/12, /*6*/false);)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "1");
		f->VerifyQuickInfoIs(t, "var t: FooHandler", "");
		f->GoToMarker(t, "2");
		f->VerifyQuickInfoIs(t, "var t2: FooHandler2", "");
		f->GoToMarker(t, "3");
		f->VerifyQuickInfoIs(t, "type FooHandler2 = (eventName?: string | undefined, eventName2?: string) => any", "- What, another one?");
		f->GoToMarker(t, "8");
		f->VerifyQuickInfoIs(t, "type FooHandler = (eventName: string, eventName2: number | string, eventName3: any) => number", "- A kind of magic");
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocCallbackTag, TestJsdocCallbackTag);


static void TestJsdocCallbackTagRename01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @allowNonTsExtensions: true
// @Filename: jsDocCallback.js

/**
 * [|@callback [|{| "contextRangeIndex": 0 |}FooCallback|]
 * @param {string} eventName - Rename should work
 |]*/

/** @type {/*1*/[|FooCallback|]} */
var t;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1]});
	});
}
REGISTER_FOURSLASH_TEST(TestJsdocCallbackTagRename01, TestJsdocCallbackTagRename01);


static void TestJsDocTypedefQuickInfo1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: jsDocTypedef1.js
/**
 * @typedef {Object} Opts
 * @property {string} x
 * @property {string=} y
 * @property {string} [z]
 * @property {string} [w="hi"]
 * 
 * @param {Opts} opts
 */
function foo(/*1*/opts) {
    opts.x;
}
foo({x: 'abc'});
/**
 * @typedef {object} Opts1
 * @property {string} x
 * @property {string=} y
 * @property {string} [z]
 * @property {string} [w="hi"]
 * 
 * @param {Opts1} opts
 */
function foo1(/*2*/opts1) {
    opts1.x;
}
foo1({x: 'abc'});)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocTypedefQuickInfo1, TestJsDocTypedefQuickInfo1);


static void TestJsDocTypeTagQuickInfo2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @strict: true
// @allowJs: true
// @Filename: jsDocTypeTag2.js
/** @type {string} */
var /*1*/s;
/** @type {number} */
var /*2*/n;
/** @type {boolean} */
var /*3*/b;
/** @type {void} */
var /*4*/v;
/** @type {undefined} */
var /*5*/u;
/** @type {null} */
var /*6*/nl;
/** @type {array} */
var /*7*/a;
/** @type {promise} */
var /*8*/p;
/** @type {?number} */
var /*9*/nullable;
/** @type {function} */
var /*10*/func;
/** @type {function (number): number} */
var /*11*/func1;
/** @type {string | number} */
var /*12*/sOrn;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocTypeTagQuickInfo2, TestJsDocTypeTagQuickInfo2);


static void TestJsDocTypeTagQuickInfo1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @strict: true
// @allowJs: true
// @Filename: jsDocTypeTag1.js
/** @type {String} */
var /*1*/S;
/** @type {Number} */
var /*2*/N;
/** @type {Boolean} */
var /*3*/B;
/** @type {Void} */
var /*4*/V;
/** @type {Undefined} */
var /*5*/U;
/** @type {Null} */
var /*6*/Nl;
/** @type {Array} */
var /*7*/A;
/** @type {Promise} */
var /*8*/P;
/** @type {Object} */
var /*9*/Obj;
/** @type {Function} */
var /*10*/Func;
/** @type {*} */
var /*11*/AnyType;
/** @type {?} */
var /*12*/QType;
/** @type {String|Number} */
var /*13*/SOrN;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocTypeTagQuickInfo1, TestJsDocTypeTagQuickInfo1);


static void TestJsDocTagsWithHyphen(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: dummy.js
/**
 * @typedef Product
 * @property {string} title
 * @property {boolean} h/*1*/igh-top some-comments
 */

/**
 * @type {Pro/*2*/duct}
 */
const product = {
    /*3*/
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(property) high-top: boolean", "some-comments");
		f->VerifyQuickInfoAt(t, "2", R"TS(type Product = {
    title: string;
    "high-top": boolean;
})TS", "");
		f->VerifyCompletions(t, std::vector<std::string>{"3"}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {R"TS("high-top")TS"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocTagsWithHyphen, TestJsDocTagsWithHyphen);


static void TestJsDocSignature_43394(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/**
 * @typedef {Object} Foo
 * @property {number} ...
 * /**/@typedef {number} Bar
 */)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocSignature_43394, TestJsDocSignature_43394);


static void TestJsDocServices(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface /*I*/I {}

/**
 * @param /*use*/[|foo|] I pity the foo
 */
function f([|[|/*def*/{| "contextRangeIndex": 1 |}foo|]: I|]) {
    return /*use2*/[|foo|];
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "use");
		f->VerifyQuickInfoIs(t, "(parameter) foo: I", "I pity the foo");
		f->VerifyBaselineFindAllReferences(t, {"use", "def", "use2"});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[0], f->Ranges()[2], f->Ranges()[3]});
		f->VerifyBaselineDocumentHighlights(t, nullptr, {f->Ranges()[0], f->Ranges()[2], f->Ranges()[3]});
		f->VerifyBaselineGoToTypeDefinition(t, {"use"});
		f->VerifyBaselineGoToDefinition(t, false, {"use"});
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocServices, TestJsDocServices);



static void TestJsDocSee4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class [|/*def1*/A|] {
    foo () { }
}
declare const [|/*def2*/a|]: A;
/**
 * @see {/*use1*/[|A|]#foo}
 */
const t1 = 1
/**
 * @see {/*use2*/[|a|].foo()}
 */
const t2 = 1
/**
 * @see {@link /*use3*/[|a|].foo()}
 */
const t3 = 1)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"use1", "use2", "use3"});
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocSee4, TestJsDocSee4);


static void TestJsDocSee3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function foo ([|/*def1*/a|]: string) {
    /**
     * @see {/*use1*/[|a|]}
     */
    function bar ([|/*def2*/a|]: string) {
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"use1"});
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocSee3, TestJsDocSee3);


static void TestJsDocSee2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** @see {/*use1*/[|foooo|]} unknown reference*/
const a = ""
/** @see {/*use2*/[|@bar|]} invalid tag*/
const b = ""
/** @see /*use3*/[|foooo|] unknown reference without brace*/
const c = ""
/** @see /*use4*/[|@bar|] invalid tag without brace*/
const [|/*def1*/d|] = ""
/** @see {/*use5*/[|d@fff|]} partial reference */
const e = ""
/** @see /*use6*/[|@@@@@@|] total invalid tag*/
const f = ""
/** @see d@{/*use7*/[|fff|]} partial reference */
const g = "")TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, {"use1", "use2", "use3", "use4", "use5", "use6", "use7"});
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocSee2, TestJsDocSee2);


static void TestJsDocSee1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface [|/*def1*/Foo|] {
    foo: string
}
namespace NS {
    export interface [|/*def2*/Bar|] {
        baz: Foo
    }
}
/** @see {/*use1*/[|Foo|]} foooo*/
const a = ""
/** @see {NS./*use2*/[|Bar|]} ns.bar*/
const b = ""
/** @see /*use3*/[|Foo|] f1*/
const c = ""
/** @see NS./*use4*/[|Bar|] ns.bar*/
const [|/*def3*/d|] = ""
/** @see /*use5*/[|d|] dd*/
const e = "")TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, false, {"use1", "use2", "use3", "use4", "use5"});
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocSee1, TestJsDocSee1);


static void TestJsDocPropertyDescription9(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(class LiteralClass {
    /** Something generic */
    static [key: `prefix${string}`]: any;
    /** Something else */
    static [key: `prefix${number}`]: number;
}
function literalClass(e: typeof LiteralClass) {
    console.log(e./*literal1Class*/prefixMember); 
    console.log(e./*literal2Class*/anything);
    console.log(e./*literal3Class*/prefix0);
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "literal1Class", R"TS((index) LiteralClass[`prefix${string}`]: any)TS", "Something generic");
		f->VerifyQuickInfoAt(t, "literal2Class", "any", "");
		f->VerifyQuickInfoAt(t, "literal3Class", R"TS((index) LiteralClass[`prefix${string}` | `prefix${number}`]: any)TS", R"TS(Something generic
Something else)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocPropertyDescription9, TestJsDocPropertyDescription9);


static void TestJsDocPropertyDescription8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class SymbolClass {
    /** Something generic */
    static [p: symbol]: any;
}
function symbolClass(e: typeof SymbolClass) {
    console.log(e./*symbolClass*/anything);
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "symbolClass", "any", "");
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocPropertyDescription8, TestJsDocPropertyDescription8);


static void TestJsDocPropertyDescription7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(class StringClass {
    /** Something generic */
    static [p: string]: any;
}
function stringClass(e: typeof StringClass) {
    console.log(e./*stringClass*/anything);
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "stringClass", "(index) StringClass[string]: any", "Something generic");
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocPropertyDescription7, TestJsDocPropertyDescription7);


static void TestJsDocPropertyDescription6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(interface Literal1Example {
    [key: `prefix${string}`]: number | string;
    /** Something else */
    [key: `prefix${number}`]: number;
}
function literal1Example(e: Literal1Example) {
    console.log(e./*literal1*/prefixMember);
    console.log(e./*literal2*/anything);
    console.log(e./*literal3*/prefix0);
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "literal1", R"TS((index) Literal1Example[`prefix${string}`]: string | number)TS", "");
		f->VerifyQuickInfoAt(t, "literal2", "any", "");
		f->VerifyQuickInfoAt(t, "literal3", R"TS((index) Literal1Example[`prefix${string}` | `prefix${number}`]: number)TS", "Something else");
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocPropertyDescription6, TestJsDocPropertyDescription6);


static void TestJsDocPropertyDescription5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Multiple1Example {
    /** Something generic */
    [key: number | symbol | `data-${string}` | `data-${number}`]: string;
}
function multiple1Example(e: Multiple1Example) {
    console.log(e./*multiple1*/anything);
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "multiple1", "any", "");
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocPropertyDescription5, TestJsDocPropertyDescription5);


static void TestJsDocPropertyDescription4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(interface MultipleExample {
    /** Something generic */
    [key: string | number | symbol]: string;
}
function multipleExample(e: MultipleExample) {
    console.log(e./*multiple*/anything);
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "multiple", "(index) MultipleExample[string | number | symbol]: string", "Something generic");
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocPropertyDescription4, TestJsDocPropertyDescription4);


static void TestJsDocPropertyDescription3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface LiteralExample {
    /** Something generic */
    [key: `data-${string}`]: string;
     /** Something else */
    [key: `prefix${number}`]: number;
}
function literalExample(e: LiteralExample) {
    console.log(e./*literal*/anything);
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "literal", "any", "");
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocPropertyDescription3, TestJsDocPropertyDescription3);


static void TestJsDocPropertyDescription2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface SymbolExample {
    /** Something generic */
    [key: symbol]: string;
}
function symbolExample(e: SymbolExample) {
    console.log(e./*symbol*/anything);
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "symbol", "any", "");
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocPropertyDescription2, TestJsDocPropertyDescription2);


static void TestJsDocPropertyDescription1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(interface StringExample {
    /** Something generic */
    [p: string]: any; 
    /** Something specific */
    property: number;
}
function stringExample(e: StringExample) {
    console.log(e./*property*/property);
    console.log(e./*string*/anything); 
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "property", "(property) StringExample.property: number", "Something specific");
		f->VerifyQuickInfoAt(t, "string", "(index) StringExample[string]: any", "Something generic");
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocPropertyDescription1, TestJsDocPropertyDescription1);


static void TestJsDocPropertyDescription12(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type SymbolAlias = {
    /** Something generic */
    [p: symbol]: string;
}
function symbolAlias(e: SymbolAlias) {
    console.log(e./*symbolAlias*/anything);
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "symbolAlias", "any", "");
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocPropertyDescription12, TestJsDocPropertyDescription12);


static void TestJsDocPropertyDescription11(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(type AliasExample = {
    /** Something generic */
    [p: string]: string;
    /** Something else */
    [key: `any${string}`]: string;
}
function aliasExample(e: AliasExample) {
    console.log(e./*alias*/anything);
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "alias", R"TS((index) AliasExample[string | `any${string}`]: string)TS", R"TS(Something generic
Something else)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocPropertyDescription11, TestJsDocPropertyDescription11);


static void TestJsDocPropertyDescription10(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class MultipleClass {
    /** Something generic */
    [key: number | symbol | `data-${string}` | `data-${number}`]: string;
}
function multipleClass(e: typeof MultipleClass) {
    console.log(e./*multipleClass*/anything);
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "multipleClass", "any", "");
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocPropertyDescription10, TestJsDocPropertyDescription10);


static void TestJsDocInheritDoc(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: inheritDoc.ts
class Foo {
    /**
     * Foo constructor documentation
     */
    constructor(value: number) {}
    /**
     * Foo#method1 documentation
     */
    static method1() {}
    /**
     * Foo#method2 documentation
     */
    method2() {}
    /**
     * Foo#property1 documentation
     */
    property1: string;
    /**
     * Foo#property3 documentation
     */
    property3 = "instance prop";
}
interface Baz {
    /** Baz#property1 documentation */
    property1: string;
    /**
     * Baz#property2 documentation
     */
    property2: object;
}
class Bar extends Foo implements Baz {
    ctorValue: number;
    /** @inheritDoc */
    constructor(value: number) {
        super(value);
        this.ctorValue = value;
    }
    /** @inheritDoc */
    static method1() {}
    method2() {}
    /** @inheritDoc */
    property1: string;
    /**
     * Bar#property2
     * @inheritDoc
     */
    property2: object;

    static /*6*/property3 = "class prop";
}
const b = new Bar/*1*/(5);
b.method2/*2*/();
Bar.method1/*3*/();
const p1 = b.property1/*4*/;
const p2 = b.property2/*5*/;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "constructor Bar(value: number): Bar", "");
		f->VerifyQuickInfoAt(t, "2", "(method) Bar.method2(): void", "Foo#method2 documentation");
		f->VerifyQuickInfoAt(t, "3", "(method) Bar.method1(): void", "Foo#method1 documentation");
		f->VerifyQuickInfoAt(t, "4", "(property) Bar.property1: string", "Foo#property1 documentation");
		f->VerifyQuickInfoAt(t, "5", "(property) Bar.property2: object", R"TS(Baz#property2 documentation
Bar#property2)TS");
		f->VerifyQuickInfoAt(t, "6", "(property) Bar.property3: string", "");
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocInheritDoc, TestJsDocInheritDoc);


static void TestJsDocIndentationPreservation3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: Foo.js
/**
    Does some stuff.
        Second line.
    	Third line.
*/
function foo/**/(){})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyQuickInfoIs(t, "function foo(): void", R"TS(Does some stuff.
    Second line.
	Third line.)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocIndentationPreservation3, TestJsDocIndentationPreservation3);


static void TestJsDocIndentationPreservation2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: Foo.js
/**
	Does some stuff.
	    Second line.
		Third line.
*/
function foo/**/(){})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyQuickInfoIs(t, "function foo(): void", R"TS(Does some stuff.
    Second line.
	Third line.)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocIndentationPreservation2, TestJsDocIndentationPreservation2);


static void TestJsDocIndentationPreservation1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: Foo.js
/**
 * Does some stuff.
 *     Second line.
 * 	Third line.
 */
function foo/**/(){})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyQuickInfoIs(t, "function foo(): void", R"TS(Does some stuff.
    Second line.
	Third line.)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocIndentationPreservation1, TestJsDocIndentationPreservation1);


static void TestJsDocGenerics2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: Foo.js
/**
 * @param {T[]} arr
 * @param {(function(T):T)} valuator
 * @template T
 */
function SortFilter(arr,valuator)
{
    return arr;
}
var a/*1*/ = SortFilter([0, 1, 2], q/*2*/ => q);
var b/*3*/ = SortFilter([0, 1, 2], undefined);)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "var a: number[]", "");
		f->VerifyQuickInfoAt(t, "2", "(parameter) q: number", "");
		f->VerifyQuickInfoAt(t, "3", "var b: number[]", "");
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocGenerics2, TestJsDocGenerics2);


static void TestJsDocGenerics1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: ref.d.ts
namespace Thing {
    export interface Thung {
        a: number;
    ]
]
// @Filename: Foo.js

/** @type {Array<number>} */
var v;
v[0]./*1*/

/** @type {{x: Array<Array<number>>}} */
var w;
w.x[0][0]./*2*/

/** @type {Array<Thing.Thung>} */
var x;
x[0].a./*3*/)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, f->Markers(), tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "toFixed", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocGenerics1, TestJsDocGenerics1);


static void TestJsDocFunctionTypeCompletionsNoCrash(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
/**
 * @returns {function/**/(): string}
 */
function updateCalendarEvent() {
  return "";
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = tsu::CompletionGlobalTypes})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocFunctionTypeCompletionsNoCrash, TestJsDocFunctionTypeCompletionsNoCrash);


static void TestJsDocFunctionSignatures8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: Foo.js
/**
 * Represents a person
 * a b multiline test
 * @constructor
 * @param {string} name The name of the person
 * @param {number} age The age of the person
 */
function Person(name, age) {
    this.name = name;
    this.age = age;
}
var p = new Pers/**/on();)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyQuickInfoIs(t, "constructor Person(name: string, age: number): Person", R"TS(Represents a person
a b multiline test)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocFunctionSignatures8, TestJsDocFunctionSignatures8);


static void TestJsDocFunctionSignatures7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: Foo.js
/**
 * @param {string} p0
 * @param {string} [p1]
 */
function Test(p0, p1) {
    this.P0 = p0;
    this.P1 = p1;
}


var /**/test = new Test("");)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyQuickInfoIs(t, "var test: Test", "");
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocFunctionSignatures7, TestJsDocFunctionSignatures7);


static void TestJsDocFunctionSignatures6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: Foo.js
/**
 * @param {string} p1 - A string param
 * @param {string?} p2 - An optional param
 * @param {string} [p3] - Another optional param
 * @param {string} [p4="test"] - An optional param with a default value
 */
function f1(p1, p2, p3, p4){}
f1(/*1*/'foo', /*2*/'bar', /*3*/'baz', /*4*/'qux');)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocFunctionSignatures6, TestJsDocFunctionSignatures6);


static void TestJsDocFunctionSignatures6VS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: Foo.js
/**
 * @param {string} p1 - A string param
 * @param {string?} p2 - An optional param
 * @param {string} [p3] - Another optional param
 * @param {string} [p4="test"] - An optional param with a default value
 */
function f1(p1, p2, p3, p4){}
f1(/*1*/'foo', /*2*/'bar', /*3*/'baz', /*4*/'qux');)TS";
		auto __fsp1 = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = true}), content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocFunctionSignatures6VS, TestJsDocFunctionSignatures6VS);


static void TestJsDocFunctionSignatures5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @allowJs: true
// @Filename: Foo.js
/**
 * Filters a path based on a regexp or glob pattern.
 * @param {String} basePath The base path where the search will be performed.
 * @param {String} pattern A string defining a regexp of a glob pattern.
 * @param {String} type The search pattern type, can be a regexp or a glob.
 * @param {Object} options A object containing options to the search.
 * @return {Array} A list containing the filtered paths.
 */
function pathFilter(basePath, pattern, type, options){
//...
}
pathFilter(/**/'foo', 'bar', 'baz', {});)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocFunctionSignatures5, TestJsDocFunctionSignatures5);


static void TestJsDocFunctionSignatures5VS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @allowJs: true
// @Filename: Foo.js
/**
 * Filters a path based on a regexp or glob pattern.
 * @param {String} basePath The base path where the search will be performed.
 * @param {String} pattern A string defining a regexp of a glob pattern.
 * @param {String} type The search pattern type, can be a regexp or a glob.
 * @param {Object} options A object containing options to the search.
 * @return {Array} A list containing the filtered paths.
 */
function pathFilter(basePath, pattern, type, options){
//...
}
pathFilter(/**/'foo', 'bar', 'baz', {});)TS";
		auto __fsp1 = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = true}), content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocFunctionSignatures5VS, TestJsDocFunctionSignatures5VS);


static void TestJsDocFunctionSignatures4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: Foo.js
/** @param {function ({OwnerID:string,AwayID:string}):void} x
  * @param {function (string):void} y */
function fn(x, y) { })TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocFunctionSignatures4, TestJsDocFunctionSignatures4);


static void TestJsDocFunctionSignatures3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: Foo.js
var someObject = {
    /**
     * @param {string} param1 Some string param.
     * @param {number} parm2  Some number param.
     */
    someMethod: function(param1, param2) {
        console.log(param1/*1*/);
        return false;
    },
    /**
     * @param {number} p1  Some number param.
     */
    otherMethod(p1) {
        p1/*2*/
    }

};)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->Insert(t, ".");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "substring", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)})}})}));
		f->Backspace(t, 1);
		f->GoToMarker(t, "2");
		f->Insert(t, ".");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = {std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "toFixed", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)})}})}));
		f->Backspace(t, 1);
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocFunctionSignatures3, TestJsDocFunctionSignatures3);


static void TestJsDocFunctionSignatures2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: Foo.js
/** @type {(arg0: string, arg1?: boolean) => number} */
var f6;

f6('', /**/false))TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "f6(arg0: string, arg1?: boolean): number"});
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocFunctionSignatures2, TestJsDocFunctionSignatures2);


static void TestJsDocFunctionSignatures13(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(/**
 * @template {string} K/**/ a golden opportunity
 */
function Multimap(iv) {
};)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyQuickInfoIs(t, "any", "");
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocFunctionSignatures13, TestJsDocFunctionSignatures13);


static void TestJsDocFunctionSignatures12(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: jsDocFunctionSignatures.js
/**
 * @param {{
 *   stringProp: string,
 *   numProp: number,
 *   boolProp: boolean,
 *   anyProp: any,
 *   anotherAnyProp: any,
 *   functionProp: (arg0: string, arg1: any) => any
 * }} o
 */
function f1(o) {
    o/**/;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyQuickInfoIs(t, R"TS((parameter) o: {
    stringProp: string;
    numProp: number;
    boolProp: boolean;
    anyProp: any;
    anotherAnyProp: any;
    functionProp: (arg0: string, arg1: any) => any;
})TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocFunctionSignatures12, TestJsDocFunctionSignatures12);


static void TestJsDocFunctionSignatures11(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: Foo.js
/**
 * @type {{ [name: string]: string; }} variables
 */
const vari/**/ables = {};)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyQuickInfoIs(t, R"TS(const variables: {
    [name: string]: string;
})TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocFunctionSignatures11, TestJsDocFunctionSignatures11);


static void TestJsDocFunctionSignatures10(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: Foo.js
/**
 * Do some foo things
 * @template T A Foolish template
 * @param {T} x a parameter
 */
function foo(x) {
}

fo/**/o())TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyQuickInfoIs(t, "function foo<any>(x: any): void", "Do some foo things");
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocFunctionSignatures10, TestJsDocFunctionSignatures10);


static void TestJsDocForTypeAlias(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** DOC */
type /**/T = number)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyQuickInfoIs(t, "type T = number", "DOC");
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocForTypeAlias, TestJsDocForTypeAlias);


static void TestJsDocExtends(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: dummy.js
/**
 * @extends {Thing<string>}
 */
class MyStringThing extends Thing {
    constructor() {
        var x = this.mine;
        x/**/;
    }
}
// @Filename: declarations.d.ts
declare class Thing<T> {
    mine: T;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyQuickInfoIs(t, "(local var) x: string", "");
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocExtends, TestJsDocExtends);


static void TestJsDocDontBreakWithNamespaces(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: jsDocDontBreakWithNamespaces.js
/**
 * @returns {module:@nodefuel/web~Webserver~wsServer#hello} Websocket server object
 */
function foo() { }
foo(''/*foo*/);

/**
 * @type {module:xxxxx} */
 */
function bar() { }
bar(''/*bar*/);

/** @type {function(module:xxxx, module:xxxx): module:xxxxx} */
function zee() { }
zee(''/*zee*/);)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocDontBreakWithNamespaces, TestJsDocDontBreakWithNamespaces);


static void TestJsDocDontBreakWithNamespacesVS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: jsDocDontBreakWithNamespaces.js
/**
 * @returns {module:@nodefuel/web~Webserver~wsServer#hello} Websocket server object
 */
function foo() { }
foo(''/*foo*/);

/**
 * @type {module:xxxxx} */
 */
function bar() { }
bar(''/*bar*/);

/** @type {function(module:xxxx, module:xxxx): module:xxxxx} */
function zee() { }
zee(''/*zee*/);)TS";
		auto __fsp1 = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = true}), content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocDontBreakWithNamespacesVS, TestJsDocDontBreakWithNamespacesVS);


static void TestJsDocAugments(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: dummy.js
/**
 * @augments {Thing<string>}
 */
class MyStringThing extends Thing {
    constructor() {
        var x = this.mine;
        x/**/;
    }
}
// @Filename: declarations.d.ts
declare class Thing<T> {
    mine: T;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyQuickInfoIs(t, "(local var) x: string", "");
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocAugments, TestJsDocAugments);


static void TestJsDocAugmentsAndExtends(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: dummy.js
/**
 * @augments {Thing<number>}
 * [|@extends {Thing<string>}|]
 */
class MyStringThing extends Thing {
    constructor() {
        super();
        var x = this.mine;
        x/**/;
    }
}
// @Filename: declarations.d.ts
declare class Thing<T> {
    mine: T;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyQuickInfoIs(t, "(local var) x: number", "");
		f->VerifyNonSuggestionDiagnostics(t, {std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("Class declarations cannot have more than one '@augments' or '@extends' tag.")}, .Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(8025))})})});
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocAugmentsAndExtends, TestJsDocAugmentsAndExtends);


static void TestJsDocAliasQuickInfo(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /jsDocAliasQuickInfo.ts
/**
 * Comment
 * @type {number}
 */
export /*1*/default 10;
// @Filename: /test.ts
export { /*2*/default as /*3*/test } from "./jsDocAliasQuickInfo";)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestJsDocAliasQuickInfo, TestJsDocAliasQuickInfo);


static void TestDocumentSymbolPrivateName(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: first.ts
class A {
  #foo() {
    class B {
      #bar() {   
         function baz () {
         }
      }
    }
  }
}

class B {
	constructor(private prop: string) {}
}

// @Filename: second.ts
class Foo {
	#privateProp: string;
}
)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
		f->GoToFile(t, "second.ts");
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentSymbolPrivateName, TestDocumentSymbolPrivateName);
} // namespace
