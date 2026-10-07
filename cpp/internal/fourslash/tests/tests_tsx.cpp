// Ported fourslash tests -- batch B (tsx). One static void TestX(gostd::testing::T*)
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

// tsxCompletion10_test.go
static void TestTsxCompletion10(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
        div: { ONE: string; TWO: number; }
    }
}
var x1 = <div><//**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"div>"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestTsxCompletion10, TestTsxCompletion10);

// tsxCompletion11_test.go
static void TestTsxCompletion11(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@module: commonjs
//@jsx: preserve
//@Filename: exporter.tsx
export class Thing { }
//@Filename: file.tsx
import {Thing} from './exporter';
var x1 = <div></**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				"Thing"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestTsxCompletion11, TestTsxCompletion11);

// tsxCompletion12_test.go
static void TestTsxCompletion12(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
// @jsx: preserve
// @noLib: true
declare module JSX {
    interface Element { }
    interface IntrinsicElements {
    }
    interface ElementAttributesProperty { props; }
}
interface OptionPropBag {
    propx: number
    propString: "hell"
    optional?: boolean
}
declare function Opt(attributes: OptionPropBag): JSX.Element;
let opt = <Opt /*1*/ />;
let opt1 = <Opt [|prop|]/*2*/ />;
let opt2 = <Opt propx={100} /*3*/ />;
let opt3 = <Opt propx={100} optional /*4*/ />;
let opt4 = <Opt wrong /*5*/ />;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, std::vector<std::string>{"1", "5"}, tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"propString",
				"propx",
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "optional?",
					.Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindField),
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "optional",
					.InsertText = "optional",})}})}));
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"propString",
				"propx",
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "optional?",
					.Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindField),
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "optional",
					.TextEdit = std::make_shared<lsproto::TextEditOrInsertReplaceEdit>(lsproto::TextEditOrInsertReplaceEdit{
						.InsertReplaceEdit = std::make_shared<lsproto::InsertReplaceEdit>(lsproto::InsertReplaceEdit{
							.NewText = "optional",
							.Insert =  f->Ranges()[0]->LSRange,
							.Replace = f->Ranges()[0]->LSRange})}),})}})}));
		f->VerifyCompletions(t, "3", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"propString",
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "optional?",
					.Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindField),
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "optional",
					.InsertText = "optional",})}})}));
		f->VerifyCompletions(t, "4", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"propString"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestTsxCompletion12, TestTsxCompletion12);

// tsxCompletion13_test.go
static void TestTsxCompletion13(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
// @jsx: preserve
// @skipLibCheck: true
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
    }
    interface ElementAttributesProperty { props; }
}
interface ClickableProps {
    children?: string;
    className?: string;
}
interface ButtonProps extends ClickableProps {
    onClick(event?: React.MouseEvent<HTMLButtonElement>): void;
}
interface LinkProps extends ClickableProps {
    goTo: string;
}
declare function MainButton(buttonProps: ButtonProps): JSX.Element;
declare function MainButton(linkProps: LinkProps): JSX.Element;
declare function MainButton(props: ButtonProps | LinkProps): JSX.Element;
let opt = <MainButton /*1*/ />;
let opt = <MainButton children="chidlren" /*2*/ />;
let opt = <MainButton onClick={()=>{}} /*3*/ />;
let opt = <MainButton onClick={()=>{}} ignore-prop /*4*/ />;
let opt = <MainButton goTo="goTo" /*5*/ />;
let opt = <MainButton wrong /*6*/ />;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, std::vector<std::string>{"1", "6"}, tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"goTo",
				"onClick",
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "children?",
					.Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindField),
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "children",
					.InsertText = "children",}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindField),
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertText = "className",})}})}));
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"goTo",
				"onClick",
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindField),
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertText = "className",})}})}));
		f->VerifyCompletions(t, std::vector<std::string>{"3", "4", "5"}, tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "children?",
					.Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindField),
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "children",
					.InsertText = "children",}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label = "className?",
					.Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindField),
					.SortText = std::string(ls::SortTextOptionalMember),
					.FilterText = "className",
					.InsertText = "className",})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestTsxCompletion13, TestTsxCompletion13);

// tsxCompletion14_test.go
static void TestTsxCompletion14(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@module: commonjs
//@jsx: preserve
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
    }
    interface ElementAttributesProperty { props; }
}
//@Filename: exporter.tsx
export class Thing { props: { ONE: string; TWO: number } }
export namespace M {
   export declare function SFCComp(props: { Three: number; Four: string }): JSX.Element;
}
//@Filename: file.tsx
import * as Exp from './exporter';
var x1 = <Exp.Thing /*1*/ />;
var x2 = <Exp.M.SFCComp /*2*/ />;
var x3 = <Exp.Thing /*3*/ ></Exp.Thing>;
var x4 = <Exp.M.SFCComp /*4*/ ></Exp.M.SFCComp>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, std::vector<std::string>{"1", "3"}, tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"ONE",
				"TWO"}})}));
		f->VerifyCompletions(t, std::vector<std::string>{"2", "4"}, tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"Four",
				"Three"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestTsxCompletion14, TestTsxCompletion14);

