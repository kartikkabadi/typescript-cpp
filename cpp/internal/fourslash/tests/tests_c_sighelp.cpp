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

// newContentMapperFourslash — fourslash/tests/contentMapper_test.go.
[[maybe_unused]] static std::pair<std::shared_ptr<fourslash::FourslashTest>, std::function<void()>> newContentMapperFourslash(gostd::testing::T* t, std::string content, std::string mapper, const std::vector<std::string>& extensions) {
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

// signatureHelp01_test.go

// signatureHelp01_test.go
static void TestSignatureHelp01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
function foo(data: number) {
}

function bar {
    foo(/*1*/)
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.DocComment = "", .ParameterCount = 1});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelp01, TestSignatureHelp01);

// signatureHelpAfterParameterVS_test.go

// signatureHelpAfterParameterVS_test.go
static void TestSignatureHelpAfterParameterVS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type Type = (a, b, c) => void
const a: Type = (a/*1*/, b/*2*/) => {}
const b: Type = function (a/*3*/, b/*4*/) {}
const c: Type = ({ /*5*/a: { b/*6*/ }}/*7*/ = { }/*8*/, [b/*9*/]/*10*/, .../*11*/c/*12*/) => {})TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpAfterParameterVS, TestSignatureHelpAfterParameterVS);

// signatureHelpAfterParameter_test.go

// signatureHelpAfterParameter_test.go
static void TestSignatureHelpAfterParameter(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type Type = (a, b, c) => void
const a: Type = (a/*1*/, b/*2*/) => {}
const b: Type = function (a/*3*/, b/*4*/) {}
const c: Type = ({ /*5*/a: { b/*6*/ }}/*7*/ = { }/*8*/, [b/*9*/]/*10*/, .../*11*/c/*12*/) => {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpAfterParameter, TestSignatureHelpAfterParameter);

// signatureHelpAnonymousFunction_test.go

// signatureHelpAnonymousFunction_test.go
static void TestSignatureHelpAnonymousFunction(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var anonymousFunctionTest = function(n: number, s: string): (a: number, b: string) => string {
    return null;
}
anonymousFunctionTest(5, "")(/*anonymousFunction1*/1, /*anonymousFunction2*/"");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "anonymousFunction1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "(a: number, b: string): string", .ParameterCount = 2, .ParameterName = "a", .ParameterSpan = "a: number"});
		f->GoToMarker(t, "anonymousFunction2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.ParameterName = "b", .ParameterSpan = "b: string"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpAnonymousFunction, TestSignatureHelpAnonymousFunction);

// signatureHelpAnonymousTypeVS_test.go

// signatureHelpAnonymousTypeVS_test.go
static void TestSignatureHelpAnonymousTypeVS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(const comparers: Array<(a: any, b: any) => boolean> = [];

comparers.push((a,/**/ b) => true);)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpAnonymousTypeVS, TestSignatureHelpAnonymousTypeVS);

// signatureHelpApplicableRange_test.go

// signatureHelpApplicableRange_test.go
static void TestSignatureHelpApplicableRange(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(let obj = {
    foo(s: string): string {
        return s;
    }
};

let s =/*a*/ obj.foo("Hello, world!")/*b*/  
  /*c*/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoSignatureHelpForMarkers(t, {"a", "b", "c"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpApplicableRange, TestSignatureHelpApplicableRange);

// signatureHelpAtEOF2_test.go

// signatureHelpAtEOF2_test.go
static void TestSignatureHelpAtEOF2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(console.log()
/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoSignatureHelpForMarkersWithContext(t, std::make_shared<lsproto::SignatureHelpContext>(lsproto::SignatureHelpContext{.TriggerKind = lsproto::SignatureHelpTriggerKindInvoked}), {""});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpAtEOF2, TestSignatureHelpAtEOF2);

// signatureHelpAtEOF_test.go

// signatureHelpAtEOF_test.go
static void TestSignatureHelpAtEOF(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function Foo(arg1: string, arg2: string) {
}

Foo(/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "Foo(arg1: string, arg2: string): void", .ParameterCount = 2, .ParameterName = "arg1", .ParameterSpan = "arg1: string"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpAtEOF, TestSignatureHelpAtEOF);

// signatureHelpBeforeSemicolon1_test.go

// signatureHelpBeforeSemicolon1_test.go
static void TestSignatureHelpBeforeSemicolon1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function Foo(arg1: string, arg2: string) {
}

Foo(/**/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "Foo(arg1: string, arg2: string): void", .ParameterCount = 2, .ParameterName = "arg1", .ParameterSpan = "arg1: string"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpBeforeSemicolon1, TestSignatureHelpBeforeSemicolon1);

// signatureHelpBetweenTypeArgsAndArgs_test.go

// signatureHelpBetweenTypeArgsAndArgs_test.go
static void TestSignatureHelpTokenCrash2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
function foo<T, U>(x: string, y: T, z: U) {

}

foo<number,number>/*1*/("hello", 123,456)
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySignatureHelpWithCases(t, {std::make_shared<fourslash::SignatureHelpCase>(fourslash::SignatureHelpCase{.Context = std::make_shared<lsproto::SignatureHelpContext>(lsproto::SignatureHelpContext{.TriggerKind = lsproto::SignatureHelpTriggerKindTriggerCharacter, .TriggerCharacter = std::string("("), .IsRetrigger = false}), .MarkerInput = "1", .Expected = nullptr})});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpTokenCrash2, TestSignatureHelpTokenCrash2);

// signatureHelpBindingPatternVS_test.go

// signatureHelpBindingPatternVS_test.go
static void TestSignatureHelpBindingPatternVS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
/**
 * @param options An empty object binding pattern.
 */
function emptyObj({}) {}
emptyObj(/*emptyObj*/)

/**
 * @param items An empty array binding pattern.
 */
function emptyArr([]) {}
emptyArr(/*emptyArr*/)

/**
 * @param param An object with a and b properties.
 */
function nonEmptyObj({a, b}: {a: number, b: string}) {}
nonEmptyObj(/*nonEmptyObj*/)

/**
 * @param tuple A tuple with two elements.
 */
function nonEmptyArr([x, y]: [number, string]) {}
nonEmptyArr(/*nonEmptyArr*/)

/**
 * @param first The first number parameter.
 * @param second An object with a and b properties.
 */
function idLeading(first: number, {a, b}: {a: number, b: string}) {}
idLeading(123/*idLeading*/, { a: 1, b: 2 }/*bindingTrailing*/)

/**
 * @param first An object with a and b properties.
 * @param last The last number parameter.
 */
function bindingLeading({a, b}: {a: number, b: string}, last: number) {}
bindingLeading(/*bindingLeading*/{ a: 1, b: 2 }, 123 /*idTrailing*/)

/**
 * @param param1 {Object} The first parameter
 * @param param1.a {number} Comment a
 * @param param1.b {string} Comment b
 * @param param2 {Object} The second parameter
 * @param param2.c {boolean} Comment c
 * @param param2.d {unknown} Comment d
 */
function multipleBindings({ a, b }, { c, d }) {}
multipleBindings({ a: 0, b: "" }/*firstObjParam*/, { c: true, d: "" }/*secondObjParam*/)
)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpBindingPatternVS, TestSignatureHelpBindingPatternVS);

// signatureHelpBindingPattern_test.go

// signatureHelpBindingPattern_test.go
static void TestSignatureHelpBindingPattern(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
/**
 * @param options An empty object binding pattern.
 */
function emptyObj({}) {}
emptyObj(/*emptyObj*/)

/**
 * @param items An empty array binding pattern.
 */
function emptyArr([]) {}
emptyArr(/*emptyArr*/)

/**
 * @param param An object with a and b properties.
 */
function nonEmptyObj({a, b}: {a: number, b: string}) {}
nonEmptyObj(/*nonEmptyObj*/)

/**
 * @param tuple A tuple with two elements.
 */
function nonEmptyArr([x, y]: [number, string]) {}
nonEmptyArr(/*nonEmptyArr*/)

/**
 * @param first The first number parameter.
 * @param second An object with a and b properties.
 */
function idLeading(first: number, {a, b}: {a: number, b: string}) {}
idLeading(123/*idLeading*/, { a: 1, b: 2 }/*bindingTrailing*/)

/**
 * @param first An object with a and b properties.
 * @param last The last number parameter.
 */
function bindingLeading({a, b}: {a: number, b: string}, last: number) {}
bindingLeading(/*bindingLeading*/{ a: 1, b: 2 }, 123 /*idTrailing*/)

/**
 * @param param1 {Object} The first parameter
 * @param param1.a {number} Comment a
 * @param param1.b {string} Comment b
 * @param param2 {Object} The second parameter
 * @param param2.c {boolean} Comment c
 * @param param2.d {unknown} Comment d
 */
function multipleBindings({ a, b }, { c, d }) {}
multipleBindings({ a: 0, b: "" }/*firstObjParam*/, { c: true, d: "" }/*secondObjParam*/)
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpBindingPattern, TestSignatureHelpBindingPattern);

// signatureHelpCallExpressionJs_test.go

// signatureHelpCallExpressionJs_test.go
static void TestSignatureHelpCallExpressionJs(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(// @strict: false
// @checkJs: true
// @allowJs: true
// @Filename: main.js
function allOptional() { arguments; }
allOptional(/*1*/);
allOptional(1, 2, 3);
function someOptional(x, y) { arguments; }
someOptional(/*2*/);
someOptional(1, 2, 3);
someOptional(); // no error here; x and y are optional in JS)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoErrors(t);
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "allOptional(...args: any[]): void", .ParameterCount = 1, .ParameterName = "args", .ParameterSpan = "...args: any[]", .IsVariadic = true, .IsVariadicSet = true});
		f->GoToMarker(t, "2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "someOptional(x: any, y: any, ...args: any[]): void", .ParameterCount = 3, .ParameterName = "x", .ParameterSpan = "x: any", .IsVariadic = true, .IsVariadicSet = true});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpCallExpressionJs, TestSignatureHelpCallExpressionJs);

// signatureHelpCallExpressionTuples_test.go

// signatureHelpCallExpressionTuples_test.go
static void TestSignatureHelpCallExpressionTuples(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function fnTest(str: string, num: number) { }
declare function wrap<A extends any[], R>(fn: (...a: A) => R) : (...a: A) => R;
var fnWrapped = wrap(fnTest);
fnWrapped/*3*/(/*1*/'', /*2*/5);
function fnTestVariadic (str: string, ...num: number[]) { }
var fnVariadicWrapped = wrap(fnTestVariadic);
fnVariadicWrapped/*4*/(/*5*/'', /*6*/5);
function fnNoParams () { }
var fnNoParamsWrapped = wrap(fnNoParams);
fnNoParamsWrapped/*7*/(/*8*/);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "3", "var fnWrapped: (str: string, num: number) => void", "");
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "fnWrapped(str: string, num: number): void", .ParameterCount = 2, .ParameterName = "str", .ParameterSpan = "str: string"});
		f->GoToMarker(t, "2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.ParameterName = "num", .ParameterSpan = "num: number"});
		f->VerifyQuickInfoAt(t, "4", "var fnVariadicWrapped: (str: string, ...num: number[]) => void", "");
		f->GoToMarker(t, "5");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "fnVariadicWrapped(str: string, ...num: number[]): void", .ParameterCount = 2, .ParameterName = "str", .ParameterSpan = "str: string", .IsVariadic = true, .IsVariadicSet = true});
		f->GoToMarker(t, "6");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.ParameterName = "num", .ParameterSpan = "...num: number[]", .IsVariadic = true, .IsVariadicSet = true});
		f->VerifyQuickInfoAt(t, "7", "var fnNoParamsWrapped: () => void", "");
		f->GoToMarker(t, "8");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "fnNoParamsWrapped(): void", .ParameterCount = 0});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpCallExpressionTuples, TestSignatureHelpCallExpressionTuples);

// signatureHelpCallExpression_test.go

