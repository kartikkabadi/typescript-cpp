// Ported fourslash tests -- batch B (inlayhints). One static void TestX(gostd::testing::T*)
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

// inlayHintsCrash1_test.go
static void TestInlayHintsCrash1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: foo.js
/**
 * @param {function(string): boolean} f
 */
function doThing(f) {
    f(100)
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayVariableTypeHints = Tristate::True, .IncludeInlayParameterNameHints = lsutil::IncludeInlayParameterNameHintsAll}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsCrash1, TestInlayHintsCrash1);

// inlayHintsElementAccess_test.go
static void TestInlayHintsElementAccess(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(interface MySymbol {
	readonly "my dispose": unique symbol
}

declare var mySymbol: MySymbol;

let foo = {
	[mySymbol["my dispose"]]: () => {}
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{
		.IncludeInlayVariableTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsElementAccess, TestInlayHintsElementAccess);

// inlayHintsEnumMemberValue_test.go
static void TestInlayHintsEnumMemberValue(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(enum E {
    A,
    AA,
    B = 10,
    BB,
    C = 'C',
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayEnumMemberValueHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsEnumMemberValue, TestInlayHintsEnumMemberValue);

// inlayHintsFunctionParameterTypes1_test.go
static void TestInlayHintsFunctionParameterTypes1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(type F1 = (a: string, b: number) => void
const f1: F1 = (a, b) => { }
const f2: F1 = (a, b: number) => { }
function foo1 (cb: (a: string) => void) {}
foo1((a) => { })
function foo2 (cb: (a: Exclude<1 | 2 | 3, 1>) => void) {}
foo2((a) => { })
function foo3 (a: (b: (c: (d: Exclude<1 | 2 | 3, 1>) => void) => void) => void) {}
foo3(a => {
    a(d => {})
})
function foo4<T>(v: T, a: (v: T) => void) {}
foo4(1, a => { })
type F2 = (a: {
    a: number
    b: string
}) => void
const foo5: F2 = (a) => { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayFunctionParameterTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsFunctionParameterTypes1, TestInlayHintsFunctionParameterTypes1);

// inlayHintsFunctionParameterTypes2_test.go
static void TestInlayHintsFunctionParameterTypes2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(class C {}
namespace N { export class Foo {} }
interface Foo {}
function f1(a = 1) {}
function f2(a = "a") {}
function f3(a = true) {}
function f4(a = { } as Foo) {}
function f5(a = <Foo>{}) {}
function f6(a = {} as const) {}
function f7(a = (({} as const))) {}
function f8(a = new C()) {}
function f9(a = new N.C()) {}
function f10(a = ((((new C()))))) {}
function f11(a = { a: 1, b: 1 }) {}
function f12(a = ((({ a: 1, b: 1 })))) {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayFunctionParameterTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsFunctionParameterTypes2, TestInlayHintsFunctionParameterTypes2);

// inlayHintsFunctionParameterTypes3_test.go
static void TestInlayHintsFunctionParameterTypes3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(interface IFoo {
    bar(x?: boolean): void;
}

const a: IFoo = {
    bar: function (x?): void {
        throw new Error("Function not implemented.");
    }
}
class Foo {
    #value = 0;
    get foo(): number { return this.#value; }
    set foo(value) { this.#value = value; }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayFunctionParameterTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsFunctionParameterTypes3, TestInlayHintsFunctionParameterTypes3);

// inlayHintsFunctionParameterTypes4_test.go
static void TestInlayHintsFunctionParameterTypes4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: /a.js
class Foo {
    #value = 0;
    get foo() { return this.#value; }
    /**
     * @param {number} value
     */
    set foo(value) { this.#value = value; }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayFunctionParameterTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsFunctionParameterTypes4, TestInlayHintsFunctionParameterTypes4);

// inlayHintsFunctionParameterTypes5_test.go
static void TestInlayHintsFunctionParameterTypes5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(declare const STATE_SIGNAL: unique symbol;

declare function test(
  cb: (state: { [STATE_SIGNAL]: unknown }) => void,
): unknown;

test((state) => {});)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayFunctionParameterTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsFunctionParameterTypes5, TestInlayHintsFunctionParameterTypes5);

// inlayHintsIdentifierLocation_test.go
static void TestInlayHintsIdentifierLocation(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(interface Foo {}
const p = (a: Foo[]) => a;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayVariableTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsIdentifierLocation, TestInlayHintsIdentifierLocation);

// inlayHintsImportType1_test.go
static void TestInlayHintsImportType1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: /a.js
module.exports.a = 1
// @Filename: /b.js
const a = require('./a');)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.js");
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayVariableTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsImportType1, TestInlayHintsImportType1);

// inlayHintsImportType2_test.go
static void TestInlayHintsImportType2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: /a.js
module.exports.a = 1
// @Filename: /b.js
function foo () { return require('./a'); }
function bar () { return require('./a').a; }
const c = foo()
const d = bar())TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.js");
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayVariableTypeHints = Tristate::True, .IncludeInlayFunctionLikeReturnTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsImportType2, TestInlayHintsImportType2);

// inlayHintsInferredTypePredicate1_test.go
static void TestInlayHintsInferredTypePredicate1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @strict: true
function test(x: unknown) {
  return typeof x === 'number';
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayFunctionLikeReturnTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsInferredTypePredicate1, TestInlayHintsInferredTypePredicate1);

// inlayHintsInteractiveAnyParameter1_test.go
static void TestInlayHintsInteractiveAnyParameter1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function foo (v: any) {}
foo(1);
foo('');
foo(true);
foo(foo);
foo((1));
foo(foo(1));)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayParameterNameHints = lsutil::IncludeInlayParameterNameHintsLiterals}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsInteractiveAnyParameter1, TestInlayHintsInteractiveAnyParameter1);

// inlayHintsInteractiveAnyParameter2_test.go
static void TestInlayHintsInteractiveAnyParameter2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function foo (v: any) {}
foo(1);
foo('');
foo(true);
foo(() => 1);
foo(function () { return 1 });
foo({});
foo({ a: 1 });
foo([]);
foo([1]);
foo(foo);
foo((1));
foo(foo(1));)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayParameterNameHints = lsutil::IncludeInlayParameterNameHintsAll}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsInteractiveAnyParameter2, TestInlayHintsInteractiveAnyParameter2);

// inlayHintsInteractiveFunctionParameterTypes1_test.go
static void TestInlayHintsInteractiveFunctionParameterTypes1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS( type F1 = (a: string, b: number) => void
 const f1: F1 = (a, b) => { }
 const f2: F1 = (a, b: number) => { }
 function foo1 (cb: (a: string) => void) {}
 foo1((a) => { })
 function foo2 (cb: (a: Exclude<1 | 2 | 3, 1>) => void) {}
 foo2((a) => { })
 function foo3 (a: (b: (c: (d: Exclude<1 | 2 | 3, 1>) => void) => void) => void) {}
 foo3(a => {
     a(d => {})
 })
 function foo4<T>(v: T, a: (v: T) => void) {}
 foo4(1, a => { })
 type F2 = (a: {
     a: number
     b: string
     readonly c: boolean
     d?: number
     e(): string
     f?(): boolean
     g<T>(): T
     h?<X, Y>(x: X): Y
     <X, Y>(x: X): Y
     [i: string]: number
 }) => void
 const foo5: F2 = (a) => { }
 type F3 = (a: {
     (): 42
 }) => void
 const foo6: F3 = (a) => { }
interface Thing {}
function foo4(callback: (thing: Thing) => void) {}
foo4(p => {})
 type F4 = (a: {
     [i in string]: number
 }) => void
 const foo5: F4 = (a) => { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayFunctionParameterTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsInteractiveFunctionParameterTypes1, TestInlayHintsInteractiveFunctionParameterTypes1);

// inlayHintsInteractiveFunctionParameterTypes2_test.go
static void TestInlayHintsInteractiveFunctionParameterTypes2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(class C {}
namespace N { export class Foo {} }
interface Foo {}
function f1(a = 1) {}
function f2(a = "a") {}
function f3(a = true) {}
function f4(a = { } as Foo) {}
function f5(a = <Foo>{}) {}
function f6(a = {} as const) {}
function f7(a = (({} as const))) {}
function f8(a = new C()) {}
function f9(a = new N.C()) {}
function f10(a = ((((new C()))))) {}
function f11(a = { a: 1, b: 1 }) {}
function f12(a = ((({ a: 1, b: 1 })))) {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayFunctionParameterTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsInteractiveFunctionParameterTypes2, TestInlayHintsInteractiveFunctionParameterTypes2);

// inlayHintsInteractiveFunctionParameterTypes3_test.go
static void TestInlayHintsInteractiveFunctionParameterTypes3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(interface IFoo {
    bar(x?: boolean): void;
}

const a: IFoo = {
    bar: function (x?): void {
        throw new Error("Function not implemented.");
    }
}
class Foo {
    #value = 0;
    get foo(): number { return this.#value; }
    set foo(value) { this.#value = value; }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayFunctionParameterTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsInteractiveFunctionParameterTypes3, TestInlayHintsInteractiveFunctionParameterTypes3);

// inlayHintsInteractiveFunctionParameterTypes4_test.go
static void TestInlayHintsInteractiveFunctionParameterTypes4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: /a.js
class Foo {
    #value = 0;
    get foo() { return this.#value; }
    /**
     * @param {number} value
     */
    set foo(value) { this.#value = value; }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayFunctionParameterTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsInteractiveFunctionParameterTypes4, TestInlayHintsInteractiveFunctionParameterTypes4);

// inlayHintsInteractiveFunctionParameterTypes5_test.go
static void TestInlayHintsInteractiveFunctionParameterTypes5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const foo: 1n = 1n;
export function fn(b = foo) {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayFunctionParameterTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsInteractiveFunctionParameterTypes5, TestInlayHintsInteractiveFunctionParameterTypes5);

// inlayHintsInteractiveImportType1_test.go
static void TestInlayHintsInteractiveImportType1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: /a.js
module.exports.a = 1
// @Filename: /b.js
const a = require('./a');)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.js");
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayVariableTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsInteractiveImportType1, TestInlayHintsInteractiveImportType1);

// inlayHintsInteractiveImportType2_test.go
static void TestInlayHintsInteractiveImportType2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: /a.js
module.exports.a = 1
// @Filename: /b.js
function foo () { return require('./a'); }
function bar () { return require('./a').a; }
const c = foo()
const d = bar())TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.js");
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayVariableTypeHints = Tristate::True, .IncludeInlayFunctionLikeReturnTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsInteractiveImportType2, TestInlayHintsInteractiveImportType2);

// inlayHintsInteractiveInferredTypePredicate1_test.go
static void TestInlayHintsInteractiveInferredTypePredicate1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @strict: true
function test(x: unknown) {
  return typeof x === 'number';
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayFunctionLikeReturnTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsInteractiveInferredTypePredicate1, TestInlayHintsInteractiveInferredTypePredicate1);

// inlayHintsInteractiveJsDocParameterNames_test.go
static void TestInlayHintsInteractiveJsDocParameterNames(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: /a.js
var x
x.foo(1, 2);
/**
 * @type {{foo: (a: number, b: number) => void}}
 */
var y
y.foo(1, 2)
/**
 * @type {string}
 */
var z = "")TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/a.js");
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayParameterNameHints = lsutil::IncludeInlayParameterNameHintsLiterals}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsInteractiveJsDocParameterNames, TestInlayHintsInteractiveJsDocParameterNames);

// inlayHintsInteractiveMultifile1_test.go
static void TestInlayHintsInteractiveMultifile1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @lib: es5
// @Filename: /a.ts
export interface Foo { a: string }
// @Filename: /b.ts
async function foo () {
    return {} as any as import('./a').Foo
}
function bar () { return import('./a') }
async function main () {
    const a = await foo()
    const b = await bar()
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.ts");
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayVariableTypeHints = Tristate::True, .IncludeInlayFunctionLikeReturnTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsInteractiveMultifile1, TestInlayHintsInteractiveMultifile1);

// inlayHintsInteractiveMultifileFunctionCalls_test.go
static void TestInlayHintsInteractiveMultifileFunctionCalls(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Target: esnext
// @module: node18
// @Filename: aaa.mts
import { helperB } from "./bbb.mjs";
helperB("hello, world!");
// @Filename: bbb.mts
import { helperC } from "./ccc.mjs";
export function helperB(bParam: string) {
    helperC(bParam);
}
// @Filename: ccc.mts
export function helperC(cParam: string) {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "./aaa.mts");
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayParameterNameHints = lsutil::IncludeInlayParameterNameHintsAll}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsInteractiveMultifileFunctionCalls, TestInlayHintsInteractiveMultifileFunctionCalls);

// inlayHintsInteractiveOverloadCall_test.go
static void TestInlayHintsInteractiveOverloadCall(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(interface Call {
    (a: number): void
    (b: number, c: number): void
    new (d: number): Call
}
declare const call: Call;
call(1);
call(1, 2);
new call(1);
declare function foo(w: number): void
declare function foo(a: number, b: number): void;
declare function foo(a: number | undefined, b: number | undefined): void;
foo(1)
foo(1, 2)
class Class {
    constructor(a: number);
    constructor(b: number, c: number);
    constructor(b: number, c?: number) { }
}
new Class(1)
new Class(1, 2))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayParameterNameHints = lsutil::IncludeInlayParameterNameHintsLiterals}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsInteractiveOverloadCall, TestInlayHintsInteractiveOverloadCall);

// inlayHintsInteractiveParameterNamesInSpan1_test.go
static void TestInlayHintsInteractiveParameterNamesInSpan1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function foo1 (a: number, b: number) {}
function foo2 (c: number, d: number) {}
function foo3 (e: number, f: number) {}
function foo4 (g: number, h: number) {}
function foo5 (i: number, j: number) {}
function foo6 (k: number, i: number) {}

function c1 () { foo1(/*a*/1, /*b*/2); }
function c2 () { foo2(/*c*/1, /*d*/2); }
function c3 () { foo3(/*e*/1, /*f*/2); }
function c4 () { foo4(/*g*/1, /*h*/2); }
function c5 () { foo5(/*i*/1, /*j*/2); }
function c6 () { foo6(/*k*/1, /*l*/2); })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto start = f->MarkerByName(t, "c");
		auto end = f->MarkerByName(t, "h");
		auto span = std::make_shared<lsproto::Range>(lsproto::Range{.Start = start->LSPosition, .End = end->LSPosition});
		f->VerifyBaselineInlayHints(t, span, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
		.InlayHints = lsutil::InlayHintsPreferences{
			.IncludeInlayParameterNameHints = "literals"}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsInteractiveParameterNamesInSpan1, TestInlayHintsInteractiveParameterNamesInSpan1);

// inlayHintsInteractiveParameterNamesInSpan2_test.go
static void TestInlayHintsInteractiveParameterNamesInSpan2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function foo1 (a: number, b: number) {}
function foo2 (c: number, d: number) {}
function foo3 (e: number, f: number) {}
function foo4 (g: number, h: number) {}
function foo5 (i: number, j: number) {}
function foo6 (k: number, l: number) {}

foo1(/*a*/1, /*b*/2);
foo2(/*c*/1, /*d*/2);
foo3(/*e*/1, /*f*/2);
foo4(/*g*/1, /*h*/2);
foo5(/*i*/1, /*j*/2);
foo6(/*k*/1, /*l*/2);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		auto start = f->MarkerByName(t, "c");
		auto end = f->MarkerByName(t, "h");
		auto span = std::make_shared<lsproto::Range>(lsproto::Range{.Start = start->LSPosition, .End = end->LSPosition});
		f->VerifyBaselineInlayHints(t, span, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
		.InlayHints = lsutil::InlayHintsPreferences{
			.IncludeInlayParameterNameHints = "literals"}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsInteractiveParameterNamesInSpan2, TestInlayHintsInteractiveParameterNamesInSpan2);

// inlayHintsInteractiveParameterNamesWithComments_test.go
static void TestInlayHintsInteractiveParameterNamesWithComments(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const fn = (x: any) => { }
fn(/* nobody knows exactly what this param is */ 42);
function foo (aParameter: number, bParameter: number, cParameter: number) { }
foo(
    /** aParameter */
    1,
    // bParameter
    2,
    /* cParameter */
    3
)
foo(
    /** multiple comments */
    /** aParameter */
    1,
    /** bParameter */
    /** multiple comments */
    2,
    // cParameter
    /** multiple comments */
    3
)
foo(
    /** wrong name */
    1,
    2,
    /** multiple */
    /** wrong */
    /** name */
    3
))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayParameterNameHints = lsutil::IncludeInlayParameterNameHintsLiterals}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsInteractiveParameterNamesWithComments, TestInlayHintsInteractiveParameterNamesWithComments);

// inlayHintsInteractiveParameterNames_test.go
static void TestInlayHintsInteractiveParameterNames(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS( function foo1 (a: number, b: number) {}
 foo1(1, 2);
 function foo2 (a: number, { c }: any) {}
 foo2(1, { c: 1 });
const foo3 = (a = 1) => class { }
const C1 = class extends foo3(1) { }
class C2 extends foo3(1) { }
function foo4(a: number, b: number, c: number, d: number) {}
foo4(1, +1, -1, +"1");
function foo5(
    a: string,
    b: undefined,
    c: null,
    d: boolean,
    e: boolean,
    f: number,
    g: number,
    h: number,
    i: RegExp,
    j: bigint,
) {
}
foo5(
    "hello",
    undefined,
    null,
    true,
    false,
    Infinity,
    -Infinity,
    NaN,
    /hello/g,
    123n,
);
 declare const unknownCall: any;
 unknownCall();
function trace(message: string) {}
trace()TS" +std::string("`") +std::string(R"TS(${1})TS") +std::string("`") +std::string(R"TS();
trace()TS") +std::string("`") +std::string(R"TS()TS") +std::string("`") +std::string(R"TS();)TS");
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayParameterNameHints = lsutil::IncludeInlayParameterNameHintsLiterals}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsInteractiveParameterNames, TestInlayHintsInteractiveParameterNames);

// inlayHintsInteractiveRestParameters1_test.go
static void TestInlayHintsInteractiveRestParameters1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function foo1(a: number, ...b: number[]) {}
foo1(1, 1, 1, 1);
type Args2 = [a: number, b: number]
declare function foo2(c: number, ...args: Args2);
foo2(1, 2, 3)
type Args3 = [number, number]
declare function foo3(c: number, ...args: Args3);
foo3(1, 2, 3))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayParameterNameHints = lsutil::IncludeInlayParameterNameHintsLiterals}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsInteractiveRestParameters1, TestInlayHintsInteractiveRestParameters1);

