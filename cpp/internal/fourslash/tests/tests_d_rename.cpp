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


static void TestRenameNamedImportUseAliasesForRenames(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
import { /*import*/MyTypeA } from "./b";
const type: MyTypeA = { foo: "bar" };
// @Filename: /b.ts
export interface MyTypeA {
    foo: string;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::False}), {"import"});
		f->VerifyBaselineRename(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::True}), {"import"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameNamedImportUseAliasesForRenames, TestRenameNamedImportUseAliasesForRenames);

static void TestRenameNamedImportDefaultInNodeModules(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /index.ts
import { /*fooImport*/[|Foo|] } from "foo";
declare const f: Foo;
// @Filename: /tsconfig.json
{}
// @Filename: /node_modules/foo/package.json
{ "types": "index.d.ts" }
// @Filename: /node_modules/foo/index.d.ts
export interface Foo {
    bar: string;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {"fooImport"});
		f->VerifyBaselineRename(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::True}), {"fooImport"});
		f->GoToMarker(t, "fooImport");
		f->VerifyRenameFailed(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::False}));
	});
}
REGISTER_FOURSLASH_TEST(TestRenameNamedImportDefaultInNodeModules, TestRenameNamedImportDefaultInNodeModules);


static void TestRenameUnresolvedReexport1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export { [|jsonSchema|] } from "@internal/ai-sdk-v4";
// @Filename: /b.ts
import { jsonSchema } from "./a";
)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[0]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameUnresolvedReexport1, TestRenameUnresolvedReexport1);


static void TestRenameUMDModuleAlias2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: 0.d.ts
export function doThing(): string;
export function doTheOtherThing(): void;
export as namespace /**/[|myLib|];
// @Filename: 1.ts
/// <reference path="0.d.ts" />
myLib.doThing();)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyRenameSucceeded(t, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestRenameUMDModuleAlias2, TestRenameUMDModuleAlias2);


static void TestRenameUMDModuleAlias1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: 0.d.ts
export function doThing(): string;
export function doTheOtherThing(): void;
[|export as namespace [|{| "contextRangeIndex": 0 |}myLib|];|]
// @Filename: 1.ts
/// <reference path="0.d.ts" />
[|myLib|].doThing();)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"myLib"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameUMDModuleAlias1, TestRenameUMDModuleAlias1);


static void TestRenameThis(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f([|this|]) {
    return [|this|];
}
this/**/;
const _ = { [|[|{| "contextRangeIndex": 2 |}this|]: 0|] }.[|this|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyRenameFailed(t, nullptr);
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[0], f->Ranges()[1], f->Ranges()[3], f->Ranges()[4]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameThis, TestRenameThis);


static void TestRenameTemplateLiteralsDefinePropertyJs(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
let obj = {};

Object.defineProperty(obj, `[|prop|]`, { value: 0 });

obj = {
    [|[`[|{| "contextRangeIndex": 1 |}prop|]`]: 1|]
};

obj.[|prop|];
obj['[|prop|]'];
obj["[|prop|]"];
obj[`[|prop|]`];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"prop"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameTemplateLiteralsDefinePropertyJs, TestRenameTemplateLiteralsDefinePropertyJs);


static void TestRenameTemplateLiteralsComputedProperties(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a.ts
interface Obj {
    [|[`[|{| "contextRangeIndex": 0 |}num|]`]: number;|]
    [|['[|{| "contextRangeIndex": 2 |}bool|]']: boolean;|]
}

let o: Obj = {
    [|[`[|{| "contextRangeIndex": 4 |}num|]`]: 0|],
    [|['[|{| "contextRangeIndex": 6 |}bool|]']: true|],
};

o = {
    [|['[|{| "contextRangeIndex": 8 |}num|]']: 1|],
    [|[`[|{| "contextRangeIndex": 10 |}bool|]`]: false|],
};

o.[|num|];
o['[|num|]'];
o["[|num|]"];
o[`[|num|]`];

o.[|bool|];
o['[|bool|]'];
o["[|bool|]"];
o[`[|bool|]`];

export { o };
// @allowJs: true
// @Filename: b.js
import { o as obj } from './a';

obj.[|num|];
obj[`[|num|]`];

obj.[|bool|];
obj[`[|bool|]`];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"num", "bool"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameTemplateLiteralsComputedProperties, TestRenameTemplateLiteralsComputedProperties);


static void TestRenameStringPropertyNames(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var o = {
    [|[|{| "contextRangeIndex": 0 |}prop|]: 0|]
};

o = {
    [|"[|{| "contextRangeIndex": 2 |}prop|]": 1|]
};

o["[|prop|]"];
o['[|prop|]'];
o.[|prop|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"prop"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameStringPropertyNames, TestRenameStringPropertyNames);


static void TestRenameStringPropertyNames2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type Props = {
  foo: boolean;
}

let { foo }: Props = null as any;
foo;

let asd: Props = { "foo"/**/: true }; // rename foo here)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameStringPropertyNames2, TestRenameStringPropertyNames2);


static void TestRenameStringLiteralTypes5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type T = {
    "Prop 1": string;
}

declare const fn: <K extends keyof T>(p: K) => void

fn("Prop 1"/**/))TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameStringLiteralTypes5, TestRenameStringLiteralTypes5);


static void TestRenameStringLiteralTypes4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    "Prop 1": string;
}

declare const fn: <K extends keyof I>(p: K) => void

fn("Prop 1"/**/))TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameStringLiteralTypes4, TestRenameStringLiteralTypes4);


static void TestRenameStringLiteralTypes3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type Foo = "[|a|]" | "b";

class C {
    p: Foo = "[|a|]";
    m() {
        switch (this.p) {
            case "[|a|]":
                return 1;
            case "b":
                return 2;
        }
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"a"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameStringLiteralTypes3, TestRenameStringLiteralTypes3);


static void TestRenameStringLiteralTypes2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type Foo = "[|a|]" | "b";

class C {
    p: Foo = "[|a|]";
    m() {
        if (this.p === "[|a|]") {}
        if ("[|a|]" === this.p) {}

        if (this.p !== "[|a|]") {}
        if ("[|a|]" !== this.p) {}

        if (this.p == "[|a|]") {}
        if ("[|a|]" == this.p) {}

        if (this.p != "[|a|]") {}
        if ("[|a|]" != this.p) {}
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"a"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameStringLiteralTypes2, TestRenameStringLiteralTypes2);


static void TestRenameStringLiteralTypes1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface AnimationOptions {
    deltaX: number;
    deltaY: number;
    easing: "ease-in" | "ease-out" | "[|ease-in-out|]";
}

function animate(o: AnimationOptions) { }

animate({ deltaX: 100, deltaY: 100, easing: "[|ease-in-out|]" });)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"ease-in-out"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameStringLiteralTypes1, TestRenameStringLiteralTypes1);


static void TestRenameStringLiteralOk(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Foo {
    f: '[|foo|]' | 'bar'
}
const d: 'foo' = 'foo'
declare const f: Foo
f.f = '[|foo|]'
f.f = `[|foo|]`)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"foo"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameStringLiteralOk, TestRenameStringLiteralOk);


static void TestRenameStringLiteralOk1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare function f(): '[|foo|]' | 'bar'
class Foo {
    f = f()
}
const d: 'foo' = 'foo'
declare const ff: Foo
ff.f = '[|foo|]')TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"foo"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameStringLiteralOk1, TestRenameStringLiteralOk1);


static void TestRenameRest(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Gen {
    x: number;
    [|[|{| "contextRangeIndex": 0 |}parent|]: Gen;|]
    millenial: string;
}
let t: Gen;
var { x, ...rest } = t;
rest.[|parent|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"parent"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameRest, TestRenameRest);


static void TestRenameRestBindingElement(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    a: number;
    b: number;
    c: number;
}
function foo([|{ a, ...[|{| "contextRangeIndex": 0 |}rest|] }: I|]) {
    [|rest|];
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::True}), {f->Ranges()[1]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameRestBindingElement, TestRenameRestBindingElement);


