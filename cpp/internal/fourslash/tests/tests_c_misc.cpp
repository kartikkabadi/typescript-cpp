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


// codeLensFunctionExpressions01_test.go

// codeLensFunctionExpressions01_test.go
static void TestCodeLensFunctionExpressions01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @filename: anonymousFunctionExpressions.ts
export let anonFn1 = function () {};
export const anonFn2 = function () {};

let anonFn3 = function () {};
const anonFn4 = function () {};

// @filename: arrowFunctions.ts
export let arrowFn1 = () => {};
export const arrowFn2 = () => {};

let arrowFn3 = () => {};
const arrowFn4 = () => {};

// @filename: namedFunctions.ts
export let namedFn1 = function namedFn1() {
    namedFn1();
}
namedFn1();

export const namedFn2 = function namedFn2() {
    namedFn2();
}
namedFn2();

let namedFn3 = function namedFn3() {};
const namedFn4 = function namedFn4() {};
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineCodeLens(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.CodeLens = lsutil::CodeLensUserPreferences{.ReferencesCodeLensEnabled = Tristate::True, .ImplementationsCodeLensEnabled = Tristate::True, .ReferencesCodeLensShowOnAllFunctions = Tristate::True, .ImplementationsCodeLensShowOnInterfaceMethods = Tristate::True, .ImplementationsCodeLensShowOnAllClassMethods = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestCodeLensFunctionExpressions01, TestCodeLensFunctionExpressions01);

// codeLensFunctionsAndConstants01_test.go

// codeLensFunctionsAndConstants01_test.go
static void TestCodeLensFunctionsAndConstants01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @module: preserve

// @filename: ./exports.ts

let callCount = 0;
export function foo(n: number): void {
  callCount++;
  if (n > 0) {
	foo(n - 1);
  }
  else {
    console.log("function was called " + callCount + " times");
  }
}

foo(5);

export const bar = 123;

// @filename: ./importer.ts
import { foo, bar } from "./exports";

foo(5);
console.log(bar);
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineCodeLens(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.CodeLens = lsutil::CodeLensUserPreferences{.ReferencesCodeLensEnabled = Tristate::True, .ImplementationsCodeLensEnabled = Tristate::True, .ReferencesCodeLensShowOnAllFunctions = Tristate::True, .ImplementationsCodeLensShowOnInterfaceMethods = Tristate::True, .ImplementationsCodeLensShowOnAllClassMethods = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestCodeLensFunctionsAndConstants01, TestCodeLensFunctionsAndConstants01);

// codeLensInterface01_test.go

// codeLensInterface01_test.go
static void TestCodeLensInterface01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @module: preserve

// @filename: ./pointable.ts
export interface Pointable {
  getX(): number;
  getY(): number;
}

// @filename: ./classPointable.ts
import { Pointable } from "./pointable";

class Point implements Pointable {
  getX(): number {
    return 0;
  }
  getY(): number {
    return 0;
  }
}

// @filename: ./objectPointable.ts
import { Pointable } from "./pointable";

let x = 0;
let y = 0;
const p: Pointable = {
  getX(): number {
	return x;
  },
  getY(): number {
	return y;
  },
};
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineCodeLens(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.CodeLens = lsutil::CodeLensUserPreferences{.ReferencesCodeLensEnabled = Tristate::True, .ImplementationsCodeLensEnabled = Tristate::True, .ReferencesCodeLensShowOnAllFunctions = Tristate::True, .ImplementationsCodeLensShowOnInterfaceMethods = Tristate::True, .ImplementationsCodeLensShowOnAllClassMethods = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestCodeLensInterface01, TestCodeLensInterface01);

// codeLensOverloads01_test.go

// codeLensOverloads01_test.go
static void TestCodeLensOverloads01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
export function foo(x: number): number;
export function foo(x: string): string;
export function foo(x: string | number): string | number {
	return x;
}

foo(1);

foo("hello");

// This one isn't expected to match any overload,
// but is really just here to test how it affects how code lens.
foo(Math.random() ? 1 : "hello");
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineCodeLens(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.CodeLens = lsutil::CodeLensUserPreferences{.ReferencesCodeLensEnabled = Tristate::True, .ImplementationsCodeLensEnabled = Tristate::True, .ReferencesCodeLensShowOnAllFunctions = Tristate::True, .ImplementationsCodeLensShowOnInterfaceMethods = Tristate::True, .ImplementationsCodeLensShowOnAllClassMethods = Tristate::True}}));
	});
}
REGISTER_FOURSLASH_TEST(TestCodeLensOverloads01, TestCodeLensOverloads01);