// tsxCompletion15_test.go
static void TestTsxCompletion15(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@module: commonjs
//@jsx: preserve
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
    }
    interface ElementAttributesProperty { props; }
}
//@Filename: exporter.tsx
export namespace M {
   export declare function SFCComp(props: { Three: number; Four: string }): JSX.Element;
}
//@Filename: file.tsx
import * as Exp from './exporter';
var x1  = <Exp.M.SFCComp></[|/*1*/|]>;
var x2  = <Exp.M.SFCComp></[|Exp./*2*/|]>;
var x3  = <Exp.M.SFCComp></[|Exp.M./*3*/|]>;
var x4  = <Exp.M.SFCComp></[|Exp.M.SFCComp/*4*/|]
var x5  = <Exp.M.SFCComp></[|Exp.M.SFCComp/*5*/|]>;
var x6  = <Exp.M.SFCComp></      [|Exp./*6*/|]>;
var x7  = <Exp.M.SFCComp></[|/*7*/Exp.M.SFCComp|]>;
var x8  = <Exp.M.SFCComp></[|Exp/*8*/|]>;
var x9  = <Exp.M.SFCComp></[|Exp.M./*9*/|]>;
var x10 = <Exp.M.SFCComp></      [|/*10*/Exp.M.Foo.Bar.Baz.Wut|]>;
var x11 = <Exp.M.SFCComp></[|Exp./*11*/M.SFCComp|]>;
var x12 = <Exp.M.SFCComp><div><span /></div></[|Exp.M./*12*/SFCComp|]>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"Exp.M.SFCComp"}})}));
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"Exp.M.SFCComp"}})}));
		f->VerifyCompletions(t, "3", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"Exp.M.SFCComp"}})}));
		f->VerifyCompletions(t, "4", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"Exp.M.SFCComp>"}})}));
		f->VerifyCompletions(t, "5", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"Exp.M.SFCComp"}})}));
		f->VerifyCompletions(t, "6", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"Exp.M.SFCComp"}})}));
		f->VerifyCompletions(t, "7", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"Exp.M.SFCComp"}})}));
		f->VerifyCompletions(t, "8", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"Exp.M.SFCComp"}})}));
		f->VerifyCompletions(t, "9", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"Exp.M.SFCComp"}})}));
		f->VerifyCompletions(t, "10", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"Exp.M.SFCComp"}})}));
		f->VerifyCompletions(t, "11", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"Exp.M.SFCComp"}})}));
		f->VerifyCompletions(t, "12", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"Exp.M.SFCComp"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestTsxCompletion15, TestTsxCompletion15);

// tsxCompletion1_test.go
static void TestTsxCompletion1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
        div: { ONE: string; TWO: number; }
    }
}
var x = <div /**//>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"ONE",
				"TWO"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestTsxCompletion1, TestTsxCompletion1);

// tsxCompletion2_test.go
static void TestTsxCompletion2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
    }
    interface ElementAttributesProperty { props; }
}
class MyComp { props: { ONE: string; TWO: number } }
var x = <MyComp /**//>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"ONE",
				"TWO"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestTsxCompletion2, TestTsxCompletion2);

// tsxCompletion3_test.go
static void TestTsxCompletion3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
        div: { one; two; }
    }
}
<div one={1} /**//>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"two"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestTsxCompletion3, TestTsxCompletion3);

// tsxCompletion4_test.go
static void TestTsxCompletion4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
        div: { one; two; }
    }
}
let bag = { x: 100, y: 200 };
<div {.../**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				"bag"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestTsxCompletion4, TestTsxCompletion4);

// tsxCompletion5_test.go
static void TestTsxCompletion5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
        div: { ONE: string; TWO: number; }
    }
}
var x = <div ONE/**//>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"ONE",
				"TWO"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestTsxCompletion5, TestTsxCompletion5);

// tsxCompletion6_test.go
static void TestTsxCompletion6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
        div: { ONE: string; TWO: number; }
    }
}
var x = <div ONE='hello' /**/ />;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"TWO"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestTsxCompletion6, TestTsxCompletion6);

// tsxCompletion7_test.go
static void TestTsxCompletion7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
        div: { ONE: string; TWO: number; }
    }
}
let y = { ONE: '' };
var x = <div {...y} /**/ />;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label =    "TWO",
					.Kind =     std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindField),
					.SortText = std::string(ls::SortTextLocationPriority)}),
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label =    "ONE",
					.Kind =     std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindField),
					.SortText = std::string(ls::SortTextMemberDeclaredBySpreadAssignment)})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestTsxCompletion7, TestTsxCompletion7);

// tsxCompletion8_test.go
static void TestTsxCompletion8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
        div: { ONE: string; TWO: number; }
    }
}
var x = <div /*1*/ autoComplete /*2*/ />;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, std::vector<std::string>{"1", "2"}, tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"ONE",
				"TWO"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestTsxCompletion8, TestTsxCompletion8);

// tsxCompletionInFunctionExpressionOfChildrenCallback1_test.go
static void TestTsxCompletionInFunctionExpressionOfChildrenCallback1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@module: commonjs
//@jsx: preserve
// @Filename: 1.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
    }
    interface ElementAttributesProperty { props; }
    interface ElementChildrenAttribute { children; }
}
interface IUser {
    Name: string;
}
interface IFetchUserProps {
    children: (user: IUser) => any;
}
function FetchUser(props: IFetchUserProps) { return undefined; }
function UserName() {
    return (
        <FetchUser>
            { user => (
                <h1>{ user./**/ }</h1>
            )}
        </FetchUser>
    );
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"Name"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestTsxCompletionInFunctionExpressionOfChildrenCallback1, TestTsxCompletionInFunctionExpressionOfChildrenCallback1);

// tsxCompletionInFunctionExpressionOfChildrenCallback_test.go
static void TestTsxCompletionInFunctionExpressionOfChildrenCallback(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@module: commonjs
//@jsx: preserve
// @Filename: 1.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
    }
    interface ElementAttributesProperty { props; }
}
interface IUser {
    Name: string;
}
interface IFetchUserProps {
    children: (user: IUser) => any;
}
function FetchUser(props: IFetchUserProps) { return undefined; }
function UserName() {
    return (
        <FetchUser>
            { user => (
                <h1>{ user./**/ }</h1>
            )}
        </FetchUser>
    );
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestTsxCompletionInFunctionExpressionOfChildrenCallback, TestTsxCompletionInFunctionExpressionOfChildrenCallback);

// tsxCompletionNonTagLessThan_test.go
static void TestTsxCompletionNonTagLessThan(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @lib: es5
// @Filename: /a.tsx
var x: Array<numb/*a*/;
[].map<numb/*b*/;
1 < Infini/*c*/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, std::vector<std::string>{"a", "b"}, tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label =    "number",
					.SortText = std::string(ls::SortTextGlobalsOrKeywords)})},
			.Excludes = std::vector<std::string>{
				"SVGNumber"}})}));
		f->VerifyCompletions(t, "c", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{
					.Label =    "Infinity",
					.SortText = std::string(ls::SortTextGlobalsOrKeywords)})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestTsxCompletionNonTagLessThan, TestTsxCompletionNonTagLessThan);

