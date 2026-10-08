// Ported fourslash tests -- batch B (organizeimports). One static void TestX(gostd::testing::T*)
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

// organizeImports10_test.go
static void TestOrganizeImports10(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /module.ts
import type { ZodType } from './declaration';

/** Intended to be used in combination with {@link ZodType} */
export function fun() { /* ... */ }
// @Filename: /declaration.ts
 type ZodType = {};
 export type { ZodType })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import type { ZodType } from './declaration';

/** Intended to be used in combination with {@link ZodType} */
export function fun() { /* ... */ })TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports10, TestOrganizeImports10);

// organizeImports11_test.go
static void TestOrganizeImports11(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /test.ts
import { TypeA, TypeB, TypeC, UnreferencedType } from './my-types';

/**
 * MyClass {@link TypeA}
 */
export class MyClass {

  /**
   * Some Property {@link TypeB}
   */
  public something;

  /**
   * Some function {@link TypeC}
   */
  public myMethod() {

    /**
     * Some lambda function {@link TypeC}
     */
    const someFunction = () => {
      return '';
    }
    someFunction();
  }
}
// @Filename: /my-types.ts
 export type TypeA = string;
 export class TypeB { }
 export type TypeC = () => string;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import { TypeA, TypeB, TypeC } from './my-types';

/**
 * MyClass {@link TypeA}
 */
export class MyClass {

  /**
   * Some Property {@link TypeB}
   */
  public something;

  /**
   * Some function {@link TypeC}
   */
  public myMethod() {

    /**
     * Some lambda function {@link TypeC}
     */
    const someFunction = () => {
      return '';
    }
    someFunction();
  }
})TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports11, TestOrganizeImports11);

// organizeImports12_test.go
static void TestOrganizeImports12(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @allowJs: true
// @Filename: /test.js
declare export default class A {}
declare export { a, b };
declare export * from "foo";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(declare export default class A {}
declare export * from "foo";
declare export { a, b };
)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports12, TestOrganizeImports12);

// organizeImports13_test.go
static void TestOrganizeImports13(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import {
    Type1,
    Type2,
    func4,
    Type3,
    Type4,
    Type5,
    Type7,
    Type8,
    Type9,
    func1,
    func2,
    Type6,
    func3,
    func5,
    func6,
    func7,
    func8,
    func9,
} from "foo";
interface Use extends Type1, Type2, Type3, Type4, Type5, Type6, Type7, Type8, Type9 {}
console.log(func1, func2, func3, func4, func5, func6, func7, func8, func9);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import {
    Type1,
    Type2,
    Type3,
    Type4,
    Type5,
    Type6,
    Type7,
    Type8,
    Type9,
    func1,
    func2,
    func3,
    func4,
    func5,
    func6,
    func7,
    func8,
    func9,
} from "foo";
interface Use extends Type1, Type2, Type3, Type4, Type5, Type6, Type7, Type8, Type9 {}
console.log(func1, func2, func3, func4, func5, func6, func7, func8, func9);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
		f->VerifyOrganizeImports(t,
		R"TS(import {
    func1,
    func2,
    func3,
    func4,
    func5,
    func6,
    func7,
    func8,
    func9,
    Type1,
    Type2,
    Type3,
    Type4,
    Type5,
    Type6,
    Type7,
    Type8,
    Type9,
} from "foo";
interface Use extends Type1, Type2, Type3, Type4, Type5, Type6, Type7, Type8, Type9 {}
console.log(func1, func2, func3, func4, func5, func6, func7, func8, func9);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::True}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports13, TestOrganizeImports13);

// organizeImports14_test.go
static void TestOrganizeImports14(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @filename: /a.ts
export const foo = 1;
// @filename: /b.ts
/**
 * Module doc comment
 *
 * @module
 */

// comment 1

// comment 2

import { foo } from "./a";
import { foo } from "./a";
import { foo } from "./a";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.ts");
		f->VerifyOrganizeImports(t,
		R"TS(/**
 * Module doc comment
 *
 * @module
 */

// comment 1

// comment 2

)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports14, TestOrganizeImports14);

// organizeImports15_test.go
static void TestOrganizeImports15(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @filename: /a.ts
export const foo = 1;
// @filename: /b.ts
/**
 * Module doc comment
 *
 * @module
 */

// comment 1

// comment 2

import { foo } from "./a";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.ts");
		f->VerifyOrganizeImports(t,
		R"TS(/**
 * Module doc comment
 *
 * @module
 */

// comment 1

// comment 2

)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports15, TestOrganizeImports15);

// organizeImports16_test.go
static void TestOrganizeImports16(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import { a, A, b } from "foo";
interface Use extends A {}
console.log(a, b);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import { a, A, b } from "foo";
interface Use extends A {}
console.log(a, b);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
		f->ReplaceLine(t, 0, "import { a, A, b } from \"foo1\";");
		f->VerifyOrganizeImports(t,
		R"TS(import { a, A, b } from "foo1";
interface Use extends A {}
console.log(a, b);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::Unknown}));
		f->ReplaceLine(t, 0, "import { a, A, b } from \"foo2\";");
		f->VerifyOrganizeImports(t,
		R"TS(import { a, A, b } from "foo2";
interface Use extends A {}
console.log(a, b);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::True}));
		f->ReplaceLine(t, 0, "import { a, A, b } from \"foo3\";");
		f->VerifyOrganizeImports(t,
		R"TS(import { A, a, b } from "foo3";
interface Use extends A {}
console.log(a, b);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::False}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports16, TestOrganizeImports16);

// organizeImports17_test.go
static void TestOrganizeImports17(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import { Both } from "module-specifiers-unsorted";
import { aa, CaseInsensitively, sorted } from "aardvark";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import { aa, CaseInsensitively, sorted } from "aardvark";
import { Both } from "module-specifiers-unsorted";
)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::Unknown}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports17, TestOrganizeImports17);

// organizeImports18_test.go
static void TestOrganizeImports18(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @filename: /A.ts
export interface A {}
export function bFuncA(a: A) {}
// @filename: /B.ts
export interface B {}
export function bFuncB(b: B) {}
// @filename: /C.ts
export interface C {}
export function bFuncC(c: C) {}
// @filename: /test.ts
export { C } from "./C";
export { B } from "./B";
export { A } from "./A";

export { bFuncC } from "./C";
export { bFuncB } from "./B";
export { bFuncA } from "./A";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/test.ts");
		f->VerifyOrganizeImports(t,
		R"TS(export { A } from "./A";
export { B } from "./B";
export { C } from "./C";

export { bFuncA } from "./A";
export { bFuncB } from "./B";
export { bFuncC } from "./C";
)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports18, TestOrganizeImports18);

// organizeImports19_test.go
static void TestOrganizeImports19(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const a = 1;
export { a };

const b = 1;
export { b };

const c = 1;
export { c };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(const a = 1;
export { a };

const b = 1;
export { b };

const c = 1;
export { c };
)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports19, TestOrganizeImports19);

// organizeImports1_test.go
static void TestOrganizeImports1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import {
    d, d as D,
    c,
    c as C, b,
    b as B, a
} from './foo';
import {
    h, h as H,
    g,
    g as G, f,
    f as F, e
} from './foo';

