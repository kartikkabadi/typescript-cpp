// Ported fourslash tests -- batch B (jsx). One static void TestX(gostd::testing::T*)
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
#include "internal/ls/ls.h"
#include "internal/testutil/testutil.h"

namespace {
using namespace tsc;
namespace tsu = tsc::fourslash::tests::util;

// jsxAriaLikeCompletions_test.go
static void TestJsxAriaLikeCompletions(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
declare var React: any;
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
        div: { "aria-whatever"?: string  }
    }
    interface ElementAttributesProperty { props: any }
}
const a = <div {...{}} /*1*/></div>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "aria-whatever?",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "aria-whatever",
					.InsertText = "aria-whatever",})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsxAriaLikeCompletions, TestJsxAriaLikeCompletions);

// jsxAttributeCompletionStyleAuto_test.go
static void TestJsxAttributeCompletionStyleAuto(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: foo.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
        foo: {
            prop_a: boolean;
            prop_b: string;
            prop_c: any;
            prop_d: { p1: string; }
            prop_e: string | undefined;
            prop_f: boolean | undefined | { p1: string; };
            prop_g: { p1: string; } | undefined;
            prop_h?: string;
            prop_i?: boolean;
            prop_j?: { p1: string; };
            prop_string_literal_union?: 'input' | 'password' | (string & {})
        }
    }
}

<foo [|prop_/**/|] />)TS";
		auto __fsp = fourslash::NewFourslash(t, fourslash::GetDefaultCapabilitiesWithOptions(tsu::ptr(fourslash::ClientCapabilitiesOptions{
		.CompletionItem = std::make_shared<lsproto::ClientCompletionItemOptions>(lsproto::ClientCompletionItemOptions{
			.SnippetSupport = true})})), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_a"}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_b",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("prop_b=\"$1\"", f->Ranges()[0]->LSRange),}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_c",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("prop_c={$1}", f->Ranges()[0]->LSRange),}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_d",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("prop_d={$1}", f->Ranges()[0]->LSRange),}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_e",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("prop_e=\"$1\"", f->Ranges()[0]->LSRange),}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_f"}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_g",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("prop_g={$1}", f->Ranges()[0]->LSRange),}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_h?",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "prop_h",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("prop_h=\"$1\"", f->Ranges()[0]->LSRange),}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_i?",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "prop_i",
					.TextEdit = tsu::InsertReplaceTextEdit("prop_i", f->Ranges()[0]->LSRange),}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_j?",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "prop_j",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("prop_j={$1}", f->Ranges()[0]->LSRange),}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_string_literal_union?",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "prop_string_literal_union",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("prop_string_literal_union=\"$1\"", f->Ranges()[0]->LSRange),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleAuto})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsxAttributeCompletionStyleAuto, TestJsxAttributeCompletionStyleAuto);

// jsxAttributeCompletionStyleBraces_test.go
static void TestJsxAttributeCompletionStyleBraces(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: foo.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
        foo: {
            prop_a: boolean;
            prop_b: string;
            prop_c: any;
            prop_d: { p1: string; }
            prop_e: string | undefined;
            prop_f: boolean | undefined | { p1: string; };
            prop_g: { p1: string; } | undefined;
            prop_h?: string;
            prop_i?: boolean;
            prop_j?: { p1: string; };
        }
    }
}

<foo [|prop_/**/|] />)TS";
		auto __fsp = fourslash::NewFourslash(t, fourslash::GetDefaultCapabilitiesWithOptions(tsu::ptr(fourslash::ClientCapabilitiesOptions{
		.CompletionItem = std::make_shared<lsproto::ClientCompletionItemOptions>(lsproto::ClientCompletionItemOptions{
			.SnippetSupport = true})})), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_a",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("prop_a={$1}", f->Ranges()[0]->LSRange),}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_b",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("prop_b={$1}", f->Ranges()[0]->LSRange),}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_c",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("prop_c={$1}", f->Ranges()[0]->LSRange),}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_d",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("prop_d={$1}", f->Ranges()[0]->LSRange),}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_e",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("prop_e={$1}", f->Ranges()[0]->LSRange),}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_f",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("prop_f={$1}", f->Ranges()[0]->LSRange),}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_g",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("prop_g={$1}", f->Ranges()[0]->LSRange),}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_h?",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "prop_h",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("prop_h={$1}", f->Ranges()[0]->LSRange),}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_i?",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "prop_i",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("prop_i={$1}", f->Ranges()[0]->LSRange),}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_j?",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "prop_j",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("prop_j={$1}", f->Ranges()[0]->LSRange),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsxAttributeCompletionStyleBraces, TestJsxAttributeCompletionStyleBraces);

