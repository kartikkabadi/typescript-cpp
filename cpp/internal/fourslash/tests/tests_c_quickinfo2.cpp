// PORTED FROM Go fourslash tests (batch C, fsgen)
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


static std::pair<std::shared_ptr<fourslash::FourslashTest>, std::function<void()>> newContentMapperFourslash(gostd::testing::T* t, std::string content, std::string mapper, const std::vector<std::string>& extensions) {
	t->Helper();
	auto quotedExtensions = std::vector<std::string>(int(extensions.size()));
	{
		int i = 0;
		for (auto&& extension : extensions) {
			quotedExtensions[i] = gostd::sprintf("%q", {extension});
			i++;
		}
	}
	content = (((((std::string(R"TS(// @Filename: /tsconfig.json
{
	"compilerOptions": {
		"target": "es2020",
		"module": "esnext",
		"moduleResolution": "bundler",
		"strict": true
	},
	"contentMappers": [
		{ "package": "mapper", "extensions": [)TS") + gostr::join(quotedExtensions, ", ")) + std::string(R"TS(] }
	]
}

// @Filename: /node_modules/mapper/package.json
)TS")) + testutil::contentmappertest::PackageJSON(mapper)) + std::string(R"TS(

)TS")) + content);
	return fourslash::NewFourslashWithOptions(t, content, tsu::ptr(fourslash::FourslashOptions{.ContentMapperSpawner = std::shared_ptr<contentmapper::Spawner>(testutil::contentmappertest::NewSpawner()), .RunExternalCode = true}));
}

// getJavaScriptQuickInfo1_test.go

// getJavaScriptQuickInfo1_test.go
static void TestGetJavaScriptQuickInfo1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: Foo.js
/** @type {function(new:string,number)} */
var /**/v;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "var v: new (arg1: number) => string", "");
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptQuickInfo1, TestGetJavaScriptQuickInfo1);

// getJavaScriptQuickInfo2_test.go

// getJavaScriptQuickInfo2_test.go
static void TestGetJavaScriptQuickInfo2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: Foo.js
/** @param {number} [a] */
function /**/f(a) { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "function f(a?: number): void", "");
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptQuickInfo2, TestGetJavaScriptQuickInfo2);

// getJavaScriptQuickInfo3_test.go

// getJavaScriptQuickInfo3_test.go
static void TestGetJavaScriptQuickInfo3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: Foo.js
/** @param {number[]} [a] */
function /**/f(a) { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "function f(a?: number[]): void", "");
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptQuickInfo3, TestGetJavaScriptQuickInfo3);

// getJavaScriptQuickInfo4_test.go

// getJavaScriptQuickInfo4_test.go
static void TestGetJavaScriptQuickInfo4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: Foo.js
/** @param {[number,string]} [a] */
function /**/f(a) { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "function f(a?: [number, string]): void", "");
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptQuickInfo4, TestGetJavaScriptQuickInfo4);

// getJavaScriptQuickInfo5_test.go

// getJavaScriptQuickInfo5_test.go
static void TestGetJavaScriptQuickInfo5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: Foo.js
/** @param {{b:number}} [a] */
function /**/f(a) { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", R"TS(function f(a?: {
    b: number;
}): void)TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptQuickInfo5, TestGetJavaScriptQuickInfo5);

// getJavaScriptQuickInfo6_test.go

// getJavaScriptQuickInfo6_test.go
static void TestGetJavaScriptQuickInfo6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: Foo.js
/** @type {function(this:number)} */
function f() { /**/this })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptQuickInfo6, TestGetJavaScriptQuickInfo6);

// getJavaScriptQuickInfo7_test.go

// getJavaScriptQuickInfo7_test.go
static void TestGetJavaScriptQuickInfo7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: file.js
/**
 * This is a very cool function that is very nice.
 * @returns something
 * @param p anotherthing
 */
function a1(p) {
	try {
		throw new Error('x');
	} catch (x) { x--; }
	return 23;
}

x - /**/a1())TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "function a1(p: any): number", "This is a very cool function that is very nice.");
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptQuickInfo7, TestGetJavaScriptQuickInfo7);

// getJavaScriptQuickInfo8_test.go

// getJavaScriptQuickInfo8_test.go
static void TestGetJavaScriptQuickInfo8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowNonTsExtensions: true
// @Filename: file.js
let x = {
	/** @type {number} */
	get m() {
		return undefined;
	}
}
x.m/*1*/;

class Foo {
	/** @type {string} */
	get b() {
		return undefined;
	}
}
var y = new Foo();
y.b/*2*/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->Insert(t, ".");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "toFixed", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)})}})}));
		f->Backspace(t, 1);
		f->GoToMarker(t, "2");
		f->Insert(t, ".");
		f->VerifyCompletions(t, fourslash::MarkerInput{}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "substring", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindMethod)})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestGetJavaScriptQuickInfo8, TestGetJavaScriptQuickInfo8);

// quickInfoAliasVS_test.go

// quickInfoAliasVS_test.go
static void TestQuickInfoAliasVS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
/**
 * Doc
 * @tag Tag text
 */
export const x = 0;
// @Filename: /b.ts
import { x } from "./a";
x/*b*/;
// @Filename: /c.ts
/**
 * Doc 2
 * @tag Tag text 2
 */
import {
    /**
     * Doc 3
     * @tag Tag text 3
     */
    x
} from "./a";
x/*c*/;)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineVSHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoAliasVS, TestQuickInfoAliasVS);

// quickInfoAlias_test.go

// quickInfoAlias_test.go
static void TestQuickInfoAlias(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
/**
 * Doc
 * @tag Tag text
 */
export const x = 0;
// @Filename: /b.ts
import { x } from "./a";
x/*b*/;
// @Filename: /c.ts
/**
 * Doc 2
 * @tag Tag text 2
 */
import {
    /**
     * Doc 3
     * @tag Tag text 3
     */
    x
} from "./a";
x/*c*/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoAlias, TestQuickInfoAlias);

// quickInfoAmbientModule_test.go

// quickInfoAmbientModule_test.go
static void TestQuickInfoAmbientModule(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare module "*.css"/*1*/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", R"TS(module "*.css")TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoAmbientModule, TestQuickInfoAmbientModule);

// quickInfoAmbientModule_test.go
static void TestQuickInfoPatternAmbientModuleWithImportAttributes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare module "*.css"/*1*/ with { type: "css" } {
    const styles: { readonly [className: string]: string };
    export default styles;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoPatternAmbientModuleWithImportAttributes, TestQuickInfoPatternAmbientModuleWithImportAttributes);

// quickInfoAmbientModule_test.go
static void TestQuickInfoMergedPatternAmbientModuleWithImportAttributes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /first.d.ts
declare module "*.asset"/*css*/ with { type: "css" } {
    export const cssOnly: "css";
}
declare module "*.asset"/*text*/ with { type: "text" } {
    export const textOnly: "text";
}

// @Filename: /second.d.ts
declare module "*.asset" with { type: "css" } {
    export const cssAlso: "css-also";
}
declare module "*.asset" with { type: "text" } {
    export const textAlso: "text-also";
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"css", std::vector<int>{0, 1}}, {"text", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoMergedPatternAmbientModuleWithImportAttributes, TestQuickInfoMergedPatternAmbientModuleWithImportAttributes);

// quickInfoAssertionNodeNotReusedWhenTypeNotEquivalent1_test.go

// quickInfoAssertionNodeNotReusedWhenTypeNotEquivalent1_test.go
static void TestQuickInfoAssertionNodeNotReusedWhenTypeNotEquivalent1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
type Wrapper<T> = {
  _type: T;
};

function stringWrapper(): Wrapper<string> {
  return { _type: "" };
}

function objWrapper<T extends Record<string, Wrapper<any>>>(
  obj: T,
): Wrapper<T> {
  return { _type: obj };
}

const value = objWrapper({
  prop1: stringWrapper() as Wrapper<"hello">,
});

type Unwrap<T extends Wrapper<any>> = T["_type"] extends Record<
  string,
  Wrapper<any>
>
  ? { [Key in keyof T["_type"]]: Unwrap<T["_type"][Key]> }
  : T["_type"];

type Test/*1*/ = Unwrap<typeof value>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", R"TS(type Test = {
    prop1: "hello";
})TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoAssertionNodeNotReusedWhenTypeNotEquivalent1, TestQuickInfoAssertionNodeNotReusedWhenTypeNotEquivalent1);

// quickInfoAssignToExistingClass_test.go

// quickInfoAssignToExistingClass_test.go
static void TestQuickInfoAssignToExistingClass(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace Test {
    class Mocked {
        myProp: string;
    }
    class Tester {
        willThrowError() {
            Mocked = Mocked || function () { // => Error: Invalid left-hand side of assignment expression.
                return { /**/myProp: "test" };
            };
        }
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyQuickInfoExists(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoAssignToExistingClass, TestQuickInfoAssignToExistingClass);

// quickInfoAtPropWithAmbientDeclarationInJs_test.go

// quickInfoAtPropWithAmbientDeclarationInJs_test.go
static void TestQuickInfoAtPropWithAmbientDeclarationInJs(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @filename: /a.js
class C {
    constructor() {
        this.prop = "";
    }
    declare prop: string;
    method() {
        this.prop.foo/**/
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoAtPropWithAmbientDeclarationInJs, TestQuickInfoAtPropWithAmbientDeclarationInJs);

// quickInfoBindingPatternInJsdocNoCrash1_test.go

// quickInfoBindingPatternInJsdocNoCrash1_test.go
static void TestQuickInfoBindingPatternInJsdocNoCrash1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(/** @type {({ /*1*/data: any }?) => { data: string[] }} */
function useQuery({ data }): { data: string[] } {
  return {
    data,
  };
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoBindingPatternInJsdocNoCrash1, TestQuickInfoBindingPatternInJsdocNoCrash1);

// quickInfoCallProperty_test.go

// quickInfoCallProperty_test.go
static void TestQuickInfoCallProperty(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    /** Doc */
    m: () => void;
}
function f(x: I): void {
    x./**/m();
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "(property) I.m: () => void", "Doc");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoCallProperty, TestQuickInfoCallProperty);

// quickInfoCanBeTruncated_test.go

// quickInfoCanBeTruncated_test.go
static void TestQuickInfoCanBeTruncated(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @stableTypeOrdering: true
// @noLib: true
interface Foo {
  _0: 0;
  _1: 1;
  _2: 2;
  _3: 3;
  _4: 4;
  _5: 5;
  _6: 6;
  _7: 7;
  _8: 8;
  _9: 9;
  _10: 10;
  _11: 11;
  _12: 12;
  _13: 13;
  _14: 14;
  _15: 15;
  _16: 16;
  _17: 17;
  _18: 18;
  _19: 19;
  _20: 20;
  _21: 21;
  _22: 22;
  _23: 23;
  _24: 24;
  _25: 25;
  _26: 26;
  _27: 27;
  _28: 28;
  _29: 29;
  _30: 30;
  _31: 31;
  _32: 32;
  _33: 33;
  _34: 34;
  _35: 35;
  _36: 36;
  _37: 37;
  _38: 38;
  _39: 39;
  _40: 40;
  _41: 41;
  _42: 42;
  _43: 43;
  _44: 44;
  _45: 45;
  _46: 46;
  _47: 47;
  _48: 48;
  _49: 49;
  _50: 50;
  _51: 51;
  _52: 52;
  _53: 53;
  _54: 54;
  _55: 55;
  _56: 56;
  _57: 57;
  _58: 58;
  _59: 59;
  _60: 60;
  _61: 61;
  _62: 62;
  _63: 63;
  _64: 64;
  _65: 65;
  _66: 66;
  _67: 67;
  _68: 68;
  _69: 69;
  _70: 70;
  _71: 71;
  _72: 72;
  _73: 73;
  _74: 74;
  _75: 75;
  _76: 76;
  _77: 77;
  _78: 78;
  _79: 79;
  _80: 80;
  _81: 81;
  _82: 82;
  _83: 83;
  _84: 84;
  _85: 85;
  _86: 86;
  _87: 87;
  _88: 88;
  _89: 89;
  _90: 90;
  _91: 91;
  _92: 92;
  _93: 93;
  _94: 94;
  _95: 95;
  _96: 96;
  _97: 97;
  _98: 98;
  _99: 99;
  _100: 100;
  _101: 101;
  _102: 102;
  _103: 103;
  _104: 104;
  _105: 105;
  _106: 106;
  _107: 107;
  _108: 108;
  _109: 109;
  _110: 110;
  _111: 111;
  _112: 112;
  _113: 113;
  _114: 114;
  _115: 115;
  _116: 116;
  _117: 117;
  _118: 118;
  _119: 119;
  _120: 120;
  _121: 121;
  _122: 122;
  _123: 123;
  _124: 124;
  _125: 125;
  _126: 126;
  _127: 127;
  _128: 128;
  _129: 129;
  _130: 130;
  _131: 131;
  _132: 132;
  _133: 133;
  _134: 134;
  _135: 135;
  _136: 136;
  _137: 137;
  _138: 138;
  _139: 139;
  _140: 140;
  _141: 141;
  _142: 142;
  _143: 143;
  _144: 144;
  _145: 145;
  _146: 146;
  _147: 147;
  _148: 148;
  _149: 149;
  _150: 150;
  _151: 151;
  _152: 152;
  _153: 153;
  _154: 154;
  _155: 155;
  _156: 156;
  _157: 157;
  _158: 158;
  _159: 159;
  _160: 160;
  _161: 161;
  _162: 162;
  _163: 163;
  _164: 164;
  _165: 165;
  _166: 166;
  _167: 167;
  _168: 168;
  _169: 169;
  _170: 170;
  _171: 171;
  _172: 172;
  _173: 173;
  _174: 174;
  _175: 175;
  _176: 176;
  _177: 177;
  _178: 178;
  _179: 179;
  _180: 180;
  _181: 181;
  _182: 182;
  _183: 183;
  _184: 184;
  _185: 185;
  _186: 186;
  _187: 187;
  _188: 188;
  _189: 189;
  _190: 190;
  _191: 191;
  _192: 192;
  _193: 193;
  _194: 194;
  _195: 195;
  _196: 196;
  _197: 197;
  _198: 198;
  _199: 199;
  _200: 200;
  _201: 201;
  _202: 202;
  _203: 203;
  _204: 204;
  _205: 205;
  _206: 206;
  _207: 207;
  _208: 208;
  _209: 209;
  _210: 210;
  _211: 211;
  _212: 212;
  _213: 213;
  _214: 214;
  _215: 215;
  _216: 216;
  _217: 217;
  _218: 218;
  _219: 219;
  _220: 220;
  _221: 221;
  _222: 222;
  _223: 223;
  _224: 224;
  _225: 225;
  _226: 226;
  _227: 227;
  _228: 228;
  _229: 229;
  _230: 230;
  _231: 231;
  _232: 232;
  _233: 233;
  _234: 234;
  _235: 235;
  _236: 236;
  _237: 237;
  _238: 238;
  _239: 239;
  _240: 240;
  _241: 241;
  _242: 242;
  _243: 243;
  _244: 244;
  _245: 245;
  _246: 246;
  _247: 247;
  _248: 248;
  _249: 249;
  _250: 250;
  _251: 251;
  _252: 252;
  _253: 253;
  _254: 254;
  _255: 255;
  _256: 256;
  _257: 257;
  _258: 258;
  _259: 259;
  _260: 260;
  _261: 261;
  _262: 262;
  _263: 263;
  _264: 264;
  _265: 265;
  _266: 266;
  _267: 267;
  _268: 268;
  _269: 269;
  _270: 270;
  _271: 271;
  _272: 272;
  _273: 273;
  _274: 274;
  _275: 275;
  _276: 276;
  _277: 277;
  _278: 278;
  _279: 279;
  _280: 280;
  _281: 281;
  _282: 282;
  _283: 283;
  _284: 284;
  _285: 285;
  _286: 286;
  _287: 287;
  _288: 288;
  _289: 289;
  _290: 290;
  _291: 291;
  _292: 292;
  _293: 293;
  _294: 294;
  _295: 295;
  _296: 296;
  _297: 297;
  _298: 298;
  _299: 299;
  _300: 300;
  _301: 301;
  _302: 302;
  _303: 303;
  _304: 304;
  _305: 305;
  _306: 306;
  _307: 307;
  _308: 308;
  _309: 309;
  _310: 310;
  _311: 311;
  _312: 312;
  _313: 313;
  _314: 314;
  _315: 315;
  _316: 316;
  _317: 317;
  _318: 318;
  _319: 319;
  _320: 320;
  _321: 321;
  _322: 322;
  _323: 323;
  _324: 324;
  _325: 325;
  _326: 326;
  _327: 327;
  _328: 328;
  _329: 329;
  _330: 330;
  _331: 331;
  _332: 332;
  _333: 333;
  _334: 334;
  _335: 335;
  _336: 336;
  _337: 337;
  _338: 338;
  _339: 339;
  _340: 340;
  _341: 341;
  _342: 342;
  _343: 343;
  _344: 344;
  _345: 345;
  _346: 346;
  _347: 347;
  _348: 348;
  _349: 349;
  _350: 350;
  _351: 351;
  _352: 352;
  _353: 353;
  _354: 354;
  _355: 355;
  _356: 356;
  _357: 357;
  _358: 358;
  _359: 359;
  _360: 360;
  _361: 361;
  _362: 362;
  _363: 363;
  _364: 364;
  _365: 365;
  _366: 366;
  _367: 367;
  _368: 368;
  _369: 369;
  _370: 370;
  _371: 371;
  _372: 372;
  _373: 373;
  _374: 374;
  _375: 375;
  _376: 376;
  _377: 377;
  _378: 378;
  _379: 379;
  _380: 380;
  _381: 381;
  _382: 382;
  _383: 383;
  _384: 384;
  _385: 385;
  _386: 386;
  _387: 387;
  _388: 388;
  _389: 389;
  _390: 390;
  _391: 391;
  _392: 392;
  _393: 393;
  _394: 394;
  _395: 395;
  _396: 396;
  _397: 397;
  _398: 398;
  _399: 399;
  _400: 400;
  _401: 401;
  _402: 402;
  _403: 403;
  _404: 404;
  _405: 405;
  _406: 406;
  _407: 407;
  _408: 408;
  _409: 409;
  _410: 410;
  _411: 411;
  _412: 412;
  _413: 413;
  _414: 414;
  _415: 415;
  _416: 416;
  _417: 417;
  _418: 418;
  _419: 419;
  _420: 420;
  _421: 421;
  _422: 422;
  _423: 423;
  _424: 424;
  _425: 425;
  _426: 426;
  _427: 427;
  _428: 428;
  _429: 429;
  _430: 430;
  _431: 431;
  _432: 432;
  _433: 433;
  _434: 434;
  _435: 435;
  _436: 436;
  _437: 437;
  _438: 438;
  _439: 439;
  _440: 440;
  _441: 441;
  _442: 442;
  _443: 443;
  _444: 444;
  _445: 445;
  _446: 446;
  _447: 447;
  _448: 448;
  _449: 449;
  _450: 450;
  _451: 451;
  _452: 452;
  _453: 453;
  _454: 454;
  _455: 455;
  _456: 456;
  _457: 457;
  _458: 458;
  _459: 459;
  _460: 460;
  _461: 461;
  _462: 462;
  _463: 463;
  _464: 464;
  _465: 465;
  _466: 466;
  _467: 467;
  _468: 468;
  _469: 469;
  _470: 470;
  _471: 471;
  _472: 472;
  _473: 473;
  _474: 474;
  _475: 475;
  _476: 476;
  _477: 477;
  _478: 478;
  _479: 479;
  _480: 480;
  _481: 481;
  _482: 482;
  _483: 483;
  _484: 484;
  _485: 485;
  _486: 486;
  _487: 487;
  _488: 488;
  _489: 489;
  _490: 490;
  _491: 491;
  _492: 492;
  _493: 493;
  _494: 494;
  _495: 495;
  _496: 496;
  _497: 497;
  _498: 498;
  _499: 499;
}
type A/*1*/ = keyof Foo;
type Exclude<T, U> = T extends U ? never : T;
type Less/*2*/ = Exclude<A, "_0">;
function f<T extends A>(s: T, x: Exclude<A, T>, y: string) {}
f("_499", /*3*/);
type Decomposed/*4*/ = {[K in A]: Foo[K]}
type LongTuple/*5*/ = [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17.18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70];
type DeeplyMapped/*6*/ = {[K in keyof Foo]: {[K2 in keyof Foo]: [K, K2, Foo[K], Foo[K2]]}})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifyQuickInfoIs(t, "type A = keyof Foo", "");
		f->GoToMarker(t, "2");
		f->VerifyQuickInfoIs(t, R"TS(type Less = "_1" | "_10" | "_100" | "_101" | "_102" | "_103" | "_104" | "_105" | "_106" | "_107" | "_108" | "_109" | "_11" | "_110" | "_111" | "_112" | "_113" | "_114" | "_115" | "_116" | "_117" | "_118" | "_119" | "_12" | "_120" | "_121" | "_122" | "_123" | "_124" | "_125" | "_126" | "_127" | "_128" | "_129" | "_13" | "_130" | "_131" | "_132" | "_133" | "_134" | "_135" | "_136" | "_137" | "_138" | "_139" | "_14" | "_140" | "_141" | "_142" | "_143" | "_144" | "_145" | "_146" | "_147" | "_148" | "_149" | "_15" | "_150" | "_151" | "_152" | "_153" | "_154" | "_155" | "_156" | ... 434 more ... | "_99")TS", "");
		f->GoToMarker(t, "3");
		f->GoToMarker(t, "3");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = R"TS(f(s: "_499", x: "_0" | "_1" | "_10" | "_100" | "_101" | "_102" | "_103" | "_104" | "_105" | "_106" | "_107" | "_108" | "_109" | "_11" | "_110" | "_111" | "_112" | "_113" | "_114" | "_115" | "_116" | ... 477 more ... | "_99", y: string): void)TS"});
		f->GoToMarker(t, "4");
		f->VerifyQuickInfoIs(t, R"TS(type Decomposed = {
    _0: 0;
    _1: 1;
    _10: 10;
    _100: 100;
    _101: 101;
    _102: 102;
    _103: 103;
    _104: 104;
    _105: 105;
    _106: 106;
    _107: 107;
    _108: 108;
    _109: 109;
    _11: 11;
    _110: 110;
    _111: 111;
    _112: 112;
    _113: 113;
    _114: 114;
    _115: 115;
    _116: 116;
    _117: 117;
    _118: 118;
    _119: 119;
    _12: 12;
    _120: 120;
    _121: 121;
    _122: 122;
    _123: 123;
    _124: 124;
    _125: 125;
    _126: 126;
    _127: 127;
    _128: 128;
    _129: 129;
    _13: 13;
    _130: 130;
    _131: 131;
    _132: 132;
    _133: 133;
    _134: 134;
    _135: 135;
    _136: 136;
    _137: 137;
    _138: 138;
    _139: 139;
    _14: 14;
    _140: 140;
    _141: 141;
    _142: 142;
    _143: 143;
    _144: 144;
    _145: 145;
    _146: 146;
    _147: 147;
    _148: 148;
    _149: 149;
    _15: 15;
    _150: 150;
    _151: 151;
    _152: 152;
    _153: 153;
    _154: 154;
    _155: 155;
    _156: 156;
    _157: 157;
    ... 433 more ...;
    _99: 99;
})TS", "");
		f->GoToMarker(t, "5");
		f->VerifyQuickInfoIs(t, "type LongTuple = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17.18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70]", "");
		f->GoToMarker(t, "6");
		f->VerifyQuickInfoIs(t, R"TS(type DeeplyMapped = {
    _0: {
        _0: ["_0", "_0", 0, 0];
        _1: ["_0", "_1", 0, 1];
        _2: ["_0", "_2", 0, 2];
        _3: ["_0", "_3", 0, 3];
        _4: ["_0", "_4", 0, 4];
        _5: ["_0", "_5", 0, 5];
        _6: ["_0", "_6", 0, 6];
        _7: ["_0", "_7", 0, 7];
        _8: ["_0", "_8", 0, 8];
        _9: ["_0", "_9", 0, 9];
        _10: ["_0", "_10", 0, 10];
        _11: ["_0", "_11", 0, 11];
        _12: ["_0", "_12", 0, 12];
        _13: ["_0", "_13", 0, 13];
        _14: ["_0", "_14", 0, 14];
        _15: ["_0", "_15", 0, 15];
        _16: ["_0", "_16", 0, 16];
        _17: ["_0", "_17", 0, 17];
        _18: ["_0", "_18", 0, 18];
        _19: ["_0", "_19", 0, 19];
        _20: ["_0", "_20", 0, 20];
        _21: ["_0", "_21", 0, 21];
        ... 477 more ...;
        _499: [...];
    };
    ... 498 more ...;
    _499: ...;
})TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoCanBeTruncated, TestQuickInfoCanBeTruncated);

// quickInfoCatch_test.go

