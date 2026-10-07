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


// getNavigationBarItems_test.go

// getNavigationBarItems_test.go
static void TestGetNavigationBarItems(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
    foo;
    ["bar"]: string;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestGetNavigationBarItems, TestGetNavigationBarItems);

// navigationBarAnonymousClassAndFunctionExpressions2_test.go

// navigationBarAnonymousClassAndFunctionExpressions2_test.go
static void TestNavigationBarAnonymousClassAndFunctionExpressions2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(console.log(console.log(class Y {}, class X {}), console.log(class B {}, class A {}));
console.log(class Cls { meth() {} });)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarAnonymousClassAndFunctionExpressions2, TestNavigationBarAnonymousClassAndFunctionExpressions2);

// navigationBarAnonymousClassAndFunctionExpressions3_test.go

// navigationBarAnonymousClassAndFunctionExpressions3_test.go
static void TestNavigationBarAnonymousClassAndFunctionExpressions3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((((((std::string(R"TS(describe('foo', () => {
    test()TS") + "`") + std::string(R"TS(a ${1} b ${2})TS")) + std::string("`")) + std::string(R"TS(, () => {})
})

const a = 1;
const b = 2;
describe('foo', () => {
    test()TS")) + std::string("`")) + std::string(R"TS(a ${a} b {b})TS")) + std::string("`")) + std::string(R"TS(, () => {})
}))TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarAnonymousClassAndFunctionExpressions3, TestNavigationBarAnonymousClassAndFunctionExpressions3);

// navigationBarAnonymousClassAndFunctionExpressions_test.go

// navigationBarAnonymousClassAndFunctionExpressions_test.go
static void TestNavigationBarAnonymousClassAndFunctionExpressions(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((((((std::string(R"TS(global.cls = class { };
(function() {
    const x = () => {
        // Presence of inner function causes x to be a top-level function.
        function xx() {}
    };
    const y = {
        // This is not a top-level function (contains nothing, but shows up in childItems of its parent.)
        foo: function() {}
    };
    (function nest() {
        function moreNest() {}
    })();
})();
(function() { // Different anonymous functions are not merged
    // These will only show up as childItems.
    function z() {}
    console.log(function() {})
    describe("this", 'function', )TS") + "`") + std::string(R"TS(is a function)TS")) + std::string("`")) + std::string(R"TS(, )TS")) + std::string("`")) + std::string(R"TS(with template literal ${"a"})TS")) + std::string("`")) + std::string(R"TS(, () => {});
    [].map(() => {});
})
(function classes() {
    // Classes show up in top-level regardless of whether they have names or inner declarations.
    const cls2 = class { };
    console.log(class cls3 {});
    (class { });
}))TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarAnonymousClassAndFunctionExpressions, TestNavigationBarAnonymousClassAndFunctionExpressions);

// navigationBarAssignmentTypes_test.go

// navigationBarAssignmentTypes_test.go
static void TestNavigationBarAssignmentTypes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS('use strict'
const a = {
    ...b,
    c,
    d: 0
};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarAssignmentTypes, TestNavigationBarAssignmentTypes);

// navigationBarClassStaticBlock_test.go

// navigationBarClassStaticBlock_test.go
static void TestNavigationBarClassStaticBlock(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
  static {
    let x;
  }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarClassStaticBlock, TestNavigationBarClassStaticBlock);

// navigationBarComputedPropertyName_test.go

// navigationBarComputedPropertyName_test.go
static void TestNavigationBarComputedPropertyName(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function F(key, value) {
    return {
        [key]: value,
        "prop": true
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarComputedPropertyName, TestNavigationBarComputedPropertyName);

// navigationBarFunctionIndirectlyInVariableDeclaration_test.go

// navigationBarFunctionIndirectlyInVariableDeclaration_test.go
static void TestNavigationBarFunctionIndirectlyInVariableDeclaration(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var a = {
    propA: function() {
        var c;
    }
};
var b;
b = {
    propB: function() {
    // function must not have an empty body to appear top level
        var d;
    }
};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarFunctionIndirectlyInVariableDeclaration, TestNavigationBarFunctionIndirectlyInVariableDeclaration);

// navigationBarFunctionLikePropertyAssignments_test.go

// navigationBarFunctionLikePropertyAssignments_test.go
static void TestNavigationBarFunctionLikePropertyAssignments(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var functions = {
    a: 0,
    b: function () { },
    c: function x() { },
    d: () => { },
    e: y(),
    f() { }
};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarFunctionLikePropertyAssignments, TestNavigationBarFunctionLikePropertyAssignments);

// navigationBarFunctionPrototype2_test.go

// navigationBarFunctionPrototype2_test.go
static void TestNavigationBarFunctionPrototype2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: foo.js
A.prototype.a = function() { };
A.prototype.b = function() { };
function A() {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarFunctionPrototype2, TestNavigationBarFunctionPrototype2);

// navigationBarFunctionPrototype3_test.go

// navigationBarFunctionPrototype3_test.go
static void TestNavigationBarFunctionPrototype3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: foo.js
var A; 
A.prototype.a = function() { };
A.b = function() { };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarFunctionPrototype3, TestNavigationBarFunctionPrototype3);

// navigationBarFunctionPrototype4_test.go

// navigationBarFunctionPrototype4_test.go
static void TestNavigationBarFunctionPrototype4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: foo.js
var A; 
A.prototype = { };
A.prototype = { m() {} };
A.prototype.a = function() { };
A.b = function() { };

var B; 
B["prototype"] = { };
B["prototype"] = { m() {} };
B["prototype"]["a"] = function() { };
B["b"] = function() { };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarFunctionPrototype4, TestNavigationBarFunctionPrototype4);

// navigationBarFunctionPrototypeBroken_test.go

// navigationBarFunctionPrototypeBroken_test.go
static void TestNavigationBarFunctionPrototypeBroken(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: foo.js
function A() {}
A. // Started typing something here
A.prototype.a = function() { };
G. // Started typing something here
A.prototype.a = function() { };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarFunctionPrototypeBroken, TestNavigationBarFunctionPrototypeBroken);

// navigationBarFunctionPrototypeInterlaced_test.go

// navigationBarFunctionPrototypeInterlaced_test.go
static void TestNavigationBarFunctionPrototypeInterlaced(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: foo.js
var b = 1;
function A() {}; 
A.prototype.a = function() { };
A.b = function() { };
b = 2
/* Comment */
A.prototype.c = function() { }
var b = 2
A.prototype.d = function() { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarFunctionPrototypeInterlaced, TestNavigationBarFunctionPrototypeInterlaced);

// navigationBarFunctionPrototypeNested_test.go

// navigationBarFunctionPrototypeNested_test.go
static void TestNavigationBarFunctionPrototypeNested(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: foo.js
function A() {}
A.B = function () {  } 
A.B.prototype.d = function () {  }  
Object.defineProperty(A.B.prototype, "x", {
    get() {}
})
A.prototype.D = function () {  } 
A.prototype.D.prototype.d = function () {  } )TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarFunctionPrototypeNested, TestNavigationBarFunctionPrototypeNested);

// navigationBarFunctionPrototype_test.go

// navigationBarFunctionPrototype_test.go
static void TestNavigationBarFunctionPrototype(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: foo.js
function f() {}
f.prototype.x = 0;
f.y = 0;
f.prototype.method = function () {};
Object.defineProperty(f, 'staticProp', { 
    set: function() {}, 
    get: function(){
    } 
});
Object.defineProperty(f.prototype, 'name', { 
    set: function() {}, 
    get: function(){
    } 
}); )TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarFunctionPrototype, TestNavigationBarFunctionPrototype);

// navigationBarGetterAndSetter_test.go

// navigationBarGetterAndSetter_test.go
static void TestNavigationBarGetterAndSetter(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class X {
    get x() {}
    set x(value) {
        // Inner declaration should make the setter top-level.
        function f() {}
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarGetterAndSetter, TestNavigationBarGetterAndSetter);

// navigationBarImports_test.go

// navigationBarImports_test.go
static void TestNavigationBarImports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(import a, {b} from "m";
import c = require("m");
import * as d from "m";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarImports, TestNavigationBarImports);

// navigationBarInitializerSpans_test.go

// navigationBarInitializerSpans_test.go
static void TestNavigationBarInitializerSpans(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// get the name for the navbar from the variable name rather than the function name
const [|[|x|] = () => { var [|a|]; }|];
const [|[|f|] = function f() { var [|b|]; }|];
const [|[|y|] = { [|[|z|]: function z() { var [|c|]; }|] }|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarInitializerSpans, TestNavigationBarInitializerSpans);

// navigationBarItemsBindingPatternsInConstructor_test.go

// navigationBarItemsBindingPatternsInConstructor_test.go
static void TestNavigationBarItemsBindingPatternsInConstructor(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class A {
    x: any
    constructor([a]: any) {
    }
}
class B {
    x: any;
    constructor( {a} = { a: 1 }) {
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsBindingPatternsInConstructor, TestNavigationBarItemsBindingPatternsInConstructor);

// navigationBarItemsBindingPatterns_test.go

// navigationBarItemsBindingPatterns_test.go
static void TestNavigationBarItemsBindingPatterns(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS('use strict'
var foo, {}
var bar, []
let foo1, {a, b}
const bar1, [c, d]
var {e, x: [f, g]} = {a:1, x:[]};
var { h: i = function j() {} } = obj;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsBindingPatterns, TestNavigationBarItemsBindingPatterns);

// navigationBarItemsClass1_test.go

// navigationBarItemsClass1_test.go
static void TestNavigationBarItemsClass1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function Foo() {}
class Foo {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsClass1, TestNavigationBarItemsClass1);

// navigationBarItemsClass2_test.go

// navigationBarItemsClass2_test.go
static void TestNavigationBarItemsClass2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {}
function Foo() {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsClass2, TestNavigationBarItemsClass2);

// navigationBarItemsClass3_test.go

// navigationBarItemsClass3_test.go
static void TestNavigationBarItemsClass3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @filename: /foo.js
function Foo() {}
class Foo {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsClass3, TestNavigationBarItemsClass3);

// navigationBarItemsClass4_test.go

// navigationBarItemsClass4_test.go
static void TestNavigationBarItemsClass4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @filename: /foo.js
class Foo {}
function Foo() {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsClass4, TestNavigationBarItemsClass4);

// navigationBarItemsClass5_test.go

// navigationBarItemsClass5_test.go
static void TestNavigationBarItemsClass5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {}
let Foo = 1;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsClass5, TestNavigationBarItemsClass5);

// navigationBarItemsClass6_test.go

// navigationBarItemsClass6_test.go
static void TestNavigationBarItemsClass6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function Z() { }

Z.foo = 42

class Z { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsClass6, TestNavigationBarItemsClass6);

// navigationBarItemsComputedNames_test.go

// navigationBarItemsComputedNames_test.go
static void TestNavigationBarItemsComputedNames(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(const enum E {
	A = 'A',
}
const a = '';

class C {
    [a]() {
        return 1;
    }

    [E.A]() {
        return 1;
    }

    [1]() {
        return 1;
    },

    ["foo"]() {
        return 1;
    },
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsComputedNames, TestNavigationBarItemsComputedNames);

// navigationBarItemsEmptyConstructors_test.go

// navigationBarItemsEmptyConstructors_test.go
static void TestNavigationBarItemsEmptyConstructors(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Test {
    constructor() {
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsEmptyConstructors, TestNavigationBarItemsEmptyConstructors);

// navigationBarItemsExports_test.go

// navigationBarItemsExports_test.go
static void TestNavigationBarItemsExports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(export { a } from "a";

export { b as B } from "a" 

export import e = require("a");

export * from "a"; // no bindings here)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsExports, TestNavigationBarItemsExports);

// navigationBarItemsFunctionProperties_test.go

// navigationBarItemsFunctionProperties_test.go
static void TestNavigationBarItemsFunctionProperties(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS((function(){
var A;
A/*1*/
.a = function() { };
})();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsFunctionProperties, TestNavigationBarItemsFunctionProperties);

// navigationBarItemsFunctionsBroken2_test.go

// navigationBarItemsFunctionsBroken2_test.go
static void TestNavigationBarItemsFunctionsBroken2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function;
function f() {
    function;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsFunctionsBroken2, TestNavigationBarItemsFunctionsBroken2);

// navigationBarItemsFunctionsBroken_test.go

// navigationBarItemsFunctionsBroken_test.go
static void TestNavigationBarItemsFunctionsBroken(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f() {
    function;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsFunctionsBroken, TestNavigationBarItemsFunctionsBroken);

// navigationBarItemsFunctions_test.go

// navigationBarItemsFunctions_test.go
static void TestNavigationBarItemsFunctions(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function foo() {
    var x = 10;
    function bar() {
        var y = 10;
        function biz() {
            var z = 10;
        }
        function qux() {
            // A function with an empty body should not be top level
        }
    }
}

function baz() {
    var v = 10;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsFunctions, TestNavigationBarItemsFunctions);

// navigationBarItemsImports_test.go

// navigationBarItemsImports_test.go
static void TestNavigationBarItemsImports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(import d1 from "a";

import { a } from "a";

import { b as B } from "a" 

import d2, { c, d as D } from "a" 

import e = require("a");

import * as ns from "a";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsImports, TestNavigationBarItemsImports);

// navigationBarItemsInsideMethodsAndConstructors_test.go

// navigationBarItemsInsideMethodsAndConstructors_test.go
static void TestNavigationBarItemsInsideMethodsAndConstructors(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Class {
    constructor() {
        function LocalFunctionInConstructor() {}
        interface LocalInterfaceInConstrcutor {}
        enum LocalEnumInConstructor { LocalEnumMemberInConstructor }
    }

    method() {
        function LocalFunctionInMethod() {
            function LocalFunctionInLocalFunctionInMethod() {}
        }
        interface LocalInterfaceInMethod {}
        enum LocalEnumInMethod { LocalEnumMemberInMethod }
    }

    emptyMethod() { } // Non child functions method should not be duplicated
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsInsideMethodsAndConstructors, TestNavigationBarItemsInsideMethodsAndConstructors);

// navigationBarItemsItems2_test.go

// navigationBarItemsItems2_test.go
static void TestNavigationBarItemsItems2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->InsertLine(t, "module A");
		f->Insert(t, "export class ");
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsItems2, TestNavigationBarItemsItems2);

// navigationBarItemsItemsExternalModules2_test.go

// navigationBarItemsItemsExternalModules2_test.go
static void TestNavigationBarItemsItemsExternalModules2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: test/file.ts
export class Bar {
    public s: string;
}
export var x: number;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsItemsExternalModules2, TestNavigationBarItemsItemsExternalModules2);

// navigationBarItemsItemsExternalModules3_test.go

// navigationBarItemsItemsExternalModules3_test.go
static void TestNavigationBarItemsItemsExternalModules3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: test/my fil	e.ts
export class Bar {
    public s: string;
}
export var x: number;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsItemsExternalModules3, TestNavigationBarItemsItemsExternalModules3);

// navigationBarItemsItemsExternalModules_test.go

// navigationBarItemsItemsExternalModules_test.go
static void TestNavigationBarItemsItemsExternalModules(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(export class Bar {
    public s: string;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsItemsExternalModules, TestNavigationBarItemsItemsExternalModules);

// navigationBarItemsItemsModuleVariables_test.go

// navigationBarItemsItemsModuleVariables_test.go
static void TestNavigationBarItemsItemsModuleVariables(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: navigationItemsModuleVariables_0.ts
 /*file1*/
namespace Module1 {
    export var x = 0;
}
// @Filename: navigationItemsModuleVariables_1.ts
 /*file2*/
namespace Module1.SubModule {
    export var y = 0;
}
// @Filename: navigationItemsModuleVariables_2.ts
 /*file3*/
namespace Module1 {
    export var z = 0;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "file1");
		f->VerifyBaselineDocumentSymbol(t);
		f->GoToMarker(t, "file2");
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsItemsModuleVariables, TestNavigationBarItemsItemsModuleVariables);

// navigationBarItemsItems_test.go

// navigationBarItemsItems_test.go
static void TestNavigationBarItemsItems(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// Interface
interface IPoint {
    getDist(): number;
    new(): IPoint;
    (): any;
    [x:string]: number;
    prop: string;
}

/// Module
namespace Shapes {

    // Class
    export class Point implements IPoint {
        constructor (public x: number, public y: number) { }

        // Instance member
        getDist() { return Math.sqrt(this.x * this.x + this.y * this.y); }

        // Getter
        get value(): number { return 0; }

        // Setter
        set value(newValue: number) { return; }

        // Static member
        static origin = new Point(0, 0);

        // Static method
        private static getOrigin() { return Point.origin; }
    }

    enum Values { value1, value2, value3 }
}

// Local variables
var p: IPoint = new Shapes.Point(3, 4);
var dist = p.getDist();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsItems, TestNavigationBarItemsItems);

// navigationBarItemsMissingName1_test.go

// navigationBarItemsMissingName1_test.go
static void TestNavigationBarItemsMissingName1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(export function
class C {
    foo() {}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsMissingName1, TestNavigationBarItemsMissingName1);

// navigationBarItemsMissingName2_test.go

// navigationBarItemsMissingName2_test.go
static void TestNavigationBarItemsMissingName2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/**
 * This is a class.
 */
class /* But it has no name! */ {
    foo() {}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsMissingName2, TestNavigationBarItemsMissingName2);

// navigationBarItemsModules1_test.go

// navigationBarItemsModules1_test.go
static void TestNavigationBarItemsModules1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare module "X.Y.Z" {}

declare module 'X2.Y2.Z2' {}

declare module "foo";

namespace A.B.C {
    export var x;
}

namespace A.B {
    export var y;
}

namespace A {
    export var z;
}

namespace A {
    namespace B {
        namespace C {
            declare var x;
        }
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsModules1, TestNavigationBarItemsModules1);

// navigationBarItemsModules2_test.go

// navigationBarItemsModules2_test.go
static void TestNavigationBarItemsModules2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace Test.A { }

namespace Test.B {
    class Foo { }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsModules2, TestNavigationBarItemsModules2);

// navigationBarItemsMultilineStringIdentifiers1_test.go

// navigationBarItemsMultilineStringIdentifiers1_test.go
static void TestNavigationBarItemsMultilineStringIdentifiers1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare module "Multiline\r\nMadness" {
}

declare module "Multiline\
Madness" {
}
declare module "MultilineMadness" {}

declare module "Multiline\
Madness2" {
}

interface Foo {
    "a1\\\r\nb";
    "a2\
    \
    b"(): Foo;
}

class Bar implements Foo {
    'a1\\\r\nb': Foo;

    'a2\
    \
    b'(): Foo {
        return this;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsMultilineStringIdentifiers1, TestNavigationBarItemsMultilineStringIdentifiers1);

// navigationBarItemsMultilineStringIdentifiers2_test.go

// navigationBarItemsMultilineStringIdentifiers2_test.go
static void TestNavigationBarItemsMultilineStringIdentifiers2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((((std::string(R"TS(function f(p1: () => any, p2: string) { }
f(() => { }, )TS") + "`") + std::string(R"TS(line1\
line2\
line3)TS")) + std::string("`")) + std::string(R"TS();

class c1 {
    const a = ' ''line1\
        line2';
}

f(() => { }, )TS")) + std::string("`")) + std::string(R"TS(unterminated backtick 1
unterminated backtick 2
unterminated backtick 3)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsMultilineStringIdentifiers2, TestNavigationBarItemsMultilineStringIdentifiers2);

// navigationBarItemsMultilineStringIdentifiers3_test.go

// navigationBarItemsMultilineStringIdentifiers3_test.go
static void TestNavigationBarItemsMultilineStringIdentifiers3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare module 'MoreThanOneHundredAndFiftyCharacters\
MoreThanOneHundredAndFiftyCharacters\
MoreThanOneHundredAndFiftyCharacters\
MoreThanOneHundredAndFiftyCharacters\
MoreThanOneHundredAndFiftyCharacters\
MoreThanOneHundredAndFiftyCharacters\
MoreThanOneHundredAndFiftyCharacters' { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsMultilineStringIdentifiers3, TestNavigationBarItemsMultilineStringIdentifiers3);

// navigationBarItemsNamedArrowFunctions_test.go

// navigationBarItemsNamedArrowFunctions_test.go
static void TestNavigationBarItemsNamedArrowFunctions(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(export const value = 2;
export const func = () => 2;
export const func2 = function() { };
export function exportedFunction() { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsNamedArrowFunctions, TestNavigationBarItemsNamedArrowFunctions);

// navigationBarItemsPropertiesDefinedInConstructors_test.go

// navigationBarItemsPropertiesDefinedInConstructors_test.go
static void TestNavigationBarItemsPropertiesDefinedInConstructors(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class List<T> {
    constructor(public a: boolean, private b: T, readonly c: string, d: number) {
        var local = 0;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsPropertiesDefinedInConstructors, TestNavigationBarItemsPropertiesDefinedInConstructors);

// navigationBarItemsStaticAndNonStaticNoMerge_test.go

// navigationBarItemsStaticAndNonStaticNoMerge_test.go
static void TestNavigationBarItemsStaticAndNonStaticNoMerge(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
    static x;
    x;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsStaticAndNonStaticNoMerge, TestNavigationBarItemsStaticAndNonStaticNoMerge);

// navigationBarItemsSymbols1_test.go

// navigationBarItemsSymbols1_test.go
static void TestNavigationBarItemsSymbols1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
    [Symbol.isRegExp] = 0;
    [Symbol.iterator]() { }
    get [Symbol.isConcatSpreadable]() { }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsSymbols1, TestNavigationBarItemsSymbols1);

// navigationBarItemsSymbols2_test.go

// navigationBarItemsSymbols2_test.go
static void TestNavigationBarItemsSymbols2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
    [Symbol.isRegExp]: string;
    [Symbol.iterator](): string;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsSymbols2, TestNavigationBarItemsSymbols2);

// navigationBarItemsSymbols3_test.go

// navigationBarItemsSymbols3_test.go
static void TestNavigationBarItemsSymbols3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(enum E {
    // No nav bar entry for this
    [Symbol.isRegExp] = 0
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsSymbols3, TestNavigationBarItemsSymbols3);

// navigationBarItemsSymbols4_test.go

// navigationBarItemsSymbols4_test.go
static void TestNavigationBarItemsSymbols4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @checkJs: true
// @allowJs: true
// @target: es6
// @Filename: file.js
const _sym = Symbol("_sym");
class MyClass {
    constructor() {
        // Dynamic assignment properties can't show up in navigation,
        // as they're not syntactic members
        // Additonally, late bound members are always filtered out, besides
        this[_sym] = "ok";
    }

    method() {
        this[_sym] = "yep";
        const x = this[_sym];
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsSymbols4, TestNavigationBarItemsSymbols4);

// navigationBarItemsTypeAlias_test.go

// navigationBarItemsTypeAlias_test.go
static void TestNavigationBarItemsTypeAlias(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type T = number | string;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarItemsTypeAlias, TestNavigationBarItemsTypeAlias);

// navigationBarJsDocCommentWithNoTags_test.go

// navigationBarJsDocCommentWithNoTags_test.go
static void TestNavigationBarJsDocCommentWithNoTags(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** Test */
export const Test = {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarJsDocCommentWithNoTags, TestNavigationBarJsDocCommentWithNoTags);

// navigationBarMerging_grandchildren_test.go

// navigationBarMerging_grandchildren_test.go
static void TestNavigationBarMerging_grandchildren(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// Should not merge grandchildren with property assignments
const o = {
    a: {
        m() {},
    },
    b: {
        m() {},
    },
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarMerging_grandchildren, TestNavigationBarMerging_grandchildren);

// navigationBarMerging_test.go

// navigationBarMerging_test.go
static void TestNavigationBarMerging(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: file1.ts
namespace a {
    function foo() {}
}
namespace b {
    function foo() {}
}
namespace a {
    function bar() {}
}
// @Filename: file2.ts
namespace a {}
function a() {}
// @Filename: file3.ts
namespace a {
    interface A {
        foo: number;
    }
}
namespace a {
    interface A {
        bar: number;
    }
}
// @Filename: file4.ts
namespace A { export var x; }
namespace A.B { export var y; })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
		f->GoToFile(t, "file2.ts");
		f->VerifyBaselineDocumentSymbol(t);
		f->GoToFile(t, "file3.ts");
		f->VerifyBaselineDocumentSymbol(t);
		f->GoToFile(t, "file4.ts");
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarMerging, TestNavigationBarMerging);

// navigationBarNamespaceImportWithNoName_test.go

// navigationBarNamespaceImportWithNoName_test.go
static void TestNavigationBarNamespaceImportWithNoName(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(import *{} from 'foo';)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarNamespaceImportWithNoName, TestNavigationBarNamespaceImportWithNoName);

// navigationBarNestedObjectLiterals_test.go

// navigationBarNestedObjectLiterals_test.go
static void TestNavigationBarNestedObjectLiterals(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var a = {
    b: 0,
    c: {},
    d: {
        e: 1,
    },
    f: {
        g: 2,
        h: {
            i: 3,
        },
    },
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarNestedObjectLiterals, TestNavigationBarNestedObjectLiterals);

// navigationBarPrivateNameMethod_test.go

// navigationBarPrivateNameMethod_test.go
static void TestNavigationBarPrivateNameMethod(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class A {
  #foo() {
    class B {
      #bar() {
         function baz () {
         }
      }
    }
  }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarPrivateNameMethod, TestNavigationBarPrivateNameMethod);

// navigationBarPrivateName_test.go

// navigationBarPrivateName_test.go
static void TestNavigationBarPrivateName(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class A {
  #foo: () => {
    class B {
      #bar: () => {   
         function baz () {
         }
      }
    }
  }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarPrivateName, TestNavigationBarPrivateName);

// navigationBarPropertyDeclarations_test.go

// navigationBarPropertyDeclarations_test.go
static void TestNavigationBarPropertyDeclarations(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class A {
    public A1 = class {
        public x = 1;
        private y() {}
        protected z() {}
    }

    public A2 = {
        x: 1,
        y() {},
        z() {}
    }

    public A3 = function () {}
    public A4 = () => {}
    public A5 = 1;
    public A6 = "A6";

    public ["A7"] = class {
        public x = 1;
        private y() {}
        protected z() {}
    }

    public [1] = {
        x: 1,
        y() {},
        z() {}
    }

    public [1 + 1] = 1;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarPropertyDeclarations, TestNavigationBarPropertyDeclarations);

// navigationBarVariables_test.go

// navigationBarVariables_test.go
static void TestNavigationBarVariables(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var x = 0;
let y = 1;
const z = 2;
// @Filename: file2.ts
var {a} = 0;
let {a: b} = 0;
const [c] = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
		f->GoToFile(t, "file2.ts");
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarVariables, TestNavigationBarVariables);

// navigationBarWellKnownSymbolExpando_test.go

// navigationBarWellKnownSymbolExpando_test.go
static void TestNavigationBarWellKnownSymbolExpando(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f() {}
f[Symbol.iterator] = function() {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarWellKnownSymbolExpando, TestNavigationBarWellKnownSymbolExpando);

// navigationBarWithLocalVariables_test.go

// navigationBarWithLocalVariables_test.go
static void TestNavigationBarWithLocalVariables(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function x(){
	const x = Object()
	x.foo = ""
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationBarWithLocalVariables, TestNavigationBarWithLocalVariables);

// navigationItemsExactMatch2_test.go

// navigationItemsExactMatch2_test.go
static void TestNavigationItemsExactMatch2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(module Shapes {
    class [|Point|] {
        private [|_origin|] = 0.0;
        private [|distanceFromA|] = 0.0;

        get [|distance1|](distanceParam): number {
            var [|distanceLocal|];
            return 0;
        }
    }
}

var [|point|] = new Shapes.Point();
function [|distance2|](distanceParam1): void {
    var [|distanceLocal1|];
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWorkspaceSymbol(t, std::vector<std::shared_ptr<fourslash::VerifyWorkspaceSymbolCase>>{std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "point", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "Point", .Kind = lsproto::SymbolKindClass, .ContainerName = std::string("Shapes"), .Location = f->Ranges()[0]->LSLocation()}), std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "point", .Kind = lsproto::SymbolKindVariable, .Location = f->Ranges()[5]->LSLocation()})}), .Preferences = nullptr}), std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "distance", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "distance1", .Kind = lsproto::SymbolKindProperty, .ContainerName = std::string("Point"), .Location = f->Ranges()[3]->LSLocation()}), std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "distance2", .Kind = lsproto::SymbolKindFunction, .Location = f->Ranges()[6]->LSLocation()}), std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "distanceFromA", .Kind = lsproto::SymbolKindProperty, .ContainerName = std::string("Point"), .Location = f->Ranges()[2]->LSLocation()}), std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "distanceLocal", .Kind = lsproto::SymbolKindVariable, .ContainerName = std::string("distance1"), .Location = f->Ranges()[4]->LSLocation()}), std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "distanceLocal1", .Kind = lsproto::SymbolKindVariable, .ContainerName = std::string("distance2"), .Location = f->Ranges()[7]->LSLocation()})}), .Preferences = nullptr}), std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "origin", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "_origin", .Kind = lsproto::SymbolKindProperty, .ContainerName = std::string("Point"), .Location = f->Ranges()[1]->LSLocation()})}), .Preferences = nullptr}), std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "square", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{}), .Preferences = nullptr})});
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationItemsExactMatch2, TestNavigationItemsExactMatch2);

// navigationItemsExportEqualsExpression2_test.go

// navigationItemsExportEqualsExpression2_test.go
static void TestNavigationItemsExportEqualsExpression2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(export const foo = {
  foo: {},
};

export = {
  foo: {},
};

export = {
  foo: {},
};

type Type = typeof foo;

export = {
  foo: {},
} as Type;

export = {
  foo: {},
} satisfies Type;

export = (class {
  prop = 42;
});

export = (class Cls {
  prop = 42;
});)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentSymbol(t);
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationItemsExportEqualsExpression2, TestNavigationItemsExportEqualsExpression2);

// navigationItemsInConstructorsExactMatch_test.go

// navigationItemsInConstructorsExactMatch_test.go
static void TestNavigationItemsInConstructorsExactMatch(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noLib: true
class Test {
    private [|search1|]: number;
    constructor(public [|search2|]: boolean, readonly [|search3|]: string, search4: string) {
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWorkspaceSymbol(t, std::vector<std::shared_ptr<fourslash::VerifyWorkspaceSymbolCase>>{std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "search", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "search1", .Kind = lsproto::SymbolKindProperty, .ContainerName = std::string("Test"), .Location = f->Ranges()[0]->LSLocation()}), std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "search2", .Kind = lsproto::SymbolKindProperty, .ContainerName = std::string("Test"), .Location = f->Ranges()[1]->LSLocation()}), std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "search3", .Kind = lsproto::SymbolKindProperty, .ContainerName = std::string("Test"), .Location = f->Ranges()[2]->LSLocation()})}), .Preferences = nullptr})});
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationItemsInConstructorsExactMatch, TestNavigationItemsInConstructorsExactMatch);

// navigationItemsPrefixMatch2_test.go

// navigationItemsPrefixMatch2_test.go
static void TestNavigationItemsPrefixMatch2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
namespace Shapes {
    export class Point {
        private [|originality|] = 0.0;
        private [|distanceFromOrig|] = 0.0;
        get [|distanceFarFarAway|](distanceFarFarAwayParam: number): number {
            var [|distanceFarFarAwayLocal|];
            return 0;
        }
    }
}
var pointsSquareBox = new Shapes.Point();
function PointsFunc(): void {
 var pointFuncLocal;
}
interface [|OriginI|] {
    123;
    [|origin1|];
    public [|_distance|](distanceParam): void;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWorkspaceSymbol(t, std::vector<std::shared_ptr<fourslash::VerifyWorkspaceSymbolCase>>{std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "origin", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "origin1", .Kind = lsproto::SymbolKindProperty, .ContainerName = std::string("OriginI"), .Location = f->Ranges()[5]->LSLocation()}), std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "originality", .Kind = lsproto::SymbolKindProperty, .ContainerName = std::string("Point"), .Location = f->Ranges()[0]->LSLocation()}), std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "OriginI", .Kind = lsproto::SymbolKindInterface, .Location = f->Ranges()[4]->LSLocation()})}), .Preferences = nullptr}), std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "distance", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "distanceFarFarAway", .Kind = lsproto::SymbolKindProperty, .ContainerName = std::string("Point"), .Location = f->Ranges()[2]->LSLocation()}), std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "distanceFarFarAwayLocal", .Kind = lsproto::SymbolKindVariable, .ContainerName = std::string("distanceFarFarAway"), .Location = f->Ranges()[3]->LSLocation()}), std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "distanceFromOrig", .Kind = lsproto::SymbolKindProperty, .ContainerName = std::string("Point"), .Location = f->Ranges()[1]->LSLocation()}), std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "_distance", .Kind = lsproto::SymbolKindMethod, .ContainerName = std::string("OriginI"), .Location = f->Ranges()[6]->LSLocation()})}), .Preferences = nullptr}), std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "mPointThatIJustInitiated wrongKeyWord", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{}), .Preferences = nullptr})});
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationItemsPrefixMatch2, TestNavigationItemsPrefixMatch2);

// navigationItemsSpecialPropertyAssignment_test.go

// navigationItemsSpecialPropertyAssignment_test.go
static void TestNavigationItemsSpecialPropertyAssignment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @noLib: true
// @allowJs: true
// @Filename: /a.js
exports.[|x|] = 0;
exports.[|z|] = function() {};
function Cls() {
    this.[|instanceProp|] = 0;
}
Cls.[|staticMethod|] = function() {};
Cls.[|staticProperty|] = 0;
Cls.prototype.[|instanceMethod|] = function() {};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWorkspaceSymbol(t, std::vector<std::shared_ptr<fourslash::VerifyWorkspaceSymbolCase>>{std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "x", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "x", .Kind = lsproto::SymbolKindVariable, .Location = f->Ranges()[0]->LSLocation()})}), .Preferences = nullptr}), std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "z", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "z", .Kind = lsproto::SymbolKindVariable, .Location = f->Ranges()[1]->LSLocation()})}), .Preferences = nullptr}), std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "instanceProp", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "instanceProp", .Kind = lsproto::SymbolKindProperty, .ContainerName = std::string("Cls"), .Location = f->Ranges()[2]->LSLocation()})}), .Preferences = nullptr}), std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "staticMethod", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "staticMethod", .Kind = lsproto::SymbolKindProperty, .Location = f->Ranges()[3]->LSLocation()})}), .Preferences = nullptr}), std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "staticProperty", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "staticProperty", .Kind = lsproto::SymbolKindProperty, .Location = f->Ranges()[4]->LSLocation()})}), .Preferences = nullptr}), std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "instanceMethod", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "instanceMethod", .Kind = lsproto::SymbolKindProperty, .Location = f->Ranges()[5]->LSLocation()})}), .Preferences = nullptr})});
	});
}
REGISTER_FOURSLASH_TEST(TestNavigationItemsSpecialPropertyAssignment, TestNavigationItemsSpecialPropertyAssignment);

// navto_excludeLib2_test.go

// navto_excludeLib2_test.go
static void TestNavto_excludeLib2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: /index.ts
import { someName as [|weirdName|] } from "bar";
// @filename: /tsconfig.json
{}
// @filename: /node_modules/bar/index.d.ts
export const someName: number;
// @filename: /node_modules/bar/package.json
{})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWorkspaceSymbol(t, std::vector<std::shared_ptr<fourslash::VerifyWorkspaceSymbolCase>>{std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "weirdName", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "weirdName", .Kind = lsproto::SymbolKindVariable, .Location = f->Ranges()[0]->LSLocation()})}), .Preferences = std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.ExcludeLibrarySymbolsInNavTo = Tristate::False})})});
		f->VerifyWorkspaceSymbol(t, std::vector<std::shared_ptr<fourslash::VerifyWorkspaceSymbolCase>>{std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "weirdName", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "weirdName", .Kind = lsproto::SymbolKindVariable, .Location = f->Ranges()[0]->LSLocation()})}), .Preferences = nullptr})});
	});
}
REGISTER_FOURSLASH_TEST(TestNavto_excludeLib2, TestNavto_excludeLib2);

// navto_excludeLib3_test.go

// navto_excludeLib3_test.go
static void TestNavto_excludeLib3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: /index.ts
function [|parseInt|](s: string): number {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWorkspaceSymbol(t, std::vector<std::shared_ptr<fourslash::VerifyWorkspaceSymbolCase>>{std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "parseInt", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "parseInt", .Kind = lsproto::SymbolKindFunction, .Location = f->Ranges()[0]->LSLocation()})}), .Preferences = nullptr})});
	});
}
REGISTER_FOURSLASH_TEST(TestNavto_excludeLib3, TestNavto_excludeLib3);

// outlineSpansBlockCommentsWithoutStatements_test.go

// outlineSpansBlockCommentsWithoutStatements_test.go
static void TestOutlineSpansBlockCommentsWithoutStatements(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|/*
/ * Some text
  */|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOutliningSpans(t, {});
	});
}
REGISTER_FOURSLASH_TEST(TestOutlineSpansBlockCommentsWithoutStatements, TestOutlineSpansBlockCommentsWithoutStatements);

// outlineSpansTrailingBlockCommentsAfterStatements_test.go

// outlineSpansTrailingBlockCommentsAfterStatements_test.go
static void TestOutlineSpansTrailingBlockCommentsAfterStatements(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(console.log(0);
[|/*
/ * Some text
  */|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOutliningSpans(t, {});
	});
}
REGISTER_FOURSLASH_TEST(TestOutlineSpansTrailingBlockCommentsAfterStatements, TestOutlineSpansTrailingBlockCommentsAfterStatements);

// workspaceSymbolCurrentProject_test.go

// workspaceSymbolCurrentProject_test.go
static void TestWorkspaceSymbolCurrentProject(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @Filename: /home/projects/a/tsconfig.json
{}

// @Filename: /home/projects/a/index.ts
export function [|fromA|]() {}

// @Filename: /home/projects/b/tsconfig.json
{}

// @Filename: /home/projects/b/index.ts
export function [|fromB|]() {}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/home/projects/a/index.ts");
		auto allOpenProjects = lsutil::NewDefaultUserPreferences();
		auto currentProject = lsutil::NewDefaultUserPreferences();
		currentProject.WorkspaceSymbolsScope = lsutil::WorkspaceSymbolsScopeCurrentProject;
		f->VerifyWorkspaceSymbol(t, std::vector<std::shared_ptr<fourslash::VerifyWorkspaceSymbolCase>>{std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "from", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "fromA", .Kind = lsproto::SymbolKindFunction, .Location = f->Ranges()[0]->LSLocation()}), std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "fromB", .Kind = lsproto::SymbolKindFunction, .Location = f->Ranges()[1]->LSLocation()})}), .Preferences = std::make_shared<lsutil::UserPreferences>(allOpenProjects)}), std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "from", .Exact = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "fromA", .Kind = lsproto::SymbolKindFunction, .Location = f->Ranges()[0]->LSLocation()})}), .Preferences = std::make_shared<lsutil::UserPreferences>(currentProject)})});
	});
}
REGISTER_FOURSLASH_TEST(TestWorkspaceSymbolCurrentProject, TestWorkspaceSymbolCurrentProject);

// workspaceSymbolMultiProjectNonExistentRef_test.go

// workspaceSymbolMultiProjectNonExistentRef_test.go
static void TestWorkspaceSymbolMultiProjectNonExistentRef(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @Filename: /home/src/projects/project-a/tsconfig.json
{
  "compilerOptions": { "composite": true },
  "references": [{ "path": "../project-nonexistent" }]
}

// @Filename: /home/src/projects/project-a/index.ts
export const [|myValueA|]: number = 1;

// @Filename: /home/src/projects/project-b/tsconfig.json
{
  "compilerOptions": { "composite": true },
  "references": [{ "path": "../project-a" }]
}

// @Filename: /home/src/projects/project-b/index.ts
export const [|myValueB|]: string = "hello";
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyWorkspaceSymbol(t, std::vector<std::shared_ptr<fourslash::VerifyWorkspaceSymbolCase>>{std::make_shared<fourslash::VerifyWorkspaceSymbolCase>(fourslash::VerifyWorkspaceSymbolCase{.Pattern = "myValue", .Includes = std::make_shared<std::vector<std::shared_ptr<lsproto::SymbolInformation>>>(std::vector<std::shared_ptr<lsproto::SymbolInformation>>{std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "myValueA", .Kind = lsproto::SymbolKindVariable, .Location = f->Ranges()[0]->LSLocation()}), std::make_shared<lsproto::SymbolInformation>(lsproto::SymbolInformation{.Name = "myValueB", .Kind = lsproto::SymbolKindVariable, .Location = f->Ranges()[1]->LSLocation()})})})});
	});
}
REGISTER_FOURSLASH_TEST(TestWorkspaceSymbolMultiProjectNonExistentRef, TestWorkspaceSymbolMultiProjectNonExistentRef);

} // namespace