// jsxAttributeCompletionStyleDefault_test.go
static void TestJsxAttributeCompletionStyleDefault(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: foo.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
        foo: {
            prop_a: boolean;
            prop_b: string;
            prop_c: any;
            prop_d: { p1: string; }
            prop_e: string | undefined;
            prop_f: boolean | undefined | { p1: string; };
            prop_g: { p1: string; } | undefined;
            prop_h?: string;
            prop_i?: boolean;
            prop_j?: { p1: string; };
        }
    }
}

<foo [|prop_/**/|] />)TS";
		auto __fsp = fourslash::NewFourslash(t, fourslash::GetDefaultCapabilitiesWithOptions(tsu::ptr(fourslash::ClientCapabilitiesOptions{
		.CompletionItem = std::make_shared<lsproto::ClientCompletionItemOptions>(lsproto::ClientCompletionItemOptions{
			.SnippetSupport = true})})), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_a"}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_b"}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_c"}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_d"}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_e"}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_f"}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_g"}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_h?",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "prop_h",
					.TextEdit = tsu::InsertReplaceTextEdit("prop_h", f->Ranges()[0]->LSRange),}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_i?",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "prop_i",
					.TextEdit = tsu::InsertReplaceTextEdit("prop_i", f->Ranges()[0]->LSRange),}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_j?",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "prop_j",
					.TextEdit = tsu::InsertReplaceTextEdit("prop_j", f->Ranges()[0]->LSRange),})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsxAttributeCompletionStyleDefault, TestJsxAttributeCompletionStyleDefault);

// jsxAttributeCompletionStyleNoSnippet_test.go
static void TestJsxAttributeCompletionStyleNoSnippet(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: foo.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
        foo: {
            prop_a: boolean;
            prop_b: string;
            prop_c: any;
            prop_d: { p1: string; }
            prop_e: string | undefined;
            prop_f: boolean | undefined | { p1: string; };
            prop_g: { p1: string; } | undefined;
            prop_h?: string;
            prop_i?: boolean;
            prop_j?: { p1: string; };
        }
    }
}

<foo [|prop_/**/|] />)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_a"}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_b"}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_c"}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_d"}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_e"}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_f"}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_g"}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_h?",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "prop_h",
					.TextEdit = tsu::InsertReplaceTextEdit("prop_h", f->Ranges()[0]->LSRange),}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_i?",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "prop_i",
					.TextEdit = tsu::InsertReplaceTextEdit("prop_i", f->Ranges()[0]->LSRange),}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_j?",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "prop_j",
					.TextEdit = tsu::InsertReplaceTextEdit("prop_j", f->Ranges()[0]->LSRange),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleAuto})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsxAttributeCompletionStyleNoSnippet, TestJsxAttributeCompletionStyleNoSnippet);

// jsxAttributeCompletionStyleNone_test.go
static void TestJsxAttributeCompletionStyleNone(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: foo.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
        foo: {
            prop_a: boolean;
            prop_b: string;
            prop_c: any;
            prop_d: { p1: string; }
            prop_e: string | undefined;
            prop_f: boolean | undefined | { p1: string; };
            prop_g: { p1: string; } | undefined;
            prop_h?: string;
            prop_i?: boolean;
            prop_j?: { p1: string; };
        }
    }
}

<foo [|prop_/**/|] />)TS";
		auto __fsp = fourslash::NewFourslash(t, fourslash::GetDefaultCapabilitiesWithOptions(tsu::ptr(fourslash::ClientCapabilitiesOptions{
		.CompletionItem = std::make_shared<lsproto::ClientCompletionItemOptions>(lsproto::ClientCompletionItemOptions{
			.SnippetSupport = true})})), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_a"}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_b"}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_c"}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_d"}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_e"}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_f"}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_g"}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_h?",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "prop_h",
					.TextEdit = tsu::InsertReplaceTextEdit("prop_h", f->Ranges()[0]->LSRange),}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_i?",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "prop_i",
					.TextEdit = tsu::InsertReplaceTextEdit("prop_i", f->Ranges()[0]->LSRange),}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "prop_j?",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "prop_j",
					.TextEdit = tsu::InsertReplaceTextEdit("prop_j", f->Ranges()[0]->LSRange),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleNone})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsxAttributeCompletionStyleNone, TestJsxAttributeCompletionStyleNone);