// signatureHelpCallExpression_test.go
static void TestSignatureHelpCallExpression(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function fnTest(str: string, num: number) { }
fnTest(/*1*/'', /*2*/5);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "fnTest(str: string, num: number): void", .ParameterCount = 2, .ParameterName = "str", .ParameterSpan = "str: string"});
		f->GoToMarker(t, "2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.ParameterName = "num", .ParameterSpan = "num: number"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpCallExpression, TestSignatureHelpCallExpression);

// signatureHelpCommentsClassMembersVS_test.go

// signatureHelpCommentsClassMembersVS_test.go
static void TestSignatureHelpCommentsClassMembersVS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** This is comment for c1*/
class c1 {
    /** p1 is property of c1*/
    public p1: number;
    /** sum with property*/
    public p2(/** number to add*/b: number) {
        return this.p1 + b;
    }
    /** getter property 1*/
    public get p3() {
        return this.p2(/*8*/this.p1);
    }
    /** setter property 1*/
    public set p3(/** this is value*/value: number) {
        this.p1 = this.p2(/*13*/value);
    }
    /** pp1 is property of c1*/
    private pp1: number;
    /** sum with property*/
    private pp2(/** number to add*/b: number) {
        return this.p1 + b;
    }
    /** getter property 2*/
    private get pp3() {
        return this.pp2(/*20*/this.pp1);
    }
    /** setter property 2*/
    private set pp3( /** this is value*/value: number) {
        this.pp1 = this.pp2(/*25*/value);
    }
    /** Constructor method*/
    constructor() {
    }
    /** s1 is static property of c1*/
    static s1: number;
    /** static sum with property*/
    static s2(/** number to add*/b: number) {
        return c1.s1 + b;
    }
    /** static getter property*/
    static get s3() {
        return c1.s2(/*35*/c1.s1);
    }
    /** setter property 3*/
    static set s3( /** this is value*/value: number) {
        c1.s1 = c1.s2(/*42*/value);
    }
    public nc_p1: number;
    public nc_p2(b: number) {
        return this.nc_p1 + b;
    }
    public get nc_p3() {
        return this.nc_p2(/*47*/this.nc_p1);
    }
    public set nc_p3(value: number) {
        this.nc_p1 = this.nc_p2(/*49*/value);
    }
    private nc_pp1: number;
    private nc_pp2(b: number) {
        return this.nc_pp1 + b;
    }
    private get nc_pp3() {
        return this.nc_pp2(/*54*/this.nc_pp1);
    }
    private set nc_pp3(value: number) {
        this.nc_pp1 = this.nc_pp2(/*56*/value);
    }
    static nc_s1: number;
    static nc_s2(b: number) {
        return c1.nc_s1 + b;
    }
    static get nc_s3() {
        return c1.nc_s2(/*61*/c1.nc_s1);
    }
    static set nc_s3(value: number) {
        c1.nc_s1 = c1.nc_s2(/*63*/value);
    }
}
var i1 = new c1(/*65*/);
var i1_p = i1.p1;
var i1_f = i1.p2;
var i1_r = i1.p2(/*71*/20);
var i1_prop = i1.p3;
i1.p3 = i1_prop;
var i1_nc_p = i1.nc_p1;
var i1_ncf = i1.nc_p2;
var i1_ncr = i1.nc_p2(/*81*/20);
var i1_ncprop = i1.nc_p3;
i1.nc_p3 = i1_ncprop;
var i1_s_p = c1.s1;
var i1_s_f = c1.s2;
var i1_s_r = c1.s2(/*92*/20);
var i1_s_prop = c1.s3;
c1.s3 = i1_s_prop;
var i1_s_nc_p = c1.nc_s1;
var i1_s_ncf = c1.nc_s2;
var i1_s_ncr = c1.nc_s2(/*102*/20);
var i1_s_ncprop = c1.nc_s3;
c1.nc_s3 = i1_s_ncprop;
var i1_c = c1;

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
cProperties_i.p2 = cProperties_i.p1;
cProperties_i.nc_p2 = cProperties_i.nc_p1;
class cWithConstructorProperty {
    /**
    * this is class cWithConstructorProperty's constructor
    * @param a this is first parameter a
    */
    constructor(/**more info about a*/public a: number) {
        var bbbb = 10;
        this.a = a + 2 + bbbb;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpCommentsClassMembersVS, TestSignatureHelpCommentsClassMembersVS);

// signatureHelpCommentsClassMembers_test.go

// signatureHelpCommentsClassMembers_test.go
static void TestSignatureHelpCommentsClassMembers(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** This is comment for c1*/
class c1 {
    /** p1 is property of c1*/
    public p1: number;
    /** sum with property*/
    public p2(/** number to add*/b: number) {
        return this.p1 + b;
    }
    /** getter property 1*/
    public get p3() {
        return this.p2(/*8*/this.p1);
    }
    /** setter property 1*/
    public set p3(/** this is value*/value: number) {
        this.p1 = this.p2(/*13*/value);
    }
    /** pp1 is property of c1*/
    private pp1: number;
    /** sum with property*/
    private pp2(/** number to add*/b: number) {
        return this.p1 + b;
    }
    /** getter property 2*/
    private get pp3() {
        return this.pp2(/*20*/this.pp1);
    }
    /** setter property 2*/
    private set pp3( /** this is value*/value: number) {
        this.pp1 = this.pp2(/*25*/value);
    }
    /** Constructor method*/
    constructor() {
    }
    /** s1 is static property of c1*/
    static s1: number;
    /** static sum with property*/
    static s2(/** number to add*/b: number) {
        return c1.s1 + b;
    }
    /** static getter property*/
    static get s3() {
        return c1.s2(/*35*/c1.s1);
    }
    /** setter property 3*/
    static set s3( /** this is value*/value: number) {
        c1.s1 = c1.s2(/*42*/value);
    }
    public nc_p1: number;
    public nc_p2(b: number) {
        return this.nc_p1 + b;
    }
    public get nc_p3() {
        return this.nc_p2(/*47*/this.nc_p1);
    }
    public set nc_p3(value: number) {
        this.nc_p1 = this.nc_p2(/*49*/value);
    }
    private nc_pp1: number;
    private nc_pp2(b: number) {
        return this.nc_pp1 + b;
    }
    private get nc_pp3() {
        return this.nc_pp2(/*54*/this.nc_pp1);
    }
    private set nc_pp3(value: number) {
        this.nc_pp1 = this.nc_pp2(/*56*/value);
    }
    static nc_s1: number;
    static nc_s2(b: number) {
        return c1.nc_s1 + b;
    }
    static get nc_s3() {
        return c1.nc_s2(/*61*/c1.nc_s1);
    }
    static set nc_s3(value: number) {
        c1.nc_s1 = c1.nc_s2(/*63*/value);
    }
}
var i1 = new c1(/*65*/);
var i1_p = i1.p1;
var i1_f = i1.p2;
var i1_r = i1.p2(/*71*/20);
var i1_prop = i1.p3;
i1.p3 = i1_prop;
var i1_nc_p = i1.nc_p1;
var i1_ncf = i1.nc_p2;
var i1_ncr = i1.nc_p2(/*81*/20);
var i1_ncprop = i1.nc_p3;
i1.nc_p3 = i1_ncprop;
var i1_s_p = c1.s1;
var i1_s_f = c1.s2;
var i1_s_r = c1.s2(/*92*/20);
var i1_s_prop = c1.s3;
c1.s3 = i1_s_prop;
var i1_s_nc_p = c1.nc_s1;
var i1_s_ncf = c1.nc_s2;
var i1_s_ncr = c1.nc_s2(/*102*/20);
var i1_s_ncprop = c1.nc_s3;
c1.nc_s3 = i1_s_ncprop;
var i1_c = c1;

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
cProperties_i.p2 = cProperties_i.p1;
cProperties_i.nc_p2 = cProperties_i.nc_p1;
class cWithConstructorProperty {
    /**
    * this is class cWithConstructorProperty's constructor
    * @param a this is first parameter a
    */
    constructor(/**more info about a*/public a: number) {
        var bbbb = 10;
        this.a = a + 2 + bbbb;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpCommentsClassMembers, TestSignatureHelpCommentsClassMembers);

// signatureHelpCommentsClassVS_test.go

// signatureHelpCommentsClassVS_test.go
static void TestSignatureHelpCommentsClassVS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** This is class c2 without constructor*/
class c2 {
}
var i2 = new c2(/*3*/);
var i2_c = c2;
class c3 {
    /** Constructor comment*/
    constructor() {
    }
}
var i3 = new c3(/*8*/);
var i3_c = c3;
/** Class comment*/
class c4 {
    /** Constructor comment*/
    constructor() {
    }
}
var i4 = new c4(/*13*/);
var i4_c = c4;
/** Class with statics*/
class c5 {
    static s1: number;
}
var i5 = new c5(/*18*/);
var i5_c = c5;
/** class with statics and constructor*/
class c6 {
    /** s1 comment*/
    static s1: number;
    /** constructor comment*/
    constructor() {
    }
}
var i6 = new c6(/*23*/);
var i6_c = c6;

class a {
    /**
    constructor for a
    @param a this is my a
    */
    constructor(a: string) {
    }
}
new a(/*27*/"Hello");
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
var myVar = new m.m2.c1();)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpCommentsClassVS, TestSignatureHelpCommentsClassVS);

// signatureHelpCommentsClass_test.go

// signatureHelpCommentsClass_test.go
static void TestSignatureHelpCommentsClass(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** This is class c2 without constructor*/
class c2 {
}
var i2 = new c2(/*3*/);
var i2_c = c2;
class c3 {
    /** Constructor comment*/
    constructor() {
    }
}
var i3 = new c3(/*8*/);
var i3_c = c3;
/** Class comment*/
class c4 {
    /** Constructor comment*/
    constructor() {
    }
}
var i4 = new c4(/*13*/);
var i4_c = c4;
/** Class with statics*/
class c5 {
    static s1: number;
}
var i5 = new c5(/*18*/);
var i5_c = c5;
/** class with statics and constructor*/
class c6 {
    /** s1 comment*/
    static s1: number;
    /** constructor comment*/
    constructor() {
    }
}
var i6 = new c6(/*23*/);
var i6_c = c6;

class a {
    /**
    constructor for a
    @param a this is my a
    */
    constructor(a: string) {
    }
}
new a(/*27*/"Hello");
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
var myVar = new m.m2.c1();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpCommentsClass, TestSignatureHelpCommentsClass);

// signatureHelpCommentsCommentParsingVS_test.go

// signatureHelpCommentsCommentParsingVS_test.go
static void TestSignatureHelpCommentsCommentParsingVS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/// This is simple /// comments
function simple() {
}

simple( /*1*/);

/// multiLine /// Comments
/// This is example of multiline /// comments
/// Another multiLine
function multiLine() {
}
multiLine( /*2*/);

/** this is eg of single line jsdoc style comment */
function jsDocSingleLine() {
}
jsDocSingleLine(/*3*/);


/** this is multiple line jsdoc stule comment
*New line1
*New Line2*/
function jsDocMultiLine() {
}
jsDocMultiLine(/*4*/);

/** multiple line jsdoc comments no longer merge
*New line1
*New Line2*/
/** Shoul mege this line as well
* and this too*/ /** Another this one too*/
function jsDocMultiLineMerge() {
}
jsDocMultiLineMerge(/*5*/);


/// Triple slash comment
/** jsdoc comment */
function jsDocMixedComments1() {
}
jsDocMixedComments1(/*6*/);

/// Triple slash comment
/** jsdoc comment */ /** another jsDocComment*/
function jsDocMixedComments2() {
}
jsDocMixedComments2(/*7*/);

/** jsdoc comment */ /*** triplestar jsDocComment*/
/// Triple slash comment
function jsDocMixedComments3() {
}
jsDocMixedComments3(/*8*/);

/** jsdoc comment */ /** another jsDocComment*/
/// Triple slash comment
/// Triple slash comment 2
function jsDocMixedComments4() {
}
jsDocMixedComments4(/*9*/);

/// Triple slash comment 1
/** jsdoc comment */ /** another jsDocComment*/
/// Triple slash comment
/// Triple slash comment 2
function jsDocMixedComments5() {
}
jsDocMixedComments5(/*10*/);

/** another jsDocComment*/
/// Triple slash comment 1
/// Triple slash comment
/// Triple slash comment 2
/** jsdoc comment */
function jsDocMixedComments6() {
}
jsDocMixedComments6(/*11*/);

// This shoulnot be help comment
function noHelpComment1() {
}
noHelpComment1(/*12*/);

/* This shoulnot be help comment */
function noHelpComment2() {
}
noHelpComment2(/*13*/);

function noHelpComment3() {
}
noHelpComment3(/*14*/);
/** Adds two integers and returns the result
  * @param {number} a first number
  * @param b second number
  */
function sum(a: number, b: number) {
    return a + b;
}
sum(/*16*/10, /*17*/20);
/** This is multiplication function
 * @param 
 * @param a first number
 * @param b
 * @param c {
 @param d @anotherTag
 * @param e LastParam @anotherTag*/
function multiply(a: number, b: number, c?: number, d?, e?) {
}
multiply(/*19*/10,/*20*/ 20,/*21*/ 30, /*22*/40, /*23*/50);
/** fn f1 with number
* @param { string} b about b
*/
function f1(a: number);
function f1(b: string);
/**@param opt optional parameter*/
function f1(aOrb, opt?) {
    return aOrb;
}
f1(/*25*/10);
f1(/*26*/"hello");

/** This is subtract function
@param { a
*@param { number | } b this is about b
@param { { () => string; } } c this is optional param c
@param { { () => string; } d this is optional param d
@param { { () => string; } } e this is optional param e
@param { { { () => string; } } f this is optional param f
*/
function subtract(a: number, b: number, c?: () => string, d?: () => string, e?: () => string, f?: () => string) {
}
subtract(/*28*/10, /*29*/ 20, /*30*/ null, /*31*/ null, /*32*/ null, /*33*/null);
/** this is square function
@paramTag { number } a this is input number of paramTag
@param { number } a this is input number
@returnType { number } it is return type
*/
function square(a: number) {
    return a * a;
}
square(/*34*/10);
/** this is divide function
@param { number} a this is a
@paramTag { number } g this is optional param g
@param { number} b this is b
*/
function divide(a: number, b: number) {
}
divide(/*35*/10, /*36*/20);
/**
Function returns string concat of foo and bar
@param			{string}		foo		is string
@param		    {string}		bar		is second string
*/
function fooBar(foo: string, bar: string) {
    return foo + bar;
}
fooBar(/*37*/"foo",/*38*/"bar");
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
function jsDocParamTest(/** this is inline comment for a */a: number, /** this is inline comment for b*/ b: number, c: number, d: number) {
    return /*39*/a + b + c + d;
}
jsDocParamTest(/*40*/30, /*41*/40, /*42*/50, /*43*/60);
/** This is function comment
  * And properly aligned comment
  */
function jsDocCommentAlignmentTest1() {
}
jsDocCommentAlignmentTest1(/*45*/);
/** This is function comment
  *     And aligned with 4 space char margin
  */
function jsDocCommentAlignmentTest2() {
}
jsDocCommentAlignmentTest2(/*46*/);
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
function jsDocCommentAlignmentTest3(a: string, b, c) {
}
jsDocCommentAlignmentTest3(/*47*/"hello",/*48*/1, /*49*/2);
/**/
class NoQuickInfoClass {
})TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpCommentsCommentParsingVS, TestSignatureHelpCommentsCommentParsingVS);