// quickInfoCatch_test.go
static void TestQuickCatchInfo(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(try {} catch(/*1*/error) {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "var error: unknown", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickCatchInfo, TestQuickCatchInfo);

// quickInfoClassKeyword_test.go

// quickInfoClassKeyword_test.go
static void TestQuickInfoClassKeyword(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([1].forEach(cla/*1*/ss {});
[1].forEach(cla/*2*/ss OK{});)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(local class) (Anonymous class)", "");
		f->VerifyQuickInfoAt(t, "2", "(local class) OK", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoClassKeyword, TestQuickInfoClassKeyword);

// quickInfoCloduleWithRecursiveReference_test.go

// quickInfoCloduleWithRecursiveReference_test.go
static void TestQuickInfoCloduleWithRecursiveReference(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(namespace M {
    export class C {
        foo() { }
    }
    export namespace C {
    export var /**/C = M.C
  }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "var M.C.C: typeof M.C", "");
		f->VerifyNoErrors(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoCloduleWithRecursiveReference, TestQuickInfoCloduleWithRecursiveReference);

// quickInfoCommentsClassMembers_test.go

// quickInfoCommentsClassMembers_test.go
static void TestQuickInfoCommentsClassMembers(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** This is comment for c1*/
class c/*1*/1 {
    /** p1 is property of c1*/
    public p/*2*/1: number;
    /** sum with property*/
    public p/*3*/2(/** number to add*/b: number) {
        return this.p1 + b;
    }
    /** getter property 1*/
    public get p/*6*/3() {
        return this.p/*8q*/2(this.p1);
    }
    /** setter property 1*/
    public set p/*10*/3(/** this is value*/value: number) {
        this.p1 = this.p/*13q*/2(value);
    }
    /** pp1 is property of c1*/
    private p/*14*/p1: number;
    /** sum with property*/
    private p/*15*/p2(/** number to add*/b: number) {
        return this.p1 + b;
    }
    /** getter property 2*/
    private get p/*18*/p3() {
        return this.p/*20q*/p2(this.pp1);
    }
    /** setter property 2*/
    private set p/*22*/p3( /** this is value*/value: number) {
        this.pp1 = this.p/*25q*/p2(value);
    }
    /** Constructor method*/
    constru/*26*/ctor() {
    }
    /** s1 is static property of c1*/
    static s/*27*/1: number;
    /** static sum with property*/
    static s/*28*/2(/** number to add*/b: number) {
        return c1.s1 + b;
    }
    /** static getter property*/
    static get s/*32*/3() {
        return c1.s/*35q*/2(c1.s1);
    }
    /** setter property 3*/
    static set s/*37*/3( /** this is value*/value: number) {
        c1.s1 = c1.s/*42q*/2(value);
    }
    public nc_/*43*/p1: number;
    public nc_/*44*/p2(b: number) {
        return this.nc_p1 + b;
    }
    public get nc_/*46*/p3() {
        return this.nc/*47q*/_p2(this.nc_p1);
    }
    public set nc/*48*/_p3(value: number) {
        this.nc_p1 = this.nc/*49q*/_p2(value);
    }
    private nc/*50*/_pp1: number;
    private nc_/*51*/pp2(b: number) {
        return this.nc_pp1 + b;
    }
    private get nc/*53*/_pp3() {
        return this.nc_/*54q*/pp2(this.nc_pp1);
    }
    private set nc_p/*55*/p3(value: number) {
        this.nc_pp1 = this./*56q*/nc_pp2(value);
    }
    static nc/*57*/_s1: number;
    static nc/*58*/_s2(b: number) {
        return c1.nc_s1 + b;
    }
    static get nc/*60*/_s3() {
        return c1.nc/*61q*/_s2(c1.nc_s1);
    }
    static set nc/*62*/_s3(value: number) {
        c1.nc_s1 = c1.nc_/*63q*/s2(value);
    }
}
var i/*64*/1 = new c/*65q*/1();
var i1/*66*/_p = i1.p1;
var i1/*68*/_f = i1.p/*69*/2;
var i1/*70*/_r = i1.p/*71q*/2(20);
var i1_p/*72*/rop = i1./*73*/p3;
i1./*74*/p3 = i1_/*75*/prop;
var i1_/*76*/nc_p = i1.n/*77*/c_p1;
var i1/*78*/_ncf = i1.nc_/*79*/p2;
var i1_/*80*/ncr = i1.nc/*81q*/_p2(20);
var i1_n/*82*/cprop = i1.n/*83*/c_p3;
i1.nc/*84*/_p3 = i1_/*85*/ncprop;
var i1_/*86*/s_p = /*87*/c1./*88*/s1;
var i1_s/*89*/_f = c1./*90*/s2;
var i1_/*91*/s_r = c1.s/*92q*/2(20);
var i1_s/*93*/_prop = c1.s/*94*/3;
c1.s/*95*/3 = i1_s/*96*/_prop;
var i1_s/*97*/_nc_p = c1.n/*98*/c_s1;
var i1_s_/*99*/ncf = c1.nc/*100*/_s2;
var i1_s_/*101*/ncr = c1.n/*102q*/c_s2(20);
var i1_s_n/*103*/cprop = c1.nc/*104*/_s3;
c1.nc/*105*/_s3 = i1_s_nc/*106*/prop;
var i1/*107*/_c = c/*108*/1;

class cProperties {
    private val: number;
    /** getter only property*/
    public get p1() {
        return this.val;
    }
    public get nc_p1() {
        return this.val;
    }
    /**setter only property*/
    public set p2(value: number) {
        this.val = value;
    }
    public set nc_p2(value: number) {
        this.val = value;
    }
}
var cProperties_i = new cProperties();
cProperties_i./*110*/p2 = cProperties_i.p/*111*/1;
cProperties_i.nc/*112*/_p2 = cProperties_i.nc/*113*/_p1;
class cWithConstructorProperty {
    /**
    * this is class cWithConstructorProperty's constructor
    * @param a this is first parameter a
    */
    /*119*/constructor(/**more info about a*/public a: number) {
        var b/*118*/bbb = 10;
        th/*116*/is./*114*/a = /*115*/a + 2 + bb/*117*/bb;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoCommentsClassMembers, TestQuickInfoCommentsClassMembers);

// quickInfoCommentsClass_test.go

// quickInfoCommentsClass_test.go
static void TestQuickInfoCommentsClass(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** This is class c2 without constructor*/
class c/*1*/2 {
}
var i/*2*/2 = new c/*28*/2();
var i2/*4*/_c = c/*5*/2;
class c/*6*/3 {
    /** Constructor comment*/
    constructor() {
    }
}
var i/*7*/3 = new c/*29*/3();
var i3/*9*/_c = c/*10*/3;
/** Class comment*/
class c/*11*/4 {
    /** Constructor comment*/
    constructor() {
    }
}
var i/*12*/4 = new c/*30*/4();
var i4/*14*/_c = c/*15*/4;
/** Class with statics*/
class c/*16*/5 {
    static s1: number;
}
var i/*17*/5 = new c/*31*/5();
var i5_/*19*/c = c/*20*/5;
/** class with statics and constructor*/
class c/*21*/6 {
    /** s1 comment*/
    static s1: number;
    /** constructor comment*/
    constructor() {
    }
}
var i/*22*/6 = new c/*32*/6();
var i6/*24*/_c = c/*25*/6;

class a {
    /**
    constructor for a
    @param a this is my a
    */
    constructor(a: string) {
    }
}
new a("Hello");
namespace m {
    export namespace m2 {
        /** class comment */
        export class c1 {
            /** constructor comment*/
            constructor() {
            }
        }
    }
}
var myVar = new m.m2.c/*33*/1();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoCommentsClass, TestQuickInfoCommentsClass);

// quickInfoCommentsCommentParsing_test.go

// quickInfoCommentsCommentParsing_test.go
static void TestQuickInfoCommentsCommentParsing(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/// This is simple /// comments
function simple() {
}

sim/*1q*/ple( );

/// multiLine /// Comments
/// This is example of multiline /// comments
/// Another multiLine
function multiLine() {
}
mul/*2q*/tiLine( );

/** this is eg of single line jsdoc style comment */
function jsDocSingleLine() {
}
jsDoc/*3q*/SingleLine();


/** this is multiple line jsdoc stule comment
*New line1
*New Line2*/
function jsDocMultiLine() {
}
jsDocM/*4q*/ultiLine();

/** multiple line jsdoc comments no longer merge
*New line1
*New Line2*/
/** Shoul mege this line as well
* and this too*/ /** Another this one too*/
function jsDocMultiLineMerge() {
}
jsDocMu/*5q*/ltiLineMerge();


/// Triple slash comment
/** jsdoc comment */
function jsDocMixedComments1() {
}
jsDocMix/*6q*/edComments1();

/// Triple slash comment
/** jsdoc comment */ /** another jsDocComment*/
function jsDocMixedComments2() {
}
jsDocMi/*7q*/xedComments2();

/** jsdoc comment */ /*** triplestar jsDocComment*/
/// Triple slash comment
function jsDocMixedComments3() {
}
jsDocMixe/*8q*/dComments3();

/** jsdoc comment */ /** another jsDocComment*/
/// Triple slash comment
/// Triple slash comment 2
function jsDocMixedComments4() {
}
jsDocMixed/*9q*/Comments4();

/// Triple slash comment 1
/** jsdoc comment */ /** another jsDocComment*/
/// Triple slash comment
/// Triple slash comment 2
function jsDocMixedComments5() {
}
jsDocM/*10q*/ixedComments5();

/** another jsDocComment*/
/// Triple slash comment 1
/// Triple slash comment
/// Triple slash comment 2
/** jsdoc comment */
function jsDocMixedComments6() {
}
jsDocMix/*11q*/edComments6();

// This shoulnot be help comment
function noHelpComment1() {
}
noHel/*12q*/pComment1();

/* This shoulnot be help comment */
function noHelpComment2() {
}
noHelpC/*13q*/omment2();

function noHelpComment3() {
}
noHelpC/*14q*/omment3();
/** Adds two integers and returns the result
  * @param {number} a first number
  * @param b second number
  */
function sum(/*16aq*/a: number, /*17aq*/b: number) {
    return a + b;
}
s/*16q*/um(10, 20);
/** This is multiplication function
 * @param 
 * @param a first number
 * @param b
 * @param c {
 @param d @anotherTag
 * @param e LastParam @anotherTag*/
function multiply(/*19aq*/a: number, /*20aq*/b: number, /*21aq*/c?: number, /*22aq*/d?, /*23aq*/e?) {
}
mult/*19q*/iply(10, 20, 30, 40, 50);
/** fn f1 with number
* @param { string} b about b
*/
function f1(/*25aq*/a: number);
function f1(/*26aq*/b: string);
/**@param opt optional parameter*/
function f1(aOrb, opt?) {
    return aOrb;
}
f/*25q*/1(10);
f/*26q*/1("hello");

/** This is subtract function
@param { a
*@param { number | } b this is about b
@param { { () => string; } } c this is optional param c
@param { { () => string; } d this is optional param d
@param { { () => string; } } e this is optional param e
@param { { { () => string; } } f this is optional param f
*/
function subtract(/*28aq*/a: number, /*29aq*/b: number, /*30aq*/c?: () => string, /*31aq*/d?: () => string, /*32aq*/e?: () => string, /*33aq*/f?: () => string) {
}
subt/*28q*/ract(10,  20,  null,  null,  null, null);
/** this is square function
@paramTag { number } a this is input number of paramTag
@param { number } a this is input number
@returnType { number } it is return type
*/
function square(/*34aq*/a: number) {
    return a * a;
}
squ/*34q*/are(10);
/** this is divide function
@param { number} a this is a
@paramTag { number } g this is optional param g
@param { number} b this is b
*/
function divide(/*35aq*/a: number, /*36aq*/b: number) {
}
div/*35q*/ide(10, 20);
/**
Function returns string concat of foo and bar
@param			{string}		foo		is string
@param		    {string}		bar		is second string
*/
function fooBar(/*37aq*/foo: string, /*38aq*/bar: string) {
    return foo + bar;
}
fo/*37q*/oBar("foo","bar");
/** This is a comment */
var x;
/**
  * This is a comment
  */
var y;
/** this is jsdoc style function with param tag as well as inline parameter help
*@param a it is first parameter
*@param c it is third parameter
*/
function jsDocParamTest(/** this is inline comment for a *//*40aq*/a: number, /** this is inline comment for b*/ /*41aq*/b: number, /*42aq*/c: number, /*43aq*/d: number) {
    return a + b + c + d;
}
jsD/*40q*/ocParamTest(30, 40, 50, 60);
/** This is function comment
  * And properly aligned comment
  */
function jsDocCommentAlignmentTest1() {
}
jsDocCom/*45q*/mentAlignmentTest1();
/** This is function comment
  *     And aligned with 4 space char margin
  */
function jsDocCommentAlignmentTest2() {
}
jsDocComme/*46q*/ntAlignmentTest2();
/** This is function comment
  *     And aligned with 4 space char margin
  * @param {string} a this is info about a
  *                   spanning on two lines and aligned perfectly
  * @param b          this is info about b
  *                   spanning on two lines and aligned perfectly
  *                   spanning one more line alined perfectly
  *                       spanning another line with more margin
  * @param c          this is info about b
  *  not aligned text about parameter will eat only one space
  */
function jsDocCommentAlignmentTest3(/*47aq*/a: string, /*48aq*/b, /*49aq*/c) {
}
jsDocComme/*47q*/ntAlignmentTest3("hello",1, 2);
/**/
class NoQuic/*50q*/kInfoClass {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoCommentsCommentParsing, TestQuickInfoCommentsCommentParsing);

// quickInfoCommentsFunctionDeclarationVS_test.go

// quickInfoCommentsFunctionDeclarationVS_test.go
static void TestQuickInfoCommentsFunctionDeclarationVS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** This comment should appear for foo*/
function f/*1*/oo() {
}
f/*2*/oo();
/** This is comment for function signature*/
function fo/*5*/oWithParameters(/** this is comment about a*/a: string,
    /** this is comment for b*/
    b: number) {
    var /*6*/d = a;
}
fooWithParam/*8*/eters("a",10);
/**
* Does something
* @param a a string
*/
declare function fn(a: string);
fn("hello");)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineVSHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoCommentsFunctionDeclarationVS, TestQuickInfoCommentsFunctionDeclarationVS);

// quickInfoCommentsFunctionDeclaration_test.go

// quickInfoCommentsFunctionDeclaration_test.go
static void TestQuickInfoCommentsFunctionDeclaration(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** This comment should appear for foo*/
function f/*1*/oo() {
}
f/*2*/oo();
/** This is comment for function signature*/
function fo/*5*/oWithParameters(/** this is comment about a*/a: string,
    /** this is comment for b*/
    b: number) {
    var /*6*/d = a;
}
fooWithParam/*8*/eters("a",10);
/**
* Does something
* @param a a string
*/
declare function fn(a: string);
fn("hello");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoCommentsFunctionDeclaration, TestQuickInfoCommentsFunctionDeclaration);

// quickInfoCommentsFunctionExpression_test.go

// quickInfoCommentsFunctionExpression_test.go
static void TestQuickInfoCommentsFunctionExpression(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** lambdaFoo var comment*/
var lamb/*1*/daFoo = /** this is lambda comment*/ (/**param a*/a: number, /**param b*/b: number) => a + b;
var lambddaN/*3*/oVarComment = /** this is lambda multiplication*/ (/**param a*/a: number, /**param b*/b: number) => a * b;
lambdaFoo(10, 20);
function /*7*/anotherFunc(a: number) {
    /** documentation
        @param b {string} inner parameter */
    var /*8*/lambdaVar = /** inner docs */(/*9*/b: string) => {
        var /*10*/localVar = "Hello ";
        return /*11*/localVar + /*12*/b;
    }
    return lamb/*13*/daVar("World") + a;
}
/**
 * On variable
 * @param s the first parameter!
 * @returns the parameter's length
 */
var assi/*14*/gned = /**
                * Summary on expression
                * @param s param on expression
                * @returns return on expression
                */function(/** On parameter */s: string) {
  return s.length;
}
assig/*16*/ned("hey");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoCommentsFunctionExpression, TestQuickInfoCommentsFunctionExpression);

// quickInfoConstAssertion_test.go

// quickInfoConstAssertion_test.go
static void TestQuickInfoConstAssertion(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(const foo = 42 as /*1*/const)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "type const = 42", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoConstAssertion, TestQuickInfoConstAssertion);

// quickInfoContextualObjectMethodJSDoc_test.go

// quickInfoContextualObjectMethodJSDoc_test.go
static void TestQuickInfoContextualObjectMethodJSDoc(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
interface I {
    /**
     * Description of func.
     * @param arg Description of arg.
     */
    func(arg: number): void
}

class Foo {
    constructor(i: I) {}
}

new Foo({ func/*1*/() {} })
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(method) I.func(arg: number): void", R"TS(Description of func.

*@param* `arg` — Description of arg.)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoContextualObjectMethodJSDoc, TestQuickInfoContextualObjectMethodJSDoc);

// quickInfoContextualTyping_test.go

// quickInfoContextualTyping_test.go
static void TestQuickInfoContextualTyping(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// DEFAULT INTERFACES
interface IFoo {
    n: number;
    s: string;
    f(i: number, s: string): string;
    a: number[];
}
interface IBar {
    foo: IFoo;
}
// CONTEXT: Class property declaration
class C1T5 {
    /*1*/foo: (i: number, s: string) => number = function(/*2*/i) {
        return /*3*/i;
    }
}
// CONTEXT: Module property declaration
namespace C2T5 {
    export var /*4*/foo: (i: number, s: string) => number = function(/*5*/i) {
        return /*6*/i;
    }
}
// CONTEXT: Variable declaration
var /*7*/c3t1: (s: string) => string = (function(/*8*/s) { return /*9*/s });
var /*10*/c3t2 = <IFoo>({
    n: 1
})
var /*11*/c3t3: number[] = [];
var /*12*/c3t4: () => IFoo = function() { return <IFoo>({}) };
var /*13*/c3t5: (n: number) => IFoo = function(/*14*/n) { return <IFoo>({}) };
var /*15*/c3t6: (n: number, s: string) => IFoo = function(/*16*/n, /*17*/s) { return <IFoo>({}) };
var /*18*/c3t7: {
    (n: number): number;
    (s1: string): number;
};
var /*20*/c3t8: (n: number, s: string) => number = function(/*21*/n) { return n; };
var /*22*/c3t9: number[][] = [[],[]];
var /*23*/c3t10: IFoo[] = [<IFoo>({}),<IFoo>({})];
var /*24*/c3t11: {(n: number, s: string): string;}[] = [function(/*25*/n, /*26*/s) { return s; }];
var /*27*/c3t12: IBar = {
    /*28*/foo: <IFoo>({})
}
var /*29*/c3t13 = <IFoo>({
    /*30*/f: function(/*31*/i, /*32*/s) { return s; }
})
var /*33*/c3t14 = <IFoo>({
    /*34*/a: []
})
// CONTEXT: Class property assignment
class C4T5 {
    /*35*/foo: (i: number, s: string) => string;
    constructor() {
        this.foo = function(/*36*/i, /*37*/s) {
            return s;
        }
    }
}
// CONTEXT: Module property assignment
namespace C5T5 {
    export var /*38*/foo: (i: number, s: string) => string;
    foo = function(/*39*/i, /*40*/s) {
        return s;
    }
}
// CONTEXT: Variable assignment
var /*41*/c6t5: (n: number) => IFoo;
c6t5 = <(n: number) => IFoo>function(/*42*/n) { return <IFoo>({}) };
// CONTEXT: Array index assignment
var /*43*/c7t2: IFoo[];
/*44*/c7t2[0] = <IFoo>({n: 1});
// CONTEXT: Object property assignment
interface IPlaceHolder {
    t1: (s: string) => string;
    t2: IFoo;
    t3: number[];
    t4: () => IFoo;
    t5: (n: number) => IFoo;
    t6: (n: number, s: string) => IFoo;
    t7: {
            (n: number, s: string): number;
            //(s1: string, s2: string): number;
        };
    t8: (n: number, s: string) => number;
    t9: number[][];
    t10: IFoo[];
    t11: {(n: number, s: string): string;}[];
    t12: IBar;
    t13: IFoo;
    t14: IFoo;
    }
var objc8: {
    t1: (s: string) => string;
    t2: IFoo;
    t3: number[];
    t4: () => IFoo;
    t5: (n: number) => IFoo;
    t6: (n: number, s: string) => IFoo;
    t7: {
            (n: number, s: string): number;
            //(s1: string, s2: string): number;
        };
    t8: (n: number, s: string) => number;
    t9: number[][];
    t10: IFoo[];
    t11: {(n: number, s: string): string;}[];
    t12: IBar;
    t13: IFoo;
    t14: IFoo;
} = <IPlaceHolder>({});
objc8./*45*/t1 = (function(/*46*/s) { return s });
objc8./*47*/t2 = <IFoo>({
    n: 1
});
objc8./*48*/t3 = [];
objc8./*49*/t4 = function() { return <IFoo>({}) };
objc8./*50*/t5 = function(/*51*/n) { return <IFoo>({}) };
objc8./*52*/t6 = function(/*53*/n, /*54*/s) { return <IFoo>({}) };
objc8./*55*/t7 = function(n: number) { return n };
objc8./*56*/t8 = function(/*57*/n) { return n; };
objc8./*58*/t9 = [[],[]];
objc8./*59*/t10 = [<IFoo>({}),<IFoo>({})];
objc8./*60*/t11 = [function (/*61*/n, /*62*/s) { return s; }];
objc8./*63*/t12 = {
    /*64*/foo: <IFoo>({})
}
objc8./*65*/t13 = <IFoo>({
    /*66*/f: function(/*67*/i, /*68*/s) { return s; }
})
objc8./*69*/t14 = <IFoo>({
    /*70*/a: []
})
// CONTEXT: Function call
function c9t5(f: (n: number) => IFoo) {};
c9t5(function(/*71*/n) {
    return <IFoo>({});
});
// CONTEXT: Return statement
var /*72*/c10t5: () => (n: number) => IFoo = function() { return function(/*73*/n) { return <IFoo>({}) } };
// CONTEXT: Newing a class
class C11t5 { constructor(f: (n: number) => IFoo) { } };
var i = new C11t5(function(/*74*/n) { return <IFoo>({}) });
// CONTEXT: Type annotated expression
var /*75*/c12t1 = <(s: string) => string> (function (/*76*/s) { return s });
var /*77*/c12t2 = <IFoo> ({
    n: 1
});
var /*78*/c12t3 = <number[]> [];
var /*79*/c12t4 = <() => IFoo> function() { return <IFoo>({}) };
var /*80*/c12t5 = <(n: number) => IFoo> function(/*81*/n) { return <IFoo>({}) };
var /*82*/c12t6 = <(n: number, s: string) => IFoo> function(/*83*/n, /*84*/s) { return <IFoo>({}) };
var /*85*/c12t7 = <{
    (n: number, s: string): number;
    //(s1: string, s2: string): number;
}> function(n:number) { return n };
var /*86*/c12t8 = <(n: number, s: string) => number> function (/*87*/n) { return n; };
var /*88*/c12t9 = <number[][]> [[],[]];
var /*89*/c12t10 = <IFoo[]> [<IFoo>({}),<IFoo>({})];
var /*90*/c12t11 = <{ (n: number, s: string): string; }[]>[function (/*91*/n, /*92*/s) { return s; }];
var /*93*/c12t12 = <IBar> {
    /*94*/foo: <IFoo>({})
}
var /*95*/c12t13 = <IFoo> ({
    /*96*/f: function(/*97*/i, /*98*/s) { return s; }
})
var /*99*/c12t14 = <IFoo> ({
    /*100*/a: []
})
// CONTEXT: Contextual typing declarations
// contextually typing function declarations
function EF1(a: number, b:number):number;
function /*101*/EF1(/*102*/a,/*103*/b) { return a+b; }
var efv = EF1(1,2);
// contextually typing from ambient class declarations
declare class Point
{
      constructor(x: number, y: number);
      x: number;
      y: number;
      add(dx: number, dy: number): Point;
      static origin: Point;
}
Point./*110*/origin = new /*111*/Point(0, 0);
Point.prototype./*112*/add = function (/*113*/dx, /*114*/dy) {
    return new Point(this.x + dx, this.y + dy);
};
Point.prototype = {
    x: 0,
    y: 0,
    /*115*/add: function (/*116*/dx, /*117*/dy) {
        return new Point(this.x + dx, this.y + dy);
    }
};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(property) C1T5.foo: (i: number, s: string) => number", "");
		f->VerifyQuickInfoAt(t, "2", "(parameter) i: number", "");
		f->VerifyQuickInfoAt(t, "3", "(parameter) i: number", "");
		f->VerifyQuickInfoAt(t, "4", "var C2T5.foo: (i: number, s: string) => number", "");
		f->VerifyQuickInfoAt(t, "5", "(parameter) i: number", "");
		f->VerifyQuickInfoAt(t, "6", "(parameter) i: number", "");
		f->VerifyQuickInfoAt(t, "7", "var c3t1: (s: string) => string", "");
		f->VerifyQuickInfoAt(t, "8", "(parameter) s: string", "");
		f->VerifyQuickInfoAt(t, "9", "(parameter) s: string", "");
		f->VerifyQuickInfoAt(t, "10", "var c3t2: IFoo", "");
		f->VerifyQuickInfoAt(t, "11", "var c3t3: number[]", "");
		f->VerifyQuickInfoAt(t, "12", "var c3t4: () => IFoo", "");
		f->VerifyQuickInfoAt(t, "13", "var c3t5: (n: number) => IFoo", "");
		f->VerifyQuickInfoAt(t, "14", "(parameter) n: number", "");
		f->VerifyQuickInfoAt(t, "15", "var c3t6: (n: number, s: string) => IFoo", "");
		f->VerifyQuickInfoAt(t, "16", "(parameter) n: number", "");
		f->VerifyQuickInfoAt(t, "17", "(parameter) s: string", "");
		f->VerifyQuickInfoAt(t, "18", R"TS(var c3t7: {
    (n: number): number;
    (s1: string): number;
})TS", "");
		f->VerifyQuickInfoAt(t, "20", "var c3t8: (n: number, s: string) => number", "");
		f->VerifyQuickInfoAt(t, "21", "(parameter) n: number", "");
		f->VerifyQuickInfoAt(t, "22", "var c3t9: number[][]", "");
		f->VerifyQuickInfoAt(t, "23", "var c3t10: IFoo[]", "");
		f->VerifyQuickInfoAt(t, "24", "var c3t11: ((n: number, s: string) => string)[]", "");
		f->VerifyQuickInfoAt(t, "25", "(parameter) n: number", "");
		f->VerifyQuickInfoAt(t, "26", "(parameter) s: string", "");
		f->VerifyQuickInfoAt(t, "27", "var c3t12: IBar", "");
		f->VerifyQuickInfoAt(t, "28", "(property) IBar.foo: IFoo", "");
		f->VerifyQuickInfoAt(t, "29", "var c3t13: IFoo", "");
		f->VerifyQuickInfoAt(t, "30", "(method) IFoo.f(i: number, s: string): string", "");
		f->VerifyQuickInfoAt(t, "31", "(parameter) i: number", "");
		f->VerifyQuickInfoAt(t, "32", "(parameter) s: string", "");
		f->VerifyQuickInfoAt(t, "33", "var c3t14: IFoo", "");
		f->VerifyQuickInfoAt(t, "34", "(property) IFoo.a: number[]", "");
		f->VerifyQuickInfoAt(t, "35", "(property) C4T5.foo: (i: number, s: string) => string", "");
		f->VerifyQuickInfoAt(t, "36", "(parameter) i: number", "");
		f->VerifyQuickInfoAt(t, "37", "(parameter) s: string", "");
		f->VerifyQuickInfoAt(t, "38", "var C5T5.foo: (i: number, s: string) => string", "");
		f->VerifyQuickInfoAt(t, "39", "(parameter) i: number", "");
		f->VerifyQuickInfoAt(t, "40", "(parameter) s: string", "");
		f->VerifyQuickInfoAt(t, "41", "var c6t5: (n: number) => IFoo", "");
		f->VerifyQuickInfoAt(t, "42", "(parameter) n: number", "");
		f->VerifyQuickInfoAt(t, "43", "var c7t2: IFoo[]", "");
		f->VerifyQuickInfoAt(t, "44", "var c7t2: IFoo[]", "");
		f->VerifyQuickInfoAt(t, "45", "(property) t1: (s: string) => string", "");
		f->VerifyQuickInfoAt(t, "46", "(parameter) s: string", "");
		f->VerifyQuickInfoAt(t, "47", "(property) t2: IFoo", "");
		f->VerifyQuickInfoAt(t, "48", "(property) t3: number[]", "");
		f->VerifyQuickInfoAt(t, "49", "(property) t4: () => IFoo", "");
		f->VerifyQuickInfoAt(t, "50", "(property) t5: (n: number) => IFoo", "");
		f->VerifyQuickInfoAt(t, "51", "(parameter) n: number", "");
		f->VerifyQuickInfoAt(t, "52", "(property) t6: (n: number, s: string) => IFoo", "");
		f->VerifyQuickInfoAt(t, "53", "(parameter) n: number", "");
		f->VerifyQuickInfoAt(t, "54", "(parameter) s: string", "");
		f->VerifyQuickInfoAt(t, "55", "(property) t7: (n: number, s: string) => number", "");
		f->VerifyQuickInfoAt(t, "56", "(property) t8: (n: number, s: string) => number", "");
		f->VerifyQuickInfoAt(t, "57", "(parameter) n: number", "");
		f->VerifyQuickInfoAt(t, "58", "(property) t9: number[][]", "");
		f->VerifyQuickInfoAt(t, "59", "(property) t10: IFoo[]", "");
		f->VerifyQuickInfoAt(t, "60", "(property) t11: ((n: number, s: string) => string)[]", "");
		f->VerifyQuickInfoAt(t, "61", "(parameter) n: number", "");
		f->VerifyQuickInfoAt(t, "62", "(parameter) s: string", "");
		f->VerifyQuickInfoAt(t, "63", "(property) t12: IBar", "");
		f->VerifyQuickInfoAt(t, "64", "(property) IBar.foo: IFoo", "");
		f->VerifyQuickInfoAt(t, "65", "(property) t13: IFoo", "");
		f->VerifyQuickInfoAt(t, "66", "(method) IFoo.f(i: number, s: string): string", "");
		f->VerifyQuickInfoAt(t, "67", "(parameter) i: number", "");
		f->VerifyQuickInfoAt(t, "68", "(parameter) s: string", "");
		f->VerifyQuickInfoAt(t, "69", "(property) t14: IFoo", "");
		f->VerifyQuickInfoAt(t, "70", "(property) IFoo.a: number[]", "");
		f->VerifyQuickInfoAt(t, "71", "(parameter) n: number", "");
		f->VerifyQuickInfoAt(t, "72", "var c10t5: () => (n: number) => IFoo", "");
		f->VerifyQuickInfoAt(t, "73", "(parameter) n: number", "");
		f->VerifyQuickInfoAt(t, "74", "(parameter) n: number", "");
		f->VerifyQuickInfoAt(t, "75", "var c12t1: (s: string) => string", "");
		f->VerifyQuickInfoAt(t, "76", "(parameter) s: string", "");
		f->VerifyQuickInfoAt(t, "77", "var c12t2: IFoo", "");
		f->VerifyQuickInfoAt(t, "78", "var c12t3: number[]", "");
		f->VerifyQuickInfoAt(t, "79", "var c12t4: () => IFoo", "");
		f->VerifyQuickInfoAt(t, "80", "var c12t5: (n: number) => IFoo", "");
		f->VerifyQuickInfoAt(t, "81", "(parameter) n: number", "");
		f->VerifyQuickInfoAt(t, "82", "var c12t6: (n: number, s: string) => IFoo", "");
		f->VerifyQuickInfoAt(t, "83", "(parameter) n: number", "");
		f->VerifyQuickInfoAt(t, "84", "(parameter) s: string", "");
		f->VerifyQuickInfoAt(t, "85", "var c12t7: (n: number, s: string) => number", "");
		f->VerifyQuickInfoAt(t, "86", "var c12t8: (n: number, s: string) => number", "");
		f->VerifyQuickInfoAt(t, "87", "(parameter) n: number", "");
		f->VerifyQuickInfoAt(t, "88", "var c12t9: number[][]", "");
		f->VerifyQuickInfoAt(t, "89", "var c12t10: IFoo[]", "");
		f->VerifyQuickInfoAt(t, "90", "var c12t11: ((n: number, s: string) => string)[]", "");
		f->VerifyQuickInfoAt(t, "91", "(parameter) n: number", "");
		f->VerifyQuickInfoAt(t, "92", "(parameter) s: string", "");
		f->VerifyQuickInfoAt(t, "93", "var c12t12: IBar", "");
		f->VerifyQuickInfoAt(t, "94", "(property) IBar.foo: IFoo", "");
		f->VerifyQuickInfoAt(t, "95", "var c12t13: IFoo", "");
		f->VerifyQuickInfoAt(t, "96", "(method) IFoo.f(i: number, s: string): string", "");
		f->VerifyQuickInfoAt(t, "97", "(parameter) i: number", "");
		f->VerifyQuickInfoAt(t, "98", "(parameter) s: string", "");
		f->VerifyQuickInfoAt(t, "99", "var c12t14: IFoo", "");
		f->VerifyQuickInfoAt(t, "100", "(property) IFoo.a: number[]", "");
		f->VerifyQuickInfoAt(t, "101", "function EF1(a: number, b: number): number", "");
		f->VerifyQuickInfoAt(t, "102", "(parameter) a: any", "");
		f->VerifyQuickInfoAt(t, "103", "(parameter) b: any", "");
		f->VerifyQuickInfoAt(t, "110", "(property) Point.origin: Point", "");
		f->VerifyQuickInfoAt(t, "111", "constructor Point(x: number, y: number): Point", "");
		f->VerifyQuickInfoAt(t, "112", "(method) Point.add(dx: number, dy: number): Point", "");
		f->VerifyQuickInfoAt(t, "113", "(parameter) dx: number", "");
		f->VerifyQuickInfoAt(t, "114", "(parameter) dy: number", "");
		f->VerifyQuickInfoAt(t, "115", "(method) Point.add(dx: number, dy: number): Point", "");
		f->VerifyQuickInfoAt(t, "116", "(parameter) dx: number", "");
		f->VerifyQuickInfoAt(t, "117", "(parameter) dy: number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoContextualTyping, TestQuickInfoContextualTyping);

// quickInfoContextuallyTypedSignatureOptionalParameterFromIntersection1_test.go

// quickInfoContextuallyTypedSignatureOptionalParameterFromIntersection1_test.go
static void TestQuickInfoContextuallyTypedSignatureOptionalParameterFromIntersection1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @strict: true
const optionals: ((a?: number) => unknown) & ((b?: string) => unknown) = (
  arg,
) =/**/> {};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "function(arg: string | number | undefined): void", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoContextuallyTypedSignatureOptionalParameterFromIntersection1, TestQuickInfoContextuallyTypedSignatureOptionalParameterFromIntersection1);

// quickInfoDefaultTypeParameter1_test.go

// quickInfoDefaultTypeParameter1_test.go
static void TestQuickInfoDefaultTypeParameter1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type /*1*/X</*2*/T = string> = T)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "type X<T = string> = T", "");
		f->VerifyQuickInfoAt(t, "2", "(type parameter) T in type X<T = string>", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDefaultTypeParameter1, TestQuickInfoDefaultTypeParameter1);

// quickInfoDestructuredBinding_test.go

// quickInfoDestructuredBinding_test.go
static void TestQuickInfoDestructuredBinding(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
function f({ /*1*/x }: { x: number }) {}
function g([/*2*/y]: number[]) {}
function h({ a: { /*3*/b } }: { a: { b: string } }) {}
const { /*4*/c } = { c: 42 };
let { /*5*/d } = { d: "hello" };
var { /*6*/e } = { e: true };
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(parameter) x: number", "");
		f->VerifyQuickInfoAt(t, "2", "(parameter) y: number", "");
		f->VerifyQuickInfoAt(t, "3", "(parameter) b: string", "");
		f->VerifyQuickInfoAt(t, "4", "const c: number", "");
		f->VerifyQuickInfoAt(t, "5", "let d: string", "");
		f->VerifyQuickInfoAt(t, "6", "var e: boolean", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDestructuredBinding, TestQuickInfoDestructuredBinding);

// quickInfoDistributedTypeParameter_test.go

// quickInfoDistributedTypeParameter_test.go
static void TestQuickInfoDistributedTypeParameter(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type Conditional<T> =
    T/*check*/ extends T/*extends*/
        ? T/*trueType*/
        : T/*falseType*/;

type NonDistributed<T> = [T/*nonDistributed*/] extends [unknown] ? T : never;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "check", "(type parameter) (distributed) T in type Conditional<T>", "");
		f->VerifyQuickInfoAt(t, "extends", "(type parameter) (distributed) T in type Conditional<T>", "");
		f->VerifyQuickInfoAt(t, "trueType", "(type parameter) (distributed) T in type Conditional<T>", "");
		f->VerifyQuickInfoAt(t, "falseType", "(type parameter) (distributed) T in type Conditional<T>", "");
		f->VerifyQuickInfoAt(t, "nonDistributed", "(type parameter) T in type NonDistributed<T>", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDistributedTypeParameter, TestQuickInfoDistributedTypeParameter);

// quickInfoElementAccessDeclaration_test.go

// quickInfoElementAccessDeclaration_test.go
static void TestQuickInfoElementAccessDeclaration(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @checkJs: true
// @allowJs: true
// @Filename: a.js
const mod = {};
mod["@@thing1"] = {};
mod["/**/@@thing1"]["@@thing2"] = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyQuickInfoIs(t, R"TS(module mod["@@thing1"]
(property) mod["@@thing1"]: typeof mod.@@thing1)TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoElementAccessDeclaration, TestQuickInfoElementAccessDeclaration);

// quickInfoEnumMembersAcceptNonAsciiStrings_test.go

// quickInfoEnumMembersAcceptNonAsciiStrings_test.go
static void TestQuickInfoEnumMembersAcceptNonAsciiStrings(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(enum Demo {
    /*Emoji*/Emoji = '🍎',
    /*Hebrew*/Hebrew = 'תפוח',
    /*Chinese*/Chinese = '苹果',
    /*Japanese*/Japanese = 'りんご',
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "Emoji", R"TS((enum member) Demo.Emoji = "🍎")TS", "");
		f->VerifyQuickInfoAt(t, "Hebrew", R"TS((enum member) Demo.Hebrew = "תפוח")TS", "");
		f->VerifyQuickInfoAt(t, "Chinese", R"TS((enum member) Demo.Chinese = "苹果")TS", "");
		f->VerifyQuickInfoAt(t, "Japanese", R"TS((enum member) Demo.Japanese = "りんご")TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoEnumMembersAcceptNonAsciiStrings, TestQuickInfoEnumMembersAcceptNonAsciiStrings);

// quickInfoExportAssignmentOfGenericInterface_test.go

// quickInfoExportAssignmentOfGenericInterface_test.go
static void TestQuickInfoExportAssignmentOfGenericInterface(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: quickInfoExportAssignmentOfGenericInterface_0.ts
interface Foo<T> {
    a: string;
}
export = Foo;
// @Filename: quickInfoExportAssignmentOfGenericInterface_1.ts
import a = require('./quickInfoExportAssignmentOfGenericInterface_0');
export var /*1*/x: a<a<string>>;
x.a;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "var x: a<a<string>>", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoExportAssignmentOfGenericInterface, TestQuickInfoExportAssignmentOfGenericInterface);

// quickInfoExtendArray_test.go

// quickInfoExtendArray_test.go
static void TestQuickInfoExtendArray(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Foo<T> extends Array<T> { }
var x: Foo<string>;
var /*1*/r = x[0];
interface Foo2 extends Array<string> { }
var x2: Foo2;
var /*2*/r2 = x2[0];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "var r: string", "");
		f->VerifyQuickInfoAt(t, "2", "var r2: string", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoExtendArray, TestQuickInfoExtendArray);

// quickInfoForAliasedGeneric_test.go

// quickInfoForAliasedGeneric_test.go
static void TestQuickInfoForAliasedGeneric(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace M {
    export namespace N {
        export class C<T> { }
        export class D { }
    }
}
import d = M.N;
var /*1*/aa: d.C<number>;
var /*2*/bb: d.D;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "var aa: d.C<number>", "");
		f->VerifyQuickInfoAt(t, "2", "var bb: d.D", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForAliasedGeneric, TestQuickInfoForAliasedGeneric);

// quickInfoForArgumentsPropertyNameInJsMode1_test.go

// quickInfoForArgumentsPropertyNameInJsMode1_test.go
static void TestQuickInfoForArgumentsPropertyNameInJsMode1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @filename: a.js
const foo = {
    f1: (params) => { }
}

function /*1*/f2(x) {
   foo.f1({ x, arguments: [] });
}

/*2*/f2('');)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForArgumentsPropertyNameInJsMode1, TestQuickInfoForArgumentsPropertyNameInJsMode1);

// quickInfoForArgumentsPropertyNameInJsMode2_test.go

// quickInfoForArgumentsPropertyNameInJsMode2_test.go
static void TestQuickInfoForArgumentsPropertyNameInJsMode2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @filename: a.js
function /*1*/f(x) {
   arguments;
}

/*2*/f('');)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForArgumentsPropertyNameInJsMode2, TestQuickInfoForArgumentsPropertyNameInJsMode2);

// quickInfoForConstAssertions_test.go

// quickInfoForConstAssertions_test.go
static void TestQuickInfoForConstAssertions(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(const a = { a: 1 } as /*1*/const;
const b = 1 as /*2*/const;
const c = "c" as /*3*/const;
const d = [1, 2] as /*4*/const;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForConstAssertions, TestQuickInfoForConstAssertions);

// quickInfoForContextuallyTypedArrowFunctionInSuperCall_test.go

// quickInfoForContextuallyTypedArrowFunctionInSuperCall_test.go
static void TestQuickInfoForContextuallyTypedArrowFunctionInSuperCall(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(class A<T1, T2> {
    constructor(private map: (value: T1) => T2) {

    }
}

class B extends A<number, string> {
    constructor() { super(va/*1*/lue => String(va/*2*/lue.toExpone/*3*/ntial())); }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(parameter) value: number", "");
		f->VerifyQuickInfoAt(t, "2", "(parameter) value: number", "");
		f->VerifyQuickInfoAt(t, "3", "(method) Number.toExponential(fractionDigits?: number): string", "Returns a string containing a number represented in exponential notation.");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForContextuallyTypedArrowFunctionInSuperCall, TestQuickInfoForContextuallyTypedArrowFunctionInSuperCall);

// quickInfoForContextuallyTypedFunctionInReturnStatement_test.go

// quickInfoForContextuallyTypedFunctionInReturnStatement_test.go
static void TestQuickInfoForContextuallyTypedFunctionInReturnStatement(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Accumulator {
    clear(): void;
    add(x: number): void;
    result(): number;
}

function makeAccumulator(): Accumulator {
    var sum = 0;
    return {
        clear: function () { sum = 0; },
        add: function (val/**/ue) { sum += value; },
        result: function () { return sum; }
    };
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "(parameter) value: number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForContextuallyTypedFunctionInReturnStatement, TestQuickInfoForContextuallyTypedFunctionInReturnStatement);

// quickInfoForContextuallyTypedIife_test.go

// quickInfoForContextuallyTypedIife_test.go
static void TestQuickInfoForContextuallyTypedIife(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS((({ q/*1*/, qq/*2*/ }, x/*3*/, { p/*4*/ }) => {
    var s: number = q/*5*/;
    var t: number = qq/*6*/;
    var u: number = p/*7*/;
    var v: number = x/*8*/;
    return q; })({ q: 13, qq: 12 }, 1, { p: 14 });
((a/*9*/, b/*10*/, c/*11*/) => [a/*12*/,b/*13*/,c/*14*/])("foo", 101, false);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(parameter) q: number", "");
		f->VerifyQuickInfoAt(t, "2", "(parameter) qq: number", "");
		f->VerifyQuickInfoAt(t, "3", "(parameter) x: number", "");
		f->VerifyQuickInfoAt(t, "4", "(parameter) p: number", "");
		f->VerifyQuickInfoAt(t, "5", "(parameter) q: number", "");
		f->VerifyQuickInfoAt(t, "6", "(parameter) qq: number", "");
		f->VerifyQuickInfoAt(t, "7", "(parameter) p: number", "");
		f->VerifyQuickInfoAt(t, "8", "(parameter) x: number", "");
		f->VerifyQuickInfoAt(t, "9", "(parameter) a: string", "");
		f->VerifyQuickInfoAt(t, "10", "(parameter) b: number", "");
		f->VerifyQuickInfoAt(t, "11", "(parameter) c: boolean", "");
		f->VerifyQuickInfoAt(t, "12", "(parameter) a: string", "");
		f->VerifyQuickInfoAt(t, "13", "(parameter) b: number", "");
		f->VerifyQuickInfoAt(t, "14", "(parameter) c: boolean", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForContextuallyTypedIife, TestQuickInfoForContextuallyTypedIife);

// quickInfoForContextuallyTypedParameters_test.go

// quickInfoForContextuallyTypedParameters_test.go
static void TestQuickInfoForContextuallyTypedParameters(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare function foo1<T>(obj: T, settings: (row: T) => { value: string, func?: Function }): void;

foo1(new Error(),
    o/*1*/ => ({
        value: o.name,
        func: x => 'foo'
    })
);

declare function foo2<T>(settings: (row: T) => { value: string, func?: Function }, obj: T): void;

foo2(o/*2*/ => ({
        value: o.name,
        func: x => 'foo'
    }),
    new Error(),
);

declare function foof<T extends { name: string }, U extends keyof T>(settings: (row: T) => { value: T[U], func?: Function }, obj: T, key: U): U;

function q<T extends { name: string }>(x: T): T["name"] {
    return foof/*3*/(o => ({ value: o.name, func: x => 'foo' }), x, "name");
}

foof/*4*/(o => ({ value: o.name, func: x => 'foo' }), new Error(), "name");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(parameter) o: Error", "");
		f->VerifyQuickInfoAt(t, "2", "(parameter) o: Error", "");
		f->VerifyQuickInfoAt(t, "3", R"TS(function foof<T, "name">(settings: (row: T) => {
    value: T["name"];
    func?: Function;
}, obj: T, key: "name"): "name")TS", "");
		f->VerifyQuickInfoAt(t, "4", R"TS(function foof<Error, "name">(settings: (row: Error) => {
    value: string;
    func?: Function;
}, obj: Error, key: "name"): "name")TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForContextuallyTypedParameters, TestQuickInfoForContextuallyTypedParameters);

// quickInfoForDecorators_test.go

// quickInfoForDecorators_test.go
static void TestQuickInfoForDecorators(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(@/*1*/decorator
class C {
}
/** decorator documentation*/
var decorator = t=> t;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "var decorator: (t: any) => any", "decorator documentation");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForDecorators, TestQuickInfoForDecorators);

// quickInfoForDerivedGenericTypeWithConstructor_test.go

// quickInfoForDerivedGenericTypeWithConstructor_test.go
static void TestQuickInfoForDerivedGenericTypeWithConstructor(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class A<T> {
    foo() { }
}
class B<T> extends A<T> {
    bar() { }
    constructor() { super() }
}
class B2<T> extends A<T> {
    bar() { }
}
var /*1*/b: B<number>;
var /*2*/b2: B<number>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "var b: B<number>", "");
		f->VerifyQuickInfoAt(t, "2", "var b2: B<number>", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForDerivedGenericTypeWithConstructor, TestQuickInfoForDerivedGenericTypeWithConstructor);

// quickInfoForDestructuringShorthandInitializer_test.go

// quickInfoForDestructuringShorthandInitializer_test.go
static void TestQuickInfoForDestructuringShorthandInitializer(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(let a = '';
let b: string;
({b = /**/a} = {b: 'b'});)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "let a: string", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForDestructuringShorthandInitializer, TestQuickInfoForDestructuringShorthandInitializer);

// quickInfoForFunctionDeclaration_test.go

// quickInfoForFunctionDeclaration_test.go
static void TestQuickInfoForFunctionDeclaration(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface A<T> { }

function ma/*makeA*/keA<T>(t: T): A<T> { return null; }

function /*f*/f<T>(t: T) {
    return makeA(t);
}

var x = f(0);
var y = makeA(0);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "makeA", "function makeA<T>(t: T): A<T>", "");
		f->VerifyQuickInfoAt(t, "f", "function f<T>(t: T): A<T>", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForFunctionDeclaration, TestQuickInfoForFunctionDeclaration);

// quickInfoForGenericConstraints1_test.go

// quickInfoForGenericConstraints1_test.go
static void TestQuickInfoForGenericConstraints1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(function foo4<T extends Date>(te/**/st: T): T;
function foo4<T extends Date>(test: any): any { return null; })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "(parameter) test: T extends Date", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForGenericConstraints1, TestQuickInfoForGenericConstraints1);

// quickInfoForGenericPrototypeMember_test.go

// quickInfoForGenericPrototypeMember_test.go
static void TestQuickInfoForGenericPrototypeMember(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C<T> {
   foo(x: T) { }
}
var x = new /*1*/C<any>();
var y = C.proto/*2*/type;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "constructor C<any>(): C<any>", "");
		f->VerifyQuickInfoAt(t, "2", "(property) C<T>.prototype: C<any>", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForGenericPrototypeMember, TestQuickInfoForGenericPrototypeMember);

// quickInfoForGenericTaggedTemplateExpression_test.go

// quickInfoForGenericTaggedTemplateExpression_test.go
static void TestQuickInfoForGenericTaggedTemplateExpression(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const auto content = ((((((((((((((((((((((((((((std::string(R"TS(interface T1 {}
class T2 {}
type T3 = "a" | "b";

declare function foo<T>(strings: TemplateStringsArray, ...values: T[]): void;

/*1*/foo<number>)TS") + "`") + std::string(R"TS()TS")) + std::string("`")) + std::string(R"TS(;
/*2*/foo<string | number>)TS")) + std::string("`")) + std::string(R"TS()TS")) + std::string("`")) + std::string(R"TS(;
/*3*/foo<{ a: number }>)TS")) + std::string("`")) + std::string(R"TS()TS")) + std::string("`")) + std::string(R"TS(;
/*4*/foo<T1>)TS")) + std::string("`")) + std::string(R"TS()TS")) + std::string("`")) + std::string(R"TS(;
/*5*/foo<T2>)TS")) + std::string("`")) + std::string(R"TS()TS")) + std::string("`")) + std::string(R"TS(;
/*6*/foo<T3>)TS")) + std::string("`")) + std::string(R"TS()TS")) + std::string("`")) + std::string(R"TS(;
/*7*/foo)TS")) + std::string("`")) + std::string(R"TS()TS")) + std::string("`")) + std::string(R"TS(;)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "function foo<number>(strings: TemplateStringsArray, ...values: number[]): void", "");
		f->VerifyQuickInfoAt(t, "2", "function foo<string | number>(strings: TemplateStringsArray, ...values: (string | number)[]): void", "");
		f->VerifyQuickInfoAt(t, "3", R"TS(function foo<{
    a: number;
}>(strings: TemplateStringsArray, ...values: {
    a: number;
}[]): void)TS", "");
		f->VerifyQuickInfoAt(t, "4", "function foo<T1>(strings: TemplateStringsArray, ...values: T1[]): void", "");
		f->VerifyQuickInfoAt(t, "5", "function foo<T2>(strings: TemplateStringsArray, ...values: T2[]): void", "");
		f->VerifyQuickInfoAt(t, "6", "function foo<T3>(strings: TemplateStringsArray, ...values: T3[]): void", "");
		f->VerifyQuickInfoAt(t, "7", "function foo<unknown>(strings: TemplateStringsArray, ...values: unknown[]): void", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForGenericTaggedTemplateExpression, TestQuickInfoForGenericTaggedTemplateExpression);

// quickInfoForGetterAndSetter_test.go

// quickInfoForGetterAndSetter_test.go
static void TestQuickInfoForGetterAndSetter(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(class Test {
    constructor() {
        this.value;
    }

    /** Getter text */
    get val/*1*/ue() {
        return this.value;
    }

    /** Setter text */
    set val/*2*/ue(value) {
        this.value = value;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifyQuickInfoIs(t, "(getter) Test.value: any", "Getter text");
		f->GoToMarker(t, "2");
		f->VerifyQuickInfoIs(t, "(setter) Test.value: any", "Setter text");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForGetterAndSetter, TestQuickInfoForGetterAndSetter);

// quickInfoForIn_test.go

// quickInfoForIn_test.go
static void TestQuickInfoForIn(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var obj;
for (var /**/p in obj) { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "var p: string", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForIn, TestQuickInfoForIn);

// quickInfoForIndexerResultWithConstraint_test.go

// quickInfoForIndexerResultWithConstraint_test.go
static void TestQuickInfoForIndexerResultWithConstraint(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(function foo<T>(x: T) {
        return x;
}
function other2<T extends Date>(arg: T) {
    var b: { [x: string]: T };
    var /*1*/r2 = foo(b); // just shows T
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", R"TS((local var) r2: {
    [x: string]: T;
})TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForIndexerResultWithConstraint, TestQuickInfoForIndexerResultWithConstraint);

// quickInfoForJSDocCodefence_test.go

// quickInfoForJSDocCodefence_test.go
static void TestQuickInfoForJSDocCodefence(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((((((((((((((((std::string(R"TS(/**
 * @example
 * )TS") + "`") + std::string(R"TS()TS")) + std::string("`")) + std::string(R"TS()TS")) + std::string("`")) + std::string(R"TS(
 * 1 + 2
 * )TS")) + std::string("`")) + std::string(R"TS()TS")) + std::string("`")) + std::string(R"TS()TS")) + std::string("`")) + std::string(R"TS(
 */
function fo/*1*/o() {
    return '2';
}
/**
 * @example
 * )TS")) + std::string("`")) + std::string(R"TS()TS")) + std::string("`")) + std::string(R"TS(
 * 1 + 2
 * )TS")) + std::string("`")) + std::string(R"TS(
 */
function bo/*2*/o() {
    return '2';
})TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForJSDocCodefence, TestQuickInfoForJSDocCodefence);

// quickInfoForJSDocUnknownTag_test.go

// quickInfoForJSDocUnknownTag_test.go
static void TestQuickInfoForJSDocUnknownTag(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/**
 * @example
 * if (true) {
 *     foo()
 * }
 */
function fo/*1*/o() {
    return '2';
}
/**
 @example
 {
     foo()
 }
 */
function fo/*2*/o2() {
    return '2';
}
/**
 * @example
 *   x y
 *   12345
 *      b
 */
function m/*3*/oo() {
    return '2';
}
/**
 * @func
 * @example
 *   x y
 *   12345
 *      b
 */
function b/*4*/oo() {
    return '2';
}
/**
 * @func
 * @example    x y
 *             12345
 *                b
 */
function go/*5*/o() {
    return '2';
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForJSDocUnknownTag, TestQuickInfoForJSDocUnknownTag);

// quickInfoForJSDocWithHttpLinks_test.go

// quickInfoForJSDocWithHttpLinks_test.go
static void TestQuickInfoForJSDocWithHttpLinks(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @checkJs: true
// @filename: quickInfoForJSDocWithHttpLinks.js
/** @typedef {number} /*1*/https://wat */

/**
* @typedef {Object} Oops
* @property {number} /*2*/https://wass
*/


/** @callback /*3*/http://vad */

/** @see https://hvad */
var /*4*/see1 = true

/** @see {@link https://hva} */
var /*5*/see2 = true

/** {@link https://hvaD} */
var /*6*/see3 = true)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForJSDocWithHttpLinks, TestQuickInfoForJSDocWithHttpLinks);

// quickInfoForJSDocWithUnresolvedHttpLinks_test.go

// quickInfoForJSDocWithUnresolvedHttpLinks_test.go
static void TestQuickInfoForJSDocWithUnresolvedHttpLinks(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @checkJs: true
// @filename: quickInfoForJSDocWithHttpLinks.js
/** @see {@link https://hva} */
var /*5*/see2 = true

/** {@link https://hvaD} */
var /*6*/see3 = true)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForJSDocWithUnresolvedHttpLinks, TestQuickInfoForJSDocWithUnresolvedHttpLinks);

// quickInfoForObjectBindingElementName01_test.go

// quickInfoForObjectBindingElementName01_test.go
static void TestQuickInfoForObjectBindingElementName01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    property1: number;
    property2: string;
}

var foo: I;
var { /**/property1 } = foo;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyQuickInfoAt(t, "", "var property1: number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForObjectBindingElementName01, TestQuickInfoForObjectBindingElementName01);

// quickInfoForObjectBindingElementName02_test.go

// quickInfoForObjectBindingElementName02_test.go
static void TestQuickInfoForObjectBindingElementName02(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    property1: number;
    property2: string;
}

var foo: I;
var { property1: /**/prop1 } = foo;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "var prop1: number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForObjectBindingElementName02, TestQuickInfoForObjectBindingElementName02);

// quickInfoForObjectBindingElementName03_test.go

// quickInfoForObjectBindingElementName03_test.go
static void TestQuickInfoForObjectBindingElementName03(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Options {
    /**
     * A description of foo
     */
    foo: string;
}

function f({ foo }: Options) {
    foo/*1*/;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForObjectBindingElementName03, TestQuickInfoForObjectBindingElementName03);

// quickInfoForObjectBindingElementName04_test.go

// quickInfoForObjectBindingElementName04_test.go
static void TestQuickInfoForObjectBindingElementName04(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Options {
   /**
    * A description of 'a'
    */
    a: {
       /**
        * A description of 'b'
        */
       b: string;
   }
}

function f({ a, a: { b } }: Options) {
    a/*1*/;
    b/*2*/;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForObjectBindingElementName04, TestQuickInfoForObjectBindingElementName04);

// quickInfoForObjectBindingElementName05_test.go

// quickInfoForObjectBindingElementName05_test.go
static void TestQuickInfoForObjectBindingElementName05(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface A {
    /**
     * A description of a
     */
    a: number;
}
interface B {
    a: string;
}

function f({ a }: A | B) {
    a/**/;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForObjectBindingElementName05, TestQuickInfoForObjectBindingElementName05);

// quickInfoForObjectBindingElementName06_test.go

// quickInfoForObjectBindingElementName06_test.go
static void TestQuickInfoForObjectBindingElementName06(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type Foo = {
    /**
     * Thing is a bar
     */
    isBar: boolean

    /**
     * Thing is a baz
     */
    isBaz: boolean
}

function f(): Foo {
    return undefined as any
}

const { isBaz: isBar } = f();
isBar/**/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForObjectBindingElementName06, TestQuickInfoForObjectBindingElementName06);

// quickInfoForObjectBindingElementPropertyName01_test.go

// quickInfoForObjectBindingElementPropertyName01_test.go
static void TestQuickInfoForObjectBindingElementPropertyName01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    property1: number;
    property2: string;
}

var foo: I;
var { /**/property1: prop1 } = foo;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "(property) I.property1: number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForObjectBindingElementPropertyName01, TestQuickInfoForObjectBindingElementPropertyName01);

// quickInfoForObjectBindingElementPropertyName02_test.go

// quickInfoForObjectBindingElementPropertyName02_test.go
static void TestQuickInfoForObjectBindingElementPropertyName02(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    property1: number;
    property2: string;
}

var foo: I;
var { /**/property1: {} } = foo;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "(property) I.property1: number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForObjectBindingElementPropertyName02, TestQuickInfoForObjectBindingElementPropertyName02);

// quickInfoForObjectBindingElementPropertyName04_test.go

// quickInfoForObjectBindingElementPropertyName04_test.go
static void TestQuickInfoForObjectBindingElementPropertyName04(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Recursive {
    next?: Recursive;
    value: any;
}

function f ({ /*1*/next: { /*2*/next: x} }) {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", R"TS((property) next: {
    next: any;
})TS", "");
		f->VerifyQuickInfoAt(t, "2", "(property) next: any", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForObjectBindingElementPropertyName04, TestQuickInfoForObjectBindingElementPropertyName04);

// quickInfoForOverloadOnConst1_test.go

// quickInfoForOverloadOnConst1_test.go
static void TestQuickInfoForOverloadOnConst1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    x/*1*/1(a: number, callback: (x: 'hi') => number);
}
class C {
    x/*2*/1(a: number, call/*3*/back: (x: 'hi') => number);
    x/*4*/1(a: number, call/*5*/back: (x: string) => number) {
        call/*6*/back('hi');
        callback('bye');
        var hm = "hm";
        callback(hm);
    }
}
var c: C;
c.x/*7*/1(1, (x/*8*/x: 'hi') => { return 1; } );
c.x1(1, (x/*9*/x: 'bye') => { return 1; } );
c.x1(1, (x/*10*/x) => { return 1; } );)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(method) I.x1(a: number, callback: (x: 'hi') => number): any", "");
		f->VerifyQuickInfoAt(t, "2", "(method) C.x1(a: number, callback: (x: 'hi') => number): any", "");
		f->VerifyQuickInfoAt(t, "3", "(parameter) callback: (x: 'hi') => number", "");
		f->VerifyQuickInfoAt(t, "4", "(method) C.x1(a: number, callback: (x: string) => number): void", "");
		f->VerifyQuickInfoAt(t, "5", "(parameter) callback: (x: string) => number", "");
		f->VerifyQuickInfoAt(t, "6", "(parameter) callback: (x: string) => number", "");
		f->VerifyQuickInfoAt(t, "7", "(method) C.x1(a: number, callback: (x: 'hi') => number): any", "");
		f->VerifyQuickInfoAt(t, "8", R"TS((parameter) xx: "hi")TS", "");
		f->VerifyQuickInfoAt(t, "9", R"TS((parameter) xx: "bye")TS", "");
		f->VerifyQuickInfoAt(t, "10", R"TS((parameter) xx: "hi")TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForOverloadOnConst1, TestQuickInfoForOverloadOnConst1);

// quickInfoForRequire_test.go

// quickInfoForRequire_test.go
static void TestQuickInfoForRequire(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((std::string(R"TS(//@Filename: AA/BB.ts
export class a{}
//@Filename: quickInfoForRequire_input.ts
import a = require("./AA/B/*1*/B");
import b = require()TS") + "`") + std::string(R"TS(./AA/B/*2*/B)TS")) + std::string("`")) + std::string(R"TS();)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifyQuickInfoIs(t, "module a", "");
		f->GoToMarker(t, "2");
		f->VerifyQuickInfoIs(t, "module a", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForRequire, TestQuickInfoForRequire);

// quickInfoForShorthandProperty_test.go

// quickInfoForShorthandProperty_test.go
static void TestQuickInfoForShorthandProperty(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: false
var name1 = undefined, id1 = undefined;
var /*obj1*/obj1 = {/*name1*/name1, /*id1*/id1};
var name2 = "Hello";
var id2 = 10000;
var /*obj2*/obj2 = {/*name2*/name2, /*id2*/id2};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "obj1", R"TS(var obj1: {
    name1: any;
    id1: any;
})TS", "");
		f->VerifyQuickInfoAt(t, "name1", "(property) name1: any", "");
		f->VerifyQuickInfoAt(t, "id1", "(property) id1: any", "");
		f->VerifyQuickInfoAt(t, "obj2", R"TS(var obj2: {
    name2: string;
    id2: number;
})TS", "");
		f->VerifyQuickInfoAt(t, "name2", "(property) name2: string", "");
		f->VerifyQuickInfoAt(t, "id2", "(property) id2: number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForShorthandProperty, TestQuickInfoForShorthandProperty);

// quickInfoForSyntaxErrorNoError_test.go

// quickInfoForSyntaxErrorNoError_test.go
static void TestQuickInfoForSyntaxErrorNoError(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace X {
    export =
}
X.add/*1*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "any", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForSyntaxErrorNoError, TestQuickInfoForSyntaxErrorNoError);

// quickInfoForTypeParameterInTypeAlias1_test.go

// quickInfoForTypeParameterInTypeAlias1_test.go
static void TestQuickInfoForTypeParameterInTypeAlias1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type Ctor<AA> = new () => A/*1*/A;
type MixinCtor<AA> = new () => AA & { constructor: MixinCtor<A/*2*/A> };
type NestedCtor<AA> = new() => AA & (new () => AA & { constructor: NestedCtor<A/*3*/A> });
type Method<AA> = { method(): A/*4*/A };
type Construct<AA> = { new(): A/*5*/A };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(type parameter) AA in type Ctor<AA>", "");
		f->VerifyQuickInfoAt(t, "2", "(type parameter) AA in type MixinCtor<AA>", "");
		f->VerifyQuickInfoAt(t, "3", "(type parameter) AA in type NestedCtor<AA>", "");
		f->VerifyQuickInfoAt(t, "4", "(type parameter) AA in type Method<AA>", "");
		f->VerifyQuickInfoAt(t, "5", "(type parameter) AA in type Construct<AA>", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForTypeParameterInTypeAlias1, TestQuickInfoForTypeParameterInTypeAlias1);

// quickInfoForTypeParameterInTypeAlias2_test.go

// quickInfoForTypeParameterInTypeAlias2_test.go
static void TestQuickInfoForTypeParameterInTypeAlias2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(type Call<AA> = { (): A/*1*/A };
type Index<AA> = {[foo: string]: A/*2*/A};
type GenericMethod<AA> = { method<BB>(): A/*3*/A & B/*4*/B }
type Nesting<TT> = { method<UU>(): new <WW>() => T/*5*/T & U/*6*/U & W/*7*/W };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(type parameter) AA in type Call<AA>", "");
		f->VerifyQuickInfoAt(t, "2", "(type parameter) AA in type Index<AA>", "");
		f->VerifyQuickInfoAt(t, "3", "(type parameter) AA in type GenericMethod<AA>", "");
		f->VerifyQuickInfoAt(t, "4", "(type parameter) BB in method<BB>(): AA & BB", "");
		f->VerifyQuickInfoAt(t, "5", "(type parameter) TT in type Nesting<TT>", "");
		f->VerifyQuickInfoAt(t, "6", "(type parameter) UU in method<UU>(): new <WW>() => TT & UU & WW", "");
		f->VerifyQuickInfoAt(t, "7", "(type parameter) WW in <WW>(): TT & UU & WW", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForTypeParameterInTypeAlias2, TestQuickInfoForTypeParameterInTypeAlias2);

// quickInfoForTypeofParameter_test.go

// quickInfoForTypeofParameter_test.go
static void TestQuickInfoForTypeofParameter(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(function foo() {
    var y/*ref1*/1: string;
    var x: typeof y/*ref2*/1;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "ref1", "(local var) y1: string", "");
		f->VerifyQuickInfoAt(t, "ref2", "(local var) y1: string", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForTypeofParameter, TestQuickInfoForTypeofParameter);

// quickInfoForUMDModuleAlias_test.go

// quickInfoForUMDModuleAlias_test.go
static void TestQuickInfoForUMDModuleAlias(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: 0.d.ts
export function doThing(): string;
export function doTheOtherThing(): void;
export as namespace /*0*/myLib;
// @Filename: 1.ts
/// <reference path="0.d.ts" />
/*1*/myLib.doThing();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "0", "export namespace myLib", "");
		f->VerifyQuickInfoAt(t, "1", "export namespace myLib", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoForUMDModuleAlias, TestQuickInfoForUMDModuleAlias);

// quickInfoFromContextualType_test.go

// quickInfoFromContextualType_test.go
static void TestQuickInfoFromContextualType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: quickInfoExportAssignmentOfGenericInterface_0.ts
interface I {
    /** Documentation */
    x: number;
}
const i: I = { /**/x: 0 };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "(property) I.x: number", "Documentation");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoFromContextualType, TestQuickInfoFromContextualType);

// quickInfoFromContextualUnionType1_test.go

// quickInfoFromContextualUnionType1_test.go
static void TestQuickInfoFromContextualUnionType1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// based on https://github.com/microsoft/TypeScript/issues/55495
type X =
  | {
      name: string;
      [key: string]: any;
    }
  | {
      name: "john";
      someProp: boolean;
    };

const obj = { name: "john", /*1*/someProp: "foo" } satisfies X;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(property) someProp: string", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoFromContextualUnionType1, TestQuickInfoFromContextualUnionType1);

// quickInfoFromContextualUnionType2_test.go

// quickInfoFromContextualUnionType2_test.go
static void TestQuickInfoFromContextualUnionType2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @strict: true
function test1(arg: { prop: "foo" }) {}
test1({ /*1*/prop: "bar" });

function test2(arg: { prop: "foo" } | undefined) {}
test2({ /*2*/prop: "bar" });)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", R"TS((property) prop: "foo")TS", "");
		f->VerifyQuickInfoAt(t, "2", R"TS((property) prop: "foo")TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoFromContextualUnionType2, TestQuickInfoFromContextualUnionType2);

// quickInfoFromContextualUnionType3_test.go

// quickInfoFromContextualUnionType3_test.go
static void TestQuickInfoFromContextualUnionType3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @strict: true
declare const foo1: <D extends Foo1<D>>(definition: D) => D;

type Foo1<D, Bar = Prop<D, "bar">> = {
  bar: {
    [K in keyof Bar]: Bar[K] extends boolean
      ? Bar[K]
      : "Error: bar should be boolean";
  };
};

declare const foo2: <D extends Foo2<D>>(definition: D) => D;

type Foo2<D, Bar = Prop<D, "bar">> = {
  bar?: {
    [K in keyof Bar]: Bar[K] extends boolean
      ? Bar[K]
      : "Error: bar should be boolean";
  };
};

type Prop<T, K> = K extends keyof T ? T[K] : never;

foo1({ bar: { /*1*/X: "test" } });

foo2({ bar: { /*2*/X: "test" } });)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", R"TS((property) X: "Error: bar should be boolean")TS", "");
		f->VerifyQuickInfoAt(t, "2", R"TS((property) X: "Error: bar should be boolean")TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoFromContextualUnionType3, TestQuickInfoFromContextualUnionType3);

// quickInfoFromEmptyBlockComment_test.go

// quickInfoFromEmptyBlockComment_test.go
static void TestQuickInfoFromEmptyBlockComment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/**/
class Foo {
}
var f/*A*/ff = new Foo();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "A", "var fff: Foo", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoFromEmptyBlockComment, TestQuickInfoFromEmptyBlockComment);

// quickInfoFunctionKeyword_test.go

// quickInfoFunctionKeyword_test.go
static void TestQuickInfoFunctionKeyword(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS([1].forEach(fu/*1*/nction() {});
[1].map(x =/*2*/> x + 1);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(local function)(): void", "");
		f->VerifyQuickInfoAt(t, "2", "function(x: number): number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoFunctionKeyword, TestQuickInfoFunctionKeyword);

// quickInfoFunction_test.go

// quickInfoFunction_test.go
static void TestQuickInfoFunction(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/**/function foo() { return "hi"; })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "function foo(): string", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoFunction, TestQuickInfoFunction);

// quickInfoGenericCombinators2_test.go

// quickInfoGenericCombinators2_test.go
static void TestQuickInfoGenericCombinators2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Collection<T, U> {
   length: number;
   add(x: T, y: U): void ;
   remove(x: T, y: U): boolean;
}

interface Combinators {
   map<T, U, V>(c: Collection<T, U>, f: (x: T, y: U) => V): Collection<T, V>;
   map<T, U>(c: Collection<T, U>, f: (x: T, y: U) => any): Collection<any, any>;
}

class A {
   foo<T>(): T { return null; }
}

class B<T> {
   foo(x: T): T { return null; }
}

var c1: Collection<any, any>;
var c2: Collection<number, string>;
var c3: Collection<Collection<number, number>, string>;
var c4: Collection<number, A>;
var c5: Collection<number, B<any>>;

var _: Combinators;
// param help on open paren for arg 2 should show 'number' not T or 'any'
// x should be contextually typed to number
var rf1 = (x: number, y: string) => { return x.toFixed() };
var rf2 = (x: Collection<number, number>, y: string) => { return x.length };
var rf3 = (x: number, y: A) => { return y.foo() };

var /*9*/r1a  = _.map/*1c*/(c2, (/*1a*/x, /*1b*/y) => { return x.toFixed() });
var /*10*/r1b = _.map(c2, rf1);

var /*11*/r2a = _.map(c3, (/*2a*/x, /*2b*/y) => { return x.length });
var /*12*/r2b = _.map(c3, rf2);

var /*13*/r3a = _.map(c4, (/*3a*/x, /*3b*/y) => { return y.foo() });
var /*14*/r3b = _.map(c4, rf3);

var /*15*/r4a = _.map(c5, (/*4a*/x, /*4b*/y) => { return y.foo() });

var /*17*/r5a = _.map<number, string, Date>(c2, /*17error1*/(/*5a*/x, /*5b*/y) => { return x.toFixed() }/*17error2*/); 
var rf1b = (x: number, y: string) => { return new Date() };
var /*18*/r5b = _.map<number, string, Date>(c2, rf1b);

var /*19*/r6a = _.map<Collection<number, number>, string, Date>(c3, (/*6a*/x,/*6b*/y) => { return new Date(); });
var rf2b = (x: Collection<number, number>, y: string) => { return new Date(); };
var /*20*/r6b = _.map<Collection<number, number>, string, Date>(c3, rf2b);

var /*21*/r7a = _.map<number, A, string>(c4, (/*7a*/x,/*7b*/y) => { return y.foo() });
var /*22*/r7b = _.map<number, A, string>(c4, /*22error1*/rf3/*22error2*/);

var /*23*/r8a = _.map<number, /*error1*/B/*error2*/, string>(c5, (/*8a*/x,/*8b*/y) => { return y.foo() }); )TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "2a", "(parameter) x: Collection<number, number>", "");
		f->VerifyQuickInfoAt(t, "2b", "(parameter) y: string", "");
		f->VerifyQuickInfoAt(t, "3a", "(parameter) x: number", "");
		f->VerifyQuickInfoAt(t, "3b", "(parameter) y: A", "");
		f->VerifyQuickInfoAt(t, "4a", "(parameter) x: number", "");
		f->VerifyQuickInfoAt(t, "4b", "(parameter) y: B<any>", "");
		f->VerifyQuickInfoAt(t, "5a", "(parameter) x: number", "");
		f->VerifyQuickInfoAt(t, "5b", "(parameter) y: string", "");
		f->VerifyQuickInfoAt(t, "6a", "(parameter) x: Collection<number, number>", "");
		f->VerifyQuickInfoAt(t, "6b", "(parameter) y: string", "");
		f->VerifyQuickInfoAt(t, "7a", "(parameter) x: number", "");
		f->VerifyQuickInfoAt(t, "7b", "(parameter) y: A", "");
		f->VerifyQuickInfoAt(t, "8a", "(parameter) x: number", "");
		f->VerifyQuickInfoAt(t, "8b", "(parameter) y: any", "");
		f->VerifyQuickInfoAt(t, "9", "var r1a: Collection<number, string>", "");
		f->VerifyQuickInfoAt(t, "10", "var r1b: Collection<number, string>", "");
		f->VerifyQuickInfoAt(t, "11", "var r2a: Collection<Collection<number, number>, number>", "");
		f->VerifyQuickInfoAt(t, "12", "var r2b: Collection<Collection<number, number>, number>", "");
		f->VerifyQuickInfoAt(t, "13", "var r3a: Collection<number, unknown>", "");
		f->VerifyQuickInfoAt(t, "14", "var r3b: Collection<number, unknown>", "");
		f->VerifyQuickInfoAt(t, "15", "var r4a: Collection<number, any>", "");
		f->VerifyQuickInfoAt(t, "17", "var r5a: Collection<number, Date>", "");
		f->VerifyQuickInfoAt(t, "18", "var r5b: Collection<number, Date>", "");
		f->VerifyQuickInfoAt(t, "19", "var r6a: Collection<Collection<number, number>, Date>", "");
		f->VerifyQuickInfoAt(t, "20", "var r6b: Collection<Collection<number, number>, Date>", "");
		f->VerifyQuickInfoAt(t, "21", "var r7a: Collection<number, string>", "");
		f->VerifyQuickInfoAt(t, "22", "var r7b: Collection<number, string>", "");
		f->VerifyQuickInfoAt(t, "23", "var r8a: Collection<number, string>", "");
		f->VerifyErrorExistsBetweenMarkers(t, "error1", "error2");
		f->VerifyErrorExistsBetweenMarkers(t, "17error1", "17error2");
		f->VerifyErrorExistsBetweenMarkers(t, "22error1", "22error2");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoGenericCombinators2, TestQuickInfoGenericCombinators2);

// quickInfoGenericPropertyAccessor_test.go

// quickInfoGenericPropertyAccessor_test.go
static void TestQuickInfoGenericPropertyAccessor(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
declare const o: {
    f: <T>(x: T) => T
    get g(): <T>(x: T) => T
}

declare const x: number

o.f/*1*/(x)
o.g/*2*/(x)
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(property) f: <number>(x: number) => number", "");
		f->VerifyQuickInfoAt(t, "2", "(accessor) g: <number>(x: number) => number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoGenericPropertyAccessor, TestQuickInfoGenericPropertyAccessor);

// quickInfoGenericTypeArgumentInference1_test.go

// quickInfoGenericTypeArgumentInference1_test.go
static void TestQuickInfoGenericTypeArgumentInference1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: false
namespace Underscore {
    export interface Iterator<T, U> {
        (value: T, index: any, list: any): U;
    }

    export interface Static {
        all<T>(list: T[], iterator?: Iterator<T, boolean>, context?: any): T;
        identity<T>(value: T): T;
    }
}

declare var _: Underscore.Static;
var /*1*/r = _./*11*/all([true, 1, null, 'yes'], x => !x);
var /*2*/r2 = _./*21*/all([true], _.identity);
var /*3*/r3 = _./*31*/all([], _.identity);
var /*4*/r4 = _./*41*/all([<any>true], _.identity);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "var r: string | number | boolean", "");
		f->VerifyQuickInfoAt(t, "11", "(method) Underscore.Static.all<string | number | boolean>(list: (string | number | boolean)[], iterator?: Underscore.Iterator<string | number | boolean, boolean>, context?: any): string | number | boolean", "");
		f->VerifyQuickInfoAt(t, "2", "var r2: boolean", "");
		f->VerifyQuickInfoAt(t, "21", "(method) Underscore.Static.all<boolean>(list: boolean[], iterator?: Underscore.Iterator<boolean, boolean>, context?: any): boolean", "");
		f->VerifyQuickInfoAt(t, "3", "var r3: any", "");
		f->VerifyQuickInfoAt(t, "31", "(method) Underscore.Static.all<any>(list: any[], iterator?: Underscore.Iterator<any, boolean>, context?: any): any", "");
		f->VerifyQuickInfoAt(t, "4", "var r4: any", "");
		f->VerifyQuickInfoAt(t, "41", "(method) Underscore.Static.all<any>(list: any[], iterator?: Underscore.Iterator<any, boolean>, context?: any): any", "");
		f->VerifyNoErrors(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoGenericTypeArgumentInference1, TestQuickInfoGenericTypeArgumentInference1);

// quickInfoGenericTypePath_test.go

// quickInfoGenericTypePath_test.go
static void TestQuickInfoGenericTypePath(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
function f<T>(x: T) {
  class C {
    value = x
  }
  return new C()
}

class Box<T> {
  public value: T;
  constructor(value: T) {
    this.value = value;
  }
}

const instance = f/*callF*/("hello");
const b1/*b1*/ = new Box/*newBox*/(instance);
declare const b2/*b2*/: Box<typeof instance>;
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoGenericTypePath, TestQuickInfoGenericTypePath);

// quickInfoGenerics_test.go

// quickInfoGenerics_test.go
static void TestQuickInfoGenerics(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(class Con/*1*/tainer<T> {
    x: T;
}
interface IList</*2*/T> {
    getItem(i: number): /*3*/T;
}
class List2</*4*/T extends IList<number>> implements IList<T> {
    private __it/*6*/em: /*5*/T[];
    public get/*7*/Item(i: number) {
        return this.__item[i];
    }
    public /*8*/method</*9*/S extends IList<T>>(s: S, p: /*10*/T[]) {
        return s;
    }
}
function foo4</*11*/T extends Date>(test: T): T;
function foo4</*12*/S extends string>(test: S): S;
function foo4(test: any): any;
function foo4</*13*/T extends Date>(test: any): any { return null; }
var x: List2<IList<number>>;
var y = x./*14*/getItem(10);
var x2: IList<IList<number>>;
var x3: IList<number>;
var y2 = x./*15*/method(x2, [x3, x3]);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "class Container<T>", "");
		f->VerifyQuickInfoAt(t, "2", "(type parameter) T in IList<T>", "");
		f->VerifyQuickInfoAt(t, "3", "(type parameter) T in IList<T>", "");
		f->VerifyQuickInfoAt(t, "4", "(type parameter) T in List2<T extends IList<number>>", "");
		f->VerifyQuickInfoAt(t, "5", "(type parameter) T in List2<T extends IList<number>>", "");
		f->VerifyQuickInfoAt(t, "6", "(property) List2<T extends IList<number>>.__item: T[]", "");
		f->VerifyQuickInfoAt(t, "7", "(method) List2<T extends IList<number>>.getItem(i: number): T", "");
		f->VerifyQuickInfoAt(t, "8", "(method) List2<T extends IList<number>>.method<S extends IList<T>>(s: S, p: T[]): S", "");
		f->VerifyQuickInfoAt(t, "9", "(type parameter) S in List2<T extends IList<number>>.method<S extends IList<T>>(s: S, p: T[]): S", "");
		f->VerifyQuickInfoAt(t, "10", "(type parameter) T in List2<T extends IList<number>>", "");
		f->VerifyQuickInfoAt(t, "11", "(type parameter) T in foo4<T extends Date>(test: T): T", "");
		f->VerifyQuickInfoAt(t, "12", "(type parameter) S in foo4<S extends string>(test: S): S", "");
		f->VerifyQuickInfoAt(t, "13", "(type parameter) T in foo4<T extends Date>(test: any): any", "");
		f->VerifyQuickInfoAt(t, "14", "(method) List2<IList<number>>.getItem(i: number): IList<number>", "");
		f->VerifyQuickInfoAt(t, "15", "(method) List2<IList<number>>.method<IList<IList<number>>>(s: IList<IList<number>>, p: IList<number>[]): IList<IList<number>>", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoGenerics, TestQuickInfoGenerics);

// quickInfoGetterSetter_test.go

// quickInfoGetterSetter_test.go
static void TestQuickInfoGetterSetter(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @target: es2015
class C {
    #x = Promise.resolve("")
    set /*setterDef*/myValue(x: Promise<string> | string) {
        this.#x = Promise.resolve(x);
    }
    get /*getterDef*/myValue(): Promise<string> {
        return this.#x;
    }
}
let instance = new C();
instance./*setterUse*/myValue = instance./*getterUse*/myValue;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "getterUse", "(property) C.myValue: Promise<string>", "");
		f->VerifyQuickInfoAt(t, "getterDef", "(getter) C.myValue: Promise<string>", "");
		f->VerifyQuickInfoAt(t, "setterUse", "(property) C.myValue: string | Promise<string>", "");
		f->VerifyQuickInfoAt(t, "setterDef", "(setter) C.myValue: string | Promise<string>", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoGetterSetter, TestQuickInfoGetterSetter);

// quickInfoImportMeta_test.go

// quickInfoImportMeta_test.go
static void TestQuickInfoImportMeta(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((((((std::string(R"TS(// @module: esnext
// @Filename: foo.ts
/// <reference path='./bar.d.ts' />
im/*1*/port.me/*2*/ta;
//@Filename: bar.d.ts
/**
 * The type of )TS") + "`") + std::string(R"TS(import.meta)TS")) + std::string("`")) + std::string(R"TS(.
 *
 * If you need to declare that a given property exists on )TS")) + std::string("`")) + std::string(R"TS(import.meta)TS")) + std::string("`")) + std::string(R"TS(,
 * this type may be augmented via interface merging.
 */
 interface ImportMeta {
})TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoImportMeta, TestQuickInfoImportMeta);

// quickInfoImportNonunicodePath_test.go

// quickInfoImportNonunicodePath_test.go
static void TestQuickInfoImportNonunicodePath(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /江南今何在/tmp.ts
export const foo = 1;
// @Filename: /test.ts
import { foo } from "./江南/*1*/今何在/tmp";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", R"TS(module "./江南今何在/tmp")TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoImportNonunicodePath, TestQuickInfoImportNonunicodePath);

// quickInfoInFunctionTypeReference2_test.go

// quickInfoInFunctionTypeReference2_test.go
static void TestQuickInfoInFunctionTypeReference2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C<T> {
    map(fn: (/*1*/k: string, /*2*/value: T, context: any) => void, context: any) {
    }
}
var c: C<number>;
c.map(/*3*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(parameter) k: string", "");
		f->VerifyQuickInfoAt(t, "2", "(parameter) value: T", "");
		f->GoToMarker(t, "3");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "map(fn: (k: string, value: number, context: any) => void, context: any): void"});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoInFunctionTypeReference2, TestQuickInfoInFunctionTypeReference2);

// quickInfoInFunctionTypeReference_test.go

// quickInfoInFunctionTypeReference_test.go
static void TestQuickInfoInFunctionTypeReference(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function map(fn: (variab/*1*/le1: string) => void) {
}
var x = <{ (fn: (va/*2*/riable2: string) => void, a: string): void; }> () => { };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(parameter) variable1: string", "");
		f->VerifyQuickInfoAt(t, "2", "(parameter) variable2: string", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoInFunctionTypeReference, TestQuickInfoInFunctionTypeReference);

// quickInfoInInvalidIndexSignature_test.go

// quickInfoInInvalidIndexSignature_test.go
static void TestQuickInfoInInvalidIndexSignature(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(function method() { var /**/dictionary = <{ [index]: string; }>{}; })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", R"TS((local var) dictionary: {
    [x: number]: string;
})TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoInInvalidIndexSignature, TestQuickInfoInInvalidIndexSignature);

// quickInfoInJsdocInTsFile1_test.go

// quickInfoInJsdocInTsFile1_test.go
static void TestQuickInfoInJsdocInTsFile1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(/** @type {() => { /*1*/data: string[] }} */
function test(): { data: string[] } {
  return {
    data: [],
  };
}

/** @returns {{ /*2*/data: string[] }} */
function test2(): { data: string[] } {
  return {
    data: [],
  };
}

/** @type {{ /*3*/bar: string; }} */
const test3 = { bar: '' };

type SomeObj = { bar: string; };
/** @type {SomeObj/*4*/} */
const test4 = { bar: '' }

/**
 * @param/*5*/ stuff/*6*/ Stuff to do stuff with
 */
function doStuffWithStuff(stuff: { quantity: number }) {}

declare const stuff: { quantity: number };
/** @see {doStuffWithStuff/*7*/} */
if (stuff.quantity) {}

/** @type {(a/*8*/: string) => void} */
function test2(a: string) {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "", "");
		f->VerifyQuickInfoAt(t, "2", "", "");
		f->VerifyQuickInfoAt(t, "3", "", "");
		f->VerifyQuickInfoAt(t, "4", R"TS(type SomeObj = {
    bar: string;
})TS", "");
		f->VerifyQuickInfoAt(t, "5", R"TS((parameter) stuff: {
    quantity: number;
})TS", "Stuff to do stuff with");
		f->VerifyQuickInfoAt(t, "6", R"TS((parameter) stuff: {
    quantity: number;
})TS", "Stuff to do stuff with");
		f->VerifyQuickInfoAt(t, "7", R"TS(function doStuffWithStuff(stuff: {
    quantity: number;
}): void)TS", "");
		f->VerifyQuickInfoAt(t, "8", "", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoInJsdocInTsFile1, TestQuickInfoInJsdocInTsFile1);

// quickInfoInObjectLiteral_test.go

// quickInfoInObjectLiteral_test.go
static void TestQuickInfoInObjectLiteral(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Foo {
    doStuff(x: string, callback: (a: string) => string);
}
var x1: Foo = {
    y/*1*/1: () => {
        return "";
    } ,
    doStuff: (z, callback) => { return callback(this.y); }
}
var value = 3;
class Foo {
    static getRandomPosition() {
        return {
            "row": v/*2*/alue
        }
  }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(property) y1: () => string", "");
		f->VerifyQuickInfoAt(t, "2", "var value: number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoInObjectLiteral, TestQuickInfoInObjectLiteral);

// quickInfoInOptionalChain_test.go

// quickInfoInOptionalChain_test.go
static void TestQuickInfoInOptionalChain(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
interface A {
  arr: string[];
}

function test(a?: A): string {
  return a?.ar/*1*/r.length ? "A" : "B";
}

interface Foo { bar: { baz: string } };
declare const foo: Foo | undefined;

if (foo?.b/*2*/ar.b/*3*/az) {}

interface Foo2 { bar?: { baz: { qwe: string } } };
declare const foo2: Foo2;

if (foo2.b/*4*/ar?.b/*5*/az.q/*6*/we) {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(property) A.arr: string[]", "");
		f->VerifyQuickInfoAt(t, "2", R"TS((property) Foo.bar: {
    baz: string;
})TS", "");
		f->VerifyQuickInfoAt(t, "3", "(property) baz: string | undefined", "");
		f->VerifyQuickInfoAt(t, "4", R"TS((property) Foo2.bar?: {
    baz: {
        qwe: string;
    };
} | undefined)TS", "");
		f->VerifyQuickInfoAt(t, "5", R"TS((property) baz: {
    qwe: string;
})TS", "");
		f->VerifyQuickInfoAt(t, "6", "(property) qwe: string | undefined", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoInOptionalChain, TestQuickInfoInOptionalChain);

// quickInfoInWithBlock_test.go

// quickInfoInWithBlock_test.go
static void TestQuickInfoInWithBlock(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(with (x) {
    function /*1*/f() { }
    var /*2*/b = /*3*/f;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "any", "");
		f->VerifyQuickInfoAt(t, "2", "any", "");
		f->VerifyQuickInfoAt(t, "3", "any", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoInWithBlock, TestQuickInfoInWithBlock);

// quickInfoIndexSignatureMappedType_test.go

// quickInfoIndexSignatureMappedType_test.go
static void TestQuickInfoIndexSignatureMappedType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @strict: true
// @filename: main.ts
declare const record: Record<string, string>;
record.fo/*1*/o;
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "string", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoIndexSignatureMappedType, TestQuickInfoIndexSignatureMappedType);

// quickInfoInheritDoc2_test.go

// quickInfoInheritDoc2_test.go
static void TestQuickInfoInheritDoc2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noEmit: true
// @allowJs: true
// @Filename: quickInfoInheritDoc2.ts
class Base<T> {
    /**
     * Base.prop
     */
    prop: T | undefined;
}

class SubClass<T> extends Base<T> {
    /**
     * @inheritdoc
     * SubClass.prop
     */
    /*1*/prop: T | undefined;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoInheritDoc2, TestQuickInfoInheritDoc2);

// quickInfoInheritDoc3_test.go

// quickInfoInheritDoc3_test.go
static void TestQuickInfoInheritDoc3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noEmit: true
// @allowJs: true
// @Filename: quickInfoInheritDoc3.ts
function getBaseClass() {
    return class Base {
        /**
         * Base.prop
         */
        prop: string | undefined;
    }
}
class SubClass extends getBaseClass() {
    /**
     * @inheritdoc
     * SubClass.prop
     */
    /*1*/prop: string | undefined;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoInheritDoc3, TestQuickInfoInheritDoc3);

// quickInfoInheritDoc4_test.go

// quickInfoInheritDoc4_test.go
static void TestQuickInfoInheritDoc4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: quickInfoInheritDoc4.ts
var A: any;

class B extends A {
    /**
     * @inheritdoc
     */
    static /**/value() {
        return undefined;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoInheritDoc4, TestQuickInfoInheritDoc4);

// quickInfoInheritDoc5_test.go

// quickInfoInheritDoc5_test.go
static void TestQuickInfoInheritDoc5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: quickInfoInheritDoc5.js
function A() {}

class B extends A {
    /**
     * @inheritdoc
     */
    static /**/value() {
        return undefined;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoInheritDoc5, TestQuickInfoInheritDoc5);

// quickInfoInheritDoc6_test.go

// quickInfoInheritDoc6_test.go
static void TestQuickInfoInheritDoc6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: quickInfoInheritDoc6.js
class B extends UNRESOLVED_VALUE_DEFINITELY_DOES_NOT_EXIST {
    /**
     * @inheritdoc
     */
    static /**/value() {
        return undefined;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoInheritDoc6, TestQuickInfoInheritDoc6);

// quickInfoInheritDoc_test.go

// quickInfoInheritDoc_test.go
static void TestQuickInfoInheritDoc(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noEmit: true
// @allowJs: true
// @Filename: quickInfoInheritDoc.ts
abstract class BaseClass {
    /**
     * Useful description always applicable
     * 
     * @returns {string} Useful description of return value always applicable.
     */
    public static doSomethingUseful(stuff?: any): string {
        throw new Error('Must be implemented by subclass');
    }

    /**
     * BaseClass.func1
     * @param {any} stuff1 BaseClass.func1.stuff1
     * @returns {void} BaseClass.func1.returns
     */
    public static func1(stuff1: any): void {
    }

    /**
     * Applicable description always.
     */
    public static readonly someProperty: string = 'general value';
}




class SubClass extends BaseClass {

    /**
     * @inheritDoc
     * 
     * @param {{ tiger: string; lion: string; }} [mySpecificStuff] Description of my specific parameter.
     */
    public static /*1*/doSomethingUseful(mySpecificStuff?: { tiger: string; lion: string; }): string {
        let useful = '';

        // do something useful to useful

        return useful;
    }

    /**
     * @inheritDoc
     * @param {any} stuff1 SubClass.func1.stuff1
     * @returns {void} SubClass.func1.returns
     */
    public static /*2*/func1(stuff1: any): void {
    }

    /**
     * text over tag
     * @inheritDoc
     * text after tag
     */
    public static readonly /*3*/someProperty: string = 'specific to this class value'
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoInheritDoc, TestQuickInfoInheritDoc);

// quickInfoInheritedLinkTag_test.go

// quickInfoInheritedLinkTag_test.go
static void TestQuickInfoInheritedLinkTag(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(export class C {
     /**
      * @deprecated Use {@link PerspectiveCamera#setFocalLength .setFocalLength()} and {@link PerspectiveCamera#filmGauge .filmGauge} instead.
      */
    m() { }
}
export class D extends C {
    m() { } // crashes here
}
new C().m/**/ // and here (with a different thing trying to access undefined))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoInheritedLinkTag, TestQuickInfoInheritedLinkTag);

// quickInfoJSDocAtBeforeSpace_test.go

// quickInfoJSDocAtBeforeSpace_test.go
static void TestQuickInfoJSDocAtBeforeSpace(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/**
 * @return Don't @ me
 */
function /*f*/f() { }
/**
 * @return One final @
 */
function /*g*/g() { }
/**
 * @return An @
 * But another line
 */
function /*h*/h() { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJSDocAtBeforeSpace, TestQuickInfoJSDocAtBeforeSpace);

// quickInfoJSDocBackticks_test.go

// quickInfoJSDocBackticks_test.go
static void TestQuickInfoJSDocBackticks(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const auto content = ((((((((((std::string(R"TS(// @noEmit: true
// @allowJs: true
// @checkJs: true
// @strict: true
// @Filename: jsdocParseMatchingBackticks.js
/**
 * )TS") + "`") + std::string(R"TS(@param)TS")) + std::string("`")) + std::string(R"TS( initial at-param is OK in title comment
 * @param {string} x hi there )TS")) + std::string("`")) + std::string(R"TS(@param)TS")) + std::string("`")) + std::string(R"TS(
 * @param {string} y hi there )TS")) + std::string("`")) + std::string(R"TS(@ * param
 *                   this is the margin
 */
export function f(x, y) {
    return x/*x*/ + y/*y*/
}
f/*f*/)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "f");
		f->VerifyQuickInfoIs(t, "function f(x: string, y: string): string", "`@param` initial at-param is OK in title comment");
		f->GoToMarker(t, "x");
		f->VerifyQuickInfoIs(t, "(parameter) x: string", "hi there `@param`");
		f->GoToMarker(t, "y");
		f->VerifyQuickInfoIs(t, "(parameter) y: string", R"TS(hi there `@ * param
this is the margin)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJSDocBackticks, TestQuickInfoJSDocBackticks);

// quickInfoJSDocCodefenceAtSign_test.go

// quickInfoJSDocCodefenceAtSign_test.go
static void TestQuickInfoJSDocCodefenceAtSign(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((((((((((std::string(R"TS(/**
 * text
 * @example Foo
 * )TS") + "```") + std::string(R"TS(
 * @Embed[asfasdfasf]
 * )TS")) + std::string("```")) + std::string(R"TS(
 * becomes
 * )TS")) + std::string("```html")) + std::string(R"TS(
 * <div></div>
 * )TS")) + std::string("```")) + std::string(R"TS(
 */
const /*1*/x = 1;

/**
 * Some text
 * )TS")) + std::string("```")) + std::string(R"TS(
 * @tag inside code
 * )TS")) + std::string("```")) + std::string(R"TS(
 * @param y - a number
 */
function /*2*/foo(y: number) {}
)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJSDocCodefenceAtSign, TestQuickInfoJSDocCodefenceAtSign);

// quickInfoJSDocFunctionNew_test.go

// quickInfoJSDocFunctionNew_test.go
static void TestQuickInfoJSDocFunctionNew(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: Foo.js
/** @type {function (new: string, string): string} */
var f/**/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyQuickInfoIs(t, "var f: new (arg1: string) => string", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJSDocFunctionNew, TestQuickInfoJSDocFunctionNew);

// quickInfoJSDocFunctionThis_test.go

// quickInfoJSDocFunctionThis_test.go
static void TestQuickInfoJSDocFunctionThis(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: Foo.js
/** @type {function (this: string, string): string} */
var f/**/ = function (s) { return s; })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyQuickInfoIs(t, "var f: (this: string, arg1: string) => string", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJSDocFunctionThis, TestQuickInfoJSDocFunctionThis);

// quickInfoJSDocLinkBackticks_test.go

// quickInfoJSDocLinkBackticks_test.go
static void TestQuickInfoJSDocLinkBackticks(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((((((std::string(R"TS(// @noEmit: true
// @allowJs: true
// @checkJs: true
// @strict: true
// @Filename: jsdocParseMatchingBackticks.js
/**
 * )TS") + "`") + std::string(R"TS({@link foo})TS")) + std::string("`")) + std::string(R"TS( initial at-param is OK in title comment
 * @param {string} x hi there )TS")) + std::string("`")) + std::string(R"TS({@link foo})TS")) + std::string("`")) + std::string(R"TS(
 */
export function f(x) {
    return x/*x*/
}
f/*f*/)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "f");
		f->VerifyQuickInfoIs(t, "function f(x: string): string", R"TS(`{@link foo}` initial at-param is OK in title comment

*@param* `x` — hi there `{@link foo}`)TS");
		f->GoToMarker(t, "x");
		f->VerifyQuickInfoIs(t, "(parameter) x: string", "hi there `{@link foo}`");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJSDocLinkBackticks, TestQuickInfoJSDocLinkBackticks);

// quickInfoJSDocParamWithInvalidTagInComment_test.go

// quickInfoJSDocParamWithInvalidTagInComment_test.go
static void TestQuickInfoJSDocParamWithInvalidTagInComment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
/**
 * @param {string} x Checks @-rule here
 * @param {string} a see @foo*bar here
 * @param {string} b see @test(something) here
 * @param {string} c see @*not-ident here
 * @param {string} d see @(paren) here
 */
function /*fn*/foo(/**/x, /*a*/a, /*b*/b, /*c*/c, /*d*/d) {}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "fn", "function foo(x: string, a: string, b: string, c: string, d: string): void", (((((((std::string("") + R"TS(

*@param* `x` — Checks @-rule here
)TS") + std::string(R"TS(
*@param* `a` — see)TS")) + std::string(R"TS(

*@foo* — *bar here
)TS")) + std::string(R"TS(
*@param* `b` — see)TS")) + std::string(R"TS(

*@test* — (something) here)TS")) + std::string(R"TS(

*@param* `c` — see @*not-ident here)TS")) + std::string(R"TS(

*@param* `d` — see @(paren) here)TS")));
		f->VerifyQuickInfoAt(t, "", "(parameter) x: string", "Checks @-rule here");
		f->VerifyQuickInfoAt(t, "a", "(parameter) a: string", "see");
		f->VerifyQuickInfoAt(t, "b", "(parameter) b: string", "see");
		f->VerifyQuickInfoAt(t, "c", "(parameter) c: string", "see @*not-ident here");
		f->VerifyQuickInfoAt(t, "d", "(parameter) d: string", "see @(paren) here");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJSDocParamWithInvalidTagInComment, TestQuickInfoJSDocParamWithInvalidTagInComment);

// quickInfoJSDocParamWithTrailingAtBeforeCommentEnd_test.go

// quickInfoJSDocParamWithTrailingAtBeforeCommentEnd_test.go
static void TestQuickInfoJSDocParamWithTrailingAtBeforeCommentEnd(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
/** @param {string} x trailing @/*at*/*/
function /*fn*/foo(/*x*/x) {}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "fn", "function foo(x: string): void", R"TS(

*@param* `x` — trailing @)TS");
		f->VerifyQuickInfoAt(t, "x", "(parameter) x: string", "trailing @");
		f->VerifyCompletions(t, "at", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "param", .Kind = std::make_shared<lsproto::CompletionItemKind>(lsproto::CompletionItemKindKeyword)})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJSDocParamWithTrailingAtBeforeCommentEnd, TestQuickInfoJSDocParamWithTrailingAtBeforeCommentEnd);

// quickInfoJSDocTags_test.go

// quickInfoJSDocTags_test.go
static void TestQuickInfoJSDocTags(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/**
 * This is class Foo.
 * @mytag comment1 comment2
 */
class Foo {
    /**
     * This is the constructor.
     * @myjsdoctag this is a comment
     */
    constructor(value: number) {}
    /**
     * method1 documentation
     * @mytag comment1 comment2
     */
    static method1() {}
    /**
     * @mytag
     */
    method2() {}
    /**
     * @mytag comment1 comment2
     */
    property1: string;
    /**
     * @mytag1 some comments
     * some more comments about mytag1
     * @mytag2
     * here all the comments are on a new line
     * @mytag3
     * @mytag
     */
    property2: number;
    /**
     * @returns {number} a value
     */
    method3(): number { return 3; }
    /**
     * @param {string} foo A value.
     * @returns {number} Another value
     * @mytag
     */
    method4(foo: string): number { return 3; }
    /** @mytag */
    method5() {}
    /** method documentation
     *  @mytag a JSDoc tag
     */
    newMethod() {}
}
var foo = new /*1*/Foo(/*10*/4);
/*2*/Foo./*3*/method1(/*11*/);
foo./*4*/method2(/*12*/);
foo./*5*/method3(/*13*/);
foo./*6*/method4();
foo./*7*/property1;
foo./*8*/property2;
foo./*9*/method5();
foo.newMet/*14*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJSDocTags, TestQuickInfoJSDocTags);

// quickInfoJSDocTypedefPropertyWithInvalidTag_test.go

// quickInfoJSDocTypedefPropertyWithInvalidTag_test.go
static void TestQuickInfoJSDocTypedefPropertyWithInvalidTag(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
/**
 * @typedef {Object} MyType1
 * @property {string} name
 * @-rule
 * @property {number} age
 */

/**
 * @typedef {Object} MyType2
 * @property {string} name
 * some comment
 * @property {number} age
 */

/**
 * @typedef {Object} MyType3
 * @property {string} name
 * @*stars
 * @property {number} age
 */

/**
 * @typedef {Object} MyType4
 * @property {string} name
 * @(parens)
 * @property {number} age
 */

/**
 * @typedef {Object} MyType5
 * @property {string} name
 * @foo*bar
 * @property {number} age
 */

/** @type {/*t1*/MyType1} */
const obj1 = { /*1n*/name: "", /*1a*/age: 10 };

/** @type {/*t2*/MyType2} */
const obj2 = { /*2n*/name: "", /*2a*/age: 10 };

/** @type {/*t3*/MyType3} */
const obj3 = { /*3n*/name: "", /*3a*/age: 10 };

/** @type {/*t4*/MyType4} */
const obj4 = { /*4n*/name: "", /*4a*/age: 10 };

/** @type {/*t5*/MyType5} */
const obj5 = { /*5n*/name: "", /*5a*/age: 10 };
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "t1", R"TS(type MyType1 = {
    name: string;
    age: number;
})TS", "");
		f->VerifyQuickInfoAt(t, "t2", R"TS(type MyType2 = {
    name: string;
    age: number;
})TS", "");
		f->VerifyQuickInfoAt(t, "t3", R"TS(type MyType3 = {
    name: string;
    age: number;
})TS", "");
		f->VerifyQuickInfoAt(t, "t4", R"TS(type MyType4 = {
    name: string;
    age: number;
})TS", "");
		f->VerifyQuickInfoAt(t, "t5", R"TS(type MyType5 = {
    name: string;
})TS", ((std::string("") + R"TS(

*@foo* — *bar)TS") + std::string(R"TS(

*@property* — {number} age)TS")));
		f->VerifyQuickInfoAt(t, "1n", "(property) name: string", "@-rule");
		f->VerifyQuickInfoAt(t, "2n", "(property) name: string", "some comment");
		f->VerifyQuickInfoAt(t, "3n", "(property) name: string", "@*stars");
		f->VerifyQuickInfoAt(t, "4n", "(property) name: string", "@(parens)");
		f->VerifyQuickInfoAt(t, "5n", "(property) name: string", "");
		f->VerifyQuickInfoAt(t, "1a", "(property) age: number", "");
		f->VerifyQuickInfoAt(t, "2a", "(property) age: number", "");
		f->VerifyQuickInfoAt(t, "3a", "(property) age: number", "");
		f->VerifyQuickInfoAt(t, "4a", "(property) age: number", "");
		f->VerifyQuickInfoAt(t, "5a", "(property) age: number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJSDocTypedefPropertyWithInvalidTag, TestQuickInfoJSDocTypedefPropertyWithInvalidTag);

// quickInfoJSExport_test.go

// quickInfoJSExport_test.go
static void TestQuickInfoJSExport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: a.js
// @allowJs: true
/**
 * @enum {string}
 */
const testString = {
    one: "1",
    two: "2"
};

export { test/**/String };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", R"TS((alias) type testString = string
(alias) const testString: {
    one: string;
    two: string;
}
export testString)TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJSExport, TestQuickInfoJSExport);

// quickInfoJsDocAlias_test.go

// quickInfoJsDocAlias_test.go
static void TestQuickInfoJsDocAlias(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: /a.d.ts
/** docs - type T */
export type T = () => void;
/**
 * docs - const A: T
 */
export declare const A: T;
// @filename: /b.ts
import { A } from "./a";
A/**/())TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocAlias, TestQuickInfoJsDocAlias);

// quickInfoJsDocGetterSetterNoCrash1_test.go

// quickInfoJsDocGetterSetterNoCrash1_test.go
static void TestQuickInfoJsDocGetterSetterNoCrash1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(class A implements A {
  get x(): string { return "" }
}
const e = new A()
e.x/*1*/

class B implements B {
  set x(v: string) {}
}
const f = new B()
f.x/*2*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(property) A.x: string", "");
		f->VerifyQuickInfoAt(t, "2", "(property) B.x: string", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocGetterSetterNoCrash1, TestQuickInfoJsDocGetterSetterNoCrash1);

// quickInfoJsDocGetterSetter_test.go

// quickInfoJsDocGetterSetter_test.go
static void TestQuickInfoJsDocGetterSetter(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class A {
    /**
     * getter A
     * @returns return A
     */
    get /*1*/x(): string {
        return "";
    }
    /**
     * setter A
     * @param value foo A
     * @todo empty jsdoc
     */
    set /*2*/x(value) { }
}
// override both getter and setter
class B extends A {
    /**
     * getter B
     * @returns return B
     */
    get /*3*/x(): string {
        return "";
    }
    /**
     * setter B
     * @param value foo B
     */
    set /*4*/x(vale) { }
}
// not override
class C extends A { }
// only override setter
class D extends A {
    /**
     * setter D
     * @param value foo D
     */
    set /*5*/x(val: string) { }
}
new A()./*6*/x = "1";
new B()./*7*/x = "1";
new C()./*8*/x = "1";
new D()./*9*/x = "1";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocGetterSetter, TestQuickInfoJsDocGetterSetter);

// quickInfoJsDocInheritage_test.go

// quickInfoJsDocInheritage_test.go
static void TestQuickInfoJsDocInheritage(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface A {
    /**
     * @description A.foo1
     */
    foo1: number;
    /**
     * @description A.foo2
     */
    foo2: (para1: string) => number;
}

interface B {
    /**
     * @description B.foo1
     */
    foo1: number;
    /**
     * @description B.foo2
     */
    foo2: (para2: string) => number;
}

// implement multi interfaces with duplicate name
// method for function signature
class C implements A, B {
    /*1*/foo1: number = 1;
    /*2*/foo2(q: string) { return 1 }
}

// implement multi interfaces with duplicate name
// property for function signature
class D implements A, B {
    /*3*/foo1: number = 1;
    /*4*/foo2 = (q: string) => { return 1 }
}

new C()./*5*/foo1;
new C()./*6*/foo2;
new D()./*7*/foo1;
new D()./*8*/foo2;

class Base1 {
    /**
     * @description Base1.foo1 
     */
    foo1: number = 1;

    /**
     * 
     * @param q Base1.foo2 parameter
     * @returns Base1.foo2 return
     */
     foo2(q: string) { return 1 }
}

// extends class and implement interfaces with duplicate name
// property override method
class Drived1 extends Base1 implements A {
    /*9*/foo1: number = 1;
    /*10*/foo2(para1: string) { return 1 };
}

// extends class and implement interfaces with duplicate name
// method override method
class Drived2 extends Base1 implements B {
    /*11*/foo1: number = 1;
    /*12*/foo2 = (para1: string) => { return 1; };
}

class Base2 {
    /**
     * @description Base2.foo1 
     */
    foo1: number = 1;
    /**
     * 
     * @param q Base2.foo2 parameter
     * @returns Base2.foo2 return
     */
    foo2(q: string) { return 1 }
}

// extends class and implement interfaces with duplicate name
// property override method
class Drived3 extends Base2 implements A {
    /*13*/foo1: number = 1;
    /*14*/foo2(para1: string) { return 1 };
}

// extends class and implement interfaces with duplicate name
// method override method
class Drived4 extends Base2 implements B {
    /*15*/foo1: number = 1;
    /*16*/foo2 = (para1: string) => { return 1; };
}

new Drived1()./*17*/foo1;
new Drived1()./*18*/foo2;
new Drived2()./*19*/foo1;
new Drived2()./*20*/foo2;
new Drived3()./*21*/foo1;
new Drived3()./*22*/foo2;
new Drived4()./*23*/foo1;
new Drived4()./*24*/foo2;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocInheritage, TestQuickInfoJsDocInheritage);

// quickInfoJsDocNonDiscriminatedUnionSharedProp_test.go

// quickInfoJsDocNonDiscriminatedUnionSharedProp_test.go
static void TestQuickInfoJsDocNonDiscriminatedUnionSharedProp(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: false
interface Entries {
  /**
   * Plugins info...
   */
  plugins?: Record<string, Record<string, unknown>>;
  /**
   * Output info...
   */
  output?: string;
  /**
   * Format info...
   */
  format?: string;
}

interface Input extends Entries {
  /**
   * Input info...
   */
  input: string;
}

interface Types extends Entries {
  /**
   * Types info...
   */
  types: string;
}

type EntriesOptions = Input | Types;

const options: EntriesOptions[] = [
  {
    input: "./src/index.ts",
    /*1*/output: "./dist/index.mjs",
  },
  {
    types: "./src/types.ts",
    format: "esm",
  },
];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(property) Entries.output?: string", "Output info...");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocNonDiscriminatedUnionSharedProp, TestQuickInfoJsDocNonDiscriminatedUnionSharedProp);

// quickInfoJsDocTags10_test.go

// quickInfoJsDocTags10_test.go
static void TestQuickInfoJsDocTags10(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noEmit: true
// @allowJs: true
// @Filename: quickInfoJsDocTags10.js
/**
 * @param {T1} a
 * @param {T2} a
 * @template T1,T2 Comment Text
 */
const /**/foo = (a, b) => {};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocTags10, TestQuickInfoJsDocTags10);

// quickInfoJsDocTags11_test.go

// quickInfoJsDocTags11_test.go
static void TestQuickInfoJsDocTags11(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noEmit: true
// @allowJs: true
// @Filename: quickInfoJsDocTags11.js
/**
 * @param {T1} a
 * @param {T2} b
 * @template {number} T1 Comment T1
 * @template {number} T2 Comment T2
 */
const /**/foo = (a, b) => {};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocTags11, TestQuickInfoJsDocTags11);

// quickInfoJsDocTags12_test.go

// quickInfoJsDocTags12_test.go
static void TestQuickInfoJsDocTags12(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/**
 * @param {Object} options the args object
 * @param {number} options.a first number
 * @param {number} options.b second number
 * @param {Function} callback the callback function
 * @returns {number}
 */
function /**/f(options, callback = null) {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocTags12, TestQuickInfoJsDocTags12);

// quickInfoJsDocTags13VS_test.go

// quickInfoJsDocTags13VS_test.go
static void TestQuickInfoJsDocTags13VS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @filename: ./a.js
/**
 * First overload
 * @overload
 * @param {number} a
 * @returns {void}
 */

/**
 * Second overload
 * @overload
 * @param {string} a
 * @returns {void}
 */

/**
 * @param {string | number} a
 * @returns {void}
 */
function f(a) {}

f(/*a*/1);
f(/*b*/"");)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocTags13VS, TestQuickInfoJsDocTags13VS);

// quickInfoJsDocTags13_test.go

// quickInfoJsDocTags13_test.go
static void TestQuickInfoJsDocTags13(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @filename: ./a.js
/**
 * First overload
 * @overload
 * @param {number} a
 * @returns {void}
 */

/**
 * Second overload
 * @overload
 * @param {string} a
 * @returns {void}
 */

/**
 * @param {string | number} a
 * @returns {void}
 */
function f(a) {}

f(/*a*/1);
f(/*b*/"");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocTags13, TestQuickInfoJsDocTags13);

// quickInfoJsDocTags14_test.go

// quickInfoJsDocTags14_test.go
static void TestQuickInfoJsDocTags14(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/**
 * @param {Object} options the args object
 * @param {number} options.a first number
 * @param {number} options.b second number
 * @param {Object} options.c sub-object
 * @param {number} options.c.d third number
 * @param {Function} callback the callback function
 * @returns {number}
 */
function /**/fn(options, callback = null) { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocTags14, TestQuickInfoJsDocTags14);

// quickInfoJsDocTags15_test.go

// quickInfoJsDocTags15_test.go
static void TestQuickInfoJsDocTags15(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @filename: /a.js
/**
 * @callback Bar
 * @param {string} name
 * @returns {string}
 */

/**
 * @typedef Foo
 * @property {Bar} getName
 */
export const foo = 1;
// @filename: /b.js
import * as _a from "./a.js";
/**
 * @implements {_a.Foo/*1*/}
 */
class C1 { }

/**
 * @extends {_a.Foo/*2*/}
 */
class C2 { }

/**
 * @augments {_a.Foo/*3*/}
 */
class C3 { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.js");
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocTags15, TestQuickInfoJsDocTags15);

// quickInfoJsDocTags16_test.go

// quickInfoJsDocTags16_test.go
static void TestQuickInfoJsDocTags16(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class A {
    /**
     * Description text here.
     *
     * @virtual
     */
    foo() { }
}

class B extends A {
    override /*1*/foo() { }
}

class C extends B {
    override /*2*/foo() { }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocTags16, TestQuickInfoJsDocTags16);

// quickInfoJsDocTags1_test.go

// quickInfoJsDocTags1_test.go
static void TestQuickInfoJsDocTags1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: quickInfoJsDocTags1.ts
/**
 * Doc
 * @author Me <me@domain.tld>
 * @augments {C<T>} Augments it
 * @template T A template
 * @type {number | string} A type
 * @typedef {number | string} NumOrStr
 * @property {number} x The prop
 * @param {number} x The param
 * @returns The result
 * @see x (the parameter)
 */
function /**/foo(x) {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocTags1, TestQuickInfoJsDocTags1);

// quickInfoJsDocTags2_test.go

// quickInfoJsDocTags2_test.go
static void TestQuickInfoJsDocTags2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: quickInfoJsDocTags2.ts
/** Doc   */
const /**/x = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "const x: 0", "Doc");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocTags2, TestQuickInfoJsDocTags2);

// quickInfoJsDocTags3_test.go

// quickInfoJsDocTags3_test.go
static void TestQuickInfoJsDocTags3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: quickInfoJsDocTags3.ts
interface Foo {
    /**
     * comment
     * @author Me <me@domain.tld>
     * @see x (the parameter)
     * @param {number} x - x comment
     * @param {number} y - y comment
     * @throws {Error} comment
     */
    method(x: number, y: number): void;
}

class Bar implements Foo {
    /**/method(): void {
        throw new Error("Method not implemented.");
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocTags3, TestQuickInfoJsDocTags3);

// quickInfoJsDocTags4_test.go

// quickInfoJsDocTags4_test.go
static void TestQuickInfoJsDocTags4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: quickInfoJsDocTags4.ts
class Foo {
    /**
     * comment
     * @author Me <me@domain.tld>
     * @see x (the parameter)
     * @param {number} x - x comment
     * @param {number} y - y comment
     * @returns The result
     */
    method(x: number, y: number): number {
       return x + y;
    }
}

class Bar extends Foo {
    /**/method(x: number, y: number): number {
        const res = super.method(x, y) + 100;
        return res;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocTags4, TestQuickInfoJsDocTags4);

// quickInfoJsDocTags5_test.go

// quickInfoJsDocTags5_test.go
static void TestQuickInfoJsDocTags5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noEmit: true
// @allowJs: true
// @Filename: quickInfoJsDocTags5.js
class Foo {
    /**
     * comment
     * @author Me <me@domain.tld>
     * @see x (the parameter)
     * @param {number} x - x comment
     * @param {number} y - y comment
     * @returns The result
     */
    method(x, y) {
       return x + y;
    }
}

class Bar extends Foo {
    /**/method(x, y) {
        const res = super.method(x, y) + 100;
        return res;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocTags5, TestQuickInfoJsDocTags5);

// quickInfoJsDocTags6_test.go

// quickInfoJsDocTags6_test.go
static void TestQuickInfoJsDocTags6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noEmit: true
// @allowJs: true
// @Filename: quickInfoJsDocTags6.js
class Foo {
    /**
     * comment
     * @author Me <me@domain.tld>
     * @see x (the parameter)
     * @param {number} x - x comment
     * @param {number} y - y comment
     * @returns The result
     */
    method(x, y) {
       return x + y;
    }
}

class Bar extends Foo {
    /** @inheritDoc */
    /**/method(x, y) {
        const res = super.method(x, y) + 100;
        return res;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocTags6, TestQuickInfoJsDocTags6);

// quickInfoJsDocTags7_test.go

// quickInfoJsDocTags7_test.go
static void TestQuickInfoJsDocTags7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noEmit: true
// @allowJs: true
// @Filename: quickInfoJsDocTags7.js
/**
 * @typedef {{ [x: string]: any, y: number }} Foo
 */

/**
 * @type {(t: T) => number}
 * @template T
 */
const /**/foo = t => t.y;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocTags7, TestQuickInfoJsDocTags7);

// quickInfoJsDocTags8_test.go

// quickInfoJsDocTags8_test.go
static void TestQuickInfoJsDocTags8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noEmit: true
// @allowJs: true
// @Filename: quickInfoJsDocTags8.js
/**
 * @typedef {{ [x: string]: any, y: number }} Foo
 */

/**
 * @type {(t: T) => number}
 * @template {Foo} T
 */
const /**/foo = t => t.y;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocTags8, TestQuickInfoJsDocTags8);

// quickInfoJsDocTags9_test.go

// quickInfoJsDocTags9_test.go
static void TestQuickInfoJsDocTags9(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noEmit: true
// @allowJs: true
// @Filename: quickInfoJsDocTags9.js
/**
 * @typedef {{ [x: string]: any, y: number }} Foo
 */

/**
 * @type {(t: T) => number}
 * @template {Foo} T Comment Text
 */
const /**/foo = t => t.y;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocTags9, TestQuickInfoJsDocTags9);

// quickInfoJsDocTagsCallback_test.go

// quickInfoJsDocTagsCallback_test.go
static void TestQuickInfoJsDocTagsCallback(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noEmit: true
// @allowJs: true
// @Filename: quickInfoJsDocTagsCallback.js
/**
 * @callback cb/*1*/
 * @param {string} x - x comment
 */

/**
 * @param {/*2*/cb} bar -callback comment
 */
function foo(bar) {
    bar(bar);
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocTagsCallback, TestQuickInfoJsDocTagsCallback);

// quickInfoJsDocTagsFunctionOverload01_test.go

// quickInfoJsDocTagsFunctionOverload01_test.go
static void TestQuickInfoJsDocTagsFunctionOverload01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: quickInfoJsDocTagsFunctionOverload01.ts
/**
 * Doc foo
 */
declare function /*1*/foo(): void;

/**
 * Doc foo overloaded
 * @tag Tag text
 */
declare function /*2*/foo(x: number): void)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocTagsFunctionOverload01, TestQuickInfoJsDocTagsFunctionOverload01);

// quickInfoJsDocTagsFunctionOverload03_test.go

// quickInfoJsDocTagsFunctionOverload03_test.go
static void TestQuickInfoJsDocTagsFunctionOverload03(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: quickInfoJsDocTagsFunctionOverload03.ts
declare function /*1*/foo(): void;

/**
 * Doc foo overloaded
 * @tag Tag text
 */
declare function /*2*/foo(x: number): void)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocTagsFunctionOverload03, TestQuickInfoJsDocTagsFunctionOverload03);

// quickInfoJsDocTagsFunctionOverload05_test.go

// quickInfoJsDocTagsFunctionOverload05_test.go
static void TestQuickInfoJsDocTagsFunctionOverload05(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: quickInfoJsDocTagsFunctionOverload05.ts
declare function /*1*/foo(): void;

/**
 * @tag Tag text
 */
declare function /*2*/foo(x: number): void)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocTagsFunctionOverload05, TestQuickInfoJsDocTagsFunctionOverload05);

// quickInfoJsDocTagsTypedef_test.go

// quickInfoJsDocTagsTypedef_test.go
static void TestQuickInfoJsDocTagsTypedef(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noEmit: true
// @allowJs: true
// @Filename: quickInfoJsDocTagsTypedef.js
/**
 * Bar comment
 * @typedef {Object} /*1*/Bar
 * @property {string} baz - baz comment
 * @property {string} qux - qux comment
 */

/**
 * foo comment
 * @param {/*2*/Bar} x - x comment
 * @returns {Bar}
 */
function foo(x) {
    return x;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocTagsTypedef, TestQuickInfoJsDocTagsTypedef);

// quickInfoJsDocTextFormatting1VS_test.go

// quickInfoJsDocTextFormatting1VS_test.go
static void TestQuickInfoJsDocTextFormatting1VS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/**
 * @param {number} var1 **Highlighted text**
 * @param {string} var2 Another **Highlighted text**
*/
function f1(var1, var2) { }

/**
 * @param {number} var1 *Regular text with an asterisk
 * @param {string} var2 Another *Regular text with an asterisk
*/
function f2(var1, var2) { }

/**
 * @param {number} var1 
 * *Regular text with an asterisk
 * @param {string} var2 
 * Another *Regular text with an asterisk
*/
function f3(var1, var2) { }

/**
 * @param {number} var1 
 * **Highlighted text**
 * @param {string} var2 
 * Another **Highlighted text**
*/
function f4(var1, var2) { }

/**
 * @param {number} var1 
   **Highlighted text**
 * @param {string} var2 
   Another **Highlighted text**
*/
function f5(var1, var2) { }

f1(/*1*/);
f2(/*2*/);
f3(/*3*/);
f4(/*4*/);
f5(/*5*/);)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocTextFormatting1VS, TestQuickInfoJsDocTextFormatting1VS);

// quickInfoJsDocTextFormatting1_test.go

// quickInfoJsDocTextFormatting1_test.go
static void TestQuickInfoJsDocTextFormatting1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/**
 * @param {number} var1 **Highlighted text**
 * @param {string} var2 Another **Highlighted text**
*/
function f1(var1, var2) { }

/**
 * @param {number} var1 *Regular text with an asterisk
 * @param {string} var2 Another *Regular text with an asterisk
*/
function f2(var1, var2) { }

/**
 * @param {number} var1 
 * *Regular text with an asterisk
 * @param {string} var2 
 * Another *Regular text with an asterisk
*/
function f3(var1, var2) { }

/**
 * @param {number} var1 
 * **Highlighted text**
 * @param {string} var2 
 * Another **Highlighted text**
*/
function f4(var1, var2) { }

/**
 * @param {number} var1 
   **Highlighted text**
 * @param {string} var2 
   Another **Highlighted text**
*/
function f5(var1, var2) { }

f1(/*1*/);
f2(/*2*/);
f3(/*3*/);
f4(/*4*/);
f5(/*5*/);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocTextFormatting1, TestQuickInfoJsDocTextFormatting1);

// quickInfoJsDocThisTag_test.go

// quickInfoJsDocThisTag_test.go
static void TestQuickInfoJsDocThisTag(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @filename: /a.ts
/** @this {number} */
function f/**/() {
    this
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDocThisTag, TestQuickInfoJsDocThisTag);

// quickInfoJsDoc_test.go

// quickInfoJsDoc_test.go
static void TestQuickInfoJsDoc(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @target: esnext
/**
 * A constant
 * @deprecated
 */
var foo = "foo";

/**
 * A function
 * @deprecated
 */
function fn() { }

/**
 * A class
 * @deprecated
 */
class C {
    /**
     * A field
     * @deprecated
     */
    field = "field";

    /**
     * A getter
     * @deprecated
     */
    get getter() {
        return;
    }

    /**
     * A method
     * @deprecated
     */
    m() { }

    get a() {
        this.field/*0*/;
        this.getter/*1*/;
        this.m/*2*/;
        foo/*3*/;
        C/*4*//;
        fn()/*5*/;

        return 1;
    }

    set a(value: number) {
        this.field/*6*/;
        this.getter/*7*/;
        this.m/*8*/;
        foo/*9*/;
        C/*10*/;
        fn/*11*/();
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsDoc, TestQuickInfoJsDoc);

// quickInfoJsPropertyAssignedAfterMethodDeclaration_test.go

// quickInfoJsPropertyAssignedAfterMethodDeclaration_test.go
static void TestQuickInfoJsPropertyAssignedAfterMethodDeclaration(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noLib: true
// @allowJs: true
// @noImplicitThis: true
// @Filename: /a.js
const o = {
    test/*1*/() {
        this./*2*/test = 0;
    }
};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(method) test(): void", "");
		f->VerifyQuickInfoAt(t, "2", "(method) test(): void", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsPropertyAssignedAfterMethodDeclaration, TestQuickInfoJsPropertyAssignedAfterMethodDeclaration);

// quickInfoJsdocEnum_test.go

// quickInfoJsdocEnum_test.go
static void TestQuickInfoJsdocEnum(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @noLib: true
// @Filename: /a.js
/**
 * Doc
 * @enum {number}
 */
const E = {
    A: 0,
}

/** @type {/*type*/E} */
const x = /*value*/E.A;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyQuickInfoAt(t, "type", "type E = number", "Doc");
		f->VerifyQuickInfoAt(t, "value", R"TS(const E: {
    A: number;
})TS", "Doc");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsdocEnum, TestQuickInfoJsdocEnum);

// quickInfoJsdocTypedefMissingType_test.go

// quickInfoJsdocTypedefMissingType_test.go
static void TestQuickInfoJsdocTypedefMissingType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
/**
 * @typedef /**/A
 */
var x;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "type A = any", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsdocTypedefMissingType, TestQuickInfoJsdocTypedefMissingType);

// quickInfoJsxNamespacedIntrinsic_test.go

// quickInfoJsxNamespacedIntrinsic_test.go
static void TestQuickInfoJsxNamespacedIntrinsic(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @jsx: react
// @Filename: /a.tsx
declare const React: any;
declare namespace JSX {
    interface Element {}
    interface IntrinsicElements {
        /** Element docs */
        "foo:bar": {
            /** Foo docs */
            foo: boolean
            /** Bar docs */
            bar: string
        }
    }
}
<foo:ba/*tag*/r fo/*attr*/o />)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "tag", R"TS((property) JSX.IntrinsicElements["foo:bar"]: {
    foo: boolean;
    bar: string;
})TS", "Element docs");
		f->VerifyQuickInfoAt(t, "attr", "(property) foo: boolean", "Foo docs");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoJsxNamespacedIntrinsic, TestQuickInfoJsxNamespacedIntrinsic);

// quickInfoLink10_test.go

// quickInfoLink10_test.go
static void TestQuickInfoLink10(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/**
 * start {@link https://vscode.dev/ | end}
 */
const /**/a = () => 1;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoLink10, TestQuickInfoLink10);

// quickInfoLink11_test.go

// quickInfoLink11_test.go
static void TestQuickInfoLink11(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/**
 * {@link https://vscode.dev}
 * [link text]{https://vscode.dev}
 * {@link https://vscode.dev|link text}
 * {@link https://vscode.dev link text}
 */
function f() {}

/**/f();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoLink11, TestQuickInfoLink11);

// quickInfoLink2_test.go

// quickInfoLink2_test.go
static void TestQuickInfoLink2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @checkJs: true
// @Filename: quickInfoLink2.js
/**
 * @typedef AdditionalWallabyConfig/**/ Additional valid Wallaby config properties
 * that aren't defined in {@link IWallabyConfig}.
 * @property {boolean} autoDetect
 */)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoLink2, TestQuickInfoLink2);

// quickInfoLink3_test.go

// quickInfoLink3_test.go
static void TestQuickInfoLink3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo<T> {
    /**
     * {@link Foo}
     * {@link Foo<T>}
     * {@link Foo<Array<X>>}
     * {@link Foo<>}
     * {@link Foo>}
     * {@link Foo<}
     */
    bar/**/(){}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoLink3, TestQuickInfoLink3);

// quickInfoLink4_test.go

// quickInfoLink4_test.go
static void TestQuickInfoLink4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type A = 1 | 2;

switch (0 as A) {
	/** {@link /**/A} */
	case 1:
	case 2:
    break;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoLink4, TestQuickInfoLink4);

// quickInfoLink5_test.go

// quickInfoLink5_test.go
static void TestQuickInfoLink5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(const A = 123;
/**
 *  See {@link A| constant A} instead
 */
const /**/B = 456;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoLink5, TestQuickInfoLink5);

// quickInfoLink6_test.go

// quickInfoLink6_test.go
static void TestQuickInfoLink6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(const A = 123;
/**
 *  See {@link A |constant A} instead
 */
const /**/B = 456;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoLink6, TestQuickInfoLink6);

// quickInfoLink7_test.go

// quickInfoLink7_test.go
static void TestQuickInfoLink7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/**
 * See {@link |       } instead
 */
const /**/B = 456;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoLink7, TestQuickInfoLink7);

// quickInfoLink8_test.go

// quickInfoLink8_test.go
static void TestQuickInfoLink8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(const A = 123;
/**
 * See {@link A | constant A} instead
 */
const /**/B = 456;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoLink8, TestQuickInfoLink8);

// quickInfoLink9_test.go

// quickInfoLink9_test.go
static void TestQuickInfoLink9(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type Foo = {
    /**
     * Text before {@link /**/a} text after
     */
    c: (a: number) => void;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoLink9, TestQuickInfoLink9);

// quickInfoLinkCodePlain_test.go

// quickInfoLinkCodePlain_test.go
static void TestQuickInfoLinkCodePlain(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(export class C {
     /**
      * @deprecated Use {@linkplain PerspectiveCamera#setFocalLength .setFocalLength()} and {@linkcode PerspectiveCamera#filmGauge .filmGauge} instead.
      */
    m() { }
}
new C().m/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoLinkCodePlain, TestQuickInfoLinkCodePlain);

// quickInfoMappedSpreadTypes_test.go

// quickInfoMappedSpreadTypes_test.go
static void TestQuickInfoMappedSpreadTypes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Foo {
    /** Doc */
    bar: number;
}

const f: Foo = { bar: 0 };
f./*f*/bar;

const f2: { [TKey in keyof Foo]: string } = { bar: "0" };
f2./*f2*/bar;

const f3 = { ...f };
f3./*f3*/bar;

const f4 = { ...f2 };
f4./*f4*/bar;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "f");
		f->VerifyQuickInfoIs(t, "(property) Foo.bar: number", "Doc");
		f->GoToMarker(t, "f2");
		f->VerifyQuickInfoIs(t, "(property) bar: string", "Doc");
		f->GoToMarker(t, "f3");
		f->VerifyQuickInfoIs(t, "(property) Foo.bar: number", "Doc");
		f->GoToMarker(t, "f4");
		f->VerifyQuickInfoIs(t, "(property) bar: string", "Doc");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoMappedSpreadTypes, TestQuickInfoMappedSpreadTypes);

// quickInfoMappedTypeMethods_test.go

// quickInfoMappedTypeMethods_test.go
static void TestQuickInfoMappedTypeMethods(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type M = { [K in 'one']: any };
const x: M = {
  /**/one() {}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "(property) one: any", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoMappedTypeMethods, TestQuickInfoMappedTypeMethods);

// quickInfoMappedTypeRecursiveInference_test.go

// quickInfoMappedTypeRecursiveInference_test.go
static void TestQuickInfoMappedTypeRecursiveInference(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: test.ts
interface A { a: A }
declare let a: A;
type Deep<T> = { [K in keyof T]: Deep<T[K]> }
declare function foo<T>(deep: Deep<T>): T;
const out/*1*/ = foo/*2*/(a);
out.a/*3*/
out.a.a/*4*/
out.a.a.a.a.a.a.a/*5*/

interface B { [s: string]: B }
declare let b: B;
const oub/*6*/ = foo/*7*/(b);
oub.b/*8*/
oub.b.b/*9*/
oub.b.a.n.a.n.a/*10*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", R"TS(const out: {
    a: {
        a: ...;
    };
})TS", "");
		f->VerifyQuickInfoAt(t, "2", R"TS(function foo<{
    a: {
        a: ...;
    };
}>(deep: Deep<{
    a: {
        a: ...;
    };
}>): {
    a: {
        a: ...;
    };
})TS", "");
		f->VerifyQuickInfoAt(t, "3", R"TS((property) a: {
    a: {
        a: ...;
    };
})TS", "");
		f->VerifyQuickInfoAt(t, "4", R"TS((property) a: {
    a: {
        a: ...;
    };
})TS", "");
		f->VerifyQuickInfoAt(t, "5", R"TS((property) a: {
    a: {
        a: ...;
    };
})TS", "");
		f->VerifyQuickInfoAt(t, "6", R"TS(const oub: {
    [x: string]: ...;
})TS", "");
		f->VerifyQuickInfoAt(t, "7", R"TS(function foo<{
    [x: string]: ...;
}>(deep: Deep<{
    [x: string]: ...;
}>): {
    [x: string]: ...;
})TS", "");
		f->VerifyQuickInfoAt(t, "8", R"TS({
    [x: string]: ...;
})TS", "");
		f->VerifyQuickInfoAt(t, "9", R"TS({
    [x: string]: ...;
})TS", "");
		f->VerifyQuickInfoAt(t, "10", R"TS({
    [x: string]: ...;
})TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoMappedTypeRecursiveInference, TestQuickInfoMappedTypeRecursiveInference);

// quickInfoMappedType_test.go

// quickInfoMappedType_test.go
static void TestQuickInfoMappedType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(interface I {
  /** m documentation */ m(): void;
}
declare const o: { [K in keyof I]: number };
o.m/*0*/;

declare const p: { [K in keyof I]: I[K] };
p.m/*1*/;

declare const q: Pick<I, "m">;
q.m/*2*/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "0", "(property) m: number", "m documentation");
		f->VerifyQuickInfoAt(t, "1", "(method) m(): void", "m documentation");
		f->VerifyQuickInfoAt(t, "2", "(method) m(): void", "m documentation");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoMappedType, TestQuickInfoMappedType);

// quickInfoMeaning_test.go

// quickInfoMeaning_test.go
static void TestQuickInfoMeaning(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
// @module: commonjs
// @Filename: foo.d.ts
declare const [|/*foo_value_declaration*/foo: number|];
[|declare module "foo_module" {
    interface /*foo_type_declaration*/I { x: number; y: number }
    export = I;
}|]
// @Filename: foo_user.ts
///<reference path="foo.d.ts" />
[|import foo = require("foo_module");|]
const x = foo/*foo_value*/;
const i: foo/*foo_type*/ = { x: 1, y: 2 };
// @Filename: bar.d.ts
[|declare interface /*bar_type_declaration*/bar { x: number; y: number }|]
[|declare module "bar_module" {
    const /*bar_value_declaration*/x: number;
    export = x;
}|]
// @Filename: bar_user.ts
///<reference path="bar.d.ts" />
[|import bar = require("bar_module");|]
const x = bar/*bar_value*/;
const i: bar/*bar_type*/ = { x: 1, y: 2 };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyWorkspaceSymbol(t, std::vector<std::shared_ptr<fourslash::VerifyWorkspaceSymbolCase>>{std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "foo", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "foo", .Kind = lsproto::SymbolKindVariable, .Location = f->Ranges()[0]->LSLocation()}), std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "foo", .Kind = lsproto::SymbolKindVariable, .Location = f->Ranges()[2]->LSLocation()}), std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "foo_module", .Kind = lsproto::SymbolKindNamespace, .Location = f->Ranges()[1]->LSLocation()})}), .Preferences = nullptr})});
		f->GoToMarker(t, "foo_value");
		f->VerifyQuickInfoIs(t, "const foo: number", "");
		f->GoToMarker(t, "foo_type");
		f->VerifyQuickInfoIs(t, R"TS((alias) interface foo
import foo = require("foo_module"))TS", "");
		f->VerifyWorkspaceSymbol(t, std::vector<std::shared_ptr<fourslash::VerifyWorkspaceSymbolCase>>{std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "bar", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "bar", .Kind = lsproto::SymbolKindInterface, .Location = f->Ranges()[3]->LSLocation()}), std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "bar", .Kind = lsproto::SymbolKindVariable, .Location = f->Ranges()[5]->LSLocation()}), std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "bar_module", .Kind = lsproto::SymbolKindNamespace, .Location = f->Ranges()[4]->LSLocation()})}), .Preferences = nullptr})});
		f->GoToMarker(t, "bar_value");
		f->VerifyQuickInfoIs(t, R"TS((alias) const bar: number
import bar = require("bar_module"))TS", "");
		f->GoToMarker(t, "bar_type");
		f->VerifyQuickInfoIs(t, "interface bar", "");
		f->VerifyBaselineGoToDefinition(t, false, {"foo_value", "foo_type", "bar_value", "bar_type"});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoMeaning, TestQuickInfoMeaning);

// quickInfoMergedAlias_test.go

// quickInfoMergedAlias_test.go
static void TestQuickInfoMergedAlias(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: /a.ts
/**
 * A function
 */
export function foo/*1*/() {}
// @filename: /b.ts
import { foo/*2*/ } from './a';
export { foo/*3*/ };

/**
 * A type
 */
type foo/*4*/ = number;

foo/*5*/()
let x1: foo/*6*/;
// @filename: /c.ts
import { foo/*7*/ } from './b';

/**
 * A namespace
 */
namespace foo/*8*/ {
    export type bar = string[];
}

foo/*9*/()
let x1: foo/*10*/;
let x2: foo/*11*/.bar;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoMergedAlias, TestQuickInfoMergedAlias);

// quickInfoModuleVariables_test.go

// quickInfoModuleVariables_test.go
static void TestQuickInfoModuleVariables(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(var x = 1;
namespace M {
    export var x = 2;
    console.log(/*1*/x); // 2
}
namespace M {
    console.log(/*2*/x); // 2
}
namespace M {
    var x = 3;
    console.log(/*3*/x); // 3
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "var M.x: number", "");
		f->VerifyQuickInfoAt(t, "2", "var M.x: number", "");
		f->VerifyQuickInfoAt(t, "3", "var x: number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoModuleVariables, TestQuickInfoModuleVariables);

// quickInfoNarrowedTypeOfAliasSymbol_test.go

// quickInfoNarrowedTypeOfAliasSymbol_test.go
static void TestQuickInfoNarrowedTypeOfAliasSymbol(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @Filename: modules.ts
export declare const someEnv: string | undefined;
// @Filename: app.ts
import { someEnv } from "./modules";
declare function isString(v: any): v is string;

if (isString(someEnv)) {
  someEnv/*1*/.charAt(0);
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "app.ts");
		f->GoToMarker(t, "1");
		f->VerifyQuickInfoIs(t, R"TS((alias) const someEnv: string
import someEnv)TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoNarrowedTypeOfAliasSymbol, TestQuickInfoNarrowedTypeOfAliasSymbol);

// quickInfoNestedExportEqualExportDefault_test.go

// quickInfoNestedExportEqualExportDefault_test.go
static void TestQuickInfoNestedExportEqualExportDefault(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(export = (state, messages) => {
   export/*1*/ default/*2*/ {
   }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoNestedExportEqualExportDefault, TestQuickInfoNestedExportEqualExportDefault);

// quickInfoNestedGenericCalls_test.go

// quickInfoNestedGenericCalls_test.go
static void TestQuickInfoNestedGenericCalls(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
/*1*/m({ foo: /*2*/$("foo") });
m({ foo: /*3*/$("foo") });
declare const m: <S extends string>(s: { [_ in S]: { $: NoInfer<S> } }) => void
declare const $: <S, T extends S>(s: T) => { $: S }
type NoInfer<T> = [T][T extends any ? 0 : never];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", R"TS(const m: <"foo">(s: {
    foo: {
        $: "foo";
    };
}) => void)TS", "");
		f->VerifyQuickInfoAt(t, "2", R"TS(const $: <unknown, string>(s: string) => {
    $: unknown;
})TS", "");
		f->VerifyQuickInfoAt(t, "3", R"TS(const $: <unknown, string>(s: string) => {
    $: unknown;
})TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoNestedGenericCalls, TestQuickInfoNestedGenericCalls);

// quickInfoObjectTypeMultiline_test.go

// quickInfoObjectTypeMultiline_test.go
static void TestQuickInfoObjectTypeMultiline(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
type X/*1*/ = {
    a: number
    b: string
    c: C
}
type C = {}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoObjectTypeMultiline, TestQuickInfoObjectTypeMultiline);

// quickInfoOfGenericTypeAssertions1_test.go

// quickInfoOfGenericTypeAssertions1_test.go
static void TestQuickInfoOfGenericTypeAssertions1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f<T>(x: T): T { return null; }
var /*1*/r = <T>(x: T) => x;
var /*2*/r2 = < <T>(x: T) => T>f;
var a;
var /*3*/r3 = < <T>(x: <A>(y: A) => A) => T>a;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "var r: <T>(x: T) => T", "");
		f->VerifyQuickInfoAt(t, "2", "var r2: <T>(x: T) => T", "");
		f->VerifyQuickInfoAt(t, "3", "var r3: <T>(x: <A>(y: A) => A) => T", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOfGenericTypeAssertions1, TestQuickInfoOfGenericTypeAssertions1);

// quickInfoOfLablledForStatementIterator_test.go

// quickInfoOfLablledForStatementIterator_test.go
static void TestQuickInfoOfLablledForStatementIterator(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(label1: for(var /**/i = 0; i < 1; i++) { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyQuickInfoExists(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOfLablledForStatementIterator, TestQuickInfoOfLablledForStatementIterator);

// quickInfoOfStringPropertyNames1_test.go

// quickInfoOfStringPropertyNames1_test.go
static void TestQuickInfoOfStringPropertyNames1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface foo {
    "foo bar": string;
}
var f: foo;
var /*1*/r = f['foo bar'];
class bar {
    'hello world': number;
    '1': string;
    constructor() {
        bar['hello world'] = 3;
    }
}
var b: bar;
var /*2*/r2 = b["hello world"];
var /*3*/r4 = b['1'];
var /*4*/r5 = b[1];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "var r: string", "");
		f->VerifyQuickInfoAt(t, "2", "var r2: number", "");
		f->VerifyQuickInfoAt(t, "3", "var r4: string", "");
		f->VerifyQuickInfoAt(t, "4", "var r5: string", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOfStringPropertyNames1, TestQuickInfoOfStringPropertyNames1);

// quickInfoOnArgumentsInsideFunction_test.go

// quickInfoOnArgumentsInsideFunction_test.go
static void TestQuickInfoOnArgumentsInsideFunction(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(function foo(x: string) {
    return /*1*/arguments;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(local var) arguments: IArguments", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnArgumentsInsideFunction, TestQuickInfoOnArgumentsInsideFunction);

// quickInfoOnCatchVariable_test.go

// quickInfoOnCatchVariable_test.go
static void TestQuickInfoOnCatchVariable(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @strict: false
function f() {
   try { } catch (/**/e) { }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "(local var) e: any", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnCatchVariable, TestQuickInfoOnCatchVariable);

// quickInfoOnCircularTypes_test.go

// quickInfoOnCircularTypes_test.go
static void TestQuickInfoOnCircularTypes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface A { (): B; };
declare var a: A;
var xx = a();

interface B { (): C; };
declare var b: B;
var yy = b();

interface C { (): A; };
declare var c: C;
var zz = c();

x/*B*/x = y/*C*/y;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "B", "var xx: B", "");
		f->VerifyQuickInfoAt(t, "C", "var yy: C", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnCircularTypes, TestQuickInfoOnCircularTypes);

// quickInfoOnClassMergedWithFunction_test.go

// quickInfoOnClassMergedWithFunction_test.go
static void TestQuickInfoOnClassMergedWithFunction(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace Test {
    class Mocked {
        myProp: string;
    }
    class Tester {
        willThrowError() {
            Mocked = Mocked || function () { // => Error: Invalid left-hand side of assignment expression.
                return { /**/myProp: "test" };
            };
        }
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "(property) myProp: string", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnClassMergedWithFunction, TestQuickInfoOnClassMergedWithFunction);

// quickInfoOnClosingJsx_test.go

// quickInfoOnClosingJsx_test.go
static void TestQuickInfoOnClosingJsx(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: foo.tsx
let x = <div>
    /*$*/</div >)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "$");
		f->VerifyNotQuickInfoExists(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnClosingJsx, TestQuickInfoOnClosingJsx);

// quickInfoOnConstructorWithGenericParameter_test.go

// quickInfoOnConstructorWithGenericParameter_test.go
static void TestQuickInfoOnConstructorWithGenericParameter(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    x: number;
}
class Foo<T> {
    y: T;
}
class A {
    foo() { }
}
class B extends A {
    constructor(a: Foo<I>, b: number) {
        super();
    }
}
var x = new /*2*/B(/*1*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "B(a: Foo<I>, b: number): B"});
		f->Insert(t, "null,");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "B(a: Foo<I>, b: number): B"});
		f->Insert(t, "10);");
		f->VerifyQuickInfoAt(t, "2", "constructor B(a: Foo<I>, b: number): B", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnConstructorWithGenericParameter, TestQuickInfoOnConstructorWithGenericParameter);

// quickInfoOnElementAccessInWriteLocation1_test.go

// quickInfoOnElementAccessInWriteLocation1_test.go
static void TestQuickInfoOnElementAccessInWriteLocation1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @exactOptionalPropertyTypes: true
declare const xx: { prop?: number };
xx['prop'/*1*/] = 1;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(property) prop?: number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnElementAccessInWriteLocation1, TestQuickInfoOnElementAccessInWriteLocation1);

// quickInfoOnElementAccessInWriteLocation2_test.go

// quickInfoOnElementAccessInWriteLocation2_test.go
static void TestQuickInfoOnElementAccessInWriteLocation2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @exactOptionalPropertyTypes: true
declare const xx: { prop?: number };
xx['prop'/*1*/] += 1;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(property) prop?: number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnElementAccessInWriteLocation2, TestQuickInfoOnElementAccessInWriteLocation2);

// quickInfoOnElementAccessInWriteLocation3_test.go

// quickInfoOnElementAccessInWriteLocation3_test.go
static void TestQuickInfoOnElementAccessInWriteLocation3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @exactOptionalPropertyTypes: true
declare const xx: { prop?: number };
xx['prop'/*1*/] ??= 1;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(property) prop?: number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnElementAccessInWriteLocation3, TestQuickInfoOnElementAccessInWriteLocation3);

// quickInfoOnElementAccessInWriteLocation4_test.go

// quickInfoOnElementAccessInWriteLocation4_test.go
static void TestQuickInfoOnElementAccessInWriteLocation4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @strict: true
interface Serializer {
  set value(v: string | number | boolean);
  get value(): string;
}
declare let box: Serializer;
box['value'/*1*/] = true;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(property) Serializer.value: string | number | boolean", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnElementAccessInWriteLocation4, TestQuickInfoOnElementAccessInWriteLocation4);

// quickInfoOnElementAccessInWriteLocation5_test.go

// quickInfoOnElementAccessInWriteLocation5_test.go
static void TestQuickInfoOnElementAccessInWriteLocation5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @strict: true
interface Serializer {
  set value(v: string | number);
  get value(): string;
}
declare let box: Serializer;
box['value'/*1*/] += 10;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(property) Serializer.value: string | number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnElementAccessInWriteLocation5, TestQuickInfoOnElementAccessInWriteLocation5);

// quickInfoOnErrorTypes1_test.go

// quickInfoOnErrorTypes1_test.go
static void TestQuickInfoOnErrorTypes1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var /*A*/f: {
    x: number;
    <
};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "A", R"TS(var f: {
    (): any;
    x: number;
})TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnErrorTypes1, TestQuickInfoOnErrorTypes1);

// quickInfoOnExpandoLikePropertyWithSetterDeclarationJs1_test.go

// quickInfoOnExpandoLikePropertyWithSetterDeclarationJs1_test.go
static void TestQuickInfoOnExpandoLikePropertyWithSetterDeclarationJs1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @checkJs: true
// @filename: index.js
const x = {};

Object.defineProperty(x, "foo", {
  /** @param {number} v */
  set(v) {},
});

x.foo/**/ = 1;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "(property) x.foo: number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnExpandoLikePropertyWithSetterDeclarationJs1, TestQuickInfoOnExpandoLikePropertyWithSetterDeclarationJs1);

// quickInfoOnExpandoLikePropertyWithSetterDeclarationJs2_test.go

// quickInfoOnExpandoLikePropertyWithSetterDeclarationJs2_test.go
static void TestQuickInfoOnExpandoLikePropertyWithSetterDeclarationJs2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @checkJs: true
// @filename: index.js
const obj = {};
let val = 10;
Object.defineProperty(obj, "a", {
  configurable: true,
  enumerable: true,
  set(v) {
    val = v;
  },
});

obj.a/**/ = 100;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "(property) obj.a: any", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnExpandoLikePropertyWithSetterDeclarationJs2, TestQuickInfoOnExpandoLikePropertyWithSetterDeclarationJs2);

// quickInfoOnFunctionPropertyReturnedFromGenericFunction1_test.go

// quickInfoOnFunctionPropertyReturnedFromGenericFunction1_test.go
static void TestQuickInfoOnFunctionPropertyReturnedFromGenericFunction1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function createProps<T>(t: T) {
  function getProps() {}
  function createVariants() {}

  getProps.createVariants = createVariants;
  return getProps;
}

createProps({})./**/createVariants();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "(property) getProps<{}>.createVariants: () => void", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnFunctionPropertyReturnedFromGenericFunction1, TestQuickInfoOnFunctionPropertyReturnedFromGenericFunction1);

// quickInfoOnFunctionPropertyReturnedFromGenericFunction2_test.go

// quickInfoOnFunctionPropertyReturnedFromGenericFunction2_test.go
static void TestQuickInfoOnFunctionPropertyReturnedFromGenericFunction2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function createProps<T>(t: T) {
  const getProps = function() {}
  const createVariants = function() {}

  getProps.createVariants = createVariants;
  return getProps;
}

createProps({})./**/createVariants();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "(property) getProps<{}>.createVariants: () => void", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnFunctionPropertyReturnedFromGenericFunction2, TestQuickInfoOnFunctionPropertyReturnedFromGenericFunction2);

// quickInfoOnFunctionPropertyReturnedFromGenericFunction3_test.go

// quickInfoOnFunctionPropertyReturnedFromGenericFunction3_test.go
static void TestQuickInfoOnFunctionPropertyReturnedFromGenericFunction3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function createProps<T>(t: T) {
  const getProps = () => {}
  const createVariants = () => {}

  getProps.createVariants = createVariants;
  return getProps;
}

createProps({})./**/createVariants();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "(property) getProps<{}>.createVariants: () => void", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnFunctionPropertyReturnedFromGenericFunction3, TestQuickInfoOnFunctionPropertyReturnedFromGenericFunction3);

// quickInfoOnGenericClass_test.go

// quickInfoOnGenericClass_test.go
static void TestQuickInfoOnGenericClass(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Contai/**/ner<T> {
    x: T;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "class Container<T>", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnGenericClass, TestQuickInfoOnGenericClass);

// quickInfoOnGenericWithConstraints1_test.go

// quickInfoOnGenericWithConstraints1_test.go
static void TestQuickInfoOnGenericWithConstraints1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(interface Fo/*1*/o<T/*2*/T extends Date> {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "interface Foo<TT extends Date>", "");
		f->VerifyQuickInfoAt(t, "2", "(type parameter) TT in Foo<TT extends Date>", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnGenericWithConstraints1, TestQuickInfoOnGenericWithConstraints1);

// quickInfoOnInternalAliases_test.go

// quickInfoOnInternalAliases_test.go
static void TestQuickInfoOnInternalAliases(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(/** Module comment*/
export namespace m1 {
    /** m2 comments*/
    export namespace m2 {
        /** class comment;*/
        export class /*1*/c {
        };
    }
    export function foo() {
    }
}
/**This is on import declaration*/
import /*2*/internalAlias = m1.m2./*3*/c;
var /*4*/newVar = new /*5*/internalAlias();
var /*6*/anotherAliasVar = /*7*/internalAlias;
import /*8*/internalFoo = m1./*9*/foo;
var /*10*/callVar = /*11*/internalFoo();
var /*12*/anotherAliasFoo = /*13*/internalFoo;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "class m1.m2.c", "class comment;");
		f->VerifyQuickInfoAt(t, "2", R"TS((alias) class internalAlias
import internalAlias = m1.m2.c)TS", "This is on import declaration");
		f->VerifyQuickInfoAt(t, "3", "class m1.m2.c", "class comment;");
		f->VerifyQuickInfoAt(t, "4", "var newVar: internalAlias", "");
		f->VerifyQuickInfoAt(t, "5", R"TS((alias) new internalAlias(): internalAlias
import internalAlias = m1.m2.c)TS", "This is on import declaration");
		f->VerifyQuickInfoAt(t, "6", "var anotherAliasVar: typeof internalAlias", "");
		f->VerifyQuickInfoAt(t, "7", R"TS((alias) class internalAlias
import internalAlias = m1.m2.c)TS", "This is on import declaration");
		f->VerifyQuickInfoAt(t, "8", R"TS((alias) function internalFoo(): void
import internalFoo = m1.foo)TS", "");
		f->VerifyQuickInfoAt(t, "9", "function m1.foo(): void", "");
		f->VerifyQuickInfoAt(t, "10", "var callVar: void", "");
		f->VerifyQuickInfoAt(t, "11", R"TS((alias) internalFoo(): void
import internalFoo = m1.foo)TS", "");
		f->VerifyQuickInfoAt(t, "12", "var anotherAliasFoo: () => void", "");
		f->VerifyQuickInfoAt(t, "13", R"TS((alias) function internalFoo(): void
import internalFoo = m1.foo)TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnInternalAliases, TestQuickInfoOnInternalAliases);

// quickInfoOnJsxIntrinsicDeclaredUsingCatchCallIndexSignature_test.go

// quickInfoOnJsxIntrinsicDeclaredUsingCatchCallIndexSignature_test.go
static void TestQuickInfoOnJsxIntrinsicDeclaredUsingCatchCallIndexSignature(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @jsx: react
// @filename: /a.tsx
declare namespace JSX {
  interface IntrinsicElements { [elemName: string]: any; }
}
</**/div class="democlass" />;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnJsxIntrinsicDeclaredUsingCatchCallIndexSignature, TestQuickInfoOnJsxIntrinsicDeclaredUsingCatchCallIndexSignature);

// quickInfoOnJsxIntrinsicDeclaredUsingTemplateLiteralTypeSignatures_test.go

// quickInfoOnJsxIntrinsicDeclaredUsingTemplateLiteralTypeSignatures_test.go
static void TestQuickInfoOnJsxIntrinsicDeclaredUsingTemplateLiteralTypeSignatures(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((((((std::string(R"TS(// @jsx: react
// @filename: /a.tsx
declare namespace JSX {
  interface IntrinsicElements {
    [k: )TS") + "`") + std::string(R"TS(foo${string})TS")) + std::string("`")) + std::string(R"TS(]: any;
    [k: )TS")) + std::string("`")) + std::string(R"TS(foobar${string})TS")) + std::string("`")) + std::string(R"TS(]: any;
  }
}
</*1*/foobaz />;
</*2*/foobarbaz />;)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnJsxIntrinsicDeclaredUsingTemplateLiteralTypeSignatures, TestQuickInfoOnJsxIntrinsicDeclaredUsingTemplateLiteralTypeSignatures);

// quickInfoOnJsxNamespacedNameWithDoc1_test.go

// quickInfoOnJsxNamespacedNameWithDoc1_test.go
static void TestQuickInfoOnJsxNamespacedNameWithDoc1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @jsx: react
// @Filename: /types.d.ts
declare namespace JSX {
  interface IntrinsicElements {
    'my-el': {
      /** This appears */
      foo: string;

      /** This also appears */
      'prop:foo': string;
    };
  }
}
// @filename: /a.tsx
<my-el /*1*/prop:foo="bar" /*2*/foo="baz" />)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(property) 'prop:foo': string", "This also appears");
		f->VerifyQuickInfoAt(t, "2", "(property) foo: string", "This appears");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnJsxNamespacedNameWithDoc1, TestQuickInfoOnJsxNamespacedNameWithDoc1);

// quickInfoOnJsxNamespacedName_test.go

// quickInfoOnJsxNamespacedName_test.go
static void TestQuickInfoOnJsxNamespacedName(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @jsx: react
// @Filename: /types.d.ts
declare namespace JSX {
    interface IntrinsicElements { ['a:b']: { a: string }; }
}
// @filename: /a.tsx
</**/a:b a="accepted" b="rejected" />;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnJsxNamespacedName, TestQuickInfoOnJsxNamespacedName);

// quickInfoOnMergedInterfacesWithIncrementalEdits_test.go

// quickInfoOnMergedInterfacesWithIncrementalEdits_test.go
static void TestQuickInfoOnMergedInterfacesWithIncrementalEdits(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: false
namespace MM {
    interface B<T> {
        foo: number;
    }
    interface B<T> {
        bar: string;
    }
    var b: B<string>;
    var r3 = b.foo; // number
    var r/*2*/4 = b.b/*1*/ar; // string
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifyQuickInfoIs(t, "(property) B<string>.bar: string", "");
		f->DeleteAtCaret(t, 1);
		f->Insert(t, "z");
		f->VerifyQuickInfoIs(t, "any", "");
		f->VerifyNumberOfErrorsInCurrentFile(t, 1);
		f->Backspace(t, 1);
		f->Insert(t, "a");
		f->VerifyQuickInfoIs(t, "(property) B<string>.bar: string", "");
		f->GoToMarker(t, "2");
		f->VerifyQuickInfoIs(t, "var r4: string", "");
		f->VerifyNoErrors(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnMergedInterfacesWithIncrementalEdits, TestQuickInfoOnMergedInterfacesWithIncrementalEdits);

// quickInfoOnMergedInterfaces_test.go

// quickInfoOnMergedInterfaces_test.go
static void TestQuickInfoOnMergedInterfaces(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace M {
    interface A<T> {
        (): string;
        (x: T): T;
    }
    interface A<T> {
        (x: T, y: number): T;
        <U>(x: U, y: T): U;
    }
    var a: A<boolean>;
    var r = a();
    var r2 = a(true);
    var r3 = a(true, 2);
    var /*1*/r4 = a(1, true);
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "var r4: number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnMergedInterfaces, TestQuickInfoOnMergedInterfaces);

// quickInfoOnMergedModule_test.go

// quickInfoOnMergedModule_test.go
static void TestQuickInfoOnMergedModule(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @strict: false
namespace M2 {
    export interface A {
        foo: string;
    }
    var a: A;
    var r = a.foo + a.bar;
}
namespace M2 {
    export interface A {
        bar: number;
    }
    var a: A;
    var r = a.fo/*1*/o + a.bar;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(property) M2.A.foo: string", "");
		f->VerifyNoErrors(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnMergedModule, TestQuickInfoOnMergedModule);

// quickInfoOnMethodOfImportEquals_test.go

// quickInfoOnMethodOfImportEquals_test.go
static void TestQuickInfoOnMethodOfImportEquals(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.d.ts
declare class C<T> {
    m(): void;
}
export = C;
// @Filename: /b.ts
import C = require("./a");
declare var x: C<number>;
x./**/m;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "(method) C<number>.m(): void", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnMethodOfImportEquals, TestQuickInfoOnMethodOfImportEquals);

// quickInfoOnNarrowedTypeInModule_test.go

// quickInfoOnNarrowedTypeInModule_test.go
static void TestQuickInfoOnNarrowedTypeInModule(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @strict: false
var strOrNum: string | number;
namespace m {
    var nonExportedStrOrNum: string | number;
    export var exportedStrOrNum: string | number;
    var num: number;
    var str: string;
    if (typeof /*1*/nonExportedStrOrNum === "number") {
        num = /*2*/nonExportedStrOrNum;
    }
    else {
        str = /*3*/nonExportedStrOrNum.length;
    }
    if (typeof /*4*/exportedStrOrNum === "number") {
        strOrNum = /*5*/exportedStrOrNum;
    }
    else {
        strOrNum = /*6*/exportedStrOrNum;
    }
}
if (typeof m./*7*/exportedStrOrNum === "number") {
    strOrNum = m./*8*/exportedStrOrNum;
}
else {
    strOrNum = m./*9*/exportedStrOrNum;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "var nonExportedStrOrNum: string | number", "");
		f->VerifyQuickInfoAt(t, "2", "var nonExportedStrOrNum: number", "");
		f->VerifyQuickInfoAt(t, "3", "var nonExportedStrOrNum: string", "");
		f->VerifyQuickInfoAt(t, "4", "var m.exportedStrOrNum: string | number", "");
		f->VerifyQuickInfoAt(t, "5", "var m.exportedStrOrNum: number", "");
		f->VerifyQuickInfoAt(t, "6", "var m.exportedStrOrNum: string", "");
		f->VerifyQuickInfoAt(t, "7", "var m.exportedStrOrNum: string | number", "");
		f->VerifyQuickInfoAt(t, "8", "var m.exportedStrOrNum: number", "");
		f->VerifyQuickInfoAt(t, "9", "var m.exportedStrOrNum: string", "");
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nonExportedStrOrNum", .Detail = std::string("var nonExportedStrOrNum: string | number")})}})}));
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nonExportedStrOrNum", .Detail = std::string("var nonExportedStrOrNum: number")})}})}));
		f->VerifyCompletions(t, "3", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "nonExportedStrOrNum", .Detail = std::string("var nonExportedStrOrNum: string")})}})}));
		f->VerifyCompletions(t, "4", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "exportedStrOrNum", .Detail = std::string("var exportedStrOrNum: string | number")})}})}));
		f->VerifyCompletions(t, "5", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "exportedStrOrNum", .Detail = std::string("var exportedStrOrNum: number")})}})}));
		f->VerifyCompletions(t, "6", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(std::vector<std::string>{}), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "exportedStrOrNum", .Detail = std::string("var exportedStrOrNum: string")})}})}));
		f->VerifyCompletions(t, "7", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "exportedStrOrNum", .Detail = std::string("var m.exportedStrOrNum: string | number")})}})}));
		f->VerifyCompletions(t, "8", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "exportedStrOrNum", .Detail = std::string("var m.exportedStrOrNum: number")})}})}));
		f->VerifyCompletions(t, "9", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "exportedStrOrNum", .Detail = std::string("var m.exportedStrOrNum: string")})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnNarrowedTypeInModule, TestQuickInfoOnNarrowedTypeInModule);

// quickInfoOnNarrowedType_test.go

// quickInfoOnNarrowedType_test.go
static void TestQuickInfoOnNarrowedType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strictNullChecks: true
function foo(strOrNum: string | number) {
    if (typeof /*1*/strOrNum === "number") {
        return /*2*/strOrNum;
    }
    else {
        return /*3*/strOrNum.length;
    }
}
function bar() {
   let s: string | undefined;
   /*4*/s;
   /*5*/s = "abc";
   /*6*/s;
}
class Foo {
    #privateProperty: string[] | null;
    constructor() {
        this.#privateProperty = null;
    }
    testMethod() {
        if (this.#privateProperty === null)
            return;
        this./*7*/#privateProperty;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(parameter) strOrNum: string | number", "");
		f->VerifyQuickInfoAt(t, "2", "(parameter) strOrNum: number", "");
		f->VerifyQuickInfoAt(t, "3", "(parameter) strOrNum: string", "");
		f->VerifyQuickInfoAt(t, "4", "let s: string | undefined", "");
		f->VerifyQuickInfoAt(t, "5", "let s: string | undefined", "");
		f->VerifyQuickInfoAt(t, "6", "let s: string", "");
		f->VerifyQuickInfoAt(t, "7", "(property) Foo.#privateProperty: string[]", "");
		f->VerifyCompletions(t, "1", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "strOrNum", .Detail = std::string("(parameter) strOrNum: string | number")})}})}));
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "strOrNum", .Detail = std::string("(parameter) strOrNum: number")})}})}));
		f->VerifyCompletions(t, "3", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "strOrNum", .Detail = std::string("(parameter) strOrNum: string")})}})}));
		f->VerifyCompletions(t, std::vector<std::string>{"4", "5"}, tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "s", .Detail = std::string("let s: string | undefined")})}})}));
		f->VerifyCompletions(t, "6", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "s", .Detail = std::string("let s: string")})}})}));
		f->VerifyCompletions(t, "7", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Includes = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "#privateProperty", .Detail = std::string("(property) Foo.#privateProperty: string[]")})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnNarrowedType, TestQuickInfoOnNarrowedType);