// jsxAttributeSnippetCompletionAfterTypeArgs_test.go
static void TestJsxAttributeSnippetCompletionAfterTypeArgs(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @strict: false
//@Filename: file.tsx
declare const React: any;

namespace JSX {
    export interface IntrinsicElements {
        div: any;
    }
}

function GenericElement<T>(props: {xyz?: T}) {
    return <></>
}

function fn1() {
    return <div>
        <GenericElement<number> /*1*/ />
    </div>
}

function fn2() {
    return <>
        <GenericElement<number> /*2*/ />
    </>
}
function fn3() {
    return <div>
        <GenericElement<number> /*3*/ ></GenericElement>
    </div>
}

function fn4() {
    return <>
        <GenericElement<number> /*4*/ ></GenericElement>
    </>
})TS";
		auto __fsp = fourslash::NewFourslash(t, fourslash::GetDefaultCapabilitiesWithOptions(tsu::ptr(fourslash::ClientCapabilitiesOptions{
		.CompletionItem = std::make_shared<lsproto::ClientCompletionItemOptions>(lsproto::ClientCompletionItemOptions{
			.SnippetSupport = true})})), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, f->Markers(), tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "xyz?",
					.Detail = "(property) xyz?: number",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "xyz",
					.InsertText = "xyz={$1}",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsxAttributeSnippetCompletionAfterTypeArgs, TestJsxAttributeSnippetCompletionAfterTypeArgs);

// jsxAttributeSnippetCompletionClosed_test.go
static void TestJsxAttributeSnippetCompletionClosed(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @strict: false
//@Filename: file.tsx
interface NestedInterface {
    Foo: NestedInterface;
    (props: {className?: string, onClick?: () => void}): any;
}

declare const Foo: NestedInterface;

function fn1() {
    return <Foo>
        <Foo /*1*/ />
    </Foo>
}
function fn2() {
    return <Foo>
        <Foo.Foo /*2*/ />
    </Foo>
}
function fn3() {
    return <Foo>
        <Foo.Foo [|cla/*3*/|] />
    </Foo>
}
function fn4() {
    return <Foo>
        <Foo.Foo [|cla/*4*/|] something />
    </Foo>
}
function fn5() {
    return <Foo>
        <Foo.Foo something /*5*/ />
    </Foo>
}
function fn6() {
    return <Foo>
        <Foo.Foo something [|cla/*6*/|] />
    </Foo>
}
function fn7() {
    return <Foo /*7*/ />
}
function fn8() {
    return <Foo [|cla/*8*/|] />
}
function fn9() {
    return <Foo [|cla/*9*/|] something />
}
function fn10() {
    return <Foo something /*10*/ />
}
function fn11() {
    return <Foo something [|cla/*11*/|] />
}
function fn12() {
    return <Foo something={false} [|cla/*12*/|] />
}
function fn13() {
    return <Foo something={false} /*13*/ foo />
}
function fn14() {
    return <Foo something={false} [|cla/*14*/|] foo />
}
function fn15() {
    return <Foo [|onC/*15*/|]="" />
}
function fn16() {
    return <Foo something={false} [|onC/*16*/|]="" foo />
})TS";
		auto __fsp = fourslash::NewFourslash(t, fourslash::GetDefaultCapabilitiesWithOptions(tsu::ptr(fourslash::ClientCapabilitiesOptions{
		.CompletionItem = std::make_shared<lsproto::ClientCompletionItemOptions>(lsproto::ClientCompletionItemOptions{
			.SnippetSupport = true})})), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Detail = "(property) className?: string",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertText = "className={$1}",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Detail = "(property) className?: string",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertText = "className={$1}",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "3", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Detail = "(property) className?: string",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("className={$1}", f->Ranges()[0]->LSRange),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "4", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Detail = "(property) className?: string",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("className={$1}", f->Ranges()[1]->LSRange),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "5", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Detail = "(property) className?: string",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertText = "className={$1}",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "6", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Detail = "(property) className?: string",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("className={$1}", f->Ranges()[2]->LSRange),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "7", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Detail = "(property) className?: string",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertText = "className={$1}",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "8", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Detail = "(property) className?: string",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("className={$1}", f->Ranges()[3]->LSRange),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "9", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Detail = "(property) className?: string",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("className={$1}", f->Ranges()[4]->LSRange),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "10", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Detail = "(property) className?: string",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertText = "className={$1}",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "11", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Detail = "(property) className?: string",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("className={$1}", f->Ranges()[5]->LSRange),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "12", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Detail = "(property) className?: string",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("className={$1}", f->Ranges()[6]->LSRange),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "13", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Detail = "(property) className?: string",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertText = "className={$1}",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "14", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Detail = "(property) className?: string",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("className={$1}", f->Ranges()[7]->LSRange),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "15", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "onClick?",
					.Detail = "(property) onClick?: () => void",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "onClick",
					.TextEdit = tsu::InsertReplaceTextEdit("onClick", f->Ranges()[8]->LSRange),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "16", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "onClick?",
					.Detail = "(property) onClick?: () => void",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "onClick",
					.TextEdit = tsu::InsertReplaceTextEdit("onClick", f->Ranges()[9]->LSRange),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsxAttributeSnippetCompletionClosed, TestJsxAttributeSnippetCompletionClosed);