// codeLensShowOnAllClassMethods_test.go

// codeLensShowOnAllClassMethods_test.go
static void TestCodeLensReferencesShowOnAllClassMethods(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto containingTestName = t->Name();
		{
			int _ = 0;
			for (auto&& value : std::vector<Tristate>{Tristate::True, Tristate::False}) {
				t->Run(gostd::sprintf("%s=%v", {containingTestName, tristateIsTrue(value)}), [&](gostd::testing::T* t) {
	t->Parallel();
	const std::string content = R"TS(
export abstract class ABC {
  abstract methodA(): void;
  methodB(): void {}
  #methodC(): void {}
  protected methodD(): void {}
  private methodE(): void {}
  protected abstract methodG(): void;
  public methodH(): void {}

  static methodStaticA(): void {}
  protected static methodStaticB(): void {}
  private static methodStaticC(): void {}
  static #methodStaticD(): void {}
}
)TS";
	auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
	f->VerifyBaselineCodeLens(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.CodeLens = lsutil::CodeLensUserPreferences{.ImplementationsCodeLensEnabled = Tristate::True, .ImplementationsCodeLensShowOnAllClassMethods = value}}));
	});
				_++;
			}
		}
	});
}
REGISTER_FOURSLASH_TEST(TestCodeLensReferencesShowOnAllClassMethods, TestCodeLensReferencesShowOnAllClassMethods);

// codeLensShowOnAllFunctions_test.go

// codeLensShowOnAllFunctions_test.go
static void TestCodeLensReferencesShowOnAllFunctions(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto containingTestName = t->Name();
		{
			int _ = 0;
			for (auto&& value : std::vector<Tristate>{Tristate::True, Tristate::False}) {
				t->Run(gostd::sprintf("%s=%v", {containingTestName, tristateIsTrue(value)}), [&](gostd::testing::T* t) {
	t->Parallel();
	const std::string content = R"TS(
export function f1(): void {}

function f2(): void {}

export const f3 = () => {};

const f4 = () => {};

const f5 = function() {};
)TS";
	auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
	f->VerifyBaselineCodeLens(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.CodeLens = lsutil::CodeLensUserPreferences{.ReferencesCodeLensEnabled = Tristate::True, .ReferencesCodeLensShowOnAllFunctions = value}}));
	});
				_++;
			}
		}
	});
}
REGISTER_FOURSLASH_TEST(TestCodeLensReferencesShowOnAllFunctions, TestCodeLensReferencesShowOnAllFunctions);

// codeLensShowOnInterfaceMethods_test.go

// codeLensShowOnInterfaceMethods_test.go
static void TestCodeLensReferencesShowOnInterfaceMethods(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		auto containingTestName = t->Name();
		{
			int _ = 0;
			for (auto&& value : std::vector<Tristate>{Tristate::True, Tristate::False}) {
				t->Run(gostd::sprintf("%s=%v", {containingTestName, tristateIsTrue(value)}), [&](gostd::testing::T* t) {
	t->Parallel();
	const std::string content = R"TS(
export interface I {
  methodA(): void;
}
export interface I {
  methodB(): void;
}

interface J extends I {
  methodB(): void;
  methodC(): void;
}

class C implements J {
  methodA(): void {}
  methodB(): void {}
  methodC(): void {}
}

class AbstractC implements J {
  abstract methodA(): void;
  methodB(): void {}
  abstract methodC(): void;
}
)TS";
	auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
	f->VerifyBaselineCodeLens(t, std::make_shared<lsutil::UserPreferences>(lsutil::UserPreferences{.CodeLens = lsutil::CodeLensUserPreferences{.ImplementationsCodeLensEnabled = Tristate::True, .ImplementationsCodeLensShowOnInterfaceMethods = value}}));
	});
				_++;
			}
		}
	});
}
REGISTER_FOURSLASH_TEST(TestCodeLensReferencesShowOnInterfaceMethods, TestCodeLensReferencesShowOnInterfaceMethods);

// indentAfterFunctionClosingBraces_test.go

// indentAfterFunctionClosingBraces_test.go
static void TestIndentAfterFunctionClosingBraces(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class foo {
    public f() {
        return 0;
    /*1*/}/*2*/
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToMarker(t, "2");
		f->InsertLine(t, "");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(    })TS");
	});
}
REGISTER_FOURSLASH_TEST(TestIndentAfterFunctionClosingBraces, TestIndentAfterFunctionClosingBraces);

// indentationInJsx3_test.go

