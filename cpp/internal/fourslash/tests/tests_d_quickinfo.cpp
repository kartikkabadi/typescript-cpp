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

static void TestHoverSelfReExportedNamespaceGenericNoCrash(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: mod.ts
export interface Box<A> { content: Content<A> }
export type Content<A> = A
export * as Box from "./mod"
// @filename: main.ts
import { Box } from "./mod"
declare const b: Box<string>
const x = b./*1*/content
)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestHoverSelfReExportedNamespaceGenericNoCrash, TestHoverSelfReExportedNamespaceGenericNoCrash);

static void TestHoverSelfReExportedNamespaceGenericClassNoCrash(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: mod.ts
export class Box<A> { content!: A }
export * as Box from "./mod"
// @filename: main.ts
import { Box } from "./mod"
declare const b: Box<string>
const x = b./*1*/content
)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestHoverSelfReExportedNamespaceGenericClassNoCrash, TestHoverSelfReExportedNamespaceGenericClassNoCrash);

static void TestHoverNamespaceExportGenericNonColliding(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: mod.ts
export interface Box<A> { content: A }
export * as BoxNS from "./mod"
// @filename: main.ts
import { Box } from "./mod"
declare const b: Box<string>
const x = b./*1*/content
)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestHoverNamespaceExportGenericNonColliding, TestHoverNamespaceExportGenericNonColliding);

static void TestHoverMappedTypePropertyJSDoc(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @filename: a.ts
export declare const A: Readonly<{
  /**
   * x prop
   */
  readonly X: 200;

  /**
   * y prop
   */
  readonly Y: 201;
}>;

A.X/*1*/;

// @filename: b.ts
import { A } from './a';

A.X/*2*/;
)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestHoverMappedTypePropertyJSDoc, TestHoverMappedTypePropertyJSDoc);

static void TestHoverMappedTypeWithoutPropertyType(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
declare function uhoh/*1*/<T>(x: { [K in keyof T] }): void;
)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestHoverMappedTypeWithoutPropertyType, TestHoverMappedTypeWithoutPropertyType);

static void TestHoverThenDiagnosticsJsxIntrinsic(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /tsconfig.json
{ "compilerOptions": { "strict": true, "jsx": "preserve" } }
// @Filename: /jsx.d.ts
declare namespace JSX {
    interface Element { }
    interface IntrinsicElements {
        div: any;
    }
}
// @Filename: /file.tsx
export default function Home() {
    return <di/*1*/v>hi</div>;
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(property) JSX.IntrinsicElements.div: any", "");
		f->VerifyNoErrors(t);
	});
}
REGISTER_FOURSLASH_TEST(TestHoverThenDiagnosticsJsxIntrinsic, TestHoverThenDiagnosticsJsxIntrinsic);

static void TestHoverQualifiedGenericNames(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
function f<T>(x: T) {
    class C {
        value = x
    }
    return new C()
}

class A<T> {
    foo() {}
}
class B extends A<string> {}

let t1/*1*/ = f("hello")
const t2/*2*/ = new B()
t2./*3*/foo()
)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "let t1: f<string>.C", "");
		f->VerifyQuickInfoAt(t, "2", "const t2: B", "");
		f->VerifyQuickInfoAt(t, "3", "(method) A<string>.foo(): void", "");
	});
}
REGISTER_FOURSLASH_TEST(TestHoverQualifiedGenericNames, TestHoverQualifiedGenericNames);

static void TestHoverOverPrivateName(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class A {
    #f/*1*/oo = 3;
    #b/*2*/ar: number;
    #b/*3*/az = () => "hello";
    #q/*4*/ux(n: number): string {
        return "" + n;
    }
    static #staticF/*5*/oo = 3;
    static #staticB/*6*/ar: number;
    static #staticB/*7*/az = () => "hello";
    static #staticQ/*8*/ux(n: number): string {
        return "" + n;
    }
})TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(property) A.#foo: number", "");
		f->VerifyQuickInfoAt(t, "2", "(property) A.#bar: number", "");
		f->VerifyQuickInfoAt(t, "3", "(property) A.#baz: () => string", "");
		f->VerifyQuickInfoAt(t, "4", "(method) A.#qux(n: number): string", "");
		f->VerifyQuickInfoAt(t, "5", "(property) A.#staticFoo: number", "");
		f->VerifyQuickInfoAt(t, "6", "(property) A.#staticBar: number", "");
		f->VerifyQuickInfoAt(t, "7", "(property) A.#staticBaz: () => string", "");
		f->VerifyQuickInfoAt(t, "8", "(method) A.#staticQux(n: number): string", "");
	});
}
REGISTER_FOURSLASH_TEST(TestHoverOverPrivateName, TestHoverOverPrivateName);