// jsxAttributeSnippetCompletionUnclosed_test.go
static void TestJsxAttributeSnippetCompletionUnclosed(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @strict: false
//@Filename: file.tsx
interface NestedInterface {
    Foo: NestedInterface;
    (props: {className?: string}): any;
}

declare const Foo: NestedInterface;

function fn1() {
    return <Foo>
        <Foo /*1*/
    </Foo>
}
function fn2() {
    return <Foo>
        <Foo.Foo /*2*/
    </Foo>
}
function fn3() {
    return <Foo>
        <Foo.Foo [|cla/*3*/|]
    </Foo>
}
function fn4() {
    return <Foo>
        <Foo.Foo [|cla/*4*/|] something
    </Foo>
}
function fn5() {
    return <Foo>
        <Foo.Foo something /*5*/
    </Foo>
}
function fn6() {
    return <Foo>
        <Foo.Foo something [|cla/*6*/|]
    </Foo>
}
function fn7() {
    return <Foo /*7*/
}
function fn8() {
    return <Foo [|cla/*8*/|]
}
function fn9() {
    return <Foo [|cla/*9*/|] something
}
function fn10() {
    return <Foo something /*10*/
}
function fn11() {
    return <Foo something [|cla/*11*/|]
})TS";
		auto __fsp = fourslash::NewFourslash(t, fourslash::GetDefaultCapabilitiesWithOptions(tsu::ptr(fourslash::ClientCapabilitiesOptions{
		.CompletionItem = std::make_shared<lsproto::ClientCompletionItemOptions>(lsproto::ClientCompletionItemOptions{
			.SnippetSupport = true})})), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Detail = "(property) className?: string",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertText = "className={$1}",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Detail = "(property) className?: string",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertText = "className={$1}",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "3", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Detail = "(property) className?: string",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("className={$1}", f->Ranges()[0]->LSRange),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "4", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Detail = "(property) className?: string",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("className={$1}", f->Ranges()[1]->LSRange),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "5", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Detail = "(property) className?: string",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertText = "className={$1}",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "6", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Detail = "(property) className?: string",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("className={$1}", f->Ranges()[2]->LSRange),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "7", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Detail = "(property) className?: string",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertText = "className={$1}",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "8", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Detail = "(property) className?: string",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("className={$1}", f->Ranges()[3]->LSRange),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "9", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Detail = "(property) className?: string",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("className={$1}", f->Ranges()[4]->LSRange),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "10", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Detail = "(property) className?: string",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertText = "className={$1}",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "11", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Detail = "(property) className?: string",
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertTextFormat = std::make_shared<lsproto::InsertTextFormat>(lsproto::InsertTextFormatSnippet),
					.TextEdit = tsu::InsertReplaceTextEdit("className={$1}", f->Ranges()[5]->LSRange),})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsxAttributeSnippetCompletionUnclosed, TestJsxAttributeSnippetCompletionUnclosed);

// jsxElementExtendsNoCrash1_test.go
static void TestJsxElementExtendsNoCrash1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @filename: index.tsx
<const T extends/>)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySuggestionDiagnostics(t, std::vector<std::shared_ptr<lsproto::Diagnostic>>{});
	});
}
REGISTER_FOURSLASH_TEST(TestJsxElementExtendsNoCrash1, TestJsxElementExtendsNoCrash1);