// indentationInJsx3_test.go
static void TestIndentationInJsx3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(//@Filename: file.tsx
function foo() {
   return (
        <div>
hello
goodbye
        </div>
    )
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyCurrentFileContent(t, R"TS(function foo() {
   return (
        <div>
hello
goodbye
        </div>
    )
})TS");
		f->FormatDocument(t, "");
		f->VerifyCurrentFileContent(t, R"TS(function foo() {
    return (
        <div>
            hello
            goodbye
        </div>
    )
})TS");
	});
}
REGISTER_FOURSLASH_TEST(TestIndentationInJsx3, TestIndentationInJsx3);

// semanticClassificationClassExpressionMethod_test.go

// semanticClassificationClassExpressionMethod_test.go
static void TestSemanticClassificationClassExpressionMethod(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var x = class C {
  equals(other: C) { return this == other; }
};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "class.declaration", .Text = "x"}, fourslash::SemanticToken{.Type = "class.declaration", .Text = "C"}, fourslash::SemanticToken{.Type = "method.declaration", .Text = "equals"}, fourslash::SemanticToken{.Type = "parameter.declaration", .Text = "other"}, fourslash::SemanticToken{.Type = "class", .Text = "C"}, fourslash::SemanticToken{.Type = "parameter", .Text = "other"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSemanticClassificationClassExpressionMethod, TestSemanticClassificationClassExpressionMethod);

// semanticClassificationClassExpression_test.go

