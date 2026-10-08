// Ported fourslash tests -- batch B (quickinfodp). One static void TestX(gostd::testing::T*)
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

// quickInfoDisplayPartsArrowFunctionExpression_test.go
static void TestQuickInfoDisplayPartsArrowFunctionExpression(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(var /*1*/x = /*5*/a => 10;
var /*2*/y = (/*6*/a, /*7*/b) => 10;
var /*3*/z = (/*8*/a: number) => 10;
var /*4*/z2 = () => 10;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsArrowFunctionExpression, TestQuickInfoDisplayPartsArrowFunctionExpression);

// quickInfoDisplayPartsClassAccessors_test.go
static void TestQuickInfoDisplayPartsClassAccessors(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(class c {
    public get /*1*/publicProperty() { return ""; }
    public set /*1s*/publicProperty(x: string) { }
    private get /*2*/privateProperty() { return ""; }
    private set /*2s*/privateProperty(x: string) { }
    protected get /*21*/protectedProperty() { return ""; }
    protected set /*21s*/protectedProperty(x: string) { }
    static get /*3*/staticProperty() { return ""; }
    static set /*3s*/staticProperty(x: string) { }
    private static get  /*4*/privateStaticProperty() { return ""; }
    private static set /*4s*/privateStaticProperty(x: string) { }
    protected static get /*41*/protectedStaticProperty() { return ""; }
    protected static set /*41s*/protectedStaticProperty(x: string) { }
    method() {
        var x : string;
        x = this./*5*/publicProperty;
        x = this./*6*/privateProperty;
        x = this./*61*/protectedProperty;
        x = c./*7*/staticProperty;
        x = c./*8*/privateStaticProperty;
        x = c./*81*/protectedStaticProperty;
        this./*5s*/publicProperty = "";
        this./*6s*/privateProperty = "";
        this./*61s*/protectedProperty = "";
        c./*7s*/staticProperty = "";
        c./*8s*/privateStaticProperty = "";
        c./*81s*/protectedStaticProperty = "";
    }
}
var cInstance = new c();
var y: string;
y = /*9*/cInstance./*10*/publicProperty;
y = /*11*/c./*12*/staticProperty;
/*9s*/cInstance./*10s*/publicProperty = y;
/*11s*/c./*12s*/staticProperty = y;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsClassAccessors, TestQuickInfoDisplayPartsClassAccessors);

// quickInfoDisplayPartsClassAutoAccessors_test.go
static void TestQuickInfoDisplayPartsClassAutoAccessors(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(class c {
    public accessor /*1a*/publicProperty: string;
    private accessor /*2a*/privateProperty: string;
    protected accessor /*3a*/protectedProperty: string;
    static accessor /*4a*/staticProperty: string;
    private static accessor /*5a*/privateStaticProperty: string;
    protected static accessor /*6a*/protectedStaticProperty: string;
    method() {
        var x: string;
        x = this./*1g*/publicProperty;
        x = this./*2g*/privateProperty;
        x = this./*3g*/protectedProperty;
        x = c./*4g*/staticProperty;
        x = c./*5g*/privateStaticProperty;
        x = c./*6g*/protectedStaticProperty;
        this./*1s*/publicProperty = "";
        this./*2s*/privateProperty = "";
        this./*3s*/protectedProperty = "";
        c./*4s*/staticProperty = "";
        c./*5s*/privateStaticProperty = "";
        c./*6s*/protectedStaticProperty = "";
    }
}
var cInstance = new c();
var y: string;
y = /*7g*/cInstance./*8g*/publicProperty;
y = /*9g*/c./*10g*/staticProperty;
/*7s*/cInstance./*8s*/publicProperty = y;
/*9s*/c./*10s*/staticProperty = y;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsClassAutoAccessors, TestQuickInfoDisplayPartsClassAutoAccessors);

// quickInfoDisplayPartsClassConstructor_test.go
static void TestQuickInfoDisplayPartsClassConstructor(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(class c {
    /*1*/constructor() {
    }
}
var /*2*/cInstance = new /*3*/c();
var /*4*/cVal = /*5*/c;
class cWithOverloads {
    /*6*/constructor(x: string);
    /*7*/constructor(x: number);
    /*8*/constructor(x: any) {
    }
}
var /*9*/cWithOverloadsInstance = new /*10*/cWithOverloads("hello");
var /*11*/cWithOverloadsInstance2 = new /*12*/cWithOverloads(10);
var /*13*/cWithOverloadsVal = /*14*/cWithOverloads;
class cWithMultipleOverloads {
    /*15*/constructor(x: string);
    /*16*/constructor(x: number);
    /*17*/constructor(x: boolean);
    /*18*/constructor(x: any) {
    }
}
var /*19*/cWithMultipleOverloadsInstance = new /*20*/cWithMultipleOverloads("hello");
var /*21*/cWithMultipleOverloadsInstance2 = new /*22*/cWithMultipleOverloads(10);
var /*23*/cWithMultipleOverloadsInstance3 = new /*24*/cWithMultipleOverloads(true);
var /*25*/cWithMultipleOverloadsVal = /*26*/cWithMultipleOverloads;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsClassConstructor, TestQuickInfoDisplayPartsClassConstructor);

// quickInfoDisplayPartsClassDefaultAnonymous_test.go
static void TestQuickInfoDisplayPartsClassDefaultAnonymous(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(/*1*/export /*2*/default /*3*/class /*4*/ {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsClassDefaultAnonymous, TestQuickInfoDisplayPartsClassDefaultAnonymous);

// quickInfoDisplayPartsClassDefaultNamed_test.go
static void TestQuickInfoDisplayPartsClassDefaultNamed(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(/*1*/export /*2*/default /*3*/class /*4*/C /*5*/ {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsClassDefaultNamed, TestQuickInfoDisplayPartsClassDefaultNamed);

// quickInfoDisplayPartsClassIncomplete_test.go
static void TestQuickInfoDisplayPartsClassIncomplete(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(/*1*/class /*2*/ {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsClassIncomplete, TestQuickInfoDisplayPartsClassIncomplete);

// quickInfoDisplayPartsClassMethodVS_test.go
static void TestQuickInfoDisplayPartsClassMethodVS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(class c {
    public /*1*/publicMethod() { }
    private /*2*/privateMethod() { }
    protected /*21*/protectedMethod() { }
    static /*3*/staticMethod() { }
    private static /*4*/privateStaticMethod() { }
    protected static /*41*/protectedStaticMethod() { }
    method() {
        this./*5*/publicMethod();
        this./*6*/privateMethod();
        this./*61*/protectedMethod();
        c./*7*/staticMethod();
        c./*8*/privateStaticMethod();
        c./*81*/protectedStaticMethod();
    }
}
var cInstance = new c();
/*9*/cInstance./*10*/publicMethod();
/*11*/c./*12*/staticMethod();)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = true}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineVSHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsClassMethodVS, TestQuickInfoDisplayPartsClassMethodVS);

// quickInfoDisplayPartsClassMethod_test.go
static void TestQuickInfoDisplayPartsClassMethod(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(class c {
    public /*1*/publicMethod() { }
    private /*2*/privateMethod() { }
    protected /*21*/protectedMethod() { }
    static /*3*/staticMethod() { }
    private static /*4*/privateStaticMethod() { }
    protected static /*41*/protectedStaticMethod() { }
    method() {
        this./*5*/publicMethod();
        this./*6*/privateMethod();
        this./*61*/protectedMethod();
        c./*7*/staticMethod();
        c./*8*/privateStaticMethod();
        c./*81*/protectedStaticMethod();
    }
}
var cInstance = new c();
/*9*/cInstance./*10*/publicMethod();
/*11*/c./*12*/staticMethod();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsClassMethod, TestQuickInfoDisplayPartsClassMethod);

// quickInfoDisplayPartsClassPropertyVS_test.go
static void TestQuickInfoDisplayPartsClassPropertyVS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(class c {
    public /*1*/publicProperty: string;
    private /*2*/privateProperty: string;
    protected /*21*/protectedProperty: string;
    static /*3*/staticProperty: string;
    private static /*4*/privateStaticProperty: string;
    protected static /*41*/protectedStaticProperty: string;
    method() {
        this./*5*/publicProperty;
        this./*6*/privateProperty;
        this./*61*/protectedProperty;
        c./*7*/staticProperty;
        c./*8*/privateStaticProperty;
        c./*81*/protectedStaticProperty;
    }
}
var cInstance = new c();
/*9*/cInstance./*10*/publicProperty;
/*11*/c./*12*/staticProperty;)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = true}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineVSHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsClassPropertyVS, TestQuickInfoDisplayPartsClassPropertyVS);

// quickInfoDisplayPartsClassProperty_test.go
static void TestQuickInfoDisplayPartsClassProperty(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(class c {
    public /*1*/publicProperty: string;
    private /*2*/privateProperty: string;
    protected /*21*/protectedProperty: string;
    static /*3*/staticProperty: string;
    private static /*4*/privateStaticProperty: string;
    protected static /*41*/protectedStaticProperty: string;
    method() {
        this./*5*/publicProperty;
        this./*6*/privateProperty;
        this./*61*/protectedProperty;
        c./*7*/staticProperty;
        c./*8*/privateStaticProperty;
        c./*81*/protectedStaticProperty;
    }
}
var cInstance = new c();
/*9*/cInstance./*10*/publicProperty;
/*11*/c./*12*/staticProperty;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsClassProperty, TestQuickInfoDisplayPartsClassProperty);

// quickInfoDisplayPartsClass_test.go
static void TestQuickInfoDisplayPartsClass(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(class /*1*/c {
}
var /*2*/cInstance = new /*3*/c();
var /*4*/cVal = /*5*/c;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsClass, TestQuickInfoDisplayPartsClass);

// quickInfoDisplayPartsConst_test.go
static void TestQuickInfoDisplayPartsConst(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const /*1*/a = 10;
function foo() {
    const /*2*/b = /*3*/a;
    if (b) {
        const /*4*/b1 = 10;
    }
}
namespace m {
    const /*5*/c = 10;
    export const /*6*/d = 10;
    if (c) {
        const /*7*/e = 10;
    }
}
const /*8*/f: () => number = () => 10;
const /*9*/g = /*10*/f;
/*11*/f();
const /*12*/h: { (a: string): number; (a: number): string; } = a => a;
const /*13*/i = /*14*/h;
/*15*/h(10);
/*16*/h("hello");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsConst, TestQuickInfoDisplayPartsConst);

// quickInfoDisplayPartsEnum1_test.go
static void TestQuickInfoDisplayPartsEnum1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(enum /*1*/E {
    /*2*/e1,
    /*3*/e2 = 10,
    /*4*/e3
}
var /*5*/eInstance: /*6*/E;
/*7*/eInstance = /*8*/E./*9*/e1;
/*10*/eInstance = /*11*/E./*12*/e2;
/*13*/eInstance = /*14*/E./*15*/e3;
const enum /*16*/constE {
    /*17*/e1,
    /*18*/e2 = 10,
    /*19*/e3
}
var /*20*/eInstance1: /*21*/constE;
/*22*/eInstance1 = /*23*/constE./*24*/e1;
/*25*/eInstance1 = /*26*/constE./*27*/e2;
/*28*/eInstance1 = /*29*/constE./*30*/e3;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsEnum1, TestQuickInfoDisplayPartsEnum1);

// quickInfoDisplayPartsEnum2_test.go
static void TestQuickInfoDisplayPartsEnum2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(enum /*1*/E {
    /*2*/"e1",
    /*3*/'e2' = 10,
    /*4*/"e3"
}
var /*5*/eInstance: /*6*/E;
/*7*/eInstance = /*8*/E./*9*/e1;
/*10*/eInstance = /*11*/E./*12*/e2;
/*13*/eInstance = /*14*/E./*15*/e3;
const enum /*16*/constE {
    /*17*/"e1",
    /*18*/'e2' = 10,
    /*19*/"e3"
}
var /*20*/eInstance1: /*21*/constE;
/*22*/eInstance1 = /*23*/constE./*24*/e1;
/*25*/eInstance1 = /*26*/constE./*27*/e2;
/*28*/eInstance1 = /*29*/constE./*30*/e3;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsEnum2, TestQuickInfoDisplayPartsEnum2);

// quickInfoDisplayPartsEnum3_test.go
static void TestQuickInfoDisplayPartsEnum3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(enum /*1*/E {
    /*2*/"e1",
    /*3*/'e2' = 10,
    /*4*/"e3"
}
var /*5*/eInstance: /*6*/E;
/*7*/eInstance = /*8*/E[/*9*/"e1"];
/*10*/eInstance = /*11*/E[/*12*/"e2"];
/*13*/eInstance = /*14*/E[/*15*/'e3'];
const enum /*16*/constE {
    /*17*/"e1",
    /*18*/'e2' = 10,
    /*19*/"e3"
}
var /*20*/eInstance1: /*21*/constE;
/*22*/eInstance1 = /*23*/constE[/*24*/"e1"];
/*25*/eInstance1 = /*26*/constE[/*27*/"e2"];
/*28*/eInstance1 = /*29*/constE[/*30*/'e3'];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsEnum3, TestQuickInfoDisplayPartsEnum3);

// quickInfoDisplayPartsEnum4_test.go
static void TestQuickInfoDisplayPartsEnum4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const enum Foo {
	"\t" = 9,
	"\u007f" = 127,
}
Foo[/*1*/"\t"]
Foo[/*2*/"\u007f"])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsEnum4, TestQuickInfoDisplayPartsEnum4);

// quickInfoDisplayPartsExternalModuleAlias_test.go
static void TestQuickInfoDisplayPartsExternalModuleAlias(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: quickInfoDisplayPartsExternalModuleAlias_file0.ts
export namespace m1 {
    export class c {
    }
}
// @Filename: quickInfoDisplayPartsExternalModuleAlias_file1.ts
import /*1*/a1 = require(/*mod1*/"./quickInfoDisplayPartsExternalModuleAlias_file0");
new /*2*/a1.m1.c();
export import /*3*/a2 = require(/*mod2*/"./quickInfoDisplayPartsExternalModuleAlias_file0");
new /*4*/a2.m1.c();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsExternalModuleAlias, TestQuickInfoDisplayPartsExternalModuleAlias);

// quickInfoDisplayPartsExternalModules_test.go
static void TestQuickInfoDisplayPartsExternalModules(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(export namespace /*1*/m {
    var /*2*/namespaceElemWithoutExport = 10;
    export var /*3*/namespaceElemWithExport = 10;
}
export var /*4*/a = /*5*/m;
export var /*6*/b: typeof /*7*/m;
export namespace /*8*/m1./*9*/m2 {
    var /*10*/namespaceElemWithoutExport = 10;
    export var /*11*/namespaceElemWithExport = 10;
}
export var /*12*/x = /*13*/m1./*14*/m2;
export var /*15*/y: typeof /*16*/m1./*17*/m2;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsExternalModules, TestQuickInfoDisplayPartsExternalModules);

// quickInfoDisplayPartsFunctionExpression_test.go
static void TestQuickInfoDisplayPartsFunctionExpression(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(var /*1*/x = function /*2*/foo() {
    /*3*/foo();
};
var /*4*/y = function () {
};
(function /*5*/foo1() {
    /*6*/foo1();
})();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsFunctionExpression, TestQuickInfoDisplayPartsFunctionExpression);

// quickInfoDisplayPartsFunctionIncomplete_test.go
static void TestQuickInfoDisplayPartsFunctionIncomplete(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(/*1*/function /*2*/(param: string) {
}\
/*3*/function /*4*/ {
}\)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsFunctionIncomplete, TestQuickInfoDisplayPartsFunctionIncomplete);

// quickInfoDisplayPartsFunctionVS_test.go
static void TestQuickInfoDisplayPartsFunctionVS(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function /*1*/foo(param: string, optionalParam?: string, paramWithInitializer = "hello", ...restParam: string[]) {
}
function /*2*/foowithoverload(a: string): string;
function /*3*/foowithoverload(a: number): number;
function /*4*/foowithoverload(a: any): any {
    return a;
}
function /*5*/foowith3overload(a: string): string;
function /*6*/foowith3overload(a: number): number;
function /*7*/foowith3overload(a: boolean): boolean;
function /*8*/foowith3overload(a: any): any {
    return a;
}
/*9*/foo("hello");
/*10*/foowithoverload("hello");
/*11*/foowithoverload(10);
/*12*/foowith3overload("hello");
/*13*/foowith3overload(10);
/*14*/foowith3overload(true);)TS";
		auto __fsp = fourslash::NewFourslash(t, std::make_shared<lsproto::ClientCapabilities>(lsproto::ClientCapabilities{.VSSupportsVisualStudioExtensions = true}), content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineVSHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsFunctionVS, TestQuickInfoDisplayPartsFunctionVS);

// quickInfoDisplayPartsFunction_test.go
static void TestQuickInfoDisplayPartsFunction(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function /*1*/foo(param: string, optionalParam?: string, paramWithInitializer = "hello", ...restParam: string[]) {
}
function /*2*/foowithoverload(a: string): string;
function /*3*/foowithoverload(a: number): number;
function /*4*/foowithoverload(a: any): any {
    return a;
}
function /*5*/foowith3overload(a: string): string;
function /*6*/foowith3overload(a: number): number;
function /*7*/foowith3overload(a: boolean): boolean;
function /*8*/foowith3overload(a: any): any {
    return a;
}
/*9*/foo("hello");
/*10*/foowithoverload("hello");
/*11*/foowithoverload(10);
/*12*/foowith3overload("hello");
/*13*/foowith3overload(10);
/*14*/foowith3overload(true);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsFunction, TestQuickInfoDisplayPartsFunction);

// quickInfoDisplayPartsIife_test.go
static void TestQuickInfoDisplayPartsIife(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"});
		return;
		const std::string content = R"TS(// @strictNullChecks: true
var iife = (function foo/*1*/(x, y) { return x })(12);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(local function) foo(x: number, y?: undefined): number", "");
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsIife, TestQuickInfoDisplayPartsIife);

// quickInfoDisplayPartsInterfaceMembers_test.go
static void TestQuickInfoDisplayPartsInterfaceMembers(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(interface I {
    /*1*/property: string;
    /*2*/method(): string;
    (): string;
    new (): I;
}
var iInstance: I;
/*3*/iInstance./*4*/property = /*5*/iInstance./*6*/method();
/*7*/iInstance();
var /*8*/anotherInstance = new /*9*/iInstance();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsInterfaceMembers, TestQuickInfoDisplayPartsInterfaceMembers);

// quickInfoDisplayPartsInterface_test.go
static void TestQuickInfoDisplayPartsInterface(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(interface /*1*/i {
}
var /*2*/iInstance: /*3*/i;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsInterface, TestQuickInfoDisplayPartsInterface);

// quickInfoDisplayPartsInternalModuleAlias_test.go
static void TestQuickInfoDisplayPartsInternalModuleAlias(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(namespace m.m1 {
    export class c {
    }
}
namespace m2 {
    import /*1*/a1 = m;
    new /*2*/a1.m1.c();
    import /*3*/a2 = m.m1;
    new /*4*/a2.c();
    export import /*5*/a3 = m;
    new /*6*/a3.m1.c();
    export import /*7*/a4 = m.m1;
    new /*8*/a4.c();
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsInternalModuleAlias, TestQuickInfoDisplayPartsInternalModuleAlias);

// quickInfoDisplayPartsLet_test.go
static void TestQuickInfoDisplayPartsLet(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(let /*1*/a = 10;
function foo() {
    let /*2*/b = /*3*/a;
    if (b) {
        let /*4*/b1 = 10;
    }
}
namespace m {
    let /*5*/c = 10;
    export let /*6*/d = 10;
    if (c) {
        let /*7*/e = 10;
    }
}
let /*8*/f: () => number;
let /*9*/g = /*10*/f;
/*11*/f();
let /*12*/h: { (a: string): number; (a: number): string; };
let /*13*/i = /*14*/h;
/*15*/h(10);
/*16*/h("hello");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsLet, TestQuickInfoDisplayPartsLet);

// quickInfoDisplayPartsLiteralLikeNames01_test.go
static void TestQuickInfoDisplayPartsLiteralLikeNames01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(class C {
    public /*1*/1() { }
    private /*2*/Infinity() { }
    protected /*3*/NaN() { }
    static /*4*/"stringLiteralName"() { }
    method() {
        this[/*5*/1]();
        this[/*6*/"1"]();
        this./*7*/Infinity();
        this[/*8*/"Infinity"]();
        this./*9*/NaN();
        C./*10*/stringLiteralName();
    })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsLiteralLikeNames01, TestQuickInfoDisplayPartsLiteralLikeNames01);

// quickInfoDisplayPartsLocalFunction_test.go
static void TestQuickInfoDisplayPartsLocalFunction(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function /*1*/outerFoo() {
    function /*2*/foo(param: string, optionalParam?: string, paramWithInitializer = "hello", ...restParam: string[]) {
    }
    function /*3*/foowithoverload(a: string): string;
    function /*4*/foowithoverload(a: number): number;
    function /*5*/foowithoverload(a: any): any {
        return a;
    }
    function /*6*/foowith3overload(a: string): string;
    function /*7*/foowith3overload(a: number): number;
    function /*8*/foowith3overload(a: boolean): boolean;
    function /*9*/foowith3overload(a: any): any {
        return a;
    }
    /*10*/foo("hello");
    /*11*/foowithoverload("hello");
    /*12*/foowithoverload(10);
    /*13*/foowith3overload("hello");
    /*14*/foowith3overload(10);
    /*15*/foowith3overload(true);
}
/*16*/outerFoo();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsLocalFunction, TestQuickInfoDisplayPartsLocalFunction);

// quickInfoDisplayPartsModules_test.go
static void TestQuickInfoDisplayPartsModules(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(namespace /*1*/m {
    var /*2*/namespaceElemWithoutExport = 10;
    export var /*3*/namespaceElemWithExport = 10;
}
var /*4*/a = /*5*/m;
var /*6*/b: typeof /*7*/m;
namespace /*8*/m1./*9*/m2 {
    var /*10*/namespaceElemWithoutExport = 10;
    export var /*11*/namespaceElemWithExport = 10;
}
var /*12*/x = /*13*/m1./*14*/m2;
var /*15*/y: typeof /*16*/m1./*17*/m2;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsModules, TestQuickInfoDisplayPartsModules);

// quickInfoDisplayPartsParameters_test.go
static void TestQuickInfoDisplayPartsParameters(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(/** @return *crunch* */
function /*1*/foo(/*2*/param: string, /*3*/optionalParam?: string, /*4*/paramWithInitializer = "hello", .../*5*/restParam: string[]) {
    /*6*/param = "Hello";
    /*7*/optionalParam = "World";
    /*8*/paramWithInitializer = "Hello";
    /*9*/restParam[0] = "World";
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsParameters, TestQuickInfoDisplayPartsParameters);

// quickInfoDisplayPartsTypeAlias_test.go
static void TestQuickInfoDisplayPartsTypeAlias(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(class /*1*/c {
}
type /*2*/t1 = /*3*/c;
var /*4*/cInstance: /*5*/t1 = new /*6*/c();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsTypeAlias, TestQuickInfoDisplayPartsTypeAlias);

// quickInfoDisplayPartsTypeParameterInClass_test.go
static void TestQuickInfoDisplayPartsTypeParameterInClass(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(class /*1*/c</*2*/T> {
    /*3*/constructor(/*4*/a: /*5*/T) {
    }
    /*6*/method</*7*/U>(/*8*/a: /*9*/U, /*10*/b: /*11*/T) {
        return /*12*/a;
    }
}
var /*13*/cInstance = new /*14*/c("Hello");
var /*15*/cVal = /*16*/c;
/*17*/cInstance./*18*/method("hello", "cello");
class /*19*/c2</*20*/T extends /*21*/c<string>> {
    /*22*/constructor(/*23*/a: /*24*/T) {
    }
    /*25*/method</*26*/U extends /*27*/c<string>>(/*28*/a: /*29*/U, /*30*/b: /*31*/T) {
        return /*32*/a;
    }
}
var /*33*/cInstance1 = new /*34*/c2(/*35*/cInstance);
var /*36*/cVal2 = /*37*/c2;
/*38*/cInstance1./*39*/method(/*40*/cInstance, /*41*/cInstance);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsTypeParameterInClass, TestQuickInfoDisplayPartsTypeParameterInClass);

// quickInfoDisplayPartsTypeParameterInFunctionLikeInTypeAlias_test.go
static void TestQuickInfoDisplayPartsTypeParameterInFunctionLikeInTypeAlias(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(type MixinCtor<A> = new () => /*0*/A & { constructor: MixinCtor</*1*/A> };
type MixinCtor<A> = new () => A & { constructor: { constructor: MixinCtor</*2*/A> } };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsTypeParameterInFunctionLikeInTypeAlias, TestQuickInfoDisplayPartsTypeParameterInFunctionLikeInTypeAlias);

// quickInfoDisplayPartsTypeParameterInFunction_test.go
static void TestQuickInfoDisplayPartsTypeParameterInFunction(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function /*1*/foo</*2*/U>(/*3*/a: /*4*/U) {
    return /*5*/a;
}
/*6*/foo("Hello");
function /*7*/foo2</*8*/U extends string>(/*9*/a: /*10*/U) {
    return /*11*/a;
}
/*12*/foo2("hello");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsTypeParameterInFunction, TestQuickInfoDisplayPartsTypeParameterInFunction);

// quickInfoDisplayPartsTypeParameterInInterface_test.go
static void TestQuickInfoDisplayPartsTypeParameterInInterface(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(interface /*1*/I</*2*/T> {
    new </*3*/U>(/*4*/a: /*5*/U, /*6*/b: /*7*/T): /*8*/U;
    </*9*/U>(/*10*/a: /*11*/U, /*12*/b: /*13*/T): /*14*/U;
    /*15*/method</*16*/U>(/*17*/a: /*18*/U, /*19*/b: /*20*/T): /*21*/U;
}
var /*22*/iVal: /*23*/I<string>;
new /*24*/iVal("hello", "hello");
/*25*/iVal("hello", "hello");
/*26*/iVal./*27*/method("hello", "hello");
interface /*28*/I1</*29*/T extends /*30*/I<string>> {
    new </*31*/U extends /*32*/I<string>>(/*33*/a: /*34*/U, /*35*/b: /*36*/T): /*37*/U;
    </*38*/U extends /*39*/I<string>>(/*40*/a: /*41*/U, /*42*/b: /*43*/T): /*44*/U;
    /*45*/method</*46*/U extends /*47*/I<string>>(/*48*/a: /*49*/U, /*50*/b: /*51*/T): /*52*/U;
}
var /*53*/iVal1: /*54*/I1</*55*/I<string>>;
new /*56*/iVal1(/*57*/iVal, /*58*/iVal);
/*59*/iVal1(/*60*/iVal, /*61*/iVal);
/*62*/iVal1./*63*/method(/*64*/iVal, /*65*/iVal);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsTypeParameterInInterface, TestQuickInfoDisplayPartsTypeParameterInInterface);

// quickInfoDisplayPartsTypeParameterInTypeAlias_test.go
static void TestQuickInfoDisplayPartsTypeParameterInTypeAlias(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(type /*0*/List</*1*/T> = /*2*/T[]
type /*3*/List2</*4*/T extends string> = /*5*/T[];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsTypeParameterInTypeAlias, TestQuickInfoDisplayPartsTypeParameterInTypeAlias);

// quickInfoDisplayPartsUsing_test.go
static void TestQuickInfoDisplayPartsUsing(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @lib: esnext
using a/*a*/ = "a";
const f = async () => {
    await using /*b*/b = { async [Symbol.asyncDispose]() {} };
};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsUsing, TestQuickInfoDisplayPartsUsing);

// quickInfoDisplayPartsVarWithStringTypes01_test.go
static void TestQuickInfoDisplayPartsVarWithStringTypes01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(let /*1*/hello: "hello" | 'hello' = "hello";
let /*2*/world: 'world' = "world";
let /*3*/helloOrWorld: "hello" | 'world';)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsVarWithStringTypes01, TestQuickInfoDisplayPartsVarWithStringTypes01);

// quickInfoDisplayPartsVar_test.go
static void TestQuickInfoDisplayPartsVar(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(var /*1*/a = 10;
function foo() {
    var /*2*/b = /*3*/a;
}
namespace m {
    var /*4*/c = 10;
    export var /*5*/d = 10;
}
var /*6*/f: () => number;
var /*7*/g = /*8*/f;
/*9*/f();
var /*10*/h: { (a: string): number; (a: number): string; };
var /*11*/i = /*12*/h;
/*13*/h(10);
/*14*/h("hello");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestQuickInfoDisplayPartsVar, TestQuickInfoDisplayPartsVar);


}  // namespace