static void TestHoverOverComment(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(export function f() {}
//foo
/**///moo)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->GoToMarker(t, "");
		f->VerifyQuickInfoIs(t, "", "");
		f->VerifyBaselineFindAllReferences(t, {""});
		f->VerifyBaselineGoToDefinition(t, false, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestHoverOverComment, TestHoverOverComment);

static void TestHoverOptionalMembers(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
type Foo1 = {
    x?: string;
    f?: (x: number) => void;
    g?: { (x: number): void; (x: string): void; }
    h?: ((x: number) => void) & ((x: string) => void);
    m?(x: number): void;
    m?(x: string): void;
}

interface Foo2 {
    x?: string;
    f?: (x: number) => void;
    g?: { (x: number): void; (x: string): void; }
    h?: ((x: number) => void) & ((x: string) => void);
    m?(x: number): void;
    m?(x: string): void;
}

class Foo3 {
    x?: string;
    f?: (x: number) => void;
    g?: { (x: number): void; (x: string): void; }
    h?: ((x: number) => void) & ((x: string) => void);
    m?(x: number): void;
    m?(x: string): void;
}

declare const foo1: Foo1;
declare const foo2: Foo2;
declare const foo3: Foo3;

foo1./*1*/x
foo1./*1a*/f
foo1./*1b*/f?.(42)
foo1./*1c*/g
foo1./*1d*/g?.(42)
foo1./*1e*/g?.("abc")
foo1./*1f*/h
foo1./*1g*/h?.(42)
foo1./*1h*/h?.("abc")
foo1./*1i*/m
foo1./*1j*/m?.(42)
foo1./*1k*/m?.("abc")

foo2./*2*/x
foo2./*2a*/f
foo2./*2b*/f?.(42)
foo2./*2c*/g
foo2./*2d*/g?.(42)
foo2./*2e*/g?.("abc")
foo2./*2f*/h
foo2./*2g*/h?.(42)
foo2./*2h*/h?.("abc")
foo2./*2i*/m
foo2./*2j*/m?.(42)
foo2./*2k*/m?.("abc")

foo3./*3*/x
foo3./*3a*/f
foo3./*3b*/f?.(42)
foo3./*3c*/g
foo3./*3d*/g?.(42)
foo3./*3e*/g?.("abc")
foo3./*3f*/h
foo3./*3g*/h?.(42)
foo3./*3h*/h?.("abc")
foo3./*3i*/m
foo3./*3j*/m?.(42)
foo3./*3k*/m?.("abc")
)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestHoverOptionalMembers, TestHoverOptionalMembers);

static void TestHoverNilBaseSymbolIntersection(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @strict: true
// @filename: main.ts

class Base {}

declare const BaseFactory: new() => Base & { c: string };

class Derived extends BaseFactory {
  static /*1*/idField = "id" as const;
}
)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyBaselineHover(t);
	});
}
REGISTER_FOURSLASH_TEST(TestHoverNilBaseSymbolIntersection, TestHoverNilBaseSymbolIntersection);

static void TestHoverMixinOverrideDocumentation(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @strict: true
// @filename: main.ts

declare class BaseClass {
    /** some documentation */
    static method(): number;
}

type AnyConstructor = abstract new (...args: any[]) => object

class MixinClass {}
declare function Mix<T extends AnyConstructor>(BaseClass: T): typeof MixinClass & T;

declare class Mixed extends Mix(BaseClass) {
    static method(): number;
}

Mixed./*1*/method;
)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(method) Mixed.method(): number", "some documentation");
	});
}
REGISTER_FOURSLASH_TEST(TestHoverMixinOverrideDocumentation, TestHoverMixinOverrideDocumentation);

static void TestHoverCircularInheritedDocumentation(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @filename: base.ts
export interface Options {}
// @filename: bridge.ts
import type { Options as _Options } from "./base";
export * from "./base";
declare module "./bridge" {
    interface Options extends _Options { hooks: {} }
}
declare const v: Options;
v.hooks/*1*/;)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(property) Options.hooks: {}", "");
	});
}
REGISTER_FOURSLASH_TEST(TestHoverCircularInheritedDocumentation, TestHoverCircularInheritedDocumentation);

static void TestHoverCallSignatureDocumentation(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
type X = {
    /** Description of invoking. */
    (): string

    /** Description of constructor. */
    new (): number
}

declare const x: X

/*1*/x()
new /*2*/x()
)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "const x: () => string", "Description of invoking.");
		f->VerifyQuickInfoAt(t, "2", "const x: new () => number", "Description of constructor.");
	});
}
REGISTER_FOURSLASH_TEST(TestHoverCallSignatureDocumentation, TestHoverCallSignatureDocumentation);

static void TestHoverAliasInImportedFile(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @filename: other2.ts
export type SomeAliasType<T> = { value: T };

// @filename: other.ts
import { SomeAliasType } from './other2';

declare function isSomeAliasType(x: any): x is SomeAliasType<any>;

export { isSomeAliasType };

// @filename: main.ts
import { isSomeAliasType } from './other';

export function processValue(value: any) {
  if (/*1*/isSomeAliasType(value)) {
    console.log("ok");
  }
}
)TS";
		auto __fsp1 = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp1.first; auto done = __fsp1.second; TSC_DEFER(done());
		f->VerifyQuickInfoAt(t, "1", "(alias) function isSomeAliasType(x: any): x is SomeAliasType<any>", "");
	});
}
REGISTER_FOURSLASH_TEST(TestHoverAliasInImportedFile, TestHoverAliasInImportedFile);
} // namespace
