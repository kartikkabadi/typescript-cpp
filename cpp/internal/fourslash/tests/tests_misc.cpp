// Ported fourslash tests — batch A. One static void TestX(gostd::testing::T*)
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
#include "internal/testutil/testutil.h"

namespace {
using namespace tsc;
namespace tsu = tsc::fourslash::tests::util;

// documentHighlights01_test.go
static void TestDocumentHighlights01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @lib: es5
// @Filename: a.ts
function [|f|](x: typeof [|f|]) {
    [|f|]([|f|]);
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		auto ranges = f->Ranges();
		f->VerifyBaselineDocumentHighlights(t, nullptr, std::vector<fourslash::MarkerOrRangeOrName>(ranges.begin(), ranges.end()));
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlights01, TestDocumentHighlights01);

// documentHighlights02_test.go
static void TestDocumentHighlights02(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @lib: es5
// @Filename: a.ts
function [|foo|] () {
	return 1;
}
[|foo|]();
// @Filename: b.ts
/// <reference path="a.ts"/>
[|foo|]();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->GoToFile(t, "a.ts");
		f->GoToFile(t, "b.ts");
		auto ranges = f->Ranges();
		f->VerifyBaselineDocumentHighlightsWithOptions(t, nullptr, std::vector<std::string>{"a.ts", "b.ts"}, std::vector<fourslash::MarkerOrRangeOrName>(ranges.begin(), ranges.end()));
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlights02, TestDocumentHighlights02);

// documentHighlightsExportEqualsInMergedNamespace_test.go
static void TestDocumentHighlightsExportEqualsInMergedNamespace(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(
class C {}
namespace C {
    /*marker*/export = C;
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, {"marker"});
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightsExportEqualsInMergedNamespace, TestDocumentHighlightsExportEqualsInMergedNamespace);

// outliningSpansForFunction_test.go
static void TestOutliningSpansForFunction(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS([|(
    a: number,
    b: number
) => {
    return a + b;
}|];

(a: number, b: number) =>[| {
    return a + b;
}|]

const f1 = function[| (
    a: number
    b: number
) {
    return a + b;
}|]

const f2 = function (a: number, b: number)[| {
    return a + b;
}|]

function f3[| (
    a: number
    b: number
) {
    return a + b;
}|]

function f4(a: number, b: number)[| {
    return a + b;
}|]

class Foo[| {
    constructor[|(
        a: number,
        b: number
    ) {
        this.a = a;
        this.b = b;
    }|]

    m1[|(
        a: number,
        b: number
    ) {
        return a + b;
    }|]

    m1(a: number, b: number)[| {
        return a + b;
    }|]
}|]

declare function foo(props: any): void;
foo[|(
    a =>[| {

    }|]
)|]

foo[|(
    (a) =>[| {

    }|]
)|]

foo[|(
    (a, b, c) =>[| {

    }|]
)|]

foo[|([|
    (a,
     b,
     c) => {

    }|]
)|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOutliningSpans(t, {});
	});
}
REGISTER_FOURSLASH_TEST(TestOutliningSpansForFunction, TestOutliningSpansForFunction);

// outliningSpansForArrowFunctionBody_test.go
static void TestOutliningSpansForArrowFunctionBody(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(() => 42;
() => ( 42 );
() =>[| {
    42
}|];
() => [|(
    42
)|];
() =>[| "foo" +
    "bar" +
    "baz"|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOutliningSpans(t, {});
	});
}
REGISTER_FOURSLASH_TEST(TestOutliningSpansForArrowFunctionBody, TestOutliningSpansForArrowFunctionBody);

// outliningSpansForImportsAndExports_test.go
static void TestOutliningSpansForImportsAndExports(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(import { a1, a2 } from "a";
;
import {
} from "a";
;
import [|{
  b1,
  b2,
}|] from "b";
;
import j1 from "./j" with { type: "json" };
;
import j2 from "./j" with {
};
;
import j3 from "./j" with [|{
  type: "json"
}|];
;
[|import { a5, a6 } from "a";
import [|{
  a7,
  a8,
}|] from "a";|]
export { a1, a2 };
;
export { a3, a4 } from "a";
;
export {
};
;
export [|{
  b1,
  b2,
}|];
;
export {
} from "b";
;
export [|{
  b3,
  b4,
}|] from "b";
;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyOutliningSpans(t, {});
	});
}
REGISTER_FOURSLASH_TEST(TestOutliningSpansForImportsAndExports, TestOutliningSpansForImportsAndExports);

// semanticClassification1_test.go
static void TestSemanticClassification1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(module /*0*/M {
    export interface /*1*/I {
    }
}
interface /*2*/X extends /*3*/M./*4*/I { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "namespace.declaration", .Text = "M"}, fourslash::SemanticToken{.Type = "interface.declaration", .Text = "I"}, fourslash::SemanticToken{.Type = "interface.declaration", .Text = "X"}, fourslash::SemanticToken{.Type = "namespace", .Text = "M"}, fourslash::SemanticToken{.Type = "interface", .Text = "I"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSemanticClassification1, TestSemanticClassification1);

// semanticClassification2_test.go
static void TestSemanticClassification2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(interface /*0*/Thing {
    toExponential(): number;
}

var Thing = 0;
Thing.toExponential();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "interface.declaration", .Text = "Thing"}, fourslash::SemanticToken{.Type = "method.declaration", .Text = "toExponential"}, fourslash::SemanticToken{.Type = "variable.declaration", .Text = "Thing"}, fourslash::SemanticToken{.Type = "variable", .Text = "Thing"}, fourslash::SemanticToken{.Type = "method.defaultLibrary", .Text = "toExponential"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSemanticClassification2, TestSemanticClassification2);

// semanticClassificationAlias_test.go
static void TestSemanticClassificationAlias(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		const std::string content = R"TS(// @Filename: /a.ts
export type x = number;
export class y {};
// @Filename: /b.ts
import { /*0*/x, /*1*/y } from "./a";
const v: /*2*/x = /*3*/y;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->GoToFile(t, "/b.ts");
		f->VerifySemanticTokens(t, std::vector<fourslash::SemanticToken>{fourslash::SemanticToken{.Type = "variable.declaration.readonly", .Text = "v"}, fourslash::SemanticToken{.Type = "type", .Text = "x"}, fourslash::SemanticToken{.Type = "class", .Text = "y"}});
	});
}
REGISTER_FOURSLASH_TEST(TestSemanticClassificationAlias, TestSemanticClassificationAlias);

}  // namespace