// inlayHintsInteractiveRestParameters2_test.go
static void TestInlayHintsInteractiveRestParameters2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function foo(a: unknown, b: unknown, c: unknown) { }
function foo1(...x: [number, number | undefined]) {
    foo(...x, 3);
}
function foo2(...x: []) {
    foo(...x, 1, 2, 3);
}
function foo3(...x: [number, number?]) {
    foo(1, ...x);
}
function foo4(...x: [number, number?]) {
    foo(...x, 3);
}
function foo5(...x: [number, number]) {
    foo(...x, 3);
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayParameterNameHints = lsutil::IncludeInlayParameterNameHintsAll}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsInteractiveRestParameters2, TestInlayHintsInteractiveRestParameters2);

// inlayHintsInteractiveRestParameters3_test.go
static void TestInlayHintsInteractiveRestParameters3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function fn(x: number, y: number, a: number, b: number) {
    return x + y + a + b;
}
const foo: [x: number, y: number] = [1, 2];
fn(...foo, 3, 4);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayParameterNameHints = lsutil::IncludeInlayParameterNameHintsAll}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsInteractiveRestParameters3, TestInlayHintsInteractiveRestParameters3);

// inlayHintsInteractiveReturnType_test.go
static void TestInlayHintsInteractiveReturnType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function foo1 () {
    return 1
}
function foo2 (): number {
    return 1
}
class C {
    foo() {
        return 1
    }
    bar() {
        return this
    }
}
const a = () => 1
const b = function () { return 1 }
const c = (b) => 1
const d = b => 1)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayFunctionLikeReturnTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsInteractiveReturnType, TestInlayHintsInteractiveReturnType);