console.log(a, B, b, c, C, d, D);
console.log(e, f, F, g, G, H, h);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImportsWithRequestKind(t,
		R"TS(import {
    a,
    b,
    b as B,
    c,
    c as C,
    d, d as D,
    e,
    f,
    f as F,
    g,
    g as G,
    h, h as H
} from './foo';

console.log(a, B, b, c, C, d, D);
console.log(e, f, F, g, G, H, h);)TS",
		lsproto::CodeActionKindSourceOrganizeImports,
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::True}));
		f->VerifyOrganizeImports(t,
		R"TS(import {
    b as B,
    c as C,
    d as D,
    f as F,
    g as G,
    h as H,
    a,
    b,
    c,
    d,
    e,
    f,
    g,
    h
} from './foo';

console.log(a, B, b, c, C, d, D);
console.log(e, f, F, g, G, H, h);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::False}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports1, TestOrganizeImports1);

// organizeImports20_test.go
static void TestOrganizeImports20(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const a = 1;
const b = 1;
export { a };
export { b };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(const a = 1;
const b = 1;
export { a, b };
)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports20, TestOrganizeImports20);

// organizeImports21_test.go
static void TestOrganizeImports21(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @filename: /a.ts
export interface LocationDefinitions {}
export interface PersonDefinitions {}
// @filename: /b.ts
export {
    /** @deprecated Use LocationDefinitions instead */
    LocationDefinitions as AddressDefinitions,
    LocationDefinitions,
    /** @deprecated Use PersonDefinitions instead */
    PersonDefinitions as NameDefinitions,
    PersonDefinitions,
} from './a';)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.ts");
		f->VerifyOrganizeImports(t,
		R"TS(export {
    /** @deprecated Use LocationDefinitions instead */
    LocationDefinitions as AddressDefinitions,
    LocationDefinitions,
    /** @deprecated Use PersonDefinitions instead */
    PersonDefinitions as NameDefinitions,
    PersonDefinitions
} from './a';
)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports21, TestOrganizeImports21);

// organizeImports22_test.go
static void TestOrganizeImports22(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import {abc, Abc, bc, Bc} from 'b';
import {
  I,
  R,
  M,
} from 'a';
console.log(abc, Abc, bc, Bc, I, R, M);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import {
    I,
    M,
    R,
} from 'a';
import { abc, Abc, bc, Bc } from 'b';
console.log(abc, Abc, bc, Bc, I, R, M);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
		f->VerifyOrganizeImports(t,
		R"TS(import {
    I,
    M,
    R,
} from 'a';
import { abc, Abc, bc, Bc } from 'b';
console.log(abc, Abc, bc, Bc, I, R, M);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports22, TestOrganizeImports22);

// organizeImports23_test.go
static void TestOrganizeImports23(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import {abc, Abc, type bc, type Bc} from 'b';
import {
  I,
  R,
  M,
} from 'a';
type x = bc | Bc;
console.log(abc, Abc, I, R, M);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import {
    I,
    M,
    R,
} from 'a';
import { abc, Abc, type bc, type Bc } from 'b';
type x = bc | Bc;
console.log(abc, Abc, I, R, M);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
		f->VerifyOrganizeImports(t,
		R"TS(import {
    I,
    M,
    R,
} from 'a';
import { abc, Abc, type bc, type Bc } from 'b';
type x = bc | Bc;
console.log(abc, Abc, I, R, M);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports23, TestOrganizeImports23);

// organizeImports2_test.go
static void TestOrganizeImports2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import {
    Foo   
 , Bar   
} from "foo"

console.log(Foo, Bar);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import {
    Bar,
    Foo
} from "foo";

console.log(Foo, Bar);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports2, TestOrganizeImports2);

// organizeImports3_test.go
static void TestOrganizeImports3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import {
    Bar   
    , Foo   
  } from "foo"

console.log(Foo, Bar);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import {
    Bar,
    Foo
} from "foo";

console.log(Foo, Bar);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports3, TestOrganizeImports3);

// organizeImports4_test.go
static void TestOrganizeImports4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import * as something from "path";/** 
 * some comment here
 * and there
 */
import * as somethingElse from "anotherpath";
import * as AnotherThing from "somepath";/** 
 * some comment here
 * and there
 */
import * as AnotherThingElse from "someotherpath";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS()TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports4, TestOrganizeImports4);

// organizeImports5_test.go
static void TestOrganizeImports5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import * as something from "path";/** 
 * some comment here
 * and there
 */
import * as somethingElse from "anotherpath";
import * as AnotherThing from "somepath";/** 
 * some comment here
 * and there
 */
import * as AnotherThingElse from "someotherpath";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS()TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports5, TestOrganizeImports5);

// organizeImports6_test.go
static void TestOrganizeImports6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import * as something from "path"; /* small comment */ // single line one.
/* some comment here
* and there
*/
import * as somethingElse from "anotherpath";
import * as anotherThing from "someopath"; /* small comment */ // single line one.
/* some comment here
* and there
*/
import * as anotherThingElse from "someotherpath";

anotherThing;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(/* some comment here
* and there
*/
import * as anotherThing from "someopath"; /* small comment */ // single line one.
/* some comment here
* and there
*/

anotherThing;)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports6, TestOrganizeImports6);

// organizeImports7_test.go
static void TestOrganizeImports7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import * as something from "path"; /**
 * some comment here
 * and there
 */
import * as somethingElse from "anotherpath";

something;
somethingElse;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import * as somethingElse from "anotherpath";
import * as something from "path"; /**
 * some comment here
 * and there
 */

something;
somethingElse;)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports7, TestOrganizeImports7);

// organizeImports8_test.go
static void TestOrganizeImports8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import { foo as foo } from "foo";
foo;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import { foo } from "foo";
foo;)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports8, TestOrganizeImports8);

// organizeImports9_test.go
static void TestOrganizeImports9(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import { a as a, b, c, d as d, e as e } from "foo";
a(b, d);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import { a, b, d } from "foo";
a(b, d);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports9, TestOrganizeImports9);

// organizeImportsAttributes2_test.go
static void TestOrganizeImportsAttributes2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import { A } from "./a";
import { C } from "./a" with { type: "a" };
import { Z } from "./z";
import { A as D } from "./a" with { type: "b" };
import { E } from "./a" with { type: "a" };
import { F } from "./a" with { type: "a" };
import { B } from "./a";

export type G = A | B | C | D | E | F | Z;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import { A, B } from "./a";
import { C, E, F } from "./a" with { type: "a" };
import { A as D } from "./a" with { type: "b" };
import { Z } from "./z";

export type G = A | B | C | D | E | F | Z;)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsAttributes2, TestOrganizeImportsAttributes2);

// organizeImportsAttributes3_test.go
static void TestOrganizeImportsAttributes3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import { A } from "./a";
import { C } from "./a" with {      type: "a" };
import { Z } from "./z";
import { A as D } from "./a" with    { type: "b" };
import { E } from "./a" with { type: /* comment*/ "a"              };
import { F } from "./a" with     {type: "a" };
import { Y } from "./a"   with{ type: "b" /* comment*/};
import { B } from "./a";