static void TestRenameReferenceFromLinkTag5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(enum E {
    /** {@link E./**/A} */
    A
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameReferenceFromLinkTag5, TestRenameReferenceFromLinkTag5);


static void TestRenameReferenceFromLinkTag4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(enum E {
    /** {@link /**/B} */
    A,
    B
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameReferenceFromLinkTag4, TestRenameReferenceFromLinkTag4);


static void TestRenameReferenceFromLinkTag3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: a.ts
interface Foo {
    foo: E.Foo;
}
// @Filename: b.ts
enum E {
    /** {@link /**/Foo} */
    Foo
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameReferenceFromLinkTag3, TestRenameReferenceFromLinkTag3);


static void TestRenameReferenceFromLinkTag2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
enum E {
    /** {@link /**/Foo} */
    Foo
}
interface Foo {
    foo: E.Foo;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameReferenceFromLinkTag2, TestRenameReferenceFromLinkTag2);


static void TestRenameReferenceFromLinkTag1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(enum E {
    /** {@link /**/A} */
    A
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameReferenceFromLinkTag1, TestRenameReferenceFromLinkTag1);


static void TestRenameReExportDefault(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export { default } from "./b";
[|export { default as [|{| "contextRangeIndex": 0 |}b|] } from "./b";|]
export { default as bee } from "./b";
[|import { default as [|{| "contextRangeIndex": 2 |}b|] } from "./b";|]
import { default as bee } from "./b";
[|import [|{| "contextRangeIndex": 4 |}b|] from "./b";|]
// @Filename: /b.ts
[|const [|{| "contextRangeIndex": 6 |}b|] = 0;|]
[|export default [|{| "contextRangeIndex": 8 |}b|];|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[3], f->Ranges()[5], f->Ranges()[7], f->Ranges()[9]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameReExportDefault, TestRenameReExportDefault);


static void TestRenameQuotedSingleCharacterPropertyName1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
const obj = {
  "'"/**/: 1,
}
)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameQuotedSingleCharacterPropertyName1, TestRenameQuotedSingleCharacterPropertyName1);


static void TestRenamePropertyAccessExpressionHeritageClause(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class B {}
function foo() {
    return {[|[|{| "contextRangeIndex": 0 |}B|]: B|]};
}
class C extends (foo()).[|B|] {}
class C1 extends foo().[|B|] {})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"B"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenamePropertyAccessExpressionHeritageClause, TestRenamePropertyAccessExpressionHeritageClause);


static void TestRenamePrivateMethod(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
   [|[|{| "contextRangeIndex": 0 |}#foo|]() { }|]
   callFoo() {
       return this.[|#foo|]();
   }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, asMonVec(tsu::ToAny(f->GetRangesByText()->Get("#foo"))));
	});
}
REGISTER_FOURSLASH_TEST(TestRenamePrivateMethod, TestRenamePrivateMethod);


static void TestRenamePrivateFields(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
   [|/**/#foo|] = 1;

   getFoo() {
       return this.#foo;
   }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyRenameSucceeded(t, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestRenamePrivateFields, TestRenamePrivateFields);


static void TestRenamePrivateFields1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
   [|[|{| "contextRangeIndex": 0 |}#foo|] = 1;|]

   getFoo() {
       return this.[|#foo|];
   }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"#foo"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenamePrivateFields1, TestRenamePrivateFields1);


static void TestRenamePrivateAccessor(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
   [|get [|{| "contextRangeIndex": 0 |}#foo|]() { return 1 }|]
   [|set [|{| "contextRangeIndex": 2 |}#foo|](value: number) { }|]
   retFoo() {
       return this.[|#foo|];
   }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, asMonVec(tsu::ToAny(f->GetRangesByText()->Get("#foo"))));
	});
}
REGISTER_FOURSLASH_TEST(TestRenamePrivateAccessor, TestRenamePrivateAccessor);


static void TestRenameParameterPropertyDeclaration5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
    constructor([|protected [ [|{| "contextRangeIndex": 0 |}protectedParam|] ]|]) {
        let myProtectedParam = [|protectedParam|];
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"protectedParam"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameParameterPropertyDeclaration5, TestRenameParameterPropertyDeclaration5);


static void TestRenameParameterPropertyDeclaration4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
    constructor([|protected { [|{| "contextRangeIndex": 0 |}protectedParam|] }|]) {
        let myProtectedParam = [|protectedParam|];
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[2]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameParameterPropertyDeclaration4, TestRenameParameterPropertyDeclaration4);


static void TestRenameParameterPropertyDeclaration3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
    constructor([|protected [|{| "contextRangeIndex": 0 |}protectedParam|]: number|]) {
        let protectedParam = [|protectedParam|];
        this.[|protectedParam|] += 10;
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"protectedParam"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameParameterPropertyDeclaration3, TestRenameParameterPropertyDeclaration3);


static void TestRenameParameterPropertyDeclaration2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
    constructor([|public [|{| "contextRangeIndex": 0 |}publicParam|]: number|]) {
        let publicParam = [|publicParam|];
        this.[|publicParam|] += 10;
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"publicParam"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameParameterPropertyDeclaration2, TestRenameParameterPropertyDeclaration2);


static void TestRenameParameterPropertyDeclaration1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
    constructor([|private [|{| "contextRangeIndex": 0 |}privateParam|]: number|]) {
        let localPrivate = [|privateParam|];
        this.[|privateParam|] += 10;
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"privateParam"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameParameterPropertyDeclaration1, TestRenameParameterPropertyDeclaration1);


static void TestRenameObjectSpread(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface A1 { [|[|{| "contextRangeIndex": 0 |}a|]: number|] };
interface A2 { [|[|{| "contextRangeIndex": 2 |}a|]?: number|] };
let a1: A1;
let a2: A2;
let a12 = { ...a1, ...a2 };
a12.[|a|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[3], f->Ranges()[4]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameObjectSpread, TestRenameObjectSpread);


static void TestRenameObjectSpreadAssignment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface A1 { a: number };
interface A2 { a?: number };
[|let [|{| "contextRangeIndex": 0 |}a1|]: A1;|]
[|let [|{| "contextRangeIndex": 2 |}a2|]: A2;|]
let a12 = { ...[|a1|], ...[|a2|] };)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[4], f->Ranges()[3], f->Ranges()[5]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameObjectSpreadAssignment, TestRenameObjectSpreadAssignment);


static void TestRenameObjectBindingElementPropertyName01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    [|[|{| "contextRangeIndex": 0 |}property1|]: number;|]
    property2: string;
}

var foo: I;
[|var { [|{| "contextRangeIndex": 2 |}property1|]: prop1 } = foo;|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"property1"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameObjectBindingElementPropertyName01, TestRenameObjectBindingElementPropertyName01);


static void TestRenameNumericalIndexSingleQuoted(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(const foo = { [|0|]: true };
foo[[|0|]];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.QuotePreference = lsutil::QuotePreference("single")}), {"0"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameNumericalIndexSingleQuoted, TestRenameNumericalIndexSingleQuoted);


static void TestRenameNoDefaultLib(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @checkJs: true
// @allowJs: true
// @Filename: /foo.js
// @ts-check
const [|/**/foo|] = 1;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyRenameSucceeded(t, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestRenameNoDefaultLib, TestRenameNoDefaultLib);


static void TestRenameNamespace(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace /**/NS {
    export const enum E {
        A = 'a'
    }
}

const a: NS.E = NS.E.A;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameNamespace, TestRenameNamespace);


static void TestRenameNamespaceImport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/lib/tsconfig.json
{ "compilerOptions": { "lib": ["es5"] } }
// @Filename: /home/src/workspaces/project/lib/index.ts
const unrelatedLocalVariable = 123;
export const someExportedVariable = unrelatedLocalVariable;
// @Filename: /home/src/workspaces/project/src/tsconfig.json
{ "compilerOptions": { "lib": ["es5"] } }
// @Filename: /home/src/workspaces/project/src/index.ts
import * as /*i*/lib from '../lib/index';
lib.someExportedVariable;
// @Filename: /home/src/workspaces/project/tsconfig.json
{ "compilerOptions": { "lib": ["es5"] } })TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToFile(t, "/home/src/workspaces/project/lib/index.ts");
		f->GoToFile(t, "/home/src/workspaces/project/src/index.ts");
		f->VerifyBaselineRename(t, nullptr, {"i"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameNamespaceImport, TestRenameNamespaceImport);


static void TestRenameNamedImport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /home/src/workspaces/project/lib/tsconfig.json
{ "compilerOptions": { "lib": ["es5"] } }
// @Filename: /home/src/workspaces/project/lib/index.ts
const unrelatedLocalVariable = 123;
export const someExportedVariable = unrelatedLocalVariable;
// @Filename: /home/src/workspaces/project/src/tsconfig.json
{ "compilerOptions": { "lib": ["es5"] } }
// @Filename: /home/src/workspaces/project/src/index.ts
import { /*i*/someExportedVariable } from '../lib/index';
someExportedVariable;
// @Filename: /home/src/workspaces/project/tsconfig.json
{ "compilerOptions": { "lib": ["es5"] } })TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToFile(t, "/home/src/workspaces/project/lib/index.ts");
		f->GoToFile(t, "/home/src/workspaces/project/src/index.ts");
		f->VerifyBaselineRename(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::True}), {"i"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameNamedImport, TestRenameNamedImport);


static void TestRenameNameOnEnumMember(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(enum e {
    firstMember,
    secondMember,
    thirdMember
}
var enumMember = e.[|/**/thirdMember|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyRenameSucceeded(t, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestRenameNameOnEnumMember, TestRenameNameOnEnumMember);


static void TestRenameModuleToVar(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface IMod {
    y: number;
}
declare module/**/ X: IMod;// {
//    export var y: numb;
var y: number;
namespace Y {
    var z = y + 5;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->Backspace(t, 6);
		f->Insert(t, "var");
		f->VerifyNoErrors(t);
	});
}
REGISTER_FOURSLASH_TEST(TestRenameModuleToVar, TestRenameModuleToVar);


static void TestRenameModuleExportsProperties3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
[|class [|{| "contextRangeIndex": 0 |}A|] {}|]
module.exports = { [|A|] })TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::True}), {f->Ranges()[1], f->Ranges()[2]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameModuleExportsProperties3, TestRenameModuleExportsProperties3);


static void TestRenameModuleExportsProperties1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|class [|{| "contextRangeIndex": 0 |}A|] {}|]
module.exports = { [|A|] })TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::True}), {f->Ranges()[1], f->Ranges()[2]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameModuleExportsProperties1, TestRenameModuleExportsProperties1);


