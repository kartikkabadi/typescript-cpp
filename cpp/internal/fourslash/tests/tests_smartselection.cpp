// Ported fourslash tests -- batch B (smartselection). One static void TestX(gostd::testing::T*)
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

// smartSelection_JSDocTags10_test.go
static void TestSmartSelection_JSDocTags10(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(/**
 * @template T
 * @extends {/**/Set<T>}
 */
class A extends B {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_JSDocTags10, TestSmartSelection_JSDocTags10);

// smartSelection_JSDocTags11_test.go
static void TestSmartSelection_JSDocTags11(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const x = 1;
type Foo = {
  /** comment */
  /*2*/readonly /*1*/status: number;
};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_JSDocTags11, TestSmartSelection_JSDocTags11);

// smartSelection_JSDocTags12_test.go
static void TestSmartSelection_JSDocTags12(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(type B = {};
type A = {
    a(/** Comment */ /*1*/p0: number, /** Comment */ /*2*/p1: number, /** Comment */ /*3*/p2: number): string;
};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_JSDocTags12, TestSmartSelection_JSDocTags12);

// smartSelection_JSDocTags13_test.go
static void TestSmartSelection_JSDocTags13(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(let a;
let b: {
    /** Comment */ /*1*/p0: number
    /** Comment */ /*2*/p1: number
    /** Comment */ /*3*/p2: number
};
let c;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_JSDocTags13, TestSmartSelection_JSDocTags13);

// smartSelection_JSDocTags1_test.go
static void TestSmartSelection_JSDocTags1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(/**
 * @returns {Array<{ value: /**/string }>}
 */
function foo() { return [] })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_JSDocTags1, TestSmartSelection_JSDocTags1);

// smartSelection_JSDocTags2_test.go
static void TestSmartSelection_JSDocTags2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(/**
 * @type {/**/string}
 */
const foo;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_JSDocTags2, TestSmartSelection_JSDocTags2);

// smartSelection_JSDocTags3_test.go
static void TestSmartSelection_JSDocTags3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(/**
 * @param {/**/string} x
 */
function foo(x) {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_JSDocTags3, TestSmartSelection_JSDocTags3);

// smartSelection_JSDocTags4_test.go
static void TestSmartSelection_JSDocTags4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(/**
 * @typedef {object} Foo
 * @property {string} a
 * @property {number} b
 * @property {/**/number} c
 */

/** @type {Foo} */
const foo;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_JSDocTags4, TestSmartSelection_JSDocTags4);

// smartSelection_JSDocTags5_test.go
static void TestSmartSelection_JSDocTags5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(/**
 * @callback Foo
 * @param {string} data
 * @param {/**/number} [index] - comment
 * @return {boolean}
 */

/** @type {Foo} */
const foo = s => !(s.length % 2);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_JSDocTags5, TestSmartSelection_JSDocTags5);

// smartSelection_JSDocTags6_test.go
static void TestSmartSelection_JSDocTags6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(/**
 * @template T
 * @param {/**/T} x
 * @return {T}
 */
function foo(x) {
    return x;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_JSDocTags6, TestSmartSelection_JSDocTags6);

// smartSelection_JSDocTags7_test.go
static void TestSmartSelection_JSDocTags7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(/**
 * @constructor
 * @param {/**/number} data
 */
function Foo(data) {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_JSDocTags7, TestSmartSelection_JSDocTags7);

// smartSelection_JSDocTags8_test.go
static void TestSmartSelection_JSDocTags8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(/**
 * @this {/*1*/Foo}
 * @param {/*2*/*} e
 */
function callback(e) {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_JSDocTags8, TestSmartSelection_JSDocTags8);

// smartSelection_JSDocTags9_test.go
static void TestSmartSelection_JSDocTags9(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(/** @enum {/**/number} */
const Foo = {
    x: 0,
    y: 1,
};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_JSDocTags9, TestSmartSelection_JSDocTags9);