// quickInfoOnNewKeyword01_test.go

// quickInfoOnNewKeyword01_test.go
static void TestQuickInfoOnNewKeyword01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Cat {
  /**
   * NOTE: this constructor is private! Please use the factory function
   */
  private constructor() { }

  static makeCat() { new Cat(); }
}

ne/*1*/w Ca/*2*/t();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "constructor Cat(): Cat", "NOTE: this constructor is private! Please use the factory function");
		f->VerifyQuickInfoAt(t, "2", "constructor Cat(): Cat", "NOTE: this constructor is private! Please use the factory function");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnNewKeyword01, TestQuickInfoOnNewKeyword01);

// quickInfoOnObjectLiteralWithAccessors_test.go

// quickInfoOnObjectLiteralWithAccessors_test.go
static void TestQuickInfoOnObjectLiteralWithAccessors(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(function /*1*/makePoint(x: number) {
    return {
        b: 10,
        get x() { return x; },
        set x(a: number) { this.b = a; }
    };
};
var /*4*/point = makePoint(2);
var /*2*/x = point.x;
point./*3*/x = 30;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", R"TS(function makePoint(x: number): {
    b: number;
    x: number;
})TS", "");
		f->VerifyQuickInfoAt(t, "2", "var x: number", "");
		f->VerifyQuickInfoAt(t, "3", "(property) x: number", "");
		f->VerifyQuickInfoAt(t, "4", R"TS(var point: {
    b: number;
    x: number;
})TS", "");
		f->VerifyCompletions(t, "3", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "b", .Detail = std::string("(property) b: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "x", .Detail = std::string("(property) x: number")})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnObjectLiteralWithAccessors, TestQuickInfoOnObjectLiteralWithAccessors);

// quickInfoOnObjectLiteralWithOnlyGetter_test.go

// quickInfoOnObjectLiteralWithOnlyGetter_test.go
static void TestQuickInfoOnObjectLiteralWithOnlyGetter(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(function /*1*/makePoint(x: number) {
    return {
        get x() { return x; },
    };
};
var /*4*/point = makePoint(2);
var /*2*/x = point./*3*/x;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", R"TS(function makePoint(x: number): {
    readonly x: number;
})TS", "");
		f->VerifyQuickInfoAt(t, "2", "var x: number", "");
		f->VerifyQuickInfoAt(t, "4", R"TS(var point: {
    readonly x: number;
})TS", "");
		f->VerifyCompletions(t, "3", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "x", .Detail = std::string("(property) x: number")})}})}));
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnObjectLiteralWithOnlyGetter, TestQuickInfoOnObjectLiteralWithOnlyGetter);