// tsxCompletionOnClosingTag1_test.go
static void TestTsxCompletionOnClosingTag1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
        div: { ONE: string; TWO: number; }
    }
}
var x1 = <div><//**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"div>"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestTsxCompletionOnClosingTag1, TestTsxCompletionOnClosingTag1);

// tsxCompletionOnClosingTag2_test.go
static void TestTsxCompletionOnClosingTag2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
        div: { ONE: string; TWO: number; }
    }
}
var x1 = <div>
   <h1> Hello world </ /*2*/>
   </ /*1*/>)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"div"}})}));
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"h1"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestTsxCompletionOnClosingTag2, TestTsxCompletionOnClosingTag2);

// tsxCompletionOnClosingTagWithoutJSX1_test.go
static void TestTsxCompletionOnClosingTagWithoutJSX1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
var x1 = <div><//**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"div>"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestTsxCompletionOnClosingTagWithoutJSX1, TestTsxCompletionOnClosingTagWithoutJSX1);

// tsxCompletionOnClosingTagWithoutJSX2_test.go
static void TestTsxCompletionOnClosingTagWithoutJSX2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
var x1 = <div>
   <h1> Hello world </ /*2*/>
   </ /*1*/>)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"div"}})}));
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Exact = std::vector<fourslash::CompletionsExpectedItem>{
				"h1"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestTsxCompletionOnClosingTagWithoutJSX2, TestTsxCompletionOnClosingTagWithoutJSX2);

// tsxCompletionsGenericComponent_test.go
static void TestTsxCompletionsGenericComponent(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @jsx: preserve
// @skipLibCheck: true
// @Filename: file.tsx
 declare namespace JSX {
     interface Element { }
     interface IntrinsicElements {
     }
     interface ElementAttributesProperty { props; }
 }

class Table<P> {
    constructor(public props: P) {}
}

type Props = { widthInCol: number; text: string; };

/**
 * @param width {number} Table width in px
 */
function createTable(width) {
    return <Table<Props> /*1*/ />
}

createTable(800);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{
		.IsIncomplete = false,
		.ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{
			.CommitCharacters = std::make_shared<std::remove_cvref_t<decltype(tsu::DefaultCommitCharacters)>>(tsu::DefaultCommitCharacters),
			.EditRange =        fourslash::Ignored{}}),
		.Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{
			.Includes = std::vector<fourslash::CompletionsExpectedItem>{
				"widthInCol",
				"text"}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestTsxCompletionsGenericComponent, TestTsxCompletionsGenericComponent);

// tsxFindAllReferences10_test.go
static void TestTsxFindAllReferences10(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
// @jsx: preserve
// @noLib: true
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
    }
    interface ElementAttributesProperty { props; }
}
interface ClickableProps {
    children?: string;
    className?: string;
}
interface ButtonProps extends ClickableProps {
    /*1*/onClick(event?: React.MouseEvent<HTMLButtonElement>): void;
}
interface LinkProps extends ClickableProps {
    goTo: string;
}
declare function MainButton(buttonProps: ButtonProps): JSX.Element;
declare function MainButton(linkProps: LinkProps): JSX.Element;
declare function MainButton(props: ButtonProps | LinkProps): JSX.Element;
let opt = <MainButton />;
let opt = <MainButton children="chidlren" />;
let opt = <MainButton onClick={()=>{}} />;
let opt = <MainButton onClick={()=>{}} ignore-prop />;
let opt = <MainButton goTo="goTo" />;
let opt = <MainButton wrong />;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, std::vector<std::string>{"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxFindAllReferences10, TestTsxFindAllReferences10);

// tsxFindAllReferences11_test.go
static void TestTsxFindAllReferences11(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
// @jsx: preserve
// @noLib: true
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
    }
    interface ElementAttributesProperty { props; }
}
interface ClickableProps {
    children?: string;
    className?: string;
}
interface ButtonProps extends ClickableProps {
    onClick(event?: React.MouseEvent<HTMLButtonElement>): void;
}
interface LinkProps extends ClickableProps {
    goTo: string;
}
declare function MainButton(buttonProps: ButtonProps): JSX.Element;
declare function MainButton(linkProps: LinkProps): JSX.Element;
declare function MainButton(props: ButtonProps | LinkProps): JSX.Element;
let opt = <MainButton /*1*/wrong />;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, std::vector<std::string>{"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxFindAllReferences11, TestTsxFindAllReferences11);

// tsxFindAllReferences1VS_test.go
static void TestTsxFindAllReferences1VS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
        /*1*/div: {
            name?: string;
            isOpen?: boolean;
        };
        span: { n: string; };
    }
}
var x = /*2*/</*3*/div />;)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = true}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineVSFindAllReferences(t, std::vector<std::string>{"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxFindAllReferences1VS, TestTsxFindAllReferences1VS);

// tsxFindAllReferences1_test.go
static void TestTsxFindAllReferences1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
        /*1*/div: {
            name?: string;
            isOpen?: boolean;
        };
        span: { n: string; };
    }
}
var x = /*2*/</*3*/div />;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, std::vector<std::string>{"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxFindAllReferences1, TestTsxFindAllReferences1);

// tsxFindAllReferences2_test.go
static void TestTsxFindAllReferences2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
        div: {
            /*1*/name?: string;
            isOpen?: boolean;
        };
        span: { n: string; };
    }
}
var x = <div name="hello" />;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, std::vector<std::string>{"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxFindAllReferences2, TestTsxFindAllReferences2);

// tsxFindAllReferences3_test.go
static void TestTsxFindAllReferences3(gostd::testing::T* t) {
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
    /*1*/name?: string;
    size?: number;
}


var x = <MyClass name='hello'/>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, std::vector<std::string>{"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxFindAllReferences3, TestTsxFindAllReferences3);

// tsxFindAllReferences4_test.go
static void TestTsxFindAllReferences4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
    }
    interface ElementAttributesProperty { props }
}
/*1*/class /*2*/MyClass {
  props: {
    name?: string;
    size?: number;
}


var x = /*3*/</*4*/MyClass name='hello'><//*5*/MyClass>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, std::vector<std::string>{"1", "2", "3", "4", "5"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxFindAllReferences4, TestTsxFindAllReferences4);

// tsxFindAllReferences5_test.go
static void TestTsxFindAllReferences5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
// @jsx: preserve
// @noLib: true
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
    }
    interface ElementAttributesProperty { props; }
}
interface OptionPropBag {
    propx: number
    propString: string
    optional?: boolean
}
/*1*/declare function /*2*/Opt(attributes: OptionPropBag): JSX.Element;
let opt = /*3*/</*4*/Opt />;
let opt1 = /*5*/</*6*/Opt propx={100} propString />;
let opt2 = /*7*/</*8*/Opt propx={100} optional/>;
let opt3 = /*9*/</*10*/Opt wrong />;
let opt4 = /*11*/</*12*/Opt propx={100} propString="hi" />;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, std::vector<std::string>{"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxFindAllReferences5, TestTsxFindAllReferences5);

// tsxFindAllReferences6_test.go
static void TestTsxFindAllReferences6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
// @jsx: preserve
// @noLib: true
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
    }
    interface ElementAttributesProperty { props; }
}
interface OptionPropBag {
    propx: number
    propString: string
    optional?: boolean
}
declare function Opt(attributes: OptionPropBag): JSX.Element;
let opt = <Opt /*1*/wrong />;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, std::vector<std::string>{"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxFindAllReferences6, TestTsxFindAllReferences6);

// tsxFindAllReferences7_test.go
static void TestTsxFindAllReferences7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
// @jsx: preserve
// @noLib: true
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
    }
    interface ElementAttributesProperty { props; }
}
interface OptionPropBag {
    /*1*/propx: number
    propString: string
    optional?: boolean
}
declare function Opt(attributes: OptionPropBag): JSX.Element;
let opt = <Opt />;
let opt1 = <Opt propx={100} propString />;
let opt2 = <Opt propx={100} optional/>;
let opt3 = <Opt wrong />;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, std::vector<std::string>{"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxFindAllReferences7, TestTsxFindAllReferences7);

// tsxFindAllReferences8_test.go
static void TestTsxFindAllReferences8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
// @jsx: preserve
// @noLib: true
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
    }
    interface ElementAttributesProperty { props; }
}
interface ClickableProps {
    children?: string;
    className?: string;
}
interface ButtonProps extends ClickableProps {
    onClick(event?: React.MouseEvent<HTMLButtonElement>): void;
}
interface LinkProps extends ClickableProps {
    goTo: string;
}
/*1*/declare function /*2*/MainButton(buttonProps: ButtonProps): JSX.Element;
/*3*/declare function /*4*/MainButton(linkProps: LinkProps): JSX.Element;
/*5*/declare function /*6*/MainButton(props: ButtonProps | LinkProps): JSX.Element;
let opt = /*7*/</*8*/MainButton />;
let opt = /*9*/</*10*/MainButton children="chidlren" />;
let opt = /*11*/</*12*/MainButton onClick={()=>{}} />;
let opt = /*13*/</*14*/MainButton onClick={()=>{}} ignore-prop />;
let opt = /*15*/</*16*/MainButton goTo="goTo" />;
let opt = /*17*/</*18*/MainButton wrong />;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, std::vector<std::string>{"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16", "17", "18"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxFindAllReferences8, TestTsxFindAllReferences8);