static void TestRenameModifiers(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|[|declare|] [|abstract|] class [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeDelta": -3 |}C1|] {
    [|[|static|] [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeDelta": -2 |}a|];|]
    [|[|readonly|] [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeDelta": -2 |}b|];|]
    [|[|public|] [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeDelta": -2 |}c|];|]
    [|[|protected|] [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeDelta": -2 |}d|];|]
    [|[|private|] [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeDelta": -2 |}e|];|]
}|]
[|[|const|] enum [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeDelta": -2 |}E|] {
}|]
[|[|async|] function [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeDelta": -2 |}fn|]() {}|]
[|[|export|] [|default|] class [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeDelta": -3 |}C2|] {}|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[2], f->Ranges()[5], f->Ranges()[8], f->Ranges()[11], f->Ranges()[14], f->Ranges()[17], f->Ranges()[20], f->Ranges()[23], f->Ranges()[26], f->Ranges()[27]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameModifiers, TestRenameModifiers);


static void TestRenameLocationsForFunctionExpression02(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f() {

}
var x = [|function [|{| "contextRangeIndex": 0 |}f|](g: any, h: any) {

    let helper = function f(): any { f(); }

    let foo = () => [|f|]([|f|], g);
}|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"f"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameLocationsForFunctionExpression02, TestRenameLocationsForFunctionExpression02);


static void TestRenameLocationsForFunctionExpression01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var x = [|function [|{| "contextRangeIndex": 0 |}f|](g: any, h: any) {
    [|f|]([|f|], g);
}|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"f"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameLocationsForFunctionExpression01, TestRenameLocationsForFunctionExpression01);


static void TestRenameLocationsForClassExpression01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
}

var x = [|class [|{| "contextRangeIndex": 0 |}Foo|] {
    doIt() {
        return [|Foo|];
    }

    static doItStatically() {
        return [|Foo|].y;
    }
}|]

var y = class {
   getSomeName() {
      return Foo
   }
}
var z = class Foo {})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"Foo"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameLocationsForClassExpression01, TestRenameLocationsForClassExpression01);


static void TestRenameLabel6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(loop1: for (let i = 0; i <= 10; i++) {
    loop2: for (let j = 0; j <= 10; j++) {
        if (i === 5) continue loop1;
        if (j === 5) break /**/loop2;
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameLabel6, TestRenameLabel6);


static void TestRenameLabel5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(loop1: for (let i = 0; i <= 10; i++) {
    loop2: for (let j = 0; j <= 10; j++) {
        if (i === 5) continue /**/loop1;
        if (j === 5) break loop2;
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameLabel5, TestRenameLabel5);


static void TestRenameLabel4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(loop:
for (let i = 0; i <= 10; i++) {
   if (i === 0) continue loop;
   if (i === 1) continue /**/loop;
   if (i === 10) break loop;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameLabel4, TestRenameLabel4);


static void TestRenameLabel3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/**/loop:
for (let i = 0; i <= 10; i++) {
   if (i === 0) continue loop;
   if (i === 1) continue loop;
   if (i === 10) break loop;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameLabel3, TestRenameLabel3);


static void TestRenameLabel2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/**/foo: {
    break foo;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameLabel2, TestRenameLabel2);


static void TestRenameLabel1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(foo: {
    break /**/foo;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameLabel1, TestRenameLabel1);


static void TestRenameJsThisProperty06(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
var C = class {
  constructor(y) {
    this.x = y;
  }
}
[|C.prototype.[|{| "contextRangeIndex": 0 |}z|] = 1;|]
var t = new C(12);
[|t.[|{| "contextRangeIndex": 2 |}z|] = 11;|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"z"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameJsThisProperty06, TestRenameJsThisProperty06);


static void TestRenameJsThisProperty05(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
class C {
  constructor(y) {
    this.x = y;
  }
}
[|C.prototype.[|{| "contextRangeIndex": 0 |}z|] = 1;|]
var t = new C(12);
[|t.[|{| "contextRangeIndex": 2 |}z|] = 11;|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"z"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameJsThisProperty05, TestRenameJsThisProperty05);


static void TestRenameJsThisProperty03(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
class C {
  constructor(y) {
    [|this.[|{| "contextRangeIndex": 0 |}x|] = y;|]
  }
}
var t = new C(12);
[|t.[|{| "contextRangeIndex": 2 |}x|] = 11;|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"x"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameJsThisProperty03, TestRenameJsThisProperty03);


static void TestRenameJsThisProperty01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
function bar() {
    [|this.[|{| "contextRangeIndex": 0 |}x|] = 10;|]
}
var t = new bar();
[|t.[|{| "contextRangeIndex": 2 |}x|] = 11;|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"x"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameJsThisProperty01, TestRenameJsThisProperty01);


static void TestRenameJsSpecialAssignmentRhs2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
const foo = {
    set: function (x) {
        this._x = x;
    },
    copy: function ([|x|]) {
        this._x = [|x|].prop;
    }
};)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameJsSpecialAssignmentRhs2, TestRenameJsSpecialAssignmentRhs2);


static void TestRenameJsSpecialAssignmentRhs1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
const foo = {
    set: function (x) {
        this._x = x;
    },
    copy: function ([|x|]) {
        this._x = [|x|].prop;
    }
};)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameJsSpecialAssignmentRhs1, TestRenameJsSpecialAssignmentRhs1);


static void TestRenameJsPrototypeProperty02(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
function bar() {
}
[|bar.prototype.[|{| "contextRangeIndex": 0 |}x|] = 10;|]
var t = new bar();
[|t.[|{| "contextRangeIndex": 2 |}x|] = 11;|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"x"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameJsPrototypeProperty02, TestRenameJsPrototypeProperty02);


static void TestRenameJsPrototypeProperty01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
function bar() {
}
[|bar.prototype.[|{| "contextRangeIndex": 0 |}x|] = 10;|]
var t = new bar();
[|t.[|{| "contextRangeIndex": 2 |}x|] = 11;|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"x"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameJsPrototypeProperty01, TestRenameJsPrototypeProperty01);


static void TestRenameJsPropertyAssignment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
function bar() {
}
[|bar.[|{| "contextRangeIndex": 0 |}foo|] = "foo";|]
console.log(bar.[|foo|]);)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"foo"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameJsPropertyAssignment, TestRenameJsPropertyAssignment);


static void TestRenameJsPropertyAssignment4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
function f() {
   var /*1*/foo = this;
   /*2*/foo.x = 1;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToFile(t, "/a.js");
		f->VerifyBaselineRename(t, nullptr, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameJsPropertyAssignment4, TestRenameJsPropertyAssignment4);


static void TestRenameJsPropertyAssignment3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
var C = class  {
}
[|C.[|{| "contextRangeIndex": 0 |}staticProperty|] = "string";|]
console.log(C.[|staticProperty|]);)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"staticProperty"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameJsPropertyAssignment3, TestRenameJsPropertyAssignment3);


static void TestRenameJsPropertyAssignment2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
class Minimatch {
}
[|Minimatch.[|{| "contextRangeIndex": 0 |}staticProperty|] = "string";|]
console.log(Minimatch.[|staticProperty|]);)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"staticProperty"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameJsPropertyAssignment2, TestRenameJsPropertyAssignment2);


static void TestRenameJsOverloadedFunctionParameter(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: foo.js
/**
 * @overload
 * @param {number} x
 * @returns {number}
 *
 * @overload
 * @param {string} x
 * @returns {string} 
 *
 * @param {unknown} x
 * @returns {unknown} 
 */
function foo(x/**/) {
  return x;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameJsOverloadedFunctionParameter, TestRenameJsOverloadedFunctionParameter);


static void TestRenameJsExports03(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
class /*1*/A {
    /*2*/constructor() { }
}
module.exports = A;
// @Filename: b.js
const /*3*/A = require("./a");
new /*4*/A;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameJsExports03, TestRenameJsExports03);


static void TestRenameJsExports02(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
module.exports = class /*1*/A {}
// @Filename: b.js
const /*2*/A = require("./a");)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameJsExports02, TestRenameJsExports02);


static void TestRenameJsExports01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
[|exports.[|{| "contextRangeIndex": 0 |}area|] = function (r) { return r * r; }|]
// @Filename: b.js
var mod = require('./a');
var t = mod./*1*/[|area|](10);)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1"});
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"area"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameJsExports01, TestRenameJsExports01);


static void TestRenameJsDocTypeLiteral(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @filename: /a.js
/**
 * @param {Object} options
 * @param {string} options.foo
 * @param {number} options.bar
 */
function foo(/**/options) {})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToFile(t, "/a.js");
		f->VerifyBaselineRename(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameJsDocTypeLiteral, TestRenameJsDocTypeLiteral);


static void TestRenameJsDocImportTag(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJS: true
// @checkJs: true
// @Filename: /b.ts
export interface A { }
// @Filename: /a.js
/**
 * @import { A } from "./b";
 */

/**
 * @param { [|A/**/|] } a
 */
function f(a) {})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameJsDocImportTag, TestRenameJsDocImportTag);


static void TestRenameJSDocNamepath(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noLib: true
/**
 * @type {module:foo/A} x
 */
var x = 1
var /*0*/A = 0;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {"0"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameJSDocNamepath, TestRenameJSDocNamepath);


static void TestRenameInfoForFunctionExpression01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var x = function /**/[|f|](g: any, h: any) {
    f(f, g);
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyRenameSucceeded(t, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestRenameInfoForFunctionExpression01, TestRenameInfoForFunctionExpression01);



static void TestRenameImportSpecifierPropertyName(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: canada.ts
export interface /**/Ginger {}
// @Filename: dry.ts
import { Ginger as Ale } from './canada';)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameImportSpecifierPropertyName, TestRenameImportSpecifierPropertyName);


static void TestRenameImportSpecifierNoResourceOperations(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @Filename: /a.ts
export const x = 0;
// @Filename: /b.ts
import * as a from ".//*rename*/a";)TS";
		auto capabilities = fourslash::GetDefaultCapabilities();
		capabilities->Workspace->WorkspaceEdit = std::make_shared<lsproto::WorkspaceEditClientCapabilities>(lsproto::WorkspaceEditClientCapabilities{.DocumentChanges = true, .ResourceOperations = std::make_shared<lsproto::Slice<lsproto::ResourceOperationKind>>(std::vector<lsproto::ResourceOperationKind>{})});
		auto __fsp1 = fourslash::NewFourslash(t, capabilities, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "rename");
		f->VerifyRenameFailed(t, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestRenameImportSpecifierNoResourceOperations, TestRenameImportSpecifierNoResourceOperations);


static void TestRenameImportRequire(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
[|import [|{| "contextRangeIndex": 0 |}e|] = require("mod4");|]
[|e|];
a = { [|e|] };
[|export { [|{| "contextRangeIndex": 4 |}e|] };|]
// @Filename: /b.ts
[|import { [|{| "contextRangeIndex": 6 |}e|] } from "./a";|]
[|export { [|{| "contextRangeIndex": 8 |}e|] };|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[2], f->Ranges()[3], f->Ranges()[5], f->Ranges()[7], f->Ranges()[9]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameImportRequire, TestRenameImportRequire);


static void TestRenameImportOfReExport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noLib: true
declare module "a" {
    [|export class /*1*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 0 |}C|] {}|]
}
declare module "b" {
    [|export { /*2*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 2 |}C|] } from "a";|]
}
declare module "c" {
    [|import { /*3*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 4 |}C|] } from "b";|]
    export function f(c: [|C|]): void;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1]});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[3]});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[5], f->Ranges()[6]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameImportOfReExport, TestRenameImportOfReExport);


static void TestRenameImportOfReExport2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare module "a" {
    [|export class /*1*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 0 |}C|] {}|]
}
declare module "b" {
    [|export { [|{| "contextRangeIndex": 2 |}C|] as /*2*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 2 |}D|] } from "a";|]
}
declare module "c" {
    [|import { /*3*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 5 |}D|] } from "b";|]
    export function f(c: [|D|]): void;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
		f->VerifyBaselineRename(t, nullptr, asMonVec(tsu::ToAny(f->GetRangesByText()->Get("C"))));
		f->VerifyBaselineRename(t, nullptr, {f->GetRangesByText()->Get("D")[0]});
		f->VerifyBaselineRename(t, nullptr, {f->GetRangesByText()->Get("D")[1], f->GetRangesByText()->Get("D")[2]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameImportOfReExport2, TestRenameImportOfReExport2);


static void TestRenameImportOfExportEquals(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|declare namespace /*N*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 0 |}N|] {
    [|export var /*x*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 2 |}x|]: number;|]
}|]
declare module "mod" {
    [|export = [|{| "contextRangeIndex": 4 |}N|];|]
}
declare module "a" {
    [|import * as /*a*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 6 |}N|] from "mod";|]
    [|export { [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 8 |}N|] };|] // Renaming N here would rename
}
declare module "b" {
    [|import { /*b*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 10 |}N|] } from "a";|]
    export const y: typeof [|N|].[|x|];
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"N", "a", "b", "x"});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[5]});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[7]});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[9]});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[11], f->Ranges()[12]});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[3], f->Ranges()[13]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameImportOfExportEquals, TestRenameImportOfExportEquals);


static void TestRenameImportOfExportEquals2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|declare namespace /*N*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 0 |}N|] {
    export var x: number;
}|]
declare module "mod" {
    [|export = [|{| "contextRangeIndex": 2 |}N|];|]
}
declare module "a" {
    [|import * as /*O*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 4 |}O|] from "mod";|]
    [|export { [|{| "contextRangeIndex": 6 |}O|] as /*P*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 6 |}P|] };|] // Renaming N here would rename
}
declare module "b" {
    [|import { [|{| "contextRangeIndex": 9 |}P|] as /*Q*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 9 |}Q|] } from "a";|]
    export const y: typeof [|Q|].x;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineFindAllReferences(t, {"N", "O", "P", "Q"});
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"N", "O", "P", "Q"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameImportOfExportEquals2, TestRenameImportOfExportEquals2);


static void TestRenameImportAndExportInDiffFiles(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a.ts
[|export var /*1*/[|{| "isDefinition": true, "contextRangeIndex": 0 |}a|];|]
// @Filename: b.ts
[|import { /*2*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 2 |}a|] } from './a';|]
[|export { /*3*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 4 |}a|] };|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[3], f->Ranges()[5]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameImportAndExportInDiffFiles, TestRenameImportAndExportInDiffFiles);


static void TestRenameFunctionParameter2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/**
 * @param {number} p
 */
const foo = function foo(p/**/) {
    return p;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameFunctionParameter2, TestRenameFunctionParameter2);


static void TestRenameFunctionParameter1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function Foo() {
    /**
     * @param {number} p
     */
    this.foo = function foo(p/**/) {
        return p;
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameFunctionParameter1, TestRenameFunctionParameter1);


static void TestRenameFromNodeModulesDep4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /index.ts
import hljs from "highlight.js/lib/core"
import { h } from "highlight.js/lib/core";
import { /*notOk*/h as hh } from "highlight.js/lib/core";
/*ok*/[|hljs|];
/*okWithAlias*/[|h|];
/*ok2*/[|hh|];
// @Filename: /node_modules/highlight.js/lib/core.d.ts
declare const hljs: { registerLanguage(s: string): void };
export default hljs;
export const h: string;
// @Filename: /tsconfig.json
{})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "ok");
		f->VerifyRenameSucceeded(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::True}));
		f->VerifyRenameSucceeded(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::False}));
		f->GoToMarker(t, "ok2");
		f->VerifyRenameSucceeded(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::True}));
		f->VerifyRenameSucceeded(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::False}));
		f->GoToMarker(t, "notOk");
		f->VerifyRenameFailed(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::True}));
		f->VerifyRenameFailed(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::False}));
		f->GoToMarker(t, "okWithAlias");
		f->VerifyRenameSucceeded(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::True}));
		f->VerifyRenameFailed(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::False}));
	});
}
REGISTER_FOURSLASH_TEST(TestRenameFromNodeModulesDep4, TestRenameFromNodeModulesDep4);