// quickInfoOnObjectLiteralWithOnlySetter_test.go

// quickInfoOnObjectLiteralWithOnlySetter_test.go
static void TestQuickInfoOnObjectLiteralWithOnlySetter(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(function /*1*/makePoint(x: number) {
    return {
        b: 10,
        set x(a: number) { this.b = a; }
    };
};
var /*3*/point = makePoint(2);
point./*2*/x = 30;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCompletions(t, "2", tsu::ptr(fourslash::CompletionsExpectedList{.IsIncomplete = false, .ItemDefaults = std::make_shared<fourslash::CompletionsExpectedItemDefaults>(fourslash::CompletionsExpectedItemDefaults{.CommitCharacters = std::make_shared<std::vector<std::string>>(tsu::DefaultCommitCharacters), .EditRange = fourslash::Ignored{}}), .Items = std::make_shared<fourslash::CompletionsExpectedItems>(fourslash::CompletionsExpectedItems{.Exact = std::vector<fourslash::CompletionsExpectedItem>{std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "b", .Detail = std::string("(property) b: number")}), std::make_shared<lsproto::CompletionItem>(lsproto::CompletionItem{.Label = "x", .Detail = std::string("(property) x: number")})}})}));
		f->VerifyQuickInfoAt(t, "1", R"TS(function makePoint(x: number): {
    b: number;
    x: number;
})TS", "");
		f->VerifyQuickInfoAt(t, "2", "(property) x: number", "");
		f->VerifyQuickInfoAt(t, "3", R"TS(var point: {
    b: number;
    x: number;
})TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnObjectLiteralWithOnlySetter, TestQuickInfoOnObjectLiteralWithOnlySetter);