export type G = A | B | C | D | E | F | Y | Z;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import { A, B } from "./a";
import { C, E, F } from "./a" with { type: "a" };
import { A as D, Y } from "./a" with { type: "b" };
import { Z } from "./z";

export type G = A | B | C | D | E | F | Y | Z;)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsAttributes3, TestOrganizeImportsAttributes3);

// organizeImportsAttributes4_test.go
static void TestOrganizeImportsAttributes4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import { A } from "./a" with { foo: "foo", bar: "bar" };
import { B } from "./a" with { bar: "bar", foo: "foo" };
import { D } from "./a" with { bar: "foo", foo: "bar" };
import { E } from "./a" with { foo: 'bar', bar: "foo" };
import { C } from "./a" with { foo: "bar", bar: "foo" };
import { F } from "./a" with { foo: "42" };
import { Y } from "./a" with { foo: 42 };
import { Z } from "./a" with { foo: "42" };

export type G = A | B | C | D | E | F | Y | Z;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import { A, B } from "./a" with { foo: "foo", bar: "bar" };
import { C, D, E } from "./a" with { bar: "foo", foo: "bar" };
import { F, Z } from "./a" with { foo: "42" };
import { Y } from "./a" with { foo: 42 };

export type G = A | B | C | D | E | F | Y | Z;)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsAttributes4, TestOrganizeImportsAttributes4);

// organizeImportsAttributes_test.go
static void TestOrganizeImportsAttributes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import { A } from "./file";
import { type B } from "./file";
import { C } from "./file" with { type: "a" };
import { A as D } from "./file" with { type: "b" };
import { E } from "./file" with { type: "a" };
import { A as F } from "./file" with { type: "b" };

type G = A | B | C | D | E | F;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import { A, type B } from "./file";
import { C, E } from "./file" with { type: "a" };
import { A as D, A as F } from "./file" with { type: "b" };

type G = A | B | C | D | E | F;)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsAttributes, TestOrganizeImportsAttributes);

// organizeImportsGroup_CommentInNewline_test.go
static void TestOrganizeImportsGroup_CommentInNewline(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// polyfill
import c from "C";
// not polyfill
import d from "D";
import a from "A";
import b from "B";

console.log(a, b, c, d))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(// polyfill
import c from "C";
// not polyfill
import a from "A";
import b from "B";
import d from "D";

console.log(a, b, c, d))TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsGroup_CommentInNewline, TestOrganizeImportsGroup_CommentInNewline);

// organizeImportsGroup_MultiNewlines_test.go
static void TestOrganizeImportsGroup_MultiNewlines(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import c from "C";


import d from "D";
import a from "A";
import b from "B";

console.log(a, b, c, d))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import c from "C";


import a from "A";
import b from "B";
import d from "D";

console.log(a, b, c, d))TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsGroup_MultiNewlines, TestOrganizeImportsGroup_MultiNewlines);

// organizeImportsGroup_MultilineCommentInNewline_test.go
static void TestOrganizeImportsGroup_MultilineCommentInNewline(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// polyfill
import c from "C";
/*
* demo
*/
import d from "D";
import a from "A";
import b from "B";

console.log(a, b, c, d))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(// polyfill
import c from "C";
/*
* demo
*/
import a from "A";
import b from "B";
import d from "D";

console.log(a, b, c, d))TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsGroup_MultilineCommentInNewline, TestOrganizeImportsGroup_MultilineCommentInNewline);

// organizeImportsGroup_Newline_test.go
static void TestOrganizeImportsGroup_Newline(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import c from "C";

import d from "D";
import a from "A"; // not count
import b from "B";

console.log(a, b, c, d))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import c from "C";

import a from "A"; // not count
import b from "B";
import d from "D";

console.log(a, b, c, d))TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsGroup_Newline, TestOrganizeImportsGroup_Newline);

// organizeImportsPathsUnicode1_test.go
static void TestOrganizeImportsPathsUnicode1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import * as Ab from "./Ab";
import * as _aB from "./_aB";
import * as aB from "./aB";
import * as _Ab from "./_Ab";

console.log(_aB, _Ab, aB, Ab);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import * as Ab from "./Ab";
import * as _Ab from "./_Ab";
import * as _aB from "./_aB";
import * as aB from "./aB";

console.log(_aB, _Ab, aB, Ab);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::False,
			.OrganizeImportsCollation =  lsutil::OrganizeImportsCollationOrdinal}));
		f->VerifyOrganizeImports(t,
		R"TS(import * as _aB from "./_aB";
import * as _Ab from "./_Ab";
import * as aB from "./aB";
import * as Ab from "./Ab";

console.log(_aB, _Ab, aB, Ab);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::False,
			.OrganizeImportsCollation =  lsutil::OrganizeImportsCollationUnicode}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsPathsUnicode1, TestOrganizeImportsPathsUnicode1);

// organizeImportsPathsUnicode2_test.go
static void TestOrganizeImportsPathsUnicode2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import * as a2 from "./a2";
import * as a100 from "./a100";
import * as a1 from "./a1";

console.log(a1, a2, a100);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import * as a1 from "./a1";
import * as a100 from "./a100";
import * as a2 from "./a2";

console.log(a1, a2, a100);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase =       Tristate::False,
			.OrganizeImportsCollation =        lsutil::OrganizeImportsCollationUnicode,
			.OrganizeImportsNumericCollation = Tristate::False}));
		f->VerifyOrganizeImports(t,
		R"TS(import * as a1 from "./a1";
import * as a2 from "./a2";
import * as a100 from "./a100";

console.log(a1, a2, a100);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase =       Tristate::False,
			.OrganizeImportsCollation =        lsutil::OrganizeImportsCollationUnicode,
			.OrganizeImportsNumericCollation = Tristate::True}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsPathsUnicode2, TestOrganizeImportsPathsUnicode2);

// organizeImportsPathsUnicode3_test.go
static void TestOrganizeImportsPathsUnicode3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import * as B from "./B";
import * as À from "./À";
import * as A from "./A";

console.log(A, À, B);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import * as A from "./A";
import * as À from "./À";
import * as B from "./B";

console.log(A, À, B);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase =      Tristate::False,
			.OrganizeImportsCollation =       lsutil::OrganizeImportsCollationUnicode,
			.OrganizeImportsAccentCollation = Tristate::False}));
		f->VerifyOrganizeImports(t,
		R"TS(import * as A from "./A";
import * as À from "./À";
import * as B from "./B";

console.log(A, À, B);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase =      Tristate::False,
			.OrganizeImportsCollation =       lsutil::OrganizeImportsCollationUnicode,
			.OrganizeImportsAccentCollation = Tristate::True}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsPathsUnicode3, TestOrganizeImportsPathsUnicode3);

// organizeImportsPathsUnicode4_test.go
static void TestOrganizeImportsPathsUnicode4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import * as Ab from "./Ab";
import * as _aB from "./_aB";
import * as aB from "./aB";
import * as _Ab from "./_Ab";

console.log(_aB, _Ab, aB, Ab);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import * as _Ab from "./_Ab";
import * as _aB from "./_aB";
import * as Ab from "./Ab";
import * as aB from "./aB";