// smartSelection_JSDoc_test.go
static void TestSmartSelection_JSDoc(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// Not a JSDoc comment
/**
 * @param {number} x The number to square
 */
function /**/square(x) {
  return x * x;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_JSDoc, TestSmartSelection_JSDoc);

// smartSelection_behindCaret_test.go
static void TestSmartSelection_behindCaret(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(let/**/ x: string)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_behindCaret, TestSmartSelection_behindCaret);

// smartSelection_bindingPatterns_test.go
static void TestSmartSelection_bindingPatterns(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const { /*1*/x, y: /*2*/a, .../*3*/zs = {} } = {};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_bindingPatterns, TestSmartSelection_bindingPatterns);

// smartSelection_comment1_test.go
static void TestSmartSelection_comment1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const a = 1; ///**/comment content)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_comment1, TestSmartSelection_comment1);

// smartSelection_comment2_test.go
static void TestSmartSelection_comment2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const a = 1; //a b/**/c d)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_comment2, TestSmartSelection_comment2);

// smartSelection_complex_test.go
static void TestSmartSelection_complex(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(type X<T, P> = IsExactlyAny<P> extends true ? T : ({ [K in keyof P]: IsExactlyAny<P[K]> extends true ? K extends keyof T ? T[K] : P[/**/K] : P[K]; } & Pick<T, Exclude<keyof T, keyof P>>))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_complex, TestSmartSelection_complex);

// smartSelection_emptyRanges_test.go
static void TestSmartSelection_emptyRanges(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(class HomePage {
  componentDidMount(/*1*/) {
    if (this.props.username/*2*/) {
      return '/*3*/';
    }
  }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_emptyRanges, TestSmartSelection_emptyRanges);

// smartSelection_function1_test.go
static void TestSmartSelection_function1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const f1 = () => {
   /**/
};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_function1, TestSmartSelection_function1);

// smartSelection_function2_test.go
static void TestSmartSelection_function2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function f2() {
    /**/
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_function2, TestSmartSelection_function2);

// smartSelection_function3_test.go
static void TestSmartSelection_function3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const f3 = function () {
    /**/
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_function3, TestSmartSelection_function3);

// smartSelection_functionParams1_test.go
static void TestSmartSelection_functionParams1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function f(/*1*/p, /*2*/q?, /*3*/...r: any[] = []) {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_functionParams1, TestSmartSelection_functionParams1);

// smartSelection_functionParams2_test.go
static void TestSmartSelection_functionParams2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(function f(
  a,
  /**/b
) {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_functionParams2, TestSmartSelection_functionParams2);

// smartSelection_imports_test.go
static void TestSmartSelection_imports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import { /**/x as y, z } from './z';
import { b } from './';

console.log(1);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_imports, TestSmartSelection_imports);

// smartSelection_lastBlankLine_test.go
static void TestSmartSelection_lastBlankLine(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(class C {}
/**/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_lastBlankLine, TestSmartSelection_lastBlankLine);

// smartSelection_loneVariableDeclaration_test.go
static void TestSmartSelection_loneVariableDeclaration(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const /**/x = 3;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_loneVariableDeclaration, TestSmartSelection_loneVariableDeclaration);

// smartSelection_mappedTypes_test.go
static void TestSmartSelection_mappedTypes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(type M = { /*1*/-re/*2*/adonly /*3*/[K in ke/*4*/yof any]/*5*/-/*6*/?: any };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_mappedTypes, TestSmartSelection_mappedTypes);

// smartSelection_objectTypes_test.go
static void TestSmartSelection_objectTypes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(type X = {
  /*1*/foo?: string;
  /*2*/readonly /*3*/bar: { x: num/*4*/ber };
  /*5*/meh
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_objectTypes, TestSmartSelection_objectTypes);

// smartSelection_punctuationPriority_test.go
static void TestSmartSelection_punctuationPriority(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(console/**/.log();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_punctuationPriority, TestSmartSelection_punctuationPriority);

// smartSelection_simple1_test.go
static void TestSmartSelection_simple1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(class Foo {
  bar(a, b) {
      if (/*1*/a === b) {
          return tr/*2*/ue;
      }
      return false;
  }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_simple1, TestSmartSelection_simple1);

// smartSelection_simple2_test.go
static void TestSmartSelection_simple2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(export interface IService {
  _serviceBrand: any;

  open(ho/*1*/st: number, data: any): Promise<any>;
  bar(): void/*2*/
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_simple2, TestSmartSelection_simple2);

// smartSelection_stringLiteral_test.go
static void TestSmartSelection_stringLiteral(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const a = 'a';
const b = /**/'b';)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_stringLiteral, TestSmartSelection_stringLiteral);

// smartSelection_templateStrings2_test.go
static void TestSmartSelection_templateStrings2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS()TS" +std::string("`") +std::string(R"TS(a ${b} /**/c)TS") +std::string("`") +std::string(R"TS()TS");
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_templateStrings2, TestSmartSelection_templateStrings2);

// smartSelection_templateStrings_test.go
static void TestSmartSelection_templateStrings(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS()TS" +std::string("`") +std::string(R"TS(a /*1*/b ${
  '/*2*/c'
} d)TS") +std::string("`") +std::string(R"TS()TS");
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineSelectionRanges(t);
	});
}
REGISTER_FOURSLASH_TEST(TestSmartSelection_templateStrings, TestSmartSelection_templateStrings);


}  // namespace