// quickInfoOnParameterProperties_test.go

// quickInfoOnParameterProperties_test.go
static void TestQuickInfoOnParameterProperties(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface IFoo {
  /** this is the name of blabla 
   *  - use blabla 
   *  @example blabla
   */
  name?: string;
}

// test1 should work
class Foo implements IFoo {
  //public name: string = '';
  constructor(
    public na/*1*/me: string, // documentation should leech and work ! 
  ) {
  }
}

// test2 work
class Foo2 implements IFoo {
  public na/*2*/me: string = ''; // documentation leeched and work ! 
  constructor(
    //public name: string,
  ) {
  }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnParameterProperties, TestQuickInfoOnParameterProperties);

// quickInfoOnPrivateConstructorCall_test.go

// quickInfoOnPrivateConstructorCall_test.go
static void TestQuickInfoOnPrivateConstructorCall(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class A {
    private constructor() {}
}
var x = new A(/*1*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoSignatureHelpForMarkers(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnPrivateConstructorCall, TestQuickInfoOnPrivateConstructorCall);

// quickInfoOnPropDeclaredUsingIndexSignatureOnInterfaceWithBase_test.go

// quickInfoOnPropDeclaredUsingIndexSignatureOnInterfaceWithBase_test.go
static void TestQuickInfoOnPropDeclaredUsingIndexSignatureOnInterfaceWithBase(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(interface P {}
interface B extends P {
  [k: string]: number;
}
declare const b: B;
b.t/*1*/est = 10;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(index) B[string]: number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnPropDeclaredUsingIndexSignatureOnInterfaceWithBase, TestQuickInfoOnPropDeclaredUsingIndexSignatureOnInterfaceWithBase);

// quickInfoOnPropertyAccessInWriteLocation1_test.go

// quickInfoOnPropertyAccessInWriteLocation1_test.go
static void TestQuickInfoOnPropertyAccessInWriteLocation1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @exactOptionalPropertyTypes: true
declare const xx: { prop?: number };
xx.prop/*1*/ = 1;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(property) prop?: number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnPropertyAccessInWriteLocation1, TestQuickInfoOnPropertyAccessInWriteLocation1);

// quickInfoOnPropertyAccessInWriteLocation2_test.go

// quickInfoOnPropertyAccessInWriteLocation2_test.go
static void TestQuickInfoOnPropertyAccessInWriteLocation2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @exactOptionalPropertyTypes: true
declare const xx: { prop?: number };
xx.prop/*1*/ += 1;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(property) prop?: number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnPropertyAccessInWriteLocation2, TestQuickInfoOnPropertyAccessInWriteLocation2);