// tsxFindAllReferences9_test.go
static void TestTsxFindAllReferences9(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
// @jsx: preserve
// @noLib: true
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
    }
    interface ElementAttributesProperty { props; }
}
interface ClickableProps {
    children?: string;
    className?: string;
}
interface ButtonProps extends ClickableProps {
    onClick(event?: React.MouseEvent<HTMLButtonElement>): void;
}
interface LinkProps extends ClickableProps {
    /*1*/goTo: string;
}
declare function MainButton(buttonProps: ButtonProps): JSX.Element;
declare function MainButton(linkProps: LinkProps): JSX.Element;
declare function MainButton(props: ButtonProps | LinkProps): JSX.Element;
let opt = <MainButton />;
let opt = <MainButton children="chidlren" />;
let opt = <MainButton onClick={()=>{}} />;
let opt = <MainButton onClick={()=>{}} ignore-prop />;
let opt = <MainButton goTo="goTo" />;
let opt = <MainButton goTo />;
let opt = <MainButton wrong />;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, std::vector<std::string>{"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxFindAllReferences9, TestTsxFindAllReferences9);

// tsxFindAllReferencesUnionElementType1_test.go
static void TestTsxFindAllReferencesUnionElementType1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
// @jsx: preserve
// @noLib: true
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
    }
    interface ElementAttributesProperty { props; }
}
function SFC1(prop: { x: number }) {
    return <div>hello </div>;
};
function SFC2(prop: { x: boolean }) {
    return <h1>World </h1>;
}
/*1*/var /*2*/SFCComp = SFC1 || SFC2;
/*3*/</*4*/SFCComp x={ "hi" } />)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, std::vector<std::string>{"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxFindAllReferencesUnionElementType1, TestTsxFindAllReferencesUnionElementType1);

// tsxFindAllReferencesUnionElementType2_test.go
static void TestTsxFindAllReferencesUnionElementType2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
// @jsx: preserve
// @noLib: true
class RC1 extends React.Component<{}, {}> {
    render() {
        return null;
    }
}
class RC2 extends React.Component<{}, {}> {
    render() {
        return null;
    }
    private method() { }
}
/*1*/var /*2*/RCComp = RC1 || RC2;
/*3*/</*4*/RCComp />)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, std::vector<std::string>{"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxFindAllReferencesUnionElementType2, TestTsxFindAllReferencesUnionElementType2);

// tsxGoToDefinitionClassInDifferentFile_test.go
static void TestTsxGoToDefinitionClassInDifferentFile(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @jsx: preserve
// @Filename: C.tsx
export default class /*def*/C {}
// @Filename: a.tsx
import C from "./C";
const foo = </*use*/C />;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineGoToDefinition(t, false, std::vector<std::string>{"use"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxGoToDefinitionClassInDifferentFile, TestTsxGoToDefinitionClassInDifferentFile);

// tsxGoToDefinitionClasses_test.go
static void TestTsxGoToDefinitionClasses(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements { }
    interface ElementAttributesProperty { props; }
}
class /*ct*/MyClass {
    props: {
        /*pt*/foo: string;
    }
}
var x = <[|My/*c*/Class|] />;
var y = <MyClass [|f/*p*/oo|]= 'hello' />;
var z = <[|MyCl/*w*/ass|] wrong= 'hello' />;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, std::vector<std::string>{"c", "p", "w"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxGoToDefinitionClasses, TestTsxGoToDefinitionClasses);

// tsxGoToDefinitionIntrinsics_test.go
static void TestTsxGoToDefinitionIntrinsics(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
        /*dt*/div: {
            /*pt*/name?: string;
            isOpen?: boolean;
        };
        /*st*/span: { n: string; };
    }
}
var x = <[|di/*ds*/v|] />;
var y = <[|s/*ss*/pan|] />;
var z = <div [|na/*ps*/me|]='hello' />;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, std::vector<std::string>{"ds", "ss", "ps"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxGoToDefinitionIntrinsics, TestTsxGoToDefinitionIntrinsics);

// tsxGoToDefinitionStatelessFunction1_test.go
static void TestTsxGoToDefinitionStatelessFunction1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
// @jsx: preserve
// @noLib: true
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
    }
    interface ElementAttributesProperty { props; }
}
interface OptionPropBag {
    /*pt1*/propx: number
    propString: "hell"
    /*pt2*/optional?: boolean
}
declare function /*opt*/Opt(attributes: OptionPropBag): JSX.Element;
let opt = <[|O/*one*/pt|] />;
let opt1 = <[|Op/*two*/t|] [|pr/*p1*/opx|]={100} />;
let opt2 = <[|Op/*three*/t|] propx={100} [|opt/*p2*/ional|] />;
let opt3 = <[|Op/*four*/t|] wr/*p3*/ong />;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, std::vector<std::string>{"one", "two", "three", "four", "p1", "p2"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxGoToDefinitionStatelessFunction1, TestTsxGoToDefinitionStatelessFunction1);