static void TestRenameFromNodeModulesDep3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /packages/first/index.d.ts
import { /*ok*/[|Foo|] } from "foo";
declare type FooBar = Foo[/*ok2*/"[|bar|]"];
// @Filename: /packages/foo/package.json
 { "types": "index.d.ts" }
// @Filename: /packages/foo/index.d.ts
export interface Foo {
    /*ok3*/[|bar|]: string;
}
// @link: /packages/foo -> /packages/first/node_modules/foo)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "ok");
		f->VerifyRenameSucceeded(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::True}));
		f->VerifyRenameSucceeded(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::False}));
		f->GoToMarker(t, "ok2");
		f->VerifyRenameSucceeded(t, nullptr);
		f->GoToMarker(t, "ok3");
		f->VerifyRenameSucceeded(t, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestRenameFromNodeModulesDep3, TestRenameFromNodeModulesDep3);


static void TestRenameFromNodeModulesDep2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /node_modules/first/index.d.ts
import { /*okWithAlias*/[|Foo|] } from "foo";
declare type FooBar = Foo[/*notOk*/"bar"];
// @Filename: /node_modules/first/node_modules/foo/package.json
 { "types": "index.d.ts" }
// @Filename: /node_modules/first/node_modules/foo/index.d.ts
export interface Foo {
    /*ok2*/[|bar|]: string;
}
// @Filename: /node_modules/first/node_modules/foo/bar.d.ts
import { Foo } from "./index";
declare type FooBar = Foo[/*ok3*/"[|bar|]"];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "okWithAlias");
		f->VerifyRenameSucceeded(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::True}));
		f->VerifyRenameFailed(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::False}));
		f->GoToMarker(t, "notOk");
		f->VerifyRenameFailed(t, nullptr);
		f->GoToMarker(t, "ok2");
		f->VerifyRenameSucceeded(t, nullptr);
		f->GoToMarker(t, "ok3");
		f->VerifyRenameSucceeded(t, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestRenameFromNodeModulesDep2, TestRenameFromNodeModulesDep2);