// semanticClassificationClassExpression_test.go
static void TestSemanticClassificationClassExpression(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var x = class /*0*/C {}
class /*1*/C {}
class /*2*/D extends class /*3*/B{} { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "class.declaration", .Text = "x"}, fourslash::SemanticToken{.Type = "class.declaration", .Text = "C"}, fourslash::SemanticToken{.Type = "class.declaration", .Text = "C"}, fourslash::SemanticToken{.Type = "class.declaration", .Text = "D"}, fourslash::SemanticToken{.Type = "class.declaration", .Text = "B"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSemanticClassificationClassExpression, TestSemanticClassificationClassExpression);

// semanticClassificationInTemplateExpressions_test.go

// semanticClassificationInTemplateExpressions_test.go
static void TestSemanticClassificationInTemplateExpressions(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((std::string(R"TS(module /*0*/M {
    export class /*1*/C {
        static x;
    }
    export enum /*2*/E {
        E1 = 0
    }
}
)TS") + "`") + std::string(R"TS(abcd${ /*3*/M./*4*/C.x + /*5*/M./*6*/E.E1}efg)TS")) + std::string("`")) + std::string(R"TS()TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "namespace.declaration", .Text = "M"}, fourslash::SemanticToken{.Type = "class.declaration", .Text = "C"}, fourslash::SemanticToken{.Type = "property.declaration.static", .Text = "x"}, fourslash::SemanticToken{.Type = "enum.declaration", .Text = "E"}, fourslash::SemanticToken{.Type = "enumMember.declaration.readonly", .Text = "E1"}, fourslash::SemanticToken{.Type = "namespace", .Text = "M"}, fourslash::SemanticToken{.Type = "class", .Text = "C"}, fourslash::SemanticToken{.Type = "property.static", .Text = "x"}, fourslash::SemanticToken{.Type = "namespace", .Text = "M"}, fourslash::SemanticToken{.Type = "enum", .Text = "E"}, fourslash::SemanticToken{.Type = "enumMember.readonly", .Text = "E1"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSemanticClassificationInTemplateExpressions, TestSemanticClassificationInTemplateExpressions);

// semanticClassificationInstantiatedModuleWithVariableOfSameName1_test.go

// semanticClassificationInstantiatedModuleWithVariableOfSameName1_test.go
static void TestSemanticClassificationInstantiatedModuleWithVariableOfSameName1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(module /*0*/M {
    export interface /*1*/I {
    }
    var x = 10;
}

var /*2*/M = {
    foo: 10,
    bar: 20
}

var v: /*3*/M./*4*/I;

var x = /*5*/M;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "namespace.declaration", .Text = "M"}, fourslash::SemanticToken{.Type = "interface.declaration", .Text = "I"}, fourslash::SemanticToken{.Type = "variable.declaration.local", .Text = "x"}, fourslash::SemanticToken{.Type = "variable.declaration", .Text = "M"}, fourslash::SemanticToken{.Type = "property.declaration", .Text = "foo"}, fourslash::SemanticToken{.Type = "property.declaration", .Text = "bar"}, fourslash::SemanticToken{.Type = "variable.declaration", .Text = "v"}, fourslash::SemanticToken{.Type = "namespace", .Text = "M"}, fourslash::SemanticToken{.Type = "interface", .Text = "I"}, fourslash::SemanticToken{.Type = "variable.declaration", .Text = "x"}, fourslash::SemanticToken{.Type = "namespace", .Text = "M"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSemanticClassificationInstantiatedModuleWithVariableOfSameName1, TestSemanticClassificationInstantiatedModuleWithVariableOfSameName1);

// semanticClassificationInstantiatedModuleWithVariableOfSameName2_test.go

// semanticClassificationInstantiatedModuleWithVariableOfSameName2_test.go
static void TestSemanticClassificationInstantiatedModuleWithVariableOfSameName2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(module /*0*/M {
    export interface /*1*/I {
    }
}

module /*2*/M {
    var x = 10;
}

var /*3*/M = {
    foo: 10,
    bar: 20
}

var v: /*4*/M./*5*/I;

var x = /*6*/M;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "namespace.declaration", .Text = "M"}, fourslash::SemanticToken{.Type = "interface.declaration", .Text = "I"}, fourslash::SemanticToken{.Type = "namespace.declaration", .Text = "M"}, fourslash::SemanticToken{.Type = "variable.declaration.local", .Text = "x"}, fourslash::SemanticToken{.Type = "variable.declaration", .Text = "M"}, fourslash::SemanticToken{.Type = "property.declaration", .Text = "foo"}, fourslash::SemanticToken{.Type = "property.declaration", .Text = "bar"}, fourslash::SemanticToken{.Type = "variable.declaration", .Text = "v"}, fourslash::SemanticToken{.Type = "namespace", .Text = "M"}, fourslash::SemanticToken{.Type = "interface", .Text = "I"}, fourslash::SemanticToken{.Type = "variable.declaration", .Text = "x"}, fourslash::SemanticToken{.Type = "namespace", .Text = "M"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSemanticClassificationInstantiatedModuleWithVariableOfSameName2, TestSemanticClassificationInstantiatedModuleWithVariableOfSameName2);

// semanticClassificationJSX_test.go

// semanticClassificationJSX_test.go
static void TestSemanticClassificationJSX(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.tsx
const Component = () => <div>Hello</div>;
const afterJSX = 42;
const alsoAfterJSX = "test";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/a.tsx");
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "function.declaration.readonly", .Text = "Component"}, fourslash::SemanticToken{.Type = "variable.declaration.readonly", .Text = "afterJSX"}, fourslash::SemanticToken{.Type = "variable.declaration.readonly", .Text = "alsoAfterJSX"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSemanticClassificationJSX, TestSemanticClassificationJSX);

// semanticClassificationModules_test.go

// semanticClassificationModules_test.go
static void TestSemanticClassificationModules(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(module /*0*/M {
    export var v;
    export interface /*1*/I {
    }
}

var x: /*2*/M./*3*/I = /*4*/M.v;
var y = /*5*/M;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "namespace.declaration", .Text = "M"}, fourslash::SemanticToken{.Type = "variable.declaration.local", .Text = "v"}, fourslash::SemanticToken{.Type = "interface.declaration", .Text = "I"}, fourslash::SemanticToken{.Type = "variable.declaration", .Text = "x"}, fourslash::SemanticToken{.Type = "namespace", .Text = "M"}, fourslash::SemanticToken{.Type = "interface", .Text = "I"}, fourslash::SemanticToken{.Type = "namespace", .Text = "M"}, fourslash::SemanticToken{.Type = "variable.local", .Text = "v"}, fourslash::SemanticToken{.Type = "variable.declaration", .Text = "y"}, fourslash::SemanticToken{.Type = "namespace", .Text = "M"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSemanticClassificationModules, TestSemanticClassificationModules);

// semanticClassificationUninstantiatedModuleWithVariableOfSameName1_test.go

// semanticClassificationUninstantiatedModuleWithVariableOfSameName1_test.go
static void TestSemanticClassificationUninstantiatedModuleWithVariableOfSameName1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare module /*0*/M {
    interface /*1*/I {

    }
}

var M = { I: 10 };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "variable", .Text = "M"}, fourslash::SemanticToken{.Type = "interface.declaration", .Text = "I"}, fourslash::SemanticToken{.Type = "variable.declaration", .Text = "M"}, fourslash::SemanticToken{.Type = "property.declaration", .Text = "I"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSemanticClassificationUninstantiatedModuleWithVariableOfSameName1, TestSemanticClassificationUninstantiatedModuleWithVariableOfSameName1);

// semanticClassificationUninstantiatedModuleWithVariableOfSameName2_test.go

// semanticClassificationUninstantiatedModuleWithVariableOfSameName2_test.go
static void TestSemanticClassificationUninstantiatedModuleWithVariableOfSameName2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(module /*0*/M {
    export interface /*1*/I {
    }
}

var /*2*/M = {
    foo: 10,
    bar: 20
}

var v: /*3*/M./*4*/I;

var x = /*5*/M;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "variable", .Text = "M"}, fourslash::SemanticToken{.Type = "interface.declaration", .Text = "I"}, fourslash::SemanticToken{.Type = "variable.declaration", .Text = "M"}, fourslash::SemanticToken{.Type = "property.declaration", .Text = "foo"}, fourslash::SemanticToken{.Type = "property.declaration", .Text = "bar"}, fourslash::SemanticToken{.Type = "variable.declaration", .Text = "v"}, fourslash::SemanticToken{.Type = "variable", .Text = "M"}, fourslash::SemanticToken{.Type = "interface", .Text = "I"}, fourslash::SemanticToken{.Type = "variable.declaration", .Text = "x"}, fourslash::SemanticToken{.Type = "variable", .Text = "M"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSemanticClassificationUninstantiatedModuleWithVariableOfSameName2, TestSemanticClassificationUninstantiatedModuleWithVariableOfSameName2);

// semanticClassificationWithUnionTypes_test.go

// semanticClassificationWithUnionTypes_test.go
static void TestSemanticClassificationWithUnionTypes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(module /*0*/M {
    export interface /*1*/I {
    }
}

interface /*2*/I {
}
class /*3*/C {
}

var M: /*4*/M./*5*/I | /*6*/I | /*7*/C;
var I: typeof M | typeof /*8*/C;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "variable", .Text = "M"}, fourslash::SemanticToken{.Type = "interface.declaration", .Text = "I"}, fourslash::SemanticToken{.Type = "interface.declaration", .Text = "I"}, fourslash::SemanticToken{.Type = "class.declaration", .Text = "C"}, fourslash::SemanticToken{.Type = "variable.declaration", .Text = "M"}, fourslash::SemanticToken{.Type = "variable", .Text = "M"}, fourslash::SemanticToken{.Type = "interface", .Text = "I"}, fourslash::SemanticToken{.Type = "interface", .Text = "I"}, fourslash::SemanticToken{.Type = "class", .Text = "C"}, fourslash::SemanticToken{.Type = "class.declaration", .Text = "I"}, fourslash::SemanticToken{.Type = "variable", .Text = "M"}, fourslash::SemanticToken{.Type = "class", .Text = "C"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSemanticClassificationWithUnionTypes, TestSemanticClassificationWithUnionTypes);

// smartIndentNamedImport_test.go

// smartIndentNamedImport_test.go
static void TestSmartIndentNamedImport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(import {/*0*/
    numbers as bn,/*1*/
    list/*2*/
} from '@bykov/basics';/*3*/)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->FormatDocument(t, "");
		f->GoToMarker(t, "0");
		f->VerifyCurrentLineContent(t, R"TS(import {)TS");
		f->GoToMarker(t, "1");
		f->VerifyCurrentLineContent(t, R"TS(    numbers as bn,)TS");
		f->GoToMarker(t, "2");
		f->VerifyCurrentLineContent(t, R"TS(    list)TS");
		f->GoToMarker(t, "3");
		f->VerifyCurrentLineContent(t, R"TS(} from '@bykov/basics';)TS");
	});
}
REGISTER_FOURSLASH_TEST(TestSmartIndentNamedImport, TestSmartIndentNamedImport);