// tsxGoToDefinitionStatelessFunction2_test.go
static void TestTsxGoToDefinitionStatelessFunction2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
// @jsx: preserve
// @noLib: true
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
    }
    interface ElementAttributesProperty { props; }
}
interface ClickableProps {
    children?: string;
    className?: string;
}
interface ButtonProps extends ClickableProps {
    onClick(event?: React.MouseEvent<HTMLButtonElement>): void;
}
interface LinkProps extends ClickableProps {
    goTo: string;
}
declare function /*firstSource*/MainButton(buttonProps: ButtonProps): JSX.Element;
declare function /*secondSource*/MainButton(linkProps: LinkProps): JSX.Element;
declare function /*thirdSource*/MainButton(props: ButtonProps | LinkProps): JSX.Element;
let opt = <[|Main/*firstTarget*/Button|] />;
let opt = <[|Main/*secondTarget*/Button|] children="chidlren" />;
let opt = <[|Main/*thirdTarget*/Button|] onClick={()=>{}} />;
let opt = <[|Main/*fourthTarget*/Button|] onClick={()=>{}} ignore-prop />;
let opt = <[|Main/*fifthTarget*/Button|] goTo="goTo" />;
let opt = <[|Main/*sixthTarget*/Button|] wrong />;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, std::vector<std::string>{"firstTarget", "secondTarget", "thirdTarget", "fourthTarget", "fifthTarget", "sixthTarget"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxGoToDefinitionStatelessFunction2, TestTsxGoToDefinitionStatelessFunction2);

// tsxGoToDefinitionUnionElementType1_test.go
static void TestTsxGoToDefinitionUnionElementType1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
// @jsx: preserve
// @noLib: true
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
    }
    interface ElementAttributesProperty { props; }
}
function /*pt1*/SFC1(prop: { x: number }) {
    return <div>hello </div>;
};
function SFC2(prop: { x: boolean }) {
    return <h1>World </h1>;
}
var /*def*/SFCComp = SFC1 || SFC2;
<[|SFC/*one*/Comp|] x />)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, std::vector<std::string>{"one"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxGoToDefinitionUnionElementType1, TestTsxGoToDefinitionUnionElementType1);

// tsxGoToDefinitionUnionElementType2_test.go
static void TestTsxGoToDefinitionUnionElementType2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
// @jsx: preserve
// @noLib: true
class RC1 extends React.Component<{}, {}> {
    render() {
        return null;
    }
}
class RC2 extends React.Component<{}, {}> {
    render() {
        return null;
    }
    private method() { }
}
var /*pt1*/RCComp = RC1 || RC2;
<[|RC/*one*/Comp|] />)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineGoToDefinition(t, true, std::vector<std::string>{"one"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxGoToDefinitionUnionElementType2, TestTsxGoToDefinitionUnionElementType2);

// tsxIncrementalServer_test.go
static void TestTsxIncrementalServer(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @lib: es5
/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "");
		f->Insert(t, "<");
		f->Insert(t, "div");
		f->Insert(t, " ");
		f->Insert(t, " id");
		f->Insert(t, "=");
		f->Insert(t, "\"foo");
		f->Insert(t, "\"");
		f->Insert(t, ">");
	});
}
REGISTER_FOURSLASH_TEST(TestTsxIncrementalServer, TestTsxIncrementalServer);

// tsxIncremental_test.go
static void TestTsxIncremental(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->Insert(t, "<");
		f->Insert(t, "div");
		f->Insert(t, " ");
		f->Insert(t, " id");
		f->Insert(t, "=");
		f->Insert(t, "\"foo");
		f->Insert(t, "\"");
		f->Insert(t, ">");
	});
}
REGISTER_FOURSLASH_TEST(TestTsxIncremental, TestTsxIncremental);

// tsxParsing_test.go
static void TestTsxParsing(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(var x = <div id="foo" master="bar"></div>;
var y = /**/x;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyQuickInfoExists(t);
	});
}
REGISTER_FOURSLASH_TEST(TestTsxParsing, TestTsxParsing);