console.log(_aB, _Ab, aB, Ab);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::False,
			.OrganizeImportsCollation =  lsutil::OrganizeImportsCollationUnicode,
			.OrganizeImportsCaseFirst =  lsutil::OrganizeImportsCaseFirstUpper}));
		f->VerifyOrganizeImports(t,
		R"TS(import * as _aB from "./_aB";
import * as _Ab from "./_Ab";
import * as aB from "./aB";
import * as Ab from "./Ab";

console.log(_aB, _Ab, aB, Ab);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::False,
			.OrganizeImportsCollation =  lsutil::OrganizeImportsCollationUnicode,
			.OrganizeImportsCaseFirst =  lsutil::OrganizeImportsCaseFirstLower}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsPathsUnicode4, TestOrganizeImportsPathsUnicode4);

// organizeImportsReactJsxDev_test.go
static void TestOrganizeImportsReactJsxDev(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @allowSyntheticDefaultImports: true
// @moduleResolution: bundler
// @noUnusedLocals: true
// @target: es2018
// @jsx: react-jsxdev
// @filename: test.tsx
import React from 'react';
export default () => <div></div>
// @filename: node_modules/react/package.json
{
    "name": "react",
    "types": "index.d.ts"
}
// @filename: node_modules/react/index.d.ts
export = React;
declare namespace JSX {
    interface IntrinsicElements { [x: string]: any; }
}
declare namespace React {}
// @filename: node_modules/react/jsx-runtime.d.ts
import './';
// @filename: node_modules/react/jsx-dev-runtime.d.ts
import './';)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "test.tsx");
		f->VerifyOrganizeImports(t,
		R"TS(export default () => <div></div>)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsReactJsxDev, TestOrganizeImportsReactJsxDev);

// organizeImportsReactJsx_test.go
static void TestOrganizeImportsReactJsx(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @allowSyntheticDefaultImports: true
// @moduleResolution: bundler
// @noUnusedLocals: true
// @target: es2018
// @jsx: react-jsx
// @filename: test.tsx
import React from 'react';
export default () => <div></div>
// @filename: node_modules/react/package.json
{
    "name": "react",
    "types": "index.d.ts"
}
// @filename: node_modules/react/index.d.ts
export = React;
declare namespace JSX {
    interface IntrinsicElements { [x: string]: any; }
}
declare namespace React {}
// @filename: node_modules/react/jsx-runtime.d.ts
import './';
// @filename: node_modules/react/jsx-dev-runtime.d.ts
import './';)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "test.tsx");
		f->VerifyOrganizeImports(t,
		R"TS(export default () => <div></div>)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsReactJsx, TestOrganizeImportsReactJsx);

// organizeImportsShebang_test.go
static void TestOrganizeImports_Shebang_PreserveAndSort(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(#!/usr/bin/env node
import Foo from "foo";
import Bar from "bar";

import Foobar from "foobar";

console.log(Foo, Bar, Foobar);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(#!/usr/bin/env node
import Bar from "bar";
import Foo from "foo";

import Foobar from "foobar";

console.log(Foo, Bar, Foobar);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_Shebang_PreserveAndSort, TestOrganizeImports_Shebang_PreserveAndSort);

// organizeImportsType10_test.go
static void TestOrganizeImportsType10(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import {
    type Type1,
    type Type2,
    func4,
    type Type3,
    type Type4,
    type Type5,
    type Type7,
    type Type8,
    type Type9,
    func1,
    func2,
    type Type6,
    func3,
    func5,
    func6,
    func7,
    func8,
    func9,
} from "foo";
interface Use extends Type1, Type2, Type3, Type4, Type5, Type6, Type7, Type8, Type9 {}
console.log(func1, func2, func3, func4, func5, func6, func7, func8, func9);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import {
    type Type1,
    type Type2,
    type Type3,
    type Type4,
    type Type5,
    type Type6,
    type Type7,
    type Type8,
    type Type9,
    func1,
    func2,
    func3,
    func4,
    func5,
    func6,
    func7,
    func8,
    func9,
} from "foo";
interface Use extends Type1, Type2, Type3, Type4, Type5, Type6, Type7, Type8, Type9 {}
console.log(func1, func2, func3, func4, func5, func6, func7, func8, func9);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::True}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsType10, TestOrganizeImportsType10);

// organizeImportsType11_test.go
static void TestOrganizeImportsType11(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import {
    type Type1,
    type Type2,
    func4,
    type Type3,
    type Type4,
    type Type5,
    type Type7,
    type Type8,
    type Type9,
    func1,
    func2,
    type Type6,
    func3,
    func5,
    func6,
    func7,
    func8,
    func9,
} from "foo";
interface Use extends Type1, Type2, Type3, Type4, Type5, Type6, Type7, Type8, Type9 {}
console.log(func1, func2, func3, func4, func5, func6, func7, func8, func9);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import {
    type Type1,
    type Type2,
    type Type3,
    type Type4,
    type Type5,
    type Type6,
    type Type7,
    type Type8,
    type Type9,
    func1,
    func2,
    func3,
    func4,
    func5,
    func6,
    func7,
    func8,
    func9,
} from "foo";
interface Use extends Type1, Type2, Type3, Type4, Type5, Type6, Type7, Type8, Type9 {}
console.log(func1, func2, func3, func4, func5, func6, func7, func8, func9);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsType11, TestOrganizeImportsType11);

// organizeImportsType1_test.go
static void TestOrganizeImportsType1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @allowSyntheticDefaultImports: true
// @moduleResolution: bundler
// @noUnusedLocals: true
// @target: es2018
import { A } from "foo";
import { type B } from "foo";
import { C } from "foo";
import { type E } from "foo";
import { D } from "foo";

console.log(A, B, C, D, E);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import { A, C, D, type B, type E } from "foo";

console.log(A, B, C, D, E);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
		f->VerifyOrganizeImports(t,
		R"TS(import { A, type B, C, D, type E } from "foo";

console.log(A, B, C, D, E);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderInline}));
		f->VerifyOrganizeImports(t,
		R"TS(import { type B, type E, A, C, D } from "foo";

console.log(A, B, C, D, E);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderFirst}));
		f->VerifyOrganizeImports(t,
		R"TS(import { A, C, D, type B, type E } from "foo";

console.log(A, B, C, D, E);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderLast}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsType1, TestOrganizeImportsType1);

// organizeImportsType2_test.go
static void TestOrganizeImportsType2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @allowSyntheticDefaultImports: true
// @moduleResolution: bundler
// @noUnusedLocals: true
// @target: es2018
type A = string;
type B = string;
const C = "hello";
export { A, type B, C };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(type A = string;
type B = string;
const C = "hello";
export { A, C, type B };
)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
		f->VerifyOrganizeImports(t,
		R"TS(type A = string;
type B = string;
const C = "hello";
export { A, type B, C };
)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderInline}));
		f->VerifyOrganizeImports(t,
		R"TS(type A = string;
type B = string;
const C = "hello";
export { type B, A, C };
)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderFirst}));
		f->VerifyOrganizeImports(t,
		R"TS(type A = string;
type B = string;
const C = "hello";
export { A, C, type B };
)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderLast}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsType2, TestOrganizeImportsType2);