static void TestRenameFromNodeModulesDep1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /index.ts
import { /*okWithAlias*/[|Foo|] } from "foo";
declare const f: Foo;
f./*notOk*/bar;
// @Filename: /tsconfig.json
 { }
// @Filename: /node_modules/foo/package.json
 { "types": "index.d.ts" }
// @Filename: /node_modules/foo/index.d.ts
export interface Foo {
    bar: string;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "okWithAlias");
		f->VerifyRenameSucceeded(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::True}));
		f->VerifyRenameFailed(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::False}));
		f->GoToMarker(t, "notOk");
		f->VerifyRenameFailed(t, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestRenameFromNodeModulesDep1, TestRenameFromNodeModulesDep1);


static void TestRenameForStringLiteral(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: /a.ts
interface Foo {
    property: /**/"foo";
}
/**
 * @type {{ property: "foo"}}
 */
const obj: Foo = {
    property: "foo",
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameForStringLiteral, TestRenameForStringLiteral);


static void TestRenameForAliasingExport02(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: foo.ts
let x = 1;

export { x as /**/[|y|] };)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyRenameSucceeded(t, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestRenameForAliasingExport02, TestRenameForAliasingExport02);


static void TestRenameForAliasingExport01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: foo.ts
let x = 1;

export { /**/[|x|] as y };)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyRenameSucceeded(t, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestRenameForAliasingExport01, TestRenameForAliasingExport01);


static void TestRenameFilePackageJson(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /src/example.ts
import brushPackageJson from './visx-brush//*rename*/package.json';
// @Filename: /src/visx-brush/package.json
{ "name": "brush" })TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyRename(t, "rename", "package2.json", std::unordered_map<std::string, std::string>{{"/src/example.ts", "import brushPackageJson from './visx-brush/package2.json';"}, {"/src/visx-brush/package2.json", R"TS({ "name": "brush" })TS"}});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameFilePackageJson, TestRenameFilePackageJson);