// suggestionNoDuplicates_test.go

// suggestionNoDuplicates_test.go
static void TestSuggestionNoDuplicates(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @strict: false
// @Filename: foo.ts
import { f } from [|'m'|]
f
// @Filename: node_modules/m/index.js
module.exports.f = function (x) { return x })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyNonSuggestionDiagnostics(t, {});
		f->VerifySuggestionDiagnostics(t, std::vector<std::shared_ptr<lsproto::Diagnostic>>{std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(7016))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("Could not find a declaration file for module 'm'. '/node_modules/m/index.js' implicitly has an 'any' type.")}})});
	});
}
REGISTER_FOURSLASH_TEST(TestSuggestionNoDuplicates, TestSuggestionNoDuplicates);

// suggestionOfUnusedVariableWithExternalModule_test.go

// suggestionOfUnusedVariableWithExternalModule_test.go
static void TestSuggestionOfUnusedVariableWithExternalModule(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Skip({"Known failing fourslash test"}); return;
		t->Parallel();
		const std::string content = R"TS(//@allowJs: true
//@module: commonjs
// @Filename: /mymodule.js
(function ([|root|], factory) {
    module.exports = factory();
}(this, function () {
    var [|unusedVar|] = "something";
    return {};
}));
// @Filename: /app.js
//@ts-check
[|require("./mymodule")|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/app.js");
		f->VerifySuggestionDiagnostics(t, std::vector<std::shared_ptr<lsproto::Diagnostic>>{std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Range = f->Ranges()[2]->LSRange, .Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(80001))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("File is a CommonJS module; it may be converted to an ES module.")}})});
		f->GoToFile(t, "/mymodule.js");
		f->VerifySuggestionDiagnostics(t, std::vector<std::shared_ptr<lsproto::Diagnostic>>{std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Range = f->Ranges()[0]->LSRange, .Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6133))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'root' is declared but its value is never read.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagUnnecessary})}), std::make_shared<lsproto::Diagnostic>(lsproto::Diagnostic{.Range = f->Ranges()[1]->LSRange, .Code = std::make_shared<lsproto::IntegerOrString>(lsproto::IntegerOrString{.Integer = std::make_shared<int32_t>(int32_t(6133))}), .Message = lsproto::StringOrMarkupContent{.String = std::make_shared<std::string>("'unusedVar' is declared but its value is never read.")}, .Tags = std::make_shared<lsproto::Slice<lsproto::DiagnosticTag>>(std::vector<lsproto::DiagnosticTag>{lsproto::DiagnosticTagUnnecessary})})});
	});
}
REGISTER_FOURSLASH_TEST(TestSuggestionOfUnusedVariableWithExternalModule, TestSuggestionOfUnusedVariableWithExternalModule);