// organizeImportsType3_test.go
static void TestOrganizeImportsType3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import {
    d, 
    type d as D,
    type c,
    c as C,
    b,
    b as B,
    type A,
    a
} from './foo';
console.log(A, a, B, b, c, C, d, D);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import {
    type A,
    b as B,
    c as C,
    type d as D,
    a,
    b,
    type c,
    d
} from './foo';
console.log(A, a, B, b, c, C, d, D);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::False,
			.OrganizeImportsTypeOrder =  lsutil::OrganizeImportsTypeOrderInline}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsType3, TestOrganizeImportsType3);

// organizeImportsType4_test.go
static void TestOrganizeImportsType4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import {
    d, 
    type d as D,
    type c,
    c as C,
    b,
    b as B,
    type A,
    a
} from './foo';
console.log(A, a, B, b, c, C, d, D);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import {
    type A,
    a,
    b,
    b as B,
    type c,
    c as C,
    d,
    type d as D
} from './foo';
console.log(A, a, B, b, c, C, d, D);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::True,
			.OrganizeImportsTypeOrder =  lsutil::OrganizeImportsTypeOrderInline}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsType4, TestOrganizeImportsType4);

// organizeImportsType5_test.go
static void TestOrganizeImportsType5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import {
    d, 
    type d as D,
    type c,
    c as C,
    b,
    b as B,
    type A,
    a
} from './foo';
console.log(A, a, B, b, c, C, d, D);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import {
    type A,
    a,
    b,
    b as B,
    type c,
    c as C,
    d,
    type d as D
} from './foo';
console.log(A, a, B, b, c, C, d, D);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::Unknown,
			.OrganizeImportsTypeOrder =  lsutil::OrganizeImportsTypeOrderInline}));
		f->VerifyOrganizeImports(t,
		R"TS(import {
    type A,
    a,
    b,
    b as B,
    type c,
    c as C,
    d,
    type d as D
} from './foo';
console.log(A, a, B, b, c, C, d, D);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::Unknown,
			.OrganizeImportsTypeOrder =  lsutil::OrganizeImportsTypeOrderInline}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsType5, TestOrganizeImportsType5);

// organizeImportsType6_test.go
static void TestOrganizeImportsType6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import { type a, A, b } from "foo";
interface Use extends A {}
console.log(a, b);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import { type a, A, b } from "foo";
interface Use extends A {}
console.log(a, b);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderInline}));
		f->ReplaceLine(t, 0, "import { type a, A, b } from \"foo1\";");
		f->VerifyOrganizeImports(t,
		R"TS(import { type a, A, b } from "foo1";
interface Use extends A {}
console.log(a, b);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::Unknown,
			.OrganizeImportsTypeOrder =  lsutil::OrganizeImportsTypeOrderInline}));
		f->ReplaceLine(t, 0, "import { type a, A, b } from \"foo2\";");
		f->VerifyOrganizeImports(t,
		R"TS(import { type a, A, b } from "foo2";
interface Use extends A {}
console.log(a, b);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::True,
			.OrganizeImportsTypeOrder =  lsutil::OrganizeImportsTypeOrderInline}));
		f->ReplaceLine(t, 0, "import { type a, A, b } from \"foo3\";");
		f->VerifyOrganizeImports(t,
		R"TS(import { A, type a, b } from "foo3";
interface Use extends A {}
console.log(a, b);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::False,
			.OrganizeImportsTypeOrder =  lsutil::OrganizeImportsTypeOrderInline}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsType6, TestOrganizeImportsType6);

// organizeImportsType7_test.go
static void TestOrganizeImportsType7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import { a, type A, b } from "foo";
interface Use extends A {}
console.log(a, b);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import { a, type A, b } from "foo";
interface Use extends A {}
console.log(a, b);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderInline}));
		f->ReplaceLine(t, 0, "import { a, type A, b } from \"foo1\";");
		f->VerifyOrganizeImports(t,
		R"TS(import { a, type A, b } from "foo1";
interface Use extends A {}
console.log(a, b);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::Unknown,
			.OrganizeImportsTypeOrder =  lsutil::OrganizeImportsTypeOrderInline}));
		f->ReplaceLine(t, 0, "import { a, type A, b } from \"foo2\";");
		f->VerifyOrganizeImports(t,
		R"TS(import { a, type A, b } from "foo2";
interface Use extends A {}
console.log(a, b);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::True,
			.OrganizeImportsTypeOrder =  lsutil::OrganizeImportsTypeOrderInline}));
		f->ReplaceLine(t, 0, "import { a, type A, b } from \"foo3\";");
		f->VerifyOrganizeImports(t,
		R"TS(import { type A, a, b } from "foo3";
interface Use extends A {}
console.log(a, b);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::False,
			.OrganizeImportsTypeOrder =  lsutil::OrganizeImportsTypeOrderInline}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsType7, TestOrganizeImportsType7);

// organizeImportsType8_test.go
static void TestOrganizeImportsType8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import { type A, type a, b, B } from "foo";
console.log(a, b, A, B);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import { type A, type a, b, B } from "foo";
console.log(a, b, A, B);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::Unknown,
			.OrganizeImportsTypeOrder =  lsutil::OrganizeImportsTypeOrderInline}));
		f->ReplaceLine(t, 0, "import { type A, type a, b, B } from \"foo1\";");
		f->VerifyOrganizeImports(t,
		R"TS(import { type A, type a, b, B } from "foo1";
console.log(a, b, A, B);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::Unknown,
			.OrganizeImportsTypeOrder =  lsutil::OrganizeImportsTypeOrderFirst}));
		f->ReplaceLine(t, 0, "import { type A, type a, b, B } from \"foo2\";");
		f->VerifyOrganizeImports(t,
		R"TS(import { b, B, type A, type a } from "foo2";
console.log(a, b, A, B);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::Unknown,
			.OrganizeImportsTypeOrder =  lsutil::OrganizeImportsTypeOrderLast}));
		f->ReplaceLine(t, 0, "import { type A, type a, b, B } from \"foo3\";");
		f->VerifyOrganizeImports(t,
		R"TS(import { type A, type a, b, B } from "foo3";
console.log(a, b, A, B);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::Unknown}));
		f->ReplaceLine(t, 0, "import { type A, type a, b, B } from \"foo4\";");
		f->VerifyOrganizeImports(t,
		R"TS(import { type A, type a, b, B } from "foo4";
console.log(a, b, A, B);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::True}));
		f->ReplaceLine(t, 0, "import { type A, type a, b, B } from \"foo5\";");
		f->VerifyOrganizeImports(t,
		R"TS(import { type A, B, type a, b } from "foo5";
console.log(a, b, A, B);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::False}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsType8, TestOrganizeImportsType8);