// signatureHelpCommentsCommentParsing_test.go

// signatureHelpCommentsCommentParsing_test.go
static void TestSignatureHelpCommentsCommentParsing(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/// This is simple /// comments
function simple() {
}

simple( /*1*/);

/// multiLine /// Comments
/// This is example of multiline /// comments
/// Another multiLine
function multiLine() {
}
multiLine( /*2*/);

/** this is eg of single line jsdoc style comment */
function jsDocSingleLine() {
}
jsDocSingleLine(/*3*/);


/** this is multiple line jsdoc stule comment
*New line1
*New Line2*/
function jsDocMultiLine() {
}
jsDocMultiLine(/*4*/);

/** multiple line jsdoc comments no longer merge
*New line1
*New Line2*/
/** Shoul mege this line as well
* and this too*/ /** Another this one too*/
function jsDocMultiLineMerge() {
}
jsDocMultiLineMerge(/*5*/);


/// Triple slash comment
/** jsdoc comment */
function jsDocMixedComments1() {
}
jsDocMixedComments1(/*6*/);

/// Triple slash comment
/** jsdoc comment */ /** another jsDocComment*/
function jsDocMixedComments2() {
}
jsDocMixedComments2(/*7*/);

/** jsdoc comment */ /*** triplestar jsDocComment*/
/// Triple slash comment
function jsDocMixedComments3() {
}
jsDocMixedComments3(/*8*/);

/** jsdoc comment */ /** another jsDocComment*/
/// Triple slash comment
/// Triple slash comment 2
function jsDocMixedComments4() {
}
jsDocMixedComments4(/*9*/);

/// Triple slash comment 1
/** jsdoc comment */ /** another jsDocComment*/
/// Triple slash comment
/// Triple slash comment 2
function jsDocMixedComments5() {
}
jsDocMixedComments5(/*10*/);

/** another jsDocComment*/
/// Triple slash comment 1
/// Triple slash comment
/// Triple slash comment 2
/** jsdoc comment */
function jsDocMixedComments6() {
}
jsDocMixedComments6(/*11*/);

// This shoulnot be help comment
function noHelpComment1() {
}
noHelpComment1(/*12*/);

/* This shoulnot be help comment */
function noHelpComment2() {
}
noHelpComment2(/*13*/);

function noHelpComment3() {
}
noHelpComment3(/*14*/);
/** Adds two integers and returns the result
  * @param {number} a first number
  * @param b second number
  */
function sum(a: number, b: number) {
    return a + b;
}
sum(/*16*/10, /*17*/20);
/** This is multiplication function
 * @param 
 * @param a first number
 * @param b
 * @param c {
 @param d @anotherTag
 * @param e LastParam @anotherTag*/
function multiply(a: number, b: number, c?: number, d?, e?) {
}
multiply(/*19*/10,/*20*/ 20,/*21*/ 30, /*22*/40, /*23*/50);
/** fn f1 with number
* @param { string} b about b
*/
function f1(a: number);
function f1(b: string);
/**@param opt optional parameter*/
function f1(aOrb, opt?) {
    return aOrb;
}
f1(/*25*/10);
f1(/*26*/"hello");

/** This is subtract function
@param { a
*@param { number | } b this is about b
@param { { () => string; } } c this is optional param c
@param { { () => string; } d this is optional param d
@param { { () => string; } } e this is optional param e
@param { { { () => string; } } f this is optional param f
*/
function subtract(a: number, b: number, c?: () => string, d?: () => string, e?: () => string, f?: () => string) {
}
subtract(/*28*/10, /*29*/ 20, /*30*/ null, /*31*/ null, /*32*/ null, /*33*/null);
/** this is square function
@paramTag { number } a this is input number of paramTag
@param { number } a this is input number
@returnType { number } it is return type
*/
function square(a: number) {
    return a * a;
}
square(/*34*/10);
/** this is divide function
@param { number} a this is a
@paramTag { number } g this is optional param g
@param { number} b this is b
*/
function divide(a: number, b: number) {
}
divide(/*35*/10, /*36*/20);
/**
Function returns string concat of foo and bar
@param			{string}		foo		is string
@param		    {string}		bar		is second string
*/
function fooBar(foo: string, bar: string) {
    return foo + bar;
}
fooBar(/*37*/"foo",/*38*/"bar");
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
function jsDocParamTest(/** this is inline comment for a */a: number, /** this is inline comment for b*/ b: number, c: number, d: number) {
    return /*39*/a + b + c + d;
}
jsDocParamTest(/*40*/30, /*41*/40, /*42*/50, /*43*/60);
/** This is function comment
  * And properly aligned comment
  */
function jsDocCommentAlignmentTest1() {
}
jsDocCommentAlignmentTest1(/*45*/);
/** This is function comment
  *     And aligned with 4 space char margin
  */
function jsDocCommentAlignmentTest2() {
}
jsDocCommentAlignmentTest2(/*46*/);
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
function jsDocCommentAlignmentTest3(a: string, b, c) {
}
jsDocCommentAlignmentTest3(/*47*/"hello",/*48*/1, /*49*/2);
/**/
class NoQuickInfoClass {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpCommentsCommentParsing, TestSignatureHelpCommentsCommentParsing);

// signatureHelpCommentsFunctionDeclarationVS_test.go

// signatureHelpCommentsFunctionDeclarationVS_test.go
static void TestSignatureHelpCommentsFunctionDeclarationVS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** This comment should appear for foo*/
function foo() {
}
foo(/*4*/);
/** This is comment for function signature*/
function fooWithParameters(/** this is comment about a*/a: string,
    /** this is comment for b*/
    b: number) {
    var d = a;
}
fooWithParameters(/*10*/"a",/*11*/10);
/**
* Does something
* @param a a string
*/
declare function fn(a: string);
fn(/*12*/"hello");)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpCommentsFunctionDeclarationVS, TestSignatureHelpCommentsFunctionDeclarationVS);

// signatureHelpCommentsFunctionDeclaration_test.go

// signatureHelpCommentsFunctionDeclaration_test.go
static void TestSignatureHelpCommentsFunctionDeclaration(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** This comment should appear for foo*/
function foo() {
}
foo(/*4*/);
/** This is comment for function signature*/
function fooWithParameters(/** this is comment about a*/a: string,
    /** this is comment for b*/
    b: number) {
    var d = a;
}
fooWithParameters(/*10*/"a",/*11*/10);
/**
* Does something
* @param a a string
*/
declare function fn(a: string);
fn(/*12*/"hello");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpCommentsFunctionDeclaration, TestSignatureHelpCommentsFunctionDeclaration);

// signatureHelpCommentsFunctionExpressionVS_test.go

// signatureHelpCommentsFunctionExpressionVS_test.go
static void TestSignatureHelpCommentsFunctionExpressionVS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** lambdaFoo var comment*/
var lambdaFoo = /** this is lambda comment*/ (/**param a*/a: number, /**param b*/b: number) => a + b;
var lambddaNoVarComment = /** this is lambda multiplication*/ (/**param a*/a: number, /**param b*/b: number) => a * b;
lambdaFoo(/*5*/10, /*6*/20);
function anotherFunc(a: number) {
    /** documentation
        @param b {string} inner parameter */
    var lambdaVar = /** inner docs */(b: string) => {
        var localVar = "Hello ";
        return localVar + b;
    }
    return lambdaVar("World") + a;
}
/**
 * On variable
 * @param s the first parameter!
 * @returns the parameter's length
 */
var assigned = /**
                * Summary on expression
                * @param s param on expression
                * @returns return on expression
                */function(/** On parameter */s: string) {
  return s.length;
}
assigned(/*18*/"hey");)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpCommentsFunctionExpressionVS, TestSignatureHelpCommentsFunctionExpressionVS);

// signatureHelpCommentsFunctionExpression_test.go

// signatureHelpCommentsFunctionExpression_test.go
static void TestSignatureHelpCommentsFunctionExpression(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** lambdaFoo var comment*/
var lambdaFoo = /** this is lambda comment*/ (/**param a*/a: number, /**param b*/b: number) => a + b;
var lambddaNoVarComment = /** this is lambda multiplication*/ (/**param a*/a: number, /**param b*/b: number) => a * b;
lambdaFoo(/*5*/10, /*6*/20);
function anotherFunc(a: number) {
    /** documentation
        @param b {string} inner parameter */
    var lambdaVar = /** inner docs */(b: string) => {
        var localVar = "Hello ";
        return localVar + b;
    }
    return lambdaVar("World") + a;
}
/**
 * On variable
 * @param s the first parameter!
 * @returns the parameter's length
 */
var assigned = /**
                * Summary on expression
                * @param s param on expression
                * @returns return on expression
                */function(/** On parameter */s: string) {
  return s.length;
}
assigned(/*18*/"hey");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpCommentsFunctionExpression, TestSignatureHelpCommentsFunctionExpression);

// signatureHelpConstructExpression_test.go

// signatureHelpConstructExpression_test.go
static void TestSignatureHelpConstructExpression(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class sampleCls { constructor(str: string, num: number) { } }
var x = new sampleCls(/*1*/"", /*2*/5);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "sampleCls(str: string, num: number): sampleCls", .ParameterCount = 2, .ParameterName = "str", .ParameterSpan = "str: string"});
		f->GoToMarker(t, "2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.ParameterName = "num", .ParameterSpan = "num: number"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpConstructExpression, TestSignatureHelpConstructExpression);

// signatureHelpConstructorCallParamPropertiesVS_test.go

// signatureHelpConstructorCallParamPropertiesVS_test.go
static void TestSignatureHelpConstructorCallParamPropertiesVS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Circle {
    /**
      * Initialize a circle.
      * @param  radius The radius of the circle.
      */
    constructor(private radius: number) {
    }
}
var a = new Circle(/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpConstructorCallParamPropertiesVS, TestSignatureHelpConstructorCallParamPropertiesVS);

// signatureHelpConstructorCallParamProperties_test.go

// signatureHelpConstructorCallParamProperties_test.go
static void TestSignatureHelpConstructorCallParamProperties(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Circle {
    /**
      * Initialize a circle.
      * @param  radius The radius of the circle.
      */
    constructor(private radius: number) {
    }
}
var a = new Circle(/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpConstructorCallParamProperties, TestSignatureHelpConstructorCallParamProperties);

// signatureHelpConstructorInheritance_test.go

// signatureHelpConstructorInheritance_test.go
static void TestSignatureHelpConstructorInheritance(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class base {
    constructor(s: string);
    constructor(n: number);
    constructor(a: any) { }
}
class B1 extends base { }
class B2 extends B1 { }
class B3 extends B2 {
    constructor() {
        super(/*indirectSuperCall*/3);
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "indirectSuperCall");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "B2(n: number): B2", .ParameterCount = 1, .ParameterName = "n", .ParameterSpan = "n: number", .OverloadsCount = 2});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpConstructorInheritance, TestSignatureHelpConstructorInheritance);

// signatureHelpConstructorOverload_test.go

// signatureHelpConstructorOverload_test.go
static void TestSignatureHelpConstructorOverload(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class clsOverload { constructor(); constructor(test: string); constructor(test?: string) { } }
var x = new clsOverload(/*1*/);
var y = new clsOverload(/*2*/'');)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "clsOverload(): clsOverload", .ParameterCount = 0, .OverloadsCount = 2});
		f->GoToMarker(t, "2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "clsOverload(test: string): clsOverload", .ParameterCount = 1, .ParameterName = "test", .ParameterSpan = "test: string", .OverloadsCount = 2});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpConstructorOverload, TestSignatureHelpConstructorOverload);

// signatureHelpContextualConstructSignatureNoCrash_test.go

// signatureHelpContextualConstructSignatureNoCrash_test.go
static void TestSignatureHelpContextualConstructSignatureNoCrash(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
type Obj = {
    foo: new () => object
}

let obj: Obj = {
    foo(/*constructOnly*/) {}
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "constructOnly");
		f->VerifyNoSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpContextualConstructSignatureNoCrash, TestSignatureHelpContextualConstructSignatureNoCrash);

// signatureHelpEmptyList_test.go

// signatureHelpEmptyList_test.go
static void TestSignatureHelpEmptyList(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function Foo(arg1: string, arg2: string) {
}

Foo(/*1*/);
function Bar<T>(arg1: string, arg2: string) { }
Bar</*2*/>();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "Foo(arg1: string, arg2: string): void", .ParameterCount = 2, .ParameterName = "arg1", .ParameterSpan = "arg1: string"});
		f->GoToMarker(t, "2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "Bar<T>(arg1: string, arg2: string): void"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpEmptyList, TestSignatureHelpEmptyList);

// signatureHelpExpandedRestTuplesLocalLabels1VS_test.go

// signatureHelpExpandedRestTuplesLocalLabels1VS_test.go
static void TestSignatureHelpExpandedRestTuplesLocalLabels1VS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface AppleInfo {
  color: "green" | "red";
}

interface BananaInfo {
  curvature: number;
}

type FruitAndInfo1 = ["apple", AppleInfo] | ["banana", BananaInfo];

function logFruitTuple1(...[fruit, info]: FruitAndInfo1) {}
logFruitTuple1(/*1*/);

function logFruitTuple2(...[, info]: FruitAndInfo1) {}
logFruitTuple2(/*2*/);
logFruitTuple2("apple", /*3*/);

function logFruitTuple3(...[fruit, ...rest]: FruitAndInfo1) {}
logFruitTuple3(/*4*/);
logFruitTuple3("apple", /*5*/);
function logFruitTuple4(...[fruit, ...[info]]: FruitAndInfo1) {}
logFruitTuple4(/*6*/);
logFruitTuple4("apple", /*7*/);

type FruitAndInfo2 = ["apple", ...AppleInfo[]] | ["banana", ...BananaInfo[]];

function logFruitTuple5(...[fruit, firstInfo]: FruitAndInfo2) {}
logFruitTuple5(/*8*/);
logFruitTuple5("apple", /*9*/);

function logFruitTuple6(...[fruit, ...fruitInfo]: FruitAndInfo2) {}
logFruitTuple6(/*10*/);
logFruitTuple6("apple", /*11*/);

type FruitAndInfo3 = ["apple", ...AppleInfo[], number] | ["banana", ...BananaInfo[], number];

function logFruitTuple7(...[fruit, fruitInfoOrNumber, secondFruitInfoOrNumber]: FruitAndInfo3) {}
logFruitTuple7(/*12*/);
logFruitTuple7("apple", /*13*/);
logFruitTuple7("apple", { color: "red" }, /*14*/);

function logFruitTuple8(...[fruit, , secondFruitInfoOrNumber]: FruitAndInfo3) {}
logFruitTuple8(/*15*/);
logFruitTuple8("apple", /*16*/);
logFruitTuple8("apple", { color: "red" }, /*17*/);

function logFruitTuple9(...[...[fruit, fruitInfoOrNumber, secondFruitInfoOrNumber]]: FruitAndInfo3) {}
logFruitTuple9(/*18*/);
logFruitTuple9("apple", /*19*/);
logFruitTuple9("apple", { color: "red" }, /*20*/);

function logFruitTuple10(...[fruit, {}, secondFruitInfoOrNumber]: FruitAndInfo3) {}
logFruitTuple10(/*21*/);
logFruitTuple10("apple", /*22*/);
logFruitTuple10("apple", { color: "red" }, /*23*/);

function logFruitTuple11(...{}: FruitAndInfo3) {}
logFruitTuple11(/*24*/);
logFruitTuple11("apple", /*25*/);
logFruitTuple11("apple", { color: "red" }, /*26*/);
function withPair(...[first, second]: [number, named: string]) {}
withPair(/*27*/);
withPair(101, /*28*/);)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpExpandedRestTuplesLocalLabels1VS, TestSignatureHelpExpandedRestTuplesLocalLabels1VS);