// tsxQuickInfo1_test.go
static void TestTsxQuickInfo1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
var x1 = <di/*1*/v></di/*2*/v>
class MyElement {}
var z = <My/*3*/Element></My/*4*/Element>)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "any", "");
		f->VerifyQuickInfoAt(t, "2", "any", "");
		f->VerifyQuickInfoAt(t, "3", "class MyElement", "");
		f->VerifyQuickInfoAt(t, "4", "class MyElement", "");
	});
}
REGISTER_FOURSLASH_TEST(TestTsxQuickInfo1, TestTsxQuickInfo1);

// tsxQuickInfo2_test.go
static void TestTsxQuickInfo2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
        div: any
    }
}
var x1 = <di/*1*/v></di/*2*/v>
class MyElement {}
var z = <My/*3*/Element></My/*4*/Element>)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(property) JSX.IntrinsicElements.div: any", "");
		f->VerifyQuickInfoAt(t, "2", "(property) JSX.IntrinsicElements.div: any", "");
		f->VerifyQuickInfoAt(t, "3", "class MyElement", "");
		f->VerifyQuickInfoAt(t, "4", "class MyElement", "");
	});
}
REGISTER_FOURSLASH_TEST(TestTsxQuickInfo2, TestTsxQuickInfo2);

// tsxQuickInfo3_test.go
static void TestTsxQuickInfo3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
// @jsx: preserve
// @noLib: true
interface OptionProp {
    propx: 2
}
class Opt extends React.Component<OptionProp, {}> {
    render() {
        return <div>Hello</div>;
    }
}
const obj1: OptionProp = {
    propx: 2
}
let y1 = <O/*1*/pt pro/*2*/px={2} />;
let y2 = <Opt {...ob/*3*/j1} />;
let y2 = <Opt {...obj1} pr/*4*/opx />;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "class Opt", "");
		f->VerifyQuickInfoAt(t, "2", "(property) propx: number", "");
		f->VerifyQuickInfoAt(t, "3", "const obj1: OptionProp", "");
		f->VerifyQuickInfoAt(t, "4", "(property) propx: true", "");
	});
}
REGISTER_FOURSLASH_TEST(TestTsxQuickInfo3, TestTsxQuickInfo3);

// tsxQuickInfo4_test.go
static void TestTsxQuickInfo4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"});
		return;
		const std::string content = R"TS(//@Filename: file.tsx
// @jsx: preserve
// @noLib: true
export interface ClickableProps {
    children?: string;
    className?: string;
}
export interface ButtonProps extends ClickableProps {
    onClick(event?: React.MouseEvent<HTMLButtonElement>): void;
}
export interface LinkProps extends ClickableProps {
    to: string;
}
export function MainButton(buttonProps: ButtonProps): JSX.Element;
export function MainButton(linkProps: LinkProps): JSX.Element;
export function MainButton(props: ButtonProps | LinkProps): JSX.Element {
    const linkProps = props as LinkProps;
    if(linkProps.to) {
        return this._buildMainLink(props);
    }
    return this._buildMainButton(props);
}
function _buildMainButton({ onClick, children, className }: ButtonProps): JSX.Element {
    return(<button className={className} onClick={onClick}>{ children || 'MAIN BUTTON'}</button>);
}
declare function buildMainLink({ to, children, className }: LinkProps): JSX.Element;
function buildSomeElement1(): JSX.Element {
    return (
        <MainB/*1*/utton t/*2*/o='/some/path'>GO</MainButton>
    );
}
function buildSomeElement2(): JSX.Element {
    return (
        <MainB/*3*/utton onC/*4*/lick={()=>{}}>GO</MainButton>;
    );
}
let componenet = <MainButton onClick={()=>{}} ext/*5*/ra-prop>GO</MainButton>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "function MainButton(linkProps: LinkProps): JSX.Element (+1 overload)", "");
		f->VerifyQuickInfoAt(t, "2", "(property) LinkProps.to: string", "");
		f->VerifyQuickInfoAt(t, "3", "function MainButton(buttonProps: ButtonProps): JSX.Element (+1 overload)", "");
		f->VerifyQuickInfoAt(t, "4", "(method) ButtonProps.onClick(event?: React.MouseEvent<HTMLButtonElement>): void", "");
		f->VerifyQuickInfoAt(t, "5", "(property) extra-prop: true", "");
	});
}
REGISTER_FOURSLASH_TEST(TestTsxQuickInfo4, TestTsxQuickInfo4);

// tsxQuickInfo5_test.go
static void TestTsxQuickInfo5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"});
		return;
		const std::string content = R"TS(//@Filename: file.tsx
// @jsx: preserve
// @noLib: true
declare function ComponentWithTwoAttributes<K,V>(l: {key1: K, value: V}): JSX.Element;
function Baz<T,U>(key1: T, value: U) {
    let a0 = <ComponentWi/*1*/thTwoAttributes k/*2*/ey1={key1} val/*3*/ue={value} />
    let a1 = <ComponentWithTwoAttributes {...{key1, value: value}} key="Component" />
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "function ComponentWithTwoAttributes<T, U>(l: {\n    key1: T;\n    value: U;\n}): JSX.Element", "");
		f->VerifyQuickInfoAt(t, "2", "(property) key1: T", "");
		f->VerifyQuickInfoAt(t, "3", "(property) value: U", "");
	});
}
REGISTER_FOURSLASH_TEST(TestTsxQuickInfo5, TestTsxQuickInfo5);

// tsxQuickInfo6_test.go
static void TestTsxQuickInfo6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"});
		return;
		const std::string content = R"TS(//@Filename: file.tsx