// organizeImportsType9_test.go
static void TestOrganizeImportsType9(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import { type a, type A, b, B } from "foo";
console.log(a, b, A, B);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import { type a, type A, b, B } from "foo";
console.log(a, b, A, B);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::Unknown,
			.OrganizeImportsTypeOrder =  lsutil::OrganizeImportsTypeOrderInline}));
		f->ReplaceLine(t, 0, "import { type a, type A, b, B } from \"foo1\";");
		f->VerifyOrganizeImports(t,
		R"TS(import { type a, type A, b, B } from "foo1";
console.log(a, b, A, B);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::Unknown,
			.OrganizeImportsTypeOrder =  lsutil::OrganizeImportsTypeOrderFirst}));
		f->ReplaceLine(t, 0, "import { type a, type A, b, B } from \"foo2\";");
		f->VerifyOrganizeImports(t,
		R"TS(import { b, B, type a, type A } from "foo2";
console.log(a, b, A, B);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::Unknown,
			.OrganizeImportsTypeOrder =  lsutil::OrganizeImportsTypeOrderLast}));
		f->ReplaceLine(t, 0, "import { type a, type A, b, B } from \"foo3\";");
		f->VerifyOrganizeImports(t,
		R"TS(import { type a, type A, b, B } from "foo3";
console.log(a, b, A, B);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::Unknown}));
		f->ReplaceLine(t, 0, "import { type a, type A, b, B } from \"foo4\";");
		f->VerifyOrganizeImports(t,
		R"TS(import { type a, type A, b, B } from "foo4";
console.log(a, b, A, B);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::True}));
		f->ReplaceLine(t, 0, "import { type a, type A, b, B } from \"foo5\";");
		f->VerifyOrganizeImports(t,
		R"TS(import { type A, B, type a, b } from "foo5";
console.log(a, b, A, B);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::False}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsType9, TestOrganizeImportsType9);

// organizeImportsUnicode1_test.go
static void TestOrganizeImportsUnicode1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import {
    Ab,
    _aB,
    aB,
    _Ab,
} from './foo';

console.log(_aB, _Ab, aB, Ab);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import {
    Ab,
    _Ab,
    _aB,
    aB,
} from './foo';

console.log(_aB, _Ab, aB, Ab);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::False,
			.OrganizeImportsCollation =  lsutil::OrganizeImportsCollationOrdinal}));
		f->VerifyOrganizeImports(t,
		R"TS(import {
    _aB,
    _Ab,
    aB,
    Ab,
} from './foo';

console.log(_aB, _Ab, aB, Ab);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::False,
			.OrganizeImportsCollation =  lsutil::OrganizeImportsCollationUnicode}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsUnicode1, TestOrganizeImportsUnicode1);

// organizeImportsUnicode2_test.go
static void TestOrganizeImportsUnicode2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import {
    a2,
    a100,
    a1,
} from './foo';

console.log(a1, a2, a100);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import {
    a1,
    a100,
    a2,
} from './foo';

console.log(a1, a2, a100);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase =       Tristate::False,
			.OrganizeImportsCollation =        lsutil::OrganizeImportsCollationUnicode,
			.OrganizeImportsNumericCollation = Tristate::False}));
		f->VerifyOrganizeImports(t,
		R"TS(import {
    a1,
    a2,
    a100,
} from './foo';

console.log(a1, a2, a100);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase =       Tristate::False,
			.OrganizeImportsCollation =        lsutil::OrganizeImportsCollationUnicode,
			.OrganizeImportsNumericCollation = Tristate::True}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsUnicode2, TestOrganizeImportsUnicode2);

// organizeImportsUnicode3_test.go
static void TestOrganizeImportsUnicode3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import {
    B,
    À,
    A,
} from './foo';

console.log(A, À, B);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import {
    A,
    À,
    B,
} from './foo';

console.log(A, À, B);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase =      Tristate::False,
			.OrganizeImportsCollation =       lsutil::OrganizeImportsCollationUnicode,
			.OrganizeImportsAccentCollation = Tristate::False}));
		f->VerifyOrganizeImports(t,
		R"TS(import {
    A,
    À,
    B,
} from './foo';

console.log(A, À, B);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase =      Tristate::False,
			.OrganizeImportsCollation =       lsutil::OrganizeImportsCollationUnicode,
			.OrganizeImportsAccentCollation = Tristate::True}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsUnicode3, TestOrganizeImportsUnicode3);

// organizeImportsUnicode4_test.go
static void TestOrganizeImportsUnicode4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import {
    Ab,
    _aB,
    aB,
    _Ab,
} from './foo';

console.log(_aB, _Ab, aB, Ab);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import {
    _Ab,
    _aB,
    Ab,
    aB,
} from './foo';

console.log(_aB, _Ab, aB, Ab);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::False,
			.OrganizeImportsCollation =  lsutil::OrganizeImportsCollationUnicode,
			.OrganizeImportsCaseFirst =  lsutil::OrganizeImportsCaseFirstUpper}));
		f->VerifyOrganizeImports(t,
		R"TS(import {
    _aB,
    _Ab,
    aB,
    Ab,
} from './foo';

console.log(_aB, _Ab, aB, Ab);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsIgnoreCase = Tristate::False,
			.OrganizeImportsCollation =  lsutil::OrganizeImportsCollationUnicode,
			.OrganizeImportsCaseFirst =  lsutil::OrganizeImportsCaseFirstLower}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsUnicode4, TestOrganizeImportsUnicode4);

// organizeImportsWithTraceResolution1_test.go
static void TestOrganizeImportsWithTraceResolution1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /project/tsconfig.json
{
  "compilerOptions": {
    "traceResolution": true
  }
}
// @Filename: /project/main.ts
import "./dep.js";
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/project/main.ts");
		f->VerifyOrganizeImports(
		t,
		R"TS(import "./dep.js";
)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImportsWithTraceResolution1, TestOrganizeImportsWithTraceResolution1);