// signatureHelpExpandedRestTuplesLocalLabels1_test.go

// signatureHelpExpandedRestTuplesLocalLabels1_test.go
static void TestSignatureHelpExpandedRestTuplesLocalLabels1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface AppleInfo {
  color: "green" | "red";
}

interface BananaInfo {
  curvature: number;
}

type FruitAndInfo1 = ["apple", AppleInfo] | ["banana", BananaInfo];

function logFruitTuple1(...[fruit, info]: FruitAndInfo1) {}
logFruitTuple1(/*1*/);

function logFruitTuple2(...[, info]: FruitAndInfo1) {}
logFruitTuple2(/*2*/);
logFruitTuple2("apple", /*3*/);

function logFruitTuple3(...[fruit, ...rest]: FruitAndInfo1) {}
logFruitTuple3(/*4*/);
logFruitTuple3("apple", /*5*/);
function logFruitTuple4(...[fruit, ...[info]]: FruitAndInfo1) {}
logFruitTuple4(/*6*/);
logFruitTuple4("apple", /*7*/);

type FruitAndInfo2 = ["apple", ...AppleInfo[]] | ["banana", ...BananaInfo[]];

function logFruitTuple5(...[fruit, firstInfo]: FruitAndInfo2) {}
logFruitTuple5(/*8*/);
logFruitTuple5("apple", /*9*/);

function logFruitTuple6(...[fruit, ...fruitInfo]: FruitAndInfo2) {}
logFruitTuple6(/*10*/);
logFruitTuple6("apple", /*11*/);

type FruitAndInfo3 = ["apple", ...AppleInfo[], number] | ["banana", ...BananaInfo[], number];

function logFruitTuple7(...[fruit, fruitInfoOrNumber, secondFruitInfoOrNumber]: FruitAndInfo3) {}
logFruitTuple7(/*12*/);
logFruitTuple7("apple", /*13*/);
logFruitTuple7("apple", { color: "red" }, /*14*/);

function logFruitTuple8(...[fruit, , secondFruitInfoOrNumber]: FruitAndInfo3) {}
logFruitTuple8(/*15*/);
logFruitTuple8("apple", /*16*/);
logFruitTuple8("apple", { color: "red" }, /*17*/);

function logFruitTuple9(...[...[fruit, fruitInfoOrNumber, secondFruitInfoOrNumber]]: FruitAndInfo3) {}
logFruitTuple9(/*18*/);
logFruitTuple9("apple", /*19*/);
logFruitTuple9("apple", { color: "red" }, /*20*/);

function logFruitTuple10(...[fruit, {}, secondFruitInfoOrNumber]: FruitAndInfo3) {}
logFruitTuple10(/*21*/);
logFruitTuple10("apple", /*22*/);
logFruitTuple10("apple", { color: "red" }, /*23*/);

function logFruitTuple11(...{}: FruitAndInfo3) {}
logFruitTuple11(/*24*/);
logFruitTuple11("apple", /*25*/);
logFruitTuple11("apple", { color: "red" }, /*26*/);
function withPair(...[first, second]: [number, named: string]) {}
withPair(/*27*/);
withPair(101, /*28*/);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpExpandedRestTuplesLocalLabels1, TestSignatureHelpExpandedRestTuplesLocalLabels1);

// signatureHelpExpandedRestTuples_test.go

// signatureHelpExpandedRestTuples_test.go
static void TestSignatureHelpExpandedRestTuples(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(export function complex(item: string, another: string, ...rest: [] | [settings: object, errorHandler: (err: Error) => void] | [errorHandler: (err: Error) => void, ...mixins: object[]]) {
    
}

complex(/*1*/);
complex("ok", "ok", /*2*/);
complex("ok", "ok", e => void e, {}, /*3*/);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "complex(item: string, another: string): void", .ParameterCount = 2, .ParameterName = "item", .ParameterSpan = "item: string", .OverloadsCount = 3, .IsVariadic = false, .IsVariadicSet = true});
		f->GoToMarker(t, "2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "complex(item: string, another: string, settings: object, errorHandler: (err: Error) => void): void", .ParameterCount = 4, .ParameterName = "settings", .ParameterSpan = "settings: object", .OverloadsCount = 3, .IsVariadic = false, .IsVariadicSet = true});
		f->GoToMarker(t, "3");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "complex(item: string, another: string, errorHandler: (err: Error) => void, ...mixins: object[]): void", .OverloadsCount = 3, .IsVariadic = true, .IsVariadicSet = true});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpExpandedRestTuples, TestSignatureHelpExpandedRestTuples);

// signatureHelpExpandedRestUnlabeledTuples_test.go

// signatureHelpExpandedRestUnlabeledTuples_test.go
static void TestSignatureHelpExpandedRestUnlabeledTuples(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(export function complex(item: string, another: string, ...rest: [] | [object, (err: Error) => void] | [(err: Error) => void, ...object[]]) {
    
}

complex(/*1*/);
complex("ok", "ok", /*2*/);
complex("ok", "ok", e => void e, {}, /*3*/);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "complex(item: string, another: string): void", .ParameterCount = 2, .ParameterName = "item", .ParameterSpan = "item: string", .OverloadsCount = 3, .IsVariadic = false, .IsVariadicSet = true});
		f->GoToMarker(t, "2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "complex(item: string, another: string, rest_0: object, rest_1: (err: Error) => void): void", .ParameterCount = 4, .ParameterName = "rest_0", .ParameterSpan = "rest_0: object", .OverloadsCount = 3, .IsVariadic = false, .IsVariadicSet = true});
		f->GoToMarker(t, "3");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "complex(item: string, another: string, rest_0: (err: Error) => void, ...rest: object[]): void", .OverloadsCount = 3, .IsVariadic = true, .IsVariadicSet = true});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpExpandedRestUnlabeledTuples, TestSignatureHelpExpandedRestUnlabeledTuples);

// signatureHelpExpandedTuplesArgumentIndex_test.go

// signatureHelpExpandedTuplesArgumentIndex_test.go
static void TestSignatureHelpExpandedTuplesArgumentIndex(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function foo(...args: [string, string] | [number, string, string]
) {

}

foo(123/*1*/,)
foo(""/*2*/, ""/*3*/)
foo(123/*4*/, ""/*5*/, )
foo(123/*6*/, ""/*7*/, ""/*8*/))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "foo(args_0: number, args_1: string, args_2: string): void", .ParameterCount = 3, .ParameterName = "args_0", .ParameterSpan = "args_0: number", .OverloadsCount = 2, .OverrideSelectedItemIndex = 1, .IsVariadic = false, .IsVariadicSet = true});
		f->GoToMarker(t, "2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "foo(args_0: string, args_1: string): void", .ParameterCount = 2, .ParameterName = "args_0", .ParameterSpan = "args_0: string", .OverloadsCount = 2, .OverrideSelectedItemIndex = 0, .IsVariadic = false, .IsVariadicSet = true});
		f->GoToMarker(t, "3");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "foo(args_0: string, args_1: string): void", .ParameterCount = 2, .ParameterName = "args_1", .ParameterSpan = "args_1: string", .OverloadsCount = 2, .OverrideSelectedItemIndex = 0, .IsVariadic = false, .IsVariadicSet = true});
		f->GoToMarker(t, "4");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "foo(args_0: number, args_1: string, args_2: string): void", .ParameterCount = 3, .ParameterName = "args_0", .ParameterSpan = "args_0: number", .OverloadsCount = 2, .OverrideSelectedItemIndex = 1, .IsVariadic = false, .IsVariadicSet = true});
		f->GoToMarker(t, "5");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "foo(args_0: number, args_1: string, args_2: string): void", .ParameterCount = 3, .ParameterName = "args_1", .ParameterSpan = "args_1: string", .OverloadsCount = 2, .OverrideSelectedItemIndex = 1, .IsVariadic = false, .IsVariadicSet = true});
		f->GoToMarker(t, "6");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "foo(args_0: number, args_1: string, args_2: string): void", .ParameterCount = 3, .ParameterName = "args_0", .ParameterSpan = "args_0: number", .OverloadsCount = 2, .OverrideSelectedItemIndex = 1, .IsVariadic = false, .IsVariadicSet = true});
		f->GoToMarker(t, "7");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "foo(args_0: number, args_1: string, args_2: string): void", .ParameterCount = 3, .ParameterName = "args_1", .ParameterSpan = "args_1: string", .OverloadsCount = 2, .OverrideSelectedItemIndex = 1, .IsVariadic = false, .IsVariadicSet = true});
		f->GoToMarker(t, "8");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "foo(args_0: number, args_1: string, args_2: string): void", .ParameterCount = 3, .ParameterName = "args_2", .ParameterSpan = "args_2: string", .OverloadsCount = 2, .OverrideSelectedItemIndex = 1, .IsVariadic = false, .IsVariadicSet = true});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpExpandedTuplesArgumentIndex, TestSignatureHelpExpandedTuplesArgumentIndex);

// signatureHelpExplicitTypeArguments_test.go

// signatureHelpExplicitTypeArguments_test.go
static void TestSignatureHelpExplicitTypeArguments(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare function f<T = boolean, U = string>(x: T, y: U): T;
f<number, string>(/*1*/);
f(/*2*/);
f<number>(/*3*/);
f<number, string, boolean>(/*4*/);
interface A { a: number }
interface B extends A { b: string }
declare function g<T, U, V extends A = B>(x: T, y: U, z: V): T;
declare function h<T, U, V extends A>(x: T, y: U, z: V): T;
declare function j<T, U, V = B>(x: T, y: U, z: V): T;
g(/*5*/);
h(/*6*/);
j(/*7*/);
g<number>(/*8*/);
h<number>(/*9*/);
j<number>(/*10*/);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "f(x: number, y: string): number"});
		f->GoToMarker(t, "2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "f(x: boolean, y: string): boolean"});
		f->GoToMarker(t, "3");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "f(x: number, y: string): number"});
		f->GoToMarker(t, "4");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "f(x: number, y: string): number"});
		f->GoToMarker(t, "5");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "g(x: unknown, y: unknown, z: B): unknown"});
		f->GoToMarker(t, "6");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "h(x: unknown, y: unknown, z: A): unknown"});
		f->GoToMarker(t, "7");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "j(x: unknown, y: unknown, z: B): unknown"});
		f->GoToMarker(t, "8");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "g(x: number, y: unknown, z: B): number"});
		f->GoToMarker(t, "9");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "h(x: number, y: unknown, z: A): number"});
		f->GoToMarker(t, "10");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "j(x: number, y: unknown, z: B): number"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpExplicitTypeArguments, TestSignatureHelpExplicitTypeArguments);