// @jsx: preserve
// @noLib: true
declare function ComponentSpecific<U>(l: {prop: U}): JSX.Element;
declare function ComponentSpecific1<U>(l: {prop: U, "ignore-prop": number}): JSX.Element;
function Bar<T extends {prop: number}>(arg: T) {
    let a1 = <Compone/*1*/ntSpecific {...arg} ignore-prop="hi" />;  // U is number
    let a2 = <ComponentSpecific1 {...arg} ignore-prop={10} />;  // U is number
    let a3 = <Component/*2*/Specific {...arg} prop="hello" />;   // U is "hello"
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "function ComponentSpecific<number>(l: {\n    prop: number;\n}): JSX.Element", "");
		f->VerifyQuickInfoAt(t, "2", "function ComponentSpecific<never>(l: {\n    prop: never;\n}): JSX.Element", "");
	});
}
REGISTER_FOURSLASH_TEST(TestTsxQuickInfo6, TestTsxQuickInfo6);

// tsxQuickInfo7_test.go
static void TestTsxQuickInfo7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"});
		return;
		const std::string content = R"TS(//@Filename: file.tsx
// @jsx: preserve
// @noLib: true
declare function OverloadComponent<U>(attr: {b: U, a?: string, "ignore-prop": boolean}): JSX.Element;
declare function OverloadComponent<T, U>(attr: {b: U, a: T}): JSX.Element;
declare function OverloadComponent(): JSX.Element; // effective argument type of )TS" +std::string("`") +std::string(R"TS({})TS") +std::string("`") +std::string(R"TS(, needs to be last
function Baz<T extends {b: number}, U extends {a: boolean, b:string}>(arg1: T, arg2: U) {
    let a0 = <Overloa/*1*/dComponent {...arg1} a="hello" ignore-prop />;
    let a1 = <Overloa/*2*/dComponent {...arg2} ignore-pro="hello world" />;
    let a2 = <Overloa/*3*/dComponent {...arg2} />;
    let a3 = <Overloa/*4*/dComponent {...arg1} ignore-prop />;
    let a4 = <Overloa/*5*/dComponent />;
    let a5 = <Overloa/*6*/dComponent {...arg2} ignore-prop="hello" {...arg1} />;
    let a6 = <Overloa/*7*/dComponent {...arg1} ignore-prop {...arg2} />;
})TS");
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "function OverloadComponent<number>(attr: {\n    b: number;\n    a?: string;\n    \"ignore-prop\": boolean;\n}): JSX.Element (+2 overloads)", "");
		f->VerifyQuickInfoAt(t, "2", "function OverloadComponent<boolean, string>(attr: {\n    b: string;\n    a: boolean;\n}): JSX.Element (+2 overloads)", "");
		f->VerifyQuickInfoAt(t, "3", "function OverloadComponent<boolean, string>(attr: {\n    b: string;\n    a: boolean;\n}): JSX.Element (+2 overloads)", "");
		f->VerifyQuickInfoAt(t, "4", "function OverloadComponent(): JSX.Element (+2 overloads)", "");
		f->VerifyQuickInfoAt(t, "5", "function OverloadComponent(): JSX.Element (+2 overloads)", "");
		f->VerifyQuickInfoAt(t, "6", "function OverloadComponent<boolean, never>(attr: {\n    b: never;\n    a: boolean;\n}): JSX.Element (+2 overloads)", "");
		f->VerifyQuickInfoAt(t, "7", "function OverloadComponent<boolean, never>(attr: {\n    b: never;\n    a: boolean;\n}): JSX.Element (+2 overloads)", "");
	});
}
REGISTER_FOURSLASH_TEST(TestTsxQuickInfo7, TestTsxQuickInfo7);

// tsxRename1_test.go
static void TestTsxRename1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
        [|[|{| "contextRangeIndex": 0 |}div|]: {
            name?: string;
            isOpen?: boolean;
        };|]
        span: { n: string; };
    }
}
var x = [|<[|{| "contextRangeIndex": 2 |}div|] />|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr , std::vector<std::string>{"div"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxRename1, TestTsxRename1);

// tsxRename2_test.go
static void TestTsxRename2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
        div: {
            [|[|{| "contextRangeIndex": 0 |}name|]?: string;|]
            isOpen?: boolean;
        };
        span: { n: string; };
    }
}
var x = <div [|[|{| "contextRangeIndex": 2 |}name|]="hello"|] />;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr , std::vector<std::string>{"name"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxRename2, TestTsxRename2);

// tsxRename3_test.go
static void TestTsxRename3(gostd::testing::T* t) {
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
    [|[|{| "contextRangeIndex": 0 |}name|]?: string;|]
    size?: number;
}


var x = <MyClass [|[|{| "contextRangeIndex": 2 |}name|]='hello'|]/>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr , std::vector<std::string>{"name"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxRename3, TestTsxRename3);

// tsxRename4_test.go
static void TestTsxRename4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @jsx: preserve
//@Filename: file.tsx
declare namespace JSX {
    interface Element {}
    interface IntrinsicElements {
        div: {};
    }
}
[|class [|{| "contextRangeIndex": 0 |}MyClass|] {}|]

[|<[|{| "contextRangeIndex": 2 |}MyClass|]></[|{| "contextRangeIndex": 2 |}MyClass|]>|];
[|<[|{| "contextRangeIndex": 5 |}MyClass|]/>|];

[|<[|{| "contextRangeIndex": 7 |}div|]> </[|{| "contextRangeIndex": 7 |}div|]>|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr , std::vector<std::string>{"MyClass", "div"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxRename4, TestTsxRename4);

// tsxRename5_test.go
static void TestTsxRename5(gostd::testing::T* t) {
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

[|var [|{| "contextRangeIndex": 0 |}nn|]: string;|]
var x = <MyClass name={[|nn|]}></MyClass>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr , std::vector<std::string>{"nn"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxRename5, TestTsxRename5);

// tsxRename6_test.go
static void TestTsxRename6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
// @jsx: preserve
// @noLib: true
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
    }
    interface ElementAttributesProperty { props; }
}
interface OptionPropBag {
    propx: number
    propString: string
    optional?: boolean
}
[|declare function [|{| "contextRangeIndex": 0 |}Opt|](attributes: OptionPropBag): JSX.Element;|]
let opt = [|<[|{| "contextRangeIndex": 2 |}Opt|] />|];
let opt1 = [|<[|{| "contextRangeIndex": 4 |}Opt|] propx={100} propString />|];
let opt2 = [|<[|{| "contextRangeIndex": 6 |}Opt|] propx={100} optional/>|];
let opt3 = [|<[|{| "contextRangeIndex": 8 |}Opt|] wrong />|];
let opt4 = [|<[|{| "contextRangeIndex": 10 |}Opt|] propx={100} propString="hi" />|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr , std::vector<std::string>{"Opt"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxRename6, TestTsxRename6);

// tsxRename7_test.go
static void TestTsxRename7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
// @jsx: preserve
// @noLib: true
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
    }
    interface ElementAttributesProperty { props; }
}
interface OptionPropBag {
    [|[|{| "contextRangeIndex": 0 |}propx|]: number|]
    propString: string
    optional?: boolean
}
declare function Opt(attributes: OptionPropBag): JSX.Element;
let opt = <Opt />;
let opt1 = <Opt [|[|{| "contextRangeIndex": 2 |}propx|]={100}|] propString />;
let opt2 = <Opt [|[|{| "contextRangeIndex": 4 |}propx|]={100}|] optional/>;
let opt3 = <Opt wrong />;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr , std::vector<std::string>{"propx"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxRename7, TestTsxRename7);