// syntacticClassificationForJSDocTemplateTag_test.go

// syntacticClassificationForJSDocTemplateTag_test.go
static void TestSyntacticClassificationForJSDocTemplateTag(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** @template T baring strait */
function ident<T>: T {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "function.declaration", .Text = "ident"}, fourslash::SemanticToken{.Type = "typeParameter.declaration", .Text = "T"}, fourslash::SemanticToken{.Type = "typeParameter", .Text = "T"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSyntacticClassificationForJSDocTemplateTag, TestSyntacticClassificationForJSDocTemplateTag);

// syntacticClassificationWithErrors_test.go

// syntacticClassificationWithErrors_test.go
static void TestSyntacticClassificationWithErrors(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class A {
    a:
}
c =)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "class.declaration", .Text = "A"}, fourslash::SemanticToken{.Type = "property.declaration", .Text = "a"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSyntacticClassificationWithErrors, TestSyntacticClassificationWithErrors);

// syntacticClassifications1_test.go

// syntacticClassifications1_test.go
static void TestSyntacticClassifications1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// comment
namespace M {
    var v = 0 + 1;
    var s = "string";

    class C<T> {
    }

    enum E {
    }

    interface I {
    }

    namespace M1.M2 {
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "namespace.declaration", .Text = "M"}, fourslash::SemanticToken{.Type = "variable.declaration.local", .Text = "v"}, fourslash::SemanticToken{.Type = "variable.declaration.local", .Text = "s"}, fourslash::SemanticToken{.Type = "class.declaration", .Text = "C"}, fourslash::SemanticToken{.Type = "typeParameter.declaration", .Text = "T"}, fourslash::SemanticToken{.Type = "enum.declaration", .Text = "E"}, fourslash::SemanticToken{.Type = "interface.declaration", .Text = "I"}, fourslash::SemanticToken{.Type = "namespace.declaration", .Text = "M1"}, fourslash::SemanticToken{.Type = "namespace.declaration", .Text = "M2"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSyntacticClassifications1, TestSyntacticClassifications1);

// syntacticClassificationsConflictDiff3Markers1_test.go

// syntacticClassificationsConflictDiff3Markers1_test.go
static void TestSyntacticClassificationsConflictDiff3Markers1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
<<<<<<< HEAD
    v = 1;
||||||| merged common ancestors
    v = 3;
=======
    v = 2;
>>>>>>> Branch - a
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "class.declaration", .Text = "C"}, fourslash::SemanticToken{.Type = "property.declaration", .Text = "v"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSyntacticClassificationsConflictDiff3Markers1, TestSyntacticClassificationsConflictDiff3Markers1);

// syntacticClassificationsConflictDiff3Markers2_test.go

// syntacticClassificationsConflictDiff3Markers2_test.go
static void TestSyntacticClassificationsConflictDiff3Markers2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(<<<<<<< HEAD
class C { }
||||||| merged common ancestors
class E { }
=======
class D { }
>>>>>>> Branch - a)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "class.declaration", .Text = "C"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSyntacticClassificationsConflictDiff3Markers2, TestSyntacticClassificationsConflictDiff3Markers2);