static void TestRenameExportSpecifier(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a.ts
const name = {};
export { name as name/**/ };
// @Filename: b.ts
import { name } from './a';
const x = name.toString();)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::False}), {""});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameExportSpecifier, TestRenameExportSpecifier);


static void TestRenameExportSpecifier2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a.ts
const name = {};
export { name/**/ };
// @Filename: b.ts
import { name } from './a';
const x = name.toString();)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ProvidePrefixAndSuffixTextForRename = Tristate::False}), {""});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameExportSpecifier2, TestRenameExportSpecifier2);


static void TestRenameExportCrash(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: Foo.js
let a;
module.exports = /**/a;
exports["foo"] = a;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameExportCrash, TestRenameExportCrash);


static void TestRenameDestructuringNestedBindingElement(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface MultiRobot {
    name: string;
    skills: {
        [|[|{| "contextRangeIndex": 0|}primary|]: string;|]
        secondary: string;
    };
}
let multiRobots: MultiRobot[];
for ([|let { skills: {[|{| "contextRangeIndex": 2|}primary|]: primaryA, secondary: secondaryA } } of multiRobots|]) {
    console.log(primaryA);
}
for ([|let { skills: {[|{| "contextRangeIndex": 4|}primary|], secondary } } of multiRobots|]) {
    console.log([|primary|]);
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[3], f->Ranges()[5], f->Ranges()[6]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameDestructuringNestedBindingElement, TestRenameDestructuringNestedBindingElement);


static void TestRenameDestructuringFunctionParameter(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f([|{[|{| "contextRangeIndex": 0 |}a|]}: {[|a|]}|]) {
    f({[|a|]});
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[3], f->Ranges()[2]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameDestructuringFunctionParameter, TestRenameDestructuringFunctionParameter);


static void TestRenameDestructuringDeclarationInFor(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    [|[|{| "contextRangeIndex": 0 |}property1|]: number;|]
    property2: string;
}
var elems: I[];

var p2: number, property1: number;
for ([|let { [|{| "contextRangeIndex": 2 |}property1|]: p2 } = elems[0]|]; p2 < 100; p2++) {
}
for ([|let { [|{| "contextRangeIndex": 4 |}property1|] } = elems[0]|]; p2 < 100; p2++) {
    [|property1|] = p2;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[3], f->Ranges()[5], f->Ranges()[6]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameDestructuringDeclarationInFor, TestRenameDestructuringDeclarationInFor);


static void TestRenameDestructuringDeclarationInForOf(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    [|[|{| "contextRangeIndex": 0 |}property1|]: number;|]
    property2: string;
}
var elems: I[];

for ([|let { [|{| "contextRangeIndex": 2 |}property1|] } of elems|]) {
    [|property1|]++;
}
for ([|let { [|{| "contextRangeIndex": 5 |}property1|]: p2 } of elems|]) {
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[6], f->Ranges()[3], f->Ranges()[4]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameDestructuringDeclarationInForOf, TestRenameDestructuringDeclarationInForOf);


static void TestRenameDestructuringClassProperty(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class A {
    [|[|{| "contextRangeIndex": 0 |}foo|]: string;|]
}
class B {
    syntax1(a: A): void {
        [|let { [|{| "contextRangeIndex": 2 |}foo|] } = a;|]
    }
    syntax2(a: A): void {
        [|let { [|{| "contextRangeIndex": 4 |}foo|]: foo } = a;|]
    }
    syntax11(a: A): void {
        [|let { [|{| "contextRangeIndex": 6 |}foo|] } = a;|]
        [|foo|] = "newString";
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[5], f->Ranges()[3], f->Ranges()[7], f->Ranges()[8]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameDestructuringClassProperty, TestRenameDestructuringClassProperty);


static void TestRenameDestructuringAssignment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    [|[|{| "contextRangeIndex": 0 |}x|]: number;|]
}
var a: I;
var x;
([|{ [|{| "contextRangeIndex": 2 |}x|]: x } = a|]);)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"x"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameDestructuringAssignment, TestRenameDestructuringAssignment);


static void TestRenameDestructuringAssignmentNestedInFor(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface MultiRobot {
    name: string;
    skills: {
        [|[|{| "contextRangeIndex": 0 |}primary|]: string;|]
        secondary: string;
    };
}
declare let multiRobot: MultiRobot, [|[|{| "contextRangeIndex": 2 |}primary|]: string|], secondary: string, primaryA: string, secondaryA: string, i: number;
for ([|{ skills: { [|{| "contextRangeIndex": 4 |}primary|]: primaryA, secondary: secondaryA } } = multiRobot|], i = 0; i < 1; i++) {
    primaryA;
}
for ([|{ skills: { [|{| "contextRangeIndex": 6 |}primary|], secondary } } = multiRobot|], i = 0; i < 1; i++) {
    [|primary|];
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[5], f->Ranges()[3], f->Ranges()[7], f->Ranges()[8]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameDestructuringAssignmentNestedInFor, TestRenameDestructuringAssignmentNestedInFor);


static void TestRenameDestructuringAssignmentNestedInForOf(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: false
interface MultiRobot {
    name: string;
    skills: {
        [|[|{| "contextRangeIndex": 0 |}primary|]: string;|]
        secondary: string;
    };
}
let multiRobots: MultiRobot[];
let [|[|{| "contextRangeIndex": 2 |}primary|]: string|], secondary: string, primaryA: string, secondaryA: string;
for ([|{ skills: { [|{| "contextRangeIndex": 4 |}primary|]: primaryA, secondary: secondaryA } } of multiRobots|]) {
    primaryA;
}
for ([|{ skills: { [|{| "contextRangeIndex": 6 |}primary|], secondary } } of multiRobots|]) {
    [|primary|];
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[5], f->Ranges()[3], f->Ranges()[7], f->Ranges()[8]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameDestructuringAssignmentNestedInForOf, TestRenameDestructuringAssignmentNestedInForOf);


static void TestRenameDestructuringAssignmentNestedInForOf2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface MultiRobot {
    name: string;
    skills: {
        [|[|{| "contextRangeIndex": 0 |}primary|]: string;|]
        secondary: string;
    };
}
let multiRobots: MultiRobot[], [|[|{| "contextRangeIndex": 2 |}primary|]: string|];
for ([|{ skills: { [|{| "contextRangeIndex": 4 |}primary|]: primaryA, secondary: secondaryA } } of multiRobots|]) {
    console.log(primaryA);
}
for ([|{ skills: { [|{| "contextRangeIndex": 6 |}primary|], secondary } } of multiRobots|]) {
    console.log([|primary|]);
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[5], f->Ranges()[3], f->Ranges()[7], f->Ranges()[8]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameDestructuringAssignmentNestedInForOf2, TestRenameDestructuringAssignmentNestedInForOf2);


static void TestRenameDestructuringAssignmentNestedInFor2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: false
interface MultiRobot {
    name: string;
    skills: {
        [|[|{| "contextRangeIndex": 0 |}primary|]: string;|]
        secondary: string;
    };
}
let multiRobot: MultiRobot, [|[|{| "contextRangeIndex": 2 |}primary|]: string|], secondary: string, primaryA: string, secondaryA: string, i: number;
for ([|{ skills: { [|{| "contextRangeIndex": 4 |}primary|]: primaryA, secondary: secondaryA } } = multiRobot|], i = 0; i < 1; i++) {
    primaryA;
}
for ([|{ skills: { [|{| "contextRangeIndex": 6 |}primary|], secondary } } = multiRobot|], i = 0; i < 1; i++) {
    [|primary|];
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[5], f->Ranges()[3], f->Ranges()[7], f->Ranges()[8]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameDestructuringAssignmentNestedInFor2, TestRenameDestructuringAssignmentNestedInFor2);


static void TestRenameDestructuringAssignmentNestedInArrayLiteral(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    [|[|{| "contextRangeIndex": 0 |}property1|]: number;|]
    property2: string;
}
var elems: I[], p1: number, [|[|{| "contextRangeIndex": 2 |}property1|]: number|];
[|[{ [|{| "contextRangeIndex": 4 |}property1|]: p1 }] = elems;|]
[|[{ [|{| "contextRangeIndex": 6 |}property1|] }] = elems;|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[5], f->Ranges()[3], f->Ranges()[7]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameDestructuringAssignmentNestedInArrayLiteral, TestRenameDestructuringAssignmentNestedInArrayLiteral);


static void TestRenameDestructuringAssignmentInFor(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: false
interface I {
    [|[|{| "contextRangeIndex": 0 |}property1|]: number;|]
    property2: string;
}
var elems: I[];

var p2: number, [|[|{| "contextRangeIndex": 2 |}property1|]: number|];
for ([|{ [|{| "contextRangeIndex": 4 |}property1|] } = elems[0]|]; p2 < 100; p2++) {
   p2 = [|property1|]++;
}
for ([|{ [|{| "contextRangeIndex": 7 |}property1|]: p2 } = elems[0]|]; p2 < 100; p2++) {
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[8], f->Ranges()[3], f->Ranges()[5], f->Ranges()[6]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameDestructuringAssignmentInFor, TestRenameDestructuringAssignmentInFor);


static void TestRenameDestructuringAssignmentInForOf(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: false
interface I {
    [|[|{| "contextRangeIndex": 0 |}property1|]: number;|]
    property2: string;
}
var elems: I[];

var [|[|{| "contextRangeIndex": 2 |}property1|]: number|], p2: number;
for ([|{ [|{| "contextRangeIndex": 4 |}property1|] } of elems|]) {
    [|property1|]++;
}
for ([|{ [|{| "contextRangeIndex": 7 |}property1|]: p2 } of elems|]) {
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[8], f->Ranges()[3], f->Ranges()[5], f->Ranges()[6]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameDestructuringAssignmentInForOf, TestRenameDestructuringAssignmentInForOf);


static void TestRenameDefaultLibDontWork(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: file1.ts
[|var [|{| "contextRangeIndex": 0 |}test|] = "foo";|]
console.log([|test|]);)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameDefaultLibDontWork, TestRenameDefaultLibDontWork);


static void TestRenameDefaultKeyword(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @noLib: true
function f(value: string, /*1*/default: string) {}

const /*2*/default = 1;

function /*3*/default() {}

class /*4*/default {}

const foo = {
    /*5*/[|default|]: 1
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		auto markers = std::vector<std::string>{"1", "2", "3", "4"};
		for (auto marker : markers) {
			f->GoToMarker(t, marker);
			f->VerifyRenameFailed(t, nullptr);
		}
		f->GoToMarker(t, "5");
		f->VerifyRenameSucceeded(t, nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestRenameDefaultKeyword, TestRenameDefaultKeyword);


static void TestRenameDefaultImport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: B.ts
[|export default class /*1*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 0 |}B|] {
    test() {
    }
}|]
// @Filename: A.ts
[|import /*2*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 2 |}B|] from "./B";|]
let b = new [|B|]();
b.test();)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[3], f->Ranges()[4]});
		f->VerifyBaselineDocumentHighlights(t, nullptr, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameDefaultImport, TestRenameDefaultImport);


static void TestRenameDefaultImportDifferentName(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: B.ts
[|export default class /*1*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 0 |}C|] {
    test() {
    }
}|]
// @Filename: A.ts
[|import /*2*/[|{| "isWriteAccess": true, "isDefinition": true, "contextRangeIndex": 2 |}B|] from "./B";|]
let b = new [|B|]();
b.test();)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1]});
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[3], f->Ranges()[4]});
		f->VerifyBaselineDocumentHighlights(t, nullptr, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameDefaultImportDifferentName, TestRenameDefaultImportDifferentName);