// signatureHelpFilteredTriggers03_test.go

// signatureHelpFilteredTriggers03_test.go
static void TestSignatureHelpFilteredTriggers03(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare class ViewJayEss {
    constructor(obj: object);
}
new ViewJayEss({
    methods: {
        sayHello/**/
    }
});)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->Insert(t, "(");
		f->VerifyNoSignatureHelpWithContext(t, std::make_shared<lsproto::SignatureHelpContext>(lsproto::SignatureHelpContext{.TriggerKind = lsproto::SignatureHelpTriggerKindTriggerCharacter, .TriggerCharacter = std::string("("), .IsRetrigger = false}));
		f->Insert(t, ") {},");
		f->VerifyNoSignatureHelpWithContext(t, std::make_shared<lsproto::SignatureHelpContext>(lsproto::SignatureHelpContext{.TriggerKind = lsproto::SignatureHelpTriggerKindTriggerCharacter, .TriggerCharacter = std::string(","), .IsRetrigger = false}));
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpFilteredTriggers03, TestSignatureHelpFilteredTriggers03);

// signatureHelpForNonlocalTypeDoesNotUseImportType_test.go

// signatureHelpForNonlocalTypeDoesNotUseImportType_test.go
static void TestSignatureHelpForNonlocalTypeDoesNotUseImportType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: exporter.ts
export interface Thing {}
export const Foo: () => Thing = null as any;
// @Filename: usage.ts
import {Foo} from "./exporter"
function f(p = Foo()): void {}
f(/*1*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "f(p?: Thing): void"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpForNonlocalTypeDoesNotUseImportType, TestSignatureHelpForNonlocalTypeDoesNotUseImportType);

// signatureHelpForOptionalMethods_test.go

// signatureHelpForOptionalMethods_test.go
static void TestSignatureHelpForOptionalMethods(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
interface Obj {
    optionalMethod?: (current: any) => any;
};

const o: Obj = {
  optionalMethod(/*1*/) {
    return {};
  }
};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "optionalMethod(current: any): any", .ParameterName = "current", .ParameterSpan = "current: any"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpForOptionalMethods, TestSignatureHelpForOptionalMethods);

// signatureHelpForSignatureWithUnreachableType_test.go

// signatureHelpForSignatureWithUnreachableType_test.go
static void TestSignatureHelpForSignatureWithUnreachableType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /node_modules/foo/node_modules/bar/index.d.ts
export interface SomeType {
    x?: number;
}
// @Filename: /node_modules/foo/index.d.ts
import { SomeType } from "bar";
export function func<T extends SomeType>(param: T): void;
export function func<T extends SomeType>(param: T, other: T): void;
// @Filename: /usage.ts
import { func } from "foo";
func({/*1*/});)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "func(param: {}): void", .OverloadsCount = 2});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpForSignatureWithUnreachableType, TestSignatureHelpForSignatureWithUnreachableType);

// signatureHelpForSuperCalls1_test.go

// signatureHelpForSuperCalls1_test.go
static void TestSignatureHelpForSuperCalls1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class A { }
class B extends A { }
class C extends B {
    constructor() {
        super(/*1*/ // sig help here?
    }
}
class A2 { }
class B2 extends A2 {
    constructor(x:number) {}
 }
class C2 extends B2 {
    constructor() {
        super(/*2*/ // sig help here?
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "B(): B"});
		f->GoToMarker(t, "2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "B2(x: number): B2"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpForSuperCalls1, TestSignatureHelpForSuperCalls1);

// signatureHelpFunctionOverload_test.go

// signatureHelpFunctionOverload_test.go
static void TestSignatureHelpFunctionOverload(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function functionOverload();
function functionOverload(test: string);
function functionOverload(test?: string) { }
functionOverload(/*functionOverload1*/);
functionOverload(""/*functionOverload2*/);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "functionOverload1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "functionOverload(): any", .ParameterCount = 0, .OverloadsCount = 2});
		f->GoToMarker(t, "functionOverload2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "functionOverload(test: string): any", .ParameterName = "test", .ParameterSpan = "test: string", .OverloadsCount = 2});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpFunctionOverload, TestSignatureHelpFunctionOverload);

// signatureHelpFunctionParameter_test.go

// signatureHelpFunctionParameter_test.go
static void TestSignatureHelpFunctionParameter(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function parameterFunction(callback: (a: number, b: string) => void) {
    callback(/*parameterFunction1*/5, /*parameterFunction2*/"");
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "parameterFunction1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "callback(a: number, b: string): void", .ParameterCount = 2, .ParameterName = "a", .ParameterSpan = "a: number"});
		f->GoToMarker(t, "parameterFunction2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "callback(a: number, b: string): void", .ParameterName = "b", .ParameterSpan = "b: string"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpFunctionParameter, TestSignatureHelpFunctionParameter);

// signatureHelpImplicitConstructor_test.go

// signatureHelpImplicitConstructor_test.go
static void TestSignatureHelpImplicitConstructor(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class ImplicitConstructor {
}
var implicitConstructor = new ImplicitConstructor(/**/);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "ImplicitConstructor(): ImplicitConstructor", .ParameterCount = 0});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpImplicitConstructor, TestSignatureHelpImplicitConstructor);

// signatureHelpImportStarFromExportEquals_test.go

// signatureHelpImportStarFromExportEquals_test.go
static void TestSignatureHelpImportStarFromExportEquals(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /node_modules/@types/abs/index.d.ts
declare function abs(str: string): string;
export = abs;
// @Filename: /a.js
import * as abs from "abs";
abs.default/**/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->Insert(t, "(");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "default(str: string): string"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpImportStarFromExportEquals, TestSignatureHelpImportStarFromExportEquals);

// signatureHelpInAdjacentBlockBody_test.go

// signatureHelpInAdjacentBlockBody_test.go
static void TestSignatureHelpInAdjacentBlockBody(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare function foo(...args);

foo(() => {/*1*/}/*2*/))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelpPresent(t, std::make_shared<lsproto::SignatureHelpContext>(lsproto::SignatureHelpContext{.TriggerKind = lsproto::SignatureHelpTriggerKindInvoked}));
		f->GoToMarker(t, "2");
		f->VerifySignatureHelpPresent(t, std::make_shared<lsproto::SignatureHelpContext>(lsproto::SignatureHelpContext{.TriggerKind = lsproto::SignatureHelpTriggerKindInvoked}));
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpInAdjacentBlockBody, TestSignatureHelpInAdjacentBlockBody);

// signatureHelpInCallback_test.go

// signatureHelpInCallback_test.go
static void TestSignatureHelpInCallback(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare function forEach(f: () => void);
forEach(/*1*/() => {
    /*2*/
});)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "forEach(f: () => void): any"});
		f->VerifyNoSignatureHelpForMarkers(t, {"2"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpInCallback, TestSignatureHelpInCallback);

// signatureHelpInCompleteGenericsCall_test.go

// signatureHelpInCompleteGenericsCall_test.go
static void TestSignatureHelpInCompleteGenericsCall(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function foo<T>(x: number, callback: (x: T) => number) {
}
foo(/*1*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "foo(x: number, callback: (x: unknown) => number): void"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpInCompleteGenericsCall, TestSignatureHelpInCompleteGenericsCall);

// signatureHelpInFunctionCallOnFunctionDeclarationInMultipleFiles_test.go

// signatureHelpInFunctionCallOnFunctionDeclarationInMultipleFiles_test.go
static void TestSignatureHelpInFunctionCallOnFunctionDeclarationInMultipleFiles(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: signatureHelpInFunctionCallOnFunctionDeclarationInMultipleFiles_file0.ts
declare function fn(x: string, y: number);
// @Filename: signatureHelpInFunctionCallOnFunctionDeclarationInMultipleFiles_file1.ts
declare function fn(x: string);
// @Filename: signatureHelpInFunctionCallOnFunctionDeclarationInMultipleFiles_file2.ts
fn(/*1*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.OverloadsCount = 2});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpInFunctionCallOnFunctionDeclarationInMultipleFiles, TestSignatureHelpInFunctionCallOnFunctionDeclarationInMultipleFiles);

// signatureHelpInFunctionCall_test.go

// signatureHelpInFunctionCall_test.go
static void TestSignatureHelpInFunctionCall(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var items = [];
items.forEach(item => {
    for (/**/
});)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoSignatureHelpForMarkers(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpInFunctionCall, TestSignatureHelpInFunctionCall);

// signatureHelpInParenthetical_test.go

// signatureHelpInParenthetical_test.go
static void TestSignatureHelpInParenthetical(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class base { constructor (public n: number, public y: string) { } }
(new base(/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.ParameterName = "n"});
		f->Insert(t, "0, ");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.ParameterName = "y"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpInParenthetical, TestSignatureHelpInParenthetical);

// signatureHelpInRecursiveType_test.go

// signatureHelpInRecursiveType_test.go
static void TestSignatureHelpInRecursiveType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type Tail<T extends any[]> =
	((...args: T) => any) extends ((head: any, ...tail: infer R) => any) ? R : never;

type Reverse<List extends any[]> = _Reverse<List, []>;

type _Reverse<Source extends any[], Result extends any[] = []> = {
	1: Result,
	0: _Reverse<Tail<Source>, 0>,
}[Source extends [] ? 1 : 0];

type Foo = Reverse<[0,/**/]>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "Reverse<List extends any[]>"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpInRecursiveType, TestSignatureHelpInRecursiveType);

// signatureHelpIncompleteCalls_test.go

// signatureHelpIncompleteCalls_test.go
static void TestSignatureHelpIncompleteCalls(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace IncompleteCalls {
    class Foo {
        public f1() { }
        public f2(n: number): number { return 0; }
        public f3(n: number, s: string) : string { return ""; }
    }
    var x = new Foo();
    x.f1();
    x.f2(5);
    x.f3(5, "");
    x.f1(/*incompleteCalls1*/
    x.f2(5,/*incompleteCalls2*/
    x.f3(5,/*incompleteCalls3*/
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "incompleteCalls1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "f1(): void", .ParameterCount = 0});
		f->GoToMarker(t, "incompleteCalls2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "f2(n: number): number", .ParameterCount = 1});
		f->GoToMarker(t, "incompleteCalls3");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "f3(n: number, s: string): string", .ParameterCount = 2, .ParameterName = "s", .ParameterSpan = "s: string"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpIncompleteCalls, TestSignatureHelpIncompleteCalls);

// signatureHelpIncompleteJsxAttribute_test.go

// signatureHelpIncompleteJsxAttribute_test.go
static void TestSignatureHelpIncompleteJsxAttribute(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.tsx
<a><b c=
/*a*/</a>)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoSignatureHelpForMarkers(t, {"a"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpIncompleteJsxAttribute, TestSignatureHelpIncompleteJsxAttribute);

// signatureHelpInferenceJsDocImportTagVS_test.go

// signatureHelpInferenceJsDocImportTagVS_test.go
static void TestSignatureHelpInferenceJsDocImportTagVS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJS: true
// @checkJs: true
// @module: esnext
// @filename: a.ts
export interface Foo {}
// @filename: b.js
/**
 * @import {
 *     Foo
 * } from './a'
 */

/**
 * @param {Foo} a
 */
function foo(a) {}
foo(/**/))TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpInferenceJsDocImportTagVS, TestSignatureHelpInferenceJsDocImportTagVS);

// signatureHelpInferenceJsDocImportTag_test.go

// signatureHelpInferenceJsDocImportTag_test.go
static void TestSignatureHelpInferenceJsDocImportTag(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJS: true
// @checkJs: true
// @module: esnext
// @filename: a.ts
export interface Foo {}
// @filename: b.js
/**
 * @import {
 *     Foo
 * } from './a'
 */

/**
 * @param {Foo} a
 */
function foo(a) {}
foo(/**/))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpInferenceJsDocImportTag, TestSignatureHelpInferenceJsDocImportTag);

// signatureHelpIteratorNextVS_test.go

// signatureHelpIteratorNextVS_test.go
static void TestSignatureHelpIteratorNextVS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: esnext
declare const iterator: Iterator<string, void, number>;

iterator.next(/*1*/);
iterator.next(/*2*/ 0);

declare const generator: Generator<string, void, number>;

generator.next(/*3*/);
generator.next(/*4*/ 0);

declare const asyncIterator: AsyncIterator<string, void, number>;

asyncIterator.next(/*5*/);
asyncIterator.next(/*6*/ 0);

declare const asyncGenerator: AsyncGenerator<string, void, number>;

asyncGenerator.next(/*7*/);
asyncGenerator.next(/*8*/ 0);)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpIteratorNextVS, TestSignatureHelpIteratorNextVS);

// signatureHelpIteratorNext_test.go

// signatureHelpIteratorNext_test.go
static void TestSignatureHelpIteratorNext(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: esnext
declare const iterator: Iterator<string, void, number>;

iterator.next(/*1*/);
iterator.next(/*2*/ 0);

declare const generator: Generator<string, void, number>;

generator.next(/*3*/);
generator.next(/*4*/ 0);

declare const asyncIterator: AsyncIterator<string, void, number>;

asyncIterator.next(/*5*/);
asyncIterator.next(/*6*/ 0);

declare const asyncGenerator: AsyncGenerator<string, void, number>;

asyncGenerator.next(/*7*/);
asyncGenerator.next(/*8*/ 0);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpIteratorNext, TestSignatureHelpIteratorNext);

// signatureHelpJSDocCallbackTagVS_test.go

// signatureHelpJSDocCallbackTagVS_test.go
static void TestSignatureHelpJSDocCallbackTagVS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
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
 * @type {FooHandler} callback
 */
var t;

/**
 * @callback FooHandler2 - What, another one?
 * @param {string=} eventName - it keeps happening
 * @param {string} [eventName2] - i WARNED you dog
 */
/**
 * @type {FooHandler2} callback
 */
var t2;
t(/*4*/"!", /*5*/12, /*6*/false);)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpJSDocCallbackTagVS, TestSignatureHelpJSDocCallbackTagVS);

// signatureHelpJSDocCallbackTag_test.go

// signatureHelpJSDocCallbackTag_test.go
static void TestSignatureHelpJSDocCallbackTag(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
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
 * @type {FooHandler} callback
 */
var t;

/**
 * @callback FooHandler2 - What, another one?
 * @param {string=} eventName - it keeps happening
 * @param {string} [eventName2] - i WARNED you dog
 */
/**
 * @type {FooHandler2} callback
 */
var t2;
t(/*4*/"!", /*5*/12, /*6*/false);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpJSDocCallbackTag, TestSignatureHelpJSDocCallbackTag);

// signatureHelpJSDocTagsVS_test.go

// signatureHelpJSDocTagsVS_test.go
static void TestSignatureHelpJSDocTagsVS(gostd::testing::T* t) {
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
var foo = new Foo(/*10*/4);
Foo.method1(/*11*/);
foo.method2(/*12*/);
foo.method3(/*13*/);
foo.method4();
foo.property1;
foo.property2;
foo.method5();
foo.newMet)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpJSDocTagsVS, TestSignatureHelpJSDocTagsVS);