// tsxRename8_test.go
static void TestTsxRename8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
// @jsx: preserve
// @noLib: true
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
    }
    interface ElementAttributesProperty { props; }
}
interface OptionPropBag {
    propx: number
    propString: string
    optional?: boolean
}
declare function Opt(attributes: OptionPropBag): JSX.Element;
let opt = <Opt />;
let opt1 = <Opt propx={100} propString />;
let opt2 = <Opt propx={100} optional/>;
let opt3 = <Opt [|wrong|] />;
let opt4 = <Opt propx={100} propString="hi" />;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr , std::vector<fourslash::MarkerOrRangeOrName>{});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxRename8, TestTsxRename8);

// tsxRename9_test.go
static void TestTsxRename9(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(//@Filename: file.tsx
// @jsx: preserve
// @noLib: true
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
    }
    interface ElementAttributesProperty { props; }
}
interface ClickableProps {
    children?: string;
    className?: string;
}
interface ButtonProps extends ClickableProps {
    [|[|{| "contextRangeIndex": 0 |}onClick|](event?: React.MouseEvent<HTMLButtonElement>): void;|]
}
interface LinkProps extends ClickableProps {
    [|[|{| "contextRangeIndex": 2 |}goTo|]: string;|]
}
[|declare function [|{| "contextRangeIndex": 4 |}MainButton|](buttonProps: ButtonProps): JSX.Element;|]
[|declare function [|{| "contextRangeIndex": 6 |}MainButton|](linkProps: LinkProps): JSX.Element;|]
[|declare function [|{| "contextRangeIndex": 8 |}MainButton|](props: ButtonProps | LinkProps): JSX.Element;|]
let opt = [|<[|{| "contextRangeIndex": 10 |}MainButton|] />|];
let opt = [|<[|{| "contextRangeIndex": 12 |}MainButton|] children="chidlren" />|];
let opt = [|<[|{| "contextRangeIndex": 14 |}MainButton|] [|[|{| "contextRangeIndex": 16 |}onClick|]={()=>{}}|] />|];
let opt = [|<[|{| "contextRangeIndex": 18 |}MainButton|] [|[|{| "contextRangeIndex": 20 |}onClick|]={()=>{}}|] [|ignore-prop|] />|];
let opt = [|<[|{| "contextRangeIndex": 23 |}MainButton|] [|[|{| "contextRangeIndex": 25 |}goTo|]="goTo"|] />|];
let opt = [|<[|{| "contextRangeIndex": 27 |}MainButton|] [|wrong|] />|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr , std::vector<std::string>{"onClick", "goTo", "MainButton", "ignore-prop", "wrong"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxRename9, TestTsxRename9);

// tsxSignatureHelp1_test.go
static void TestTsxSignatureHelp1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @jsx: preserve
//@Filename: file.tsx
import React = require('react');
export interface ClickableProps {
    children?: string;
    className?: string;
}
export interface ButtonProps extends ClickableProps {
    onClick(event?: React.MouseEvent<HTMLButtonElement>): void;
}
function _buildMainButton({ onClick, children, className }: ButtonProps): JSX.Element {
    return(<button className={className} onClick={onClick}>{ children || 'MAIN BUTTON'}</button>);
}
export function MainButton(props: ButtonProps): JSX.Element {
    return this._buildMainButton(props);
}
let e1 = <MainButton/*1*/ /*2*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "MainButton(props: ButtonProps): JSX.Element", .ParameterSpan = "props: ButtonProps"});
		f->GoToMarker(t, "2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "MainButton(props: ButtonProps): JSX.Element", .ParameterSpan = "props: ButtonProps"});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxSignatureHelp1, TestTsxSignatureHelp1);

// tsxSignatureHelp2_test.go
static void TestTsxSignatureHelp2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @jsx: preserve
//@Filename: file.tsx
import React = require('react');
export interface ClickableProps {
    children?: string;
    className?: string;
}
export interface ButtonProps extends ClickableProps {
    onClick(event?: React.MouseEvent<HTMLButtonElement>): void;
}
export interface LinkProps extends ClickableProps {
    goTo(where: "home" | "contact"): void;
}
function _buildMainButton({ onClick, children, className }: ButtonProps): JSX.Element {
    return(<button className={className} onClick={onClick}>{ children || 'MAIN BUTTON'}</button>);
}
export function MainButton(buttonProps: ButtonProps): JSX.Element;
export function MainButton(linkProps: LinkProps): JSX.Element;
export function MainButton(props: ButtonProps | LinkProps): JSX.Element {
    return this._buildMainButton(props);
}
let e1 = <MainButton/*1*/ /*2*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "MainButton(buttonProps: ButtonProps): JSX.Element", .ParameterSpan = "buttonProps: ButtonProps", .OverloadsCount = 2});
		f->GoToMarker(t, "2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "MainButton(buttonProps: ButtonProps): JSX.Element", .ParameterSpan = "buttonProps: ButtonProps", .OverloadsCount = 2});
	});
}
REGISTER_FOURSLASH_TEST(TestTsxSignatureHelp2, TestTsxSignatureHelp2);


}  // namespace