// quickInfoOnPropertyAccessInWriteLocation3_test.go

// quickInfoOnPropertyAccessInWriteLocation3_test.go
static void TestQuickInfoOnPropertyAccessInWriteLocation3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @exactOptionalPropertyTypes: true
declare const xx: { prop?: number };
xx.prop/*1*/ ??= 1;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(property) prop?: number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnPropertyAccessInWriteLocation3, TestQuickInfoOnPropertyAccessInWriteLocation3);

// quickInfoOnPropertyAccessInWriteLocation4_test.go

// quickInfoOnPropertyAccessInWriteLocation4_test.go
static void TestQuickInfoOnPropertyAccessInWriteLocation4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @strict: true
interface Serializer {
  set value(v: string | number | boolean);
  get value(): string;
}
declare let box: Serializer;
box.value/*1*/ = true;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(property) Serializer.value: string | number | boolean", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnPropertyAccessInWriteLocation4, TestQuickInfoOnPropertyAccessInWriteLocation4);

// quickInfoOnPropertyAccessInWriteLocation5_test.go

// quickInfoOnPropertyAccessInWriteLocation5_test.go
static void TestQuickInfoOnPropertyAccessInWriteLocation5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @strict: true
interface Serializer {
  set value(v: string | number);
  get value(): string;
}
declare let box: Serializer;
box.value/*1*/ += 10;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(property) Serializer.value: string | number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnPropertyAccessInWriteLocation5, TestQuickInfoOnPropertyAccessInWriteLocation5);

// quickInfoOnProtectedConstructorCall_test.go

// quickInfoOnProtectedConstructorCall_test.go
static void TestQuickInfoOnProtectedConstructorCall(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class A {
    protected constructor() {}
}
var x = new A(/*1*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoSignatureHelpForMarkers(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnProtectedConstructorCall, TestQuickInfoOnProtectedConstructorCall);

// quickInfoOnThis2_test.go

// quickInfoOnThis2_test.go
static void TestQuickInfoOnThis2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Bar<T> {
    public explicitThis(this: this) {
        console.log(th/*1*/is);
    }
    public explicitClass(this: Bar<T>) {
        console.log(thi/*2*/s);
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "this: this", "");
		f->VerifyQuickInfoAt(t, "2", "this: Bar<T>", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnThis2, TestQuickInfoOnThis2);

// quickInfoOnThis3_test.go

// quickInfoOnThis3_test.go
static void TestQuickInfoOnThis3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(interface Restricted {
    n: number;
}
function implicitAny(x: number): void {
    return th/*1*/is;
}
function explicitVoid(th/*2*/is: void, x: number): void {
    return th/*3*/is;
}
function explicitInterface(th/*4*/is: Restricted): void {
    console.log(thi/*5*/s);
}
function explicitLiteral(th/*6*/is: { n: number }): void {
    console.log(th/*7*/is);
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "any", "");
		f->VerifyQuickInfoAt(t, "2", "(parameter) this: void", "");
		f->VerifyQuickInfoAt(t, "3", "this: void", "");
		f->VerifyQuickInfoAt(t, "4", "(parameter) this: Restricted", "");
		f->VerifyQuickInfoAt(t, "5", "this: Restricted", "");
		f->VerifyQuickInfoAt(t, "6", R"TS((parameter) this: {
    n: number;
})TS", "");
		f->VerifyQuickInfoAt(t, "7", R"TS(this: {
    n: number;
})TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnThis3, TestQuickInfoOnThis3);

// quickInfoOnThis4_test.go

// quickInfoOnThis4_test.go
static void TestQuickInfoOnThis4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface ContextualInterface {
    m: number;
    method(this: this, n: number);
}
let o: ContextualInterface = {
    m: 12,
    method(n) {
        let x = this/*1*/.m;
    }
}
interface ContextualInterface2 {
    (this: void, n: number): void;
}
let contextualInterface2: ContextualInterface2 = function (th/*2*/is, n) { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "this: ContextualInterface", "");
		f->VerifyQuickInfoAt(t, "2", "(parameter) this: void", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnThis4, TestQuickInfoOnThis4);

// quickInfoOnThis5_test.go