// signatureHelpJSDocTags_test.go

// signatureHelpJSDocTags_test.go
static void TestSignatureHelpJSDocTags(gostd::testing::T* t) {
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
var foo = new Foo(/*10*/4);
Foo.method1(/*11*/);
foo.method2(/*12*/);
foo.method3(/*13*/);
foo.method4();
foo.property1;
foo.property2;
foo.method5();
foo.newMet)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpJSDocTags, TestSignatureHelpJSDocTags);

// signatureHelpJSMissingIdentifier_test.go

// signatureHelpJSMissingIdentifier_test.go
static void TestSignatureHelpJSMissingIdentifier(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: test.js
log(/**/))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoSignatureHelpForMarkers(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpJSMissingIdentifier, TestSignatureHelpJSMissingIdentifier);

// signatureHelpJSMissingPropertyAccessVS_test.go

// signatureHelpJSMissingPropertyAccessVS_test.go
static void TestSignatureHelpJSMissingPropertyAccessVS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: test.js
foo.filter(/**/))TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpJSMissingPropertyAccessVS, TestSignatureHelpJSMissingPropertyAccessVS);

// signatureHelpJSMissingPropertyAccess_test.go

// signatureHelpJSMissingPropertyAccess_test.go
static void TestSignatureHelpJSMissingPropertyAccess(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: test.js
foo.filter(/**/))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpJSMissingPropertyAccess, TestSignatureHelpJSMissingPropertyAccess);

// signatureHelpJSX_test.go

// signatureHelpJSX_test.go
static void TestSignatureHelpJSX(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(//@Filename: test.tsx
//@jsx: react
declare var React: any;
const z = <div>{[].map(x => </**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyNoSignatureHelpWithContext(t, std::make_shared<lsproto::SignatureHelpContext>(lsproto::SignatureHelpContext{.TriggerKind = lsproto::SignatureHelpTriggerKindTriggerCharacter, .TriggerCharacter = std::string("<"), .IsRetrigger = false}));
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpJSX, TestSignatureHelpJSX);

// signatureHelpJsxTextLessThanTrigger_test.go

// signatureHelpJsxTextLessThanTrigger_test.go
static void TestSignatureHelpJsxTextLessThanTrigger(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(//@Filename: test.tsx
//@jsx: react
declare var React: any;
declare function Text(props: { children?: any }): any;

const text = () => {
	return <Text>/*m*/</Text>;
};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "m");
		f->Insert(t, "<");
		f->VerifyNoSignatureHelpWithContext(t, std::make_shared<lsproto::SignatureHelpContext>(lsproto::SignatureHelpContext{.TriggerKind = lsproto::SignatureHelpTriggerKindTriggerCharacter, .TriggerCharacter = std::string("<"), .IsRetrigger = false}));
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpJsxTextLessThanTrigger, TestSignatureHelpJsxTextLessThanTrigger);

// signatureHelpLeadingRestTuple_test.go

// signatureHelpLeadingRestTuple_test.go
static void TestSignatureHelpLeadingRestTuple(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(export function leading(...args: [...names: string[], allCaps: boolean]): void {
}

leading(/*1*/);
leading("ok", /*2*/);
leading("ok", "ok", /*3*/);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "leading(...names: string[], allCaps: boolean): void", .ParameterCount = 2, .OverloadsCount = 1, .IsVariadic = true, .IsVariadicSet = true});
		f->GoToMarker(t, "2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "leading(...names: string[], allCaps: boolean): void", .ParameterCount = 2, .OverloadsCount = 1, .IsVariadic = true, .IsVariadicSet = true});
		f->GoToMarker(t, "3");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "leading(...names: string[], allCaps: boolean): void", .ParameterCount = 2, .OverloadsCount = 1, .IsVariadic = true, .IsVariadicSet = true});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpLeadingRestTuple, TestSignatureHelpLeadingRestTuple);

// signatureHelpMalformedTaggedTemplateNoCrash1_test.go