// syntacticClassificationsConflictMarkers1_test.go

// syntacticClassificationsConflictMarkers1_test.go
static void TestSyntacticClassificationsConflictMarkers1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
<<<<<<< HEAD
    v = 1;
=======
    v = 2;
>>>>>>> Branch - a
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "class.declaration", .Text = "C"}, fourslash::SemanticToken{.Type = "property.declaration", .Text = "v"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSyntacticClassificationsConflictMarkers1, TestSyntacticClassificationsConflictMarkers1);

// syntacticClassificationsConflictMarkers2_test.go

// syntacticClassificationsConflictMarkers2_test.go
static void TestSyntacticClassificationsConflictMarkers2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(<<<<<<< HEAD
class C { }
=======
class D { }
>>>>>>> Branch - a)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "class.declaration", .Text = "C"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSyntacticClassificationsConflictMarkers2, TestSyntacticClassificationsConflictMarkers2);

// syntacticClassificationsDocComment1_test.go

// syntacticClassificationsDocComment1_test.go
static void TestSyntacticClassificationsDocComment1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** @type {number} */
var v;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "variable.declaration", .Text = "v"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSyntacticClassificationsDocComment1, TestSyntacticClassificationsDocComment1);

// syntacticClassificationsDocComment2_test.go

// syntacticClassificationsDocComment2_test.go
static void TestSyntacticClassificationsDocComment2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** @param foo { function(x): string } */
var v;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "variable.declaration", .Text = "v"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSyntacticClassificationsDocComment2, TestSyntacticClassificationsDocComment2);

// syntacticClassificationsDocComment3_test.go

// syntacticClassificationsDocComment3_test.go
static void TestSyntacticClassificationsDocComment3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** @param foo { number /* } */
var v;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "variable.declaration", .Text = "v"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSyntacticClassificationsDocComment3, TestSyntacticClassificationsDocComment3);

// syntacticClassificationsDocComment4_test.go

// syntacticClassificationsDocComment4_test.go
static void TestSyntacticClassificationsDocComment4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/** @param {number} p1 */
function foo(p1) {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "function.declaration", .Text = "foo"}, fourslash::SemanticToken{.Type = "parameter.declaration", .Text = "p1"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSyntacticClassificationsDocComment4, TestSyntacticClassificationsDocComment4);

// syntacticClassificationsForOfKeyword2_test.go

// syntacticClassificationsForOfKeyword2_test.go
static void TestSyntacticClassificationsForOfKeyword2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(for (var of in of) { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "variable.declaration", .Text = "of"}, fourslash::SemanticToken{.Type = "variable", .Text = "of"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSyntacticClassificationsForOfKeyword2, TestSyntacticClassificationsForOfKeyword2);

// syntacticClassificationsForOfKeyword3_test.go

// syntacticClassificationsForOfKeyword3_test.go
static void TestSyntacticClassificationsForOfKeyword3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(for (var of; of; of) { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "variable.declaration", .Text = "of"}, fourslash::SemanticToken{.Type = "variable", .Text = "of"}, fourslash::SemanticToken{.Type = "variable", .Text = "of"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSyntacticClassificationsForOfKeyword3, TestSyntacticClassificationsForOfKeyword3);

// syntacticClassificationsForOfKeyword_test.go

// syntacticClassificationsForOfKeyword_test.go
static void TestSyntacticClassificationsForOfKeyword(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(for (var of of of) { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "variable.declaration", .Text = "of"}, fourslash::SemanticToken{.Type = "variable", .Text = "of"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSyntacticClassificationsForOfKeyword, TestSyntacticClassificationsForOfKeyword);

// syntacticClassificationsFunctionWithComments_test.go

// syntacticClassificationsFunctionWithComments_test.go
static void TestSyntacticClassificationsFunctionWithComments(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/**
 * This is my function.
 * There are many like it, but this one is mine.
 */
function myFunction(/* x */ x: any) {
    var y = x ? x++ : ++x;
}
// end of file)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "function.declaration", .Text = "myFunction"}, fourslash::SemanticToken{.Type = "parameter.declaration", .Text = "x"}, fourslash::SemanticToken{.Type = "variable.declaration.local", .Text = "y"}, fourslash::SemanticToken{.Type = "parameter", .Text = "x"}, fourslash::SemanticToken{.Type = "parameter", .Text = "x"}, fourslash::SemanticToken{.Type = "parameter", .Text = "x"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSyntacticClassificationsFunctionWithComments, TestSyntacticClassificationsFunctionWithComments);

// syntacticClassificationsJsx1_test.go

// syntacticClassificationsJsx1_test.go
static void TestSyntacticClassificationsJsx1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: file1.tsx
let x  = <div a = "some-value" b = {1}>
    some jsx text
</div>;

let y = <element attr="123"/>)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "variable.declaration", .Text = "x"}, fourslash::SemanticToken{.Type = "variable.declaration", .Text = "y"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSyntacticClassificationsJsx1, TestSyntacticClassificationsJsx1);

// syntacticClassificationsJsx2_test.go

// syntacticClassificationsJsx2_test.go
static void TestSyntacticClassificationsJsx2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: file1.tsx
let x  = <div.name b = "some-value" c = {1}>
    some jsx text
</div.name>;

let y = <element.name attr="123"/>)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "variable.declaration", .Text = "x"}, fourslash::SemanticToken{.Type = "variable.declaration", .Text = "y"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSyntacticClassificationsJsx2, TestSyntacticClassificationsJsx2);

// syntacticClassificationsMergeConflictMarker1_test.go

// syntacticClassificationsMergeConflictMarker1_test.go
static void TestSyntacticClassificationsMergeConflictMarker1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(<<<<<<< HEAD
"AAAA"
=======
"BBBB"
>>>>>>> Feature)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{});
	});
}
REGISTER_FOURSLASH_TEST(TestSyntacticClassificationsMergeConflictMarker1, TestSyntacticClassificationsMergeConflictMarker1);