// organizeImports_coalesceExports_test.go
static void TestOrganizeImports_coalesceExports_sortSpecifiersCaseInsensitive(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(export { default as M, a as n, B, y, Z as O } from "lib";
void 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(export { B, default as M, a as n, Z as O, y } from "lib";
void 0;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_coalesceExports_sortSpecifiersCaseInsensitive, TestOrganizeImports_coalesceExports_sortSpecifiersCaseInsensitive);

static void TestOrganizeImports_coalesceExports_combineNamespaceReExports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(export * from "lib";
export * from "lib";
void 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(export * from "lib";
void 0;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_coalesceExports_combineNamespaceReExports, TestOrganizeImports_coalesceExports_combineNamespaceReExports);

static void TestOrganizeImports_coalesceExports_combinePropertyExports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const x = 1, z = 2;
export { x };
export { z as y };
void 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(const x = 1, z = 2;
export { x, z as y };
void 0;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_coalesceExports_combinePropertyExports, TestOrganizeImports_coalesceExports_combinePropertyExports);

static void TestOrganizeImports_coalesceExports_combinePropertyReExports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(export { x } from "lib";
export { y as z } from "lib";
void 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(export { x, y as z } from "lib";
void 0;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_coalesceExports_combinePropertyReExports, TestOrganizeImports_coalesceExports_combinePropertyReExports);

static void TestOrganizeImports_coalesceExports_namespaceWithPropertyReExport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// Namespace re-export and property re-export from same module should not be combined.;
		const std::string content = R"TS(export * from "lib";
export { y } from "lib";
export { z } from "aaa";
void 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(export { z } from "aaa";
export * from "lib";
export { y } from "lib";
void 0;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_coalesceExports_namespaceWithPropertyReExport, TestOrganizeImports_coalesceExports_namespaceWithPropertyReExport);

static void TestOrganizeImports_coalesceExports_combineMany(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(const x = 1, w = 2, z = 3, q = 4;
export { x };
export { w as y, z as default };
export { q as w };
void 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(const x = 1, w = 2, z = 3, q = 4;
export { z as default, q as w, x, w as y };
void 0;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_coalesceExports_combineMany, TestOrganizeImports_coalesceExports_combineMany);

static void TestOrganizeImports_coalesceExports_combineManyReExports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(export { x as a, y } from "lib";
export * from "lib";
export { z as b } from "lib";
void 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(export * from "lib";
export { x as a, z as b, y } from "lib";
void 0;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_coalesceExports_combineManyReExports, TestOrganizeImports_coalesceExports_combineManyReExports);

static void TestOrganizeImports_coalesceExports_keepTypeOnlySeparate(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// Type-only exports should be kept separate from value exports.;
		const std::string content = R"TS(const x = 1;
type y = string;
export { x };
export type { y };
export { z } from "aaa";
void 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(const x = 1;
type y = string;
export { z } from "aaa";
export { x };
export type { y };
void 0;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_coalesceExports_keepTypeOnlySeparate, TestOrganizeImports_coalesceExports_keepTypeOnlySeparate);

static void TestOrganizeImports_coalesceExports_combineTypeOnly(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(type x = string;
type y = number;
export type { x };
export type { y };
void 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(type x = string;
type y = number;
export type { x, y };
void 0;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_coalesceExports_combineTypeOnly, TestOrganizeImports_coalesceExports_combineTypeOnly);

// organizeImports_coalesceImports_test.go
static void TestOrganizeImports_coalesceImports_sortSpecifiersCaseInsensitive(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import { default as M, a as n, B, y, Z as O } from "lib";
M; n; B; y; O;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(import { B, default as M, a as n, Z as O, y } from "lib";
M; n; B; y; O;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_coalesceImports_sortSpecifiersCaseInsensitive, TestOrganizeImports_coalesceImports_sortSpecifiersCaseInsensitive);

static void TestOrganizeImports_coalesceImports_combineSideEffectOnly(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import "lib";
import "lib";
void 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(import "lib";
void 0;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_coalesceImports_combineSideEffectOnly, TestOrganizeImports_coalesceImports_combineSideEffectOnly);

static void TestOrganizeImports_coalesceImports_combineNamespaceImportsNotMerged(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// Namespace imports from the same module should not be merged into one.;
		const std::string content = R"TS(import * as x from "lib";
import * as y from "lib";
import { z } from "aaa";
x; y; z;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(import { z } from "aaa";
import * as x from "lib";
import * as y from "lib";
x; y; z;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_coalesceImports_combineNamespaceImportsNotMerged, TestOrganizeImports_coalesceImports_combineNamespaceImportsNotMerged);

static void TestOrganizeImports_coalesceImports_combineDefaultImports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import x from "lib";
import y from "lib";
x; y;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(import { default as x, default as y } from "lib";
x; y;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_coalesceImports_combineDefaultImports, TestOrganizeImports_coalesceImports_combineDefaultImports);

static void TestOrganizeImports_coalesceImports_combinePropertyImports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import { x } from "lib";
import { y as z } from "lib";
x; z;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(import { x, y as z } from "lib";
x; z;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_coalesceImports_combinePropertyImports, TestOrganizeImports_coalesceImports_combinePropertyImports);

static void TestOrganizeImports_coalesceImports_sideEffectWithNamespace(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// Side-effect-only import and namespace import from same module should not be combined.;
		const std::string content = R"TS(import "lib";
import * as x from "lib";
import { z } from "aaa";
x; z;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(import { z } from "aaa";
import "lib";
import * as x from "lib";
x; z;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_coalesceImports_sideEffectWithNamespace, TestOrganizeImports_coalesceImports_sideEffectWithNamespace);

static void TestOrganizeImports_coalesceImports_sideEffectWithDefault(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// Side-effect-only import and default import from same module should not be combined.;
		const std::string content = R"TS(import "lib";
import x from "lib";
import { z } from "aaa";
x; z;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(import { z } from "aaa";
import "lib";
import x from "lib";
x; z;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_coalesceImports_sideEffectWithDefault, TestOrganizeImports_coalesceImports_sideEffectWithDefault);

static void TestOrganizeImports_coalesceImports_sideEffectWithProperty(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// Side-effect-only import and property import from same module should not be combined.;
		const std::string content = R"TS(import "lib";
import { x } from "lib";
import { z } from "aaa";
x; z;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(import { z } from "aaa";
import "lib";
import { x } from "lib";
x; z;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_coalesceImports_sideEffectWithProperty, TestOrganizeImports_coalesceImports_sideEffectWithProperty);

static void TestOrganizeImports_coalesceImports_namespaceWithDefault(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// Namespace import and default import from same module should be combined.;
		const std::string content = R"TS(import * as x from "lib";
import y from "lib";
x; y;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(import y, * as x from "lib";
x; y;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_coalesceImports_namespaceWithDefault, TestOrganizeImports_coalesceImports_namespaceWithDefault);

static void TestOrganizeImports_coalesceImports_namespaceWithProperty(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// Namespace import and property import from same module should not be combined.;
		const std::string content = R"TS(import * as x from "lib";
import { y } from "lib";
import { z } from "aaa";
x; y; z;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(import { z } from "aaa";
import * as x from "lib";
import { y } from "lib";
x; y; z;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_coalesceImports_namespaceWithProperty, TestOrganizeImports_coalesceImports_namespaceWithProperty);

static void TestOrganizeImports_coalesceImports_defaultWithProperty(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// Default import and property import from same module should be combined.;
		const std::string content = R"TS(import x from "lib";
import { y } from "lib";
x; y;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(import x, { y } from "lib";
x; y;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_coalesceImports_defaultWithProperty, TestOrganizeImports_coalesceImports_defaultWithProperty);

static void TestOrganizeImports_coalesceImports_combineMany(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import "lib";
import * as y from "lib";
import w from "lib";
import { b } from "lib";
import "lib";
import * as x from "lib";
import z from "lib";
import { a } from "lib";
w; x; y; z; a; b;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(import "lib";
import * as x from "lib";
import * as y from "lib";
import { a, b, default as w, default as z } from "lib";
w; x; y; z; a; b;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_coalesceImports_combineMany, TestOrganizeImports_coalesceImports_combineMany);

static void TestOrganizeImports_coalesceImports_twoNamespacesOneDefault(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// Descriptive test: two namespace imports + one default should not combine.;
		const std::string content = R"TS(import * as x from "lib";
import * as y from "lib";
import z from "lib";
import { w } from "aaa";
x; y; z; w;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(import { w } from "aaa";
import * as x from "lib";
import * as y from "lib";
import z from "lib";
x; y; z; w;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_coalesceImports_twoNamespacesOneDefault, TestOrganizeImports_coalesceImports_twoNamespacesOneDefault);

static void TestOrganizeImports_coalesceImports_typeOnlySeparate(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// Type-only imports should be coalesced separately from value imports.;
		const std::string content = R"TS(import type { x } from "lib";
import type { y } from "lib";
import { z } from "lib";
x; y; z;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(import type { x, y } from "lib";
import { z } from "lib";
x; y; z;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_coalesceImports_typeOnlySeparate, TestOrganizeImports_coalesceImports_typeOnlySeparate);

static void TestOrganizeImports_coalesceImports_typeOnlyKindsNotCombined(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// Type-only default, namespace, and named imports should not be combined with each other.;
		const std::string content = R"TS(import type { x } from "lib";
import type * as y from "lib";
import type z from "lib";
x; y; z;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(import type * as y from "lib";
import type z from "lib";
import type { x } from "lib";
x; y; z;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_coalesceImports_typeOnlyKindsNotCombined, TestOrganizeImports_coalesceImports_typeOnlyKindsNotCombined);

static void TestOrganizeImports_coalesceImports_sortSpecifiersTypeOnlyInline(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import { type z, y, type x, c, type b, a } from "lib";
z; y; x; c; b; a;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(import { a, type b, c, type x, y, type z } from "lib";
z; y; x; c; b; a;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{
			.OrganizeImportsSort =      lsutil::OrganizeImportsSortOrdinalIgnoreCase,
			.OrganizeImportsTypeOrder = lsutil::OrganizeImportsTypeOrderInline}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_coalesceImports_sortSpecifiersTypeOnlyInline, TestOrganizeImports_coalesceImports_sortSpecifiersTypeOnlyInline);

// organizeImports_dtsUnusedImportWithAugmentation_test.go
static void TestOrganizeImports_dtsUnusedImportWithAugmentation(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /styled-patch.d.ts
import * as styledComponents from 'styled-components';

declare module 'styled-components' {
    interface ThemedStyledComponentsModule {
        keyframes(): Keyframes;
    }
}
// @Filename: /node_modules/styled-components/index.d.ts
export interface Keyframes {}
export interface ThemedStyledComponentsModule {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(import 'styled-components';

declare module 'styled-components' {
    interface ThemedStyledComponentsModule {
        keyframes(): Keyframes;
    }
})TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_dtsUnusedImportWithAugmentation, TestOrganizeImports_dtsUnusedImportWithAugmentation);

// organizeImports_removeOnly_test.go
static void TestOrganizeImports_removeOnly(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import { c, b, a } from "foo";
import d, { e } from "bar";
import * as f from "baz";
import { g } from "foo";

export { g, e, b, c };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(t,
		R"TS(import { c, b } from "foo";
import { e } from "bar";
import { g } from "foo";

export { g, e, b, c };)TS",
		lsproto::CodeActionKindSourceRemoveUnusedImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_removeOnly, TestOrganizeImports_removeOnly);

// organizeImports_removeUnused_preservesMultiline_test.go
static void TestOrganizeImports_removeUnused_preservesMultiline(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import {
    a,
    b,
    c,
} from "module";

export { a, b, c };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(import {
    a,
    b,
    c,
} from "module";

export { a, b, c };)TS",
		lsproto::CodeActionKindSourceRemoveUnusedImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_removeUnused_preservesMultiline, TestOrganizeImports_removeUnused_preservesMultiline);

static void TestOrganizeImports_removeUnused_preservesMultilineWithRemoval(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import {
    a,
    b,
    c,
} from "module";

export { a, c };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(import {
    a,
    c
} from "module";

export { a, c };)TS",
		lsproto::CodeActionKindSourceRemoveUnusedImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_removeUnused_preservesMultilineWithRemoval, TestOrganizeImports_removeUnused_preservesMultilineWithRemoval);

// organizeImports_sortModuleSpecifiers_test.go
static void TestOrganizeImports_sortModuleSpecifiers_nonRelativeVsNonRelative(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import x from "lib2";
import y from "lib1";
x; y;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(import y from "lib1";
import x from "lib2";
x; y;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_sortModuleSpecifiers_nonRelativeVsNonRelative, TestOrganizeImports_sortModuleSpecifiers_nonRelativeVsNonRelative);

static void TestOrganizeImports_sortModuleSpecifiers_relativeVsRelative(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import x from "./lib2";
import y from "./lib1";
x; y;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(import y from "./lib1";
import x from "./lib2";
x; y;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_sortModuleSpecifiers_relativeVsRelative, TestOrganizeImports_sortModuleSpecifiers_relativeVsRelative);

static void TestOrganizeImports_sortModuleSpecifiers_relativeVsNonRelative(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import x from "./lib";
import y from "lib";
x; y;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(import y from "lib";
import x from "./lib";
x; y;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_sortModuleSpecifiers_relativeVsNonRelative, TestOrganizeImports_sortModuleSpecifiers_relativeVsNonRelative);

static void TestOrganizeImports_sortModuleSpecifiers_caseInsensitive(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// Verify "a" sorts before "Z" (case-insensitive);
		const std::string content = R"TS(import x from "Z";
import y from "a";
x; y;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(import y from "a";
import x from "Z";
x; y;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_sortModuleSpecifiers_caseInsensitive, TestOrganizeImports_sortModuleSpecifiers_caseInsensitive);

static void TestOrganizeImports_sortModuleSpecifiers_caseInsensitiveReverse(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		// Verify "A" sorts before "z" (case-insensitive);
		const std::string content = R"TS(import x from "z";
import y from "A";
x; y;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(import y from "A";
import x from "z";
x; y;)TS",
		lsproto::CodeActionKindSourceSortImportsTs,
		std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.OrganizeImportsSort = lsutil::OrganizeImportsSortOrdinalIgnoreCase}));
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_sortModuleSpecifiers_caseInsensitiveReverse, TestOrganizeImports_sortModuleSpecifiers_caseInsensitiveReverse);

// organizeImports_typeOrderSameModule_test.go
static void TestOrganizeImports_importKindOrder(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @module: commonjs
// @Filename: /main.ts
import { foo } from './package';
import type { Foo } from './package';
import './package';
import Default from './package';
import * as ns from './package';

const x: Foo = foo;
console.log(x, Default, ns);
// @Filename: /package.d.ts
export type Foo = string;
export declare const foo: Foo;
export declare function fn(): void;
export default class Default {}
export as namespace Package;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(import './package';
import type { Foo } from './package';
import * as ns from './package';
import Default, { foo } from './package';

const x: Foo = foo;
console.log(x, Default, ns);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_importKindOrder, TestOrganizeImports_importKindOrder);

static void TestOrganizeImports_importKindOrderMultipleModules(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @module: commonjs
// @Filename: /main.ts
import { b } from './b';
import { a } from './a';
import type { TypeB } from './b';
import type { TypeA } from './a';
import './b';
import './a';

const x: TypeA = a;
const y: TypeB = b;
console.log(x, y);
// @Filename: /a.d.ts
export type TypeA = string;
export declare const a: TypeA;
// @Filename: /b.d.ts
export type TypeB = string;
export declare const b: TypeB;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr , content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOrganizeImports(
		t,
		R"TS(import './a';
import type { TypeA } from './a';
import { a } from './a';
import './b';
import type { TypeB } from './b';
import { b } from './b';

const x: TypeA = a;
const y: TypeB = b;
console.log(x, y);)TS",
		lsproto::CodeActionKindSourceOrganizeImportsTs,
		nullptr);
	});
}
REGISTER_FOURSLASH_TEST(TestOrganizeImports_importKindOrderMultipleModules, TestOrganizeImports_importKindOrderMultipleModules);


}  // namespace