static void TestRenameDeclarationKeywords(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|{| "id": "baseDecl" |}class [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "baseDecl" |}Base|] {}|]
[|{| "id": "implemented1Decl" |}interface [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "implemented1Decl" |}Implemented1|] {}|]
[|{| "id": "classDecl1" |}[|class|] [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "classDecl1" |}C1|] [|extends|] [|Base|] [|implements|] [|Implemented1|] {
    [|{| "id": "getDecl" |}[|get|] [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "getDecl" |}e|]() { return 1; }|]
    [|{| "id": "setDecl" |}[|set|] [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "setDecl" |}e|](v) {}|]
}|]
[|{| "id": "interfaceDecl1" |}[|interface|] [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "interfaceDecl1" |}I1|] [|extends|] [|Base|] { }|]
[|{| "id": "typeDecl" |}[|type|] [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "typeDecl" |}T|] = { }|]
[|{| "id": "enumDecl" |}[|enum|] [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "enumDecl" |}E|] { }|]
[|{| "id": "namespaceDecl" |}[|namespace|] [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "namespaceDecl" |}N|] { }|]
[|{| "id": "moduleDecl" |}[|module|] [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "moduleDecl" |}M|] { }|]
[|{| "id": "functionDecl" |}[|function|] [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "functionDecl" |}fn|]() {}|]
[|{| "id": "varDecl" |}[|var|] [|{| "isWriteAccess": false, "isDefinition": true, "contextRangeId": "varDecl" |}x|];|]
[|{| "id": "letDecl" |}[|let|] [|{| "isWriteAccess": false, "isDefinition": true, "contextRangeId": "letDecl" |}y|];|]
[|{| "id": "constDecl" |}[|const|] [|{| "isWriteAccess": true, "isDefinition": true, "contextRangeId": "constDecl" |}z|] = 1;|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[5], f->Ranges()[7], f->Ranges()[9], f->Ranges()[12], f->Ranges()[15], f->Ranges()[18], f->Ranges()[20], f->Ranges()[23], f->Ranges()[26], f->Ranges()[29], f->Ranges()[32], f->Ranges()[35], f->Ranges()[38], f->Ranges()[41], f->Ranges()[44]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameDeclarationKeywords, TestRenameDeclarationKeywords);


static void TestRenameCrossJsTs01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
[|exports.[|{| "contextRangeIndex": 0 |}area|] = function (r) { return r * r; }|]
// @Filename: b.ts
[|import { [|{| "contextRangeIndex": 2 |}area|] } from './a';|]
var t = [|area|](10);)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[3], f->Ranges()[4]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameCrossJsTs01, TestRenameCrossJsTs01);


static void TestRenameContextuallyTypedProperties(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    [|[|{| "contextRangeIndex": 0 |}prop1|]: () => void;|]
    prop2(): void;
}

var o1: I = {
    [|[|{| "contextRangeIndex": 2 |}prop1|]() { }|],
    prop2() { }
};

var o2: I = {
    [|[|{| "contextRangeIndex": 4 |}prop1|]: () => { }|],
    prop2: () => { }
};

var o3: I = {
    [|get [|{| "contextRangeIndex": 6 |}prop1|]() { return () => { }; }|],
    get prop2() { return () => { }; }
};

var o4: I = {
    [|set [|{| "contextRangeIndex": 8 |}prop1|](v) { }|],
    set prop2(v) { }
};

var o5: I = {
    [|"[|{| "contextRangeIndex": 10 |}prop1|]"() { }|],
    "prop2"() { }
};

var o6: I = {
    [|"[|{| "contextRangeIndex": 12 |}prop1|]": function () { }|],
    "prop2": function () { }
};

var o7: I = {
    [|["[|{| "contextRangeIndex": 14 |}prop1|]"]: function () { }|],
    ["prop2"]: function () { }
};