// syntacticClassificationsObjectLiteral_test.go

// syntacticClassificationsObjectLiteral_test.go
static void TestSyntacticClassificationsObjectLiteral(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var v = 10e0;
var x = {
    p1: 1,
    p2: 2,
    any: 3,
    function: 4,
    var: 5,
    void: void 0,
    v: v += v,
};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "variable.declaration", .Text = "v"}, fourslash::SemanticToken{.Type = "variable.declaration", .Text = "x"}, fourslash::SemanticToken{.Type = "property.declaration", .Text = "p1"}, fourslash::SemanticToken{.Type = "property.declaration", .Text = "p2"}, fourslash::SemanticToken{.Type = "property.declaration", .Text = "any"}, fourslash::SemanticToken{.Type = "property.declaration", .Text = "function"}, fourslash::SemanticToken{.Type = "property.declaration", .Text = "var"}, fourslash::SemanticToken{.Type = "property.declaration", .Text = "void"}, fourslash::SemanticToken{.Type = "property.declaration", .Text = "v"}, fourslash::SemanticToken{.Type = "variable", .Text = "v"}, fourslash::SemanticToken{.Type = "variable", .Text = "v"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSyntacticClassificationsObjectLiteral, TestSyntacticClassificationsObjectLiteral);

// syntacticClassificationsTemplates1_test.go

// syntacticClassificationsTemplates1_test.go
static void TestSyntacticClassificationsTemplates1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((((((std::string(R"TS(var v = 10e0;
var x = {
    p1: )TS") + "`") + std::string(R"TS(hello world)TS")) + std::string("`")) + std::string(R"TS(,
    p2: )TS")) + std::string("`")) + std::string(R"TS(goodbye ${0} cruel ${0} world)TS")) + std::string("`")) + std::string(R"TS(,
};)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "variable.declaration", .Text = "v"}, fourslash::SemanticToken{.Type = "variable.declaration", .Text = "x"}, fourslash::SemanticToken{.Type = "property.declaration", .Text = "p1"}, fourslash::SemanticToken{.Type = "property.declaration", .Text = "p2"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSyntacticClassificationsTemplates1, TestSyntacticClassificationsTemplates1);

// syntacticClassificationsTemplates2_test.go

// syntacticClassificationsTemplates2_test.go
static void TestSyntacticClassificationsTemplates2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((((((((((std::string(R"TS(var tiredOfCanonicalExamples =
)TS") + "`") + std::string(R"TS(goodbye "${ )TS")) + std::string("`")) + std::string(R"TS(hello world)TS")) + std::string("`")) + std::string(R"TS( }" 
and ${ )TS")) + std::string("`")) + std::string(R"TS(good${ " " }riddance)TS")) + std::string("`")) + std::string(R"TS( })TS")) + std::string("`")) + std::string(R"TS(;)TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "variable.declaration", .Text = "tiredOfCanonicalExamples"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSyntacticClassificationsTemplates2, TestSyntacticClassificationsTemplates2);

} // namespace