// signatureHelpMalformedTaggedTemplateNoCrash1_test.go
static void TestSignatureHelpMalformedTaggedTemplateNoCrash1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(`${1}
/*m1*/
// ``
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "m1");
		f->VerifyNoSignatureHelpWithContext(t, std::make_shared<lsproto::SignatureHelpContext>(lsproto::SignatureHelpContext{.TriggerKind = lsproto::SignatureHelpTriggerKindInvoked, .IsRetrigger = false}));
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpMalformedTaggedTemplateNoCrash1, TestSignatureHelpMalformedTaggedTemplateNoCrash1);

// signatureHelpNestedCallTrailingComma_test.go

// signatureHelpNestedCallTrailingComma_test.go
static void TestSignatureHelpNestedCallTrailingComma(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare function outer<T>(range: T): T;
declare function inner(a: any): any;

outer(inner/*1*/(undefined,),);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelpPresent(t, std::make_shared<lsproto::SignatureHelpContext>(lsproto::SignatureHelpContext{.TriggerKind = lsproto::SignatureHelpTriggerKindInvoked, .IsRetrigger = false}));
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpNestedCallTrailingComma, TestSignatureHelpNestedCallTrailingComma);

// signatureHelpNestedCalls_test.go

// signatureHelpNestedCalls_test.go
static void TestSignatureHelpNestedCalls(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function foo(s: string) { return s; }
function bar(s: string) { return s; }
let s = foo(/*a*/ /*b*/bar/*c*/(/*d*/"hello"/*e*/)/*f*/);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "a");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "foo(s: string): string"});
		f->GoToMarker(t, "b");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "foo(s: string): string"});
		f->GoToMarker(t, "c");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "foo(s: string): string"});
		f->GoToMarker(t, "d");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "bar(s: string): string"});
		f->GoToMarker(t, "e");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "bar(s: string): string"});
		f->GoToMarker(t, "f");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "foo(s: string): string"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpNestedCalls, TestSignatureHelpNestedCalls);

// signatureHelpNestedCalls_test.go
static void TestSignatureHelpEmptyInnerCall(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function foo(s: string) { return s; }
function bar(s: string) { return s; }
let s = foo(bar(/*a*/));)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "a");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "bar(s: string): string"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpEmptyInnerCall, TestSignatureHelpEmptyInnerCall);

// signatureHelpNestedTypeArgumentGTBalance_test.go

// signatureHelpNestedTypeArgumentGTBalance_test.go
static void TestSignatureHelpNestedTypeArgumentGTBalance(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare function f<T, U>(): void;
type A<T> = T;
type B<T> = T;
type C<T> = T;
f<A<B<C<number>>>, /*nested*/;
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "nested");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "f<T, U>(): void", .ParameterCount = 2, .ParameterName = "U", .ParameterSpan = "U"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpNestedTypeArgumentGTBalance, TestSignatureHelpNestedTypeArgumentGTBalance);

// signatureHelpNoArguments_test.go

// signatureHelpNoArguments_test.go
static void TestSignatureHelpNoArguments(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function foo(n: number): string {
}

foo(/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "foo(n: number): string", .ParameterName = "n", .ParameterSpan = "n: number"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpNoArguments, TestSignatureHelpNoArguments);

// signatureHelpObjectCreationExpressionNoArgs_NotAvailable_test.go

// signatureHelpObjectCreationExpressionNoArgs_NotAvailable_test.go
static void TestSignatureHelpObjectCreationExpressionNoArgs_NotAvailable(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class sampleCls { constructor(str: string, num: number) { } }
var x = new sampleCls/**/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoSignatureHelpForMarkers(t, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpObjectCreationExpressionNoArgs_NotAvailable, TestSignatureHelpObjectCreationExpressionNoArgs_NotAvailable);

// signatureHelpObjectLiteral_test.go

// signatureHelpObjectLiteral_test.go
static void TestSignatureHelpObjectLiteral(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var objectLiteral = { n: 5, s: "", f: (a: number, b: string) => "" };
objectLiteral.f(/*objectLiteral1*/4, /*objectLiteral2*/"");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "objectLiteral1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "f(a: number, b: string): string", .ParameterCount = 2, .ParameterName = "a", .ParameterSpan = "a: number"});
		f->GoToMarker(t, "objectLiteral2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "f(a: number, b: string): string", .ParameterName = "b", .ParameterSpan = "b: string"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpObjectLiteral, TestSignatureHelpObjectLiteral);

// signatureHelpOnImportDefer_test.go

// signatureHelpOnImportDefer_test.go
static void TestSignatureHelpOnImportDefer(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(let m = import.defer(/**/))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpOnImportDefer, TestSignatureHelpOnImportDefer);

// signatureHelpOnNestedOverloads_test.go

// signatureHelpOnNestedOverloads_test.go
static void TestSignatureHelpOnNestedOverloads(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare function fn(x: string);
declare function fn(x: string, y: number);
declare function fn2(x: string);
declare function fn2(x: string, y: number);
fn('', fn2(/*1*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "fn2(x: string): any", .ParameterName = "x", .ParameterSpan = "x: string", .OverloadsCount = 2});
		f->Insert(t, "'',");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "fn2(x: string, y: number): any", .ParameterName = "y", .ParameterSpan = "y: number", .OverloadsCount = 2});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpOnNestedOverloads, TestSignatureHelpOnNestedOverloads);

// signatureHelpOnOverloadOnConst_test.go

// signatureHelpOnOverloadOnConst_test.go
static void TestSignatureHelpOnOverloadOnConst(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function x1(x: 'hi');
function x1(y: 'bye');
function x1(z: string);
function x1(a: any) {
}

x1(''/*1*/);
x1('hi'/*2*/);
x1('bye'/*3*/);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.ParameterName = "z", .ParameterSpan = "z: string", .OverloadsCount = 3});
		f->GoToMarker(t, "2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.ParameterName = "x", .ParameterSpan = "x: 'hi'", .OverloadsCount = 3});
		f->GoToMarker(t, "3");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.ParameterName = "y", .ParameterSpan = "y: 'bye'", .OverloadsCount = 3});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpOnOverloadOnConst, TestSignatureHelpOnOverloadOnConst);

// signatureHelpOnOverloadsDifferentArity2_test.go

// signatureHelpOnOverloadsDifferentArity2_test.go
static void TestSignatureHelpOnOverloadsDifferentArity2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare function f(s: string);
declare function f(n: number);
declare function f(s: string, b: boolean);
declare function f(n: number, b: boolean);

f(1/**/ var)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "f(n: number): any", .ParameterName = "n", .ParameterSpan = "n: number", .OverloadsCount = 4});
		f->Insert(t, ", ");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "f(n: number, b: boolean): any", .ParameterName = "b", .ParameterSpan = "b: boolean", .OverloadsCount = 4});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpOnOverloadsDifferentArity2, TestSignatureHelpOnOverloadsDifferentArity2);

// signatureHelpOnOverloadsDifferentArity3_test.go

// signatureHelpOnOverloadsDifferentArity3_test.go
static void TestSignatureHelpOnOverloadsDifferentArity3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare function f();
declare function f(s: string);
declare function f(s: string, b: boolean);
declare function f(n: number, b: boolean);

f(/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "f(): any", .ParameterCount = 0, .OverloadsCount = 4});
		f->Insert(t, "x, ");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "f(s: string, b: boolean): any", .ParameterCount = 2, .ParameterName = "b", .ParameterSpan = "b: boolean", .OverloadsCount = 4});
		f->Insert(t, "x, ");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "f(s: string, b: boolean): any", .ParameterCount = 2, .OverloadsCount = 4});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpOnOverloadsDifferentArity3, TestSignatureHelpOnOverloadsDifferentArity3);

// signatureHelpOnOverloadsDifferentArity_test.go

// signatureHelpOnOverloadsDifferentArity_test.go
static void TestSignatureHelpOnOverloadsDifferentArity(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare function f(s: string);
declare function f(n: number);
declare function f(s: string, b: boolean);
declare function f(n: number, b: boolean);

f(1/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "f(n: number): any", .ParameterName = "n", .ParameterSpan = "n: number", .OverloadsCount = 4});
		f->Insert(t, ", ");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "f(n: number, b: boolean): any", .ParameterName = "b", .ParameterSpan = "b: boolean", .OverloadsCount = 4});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpOnOverloadsDifferentArity, TestSignatureHelpOnOverloadsDifferentArity);

// signatureHelpOnOverloads_test.go

// signatureHelpOnOverloads_test.go
static void TestSignatureHelpOnOverloads(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare function fn(x: string);
declare function fn(x: string, y: number);
fn(/*1*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "fn(x: string): any", .ParameterName = "x", .ParameterSpan = "x: string", .OverloadsCount = 2});
		f->Insert(t, "'',");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "fn(x: string, y: number): any", .ParameterName = "y", .ParameterSpan = "y: number", .OverloadsCount = 2});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpOnOverloads, TestSignatureHelpOnOverloads);

// signatureHelpOnSuperWhenMembersAreNotResolved_test.go

// signatureHelpOnSuperWhenMembersAreNotResolved_test.go
static void TestSignatureHelpOnSuperWhenMembersAreNotResolved(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class A { }
class B extends A { constructor(public x: string) { } }
class C extends B {
    constructor() {
        /*1*/
     }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->Insert(t, "super(");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "B(x: string): B"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpOnSuperWhenMembersAreNotResolved, TestSignatureHelpOnSuperWhenMembersAreNotResolved);

// signatureHelpOnTypePredicates_test.go

// signatureHelpOnTypePredicates_test.go
static void TestSignatureHelpOnTypePredicates(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f1(a: any): a is number {}
function f2<T>(a: any): a is T {}
function f3(a: any, ...b): a is number {}
f1(/*1*/)
f2(/*2*/)
f3(/*3*/))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "f1(a: any): a is number"});
		f->GoToMarker(t, "2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "f2(a: any): a is unknown"});
		f->GoToMarker(t, "3");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "f3(a: any, ...b: any[]): a is number", .IsVariadic = true, .IsVariadicSet = true});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpOnTypePredicates, TestSignatureHelpOnTypePredicates);

// signatureHelpOptionalCall2_test.go

// signatureHelpOptionalCall2_test.go
static void TestSignatureHelpOptionalCall2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: false
declare const fnTest: undefined | ((str: string, num: number) => void);
fnTest?.(/*1*/);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "fnTest(str: string, num: number): void", .ParameterCount = 2, .ParameterName = "str", .ParameterSpan = "str: string"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpOptionalCall2, TestSignatureHelpOptionalCall2);

// signatureHelpRestArgs1VS_test.go

// signatureHelpRestArgs1VS_test.go
static void TestSignatureHelpRestArgs1VS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function fn(a: number, b: number, c: number) {}
const a = [1, 2] as const;
const b = [1] as const;

fn(...a, /*1*/);
fn(/*2*/, ...a);

fn(...b, /*3*/);
fn(/*4*/, ...b, /*5*/);)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpRestArgs1VS, TestSignatureHelpRestArgs1VS);

// signatureHelpRestArgs1_test.go

// signatureHelpRestArgs1_test.go
static void TestSignatureHelpRestArgs1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function fn(a: number, b: number, c: number) {}
const a = [1, 2] as const;
const b = [1] as const;

fn(...a, /*1*/);
fn(/*2*/, ...a);

fn(...b, /*3*/);
fn(/*4*/, ...b, /*5*/);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpRestArgs1, TestSignatureHelpRestArgs1);

// signatureHelpRestArgs2VS_test.go

// signatureHelpRestArgs2VS_test.go
static void TestSignatureHelpRestArgs2VS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @allowJs: true
// @checkJs: true
// @filename: index.js
const promisify = function (thisArg, fnName) {
    const fn = thisArg[fnName];
    return function () {
        return new Promise((resolve) => {
            fn.call(thisArg, ...arguments, /*1*/);
        });
    };
};)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpRestArgs2VS, TestSignatureHelpRestArgs2VS);

// signatureHelpRestArgs2_test.go

// signatureHelpRestArgs2_test.go
static void TestSignatureHelpRestArgs2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: true
// @allowJs: true
// @checkJs: true
// @filename: index.js
const promisify = function (thisArg, fnName) {
    const fn = thisArg[fnName];
    return function () {
        return new Promise((resolve) => {
            fn.call(thisArg, ...arguments, /*1*/);
        });
    };
};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpRestArgs2, TestSignatureHelpRestArgs2);

// signatureHelpRestArgs3VS_test.go

// signatureHelpRestArgs3VS_test.go
static void TestSignatureHelpRestArgs3VS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @target: esnext
// @lib: esnext
const layers = Object.assign({}, /*1*/...[]);)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpRestArgs3VS, TestSignatureHelpRestArgs3VS);

// signatureHelpRestArgs3_test.go

// signatureHelpRestArgs3_test.go
static void TestSignatureHelpRestArgs3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @target: esnext
// @lib: esnext
const layers = Object.assign({}, /*1*/...[]);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpRestArgs3, TestSignatureHelpRestArgs3);

// signatureHelpSimpleConstructorCall_test.go

// signatureHelpSimpleConstructorCall_test.go
static void TestSignatureHelpSimpleConstructorCall(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class ConstructorCall {
    constructor(str: string, num: number) {
    }
}
var x = new ConstructorCall(/*constructorCall1*/1,/*constructorCall2*/2);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "constructorCall1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "ConstructorCall(str: string, num: number): ConstructorCall", .ParameterName = "str", .ParameterSpan = "str: string"});
		f->GoToMarker(t, "constructorCall2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "ConstructorCall(str: string, num: number): ConstructorCall", .ParameterName = "num", .ParameterSpan = "num: number"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpSimpleConstructorCall, TestSignatureHelpSimpleConstructorCall);

// signatureHelpSimpleFunctionCall_test.go

// signatureHelpSimpleFunctionCall_test.go
static void TestSignatureHelpSimpleFunctionCall(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// Simple function test
function functionCall(str: string, num: number) {
}
functionCall(/*functionCall1*/);
functionCall("", /*functionCall2*/1);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "functionCall1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "functionCall(str: string, num: number): void", .ParameterName = "str", .ParameterSpan = "str: string"});
		f->GoToMarker(t, "functionCall2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "functionCall(str: string, num: number): void", .ParameterName = "num", .ParameterSpan = "num: number"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpSimpleFunctionCall, TestSignatureHelpSimpleFunctionCall);

// signatureHelpSimpleSuperCall_test.go

// signatureHelpSimpleSuperCall_test.go
static void TestSignatureHelpSimpleSuperCall(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class SuperCallBase {
    constructor(b: boolean) {
    }
}
class SuperCall extends SuperCallBase {
    constructor() {
        super(/*superCall*/);
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "superCall");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "SuperCallBase(b: boolean): SuperCallBase", .ParameterName = "b", .ParameterSpan = "b: boolean"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpSimpleSuperCall, TestSignatureHelpSimpleSuperCall);

// signatureHelpSkippedArgs1VS_test.go

// signatureHelpSkippedArgs1VS_test.go
static void TestSignatureHelpSkippedArgs1VS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function fn(a: number, b: number, c: number) {}
fn(/*1*/, /*2*/, /*3*/, /*4*/, /*5*/);)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpSkippedArgs1VS, TestSignatureHelpSkippedArgs1VS);

// signatureHelpSuperConstructorOverload_test.go

// signatureHelpSuperConstructorOverload_test.go
static void TestSignatureHelpSuperConstructorOverload(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class SuperOverloadBase {
    constructor();
    constructor(test: string);
    constructor(test?: string) {
    }
}
class SuperOverLoad1 extends SuperOverloadBase {
    constructor() {
        super(/*superOverload1*/);
    }
}
class SuperOverLoad2 extends SuperOverloadBase {
    constructor() {
        super(""/*superOverload2*/);
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "superOverload1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "SuperOverloadBase(): SuperOverloadBase", .ParameterCount = 0, .OverloadsCount = 2});
		f->GoToMarker(t, "superOverload2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "SuperOverloadBase(test: string): SuperOverloadBase", .ParameterCount = 1, .ParameterName = "test", .ParameterSpan = "test: string", .OverloadsCount = 2});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpSuperConstructorOverload, TestSignatureHelpSuperConstructorOverload);

// signatureHelpTaggedTemplatesNegatives1_test.go

// signatureHelpTaggedTemplatesNegatives1_test.go
static void TestSignatureHelpTaggedTemplatesNegatives1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((((((std::string(R"TS(function f(templateStrings, x, y, z) { return 10; }
function g(templateStrings, x, y, z) { return ""; }

/*1*/f/*2*/ /*3*/)TS") + "`") + std::string(R"TS( qwerty ${ 123 } asdf ${   41234   }  zxcvb ${ g )TS")) + std::string("`")) + std::string(R"TS(    )TS")) + std::string("`")) + std::string(R"TS( }     )TS")) + std::string("`")) + std::string(R"TS(/*4*/)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoSignatureHelpForMarkers(t, f->MarkerNames());
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpTaggedTemplatesNegatives1, TestSignatureHelpTaggedTemplatesNegatives1);

// signatureHelpTaggedTemplatesNegatives2_test.go

// signatureHelpTaggedTemplatesNegatives2_test.go
static void TestSignatureHelpTaggedTemplatesNegatives2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((std::string(R"TS(function foo(strs, ...rest) {
}

/*1*/fo/*2*/o /*3*/)TS") + "`") + std::string(R"TS(abcd${0 + 1}abcd{1 + 1})TS")) + std::string("`")) + std::string(R"TS(/*4*/  /*5*/)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoSignatureHelpForMarkers(t, f->MarkerNames());
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpTaggedTemplatesNegatives2, TestSignatureHelpTaggedTemplatesNegatives2);

// signatureHelpTaggedTemplatesNegatives3_test.go

// signatureHelpTaggedTemplatesNegatives3_test.go
static void TestSignatureHelpTaggedTemplatesNegatives3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((std::string(R"TS(function foo(strs, ...rest) {
}

/*1*/fo/*2*/o /*3*/)TS") + "`") + std::string(R"TS(abcd${0 + 1}abcd{1 + 1}abcd)TS")) + std::string("`")) + std::string(R"TS(/*4*/  /*5*/)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoSignatureHelpForMarkers(t, f->MarkerNames());
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpTaggedTemplatesNegatives3, TestSignatureHelpTaggedTemplatesNegatives3);

// signatureHelpTaggedTemplatesNegatives4_test.go

// signatureHelpTaggedTemplatesNegatives4_test.go
static void TestSignatureHelpTaggedTemplatesNegatives4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((std::string(R"TS(function foo(strs, ...rest) {
}

/*1*/fo/*2*/o /*3*/)TS") + "`") + std::string(R"TS()TS")) + std::string("`")) + std::string(R"TS(/*4*/  /*5*/)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoSignatureHelpForMarkers(t, f->MarkerNames());
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpTaggedTemplatesNegatives4, TestSignatureHelpTaggedTemplatesNegatives4);

// signatureHelpTaggedTemplatesNegatives5_test.go

// signatureHelpTaggedTemplatesNegatives5_test.go
static void TestSignatureHelpTaggedTemplatesNegatives5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((std::string(R"TS(function foo(strs, ...rest) {
}

/*1*/fo/*2*/o /*3*/)TS") + "`") + std::string(R"TS(abcd)TS")) + std::string("`")) + std::string(R"TS(/*4*/)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoSignatureHelpForMarkers(t, f->MarkerNames());
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpTaggedTemplatesNegatives5, TestSignatureHelpTaggedTemplatesNegatives5);

// signatureHelpThis_test.go

// signatureHelpThis_test.go
static void TestSignatureHelpThis(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo<T> {
    public implicitAny(n: number) {
    }
    public explicitThis(this: this, n: number) {
        console.log(this);
    }
    public explicitClass(this: Foo<T>, n: number) {
        console.log(this);
    }
}

function implicitAny(x: number): void {
    return this;
}
function explicitVoid(this: void, x: number): void {
    return this;
}
function explicitLiteral(this: { n: number }, x: number): void {
    console.log(this);
}
let foo = new Foo<number>();
foo.implicitAny(/*1*/);
foo.explicitThis(/*2*/);
foo.explicitClass(/*3*/);
implicitAny(/*4*/12);
explicitVoid(/*5*/13);
let o = { n: 14, m: explicitLiteral };
o.m(/*6*/);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.ParameterName = "n"});
		f->GoToMarker(t, "2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.ParameterName = "n"});
		f->GoToMarker(t, "3");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.ParameterName = "n"});
		f->GoToMarker(t, "4");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.ParameterName = "x"});
		f->GoToMarker(t, "5");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.ParameterName = "x"});
		f->GoToMarker(t, "6");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.ParameterName = "x"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpThis, TestSignatureHelpThis);

// signatureHelpTokenCrash_test.go

// signatureHelpTokenCrash_test.go
static void TestSignatureHelpTokenCrash(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
function foo(a: any, b: any) {

}

foo((/*1*/

/** This is a JSDoc comment */
foo/** More comments*/((/*2*/
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySignatureHelpWithCases(t, {std::make_shared<fourslash::SignatureHelpCase>(fourslash::SignatureHelpCase{.Context = std::make_shared<lsproto::SignatureHelpContext>(lsproto::SignatureHelpContext{.TriggerKind = lsproto::SignatureHelpTriggerKindTriggerCharacter, .TriggerCharacter = std::string("("), .IsRetrigger = false}), .MarkerInput = "1", .Expected = nullptr})});
		f->VerifySignatureHelpWithCases(t, {std::make_shared<fourslash::SignatureHelpCase>(fourslash::SignatureHelpCase{.Context = std::make_shared<lsproto::SignatureHelpContext>(lsproto::SignatureHelpContext{.TriggerKind = lsproto::SignatureHelpTriggerKindTriggerCharacter, .TriggerCharacter = std::string("("), .IsRetrigger = false}), .MarkerInput = "2", .Expected = nullptr})});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpTokenCrash, TestSignatureHelpTokenCrash);

// signatureHelpTrailingRestTuple_test.go

// signatureHelpTrailingRestTuple_test.go
static void TestSignatureHelpTrailingRestTuple(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(export function leading(allCaps: boolean, ...names: string[]): void {
}

leading(/*1*/);
leading(false, /*2*/);
leading(false, "ok", /*3*/);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "leading(allCaps: boolean, ...names: string[]): void", .ParameterCount = 2, .ParameterName = "allCaps", .ParameterSpan = "allCaps: boolean", .OverloadsCount = 1, .IsVariadic = true, .IsVariadicSet = true});
		f->GoToMarker(t, "2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "leading(allCaps: boolean, ...names: string[]): void", .ParameterCount = 2, .ParameterName = "names", .ParameterSpan = "...names: string[]", .OverloadsCount = 1, .IsVariadic = true, .IsVariadicSet = true});
		f->GoToMarker(t, "3");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "leading(allCaps: boolean, ...names: string[]): void", .ParameterCount = 2, .ParameterName = "names", .ParameterSpan = "...names: string[]", .OverloadsCount = 1, .IsVariadic = true, .IsVariadicSet = true});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpTrailingRestTuple, TestSignatureHelpTrailingRestTuple);

// signatureHelpTypeArguments2VS_test.go

// signatureHelpTypeArguments2VS_test.go
static void TestSignatureHelpTypeArguments2VS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** some documentation
 * @template T some documentation 2
 * @template W
 * @template U,V others
 * @param a ok
 * @param b not ok
 */
function f<T, U, V, W>(a: number, b: string, c: boolean): void { }
f</*f0*/;
f<number, /*f1*/;
f<number, string, /*f2*/;
f<number, string, boolean, /*f3*/;)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpTypeArguments2VS, TestSignatureHelpTypeArguments2VS);

// signatureHelpTypeArguments2_test.go

// signatureHelpTypeArguments2_test.go
static void TestSignatureHelpTypeArguments2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** some documentation
 * @template T some documentation 2
 * @template W
 * @template U,V others
 * @param a ok
 * @param b not ok
 */
function f<T, U, V, W>(a: number, b: string, c: boolean): void { }
f</*f0*/;
f<number, /*f1*/;
f<number, string, /*f2*/;
f<number, string, boolean, /*f3*/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpTypeArguments2, TestSignatureHelpTypeArguments2);

// signatureHelpTypeArgumentsWithUntypedTarget_test.go

// signatureHelpTypeArgumentsWithUntypedTarget_test.go
static void TestSignatureHelpOnTypeArgumentsWithUnresolvedTarget(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
/*1*/un/*2*/resolvedVal/*3*/</*4*/Un/*5*/resolvedType/*6*/>/*7*/(/*8*/un/*9*/resolvedVal/*10*/);
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToEachMarker(t, {}, [&](std::shared_ptr<fourslash::Marker> marker, int index) {
	f->VerifyNoSignatureHelp(t);
	});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpOnTypeArgumentsWithUnresolvedTarget, TestSignatureHelpOnTypeArgumentsWithUnresolvedTarget);

// signatureHelpTypeArguments_test.go

// signatureHelpTypeArguments_test.go
static void TestSignatureHelpTypeArguments(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare function f(a: number, b: string, c: boolean): void; // ignored, not generic
declare function f<T extends number>(): void;
declare function f<T, U>(): void;
declare function f<T, U, V extends string>(): void;
f</*f0*/;
f<number, /*f1*/;
f<number, string, /*f2*/;

declare const C: {
    new<T extends number>(): void;
    new<T, U>(): void;
    new<T, U, V extends string>(): void;
};
new C</*C0*/;
new C<number, /*C1*/;
new C<number, string, /*C2*/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "f0");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "f<T extends number>(): void", .ParameterName = "T", .ParameterSpan = "T extends number", .OverloadsCount = 3});
		f->GoToMarker(t, "f1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "f<T, U>(): void", .ParameterName = "U", .ParameterSpan = "U", .OverloadsCount = 2});
		f->GoToMarker(t, "f2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "f<T, U, V extends string>(): void", .ParameterName = "V", .ParameterSpan = "V extends string"});
		f->GoToMarker(t, "C0");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "C<T extends number>(): void", .ParameterName = "T", .ParameterSpan = "T extends number", .OverloadsCount = 3});
		f->GoToMarker(t, "C1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "C<T, U>(): void", .ParameterName = "U", .ParameterSpan = "U", .OverloadsCount = 2});
		f->GoToMarker(t, "C2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "C<T, U, V extends string>(): void", .ParameterName = "V", .ParameterSpan = "V extends string"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpTypeArguments, TestSignatureHelpTypeArguments);

// signatureHelpTypeParametersNotVariadic_test.go

// signatureHelpTypeParametersNotVariadic_test.go
static void TestSignatureHelpTypeParametersNotVariadic(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare function f(a: any, ...b: any[]): any;
f</*1*/>(1, 2);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.IsVariadic = false, .IsVariadicSet = true});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpTypeParametersNotVariadic, TestSignatureHelpTypeParametersNotVariadic);

// signatureHelpUnresolvedTypeErrorRecovery_test.go

// signatureHelpUnresolvedTypeErrorRecovery_test.go
static void TestSignatureHelpUnresolvedTypeInErrorRecoveredSignature(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f(x: {
    a?: U =
    b?: (p: U) => void
}) {}
f(/*a*/);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpUnresolvedTypeInErrorRecoveredSignature, TestSignatureHelpUnresolvedTypeInErrorRecoveredSignature);

// signatureHelpWithInterfaceAsIdentifier_test.go

// signatureHelpWithInterfaceAsIdentifier_test.go
static void TestSignatureHelpWithInterfaceAsIdentifier(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface C {
    (): void;
}
C(/*1*/);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNoSignatureHelpForMarkers(t, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpWithInterfaceAsIdentifier, TestSignatureHelpWithInterfaceAsIdentifier);

// signatureHelpWithInvalidArgumentList1_test.go

// signatureHelpWithInvalidArgumentList1_test.go
static void TestSignatureHelpWithInvalidArgumentList1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function foo(a) { }
foo(hello my name /**/is)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "foo(a: any): void"});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpWithInvalidArgumentList1, TestSignatureHelpWithInvalidArgumentList1);

// signatureHelpWithTriggers02_test.go

// signatureHelpWithTriggers02_test.go
static void TestSignatureHelpWithTriggers02(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare function foo<T>(x: T, y: T): T;
declare function bar<U>(x: U, y: U): U;

foo(bar/*1*/))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "1");
		f->Insert(t, "(");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "bar(x: unknown, y: unknown): unknown"});
		f->Backspace(t, 1);
		f->Insert(t, "<");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "bar<U>(x: U, y: U): U"});
		f->Backspace(t, 1);
		f->Insert(t, ",");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "foo(x: <U>(x: U, y: U) => U, y: <U>(x: U, y: U) => U): <U>(x: U, y: U) => U"});
		f->Backspace(t, 1);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpWithTriggers02, TestSignatureHelpWithTriggers02);

// signatureHelpWithUnknownVS_test.go

// signatureHelpWithUnknownVS_test.go
static void TestSignatureHelpWithUnknownVS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(eval(\/*1*/)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelpWithUnknownVS, TestSignatureHelpWithUnknownVS);

// signatureHelp_contextual_test.go

// signatureHelp_contextual_test.go
static void TestSignatureHelp_contextual(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    m(n: number, s: string): void;
    m2: () => void;
}
declare function takesObj(i: I): void;
takesObj({ m: (/*takesObj0*/) });
takesObj({ m(/*takesObj1*/) });
takesObj({ m: function(/*takesObj2*/) });
takesObj({ m2: (/*takesObj3*/) });

declare function takesCb(cb: (n: number, s: string, b: boolean) => void): void;
takesCb((/*contextualParameter1*/));
takesCb((/*contextualParameter1b*/) => {});
takesCb((n, /*contextualParameter2*/));
takesCb((n, s, /*contextualParameter3*/));
takesCb((n,/*contextualParameter3_2*/ s, b));
takesCb((n, s, b, /*contextualParameter4*/));

type Cb = () => void;
const cb: Cb = (/*contextualTypeAlias*/)

const cb2: () => void = (/*contextualFunctionType*/))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "takesObj0");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "m(n: number, s: string): void", .ParameterCount = 2, .ParameterName = "n", .ParameterSpan = "n: number"});
		f->GoToMarker(t, "takesObj1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "m(n: number, s: string): void", .ParameterCount = 2, .ParameterName = "n", .ParameterSpan = "n: number"});
		f->GoToMarker(t, "takesObj2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "m(n: number, s: string): void", .ParameterCount = 2, .ParameterName = "n", .ParameterSpan = "n: number"});
		f->GoToMarker(t, "takesObj3");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "m2(): void", .ParameterCount = 0});
		f->GoToMarker(t, "contextualParameter1");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "cb(n: number, s: string, b: boolean): void", .ParameterCount = 3, .ParameterName = "n", .ParameterSpan = "n: number"});
		f->GoToMarker(t, "contextualParameter1b");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "cb(n: number, s: string, b: boolean): void", .ParameterCount = 3, .ParameterName = "n", .ParameterSpan = "n: number"});
		f->GoToMarker(t, "contextualParameter2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "cb(n: number, s: string, b: boolean): void", .ParameterCount = 3, .ParameterName = "s", .ParameterSpan = "s: string"});
		f->GoToMarker(t, "contextualParameter3");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "cb(n: number, s: string, b: boolean): void", .ParameterCount = 3, .ParameterName = "b", .ParameterSpan = "b: boolean"});
		f->GoToMarker(t, "contextualParameter3_2");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "cb(n: number, s: string, b: boolean): void", .ParameterCount = 3, .ParameterName = "s", .ParameterSpan = "s: string"});
		f->GoToMarker(t, "contextualParameter4");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "cb(n: number, s: string, b: boolean): void", .ParameterCount = 3});
		f->GoToMarker(t, "contextualTypeAlias");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "Cb(): void", .ParameterCount = 0});
		f->GoToMarker(t, "contextualFunctionType");
		f->VerifySignatureHelp(t, fourslash::VerifySignatureHelpOptions{.Text = "cb2(): void", .ParameterCount = 0});
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelp_contextual, TestSignatureHelp_contextual);

// signatureHelp_unionTypeVS_test.go

// signatureHelp_unionTypeVS_test.go
static void TestSignatureHelp_unionTypeVS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare const a: (fn?: ((x: string) => string) | ((y: number) => number)) => void;
declare const b: (x: string | number) => void;

interface Callback {
    (x: string): string;
    (x: number): number;
    (x: string | number): string | number;
}
declare function c(callback: Callback): void;
a((/*1*/) => {
    return undefined;
});

b(/*2*/);

c((/*3*/) => {});)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelp_unionTypeVS, TestSignatureHelp_unionTypeVS);

// signatureHelp_unionType_test.go

// signatureHelp_unionType_test.go
static void TestSignatureHelp_unionType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare const a: (fn?: ((x: string) => string) | ((y: number) => number)) => void;
declare const b: (x: string | number) => void;

interface Callback {
    (x: string): string;
    (x: number): number;
    (x: string | number): string | number;
}
declare function c(callback: Callback): void;
a((/*1*/) => {
    return undefined;
});

b(/*2*/);

c((/*3*/) => {});)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSignatureHelp_unionType, TestSignatureHelp_unionType);

// trailingCommaSignatureHelpVS_test.go

// trailingCommaSignatureHelpVS_test.go
static void TestTrailingCommaSignatureHelpVS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function str(n: number): string;
/**
 * Stringifies a number with radix
 * @param radix The radix
 */
function str(n: number, radix: number): string;
function str(n: number, radix?: number): string { return ""; }

str(1, /*a*/)

declare function f<T>(a: T): T;
f(2, /*b*/);)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = bool(true)}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestTrailingCommaSignatureHelpVS, TestTrailingCommaSignatureHelpVS);

// trailingCommaSignatureHelp_test.go

// trailingCommaSignatureHelp_test.go
static void TestTrailingCommaSignatureHelp(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function str(n: number): string;
/**
 * Stringifies a number with radix
 * @param radix The radix
 */
function str(n: number, radix: number): string;
function str(n: number, radix?: number): string { return ""; }

str(1, /*a*/)

declare function f<T>(a: T): T;
f(2, /*b*/);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSignatureHelp(t);
	});
}
REGISTER_FOURSLASH_TEST(TestTrailingCommaSignatureHelp, TestTrailingCommaSignatureHelp);

} // namespace