// jsxElementExtendsNoCrash2_test.go
static void TestJsxElementExtendsNoCrash2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @filename: index.tsx
<T extends/>)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySuggestionDiagnostics(t, std::vector<std::shared_ptr<lsproto::Diagnostic>>{});
	});
}
REGISTER_FOURSLASH_TEST(TestJsxElementExtendsNoCrash2, TestJsxElementExtendsNoCrash2);

// jsxElementExtendsNoCrash3_test.go
static void TestJsxElementExtendsNoCrash3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @filename: index.tsx
<T extends /=>)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySuggestionDiagnostics(t, std::vector<std::shared_ptr<lsproto::Diagnostic>>{});
	});
}
REGISTER_FOURSLASH_TEST(TestJsxElementExtendsNoCrash3, TestJsxElementExtendsNoCrash3);

// jsxElementMissingOpeningTagNoCrash_test.go
static void TestJsxElementMissingOpeningTagNoCrash(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
declare function Foo(): any;
let x = <></Fo/*$*/o>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "$", "let Foo: any", "");
	});
}
REGISTER_FOURSLASH_TEST(TestJsxElementMissingOpeningTagNoCrash, TestJsxElementMissingOpeningTagNoCrash);

// jsxFindAllReferencesOnRuntimeImportWithPaths1_test.go
static void TestJsxFindAllReferencesOnRuntimeImportWithPaths1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: project/src/foo.ts
import * as x from /**/"@foo/dir/jsx-runtime";
// @Filename: project/src/bar.tsx
export default <div></div>;
// @Filename: project/src/baz.tsx
export default <></>;
// @Filename: project/src/bam.tsx
export default <script src=""/>;
// @Filename: project/src/bat.tsx
export const a = 1;
// @Filename: project/src/bal.tsx

// @Filename: project/src/dir/jsx-runtime.ts
export {}
// @Filename: project/tsconfig.json
{
    "compilerOptions": {
        "moduleResolution": "node",
        "module": "es2020",
        "jsx": "react-jsx",
        "jsxImportSource": "@foo/dir",
        "moduleDetection": "force",
        "paths": {
            "@foo/dir/jsx-runtime": ["./src/dir/jsx-runtime"]
        }
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, std::vector<std::string>{""});
	});
}
REGISTER_FOURSLASH_TEST(TestJsxFindAllReferencesOnRuntimeImportWithPaths1, TestJsxFindAllReferencesOnRuntimeImportWithPaths1);

// jsxGenericQuickInfo_test.go
static void TestJsxGenericQuickInfo(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
    }
    interface ElementAttributesProperty { props }
}
interface PropsA<T> {
    /** comments for A */
    name: 'A',
    items: T[];
    renderItem: (item: T) => string;
}
interface PropsB<T> {
    /** comments for B */
    name: 'B',
    items: T[];
    renderItem: (item: T) => string;
}
class Component<T> {
    constructor(props: PropsA<T> | PropsB<T>) {}
    props: PropsA<T> | PropsB<T>;
}   
var b = new Component({items: [0, 1, 2], render/*0*/Item: it/*1*/em => item.toFixed(), name/*2*/: 'A',});
var c = <Component items={[0, 1, 2]} render/*3*/Item={it/*4*/em => item.toFixed()} name/*5*/="A" />)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "0", "(property) PropsA<number>.renderItem: (item: number) => string", "");
		f->VerifyQuickInfoAt(t, "1", "(parameter) item: number", "");
		f->VerifyQuickInfoAt(t, "2", "(property) PropsA<T>.name: \"A\"", "comments for A");
		f->VerifyQuickInfoAt(t, "3", "(property) PropsA<number>.renderItem: (item: number) => string", "");
		f->VerifyQuickInfoAt(t, "4", "(parameter) item: number", "");
		f->VerifyQuickInfoAt(t, "5", "(property) PropsA<T>.name: \"A\"", "comments for A");
	});
}
REGISTER_FOURSLASH_TEST(TestJsxGenericQuickInfo, TestJsxGenericQuickInfo);

// jsxQualifiedTagCompletion_test.go
static void TestJsxQualifiedTagCompletion(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
declare var React: any;
namespace NS {
    export var Foo: any = null;
}
const j = <NS.Foo>Hello!/**/
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->Insert(t, "</");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"NS.Foo>"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsxQualifiedTagCompletion, TestJsxQualifiedTagCompletion);