// inlayHintsInteractiveTemplateLiteralTypes_test.go
static void TestInlayHintsInteractiveTemplateLiteralTypes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(declare function getTemplateLiteral1(): )TS" +std::string("`") +std::string(R"TS(${string},${string})TS") +std::string("`") +std::string(R"TS(;
const lit1 = getTemplateLiteral1();
declare function getTemplateLiteral2(): )TS") +std::string("`") +std::string(R"TS(\${${string},${string})TS") +std::string("`") +std::string(R"TS(;
const lit2 = getTemplateLiteral2();
declare function getTemplateLiteral3(): )TS") +std::string("`") +std::string(R"TS(start${string}\${,$${string}end)TS") +std::string("`") +std::string(R"TS(;
const lit3 = getTemplateLiteral3();
declare function getTemplateLiteral4(): )TS") +std::string("`") +std::string(R"TS(${string}\)TS") +std::string("`") +std::string(R"TS(,${string})TS") +std::string("`") +std::string(R"TS(;
const lit4 = getTemplateLiteral4();)TS");
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayVariableTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsInteractiveTemplateLiteralTypes, TestInlayHintsInteractiveTemplateLiteralTypes);

// inlayHintsInteractiveVariableTypes1_test.go
static void TestInlayHintsInteractiveVariableTypes1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(class C {}
namespace N { export class Foo {} }
interface Foo {}
const a = "a";
const b = 1;
const c = true;
const d = {} as Foo;
const e = <Foo>{};
const f = {} as const;
const g = (({} as const));
const h = new C();
const i = new N.C();
const j = ((((new C()))));
const k = { a: 1, b: 1 };
const l = ((({ a: 1, b: 1 })));
 const m = () => 123;
 const n;
 const o = () => -1 as const;
 const p = ([a]: Foo[]) => a;
 const q = ({ a }: { a: Foo }) => a;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayVariableTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsInteractiveVariableTypes1, TestInlayHintsInteractiveVariableTypes1);

// inlayHintsInteractiveVariableTypes2_test.go
static void TestInlayHintsInteractiveVariableTypes2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const object = { foo: 1, bar: 2 }
const array = [1, 2]
const a = object;
const { foo, bar } = object;
const {} = object;
const b = array;
const [ first, second ] = array;
const [] = array;
declare function foo<T extends number>(t: T): T
const x = foo(1))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayVariableTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsInteractiveVariableTypes2, TestInlayHintsInteractiveVariableTypes2);

// inlayHintsInteractiveWithClosures_test.go
static void TestInlayHintsInteractiveWithClosures(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function foo1(a: number) {
    return (b: number) => {
        return a + b
    }
}
foo1(1)(2);
function foo2(a: (b: number) => number) {
    return a(1) + 2
}
foo2((c: number) => c + 1);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayParameterNameHints = lsutil::IncludeInlayParameterNameHintsAll}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsInteractiveWithClosures, TestInlayHintsInteractiveWithClosures);

// inlayHintsJsDocParameterNames_test.go
static void TestInlayHintsJsDocParameterNames(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: /a.js
var x
x.foo(1, 2);
/**
 * @type {{foo: (a: number, b: number) => void}}
 */
var y
y.foo(1, 2)
/**
 * @type {string}
 */
var z = "")TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/a.js");
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayParameterNameHints = lsutil::IncludeInlayParameterNameHintsLiterals}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsJsDocParameterNames, TestInlayHintsJsDocParameterNames);

// inlayHintsMultifile1_test.go
static void TestInlayHintsMultifile1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /a.ts
export interface Foo { a: string }
// @Filename: /b.ts
async function foo () {
    return {} as any as import('./a').Foo
}
function bar () { return import('./a') }
async function main () {
    const a = await foo()
    const b = await bar()
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.ts");
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayVariableTypeHints = Tristate::True, .IncludeInlayFunctionLikeReturnTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsMultifile1, TestInlayHintsMultifile1);

// inlayHintsNoHintWhenArgumentMatchesName_test.go
static void TestInlayHintsNoHintWhenArgumentMatchesName(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function foo (a: number, b: number) {}
declare const a: 1;
foo(a, 2);
declare const v: any;
foo(v.a, v.a);
foo(v.b, v.b);
foo(v.c, v.c);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayParameterNameHints = lsutil::IncludeInlayParameterNameHintsAll, .IncludeInlayParameterNameHintsWhenArgumentMatchesName = Tristate::False}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsNoHintWhenArgumentMatchesName, TestInlayHintsNoHintWhenArgumentMatchesName);

// inlayHintsNoParameterHints_test.go
static void TestInlayHintsNoParameterHints(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function foo (a: number, b: number) {}
foo(1, 2);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayParameterNameHints = lsutil::IncludeInlayParameterNameHintsNone}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsNoParameterHints, TestInlayHintsNoParameterHints);

// inlayHintsNoVariableTypeHints_test.go
static void TestInlayHintsNoVariableTypeHints(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const a = 123;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayVariableTypeHints = Tristate::False}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsNoVariableTypeHints, TestInlayHintsNoVariableTypeHints);

// inlayHintsOverloadCall1_test.go
static void TestInlayHintsOverloadCall1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(interface Call {
    (a: number): void
    (b: number, c: number): void
    new (d: number): Call
}
declare const call: Call;
call(1);
call(1, 2);
new call(1);
declare function foo(w: number): void
declare function foo(a: number, b: number): void;
declare function foo(a: number | undefined, b: number | undefined): void;
foo(1)
foo(1, 2)
class Class {
    constructor(a: number);
    constructor(b: number, c: number);
    constructor(b: number, c?: number) { }
}
new Class(1)
new Class(1, 2))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayParameterNameHints = lsutil::IncludeInlayParameterNameHintsLiterals}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsOverloadCall1, TestInlayHintsOverloadCall1);

// inlayHintsOverloadCall2_test.go
static void TestInlayHintsOverloadCall2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(type HasID = {
    id: number;
}

type Numbers = {
    n: number[];
}

declare function func(bad1: number, bad2: HasID): void;
declare function func(ok_1: Numbers, ok_2: HasID): void;

func(
    { n: [1, 2, 3] },
    {
        id: 1,
    },
);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayParameterNameHints = lsutil::IncludeInlayParameterNameHintsAll}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsOverloadCall2, TestInlayHintsOverloadCall2);

// inlayHintsParameterNames_test.go
static void TestInlayHintsParameterNames(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS( function foo1 (a: number, b: number) {}
 foo1(1, 2);
 function foo2 (a: number, { c }: any) {}
 foo2(1, { c: 1 });
function foo3(a: any, b: number) {}
foo3({}, 1);
const foo3 = (a = 1) => class { }
const C1 = class extends foo3(1) { }
class C2 extends foo3(1) { }
function foo4(a: number, b: number, c: number, d: number) {}
foo4(1, +1, -1, +"1");
function foo5(
    a: string,
    b: undefined,
    c: null,
    d: boolean,
    e: boolean,
    f: number,
    g: number,
    h: number,
    i: RegExp,
    j: bigint,
) {
}
foo5(
    "hello",
    undefined,
    null,
    true,
    false,
    Infinity,
    -Infinity,
    NaN,
    /hello/g,
    123n,
);
 declare const unknownCall: any;
 unknownCall();
function trace(message: string) {}
trace()TS" +std::string("`") +std::string(R"TS(${1})TS") +std::string("`") +std::string(R"TS();
trace()TS") +std::string("`") +std::string(R"TS()TS") +std::string("`") +std::string(R"TS();
function func(
    param1: number,
    param2: string,
    param3: boolean,
) {}
const param1 = 1;
func(
    param1,
    'foo',
    true,
))TS");
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayParameterNameHints = lsutil::IncludeInlayParameterNameHintsLiterals}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsParameterNames, TestInlayHintsParameterNames);

// inlayHintsPropertyDeclarationComputedName1_test.go
static void TestInlayHintsPropertyDeclarationComputedName1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function foo() {
  const sym = Symbol();
  class C {
    [sym] = 123;
  }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
		.InlayHints = lsutil::InlayHintsPreferences{
			.IncludeInlayPropertyDeclarationTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsPropertyDeclarationComputedName1, TestInlayHintsPropertyDeclarationComputedName1);

// inlayHintsPropertyDeclarations2_test.go
static void TestInlayHintsPropertyDeclarations2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @strict: true
// @target: esnext
class C {
    accessor a = 1
    accessor b: number = 2
    accessor c;
    accessor d;

    constructor(value: number) {
        this.d = value;
        if (value <= 0) {
            this.d = null;
        }
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayPropertyDeclarationTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsPropertyDeclarations2, TestInlayHintsPropertyDeclarations2);

// inlayHintsPropertyDeclarations_test.go
static void TestInlayHintsPropertyDeclarations(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @strict: true
class C {
    a = 1
    b: number = 2
    c;
    d;

    constructor(value: number) {
        this.d = value;
        if (value <= 0) {
            this.d = null;
        }
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayPropertyDeclarationTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsPropertyDeclarations, TestInlayHintsPropertyDeclarations);

// inlayHintsQuotePreference1_test.go
static void TestInlayHintsQuotePreference1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const a1: '"' = '"';
const b1: '\\' = '\\';
export function fn(a = a1, b = b1) {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.QuotePreference = lsutil::QuotePreference("double"), .InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayFunctionParameterTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsQuotePreference1, TestInlayHintsQuotePreference1);

// inlayHintsQuotePreference2_test.go
static void TestInlayHintsQuotePreference2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const a1: "'" = "'";
const b1: "\\" = "\\";
export function fn(a = a1, b = b1) {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.QuotePreference = lsutil::QuotePreference("single"), .InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayFunctionParameterTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsQuotePreference2, TestInlayHintsQuotePreference2);

// inlayHintsReparsedNodeCrash_test.go
static void TestInlayHintsReparsedNodeCrash(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(
// @allowJs: true
// @checkJs: true

// @Filename: /a.js
module.exports = function () {
  return 1;
};
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{
		.IncludeInlayFunctionLikeReturnTypeHints = Tristate::True,
		.IncludeInlayFunctionParameterTypeHints =  Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsReparsedNodeCrash, TestInlayHintsReparsedNodeCrash);

// inlayHintsRestParameters1_test.go
static void TestInlayHintsRestParameters1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function foo1(a: number, ...b: number[]) {}
foo1(1, 1, 1, 1);
type Args2 = [a: number, b: number]
declare function foo2(c: number, ...args: Args2);
foo2(1, 2, 3)
type Args3 = [number, number]
declare function foo3(c: number, ...args: Args3);
foo3(1, 2, 3))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayParameterNameHints = lsutil::IncludeInlayParameterNameHintsLiterals}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsRestParameters1, TestInlayHintsRestParameters1);

// inlayHintsRestParameters2_test.go
static void TestInlayHintsRestParameters2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function foo(a: unknown, b: unknown, c: unknown) { }
function foo1(...x: [number, number | undefined]) {
    foo(...x, 3);
}
function foo2(...x: []) {
    foo(...x, 1, 2, 3);
}
function foo3(...x: [number, number?]) {
    foo(1, ...x);
}
function foo4(...x: [number, number?]) {
    foo(...x, 3);
}
function foo5(...x: [number, number]) {
    foo(...x, 3);
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayParameterNameHints = lsutil::IncludeInlayParameterNameHintsAll}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsRestParameters2, TestInlayHintsRestParameters2);

// inlayHintsReturnType_test.go
static void TestInlayHintsReturnType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function foo1 () {
    return 1
}
function foo2 (): number {
    return 1
}
class C {
    foo() {
        return 1
    }
}
const a = () => 1
const b = function () { return 1 }
const c = (b) => 1
const d = b => 1)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayFunctionLikeReturnTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsReturnType, TestInlayHintsReturnType);

// inlayHintsThisParameter_test.go
static void TestInlayHintsThisParameter(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(interface I {
    a: number;
}

declare function fn(
    callback: (a: number, b: string) => void
): void;


fn(function (this, a, b) { });
fn(function (this: I, a, b) { });)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayFunctionParameterTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsThisParameter, TestInlayHintsThisParameter);

// inlayHintsTupleTypeCrash_test.go
static void TestInlayHintsTupleTypeCrash(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function iterateTuples(tuples: [string][]): void {
  tuples.forEach((l) => {})
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
		.InlayHints = lsutil::InlayHintsPreferences{
			.IncludeInlayFunctionParameterTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsTupleTypeCrash, TestInlayHintsTupleTypeCrash);

// inlayHintsTypeMatchesName_test.go
static void TestInlayHintsTypeMatchesName(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(type Client = {};
function getClient(): Client { return {}; };
const client = getClient();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayVariableTypeHints = Tristate::True, .IncludeInlayVariableTypeHintsWhenTypeMatchesName = Tristate::False}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsTypeMatchesName, TestInlayHintsTypeMatchesName);

// inlayHintsTypeParameterModifiers1_test.go
static void TestInlayHintsTypeParameterModifiers1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function test1() {
  return function <const T>(a: T) {};
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayFunctionLikeReturnTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsTypeParameterModifiers1, TestInlayHintsTypeParameterModifiers1);

// inlayHintsUsing_test.go
static void TestInlayHintsUsing(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @target: esnext
using _defer = {
	[Symbol.dispose]() {},
};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{
		.IncludeInlayVariableTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsUsing, TestInlayHintsUsing);

// inlayHintsVariableTypes1_test.go
static void TestInlayHintsVariableTypes1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(class C {}
namespace N { export class Foo {} }
interface Foo {}
const a = "a";
const b = 1;
const c = true;
const d = {} as Foo;
const e = <Foo>{};
const f = {} as const;
const g = (({} as const));
const h = new C();
const i = new N.C();
const j = ((((new C()))));
const k = { a: 1, b: 1 };
const l = ((({ a: 1, b: 1 })));
 const m = () => 123;
 const n;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayVariableTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsVariableTypes1, TestInlayHintsVariableTypes1);

// inlayHintsVariableTypes2_test.go
static void TestInlayHintsVariableTypes2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const object = { foo: 1, bar: 2 }
const array = [1, 2]
const a = object;
const { foo, bar } = object;
const {} = object;
const b = array;
const [ first, second ] = array;
const [] = array;
declare function foo<T extends number>(t: T): T
const x = foo(1))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayVariableTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsVariableTypes2, TestInlayHintsVariableTypes2);

// inlayHintsVariableTypes3_test.go
static void TestInlayHintsVariableTypes3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @strict: true
// @target: esnext
interface DivElement {}
declare var DivElementCtor: {
  prototype: DivElement;
  new(): DivElement;
};
interface ElementMap {
  div: typeof DivElementCtor;
}
declare function getCtor<K extends keyof ElementMap>(tagName: K): ElementMap[K] | undefined;
const div = getCtor("div");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayVariableTypeHints = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsVariableTypes3, TestInlayHintsVariableTypes3);

// inlayHintsWithClosures_test.go
static void TestInlayHintsWithClosures(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function foo1(a: number) {
    return (b: number) => {
        return a + b
    }
}
foo1(1)(2);
function foo2(a: (b: number) => number) {
    return a(1) + 2
}
foo2((c: number) => c + 1);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineInlayHints(t, nullptr , std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.InlayHints = lsutil::InlayHintsPreferences{.IncludeInlayParameterNameHints = lsutil::IncludeInlayParameterNameHintsAll}}));
	});
}
REGISTER_FOURSLASH_TEST(TestInlayHintsWithClosures, TestInlayHintsWithClosures);


}  // namespace