var o8: I = {
    [|["[|{| "contextRangeIndex": 16 |}prop1|]"]() { }|],
    ["prop2"]() { }
};

var o9: I = {
    [|get ["[|{| "contextRangeIndex": 18 |}prop1|]"]() { return () => { }; }|],
    get ["prop2"]() { return () => { }; }
};

var o10: I = {
    [|set ["[|{| "contextRangeIndex": 20 |}prop1|]"](v) { }|],
    set ["prop2"](v) { }
};)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"prop1"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameContextuallyTypedProperties, TestRenameContextuallyTypedProperties);


static void TestRenameContextuallyTypedProperties2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    prop1: () => void;
    [|[|{| "contextRangeIndex": 0 |}prop2|](): void;|]
}

var o1: I = {
    prop1() { },
    [|[|{| "contextRangeIndex": 2 |}prop2|]() { }|]
};

var o2: I = {
    prop1: () => { },
    [|[|{| "contextRangeIndex": 4 |}prop2|]: () => { }|]
};

var o3: I = {
    get prop1() { return () => { }; },
    [|get [|{| "contextRangeIndex": 6 |}prop2|]() { return () => { }; }|]
};

var o4: I = {
    set prop1(v) { },
    [|set [|{| "contextRangeIndex": 8 |}prop2|](v) { }|]
};

var o5: I = {
    "prop1"() { },
    [|"[|{| "contextRangeIndex": 10 |}prop2|]"() { }|]
};

var o6: I = {
    "prop1": function () { },
    [|"[|{| "contextRangeIndex": 12 |}prop2|]": function () { }|]
};

var o7: I = {
    ["prop1"]: function () { },
    [|["[|{| "contextRangeIndex": 14 |}prop2|]"]: function () { }|]
};

var o8: I = {
    ["prop1"]() { },
    [|["[|{| "contextRangeIndex": 16 |}prop2|]"]() { }|]
};

var o9: I = {
    get ["prop1"]() { return () => { }; },
    [|get ["[|{| "contextRangeIndex": 18 |}prop2|]"]() { return () => { }; }|]
};

var o10: I = {
    set ["prop1"](v) { },
    [|set ["[|{| "contextRangeIndex": 20 |}prop2|]"](v) { }|]
};)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"prop2"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameContextuallyTypedProperties2, TestRenameContextuallyTypedProperties2);


static void TestRenameCommentsAndStrings4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(///<reference path="./Bar.ts" />
[|function [|{| "contextRangeIndex": 0 |}Bar|]() {
    // This is a reference to [|Bar|] in a comment.
    "this is a reference to [|Bar|] in a string";
    `Foo [|Bar|] Baz.`;
    {
        const Bar = 0;
        `[|Bar|] ba ${Bar} bara [|Bar|] berbobo ${Bar} araura [|Bar|] ara!`;
    }
}|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameCommentsAndStrings4, TestRenameCommentsAndStrings4);


static void TestRenameCommentsAndStrings3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(///<reference path="./Bar.ts" />
[|function [|{| "contextRangeIndex": 0 |}Bar|]() {
    // This is a reference to [|Bar|] in a comment.
    "this is a reference to Bar in a string"
}|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameCommentsAndStrings3, TestRenameCommentsAndStrings3);


static void TestRenameCommentsAndStrings2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(///<reference path="./Bar.ts" />
[|function [|{| "contextRangeIndex": 0 |}Bar|]() {
    // This is a reference to Bar in a comment.
    "this is a reference to [|Bar|] in a string"
}|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameCommentsAndStrings2, TestRenameCommentsAndStrings2);


static void TestRenameCommentsAndStrings1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(///<reference path="./Bar.ts" />
[|function [|{| "contextRangeIndex": 0 |}Bar|]() {
    // This is a reference to Bar in a comment.
    "this is a reference to Bar in a string"
}|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"Bar"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameCommentsAndStrings1, TestRenameCommentsAndStrings1);


static void TestRenameBuiltinTypes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
const arr: /*1*/Array<number> = [];
const map1: /*2*/Map<string, number> = new Map();
const prom: /*3*/Promise<void> = Promise.resolve();
const str: /*4*/string = "hello";
)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		for (auto marker : std::vector<std::string>{"1", "2", "3", "4"}) {
			f->GoToMarker(t, marker);
			f->VerifyRenameFailed(t, nullptr);
		}
	});
}
REGISTER_FOURSLASH_TEST(TestRenameBuiltinTypes, TestRenameBuiltinTypes);


static void TestRenameBindingElementInitializerProperty(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f([|{[|{| "contextRangeIndex": 0 |}required|], optional = [|required|]}: {[|[|{| "contextRangeIndex": 3 |}required|]: number,|] optional?: number}|]) {
    console.log("required", [|required|]);
    console.log("optional", optional);
}

f({[|[|{| "contextRangeIndex": 6 |}required|]: 10|]});)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[2], f->Ranges()[5], f->Ranges()[4], f->Ranges()[7]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameBindingElementInitializerProperty, TestRenameBindingElementInitializerProperty);


static void TestRenameBindingElementInitializerExternal(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
[|const [|{| "contextRangeIndex": 0 |}external|] = true;|]

function f({
    lvl1 = [|external|],
    nested: { lvl2 = [|external|]},
    oldName: newName = [|external|]
}) {}

const {
    lvl1 = [|external|],
    nested: { lvl2 = [|external|]},
    oldName: newName = [|external|]
} = obj;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"external"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameBindingElementInitializerExternal, TestRenameBindingElementInitializerExternal);


static void TestRenameAliasExternalModule(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a.ts
namespace SomeModule { export class SomeClass { } }
export = SomeModule;
// @Filename: b.ts
[|import [|{| "contextRangeIndex": 0 |}M|] = require("./a");|]
import C = [|M|].SomeClass;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"M"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameAliasExternalModule, TestRenameAliasExternalModule);


static void TestRenameAliasExternalModule3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a.ts
namespace SomeModule { [|export class [|{| "contextRangeIndex": 0 |}SomeClass|] { }|] }
export = SomeModule;
// @Filename: b.ts
import M = require("./a");
import C = M.[|SomeClass|];)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"SomeClass"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameAliasExternalModule3, TestRenameAliasExternalModule3);


static void TestRenameAliasExternalModule2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: a.ts
[|module [|{| "contextRangeIndex": 0 |}SomeModule|] { export class SomeClass { } }|]
[|export = [|{| "contextRangeIndex": 2 |}SomeModule|];|]
// @Filename: b.ts
[|import [|{| "contextRangeIndex": 4 |}M|] = require("./a");|]
import C = [|M|].SomeClass;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1], f->Ranges()[3], f->Ranges()[5], f->Ranges()[6]});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameAliasExternalModule2, TestRenameAliasExternalModule2);


static void TestRenameAcrossMultipleProjects(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(//@Filename: a.ts
[|var [|{| "contextRangeIndex": 0 |}x|]: number;|]
//@Filename: b.ts
/// <reference path="a.ts" />
[|x|]++;
//@Filename: c.ts
/// <reference path="a.ts" />
[|x|]++;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineRenameAtRangesWithText(t, nullptr, {"x"});
	});
}
REGISTER_FOURSLASH_TEST(TestRenameAcrossMultipleProjects, TestRenameAcrossMultipleProjects);


static void TestRename01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
///<reference path="./Bar.ts" />
[|function [|{| "contextRangeIndex": 0 |}Bar|]() {
    // This is a reference to [|Bar|] in a comment.
    "this is a reference to [|Bar|] in a string"
}|])TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineRename(t, nullptr, {f->Ranges()[1]});
	});
}
REGISTER_FOURSLASH_TEST(TestRename01, TestRename01);
} // namespace