// jsxSpreadReference_test.go
static void TestJsxSpreadReference(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
    }
    interface ElementAttributesProperty { props }
}
class MyClass {
  props: {
    name?: string;
    size?: number;
  }
}

[|var [|/*dst*/{| "contextRangeIndex": 0 |}nn|]: {name?: string; size?: number};|]
var x = <MyClass {...[|n/*src*/n|]}></MyClass>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr , std::vector<std::string>{"nn"});
		f->VerifyBaselineGoToDefinition(t, true, std::vector<std::string>{"src"});
	});
}
REGISTER_FOURSLASH_TEST(TestJsxSpreadReference, TestJsxSpreadReference);

// jsxTagNameCompletionClosed_test.go
static void TestJsxTagNameCompletionClosed(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
interface NestedInterface {
    Foo: NestedInterface;
    (props: {}): any;
}

declare const Foo: NestedInterface;

function fn1() {
    return <Foo>
        </*1*/ />
    </Foo>
}
function fn2() {
    return <Foo>
        <Fo/*2*/ />
    </Foo>
}
function fn3() {
    return <Foo>
        <Foo./*3*/ />
    </Foo>
}
function fn4() {
    return <Foo>
        <Foo.F/*4*/ />
    </Foo>
}
function fn5() {
    return <Foo>
        <Foo.Foo./*5*/ />
    </Foo>
}
function fn6() {
    return <Foo>
        <Foo.Foo.F/*6*/ />
    </Foo>
})TS";
		auto __fsp = fourslash::NewFourslash(t, fourslash::GetDefaultCapabilitiesWithOptions(tsu::ptr(fourslash::ClientCapabilitiesOptions{
		.CompletionItem = std::make_shared<lsproto::ClientCompletionItemOptions>(lsproto::ClientCompletionItemOptions{
			.SnippetSupport = true})})), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label =  "Foo",
					.Detail = "const Foo: NestedInterface"})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label =  "Foo",
					.Detail = "const Foo: NestedInterface"})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "3", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label =  "Foo",
					.Detail = "(property) NestedInterface.Foo: NestedInterface"})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "4", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label =  "Foo",
					.Detail = "(property) NestedInterface.Foo: NestedInterface"})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "5", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label =  "Foo",
					.Detail = "(property) NestedInterface.Foo: NestedInterface"})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "6", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label =  "Foo",
					.Detail = "(property) NestedInterface.Foo: NestedInterface"})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsxTagNameCompletionClosed, TestJsxTagNameCompletionClosed);

// jsxTagNameCompletionUnclosed_test.go
static void TestJsxTagNameCompletionUnclosed(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
interface NestedInterface {
    Foo: NestedInterface;
    (props: {}): any;
}

declare const Foo: NestedInterface;

function fn1() {
    return <Foo>
        </*1*/
    </Foo>
}
function fn2() {
    return <Foo>
        <Fo/*2*/
    </Foo>
}
function fn3() {
    return <Foo>
        <Foo./*3*/
    </Foo>
}
function fn4() {
    return <Foo>
        <Foo.F/*4*/
    </Foo>
}
function fn5() {
    return <Foo>
        <Foo.Foo./*5*/
    </Foo>
}
function fn6() {
    return <Foo>
        <Foo.Foo.F/*6*/
    </Foo>
})TS";
		auto __fsp = fourslash::NewFourslash(t, fourslash::GetDefaultCapabilitiesWithOptions(tsu::ptr(fourslash::ClientCapabilitiesOptions{
		.CompletionItem = std::make_shared<lsproto::ClientCompletionItemOptions>(lsproto::ClientCompletionItemOptions{
			.SnippetSupport = true})})), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label =  "Foo",
					.Detail = "const Foo: NestedInterface"})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label =  "Foo",
					.Detail = "const Foo: NestedInterface"})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "3", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label =  "Foo",
					.Detail = "(property) NestedInterface.Foo: NestedInterface"})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "4", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label =  "Foo",
					.Detail = "(property) NestedInterface.Foo: NestedInterface"})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "5", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label =  "Foo",
					.Detail = "(property) NestedInterface.Foo: NestedInterface"})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "6", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label =  "Foo",
					.Detail = "(property) NestedInterface.Foo: NestedInterface"})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsxTagNameCompletionUnclosed, TestJsxTagNameCompletionUnclosed);