// quickInfoOnThis5_test.go
static void TestQuickInfoOnThis5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noImplicitThis: true
const foo = {
    num: 0,
    f() {
        type Y = typeof th/*1*/is;
        type Z = typeof th/*2*/is.num;
    },
    g(this: number) {
        type X = typeof th/*3*/is;
    }
}
class Foo {
    num = 0;
    f() {
        type Y = typeof th/*4*/is;
        type Z = typeof th/*5*/is.num;
    }
    g(this: number) {
        type X = typeof th/*6*/is;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnThis5, TestQuickInfoOnThis5);

// quickInfoOnThis_test.go

// quickInfoOnThis_test.go
static void TestQuickInfoOnThis(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Restricted {
    n: number;
}
function wrapper(wrapped: { (): void; }) { }
class Foo {
    n: number;
    prop1: th/*0*/is;
    public explicitThis(this: this) {
        wrapper(
            function explicitVoid(this: void) {
                console.log(th/*1*/is);
            }
        )
        console.log(th/*2*/is);
    }
    public explicitInterface(th/*3*/is: Restricted) {
        console.log(th/*4*/is);
    }
    public explicitClass(th/*5*/is: Foo) {
        console.log(th/*6*/is);
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "0", "this", "");
		f->VerifyQuickInfoAt(t, "1", "this: void", "");
		f->VerifyQuickInfoAt(t, "2", "this: this", "");
		f->VerifyQuickInfoAt(t, "3", "(parameter) this: Restricted", "");
		f->VerifyQuickInfoAt(t, "4", "this: Restricted", "");
		f->VerifyQuickInfoAt(t, "5", "(parameter) this: Foo", "");
		f->VerifyQuickInfoAt(t, "6", "this: Foo", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnThis, TestQuickInfoOnThis);

// quickInfoOnUnResolvedBaseConstructorSignature_test.go

// quickInfoOnUnResolvedBaseConstructorSignature_test.go
static void TestQuickInfoOnUnResolvedBaseConstructorSignature(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class baseClassWithConstructorParameterSpecifyingType {
    constructor(loading?: boolean) {
    }
}
class genericBaseClassInheritingConstructorFromBase<TValue> extends baseClassWithConstructorParameterSpecifyingType {
}
class classInheritingSpecializedClass extends genericBaseClassInheritingConstructorFromBase<string> {
}
new class/*1*/InheritingSpecializedClass();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifyQuickInfoExists(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnUnResolvedBaseConstructorSignature, TestQuickInfoOnUnResolvedBaseConstructorSignature);

// quickInfoOnUndefined_test.go

// quickInfoOnUndefined_test.go
static void TestQuickInfoOnUndefined(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(function foo(a: string) {
}
foo(/*1*/undefined);
var x = {
    undefined: 10
};
x./*2*/undefined = 30;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "var undefined", "");
		f->VerifyQuickInfoAt(t, "2", "(property) undefined: number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnUndefined, TestQuickInfoOnUndefined);

// quickInfoOnUnionPropertiesWithIdenticalJSDocComments01_test.go

// quickInfoOnUnionPropertiesWithIdenticalJSDocComments01_test.go
static void TestQuickInfoOnUnionPropertiesWithIdenticalJSDocComments01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((((((((((((((((((((((((((((((((((((((((((((((std::string(R"TS(export type DocumentFilter = {
    /** A language id, like )TS") + "`") + std::string(R"TS(typescript)TS")) + std::string("`")) + std::string(R"TS(. */
    language: string;
    /** A Uri [scheme](#Uri.scheme), like )TS")) + std::string("`")) + std::string(R"TS(file)TS")) + std::string("`")) + std::string(R"TS( or )TS")) + std::string("`")) + std::string(R"TS(untitled)TS")) + std::string("`")) + std::string(R"TS(. */
    scheme?: string;
    /** A glob pattern, like )TS")) + std::string("`")) + std::string(R"TS(*.{ts,js})TS")) + std::string("`")) + std::string(R"TS(. */
    pattern?: string;
} | {
    /** A language id, like )TS")) + std::string("`")) + std::string(R"TS(typescript)TS")) + std::string("`")) + std::string(R"TS(. */
    language?: string;
    /** A Uri [scheme](#Uri.scheme), like )TS")) + std::string("`")) + std::string(R"TS(file)TS")) + std::string("`")) + std::string(R"TS( or )TS")) + std::string("`")) + std::string(R"TS(untitled)TS")) + std::string("`")) + std::string(R"TS(. */
    scheme: string;
    /** A glob pattern, like )TS")) + std::string("`")) + std::string(R"TS(*.{ts,js})TS")) + std::string("`")) + std::string(R"TS(. */
    pattern?: string;
} | {
    /** A language id, like )TS")) + std::string("`")) + std::string(R"TS(typescript)TS")) + std::string("`")) + std::string(R"TS(. */
    language?: string;
    /** A Uri [scheme](#Uri.scheme), like )TS")) + std::string("`")) + std::string(R"TS(file)TS")) + std::string("`")) + std::string(R"TS( or )TS")) + std::string("`")) + std::string(R"TS(untitled)TS")) + std::string("`")) + std::string(R"TS(. */
    scheme?: string;
    /** A glob pattern, like )TS")) + std::string("`")) + std::string(R"TS(*.{ts,js})TS")) + std::string("`")) + std::string(R"TS(. */
    pattern: string;
};

declare let x: DocumentFilter;
x./**/language)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnUnionPropertiesWithIdenticalJSDocComments01, TestQuickInfoOnUnionPropertiesWithIdenticalJSDocComments01);

// quickInfoOnValueSymbolWithoutExportWithSameNameExportSymbol_test.go

// quickInfoOnValueSymbolWithoutExportWithSameNameExportSymbol_test.go
static void TestQuickInfoOnValueSymbolWithoutExportWithSameNameExportSymbol(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true

declare function num(): number
const /*1*/Unit = num()
export type Unit = number
const value = /*2*/Unit

function Fn() {}
export type Fn = () => void
/*3*/Fn()

// repro from #41897
const /*4*/X = 1;
export interface X {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "const Unit: number", "");
		f->VerifyQuickInfoAt(t, "2", "const Unit: number", "");
		f->VerifyQuickInfoAt(t, "3", "function Fn(): void", "");
		f->VerifyQuickInfoAt(t, "4", "const X: 1", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnValueSymbolWithoutExportWithSameNameExportSymbol, TestQuickInfoOnValueSymbolWithoutExportWithSameNameExportSymbol);

// quickInfoOnVarInArrowExpression_test.go

// quickInfoOnVarInArrowExpression_test.go
static void TestQuickInfoOnVarInArrowExpression(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(interface IMap<T> {
    [key: string]: T;
}
var map: IMap<string[]>;
var categories: string[];
each(categories, category => {
    var /*1*/changes = map[category];
    return each(changes, change => {
    });
});
function each<T>(items: T[], handler: (item: T) => void) { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(local var) changes: string[]", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoOnVarInArrowExpression, TestQuickInfoOnVarInArrowExpression);

// quickInfoParameter_skipThisParameter_test.go

// quickInfoParameter_skipThisParameter_test.go
static void TestQuickInfoParameter_skipThisParameter(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f(cb: (x: number) => void) {}
f(function(this: any, /**/x) {});)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "(parameter) x: number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoParameter_skipThisParameter, TestQuickInfoParameter_skipThisParameter);

// quickInfoPrivateIdentifierInTypeReferenceNoCrash1_test.go

// quickInfoPrivateIdentifierInTypeReferenceNoCrash1_test.go
static void TestQuickInfoPrivateIdentifierInTypeReferenceNoCrash1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @target: esnext
class Foo {
  #prop: string = "";

  method() {
    const test: Foo.#prop/*1*/ = "";
  }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoPrivateIdentifierInTypeReferenceNoCrash1, TestQuickInfoPrivateIdentifierInTypeReferenceNoCrash1);

// quickInfoPropertyTag_test.go

// quickInfoPropertyTag_test.go
static void TestQuickInfoPropertyTag(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
/**
 * @typedef I
 * @property {number} x Doc
 *                      More doc
 */

/** @type {I} */
const obj = { /**/x: 10 };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "(property) x: number", R"TS(Doc
More doc)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoPropertyTag, TestQuickInfoPropertyTag);

// quickInfoRootSymbolJSDocAggregation_test.go

// quickInfoRootSymbolJSDocAggregation_test.go
static void TestQuickInfoRootSymbolJSDocAggregation(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
declare const distinct: {
    /** first */
    a: number;
} & {
    /** second */
    a: number;
};

declare const duplicate: {
    /** same */
    a: number;
} & {
    /** same */
    a: number;
} & {
    /** third */
    a: number;
};

declare const mixed: {
    /** first */
    a: number;
} & {
    /** second */
    a: number;
} & {
    /** first */
    a: number;
};

distinct./*distinct*/a
duplicate./*duplicate*/a
mixed./*mixed*/a
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "distinct", "(property) a: number", R"TS(first
second)TS");
		f->VerifyQuickInfoAt(t, "duplicate", "(property) a: number", R"TS(same
third)TS");
		f->VerifyQuickInfoAt(t, "mixed", "(property) a: number", R"TS(first
second)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoRootSymbolJSDocAggregation, TestQuickInfoRootSymbolJSDocAggregation);

// quickInfoSalsaMethodsOnAssignedFunctionExpressions_test.go

// quickInfoSalsaMethodsOnAssignedFunctionExpressions_test.go
static void TestQuickInfoSalsaMethodsOnAssignedFunctionExpressions(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: something.js
var C = function () { }
/**
 * The prototype method.
 * @param {string} a Parameter definition.
 */
function f(a) {}
C.prototype.m = f;

var x = new C();
x/*1*/.m();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoSalsaMethodsOnAssignedFunctionExpressions, TestQuickInfoSalsaMethodsOnAssignedFunctionExpressions);

// quickInfoSatisfiesTag_test.go

// quickInfoSatisfiesTag_test.go
static void TestQuickInfoSatisfiesTag(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noEmit: true
// @allowJS: true
// @checkJs: true
// @filename: /a.js
/** @satisfies {number} comment */
const /*1*/a = 1;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoSatisfiesTag, TestQuickInfoSatisfiesTag);

// quickInfoShowsGenericSpecialization_test.go

// quickInfoShowsGenericSpecialization_test.go
static void TestQuickInfoShowsGenericSpecialization(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class A<T> { }
var /**/foo = new A<number>();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "var foo: A<number>", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoShowsGenericSpecialization, TestQuickInfoShowsGenericSpecialization);

// quickInfoSignatureOptionalParameterFromUnion1_test.go

// quickInfoSignatureOptionalParameterFromUnion1_test.go
static void TestQuickInfoSignatureOptionalParameterFromUnion1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: false
declare const optionals:
  | ((a?: { a: true }) => unknown)
  | ((b?: { b: true }) => unknown);

/**/optionals();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", R"TS(const optionals: (arg0?: {
    a: true;
} & {
    b: true;
}) => unknown)TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoSignatureOptionalParameterFromUnion1, TestQuickInfoSignatureOptionalParameterFromUnion1);

// quickInfoSignatureRestParameterFromUnion1_test.go

// quickInfoSignatureRestParameterFromUnion1_test.go
static void TestQuickInfoSignatureRestParameterFromUnion1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare const rest:
  | ((v: { a: true }, ...rest: string[]) => unknown)
  | ((v: { b: true }) => unknown);

/**/rest({ a: true, b: true }, "foo", "bar");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", R"TS(const rest: (v: {
    a: true;
} & {
    b: true;
}, ...rest: string[]) => unknown)TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoSignatureRestParameterFromUnion1, TestQuickInfoSignatureRestParameterFromUnion1);

// quickInfoSignatureRestParameterFromUnion2_test.go

// quickInfoSignatureRestParameterFromUnion2_test.go
static void TestQuickInfoSignatureRestParameterFromUnion2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: false
declare const rest:
  | ((a?: { a: true }, ...rest: string[]) => unknown)
  | ((b?: { b: true }) => unknown);

/**/rest({ a: true, b: true }, "foo", "bar");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", R"TS(const rest: (arg0?: {
    a: true;
} & {
    b: true;
}, ...rest: string[]) => unknown)TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoSignatureRestParameterFromUnion2, TestQuickInfoSignatureRestParameterFromUnion2);

// quickInfoSignatureRestParameterFromUnion3_test.go

// quickInfoSignatureRestParameterFromUnion3_test.go
static void TestQuickInfoSignatureRestParameterFromUnion3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare const fn:
  | ((a: { x: number }, b: { x: number }) => number)
  | ((...a: { y: number }[]) => number);

/**/fn();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", R"TS(const fn: (a: {
    x: number;
} & {
    y: number;
}, b: {
    x: number;
} & {
    y: number;
}, ...args: {
    y: number;
}[]) => number)TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoSignatureRestParameterFromUnion3, TestQuickInfoSignatureRestParameterFromUnion3);

// quickInfoSignatureRestParameterFromUnion4_test.go

// quickInfoSignatureRestParameterFromUnion4_test.go
static void TestQuickInfoSignatureRestParameterFromUnion4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare const fn:
  | ((a?: { x: number }, b?: { x: number }) => number)
  | ((...a: { y: number }[]) => number);

/**/fn();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", R"TS(const fn: (a?: {
    x: number;
} & {
    y: number;
}, b?: {
    x: number;
} & {
    y: number;
}, ...args: {
    y: number;
}[]) => number)TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoSignatureRestParameterFromUnion4, TestQuickInfoSignatureRestParameterFromUnion4);

// quickInfoSignatureWithTrailingComma_test.go

// quickInfoSignatureWithTrailingComma_test.go
static void TestQuickInfoSignatureWithTrailingComma(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare function f<T>(a: T): T;
/**/f(2,);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "function f<2>(a: 2): 2", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoSignatureWithTrailingComma, TestQuickInfoSignatureWithTrailingComma);

// quickInfoSpecialPropertyAssignment_test.go

// quickInfoSpecialPropertyAssignment_test.go
static void TestQuickInfoSpecialPropertyAssignment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
class C {
    constructor() {
      /** Doc */
      this./*write*/x = 0;
      this./*read*/x;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "write", "(property) C.x: any", "Doc");
		f->VerifyQuickInfoAt(t, "read", "(property) C.x: number", "Doc");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoSpecialPropertyAssignment, TestQuickInfoSpecialPropertyAssignment);

// quickInfoStaticPrototypePropertyOnClass_test.go

// quickInfoStaticPrototypePropertyOnClass_test.go
static void TestQuickInfoStaticPrototypePropertyOnClass(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class c1 {
}
class c2<T> {
}
class c3 {
    constructor() {
    }
}
class c4 {
    constructor(param: string);
    constructor(param: number);
    constructor(param: any) {
    }
}
c1./*1*/prototype;
c2./*2*/prototype;
c3./*3*/prototype;
c4./*4*/prototype;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(property) c1.prototype: c1", "");
		f->VerifyQuickInfoAt(t, "2", "(property) c2<T>.prototype: c2<any>", "");
		f->VerifyQuickInfoAt(t, "3", "(property) c3.prototype: c3", "");
		f->VerifyQuickInfoAt(t, "4", "(property) c4.prototype: c4", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoStaticPrototypePropertyOnClass, TestQuickInfoStaticPrototypePropertyOnClass);

// quickInfoTemplateTag_test.go

// quickInfoTemplateTag_test.go
static void TestQuickInfoTemplateTag(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: /foo.js
/**
 * Doc
 * @template {new (...args: any[]) => any} T
 * @param {T} cls
 */
function /**/myMixin(cls) {
    return class extends cls {}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", R"TS(function myMixin<T extends new (...args: any[]) => any>(cls: T): {
    new (...args: any[]): (Anonymous class);
    prototype: myMixin<any>.(Anonymous class);
} & T)TS", "Doc");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoTemplateTag, TestQuickInfoTemplateTag);

// quickInfoThrowsTag_test.go

// quickInfoThrowsTag_test.go
static void TestQuickInfoThrowsTag(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class E extends Error {}

/**
 * @throws {E}
 */
function f1() {}

/**
 * @throws {E} description
 */
function f2() {}

/**
 * @throws description
 */
function f3() {}
f1/*1*/()
f2/*2*/()
f3/*3*/())TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoThrowsTag, TestQuickInfoThrowsTag);

// quickInfoTypeAliasDefinedInDifferentFile_test.go

// quickInfoTypeAliasDefinedInDifferentFile_test.go
static void TestQuickInfoTypeAliasDefinedInDifferentFile(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export type X = { x: number };
export function f(x: X): void {}
// @Filename: /b.ts
import { f } from "./a";
/**/f({ x: 1 });)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", R"TS((alias) f(x: X): void
import f)TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoTypeAliasDefinedInDifferentFile, TestQuickInfoTypeAliasDefinedInDifferentFile);

// quickInfoTypeArgumentInferenceWithMethodWithoutBody_test.go

// quickInfoTypeArgumentInferenceWithMethodWithoutBody_test.go
static void TestQuickInfoTypeArgumentInferenceWithMethodWithoutBody(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface ProxyHandler<T extends object> {
    getPrototypeOf?(target: T): object | null;
}
interface ProxyConstructor {
    new <T extends object>(target: T, handler: ProxyHandler<T>): T;
}
declare var Proxy: ProxyConstructor;
let target = {}
let proxy = new /**/Proxy(target, {
    getPrototypeOf()
}))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyQuickInfoExists(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoTypeArgumentInferenceWithMethodWithoutBody, TestQuickInfoTypeArgumentInferenceWithMethodWithoutBody);

// quickInfoTypeError_test.go

// quickInfoTypeError_test.go
static void TestQuickInfoTypeError(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(foo({
    /**/f: function() {},
    f() {}
});)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "(method) f(): void", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoTypeError, TestQuickInfoTypeError);

// quickInfoTypeOfThisInStatics_test.go

// quickInfoTypeOfThisInStatics_test.go
static void TestQuickInfoTypeOfThisInStatics(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(class C {
    static foo() {
        var /*1*/r = this;
    }
    static get x() {
        var /*2*/r = this;
        return 1;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(local var) r: typeof C", "");
		f->VerifyQuickInfoAt(t, "2", "(local var) r: typeof C", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoTypeOfThisInStatics, TestQuickInfoTypeOfThisInStatics);

// quickInfoTypeOnlyNamespaceAndClass_test.go

// quickInfoTypeOnlyNamespaceAndClass_test.go
static void TestQuickInfoTypeOnlyNamespaceAndClass(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export namespace ns {
  export class Box<T> {}
}
// @Filename: /b.ts
import type { ns } from './a';
let x: /*1*/ns./*2*/Box<string>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", R"TS((alias) namespace ns
import ns)TS", "");
		f->VerifyQuickInfoAt(t, "2", "class ns.Box<T>", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoTypeOnlyNamespaceAndClass, TestQuickInfoTypeOnlyNamespaceAndClass);

// quickInfoTypedGenericPrototypeMember_test.go

// quickInfoTypedGenericPrototypeMember_test.go
static void TestQuickInfoTypedGenericPrototypeMember(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C<T> {
   foo(x: T) { }
}
var /*1*/x = new C<any>(); // Quick Info for x is C<any>
var /*2*/y = C.prototype; // Quick Info for y is C<{}>)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "var x: C<any>", "");
		f->VerifyQuickInfoAt(t, "2", "var y: C<any>", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoTypedGenericPrototypeMember, TestQuickInfoTypedGenericPrototypeMember);

// quickInfoTypedefTag_test.go

// quickInfoTypedefTag_test.go
static void TestQuickInfoTypedefTag(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: a.js
/**
 * The typedef tag should not appear in the quickinfo.
 * @typedef {{ foo: 'foo' }} Foo
 */
function f() { }
f/*1*/()
/**
 * A removed comment
 * @tag Usage shows that non-param tags in comments explain the typedef instead of using it
 * @typedef {{ nope: any }} Nope not here
 * @tag comment 2
 */
function g() { }
g/*2*/()
/**
 * The whole thing is kept
 * @param {Local} keep
 * @typedef {{ local: any }} Local kept too
 * @returns {void} also kept
 */
function h(keep) { }
h/*3*/({ nope: 1 }))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoTypedefTag, TestQuickInfoTypedefTag);

// quickInfoUnionOfNamespaces_test.go

// quickInfoUnionOfNamespaces_test.go
static void TestQuickInfoUnionOfNamespaces(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(declare const x: typeof A | typeof B;
x./**/f;

namespace A {
    export function f() {}
}
namespace B {
    export function f() {}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "(method) f(): void", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoUnionOfNamespaces, TestQuickInfoUnionOfNamespaces);

// quickInfoUnion_discriminated_test.go

// quickInfoUnion_discriminated_test.go
static void TestQuickInfoUnion_discriminated(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: quickInfoJsDocTags.ts
type U = A | B;

interface A {
    /** Kind A */
    kind: "a";
    /** Prop A */
    prop: number;
}

interface B {
    /** Kind B */
    kind: "b";
    /** Prop B */
    prop: string;
}

const u: U = {
    /*uKind*/kind: "a",
    /*uProp*/prop: 0,
}
const u2: U = {
    /*u2Kind*/kind: "bogus",
    /*u2Prop*/prop: 1,
};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "uKind", R"TS((property) A.kind: "a")TS", "Kind A");
		f->VerifyQuickInfoAt(t, "uProp", "(property) A.prop: number", "Prop A");
		f->VerifyQuickInfoAt(t, "u2Kind", R"TS((property) kind: "bogus")TS", "");
		f->VerifyQuickInfoAt(t, "u2Prop", "(property) prop: number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoUnion_discriminated, TestQuickInfoUnion_discriminated);

// quickInfoUniqueSymbolJsDoc_test.go

// quickInfoUniqueSymbolJsDoc_test.go
static void TestQuickInfoUniqueSymbolJsDoc(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @checkJs: true
// @allowJs: true
// @filename: ./a.js
/** @type {unique symbol} */
const foo = Symbol();
foo/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoUniqueSymbolJsDoc, TestQuickInfoUniqueSymbolJsDoc);

// quickInfoUntypedModuleImport_test.go

// quickInfoUntypedModuleImport_test.go
static void TestQuickInfoUntypedModuleImport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @strict: false
// @Filename: node_modules/foo/index.js
 /*index*/{}
// @Filename: a.ts
import /*foo*/foo from /*fooModule*/"foo";
/*fooCall*/foo();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "a.ts");
		f->VerifyNumberOfErrorsInCurrentFile(t, 0);
		f->GoToMarker(t, "fooModule");
		f->VerifyQuickInfoIs(t, "", "");
		f->GoToMarker(t, "foo");
		f->VerifyQuickInfoIs(t, "import foo", "");
		f->VerifyBaselineFindAllReferences(t, {"foo", "fooModule", "fooCall"});
		f->VerifyBaselineGoToDefinition(t, false, {"fooModule", "foo"});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoUntypedModuleImport, TestQuickInfoUntypedModuleImport);

// quickInfoVerbosityNamespaceBindingWithDefaultExportedFunction1_test.go

// quickInfoVerbosityNamespaceBindingWithDefaultExportedFunction1_test.go
static void TestQuickInfoVerbosityNamespaceBindingWithDefaultExportedFunction1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: esnext
// @filename: /a.ts
export default function fn() {}
export { fn as default };
// @filename: /b.ts
import * as ns from "./a";

ns/*1*/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoVerbosityNamespaceBindingWithDefaultExportedFunction1, TestQuickInfoVerbosityNamespaceBindingWithDefaultExportedFunction1);

// quickInfoWidenedTypes_test.go

// quickInfoWidenedTypes_test.go
static void TestQuickInfoWidenedTypes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: false
var /*1*/a = null;                   // var a: any
var /*2*/b = undefined;              // var b: any
var /*3*/c = { x: 0, y: null };	// var c: { x: number, y: any }
var /*4*/d = [null, undefined];      // var d: any[])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "var a: any", "");
		f->VerifyQuickInfoAt(t, "2", "var b: any", "");
		f->VerifyQuickInfoAt(t, "3", R"TS(var c: {
    x: number;
    y: any;
})TS", "");
		f->VerifyQuickInfoAt(t, "4", "var d: any[]", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoWidenedTypes, TestQuickInfoWidenedTypes);

// quickInfoWithNestedDestructuredParameterInLambda_test.go

// quickInfoWithNestedDestructuredParameterInLambda_test.go
static void TestQuickInfoWithNestedDestructuredParameterInLambda(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((std::string(R"TS(// @filename: a.tsx
import * as React from 'react';
interface SomeInterface {
    someBoolean: boolean,
    someString: string;
}
interface SomeProps {
    someProp: SomeInterface;
}
export const /*1*/SomeStatelessComponent = ({someProp: { someBoolean, someString}}: SomeProps) => (<div>{)TS") + "`") + std::string(R"TS(${someBoolean}${someString})TS")) + std::string("`")) + std::string(R"TS(});)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifyQuickInfoExists(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoWithNestedDestructuredParameterInLambda, TestQuickInfoWithNestedDestructuredParameterInLambda);

// quickInfo_errorSignatureFillsInTypeParameter_test.go

// quickInfo_errorSignatureFillsInTypeParameter_test.go
static void TestQuickInfo_errorSignatureFillsInTypeParameter(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare function f<T>(x: number): T;
const x/**/ = f();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "", "const x: unknown", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfo_errorSignatureFillsInTypeParameter, TestQuickInfo_errorSignatureFillsInTypeParameter);

// quickInfo_notInsideComment_test.go

// quickInfo_notInsideComment_test.go
static void TestQuickInfo_notInsideComment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(a/* /**/ */.b)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyNotQuickInfoExists(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfo_notInsideComment, TestQuickInfo_notInsideComment);

// quickInforForSucessiveInferencesIsNotAny_test.go

// quickInforForSucessiveInferencesIsNotAny_test.go
static void TestQuickInforForSucessiveInferencesIsNotAny(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare function schema<T> (value : T) : {field : T};

declare const b: boolean;
const obj/*1*/ = schema(b);
const actualTypeOfNested/*2*/ = schema(obj);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", R"TS(const obj: {
    field: boolean;
})TS", "");
		f->VerifyQuickInfoAt(t, "2", R"TS(const actualTypeOfNested: {
    field: {
        field: boolean;
    };
})TS", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInforForSucessiveInferencesIsNotAny, TestQuickInforForSucessiveInferencesIsNotAny);

// quickinfo01_test.go

// quickinfo01_test.go
static void TestQuickinfo01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
interface One {
    commonProperty: number;
    commonFunction(): number;
}

interface Two {
    commonProperty: string
    commonFunction(): number;
}

var /*1*/x : One | Two;

x./*2*/commonProperty;
x./*3*/commonFunction;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyQuickInfoAt(t, "1", "var x: One | Two", "");
		f->VerifyQuickInfoAt(t, "2", "(property) commonProperty: string | number", "");
		f->VerifyQuickInfoAt(t, "3", "(method) commonFunction(): number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfo01, TestQuickinfo01);

// quickinfoExpressionTypeNotChangedViaDeletion_test.go

// quickinfoExpressionTypeNotChangedViaDeletion_test.go
static void TestQuickinfoExpressionTypeNotChangedViaDeletion(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type TypeEq<A, B> = (<T>() => T extends A ? 1 : 2) extends (<T>() => T extends B ? 1 : 2) ? true : false;

const /*2*/test1: TypeEq<number[], [number, ...number[]]> = false;

declare const foo: [number, ...number[]];
declare const bar: number[];

const /*1*/test2: TypeEq<typeof foo, typeof bar> = false;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifyQuickInfoIs(t, "const test2: false", "");
		f->GoToMarker(t, "2");
		f->VerifyQuickInfoIs(t, "const test1: false", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoExpressionTypeNotChangedViaDeletion, TestQuickinfoExpressionTypeNotChangedViaDeletion);

// quickinfoForNamespaceMergeWithClassConstrainedToSelf_test.go

// quickinfoForNamespaceMergeWithClassConstrainedToSelf_test.go
static void TestQuickinfoForNamespaceMergeWithClassConstrainedToSelf(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare namespace AMap {
    namespace MassMarks {
        interface Data {
            style?: number;
        }
    }
    class MassMarks<D extends MassMarks.Data = MassMarks.Data> {
        constructor(data: D[] | string);
        clear(): void;
    }
}

interface MassMarksCustomData extends AMap.MassMarks./*1*/Data {
    name: string;
    id: string;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "interface AMap.MassMarks<D extends AMap.MassMarks.Data = AMap.MassMarks.Data>.Data", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoForNamespaceMergeWithClassConstrainedToSelf, TestQuickinfoForNamespaceMergeWithClassConstrainedToSelf);

// quickinfoForUnionProperty_test.go

// quickinfoForUnionProperty_test.go
static void TestQuickinfoForUnionProperty(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(interface One {
    commonProperty: number;
    commonFunction(): number;
}

interface Two {
    commonProperty: string
    commonFunction(): number;
}

var /*1*/x : One | Two;

x./*2*/commonProperty;
x./*3*/commonFunction;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "var x: One | Two", "");
		f->VerifyQuickInfoAt(t, "2", "(property) commonProperty: string | number", "");
		f->VerifyQuickInfoAt(t, "3", "(method) commonFunction(): number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoForUnionProperty, TestQuickinfoForUnionProperty);

// quickinfoVerbosity1_test.go

// quickinfoVerbosity1_test.go
static void TestQuickinfoVerbosity1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type FooType = string | number;
const foo/*a*/: FooType = 1;
type BarType = FooType | boolean;
const bar/*b*/: BarType = 1;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"a", std::vector<int>{0, 1}}, {"b", std::vector<int>{0, 1, 2}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosity1, TestQuickinfoVerbosity1);

// quickinfoVerbosity2_test.go

// quickinfoVerbosity2_test.go
static void TestQuickinfoVerbosity2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type Str = string | {};
type FooType = Str | number;
type Sym = symbol | (() => void);
type BarType = Sym | boolean;
type BothType = FooType | BarType;
const both/*b*/: BothType = 1;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"b", std::vector<int>{0, 1, 2, 3}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosity2, TestQuickinfoVerbosity2);

// quickinfoVerbosity3_test.go

// quickinfoVerbosity3_test.go
static void TestQuickinfoVerbosity3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export type X = { x: number };
export function f(x: X): void {}
// @Filename: /b.ts
import { f } from "./a";
/*1*/f({ x: 1 });
// @Filename: file.tsx
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
 let y1 = <Opt/*2*/ propx={2} />;
// @Filename: a.ts
 interface Foo/*3*/<T extends Date> {
     prop: T
 }
 class Bar/*4*/<T extends Date> implements Foo<T> {
     prop!: T
 }
// @Filename: c.ts
 class c5b { public foo() { } }
 namespace c5b/*5*/ { export var y = 2; })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{0, 1}}, {"2", std::vector<int>{0, 1, 2}}, {"3", std::vector<int>{0, 1}}, {"4", std::vector<int>{0, 1, 2}}, {"5", std::vector<int>{0, 1, 2}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosity3, TestQuickinfoVerbosity3);

// quickinfoVerbosityAbstractClass_test.go

// quickinfoVerbosityAbstractClass_test.go
static void TestQuickinfoVerbosityAbstractClass(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
declare abstract class Shape/*1*/ {
    abstract area(): number;
    abstract perimeter(): number;
    toString(): string;
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityAbstractClass, TestQuickinfoVerbosityAbstractClass);

// quickinfoVerbosityClass1_test.go

// quickinfoVerbosityClass1_test.go
static void TestQuickinfoVerbosityClass1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS({
    class Foo {
        a!: "a" | "c";
    }
    const f/*f1*/ = new Foo();
}
{
    type FooParam = "a" | "b";
    class Foo {
        constructor(public x: string) {
            this.x = "a";
        }
        foo(p: FooParam): void {}
    }
    const f/*f2*/ = new Foo("");
}
{
    class Bar/*B*/ {
        a!: string;
        bar(): void {}
        baz(param: string): void {}
    }
    class Foo extends Bar {
        b!: boolean;
        override baz(param: string | number): void {}
    }
    const f/*f3*/ = new Foo();
}
{
    class Bar<B extends string> {
        bar(param: B): void {}
        baz(): this { return this; }
    }
    class Foo extends Bar<"foo"> {
        foo(): this { return this; }
    }
    const b/*b1*/ = new Bar();
    const f/*f4*/ = new Foo();
}
{
    class Bar<B extends string> {
        bar(param: B): void {}
        baz(): this { return this; }
    }
    const noname/*n1*/ = new (class extends Bar<"foo"> {
        foo(): this { return this; }
    })();
    const klass = class extends Bar<"foo"> {
        foo(): this { return this; }
    };
    const k/*k1*/ = new klass();
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"f1", std::vector<int>{0, 1}}, {"f2", std::vector<int>{0, 1, 2}}, {"f3", std::vector<int>{0, 1}}, {"b1", std::vector<int>{0, 1}}, {"f4", std::vector<int>{0, 1}}, {"n1", std::vector<int>{0, 1}}, {"k1", std::vector<int>{0, 1}}, {"B", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityClass1, TestQuickinfoVerbosityClass1);

// quickinfoVerbosityClass2_test.go

// quickinfoVerbosityClass2_test.go
static void TestQuickinfoVerbosityClass2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Apple {
    color: string;
}
class Foo/*1*/<T> {
    constructor(public x: T) { }
    public y!: T;
    static whatever(): void { }
    private foo(): Apple { return { color: "green" }; }
    static {
        const a = class { x?: Apple; };
    }
    protected z = true;
}
type Whatever/*2*/ = Foo<string>;
const a/*3*/ = Foo;
const c/*4*/ = Foo<string>;
[1].forEach(class/*5*/ <T> {
    constructor(public x: T) { }
    public y!: T;
    static whatever(): void { }
    private foo(): Apple { return { color: "green" }; }
    static {
        const a = class { x?: Apple; };
    }
    protected z = true;
});
const b/*6*/ = Bar<number>;
@random()
abstract class Animal/*7*/ {
    name!: string;
    abstract makeSound(): void;
}
class Dog/*8*/ {
    what(this: this, that: Dog) { }
    #bones: string[];
}
const d/*9*/ = new Dog();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{0, 1, 2}}, {"2", std::vector<int>{0, 1, 2}}, {"3", std::vector<int>{0, 1}}, {"4", std::vector<int>{0}}, {"5", std::vector<int>{0, 1, 2}}, {"6", std::vector<int>{0}}, {"7", std::vector<int>{0, 1}}, {"8", std::vector<int>{0, 1}}, {"9", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityClass2, TestQuickinfoVerbosityClass2);

// quickinfoVerbosityClassInterfaceMerge_test.go

// quickinfoVerbosityClassInterfaceMerge_test.go
static void TestQuickinfoVerbosityClassInterfaceMerge(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
declare class Foo/*1*/ {
    x: number;
}
declare interface Foo {
    y: string;
}
const f: Foo/*2*/ = { x: 1, y: "hello" };
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{0, 1}}, {"2", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityClassInterfaceMerge, TestQuickinfoVerbosityClassInterfaceMerge);

// quickinfoVerbosityClassWithMixinBase_test.go

// quickinfoVerbosityClassWithMixinBase_test.go
static void TestQuickinfoVerbosityClassWithMixinBase(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
class Base {}

declare const Mixin: new () => Base & { mixed: string };

class Derived/*1*/ extends Mixin {}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityClassWithMixinBase, TestQuickinfoVerbosityClassWithMixinBase);

// quickinfoVerbosityConditionalType_test.go

// quickinfoVerbosityConditionalType_test.go
static void TestQuickinfoVerbosityConditionalType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Apple {
    color: string;
    weight: number;
}
type StrInt = string | bigint;
type T1<T extends Apple | Apple[]> = T extends { color: string } ? "one apple" : StrInt;
function f<T extends Apple | Apple[]>(x: T1<T>): void {
    x/*x*/;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"x", std::vector<int>{0, 1, 2}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityConditionalType, TestQuickinfoVerbosityConditionalType);

// quickinfoVerbosityConstEnum_test.go

// quickinfoVerbosityConstEnum_test.go
static void TestQuickinfoVerbosityConstEnum(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
const enum Direction/*1*/ {
    Up = "UP",
    Down = "DOWN",
    Left = "LEFT",
    Right = "RIGHT",
}

enum NumericEnum/*2*/ {
    A,
    B = 10,
    C,
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{0, 1}}, {"2", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityConstEnum, TestQuickinfoVerbosityConstEnum);

// quickinfoVerbosityConstMergedWithNamespace_test.go

// quickinfoVerbosityConstMergedWithNamespace_test.go
static void TestQuickinfoVerbosityConstMergedWithNamespace(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
declare function create/*1*/(x: string): number;
declare namespace create/*2*/ {
    var version: string;
    function reset(): void;
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{0, 1}}, {"2", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityConstMergedWithNamespace, TestQuickinfoVerbosityConstMergedWithNamespace);

// quickinfoVerbosityEmptyEnum_test.go

// quickinfoVerbosityEmptyEnum_test.go
static void TestQuickinfoVerbosityEmptyEnum(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
enum Degree {}

declare const e/*0*/: Degree;
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"0", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityEmptyEnum, TestQuickinfoVerbosityEmptyEnum);

// quickinfoVerbosityEnum_test.go

// quickinfoVerbosityEnum_test.go
static void TestQuickinfoVerbosityEnum(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: a.ts
export {};
enum Color/*c*/ {
    Red,
    Green,
    Blue,
}
const x/*x*/: Color = Color.Red;
const enum Direction/*d*/ {
    Up,
    Down,
}
const y/*y*/: Direction = Direction.Up;
enum Flags/*f*/ {
    None = 0,
    IsDirectory = 1 << 0,
    IsFile = 1 << 1,
    IsSymlink = 1 << 2,
}
// @filename: b.ts
export enum Color {
    Red = "red"
}
// @filename: c.ts
import { Color } from "./b";
const c: Color/*a*/ = Color.Red;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"c", std::vector<int>{0, 1}}, {"x", std::vector<int>{0, 1}}, {"d", std::vector<int>{0, 1}}, {"y", std::vector<int>{0, 1}}, {"f", std::vector<int>{0, 1}}, {"a", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityEnum, TestQuickinfoVerbosityEnum);

// quickinfoVerbosityFunction_test.go

// quickinfoVerbosityFunction_test.go
static void TestQuickinfoVerbosityFunction(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Apple {
    color: string;
    size: number;
}
interface Orchard {
    takeOneApple(a: Apple): void;
    getApple(): Apple;
    getApple(size: number): Apple[];
}
const o/*o*/: Orchard = {} as any;
declare function isApple/*f*/(x: unknown): x is Apple;
type SomeType = {
    prop1: string;
}
function someFun(a: SomeType): SomeType {
    return a;
}
someFun/*s*/.what = 'what';)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"o", std::vector<int>{0, 1, 2}}, {"f", std::vector<int>{0, 1}}, {"s", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityFunction, TestQuickinfoVerbosityFunction);

// quickinfoVerbosityImport_test.go

// quickinfoVerbosityImport_test.go
static void TestQuickinfoVerbosityImport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: esnext
// @filename: /0.ts
export type Apple = {
    a: number;
    b: string;
}
export const a: Apple = { a: 1, b: "2"};
export enum Color {
    Red,
    Green,
    Blue,
}
// @filename: /1.ts
import * as zero from "./0";
const b/*b*/ = zero;
// @filename: /2.ts
import { a/*a*/ } from "./0";
import { Color/*c*/ } from "./0";
// @filename: /3.ts
export default class {
    a: boolean;
}
// @filename: /4.ts
import Foo/*d*/ from "./3";
const f/*e*/ = new Foo/*f*/();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"b", std::vector<int>{0, 1, 2}}, {"a", std::vector<int>{0, 1}}, {"c", std::vector<int>{0, 1}}, {"d", std::vector<int>{0}}, {"e", std::vector<int>{0, 1}}, {"f", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityImport, TestQuickinfoVerbosityImport);

// quickinfoVerbosityIncreaseDecrease_test.go

// quickinfoVerbosityIncreaseDecrease_test.go
static void TestQuickinfoVerbosityIncreaseDecrease(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(export const JOB_STATES = ["created", "active", "completed", "failed", "retry", "cancelled", "archive"] as const
export type JobState = (typeof JOB_STATES)[number]
type Color = "default" | "primary" | "secondary" | "success" | "warning" | "danger"
const JobsStateToColor/*a*/: Record<
  JobState,
  {
    color: Color
    label: string
    labelPlural: string
  }
> = {
  created: {
    color: "success",
    label: "Направљен",
    labelPlural: "Направљени",
  },
  active: {
    color: "success",
    label: "Активан",
    labelPlural: "Активни",
  },
  completed: {
    color: "success",
    label: "Успешан",
    labelPlural: "Успешни",
  },
  cancelled: {
    color: "default",
    label: "Отаказан",
    labelPlural: "Отаказни",
  },
  failed: {
    color: "danger",
    label: "Пао",
    labelPlural: "Пали",
  },
  archive: {
    color: "default",
    label: "Архивиран",
    labelPlural: "Архивирани",
  },
  retry: {
    color: "warning",
    label: "Понавља се",
    labelPlural: "Понављају се",
  },
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"a", std::vector<int>{0, 1, 0}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityIncreaseDecrease, TestQuickinfoVerbosityIncreaseDecrease);

// quickinfoVerbosityIndexSignature_test.go

// quickinfoVerbosityIndexSignature_test.go
static void TestQuickinfoVerbosityIndexSignature(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type Key = string | number;
interface Apple {
    banana: number;
}
interface Foo {
    [a/*a*/: Key]: Apple;
}
const f/*f*/: Foo = {};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"a", std::vector<int>{0, 1}}, {"f", std::vector<int>{0, 1, 2}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityIndexSignature, TestQuickinfoVerbosityIndexSignature);

// quickinfoVerbosityIndexType_test.go

// quickinfoVerbosityIndexType_test.go
static void TestQuickinfoVerbosityIndexType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface T1 {
	banana: string;
	grape: number;
	apple: boolean;
}
const x1/*x1*/: keyof T1 = 'banana';
const x2/*x2*/: keyof T1 & ("grape" | "apple") = 'grape';
function fn1<T extends T1>(obj: T, key: keyof T, k2: keyof T1) {
	if (key === k2/*k2*/) {
		return obj[key/*key*/];
	}
	return key;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"x1", std::vector<int>{0, 1}}, {"x2", std::vector<int>{0}}, {"k2", std::vector<int>{0, 1}}, {"key", std::vector<int>{0}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityIndexType, TestQuickinfoVerbosityIndexType);

// quickinfoVerbosityIndexedAccessType_test.go

// quickinfoVerbosityIndexedAccessType_test.go
static void TestQuickinfoVerbosityIndexedAccessType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface T2 {
	"string key": string;
	"number key": number;
	"any key": string | number | symbol;
}
type K2 = "string key" | "any key";
function fn2<T extends T2>(obj: T, key: keyof T) {
	const value/*v1*/: T[K2] = undefined as any;
}
function fn3<K extends keyof T2>(obj: T2, key: K) {
    const value/*v2*/: T2[K] = undefined as any;;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"v1", std::vector<int>{0, 1}}, {"v2", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityIndexedAccessType, TestQuickinfoVerbosityIndexedAccessType);

// quickinfoVerbosityInterface1_test.go

// quickinfoVerbosityInterface1_test.go
static void TestQuickinfoVerbosityInterface1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS({
    interface Foo {
        a: "a" | "c";
    }
    const f/*f1*/: Foo = { a: "a" };
}
{
    interface Bar {
        b: "b" | "d";
    }
    interface Foo extends Bar {
        a: "a" | "c";
    }
    const f/*f2*/: Foo = { a: "a", b: "b" };
}
{
    type BarParam = "b" | "d";
    interface Bar {
        bar(b: BarParam): string;
    }
    type FooType = "a" | "c";
    interface FooParam {
        param: FooType;
    }
    interface Foo extends Bar {
        a: FooType;
        foo: (a: FooParam) => number;
    }
    const f/*f3*/: Foo = { a: "a", bar: () => "b", foo: () => 1 };
}
{
    interface Bar<B> {
        bar(b: B): string;
    }
    interface FooParam {
        param: "a" | "c";
    }
    interface Foo extends Bar<FooParam> {
        a: "a" | "c";
        foo: (a: FooParam) => number;
    }
    const f/*f4*/: Foo = { a: "a", bar: () => "b", foo: () => 1 };
    const b/*b1*/: Bar<number> = { bar: () => "" };
}
{
    interface Foo<A> {
        a: A;
    }
    type Alias = Foo<string>;
    const a/*a*/: Alias = { a: "a" };
}
{
    interface Foo {
        a: "a";
    }
    interface Foo {
        b: "b";
    }
    const f/*f5*/: Foo = { a: "a", b: "b" };
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"f1", std::vector<int>{0, 1}}, {"f2", std::vector<int>{0, 1}}, {"f3", std::vector<int>{0, 1, 2, 3}}, {"f4", std::vector<int>{0, 1, 2}}, {"b1", std::vector<int>{0, 1}}, {"a", std::vector<int>{0, 1, 2}}, {"f5", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityInterface1, TestQuickinfoVerbosityInterface1);

// quickinfoVerbosityInterface2_test.go

// quickinfoVerbosityInterface2_test.go
static void TestQuickinfoVerbosityInterface2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS({
    interface Foo/*1*/ {
        a: "a" | "c";
    }
}
{
    interface Bar {
        b: "b" | "d";
    }
    interface Foo/*2*/ extends Bar {
        a: "a" | "c";
    }
}
{
    type BarParam = "b" | "d";
    interface Bar {
        bar(b: BarParam): string;
    }
    type FooType = "a" | "c";
    interface FooParam {
        param: FooType;
    }
    interface Foo/*3*/ extends Bar {
        a: FooType;
        foo: (a: FooParam) => number;
    }
}
{
    interface Bar/*4*/<B> {
        bar(b: B): string;
    }
    interface FooParam {
        param: "a" | "c";
    }
    interface Foo/*5*/ extends Bar<FooParam> {
        a: "a" | "c";
        foo: (a: FooParam) => number;
    }
}
{
    interface Foo {
        a: "a";
    }
    interface Foo/*6*/ {
        b: "b";
    }
}
interface Foo/*7*/ {
    a: "a";
}
namespace Foo/*8*/ {
    export const bar: string;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{0, 1}}, {"2", std::vector<int>{0, 1}}, {"3", std::vector<int>{0, 1, 2}}, {"4", std::vector<int>{0, 1}}, {"5", std::vector<int>{0, 1, 2}}, {"6", std::vector<int>{0, 1}}, {"7", std::vector<int>{0, 1}}, {"8", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityInterface2, TestQuickinfoVerbosityInterface2);

// quickinfoVerbosityInterfaceMemberOrdering_test.go

// quickinfoVerbosityInterfaceMemberOrdering_test.go
static void TestQuickinfoVerbosityInterfaceMemberOrdering(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
interface Callable/*1*/ {
    (x: string): boolean;
    new (x: string): Callable;
    [key: string]: any;
    name: string;
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityInterfaceMemberOrdering, TestQuickinfoVerbosityInterfaceMemberOrdering);

// quickinfoVerbosityIntersection1_test.go

// quickinfoVerbosityIntersection1_test.go
static void TestQuickinfoVerbosityIntersection1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS({
    type Foo = { a: "a" | "c" };
    type Bar = { a: "a" | "b" };
    const obj/*o1*/: Foo & Bar = { a: "a" };
}
{
    type Foo = { a: "c" };
    type Bar = { a: "b" };
    const obj/*o2*/: Foo & Bar = { a: "" };
}
{
    type Foo = { a: "c" };
    type Bar = { a: "b" };
    type Never = Foo & Bar;
    const obj/*o3*/: Never = { a: "" };
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"o1", std::vector<int>{0, 1}}, {"o2", std::vector<int>{0}}, {"o3", std::vector<int>{0}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityIntersection1, TestQuickinfoVerbosityIntersection1);

// quickinfoVerbosityJSDocNamespacedTypedef_test.go

// quickinfoVerbosityJSDocNamespacedTypedef_test.go
static void TestQuickInfoVerbosityJSDocNamespacedTypedef(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @allowJs: true
// @checkJs: true
// @Filename: /index.js
// Namespaced typedef
/** @typedef {string} /*ns*/NS./*t*/T */

// Namespaced typedef aliased to qualified namespaced typedef.
/** @typedef {NS.T} NS./*u*/U */

// Namespaced typedef aliased to implicitly-resolved typedef.
/** @typedef {U} NS./*v*/V */
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"ns", std::vector<int>{0, 1}}, {"t", std::vector<int>{0, 1}}, {"u", std::vector<int>{0, 1}}, {"v", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoVerbosityJSDocNamespacedTypedef, TestQuickInfoVerbosityJSDocNamespacedTypedef);

// quickinfoVerbosityJs_test.go

// quickinfoVerbosityJs_test.go
static void TestQuickinfoVerbosityJs(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: somefile.js
// @allowJs: true
/**
 * @typedef {Object} SomeType
 * @property {string} prop1
 */
/** @type {SomeType} */
const a/*1*/ = {
    prop1: 'value',
}
/**
 * @typedef {Object} SomeType2/*2*/
 * @property {number} prop2
 * @property {SomeType} prop3
 */
/** @type {SomeType[]} */
const ss = [{ prop1: 'value' }, { prop1: 'value' }];
const d = ss.map((s/*3*/) => s.prop1);
/** @param {SomeType} a
 * @returns {SomeType}
 */
function someFun/*4*/(a) {
    return a;
}
someFun.what = 'what';
class SomeClass/*5*/ {
    /** @type {SomeType2} */
    b;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{0, 1}}, {"2", std::vector<int>{0, 1}}, {"3", std::vector<int>{0, 1}}, {"4", std::vector<int>{0, 1}}, {"5", std::vector<int>{0, 1, 2}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityJs, TestQuickinfoVerbosityJs);

// quickinfoVerbosityLibType_test.go

// quickinfoVerbosityLibType_test.go
static void TestQuickinfoVerbosityLibType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
interface Apple {
    color: string;
    size: number;
}
function f(): Promise<Apple> {
    return Promise.resolve({ color: "red", size: 5 });
}
const g/*g*/ = f;
const u/*u*/: Map<string, Apple> = new Map;
type Foo<T> = Promise/*p*/<T>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"g", std::vector<int>{0, 1}}, {"u", std::vector<int>{0, 1}}, {"p", std::vector<int>{0}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityLibType, TestQuickinfoVerbosityLibType);

// quickinfoVerbosityMappedType_test.go

// quickinfoVerbosityMappedType_test.go
static void TestQuickinfoVerbosityMappedType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type Apple = boolean | number;
type Orange = string | boolean;
type F<T> = {
	[K in keyof T as T[K] extends Apple ? never : K]: T[K];
}
type Bar = {
	banana: string;
	apple: boolean;
}
const x/*x*/: F/*F*/<Bar> = { banana: 'hello' };
const y/*y*/: { [K in keyof Bar]?: Bar[K] } = { banana: 'hello' };
type G<T> = {
	[K in keyof T]: T[K] & Apple
};
const z: G/*G*/<Bar> = { banana: 'hello', apple: true };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"x", std::vector<int>{0, 1}}, {"y", std::vector<int>{0}}, {"F", std::vector<int>{0, 1}}, {"G", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityMappedType, TestQuickinfoVerbosityMappedType);

// quickinfoVerbosityNamespaceAnonymousClassHeritage1_test.go

// quickinfoVerbosityNamespaceAnonymousClassHeritage1_test.go
static void TestQuickinfoVerbosityNamespaceAnonymousClassHeritage1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
namespace NS/*1*/ {
    export class Derived extends class {
        baseField: string;
    } {
        derivedField: number;
    }
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityNamespaceAnonymousClassHeritage1, TestQuickinfoVerbosityNamespaceAnonymousClassHeritage1);

// quickinfoVerbosityNamespaceClassHeritage_test.go

// quickinfoVerbosityNamespaceClassHeritage_test.go
static void TestQuickinfoVerbosityNamespaceClassHeritage(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
declare class Base {
    id: number;
}

declare namespace Shapes/*1*/ {
    class Circle extends Base {
        radius: number;
    }
    class Square extends Base {
        side: number;
    }
    interface Drawable {
        draw(): void;
    }
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityNamespaceClassHeritage, TestQuickinfoVerbosityNamespaceClassHeritage);

// quickinfoVerbosityNamespaceDefaultExport_test.go

// quickinfoVerbosityNamespaceDefaultExport_test.go
static void TestQuickinfoVerbosityNamespaceDefaultExport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
declare namespace ns/*1*/ {
    interface Shape {
        sides: number;
    }
    const circle: Shape;
    export default circle;
    export { Shape };
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityNamespaceDefaultExport, TestQuickinfoVerbosityNamespaceDefaultExport);

// quickinfoVerbosityNamespaceErrorClassHeritage1_test.go

// quickinfoVerbosityNamespaceErrorClassHeritage1_test.go
static void TestQuickinfoVerbosityNamespaceErrorClassHeritage1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
namespace NS/*1*/ {
    export class Derived extends NonExistentClass {
        derivedField: number;
    }
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityNamespaceErrorClassHeritage1, TestQuickinfoVerbosityNamespaceErrorClassHeritage1);

// quickinfoVerbosityNamespaceInterfaceHeritageCrash_test.go

// quickinfoVerbosityNamespaceInterfaceHeritageCrash_test.go
static void TestQuickinfoVerbosityNamespaceInterfaceHeritageCrash(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
declare namespace NS/*1*/ {
    interface Config extends Record<string, any> {}
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityNamespaceInterfaceHeritageCrash, TestQuickinfoVerbosityNamespaceInterfaceHeritageCrash);

// quickinfoVerbosityNamespaceInterfaceHeritageIntersectionCrash_test.go

// quickinfoVerbosityNamespaceInterfaceHeritageIntersectionCrash_test.go
static void TestQuickinfoVerbosityNamespaceInterfaceHeritageIntersectionCrash(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
declare namespace NS/*1*/ {
    type Mixin = { a: string } & { b: number };
    interface Config extends Mixin {}
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityNamespaceInterfaceHeritageIntersectionCrash, TestQuickinfoVerbosityNamespaceInterfaceHeritageIntersectionCrash);

// quickinfoVerbosityNamespaceMembers_test.go

// quickinfoVerbosityNamespaceMembers_test.go
static void TestQuickinfoVerbosityNamespaceMembers(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
declare namespace NS/*1*/ {
    type StringAlias = string;
    type Pair<T> = { first: T; second: T };

    enum Color { Red, Green, Blue }

    class MyClass {
        name: string;
        greet(): void;
    }

    interface MyInterface {
        id: number;
        label: string;
    }

    const value: number;
    function doSomething(x: string): boolean;
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityNamespaceMembers, TestQuickinfoVerbosityNamespaceMembers);

// quickinfoVerbosityNamespaceMergedInterfaceHeritage_test.go

// quickinfoVerbosityNamespaceMergedInterfaceHeritage_test.go
static void TestQuickinfoVerbosityNamespaceMergedInterfaceHeritage(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
declare namespace NS/*1*/ {
    interface Config extends A {
        a: string;
    }

    interface Config extends B {
        b: number;
    }

    interface A {
        a: string;
    }

    interface B {
        b: number;
    }
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityNamespaceMergedInterfaceHeritage, TestQuickinfoVerbosityNamespaceMergedInterfaceHeritage);

// quickinfoVerbosityNamespacePrivateTypes_test.go

// quickinfoVerbosityNamespacePrivateTypes_test.go
static void TestQuickinfoVerbosityNamespacePrivateTypes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
declare namespace API/*1*/ {
    interface InternalConfig {
        secret: string;
        timeout: number;
    }
    function configure(config: InternalConfig): void;
    const defaultConfig: InternalConfig;
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityNamespacePrivateTypes, TestQuickinfoVerbosityNamespacePrivateTypes);

// quickinfoVerbosityNamespaceTypeAliases_test.go

// quickinfoVerbosityNamespaceTypeAliases_test.go
static void TestQuickinfoVerbosityNamespaceTypeAliases(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
type BaseConfig = { host: string; port: number };

declare namespace Config/*1*/ {
    type Readonly<T> = { readonly [K in keyof T]: T[K] };
    type Optional<T> = { [K in keyof T]?: T[K] };
    type ServerConfig = Readonly<BaseConfig>;
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityNamespaceTypeAliases, TestQuickinfoVerbosityNamespaceTypeAliases);

// quickinfoVerbosityNamespace_test.go

// quickinfoVerbosityNamespace_test.go
static void TestQuickinfoVerbosityNamespace(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: /1.ts
export {};
class Foo<T> {
    y: string;
}
namespace Foo/*1*/ {
    export var y: number = 1;
    export var x: string = "hello";
    export var w = "world";
    var z = 2;
}
// @filename: /2.ts
export namespace Foo {
    export var y: number = 1;
    export var x: string = "hello";
}
// @filename: /3.ts
import * as Foo_1 from "./b";
export declare namespace ns/*2*/ {
    import Foo = Foo_1.Foo;
    export { Foo };
    export const c: number;
    export const d = 1;
    let e: Apple;
    export let f: Apple;
}
interface Apple {
    a: string;
}
// @filename: /4.ts
class Foo<T> {
    y!: T;
}
namespace Two/*3*/ {
    export const f = new Foo<number>();
}
// @filename: /5.ts
namespace Two {
    export const g = new Foo<string>();
}
// @filename: /6.ts
namespace OnlyLocal/*4*/ {
    const bar: number;
}
// @filename: foo.ts
export function foo() { return "foo"; }
import("/*5*/./foo")
var x = import("./foo"))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{0, 1}}, {"2", std::vector<int>{0, 1, 2}}, {"3", std::vector<int>{0, 1, 2}}, {"4", std::vector<int>{0, 1}}, {"5", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityNamespace, TestQuickinfoVerbosityNamespace);

// quickinfoVerbosityNestedNamespace_test.go

// quickinfoVerbosityNestedNamespace_test.go
static void TestQuickinfoVerbosityNestedNamespace(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
declare namespace Outer/*1*/ {
    namespace Inner {
        const x: number;
        function f(): string;
    }
    const outerVal: boolean;
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityNestedNamespace, TestQuickinfoVerbosityNestedNamespace);

// quickinfoVerbosityNoErrorTruncation1_test.go

// quickinfoVerbosityNoErrorTruncation1_test.go
static void TestQuickinfoVerbosityNoErrorTruncation1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noErrorTruncation: true
type /*1*/T = [
  1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
  1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
  1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
  1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
  1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
  1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
  1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
  1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
  1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
  1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
  1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
  1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
  1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
  1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
  1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
  1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
  1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
  'still good', 'now truncating'
];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityNoErrorTruncation1, TestQuickinfoVerbosityNoErrorTruncation1);

// quickinfoVerbosityObjectType1_test.go

// quickinfoVerbosityObjectType1_test.go
static void TestQuickinfoVerbosityObjectType1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type Str = string | {};
type FooType = Str | number;
type Sym = symbol | (() => void);
type BarType = Sym | boolean;
type Obj = { foo: FooType, bar: BarType, str: Str };
const obj1/*o1*/: Obj = { foo: 1, bar: true, str: "3"};
const obj2/*o2*/: { foo: FooType, bar: BarType, str: Str } = { foo: 1, bar: true, str: "3"};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"o1", std::vector<int>{0, 1, 2, 3}}, {"o2", std::vector<int>{0, 1, 2}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityObjectType1, TestQuickinfoVerbosityObjectType1);

// quickinfoVerbosityRecursiveType_test.go

// quickinfoVerbosityRecursiveType_test.go
static void TestQuickinfoVerbosityRecursiveType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
type Node/*N*/<T> = {
    value: T;
    left: Node<T> | undefined;
    right: Node<T> | undefined;
}
const n/*n*/: Node<number> = {
    value: 1,
    left: undefined,
    right: undefined,
}
interface Orange {
    name: string;
}
type TreeNode/*t*/<T> = {
    value: T;
    left: TreeNode<T> | undefined;
    right: TreeNode<T> | undefined;
    orange?: Orange;
}
const m/*m*/: TreeNode<number> = {
    value: 1,
    left: undefined,
    right: undefined,
    orange: { name: "orange" },
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"N", std::vector<int>{0}}, {"n", std::vector<int>{0, 1}}, {"t", std::vector<int>{0, 1}}, {"m", std::vector<int>{0, 1, 2}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityRecursiveType, TestQuickinfoVerbosityRecursiveType);

// quickinfoVerbositySelfReferentialTypeArg_test.go

// quickinfoVerbositySelfReferentialTypeArg_test.go
static void TestQuickinfoVerbositySelfReferentialTypeArg(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type ContainerChild = Container;
interface Container<C = ContainerChild> {
    parent: Container;
}
declare const x: Container;
x/*1*/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{3}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbositySelfReferentialTypeArg, TestQuickinfoVerbositySelfReferentialTypeArg);

// quickinfoVerbosityServer_test.go

// quickinfoVerbosityServer_test.go
static void TestQuickinfoVerbosityServer(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
type FooType = string | number
const foo/*a*/: FooType = 1)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"a", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityServer, TestQuickinfoVerbosityServer);

// quickinfoVerbosityToplevelTruncation1_test.go

// quickinfoVerbosityToplevelTruncation1_test.go
static void TestQuickinfoVerbosityToplevelTruncation1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(export enum LargeEnum/*1*/ {
    Member1,
    Member2,
    Member3,
    Member4,
    Member5,
    Member6,
    Member7,
    Member8,
    Member9,
    Member10,
    Member11,
    Member12,
    Member13,
    Member14,
    Member15,
    Member16,
    Member17,
    Member18,
    Member19,
    Member20,
    Member21,
    Member22,
    Member23,
    Member24,
    Member25,
}
export interface LargeInterface/*2*/ {
    property1: string;
    property2: number;
    property3: boolean;
    property4: Date;
    property5: string[];
    property6: number[];
    property7: boolean[];
    property8: { [key: string]: unknown };
    property9: string | null;
    property10: number | null;
    property11: boolean | null;
    property12: Date | null;
    property13: string | number;
    property14: number | boolean;
    property15: string | boolean;
    property16: Array<{ id: number; name: string }>;
    property17: Array<{ key: string; value: unknown }>;
    property18: { nestedProp1: string; nestedProp2: number };
    property19: { nestedProp3: boolean; nestedProp4: Date };
    property20: () => void;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{1}}, {"2", std::vector<int>{1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityToplevelTruncation1, TestQuickinfoVerbosityToplevelTruncation1);

// quickinfoVerbosityToplevelTruncation2_test.go

// quickinfoVerbosityToplevelTruncation2_test.go
static void TestQuickinfoVerbosityToplevelTruncation2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(export enum LargeEnum/*1*/ {
    Member1,
    Member2,
    Member3,
    Member4,
    Member5,
    Member6,
    Member7,
    Member8,
    Member9,
    Member10,
    Member11,
    Member12,
    Member13,
    Member14,
    Member15,
    Member16,
    Member17,
    Member18,
    Member19,
    Member20,
    Member21,
    Member22,
    Member23,
    Member24,
    Member25,
}
export interface LargeInterface/*2*/ {
    property1: string;
    property2: number;
    property3: boolean;
    property4: Date;
    property5: string[];
    property6: number[];
    property7: boolean[];
    property8: { [key: string]: unknown };
    property9: string | null;
    property10: number | null;
    property11: boolean | null;
    property12: Date | null;
    property13: string | number;
    property14: number | boolean;
    property15: string | boolean;
    property16: Array<{ id: number; name: string }>;
    property17: Array<{ key: string; value: unknown }>;
    property18: { nestedProp1: string; nestedProp2: number };
    property19: { nestedProp3: boolean; nestedProp4: Date };
    property20: () => void;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"1", std::vector<int>{1}}, {"2", std::vector<int>{1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityToplevelTruncation2, TestQuickinfoVerbosityToplevelTruncation2);

// quickinfoVerbosityTruncation1_test.go

// quickinfoVerbosityTruncation1_test.go
static void TestQuickinfoVerbosityTruncation1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type Str = string | {};
type FooType = Str | number;
type Sym = symbol | (() => void);
type BarType = Sym | boolean;
interface LotsOfProps {
    someLongPropertyName1: Str;
    someLongPropertyName2: FooType;
    someLongPropertyName3: Sym;
    someLongPropertyName4: BarType;
    someLongPropertyName5: Str;
    someLongPropertyName6: FooType;
    someLongPropertyName7: Sym;
    someLongPropertyName8: BarType;
    someLongMethodName1(a: FooType, b: BarType): Sym;
    someLongPropertyName9: Str;
    someLongPropertyName10: FooType;
    someLongPropertyName11: Sym;
    someLongPropertyName12: BarType;
    someLongPropertyName13: Str;
    someLongPropertyName14: FooType;
    someLongPropertyName15: Sym;
    someLongPropertyName16: BarType;
    someLongMethodName2(a: FooType, b: BarType): Sym;
}
const obj1/*o1*/: LotsOfProps = undefined as any as LotsOfProps;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"o1", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityTruncation1, TestQuickinfoVerbosityTruncation1);

// quickinfoVerbosityTruncation2_test.go

// quickinfoVerbosityTruncation2_test.go
static void TestQuickinfoVerbosityTruncation2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface LargeInterface/*o1*/ {
    prop1: any;
    prop2: any;
    prop3: any;
    prop4: any;
    prop5: any;
    prop6: any;
    prop7: any;
    prop8: any;
    prop9: any;
    prop10: any;
    prop11: any;
    prop12: any;
    prop13: any;
    prop14: any;
    prop15: any;
    prop16: any;
    prop17: any;
    prop18: any;
    prop19: any;
    prop20: any;
    prop21: any;
    prop22: any;
    prop23: any;
    prop24: any;
    prop25: any;
    prop26: any;
    prop27: any;
    prop28: any;
    prop29: any;
    prop30: any;
    prop31: any;
    prop32: any;
    prop33: any;
    prop34: any;
    prop35: any;
    prop36: any;
    prop37: any;
    prop38: any;
    prop39: any;
    prop40: any;
    prop41: any;
    prop42: any;
    prop43: any;
    prop44: any;
    prop45: any;
    prop46: any;
    prop47: any;
    prop48: any;
    prop49: any;
    prop50: any;
    prop51: any;
    prop52: any;
    prop53: any;
    prop54: any;
    prop55: any;
    prop56: any;
    prop57: any;
    prop58: any;
    prop59: any;
    prop60: any;
    prop61: any;
    prop62: any;
    prop63: any;
    prop64: any;
    prop65: any;
    prop66: any;
    prop67: any;
    prop68: any;
    prop69: any;
    prop70: any;
    prop71: any;
    prop72: any;
    prop73: any;
    prop74: any;
    prop75: any;
    prop76: any;
    prop77: any;
    prop78: any;
    prop79: any;
    prop80: any;
    prop81: any;
    prop82: any;
    prop83: any;
    prop84: any;
    prop85: any;
    prop86: any;
    prop87: any;
    prop88: any;
    prop89: any;
    prop90: any;
    prop91: any;
    prop92: any;
    prop93: any;
    prop94: any;
    prop95: any;
    prop96: any;
    prop97: any;
    prop98: any;
    prop99: any;
    prop100: any;
    prop101: any;
    prop102: any;
    prop103: any;
    prop104: any;
    prop105: any;
    prop106: any;
    prop107: any;
    prop108: any;
    prop109: any;
    prop110: any;
    prop111: any;
    prop112: any;
    prop113: any;
    prop114: any;
    prop115: any;
    prop116: any;
    prop117: any;
    prop118: any;
    prop119: any;
    prop120: any;
    prop121: any;
    prop122: any;
    prop123: any;
    prop124: any;
    prop125: any;
    prop126: any;
    prop127: any;
    prop128: any;
    prop129: any;
    prop130: any;
    prop131: any;
    prop132: any;
    prop133: any;
    prop134: any;
    prop135: any;
    prop136: any;
    prop137: any;
    prop138: any;
    prop139: any;
    prop140: any;
    prop141: any;
    prop142: any;
    prop143: any;
    prop144: any;
    prop145: any;
    prop146: any;
    prop147: any;
    prop148: any;
    prop149: any;
    prop150: any;
    prop151: any;
    prop152: any;
    prop153: any;
    prop154: any;
    prop155: any;
    prop156: any;
    prop157: any;
    prop158: any;
    prop159: any;
    prop160: any;
    prop161: any;
    prop162: any;
    prop163: any;
    prop164: any;
    prop165: any;
    prop166: any;
    prop167: any;
    prop168: any;
    prop169: any;
    prop170: any;
    prop171: any;
    prop172: any;
    prop173: any;
    prop174: any;
    prop175: any;
    prop176: any;
    prop177: any;
    prop178: any;
    prop179: any;
    prop180: any;
    prop181: any;
    prop182: any;
    prop183: any;
    prop184: any;
    prop185: any;
    prop186: any;
    prop187: any;
    prop188: any;
    prop189: any;
    prop190: any;
    prop191: any;
    prop192: any;
    prop193: any;
    prop194: any;
    prop195: any;
    prop196: any;
    prop197: any;
    prop198: any;
    prop199: any;
    prop200: any;
    prop201: any;
    prop202: any;
    prop203: any;
    prop204: any;
    prop205: any;
    prop206: any;
    prop207: any;
    prop208: any;
    prop209: any;
    prop210: any;
    prop211: any;
    prop212: any;
    prop213: any;
    prop214: any;
    prop215: any;
    prop216: any;
    prop217: any;
    prop218: any;
    prop219: any;
    prop220: any;
    prop221: any;
    prop222: any;
    prop223: any;
    prop224: any;
    prop225: any;
    prop226: any;
    prop227: any;
    prop228: any;
    prop229: any;
    prop230: any;
    prop231: any;
    prop232: any;
    prop233: any;
    prop234: any;
    prop235: any;
    prop236: any;
    prop237: any;
    prop238: any;
    prop239: any;
    prop240: any;
    prop241: any;
    prop242: any;
    prop243: any;
    prop244: any;
    prop245: any;
    prop246: any;
    prop247: any;
    prop248: any;
    prop249: any;
    prop250: any;
    prop251: any;
    prop252: any;
    prop253: any;
    prop254: any;
    prop255: any;
    prop256: any;
    prop257: any;
    prop258: any;
    prop259: any;
    prop260: any;
    prop261: any;
    prop262: any;
    prop263: any;
    prop264: any;
    prop265: any;
    prop266: any;
    prop267: any;
    prop268: any;
    prop269: any;
    prop270: any;
    prop271: any;
    prop272: any;
    prop273: any;
    prop274: any;
    prop275: any;
    prop276: any;
    prop277: any;
    prop278: any;
    prop279: any;
    prop280: any;
    prop281: any;
    prop282: any;
    prop283: any;
    prop284: any;
    prop285: any;
    prop286: any;
    prop287: any;
    prop288: any;
    prop289: any;
    prop290: any;
    prop291: any;
    prop292: any;
    prop293: any;
    prop294: any;
    prop295: any;
    prop296: any;
    prop297: any;
    prop298: any;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"o1", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityTruncation2, TestQuickinfoVerbosityTruncation2);

// quickinfoVerbosityTuple_test.go

// quickinfoVerbosityTuple_test.go
static void TestQuickinfoVerbosityTuple(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Orange {
    color: string;
}
interface Apple {
    color: string;
    other: Orange;
}
type TwoFruits/*T*/ = [Orange, Apple];
const tf/*f*/: TwoFruits = [
    { color: "orange" },
    { color: "red", other: { color: "orange" } }
];
const tf2/*f2*/: [Orange, Apple] = [
    { color: "orange" },
    { color: "red", other: { color: "orange" } }
];
type ManyFruits/*m*/ = (Orange | Apple)[];
const mf/*mf*/: ManyFruits = [];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"T", std::vector<int>{0, 1, 2}}, {"f", std::vector<int>{0, 1, 2, 3}}, {"f2", std::vector<int>{0, 1, 2}}, {"m", std::vector<int>{0, 1, 2}}, {"mf", std::vector<int>{0, 1, 2, 3}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityTuple, TestQuickinfoVerbosityTuple);

// quickinfoVerbosityTypeParameter_test.go

// quickinfoVerbosityTypeParameter_test.go
static void TestQuickinfoVerbosityTypeParameter(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type Str = string | {};
type FooType = Str | number;
function fn<T extends FooType>(x: T) {
    x/*x*/;
}
const y/*y*/: <T extends FooType>(x: T) => void = fn;
type MixinCtor<A> = new () => A/*a*/ & { constructor: MixinCtor<A> };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"x", std::vector<int>{0, 1, 2}}, {"y", std::vector<int>{0, 1, 2}}, {"a", std::vector<int>{0}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityTypeParameter, TestQuickinfoVerbosityTypeParameter);

// quickinfoVerbosityTypeof_test.go

// quickinfoVerbosityTypeof_test.go
static void TestQuickinfoVerbosityTypeof(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface Apple {
    color: string;
    weight: number;
}
const a: Apple = { color: "red", weight: 150 };
const b/*b*/: typeof a = { color: "green", weight: 120 };
class Banana {
    length: number;
    constructor(length: number) {
        this.length = length;
    }
}
const c/*c*/: typeof Banana = Banana;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHoverWithVerbosity(t, std::unordered_map<std::string, std::vector<int>>{{"b", std::vector<int>{0, 1}}, {"c", std::vector<int>{0, 1}}});
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoVerbosityTypeof, TestQuickinfoVerbosityTypeof);

// quickinfoWrongComment_test.go

// quickinfoWrongComment_test.go
static void TestQuickinfoWrongComment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @stableTypeOrdering: true
// @lib: es5
interface I {
    /** The colour */
    readonly colour: string
}
interface A extends I {
    readonly colour: "red" | "green";
}
interface B extends I {
    readonly colour: "yellow" | "green";
}
type F = A | B
const f: F = { colour: "green" }
f.colour/*1*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "1");
		f->VerifyQuickInfoIs(t, R"TS((property) colour: "green" | "red" | "yellow")TS", "The colour");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickinfoWrongComment, TestQuickinfoWrongComment);

} // namespace