// jsxTagNameCompletionUnderElementClosed_test.go
static void TestJsxTagNameCompletionUnderElementClosed(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
declare namespace JSX {
    interface IntrinsicElements {
        button: any;
        div: any;
    }
}
function fn() {
    return <>
        <butto/*1*/ />
    </>;
}
function fn2() {
    return <>
        preceding junk <butto/*2*/ />
    </>;
}
function fn3() {
    return <>
        <butto/*3*/ style="" />
    </>;
})TS";
		auto __fsp = fourslash::NewFourslash(t, fourslash::GetDefaultCapabilitiesWithOptions(tsu::ptr(fourslash::ClientCapabilitiesOptions{
		.CompletionItem = std::make_shared<lsproto::ClientCompletionItemOptions>(lsproto::ClientCompletionItemOptions{
			.SnippetSupport = true})})), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label =  "button",
					.Detail = "(property) JSX.IntrinsicElements.button: any"})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label =  "button",
					.Detail = "(property) JSX.IntrinsicElements.button: any"})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "3", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label =  "button",
					.Detail = "(property) JSX.IntrinsicElements.button: any"})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsxTagNameCompletionUnderElementClosed, TestJsxTagNameCompletionUnderElementClosed);

// jsxTagNameCompletionUnderElementUnclosed_test.go
static void TestJsxTagNameCompletionUnderElementUnclosed(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
declare namespace JSX {
    interface IntrinsicElements {
        button: any;
        div: any;
    }
}
function fn() {
    return <>
        <butto/*1*/
    </>;
}
function fn2() {
    return <>
        preceding junk <butto/*2*/
    </>;
}
function fn3() {
    return <>
        <butto/*3*/ style=""
    </>;
})TS";
		auto __fsp = fourslash::NewFourslash(t, fourslash::GetDefaultCapabilitiesWithOptions(tsu::ptr(fourslash::ClientCapabilitiesOptions{
		.CompletionItem = std::make_shared<lsproto::ClientCompletionItemOptions>(lsproto::ClientCompletionItemOptions{
			.SnippetSupport = true})})), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label =  "button",
					.Detail = "(property) JSX.IntrinsicElements.button: any"})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label =  "button",
					.Detail = "(property) JSX.IntrinsicElements.button: any"})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
		f->VerifyCompletions(t, "3", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label =  "button",
					.Detail = "(property) JSX.IntrinsicElements.button: any"})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsxTagNameCompletionUnderElementUnclosed, TestJsxTagNameCompletionUnderElementUnclosed);

// jsxTagNameCompletionWithExistingJsxInitializer_test.go
static void TestJsxTagNameCompletionWithExistingJsxInitializer(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @filename: /foo.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
        foo: {
            className: string;
        }
    }
}
<foo cl/**/={""} />)TS";
		auto __fsp = fourslash::NewFourslash(t, fourslash::GetDefaultCapabilitiesWithOptions(tsu::ptr(fourslash::ClientCapabilitiesOptions{
		.CompletionItem = std::make_shared<lsproto::ClientCompletionItemOptions>(lsproto::ClientCompletionItemOptions{
			.SnippetSupport = true})})), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label =  "className",
					.Detail = "(property) className: string"})}}),
		.UserPreferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.JsxAttributeCompletionStyle = lsutil::JsxAttributeCompletionStyleBraces})}));
	});
}
REGISTER_FOURSLASH_TEST(TestJsxTagNameCompletionWithExistingJsxInitializer, TestJsxTagNameCompletionWithExistingJsxInitializer);

// jsxWithTypeParametershasInstantiatedSignatureHelp_test.go
static void TestJsxWithTypeParametershasInstantiatedSignatureHelp(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"});
		return;
		const std::string content = R"TS(declare namespace JSX {
    interface Element {
        render(): Element | string | false;
    }
}

function SFC<T>(_props: Record<string, T>) {
    return '';
}

(</*1*/SFC/>);
(</*2*/SFC<string>/>);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "SFC(_props: Record<string, unknown>): string"});
		f->GoToMarker(t, "2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "SFC(_props: Record<string, string>): string"});
	});
}
REGISTER_FOURSLASH_TEST(TestJsxWithTypeParametershasInstantiatedSignatureHelp, TestJsxWithTypeParametershasInstantiatedSignatureHelp);


}  // namespace
