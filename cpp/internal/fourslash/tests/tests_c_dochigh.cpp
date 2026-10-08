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


// documentHighlightAtInheritedProperties1_test.go

// documentHighlightAtInheritedProperties1_test.go
static void TestDocumentHighlightAtInheritedProperties1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: file1.ts
interface interface1 extends interface1 {
   [|doStuff|](): void;
   [|propName|]: string;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightAtInheritedProperties1, TestDocumentHighlightAtInheritedProperties1);

// documentHighlightAtInheritedProperties2_test.go

// documentHighlightAtInheritedProperties2_test.go
static void TestDocumentHighlightAtInheritedProperties2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: file1.ts
class class1 extends class1 {
   [|doStuff|]() { }
   [|propName|]: string;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightAtInheritedProperties2, TestDocumentHighlightAtInheritedProperties2);

// documentHighlightAtInheritedProperties3_test.go

// documentHighlightAtInheritedProperties3_test.go
static void TestDocumentHighlightAtInheritedProperties3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: file1.ts
interface interface1 extends interface1 {
   [|doStuff|](): void;
   [|propName|]: string;
}

var v: interface1;
v.[|propName|];
v.[|doStuff|]();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightAtInheritedProperties3, TestDocumentHighlightAtInheritedProperties3);

// documentHighlightAtInheritedProperties4_test.go

// documentHighlightAtInheritedProperties4_test.go
static void TestDocumentHighlightAtInheritedProperties4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: file1.ts
class class1 extends class1 {
   [|doStuff|]() { }
   [|propName|]: string;
}

var c: class1;
c.[|doStuff|]();
c.[|propName|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightAtInheritedProperties4, TestDocumentHighlightAtInheritedProperties4);

// documentHighlightAtInheritedProperties5_test.go

// documentHighlightAtInheritedProperties5_test.go
static void TestDocumentHighlightAtInheritedProperties5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: file1.ts
interface C extends D {
    [|prop0|]: string;
    [|prop1|]: number;
}

interface D extends C {
    [|prop0|]: string;
    [|prop1|]: number;
}

var d: D;
d.[|prop1|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightAtInheritedProperties5, TestDocumentHighlightAtInheritedProperties5);

// documentHighlightAtInheritedProperties6_test.go

// documentHighlightAtInheritedProperties6_test.go
static void TestDocumentHighlightAtInheritedProperties6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: file1.ts
class C extends D {
    [|prop0|]: string;
    [|prop1|]: string;
}

class D extends C {
    [|prop0|]: string;
    [|prop1|]: string;
}

var d: D;
d.[|prop1|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightAtInheritedProperties6, TestDocumentHighlightAtInheritedProperties6);

// documentHighlightAtParameterPropertyDeclaration1_test.go

// documentHighlightAtParameterPropertyDeclaration1_test.go
static void TestDocumentHighlightAtParameterPropertyDeclaration1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: file1.ts
class Foo {
    constructor(private [|privateParam|]: number,
        public [|publicParam|]: string,
        protected [|protectedParam|]: boolean) {

        let localPrivate = [|privateParam|];
        this.[|privateParam|] += 10;

        let localPublic = [|publicParam|];
        this.[|publicParam|] += " Hello!";

        let localProtected = [|protectedParam|];
        this.[|protectedParam|] = false;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightAtParameterPropertyDeclaration1, TestDocumentHighlightAtParameterPropertyDeclaration1);

// documentHighlightAtParameterPropertyDeclaration2_test.go

// documentHighlightAtParameterPropertyDeclaration2_test.go
static void TestDocumentHighlightAtParameterPropertyDeclaration2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: file1.ts
class Foo {
    // This is not valid syntax: parameter property can't be binding pattern
    constructor(private {[|privateParam|]}: number,
        public {[|publicParam|]}: string,
        protected {[|protectedParam|]}: boolean) {

        let localPrivate = [|privateParam|];
        this.privateParam += 10;

        let localPublic = [|publicParam|];
        this.publicParam += " Hello!";

        let localProtected = [|protectedParam|];
        this.protectedParam = false;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightAtParameterPropertyDeclaration2, TestDocumentHighlightAtParameterPropertyDeclaration2);

// documentHighlightAtParameterPropertyDeclaration3_test.go

// documentHighlightAtParameterPropertyDeclaration3_test.go
static void TestDocumentHighlightAtParameterPropertyDeclaration3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: file1.ts
class Foo {
    // This is not valid syntax: parameter property can't be binding pattern
    constructor(private [[|privateParam|]]: number,
        public [[|publicParam|]]: string,
        protected [[|protectedParam|]]: boolean) {

        let localPrivate = [|privateParam|];
        this.privateParam += 10;

        let localPublic = [|publicParam|];
        this.publicParam += " Hello!";

        let localProtected = [|protectedParam|];
        this.protectedParam = false;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightAtParameterPropertyDeclaration3, TestDocumentHighlightAtParameterPropertyDeclaration3);

// documentHighlightDefaultInKeyword_test.go

// documentHighlightDefaultInKeyword_test.go
static void TestDocumentHighlightDefaultInKeyword(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|case|]
[|default|])TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightDefaultInKeyword, TestDocumentHighlightDefaultInKeyword);

// documentHighlightDefaultInSwitch_test.go

// documentHighlightDefaultInSwitch_test.go
static void TestDocumentHighlightDefaultInSwitch(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(const foo = 'foo';
[|switch|] (foo) {
   [|case|] 'foo':
       [|break|];
   [|default|]:
       [|break|];
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, {f->Ranges()[1], f->Ranges()[4]});
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightDefaultInSwitch, TestDocumentHighlightDefaultInSwitch);

// documentHighlightImportPath_test.go

// documentHighlightImportPath_test.go
static void TestDocumentHighlightImportPath(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export const x = 0;

// @Filename: /b.ts
import { x } from "[|./a|]";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, {f->Ranges()[0]});
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightImportPath, TestDocumentHighlightImportPath);

// documentHighlightInExport1_test.go

// documentHighlightInExport1_test.go
static void TestDocumentHighlightInExport1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class [|C|] {}
[|export|] { [|C|] [|as|] [|D|] };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightInExport1, TestDocumentHighlightInExport1);

// documentHighlightInKeyword_test.go

// documentHighlightInKeyword_test.go
static void TestDocumentHighlightInKeyword(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(export type Foo<T> = {
    [K [|in|] keyof T]: any;
}

"a" [|in|] {};

for (let a [|in|] {}) {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightInKeyword, TestDocumentHighlightInKeyword);

// documentHighlightInTypeExport_test.go

// documentHighlightInTypeExport_test.go
static void TestDocumentHighlightInTypeExport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /1.ts
type [|A|] = 1;
export { [|A|] as [|B|] };
// @Filename: /2.ts
type [|A|] = 1;
let [|A|]: [|A|] = 1;
export { [|A|] as [|B|] };
// @Filename: /3.ts
type [|A|] = 1;
let [|A|]: [|A|] = 1;
export type { [|A|] as [|B|] };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightInTypeExport, TestDocumentHighlightInTypeExport);

// documentHighlightJSDocThisFunctionExpressionNoCrash1_test.go

// documentHighlightJSDocThisFunctionExpressionNoCrash1_test.go
static void TestDocumentHighlightJSDocThisFunctionExpressionParameter(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
/**@this{A}*/x=function(/*m*/a){};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, {"m"});
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightJSDocThisFunctionExpressionParameter, TestDocumentHighlightJSDocThisFunctionExpressionParameter);

// documentHighlightJSDocTypedef_test.go

// documentHighlightJSDocTypedef_test.go
static void TestDocumentHighlightJSDocTypedef(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @checkJs: true
// @Filename: index.js
/**
 * @typedef {{
 *   [|foo|]: string;
 *   [|bar|]: number;
 * }} Foo
 */

/** @type {Foo} */
const x = {
  [|foo|]: "",
  [|bar|]: 42,
};)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightJSDocTypedef, TestDocumentHighlightJSDocTypedef);

// documentHighlightMalformedAmbientModuleExportEquals_test.go

// documentHighlightMalformedAmbientModuleExportEquals_test.go
static void TestDocumentHighlightMalformedAmbientModuleExportEquals(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.d.ts
declare moduleu "m" {
  interface A { x: 1 }
  function f(): A[];
  /*m*/export = f;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, {"m"});
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightMalformedAmbientModuleExportEquals, TestDocumentHighlightMalformedAmbientModuleExportEquals);

// documentHighlightMultilineTemplateStrings_test.go

// documentHighlightMultilineTemplateStrings_test.go
static void TestDocumentHighlightMultilineTemplateStrings(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((std::string(R"TS(const foo = )TS") + "`") + std::string(R"TS(
    a
    [|b|]
    c
)TS")) + std::string("`")) + std::string(R"TS()TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, {f->Ranges()[0]});
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightMultilineTemplateStrings, TestDocumentHighlightMultilineTemplateStrings);

// documentHighlightNoCrashNestedRequireDestructure_test.go

// documentHighlightNoCrashNestedRequireDestructure_test.go
static void TestDocumentHighlightNestedRequireDestructureNoCrash1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /bar.js
const { a: { b } } = require('./foo');
/**/b;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightNestedRequireDestructureNoCrash1, TestDocumentHighlightNestedRequireDestructureNoCrash1);

// documentHighlightReferenceDirective_test.go

// documentHighlightReferenceDirective_test.go
static void TestDocumentHighlightReferenceDirective(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
/// <reference path="[|./b.ts|]" />

const x = 1;

// @filename: b.ts
export type Foo = number;
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, {f->Ranges()[0]});
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightReferenceDirective, TestDocumentHighlightReferenceDirective);

// documentHighlightRequirePropertyDestructure1_test.go

// documentHighlightRequirePropertyDestructure1_test.go
static void TestDocumentHighlightRequirePropertyDestructure1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @allowJs: true
// @Filename: /a.js
const { a } = require("m").f;
a/*m*/;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, {"m"});
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightRequirePropertyDestructure1, TestDocumentHighlightRequirePropertyDestructure1);

// documentHighlightTemplateStrings_test.go

// documentHighlightTemplateStrings_test.go
static void TestDocumentHighlightTemplateStrings(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const auto content = ((((((((std::string(R"TS(type Foo = "[|a|]" | "b";

class C {
   p: Foo = )TS") + "`") + std::string(R"TS([|a|])TS")) + std::string("`")) + std::string(R"TS(;
   m() {
       switch (this.p) {
           case )TS")) + std::string("`")) + std::string(R"TS([|a|])TS")) + std::string("`")) + std::string(R"TS(:
               return 1;
           case "b":
               return 2;
       }
   }
})TS"));
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, {f->Ranges()[2]});
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightTemplateStrings, TestDocumentHighlightTemplateStrings);

// documentHighlightTypeParameterConstraintExpressionNoCrash1_test.go

// documentHighlightTypeParameterConstraintExpressionNoCrash1_test.go
static void TestDocumentHighlightTypeParameterConstraintExpressionNoCrash1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
const v/*m*/alue = 1;
type Box<T extends +value> = typeof value)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, {"m"});
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightTypeParameterConstraintExpressionNoCrash1, TestDocumentHighlightTypeParameterConstraintExpressionNoCrash1);

// documentHighlightTypeofThis_test.go

// documentHighlightTypeofThis_test.go
static void TestDocumentHighlightTypeofThis(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @Filename: /a.ts
interface Foo {
  bar(): typeof [|this|];
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, {f->Ranges()[0]});
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightTypeofThis, TestDocumentHighlightTypeofThis);

// documentHighlightVarianceModifiers_test.go

// documentHighlightVarianceModifiers_test.go
static void TestDocumentHighlightVarianceModifiers(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type TFoo<Value> = { value: Value };
type TBar<[|in|] [|out|] Value> = TFoo<Value>;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightVarianceModifiers, TestDocumentHighlightVarianceModifiers);

// documentHighlightYield_test.go

// documentHighlightYield_test.go
static void TestDocumentHighlightYield(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
// @Filename: /a.ts
class C {
  async *[Symbol.asyncIterator]() {
    [|yield|] {
		type: 'type',
	};
  }
}
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, {f->Ranges()[0]});
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightYield, TestDocumentHighlightYield);

// documentHighlightsInvalidGlobalThis_test.go

// documentHighlightsInvalidGlobalThis_test.go
static void TestDocumentHighlightsInvalidGlobalThis(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(declare global {
    export { globalThis as [|global|] }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightsInvalidGlobalThis, TestDocumentHighlightsInvalidGlobalThis);

// documentHighlightsInvalidModifierLocations_test.go

// documentHighlightsInvalidModifierLocations_test.go
static void TestDocumentHighlightsInvalidModifierLocations(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
    m([|readonly|] p) {}
}
function f([|readonly|] p) {}

class D {
    m([|public|] p) {}
}
function g([|public|] p) {})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightsInvalidModifierLocations, TestDocumentHighlightsInvalidModifierLocations);

// documentHighlightsTypeParameterInHeritageClause01_test.go

// documentHighlightsTypeParameterInHeritageClause01_test.go
static void TestDocumentHighlightsTypeParameterInHeritageClause01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @lib: es5
interface I<[|T|]> extends I<[|T|]>, [|T|] {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->MarkTestAsStradaServer();
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlightsTypeParameterInHeritageClause01, TestDocumentHighlightsTypeParameterInHeritageClause01);

// documentHighlights_33722_test.go

// documentHighlights_33722_test.go
static void TestDocumentHighlights_33722(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /y.ts
class Foo {
  private foo() {}
}

const f = () => new Foo();
export default f;
// @Filename: /x.ts
import y from "./y";

y().[|foo|]();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlightsWithOptions(t, nullptr, std::vector<std::string>{"/x.ts"}, {f->Ranges()[0]});
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlights_33722, TestDocumentHighlights_33722);

// documentHighlights_40082_test.go

// documentHighlights_40082_test.go
static void TestDocumentHighlights_40082(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @checkJs: true
export = (state, messages) => {
   export [|default|] {
   }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, {f->Ranges()[0]});
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlights_40082, TestDocumentHighlights_40082);

// documentHighlights_filesToSearch_test.go

// documentHighlights_filesToSearch_test.go
static void TestDocumentHighlights_filesToSearch(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /a.ts
export const [|x|] = 0;
// @Filename: /b.ts
import { [|x|] } from "./a";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlights_filesToSearch, TestDocumentHighlights_filesToSearch);

// documentHighlights_moduleImport_filesToSearchWithInvalidFile_test.go

// documentHighlights_moduleImport_filesToSearchWithInvalidFile_test.go
static void TestDocumentHighlights_moduleImport_filesToSearchWithInvalidFile(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /node_modules/@types/foo/index.d.ts
export const x: number;
// @Filename: /a.ts
import * as foo from "foo";
foo.[|x|];
// @Filename: /b.ts
import { [|x|] } from "foo";
// @Filename: /c.ts
import { x } from "foo";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlightsWithOptions(t, nullptr, std::vector<std::string>{"/a.ts", "/b.ts", "/unknown.ts"}, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlights_moduleImport_filesToSearchWithInvalidFile, TestDocumentHighlights_moduleImport_filesToSearchWithInvalidFile);

// documentHighlights_moduleImport_filesToSearch_test.go

// documentHighlights_moduleImport_filesToSearch_test.go
static void TestDocumentHighlights_moduleImport_filesToSearch(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: /node_modules/@types/foo/index.d.ts
export const x: number;
// @Filename: /a.ts
import * as foo from "foo";
foo.[|x|];
// @Filename: /b.ts
import { [|x|] } from "foo";
// @Filename: /c.ts
import { x } from "foo";)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlightsWithOptions(t, nullptr, std::vector<std::string>{"/a.ts", "/b.ts"}, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlights_moduleImport_filesToSearch, TestDocumentHighlights_moduleImport_filesToSearch);

// documentHighlights_windowsPath_test.go

// documentHighlights_windowsPath_test.go
static void TestDocumentHighlights_windowsPath(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(//@Filename: C:\a\b\c.ts
var /*1*/[|x|] = 1;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlightsWithOptions(t, nullptr, std::vector<std::string>{f->Ranges()[0]->FileName()}, {f->Ranges()[0]});
	});
}
REGISTER_FOURSLASH_TEST(TestDocumentHighlights_windowsPath, TestDocumentHighlights_windowsPath);

// getOccurrencesAbstract01_test.go

// getOccurrencesAbstract01_test.go
static void TestGetOccurrencesAbstract01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|abstract|] class Animal {
    [|abstract|] prop1; // Does not compile
    [|abstract|] abstract();
    [|abstract|] walk(): void;
    [|abstract|] makeSound(): void;
}
// Abstract class below should not get highlighted
abstract class Foo {
    abstract foo(): void;
    abstract bar(): void;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesAbstract01, TestGetOccurrencesAbstract01);

// getOccurrencesAbstract02_test.go

// getOccurrencesAbstract02_test.go
static void TestGetOccurrencesAbstract02(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// Not valid TS (abstract methods can only appear in abstract classes)
class Animal {
    [|abstract|] walk(): void;
    [|abstract|] makeSound(): void;
}
// abstract cannot appear here, won't get highlighted
let c = /*1*/abstract class Foo {
    /*2*/abstract foo(): void;
    abstract bar(): void;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, {"1", "2"});
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesAbstract02, TestGetOccurrencesAbstract02);

// getOccurrencesAbstract03_test.go

// getOccurrencesAbstract03_test.go
static void TestGetOccurrencesAbstract03(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f() {
    [|abstract|] class A {
        [|abstract|] m(): void;
    }
    abstract class B {}
}
switch (0) {
    case 0:
        [|abstract|] class A { [|abstract|] m(): void; }
    default:
        [|abstract|] class B { [|abstract|] m(): void; }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesAbstract03, TestGetOccurrencesAbstract03);

// getOccurrencesAfterEdit_test.go

// getOccurrencesAfterEdit_test.go
static void TestGetOccurrencesAfterEdit(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*0*/
interface A {
    foo: string;
}
function foo(x: A) {
    x.f/*1*/oo
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, {"1"});
		f->GoToMarker(t, "0");
		f->Insert(t, R"TS(
)TS");
		f->VerifyBaselineDocumentHighlights(t, nullptr, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesAfterEdit, TestGetOccurrencesAfterEdit);

// getOccurrencesAsyncAwait2_test.go

// getOccurrencesAsyncAwait2_test.go
static void TestGetOccurrencesAsyncAwait2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|a/**/sync|] function f() {
 [|await|] 100;
 [|await|] [|await|] 200;
 return [|await|] async function () {
   await 300;
 }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesAsyncAwait2, TestGetOccurrencesAsyncAwait2);

// getOccurrencesAsyncAwait3_test.go

// getOccurrencesAsyncAwait3_test.go
static void TestGetOccurrencesAsyncAwait3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(a/**/wait 100;
async function f() {
    await 300;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesAsyncAwait3, TestGetOccurrencesAsyncAwait3);

// getOccurrencesAsyncAwait_test.go

// getOccurrencesAsyncAwait_test.go
static void TestGetOccurrencesAsyncAwait(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|async|] function f() {
 [|await|] 100;
 [|a/**/wait|] [|await|] 200;
class Foo {
    async memberFunction() {
        await 1;
    }
}
 return [|await|] async function () {
   await 300;
 }
}
async function g() {
    await 300;
    async function f() {
        await 400;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesAsyncAwait, TestGetOccurrencesAsyncAwait);

// getOccurrencesClassExpressionConstructor_test.go

// getOccurrencesClassExpressionConstructor_test.go
static void TestGetOccurrencesClassExpressionConstructor(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(let A = class Foo {
    [|constructor|]();
    [|constructor|](x: number);
    [|constructor|](y: string);
    [|constructor|](a?: any) {
    }
}

let B = class D {
    constructor(x: number) {
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesClassExpressionConstructor, TestGetOccurrencesClassExpressionConstructor);

// getOccurrencesClassExpressionPrivate_test.go

// getOccurrencesClassExpressionPrivate_test.go
static void TestGetOccurrencesClassExpressionPrivate(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(let A = class Foo {
    [|private|] foo;
    [|private|] private;
    constructor([|private|] y: string, public x: string) {
    }
    [|private|] method() { }
    public method2() { }
    [|private|] static static() { }
}

let B = class D {
    constructor(private x: number) {
    }
    private test() {}
    public test2() {}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesClassExpressionPrivate, TestGetOccurrencesClassExpressionPrivate);

// getOccurrencesClassExpressionPublic_test.go

// getOccurrencesClassExpressionPublic_test.go
static void TestGetOccurrencesClassExpressionPublic(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(let A = class Foo {
    [|public|] foo;
    [|public|] public;
    constructor([|public|] y: string, private x: string) {
    }
    [|public|] method() { }
    private method2() {}
    [|public|] static static() { }
}

let B = class D {
    constructor(private x: number) {
    }
    private test() {}
    public test2() {}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesClassExpressionPublic, TestGetOccurrencesClassExpressionPublic);

// getOccurrencesClassExpressionStaticThis_test.go

// getOccurrencesClassExpressionStaticThis_test.go
static void TestGetOccurrencesClassExpressionStaticThis(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var x = class C {
    public x;
    public y;
    public z;
    public staticX;
    constructor() {
        this;
        this.x;
        this.y;
        this.z;
    }
    foo() {
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
        return this.x;
    }

    static bar() {
        [|this|];
        [|this|].staticX;
        () => [|this|];
        () => {
            if ([|this|]) {
                [|this|];
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesClassExpressionStaticThis, TestGetOccurrencesClassExpressionStaticThis);

// getOccurrencesClassExpressionStatic_test.go

// getOccurrencesClassExpressionStatic_test.go
static void TestGetOccurrencesClassExpressionStatic(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(let A = class Foo {
    public [|static|] foo;
    [|static|] a;
    constructor(public y: string, private x: string) {
    }
    public method() { }
    private method2() {}
    public [|static|] static() { }
    private [|static|] static2() { }
}

let B = class D {
    static a;
    constructor(private x: number) {
    }
    private static test() {}
    public static test2() {}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesClassExpressionStatic, TestGetOccurrencesClassExpressionStatic);

// getOccurrencesClassExpressionThis_test.go

// getOccurrencesClassExpressionThis_test.go
static void TestGetOccurrencesClassExpressionThis(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var x = class C {
    public x;
    public y;
    public z;
    constructor() {
        [|this|];
        [|this|].x;
        [|this|].y;
        [|this|].z;
    }
    foo() {
        [|this|];
        () => [|this|];
        () => {
            if ([|this|]) {
                [|this|];
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
        return [|this|].x;
    }

    static bar() {
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesClassExpressionThis, TestGetOccurrencesClassExpressionThis);

// getOccurrencesConst01_test.go

// getOccurrencesConst01_test.go
static void TestGetOccurrencesConst01(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|const|] enum E1 {
    v1,
    v2
}

/*2*/const c = 0;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
		f->VerifyBaselineDocumentHighlights(t, nullptr, {"2"});
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesConst01, TestGetOccurrencesConst01);

// getOccurrencesConst02_test.go

// getOccurrencesConst02_test.go
static void TestGetOccurrencesConst02(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace m {
    declare /*1*/const x;
    declare [|const|] enum E {
    }
}

declare /*2*/const x;
declare [|const|] enum E {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Markers())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesConst02, TestGetOccurrencesConst02);

// getOccurrencesConst03_test.go

// getOccurrencesConst03_test.go
static void TestGetOccurrencesConst03(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace m {
    export /*1*/const x;
    export [|const|] enum E {
    }
}

export /*2*/const x;
export [|const|] enum E {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Markers())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesConst03, TestGetOccurrencesConst03);

// getOccurrencesConst04_test.go

// getOccurrencesConst04_test.go
static void TestGetOccurrencesConst04(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(export const class C {
    private static c/*1*/onst f/*2*/oo;
    constructor(public con/*3*/st foo) {
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesConst04, TestGetOccurrencesConst04);

// getOccurrencesConstructor2_test.go

// getOccurrencesConstructor2_test.go
static void TestGetOccurrencesConstructor2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
    constructor();
    constructor(x: number);
    constructor(y: string, x: number);
    constructor(a?: any, ...r: any[]) {
        if (a === undefined && r.length === 0) {
            return;
        }

        return;
    }
}

class D {
    [|con/**/structor|](public x: number, public y: number) {
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesConstructor2, TestGetOccurrencesConstructor2);

// getOccurrencesConstructor_test.go

// getOccurrencesConstructor_test.go
static void TestGetOccurrencesConstructor(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
    [|const/**/ructor|]();
    [|constructor|](x: number);
    [|constructor|](y: string, x: number);
    [|constructor|](a?: any, ...r: any[]) {
        if (a === undefined && r.length === 0) {
            return;
        }

        return;
    }
}

class D {
    constructor(public x: number, public y: number) {
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesConstructor, TestGetOccurrencesConstructor);

// getOccurrencesDeclare1_test.go

// getOccurrencesDeclare1_test.go
static void TestGetOccurrencesDeclare1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace m {
    export class C1 {
        public pub1;
        public pub2;
        private priv1;
        private priv2;
        protected prot1;
        protected prot2;

        public public;
        private private;
        protected protected;

        public constructor(public a, private b, protected c, public d, private e, protected f) {
            this.public = 10;
            this.private = 10;
            this.protected = 10;
        }

        public get x() { return 10; }
        public set x(value) { }

        public static statPub;
        private static statPriv;
        protected static statProt;
    }

    export interface I1 {
    }

    export [|declare|] namespace ma.m1.m2.m3 {
        interface I2 {
        }
    }

    export namespace mb.m1.m2.m3 {
        declare var foo;

        export class C2 {
            public pub1;
            private priv1;
            protected prot1;

            protected constructor(public public, protected protected, private private) {
            }
        }
    }

    [|declare|] var ambientThing: number;
    export var exportedThing = 10;
    [|declare|] function foo(): string;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesDeclare1, TestGetOccurrencesDeclare1);

// getOccurrencesDeclare2_test.go

// getOccurrencesDeclare2_test.go
static void TestGetOccurrencesDeclare2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace m {
    export class C1 {
        public pub1;
        public pub2;
        private priv1;
        private priv2;
        protected prot1;
        protected prot2;

        public public;
        private private;
        protected protected;

        public constructor(public a, private b, protected c, public d, private e, protected f) {
            this.public = 10;
            this.private = 10;
            this.protected = 10;
        }

        public get x() { return 10; }
        public set x(value) { }

        public static statPub;
        private static statPriv;
        protected static statProt;
    }

    export interface I1 {
    }

    export declare namespace ma.m1.m2.m3 {
        interface I2 {
        }
    }

    export namespace mb.m1.m2.m3 {
        [|declare|] var foo;

        export class C2 {
            public pub1;
            private priv1;
            protected prot1;

            protected constructor(public public, protected protected, private private) {
                public = private = protected;
            }
        }
    }

    declare var ambientThing: number;
    export var exportedThing = 10;
    declare function foo(): string;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesDeclare2, TestGetOccurrencesDeclare2);

// getOccurrencesDeclare3_test.go

// getOccurrencesDeclare3_test.go
static void TestGetOccurrencesDeclare3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
[|declare|] var x;
export [|declare|] var y, z;

namespace m {
    export class C1 {
        public pub1;
        public pub2;
        private priv1;
        private priv2;
        protected prot1;
        protected prot2;

        public public;
        private private;
        protected protected;

        public constructor(public a, private b, protected c, public d, private e, protected f) {
            this.public = 10;
            this.private = 10;
            this.protected = 10;
        }

        public get x() { return 10; }
        public set x(value) { }

        public static statPub;
        private static statPriv;
        protected static statProt;
    }

    export interface I1 {
    }

    export declare namespace ma.m1.m2.m3 {
        interface I2 {
        }
    }

    export namespace mb.m1.m2.m3 {
        declare var foo;

        export class C2 {
            public pub1;
            private priv1;
            protected prot1;

            protected constructor(public public, protected protected, private private) {
            }
        }
    }

    declare var ambientThing: number;
    export var exportedThing = 10;
    declare function foo(): string;
}

[|declare|] export var v1, v2;
[|declare|] namespace dm { }
export class EC { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesDeclare3, TestGetOccurrencesDeclare3);

// getOccurrencesExport1_test.go

// getOccurrencesExport1_test.go
static void TestGetOccurrencesExport1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace m {
    [|export|] class C1 {
        public pub1;
        public pub2;
        private priv1;
        private priv2;
        protected prot1;
        protected prot2;

        public public;
        private private;
        protected protected;

        public constructor(public a, private b, protected c, public d, private e, protected f) {
            this.public = 10;
            this.private = 10;
            this.protected = 10;
        }

        public get x() { return 10; }
        public set x(value) { }

        public static statPub;
        private static statPriv;
        protected static statProt;
    }

    [|export|] interface I1 {
    }

    [|export|] declare namespace ma.m1.m2.m3 {
        interface I2 {
        }
    }

    [|export|] namespace mb.m1.m2.m3 {
        declare var foo;

        export class C2 {
            public pub1;
            private priv1;
            protected prot1;

            protected constructor(public public, protected protected, private private) {
            }
        }
    }

    declare var ambientThing: number;
    [|export|] var exportedThing = 10;
    declare function foo(): string;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesExport1, TestGetOccurrencesExport1);

// getOccurrencesExport2_test.go

// getOccurrencesExport2_test.go
static void TestGetOccurrencesExport2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace m {
    export class C1 {
        public pub1;
        public pub2;
        private priv1;
        private priv2;
        protected prot1;
        protected prot2;

        public public;
        private private;
        protected protected;

        public constructor(public a, private b, protected c, public d, private e, protected f) {
            this.public = 10;
            this.private = 10;
            this.protected = 10;
        }

        public get x() { return 10; }
        public set x(value) { }

        public static statPub;
        private static statPriv;
        protected static statProt;
    }

    export interface I1 {
    }

    export declare namespace ma.m1.m2.m3 {
        interface I2 {
        }
    }

    export namespace mb.m1.m2.m3 {
        declare var foo;

        [|export|] class C2 {
            public pub1;
            private priv1;
            protected prot1;

            protected constructor(public public, protected protected, private private) {
                public = private = protected;
            }
        }
    }

    declare var ambientThing: number;
    export var exportedThing = 10;
    declare function foo(): string;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesExport2, TestGetOccurrencesExport2);

// getOccurrencesExport3_test.go

// getOccurrencesExport3_test.go
static void TestGetOccurrencesExport3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(
declare var x;
[|export|] declare var y, z;

namespace m {
    export class C1 {
        public pub1;
        public pub2;
        private priv1;
        private priv2;
        protected prot1;
        protected prot2;

        public public;
        private private;
        protected protected;

        public constructor(public a, private b, protected c, public d, private e, protected f) {
            this.public = 10;
            this.private = 10;
            this.protected = 10;
        }

        public get x() { return 10; }
        public set x(value) { }

        public static statPub;
        private static statPriv;
        protected static statProt;
    }

    export interface I1 {
    }

    export declare namespace ma.m1.m2.m3 {
        interface I2 {
        }
    }

    export namespace mb.m1.m2.m3 {
        declare var foo;

        export class C2 {
            public pub1;
            private priv1;
            protected prot1;

            protected constructor(public public, protected protected, private private) {
            }
        }
    }

    declare var ambientThing: number;
    export var exportedThing = 10;
    declare function foo(): string;
}

declare [|export|] var v1, v2;
declare namespace dm { }
[|export|] class EC { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesExport3, TestGetOccurrencesExport3);

// getOccurrencesIfElse2_test.go

// getOccurrencesIfElse2_test.go
static void TestGetOccurrencesIfElse2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(if (true) {
    [|if|] (false) {
    }
    [|else|]{
    }
    if (true) {
    }
    else {
        if (false)
            if (true)
                var x = undefined;
    }
}
else            if (null) {
}
else /* whar garbl */ if (undefined) {
}
else
if (false) {
}
else { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesIfElse2, TestGetOccurrencesIfElse2);

// getOccurrencesIfElse3_test.go

// getOccurrencesIfElse3_test.go
static void TestGetOccurrencesIfElse3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(if (true) {
    if (false) {
    }
    else {
    }
    [|if|] (true) {
    }
    [|else|] {
        if (false)
            if (true)
                var x = undefined;
    }
}
else            if (null) {
}
else /* whar garbl */ if (undefined) {
}
else
if (false) {
}
else { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesIfElse3, TestGetOccurrencesIfElse3);

// getOccurrencesIfElse4_test.go

// getOccurrencesIfElse4_test.go
static void TestGetOccurrencesIfElse4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(if (true) {
    if (false) {
    }
    else {
    }
    if (true) {
    }
    else {
        /*1*/if (false)
            /*2*/i/*3*/f (true)
                var x = undefined;
    }
}
else            if (null) {
}
else /* whar garbl */ if (undefined) {
}
else
if (false) {
}
else { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Markers())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesIfElse4, TestGetOccurrencesIfElse4);

// getOccurrencesIfElse5_test.go

// getOccurrencesIfElse5_test.go
static void TestGetOccurrencesIfElse5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(if/*1*/ (true) {
    if/*2*/ (false) {
    }
    else/*3*/ {
    }
    if/*4*/ (true) {
    }
    else/*5*/ {
        if/*6*/ (false)
            if/*7*/ (true)
                var x = undefined;
    }
}
else/*8*/            if (null) {
}
else/*9*/ /* whar garbl */ if/*10*/ (undefined) {
}
else/*11*/
if/*12*/ (false) {
}
else/*13*/ { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Markers())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesIfElse5, TestGetOccurrencesIfElse5);

// getOccurrencesIfElseBroken_test.go

// getOccurrencesIfElseBroken_test.go
static void TestGetOccurrencesIfElseBroken(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|if|] (true) {
    var x = 1;
}
[|else     if|] ()
[|else if|]
[|else|]  /*  whar garbl   */   [|if|] (i/**/f (true) { } else { })
else)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
		f->VerifyBaselineDocumentHighlights(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesIfElseBroken, TestGetOccurrencesIfElseBroken);

// getOccurrencesIfElse_test.go

// getOccurrencesIfElse_test.go
static void TestGetOccurrencesIfElse(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|if|] (true) {
    if (false) {
    }
    else {
    }
    if (true) {
    }
    else {
        if (false)
            if (true)
                var x = undefined;
    }
}
[|else            i/**/f|] (null) {
}
[|else|] /* whar garbl */ [|if|] (undefined) {
}
[|else|]
[|if|] (false) {
}
[|else|] { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesIfElse, TestGetOccurrencesIfElse);

// getOccurrencesIsDefinitionOfClass_test.go

// getOccurrencesIsDefinitionOfClass_test.go
static void TestGetOccurrencesIsDefinitionOfClass(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/class /*2*/C {
    n: number;
    constructor() {
        this.n = 12;
    }
}
let c = new /*3*/C();)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesIsDefinitionOfClass, TestGetOccurrencesIsDefinitionOfClass);

// getOccurrencesIsDefinitionOfComputedProperty_test.go

// getOccurrencesIsDefinitionOfComputedProperty_test.go
static void TestGetOccurrencesIsDefinitionOfComputedProperty(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(let o = { /*1*/["/*2*/foo"]: 12 };
let y = o./*3*/foo;
let z = o['/*4*/foo'];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4"});
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesIsDefinitionOfComputedProperty, TestGetOccurrencesIsDefinitionOfComputedProperty);

// getOccurrencesIsDefinitionOfEnum_test.go

// getOccurrencesIsDefinitionOfEnum_test.go
static void TestGetOccurrencesIsDefinitionOfEnum(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/enum /*2*/E {
    First,
    Second
}
let first = /*3*/E.First;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesIsDefinitionOfEnum, TestGetOccurrencesIsDefinitionOfEnum);

// getOccurrencesIsDefinitionOfExport_test.go

// getOccurrencesIsDefinitionOfExport_test.go
static void TestGetOccurrencesIsDefinitionOfExport(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: m.ts
export var /*1*/x = 12;
// @Filename: main.ts
import { /*2*/x } from "./m";
const y = x;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesIsDefinitionOfExport, TestGetOccurrencesIsDefinitionOfExport);

// getOccurrencesIsDefinitionOfInterfaceClassMerge_test.go

// getOccurrencesIsDefinitionOfInterfaceClassMerge_test.go
static void TestGetOccurrencesIsDefinitionOfInterfaceClassMerge(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/interface /*2*/Numbers {
    p: number;
}
/*3*/interface /*4*/Numbers {
    m: number;
}
/*5*/class /*6*/Numbers {
    f(n: number) {
        return this.p + this.m + n;
    }
}
let i: /*7*/Numbers = new /*8*/Numbers();
let x = i.f(i.p + i.m);)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5", "6", "7", "8"});
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesIsDefinitionOfInterfaceClassMerge, TestGetOccurrencesIsDefinitionOfInterfaceClassMerge);

// getOccurrencesIsDefinitionOfInterface_test.go

// getOccurrencesIsDefinitionOfInterface_test.go
static void TestGetOccurrencesIsDefinitionOfInterface(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/interface /*2*/I {
    p: number;
}
let i: /*3*/I = { p: 12 };)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesIsDefinitionOfInterface, TestGetOccurrencesIsDefinitionOfInterface);

// getOccurrencesIsDefinitionOfNamespace_test.go

// getOccurrencesIsDefinitionOfNamespace_test.go
static void TestGetOccurrencesIsDefinitionOfNamespace(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/namespace /*2*/Numbers {
    export var n = 12;
}
let x = /*3*/Numbers.n + 1;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3"});
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesIsDefinitionOfNamespace, TestGetOccurrencesIsDefinitionOfNamespace);

// getOccurrencesIsDefinitionOfParameter_test.go

// getOccurrencesIsDefinitionOfParameter_test.go
static void TestGetOccurrencesIsDefinitionOfParameter(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f(/*1*/x: number) {
  return /*2*/x + 1
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2"});
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesIsDefinitionOfParameter, TestGetOccurrencesIsDefinitionOfParameter);

// getOccurrencesIsDefinitionOfVariable_test.go

// getOccurrencesIsDefinitionOfVariable_test.go
static void TestGetOccurrencesIsDefinitionOfVariable(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/var /*2*/x = 0;
var assignmentRightHandSide = /*3*/x;
var assignmentRightHandSide2 = 1 + /*4*/x;

/*5*/x = 1;
/*6*/x = /*7*/x + /*8*/x;

/*9*/x == 1;
/*10*/x <= 1;

var preIncrement = ++/*11*/x;
var postIncrement = /*12*/x++;
var preDecrement = --/*13*/x;
var postDecrement = /*14*/x--;

/*15*/x += 1;
/*16*/x <<= 1;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineFindAllReferences(t, {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"});
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesIsDefinitionOfVariable, TestGetOccurrencesIsDefinitionOfVariable);

// getOccurrencesIsWriteAccess_test.go

// getOccurrencesIsWriteAccess_test.go
static void TestGetOccurrencesIsWriteAccess(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var [|{| "isWriteAccess": true |}x|] = 0;
var assignmentRightHandSide = [|{| "isWriteAccess": false |}x|];
var assignmentRightHandSide2 = 1 + [|{| "isWriteAccess": false |}x|];

[|{| "isWriteAccess": true |}x|] = 1;
[|{| "isWriteAccess": true |}x|] = [|{| "isWriteAccess": false |}x|] + [|{| "isWriteAccess": false |}x|];

[|{| "isWriteAccess": false |}x|] == 1;
[|{| "isWriteAccess": false |}x|] <= 1;

var preIncrement = ++[|{| "isWriteAccess": true |}x|];
var postIncrement = [|{| "isWriteAccess": true |}x|]++;
var preDecrement = --[|{| "isWriteAccess": true |}x|];
var postDecrement = [|{| "isWriteAccess": true |}x|]--;

[|{| "isWriteAccess": true |}x|] += 1;
[|{| "isWriteAccess": true |}x|] <<= 1;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, {f->Ranges()[0]});
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesIsWriteAccess, TestGetOccurrencesIsWriteAccess);

// getOccurrencesLoopBreakContinue2_test.go

// getOccurrencesLoopBreakContinue2_test.go
static void TestGetOccurrencesLoopBreakContinue2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var arr = [1, 2, 3, 4];
label1: for (var n in arr) {
    break;
    continue;
    break label1;
    continue label1;

    label2: [|f/**/or|] (var i = 0; i < arr[n]; i++) {
        break label1;
        continue label1;

        [|break|];
        [|continue|];
        [|break|] label2;
        [|continue|] label2;

        function foo() {
            label3: while (true) {
                break;
                continue;
                break label3;
                continue label3;

                // these cross function boundaries
                break label1;
                continue label1;
                break label2;
                continue label2;

                label4: do {
                    break;
                    continue;
                    break label4;
                    continue label4;

                    break label3;
                    continue label3;

                    switch (10) {
                        case 1:
                        case 2:
                            break;
                            break label4;
                        default:
                            continue;
                    }

                    // these cross function boundaries
                    break label1;
                    continue label1;
                    break label2;
                    continue label2;
                    () => { break;
                } while (true)
            }
        }
    }
}

label5: while (true) break label5;

label7: while (true) continue label5;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesLoopBreakContinue2, TestGetOccurrencesLoopBreakContinue2);

// getOccurrencesLoopBreakContinue3_test.go

// getOccurrencesLoopBreakContinue3_test.go
static void TestGetOccurrencesLoopBreakContinue3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var arr = [1, 2, 3, 4];
label1: for (var n in arr) {
    break;
    continue;
    break label1;
    continue label1;

    label2: for (var i = 0; i < arr[n]; i++) {
        break label1;
        continue label1;

        break;
        continue;
        break label2;
        continue label2;

        function foo() {
            label3: [|w/**/hile|] (true) {
                [|break|];
                [|continue|];
                [|break|] label3;
                [|continue|] label3;

                // these cross function boundaries
                break label1;
                continue label1;
                break label2;
                continue label2;

                label4: do {
                    break;
                    continue;
                    break label4;
                    continue label4;

                    [|break|] label3;
                    [|continue|] label3;

                    switch (10) {
                        case 1:
                        case 2:
                            break;
                            break label4;
                        default:
                            continue;
                    }

                    // these cross function boundaries
                    break label1;
                    continue label1;
                    break label2;
                    continue label2;
                    () => { break; }
                } while (true)
            }
        }
    }
}

label5: while (true) break label5;

label7: while (true) continue label5;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesLoopBreakContinue3, TestGetOccurrencesLoopBreakContinue3);

// getOccurrencesLoopBreakContinue4_test.go

// getOccurrencesLoopBreakContinue4_test.go
static void TestGetOccurrencesLoopBreakContinue4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var arr = [1, 2, 3, 4];
label1: for (var n in arr) {
    break;
    continue;
    break label1;
    continue label1;

    label2: for (var i = 0; i < arr[n]; i++) {
        break label1;
        continue label1;

        break;
        continue;
        break label2;
        continue label2;

        function foo() {
            label3: while (true) {
                break;
                continue;
                break label3;
                continue label3;

                // these cross function boundaries
                break label1;
                continue label1;
                break label2;
                continue label2;

                label4: [|do|] {
                    [|break|];
                    [|continue|];
                    [|break|] label4;
                    [|continue|] label4;

                    break label3;
                    continue label3;

                    switch (10) {
                        case 1:
                        case 2:
                            break;
                            [|break|] label4;
                        default:
                            [|continue|];
                    }

                    // these cross function boundaries
                    break label1;
                    continue label1;
                    break label2;
                    continue label2;
                    () => { break; }
                } [|wh/**/ile|] (true)
            }
        }
    }
}

label5: while (true) break label5;

label7: while (true) continue label5;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesLoopBreakContinue4, TestGetOccurrencesLoopBreakContinue4);

// getOccurrencesLoopBreakContinue5_test.go

// getOccurrencesLoopBreakContinue5_test.go
static void TestGetOccurrencesLoopBreakContinue5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var arr = [1, 2, 3, 4];
label1: for (var n in arr) {
    break;
    continue;
    break label1;
    continue label1;

    label2: for (var i = 0; i < arr[n]; i++) {
        break label1;
        continue label1;

        break;
        continue;
        break label2;
        continue label2;

        function foo() {
            label3: while (true) {
                break;
                continue;
                break label3;
                continue label3;

                // these cross function boundaries
                break label1;
                continue label1;
                break label2;
                continue label2;

                label4: do {
                    break;
                    continue;
                    break label4;
                    continue label4;

                    break label3;
                    continue label3;

                    switch (10) {
                        case 1:
                        case 2:
                            break;
                            break label4;
                        default:
                            continue;
                    }

                    // these cross function boundaries
                    break label1;
                    continue label1;
                    break label2;
                    continue label2;
                    () => { break; }
                } while (true)
            }
        }
    }
}

label5: [|while|] (true) [|br/**/eak|] label5;

label7: while (true) continue label5;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesLoopBreakContinue5, TestGetOccurrencesLoopBreakContinue5);

// getOccurrencesLoopBreakContinue6_test.go

// getOccurrencesLoopBreakContinue6_test.go
static void TestGetOccurrencesLoopBreakContinue6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var arr = [1, 2, 3, 4];
label1: for (var n in arr) {
    break;
    continue;
    break label1;
    continue label1;

    label2: for (var i = 0; i < arr[n]; i++) {
        break label1;
        continue label1;

        break;
        continue;
        break label2;
        continue label2;

        function foo() {
            label3: while (true) {
                break;
                continue;
                break label3;
                continue label3;

                // these cross function boundaries
                br/*1*/eak label1;
                cont/*2*/inue label1;
                bre/*3*/ak label2;
                c/*4*/ontinue label2;

                label4: do {
                    break;
                    continue;
                    break label4;
                    continue label4;

                    break label3;
                    continue label3;

                    switch (10) {
                        case 1:
                        case 2:
                            break;
                            break label4;
                        default:
                            continue;
                    }

                    // these cross function boundaries
                    br/*5*/eak label1;
                    co/*6*/ntinue label1;
                    br/*7*/eak label2;
                    con/*8*/tinue label2;
                    () => { b/*9*/reak; }
                } while (true)
            }
        }
    }
}

label5: while (true) break label5;

label7: while (true) co/*10*/ntinue label5;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Markers())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesLoopBreakContinue6, TestGetOccurrencesLoopBreakContinue6);

// getOccurrencesLoopBreakContinueNegatives_test.go

// getOccurrencesLoopBreakContinueNegatives_test.go
static void TestGetOccurrencesLoopBreakContinueNegatives(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var arr = [1, 2, 3, 4];
label1: for (var n in arr) {
    break;
    continue;
    break label1;
    continue label1;

    label2: for (var i = 0; i < arr[n]; i++) {
        break label1;
        continue label1;

        break;
        continue;
        break label2;
        continue label2;

        function foo() {
            label3: while (true) {
                break;
                continue;
                break label3;
                continue label3;

                // these cross function boundaries
                br/*1*/eak label1;
                cont/*2*/inue label1;
                bre/*3*/ak label2;
                c/*4*/ontinue label2;

                label4: do {
                    break;
                    continue;
                    break label4;
                    continue label4;

                    break label3;
                    continue label3;

                    switch (10) {
                        case 1:
                        case 2:
                            break;
                            break label4;
                        default:
                            continue;
                    }

                    // these cross function boundaries
                    br/*5*/eak label1;
                    co/*6*/ntinue label1;
                    br/*7*/eak label2;
                    con/*8*/tinue label2;
                    () => { b/*9*/reak; }
                } while (true)
            }
        }
    }
}

label5: while (true) break label5;

label7: while (true) co/*10*/ntinue label5;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Markers())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesLoopBreakContinueNegatives, TestGetOccurrencesLoopBreakContinueNegatives);

// getOccurrencesLoopBreakContinue_test.go

// getOccurrencesLoopBreakContinue_test.go
static void TestGetOccurrencesLoopBreakContinue(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var arr = [1, 2, 3, 4];
label1: [|for|] (var n in arr) {
    [|break|];
    [|continue|];
    [|br/**/eak|] label1;
    [|continue|] label1;

    label2: for (var i = 0; i < arr[n]; i++) {
        [|break|] label1;
        [|continue|] label1;

        break;
        continue;
        break label2;
        continue label2;

        function foo() {
            label3: while (true) {
                break;
                continue;
                break label3;
                continue label3;

                // these cross function boundaries
                break label1;
                continue label1;
                break label2;
                continue label2;

                label4: do {
                    break;
                    continue;
                    break label4;
                    continue label4;

                    break label3;
                    continue label3;

                    switch (10) {
                        case 1:
                        case 2:
                            break;
                            break label4;
                        default:
                            continue;
                    }

                    // these cross function boundaries
                    break label1;
                    continue label1;
                    break label2;
                    continue label2;
                    () => { break; }
                } while (true)
            }
        }
    }
}

label5: while (true) break label5;

label7: while (true) continue label5;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesLoopBreakContinue, TestGetOccurrencesLoopBreakContinue);

// getOccurrencesModifiersNegatives1_test.go

// getOccurrencesModifiersNegatives1_test.go
static void TestGetOccurrencesModifiersNegatives1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
    [|{| "count": 3 |}export|] foo;
    [|{| "count": 3 |}declare|] bar;
    [|{| "count": 3 |}export|] [|{| "count": 3 |}declare|] foobar;
    [|{| "count": 3 |}declare|] [|{| "count": 3 |}export|] barfoo;

    constructor([|{| "count": 9 |}export|] conFoo,
                [|{| "count": 9 |}declare|] conBar,
                [|{| "count": 9 |}export|] [|{| "count": 9 |}declare|] conFooBar,
                [|{| "count": 9 |}declare|] [|{| "count": 9 |}export|] conBarFoo,
                [|{| "count": 4 |}static|] sue,
                [|{| "count": 4 |}static|] [|{| "count": 9 |}export|] [|{| "count": 9 |}declare|] sueFooBar,
                [|{| "count": 4 |}static|] [|{| "count": 9 |}declare|] [|{| "count": 9 |}export|] sueBarFoo,
                [|{| "count": 9 |}declare|] [|{| "count": 4 |}static|] [|{| "count": 9 |}export|] barSueFoo) {
    }
}

namespace m {
    [|{| "count": 0 |}static|] a;
    [|{| "count": 0 |}public|] b;
    [|{| "count": 0 |}private|] c;
    [|{| "count": 0 |}protected|] d;
    [|{| "count": 0 |}static|] [|{| "count": 0 |}public|] [|{| "count": 0 |}private|] [|{| "count": 0 |}protected|] e;
    [|{| "count": 0 |}public|] [|{| "count": 0 |}static|] [|{| "count": 0 |}protected|] [|{| "count": 0 |}private|] f;
    [|{| "count": 0 |}protected|] [|{| "count": 0 |}static|] [|{| "count": 0 |}public|] g;
}
[|{| "count": 0 |}static|] a;
[|{| "count": 0 |}public|] b;
[|{| "count": 0 |}private|] c;
[|{| "count": 0 |}protected|] d;
[|{| "count": 0 |}static|] [|{| "count": 0 |}public|] [|{| "count": 0 |}private|] [|{| "count": 0 |}protected|] e;
[|{| "count": 0 |}public|] [|{| "count": 0 |}static|] [|{| "count": 0 |}protected|] [|{| "count": 0 |}private|] f;
[|{| "count": 0 |}protected|] [|{| "count": 0 |}static|] [|{| "count": 0 |}public|] g;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesModifiersNegatives1, TestGetOccurrencesModifiersNegatives1);

// getOccurrencesNonStringImportAssertion_test.go

// getOccurrencesNonStringImportAssertion_test.go
static void TestGetOccurrencesNonStringImportAssertion(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
import * as react from "react" with { cache: /**/0 };
react.Children;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesNonStringImportAssertion, TestGetOccurrencesNonStringImportAssertion);

// getOccurrencesNonStringImportAttributes_test.go

// getOccurrencesNonStringImportAttributes_test.go
static void TestGetOccurrencesNonStringImportAttributes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @module: node18
import * as react from "react" with { cache: /**/0 };
react.Children;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesNonStringImportAttributes, TestGetOccurrencesNonStringImportAttributes);

// getOccurrencesOfAnonymousFunction2_test.go

// getOccurrencesOfAnonymousFunction2_test.go
static void TestGetOccurrencesOfAnonymousFunction2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(//global foo definition
function foo() {}

(function f/*local*/oo(): number {
    return foo(); // local foo reference
})
//global foo references
fo/*global*/o();
var f = foo;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, {"local", "global"});
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesOfAnonymousFunction2, TestGetOccurrencesOfAnonymousFunction2);

// getOccurrencesOfAnonymousFunction_test.go

// getOccurrencesOfAnonymousFunction_test.go
static void TestGetOccurrencesOfAnonymousFunction(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS((function [|foo|](): number {
    var x = [|foo|];
    return 0;
}))TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesOfAnonymousFunction, TestGetOccurrencesOfAnonymousFunction);

// getOccurrencesOfDecorators_test.go

// getOccurrencesOfDecorators_test.go
static void TestGetOccurrencesOfDecorators(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(// @Filename: b.ts
@/*1*/decorator
class C {
    @decorator
    method() {}
}
function decorator(target) {
    return target;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, {"1"});
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesOfDecorators, TestGetOccurrencesOfDecorators);

// getOccurrencesOfUndefinedSymbol_test.go

// getOccurrencesOfUndefinedSymbol_test.go
static void TestGetOccurrencesOfUndefinedSymbol(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var obj1: {
    (bar: any): any;
    new (bar: any): any;
    [bar: any]: any;
    bar: any;
    foob(bar: any): any;
};

class cls3 {
    property zeFunc() {
    super.ceFun/**/c();
}
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesOfUndefinedSymbol, TestGetOccurrencesOfUndefinedSymbol);

// getOccurrencesPrivate1_test.go

// getOccurrencesPrivate1_test.go
static void TestGetOccurrencesPrivate1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace m {
    export class C1 {
        public pub1;
        public pub2;
        [|private|] priv1;
        [|private|] priv2;
        protected prot1;
        protected prot2;

        public public;
        [|private|] private;
        protected protected;

        public constructor(public a, [|private|] b, protected c, public d, [|private|] e, protected f) {
            this.public = 10;
            this.private = 10;
            this.protected = 10;
        }

        public get x() { return 10; }
        public set x(value) { }

        public static statPub;
        [|private|] static statPriv;
        protected static statProt;
    }

    export interface I1 {
    }

    export declare namespace ma.m1.m2.m3 {
        interface I2 {
        }
    }

    export namespace mb.m1.m2.m3 {
        declare var foo;

        export class C2 {
            public pub1;
            private priv1;
            protected prot1;

            protected constructor(public public, protected protected, private private) {
                public = private = protected;
            }
        }
    }

    declare var ambientThing: number;
    export var exportedThing = 10;
    declare function foo(): string;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesPrivate1, TestGetOccurrencesPrivate1);

// getOccurrencesPrivate2_test.go

// getOccurrencesPrivate2_test.go
static void TestGetOccurrencesPrivate2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace m {
    export class C1 {
        public pub1;
        public pub2;
        private priv1;
        private priv2;
        protected prot1;
        protected prot2;

        public public;
        private private;
        protected protected;

        public constructor(public a, private b, protected c, public d, private e, protected f) {
            this.public = 10;
            this.private = 10;
            this.protected = 10;
        }

        public get x() { return 10; }
        public set x(value) { }

        public static statPub;
        private static statPriv;
        protected static statProt;
    }

    export interface I1 {
    }

    export declare namespace ma.m1.m2.m3 {
        interface I2 {
        }
    }

    export namespace mb.m1.m2.m3 {
        declare var foo;

        export class C2 {
            public pub1;
            [|private|] priv1;
            protected prot1;

            protected constructor(public public, protected protected, [|private|] private) {
                public = private = protected;
            }
        }
    }

    declare var ambientThing: number;
    export var exportedThing = 10;
    declare function foo(): string;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesPrivate2, TestGetOccurrencesPrivate2);

// getOccurrencesPropertyInAliasedInterface_test.go

// getOccurrencesPropertyInAliasedInterface_test.go
static void TestGetOccurrencesPropertyInAliasedInterface(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace m {
    export interface Foo {
        [|abc|]
    }
}

import Bar = m.Foo;

export interface I extends Bar {
    [|abc|]
}

class C implements Bar {
    [|abc|]
}

(new C()).[|abc|];)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesPropertyInAliasedInterface, TestGetOccurrencesPropertyInAliasedInterface);

// getOccurrencesProtected1_test.go

// getOccurrencesProtected1_test.go
static void TestGetOccurrencesProtected1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace m {
    export class C1 {
        public pub1;
        public pub2;
        private priv1;
        private priv2;
        [|protected|] prot1;
        [|protected|] prot2;

        public public;
        private private;
        [|protected|] protected;

        public constructor(public a, private b, [|protected|] c, public d, private e, [|protected|] f) {
            this.public = 10;
            this.private = 10;
            this.protected = 10;
        }

        public get x() { return 10; }
        public set x(value) { }

        public static statPub;
        private static statPriv;
        [|protected|] static statProt;
    }

    export interface I1 {
    }

    export declare namespace ma.m1.m2.m3 {
        interface I2 {
        }
    }

    export namespace mb.m1.m2.m3 {
        declare var foo;

        export class C2 {
            public pub1;
            private priv1;
            protected prot1;

            protected constructor(public public, protected protected, private private) {
                public = private = protected;
            }
        }
    }

    declare var ambientThing: number;
    export var exportedThing = 10;
    declare function foo(): string;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesProtected1, TestGetOccurrencesProtected1);

// getOccurrencesProtected2_test.go

// getOccurrencesProtected2_test.go
static void TestGetOccurrencesProtected2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace m {
    export class C1 {
        public pub1;
        public pub2;
        private priv1;
        private priv2;
        protected prot1;
        protected prot2;

        public public;
        private private;
        protected protected;

        public constructor(public a, private b, protected c, public d, private e, protected f) {
            this.public = 10;
            this.private = 10;
            this.protected = 10;
        }

        public get x() { return 10; }
        public set x(value) { }

        public static statPub;
        private static statPriv;
        protected static statProt;
    }

    export interface I1 {
    }

    export declare namespace ma.m1.m2.m3 {
        interface I2 {
        }
    }

    export namespace mb.m1.m2.m3 {
        declare var foo;

        export class C2 {
            public pub1;
            private priv1;
            [|protected|] prot1;

            [|protected|] constructor(public public, [|protected|] protected, private private) {
                public = private = protected;
            }
        }
    }

    declare var ambientThing: number;
    export var exportedThing = 10;
    declare function foo(): string;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesProtected2, TestGetOccurrencesProtected2);

// getOccurrencesPublic1_test.go

// getOccurrencesPublic1_test.go
static void TestGetOccurrencesPublic1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace m {
    export class C1 {
        [|public|] pub1;
        [|public|] pub2;
        private priv1;
        private priv2;
        protected prot1;
        protected prot2;

        [|public|] public;
        private private;
        protected protected;

        [|public|] constructor([|public|] a, private b, protected c, [|public|] d, private e, protected f) {
            this.public = 10;
            this.private = 10;
            this.protected = 10;
        }

        [|public|] get x() { return 10; }
        [|public|] set x(value) { }

        [|public|] static statPub;
        private static statPriv;
        protected static statProt;
    }

    export interface I1 {
    }

    export declare namespace ma.m1.m2.m3 {
        interface I2 {
        }
    }

    export namespace mb.m1.m2.m3 {
        declare var foo;

        export class C2 {
            public pub1;
            private priv1;
            protected prot1;

            protected constructor(public public, protected protected, private private) {
                public = private = protected;
            }
        }
    }

    declare var ambientThing: number;
    export var exportedThing = 10;
    declare function foo(): string;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesPublic1, TestGetOccurrencesPublic1);

// getOccurrencesPublic2_test.go

// getOccurrencesPublic2_test.go
static void TestGetOccurrencesPublic2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace m {
    export class C1 {
        public pub1;
        public pub2;
        private priv1;
        private priv2;
        protected prot1;
        protected prot2;

        public public;
        private private;
        protected protected;

        public constructor(public a, private b, protected c, public d, private e, protected f) {
            this.public = 10;
            this.private = 10;
            this.protected = 10;
        }

        public get x() { return 10; }
        public set x(value) { }

        public static statPub;
        private static statPriv;
        protected static statProt;
    }

    export interface I1 {
    }

    export declare namespace ma.m1.m2.m3 {
        interface I2 {
        }
    }

    export namespace mb.m1.m2.m3 {
        declare var foo;

        export class C2 {
            [|public|] pub1;
            private priv1;
            protected prot1;

            protected constructor([|public|] public, protected protected, private private) {
                public = private = protected;
            }
        }
    }

    declare var ambientThing: number;
    export var exportedThing = 10;
    declare function foo(): string;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesPublic2, TestGetOccurrencesPublic2);

// getOccurrencesReadonly1_test.go

// getOccurrencesReadonly1_test.go
static void TestGetOccurrencesReadonly1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(interface I {
  [|readonly|] prop: string;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesReadonly1, TestGetOccurrencesReadonly1);

// getOccurrencesReadonly2_test.go

// getOccurrencesReadonly2_test.go
static void TestGetOccurrencesReadonly2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(type T = {
  [|readonly|] prop: string;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesReadonly2, TestGetOccurrencesReadonly2);

// getOccurrencesReadonly3_test.go

// getOccurrencesReadonly3_test.go
static void TestGetOccurrencesReadonly3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class C {
  [|readonly|] prop: /**/readonly string[] = [];
  constructor([|readonly|] prop2: string) {
    class D {
      readonly prop: string = "";  
    }
  }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
		f->VerifyBaselineDocumentHighlights(t, nullptr, {""});
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesReadonly3, TestGetOccurrencesReadonly3);

// getOccurrencesReturn2_test.go

// getOccurrencesReturn2_test.go
static void TestGetOccurrencesReturn2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f(a: number) {
    if (a > 0) {
        return (function () {
            [|return|];
            [|ret/**/urn|];
            [|return|];

            while (false) {
                [|return|] true;
            }
        })() || true;
    }

    var unusued = [1, 2, 3, 4].map(x => { return 4 })

    return;
    return true;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesReturn2, TestGetOccurrencesReturn2);

// getOccurrencesReturn3_test.go

// getOccurrencesReturn3_test.go
static void TestGetOccurrencesReturn3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f(a: number) {
    if (a > 0) {
        return (function () {
            return;
            return;
            return;

            if (false) {
                return true;
            }
        })() || true;
    }

    var unusued = [1, 2, 3, 4].map(x => { [|return|] 4 })

    return;
    return true;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesReturn3, TestGetOccurrencesReturn3);

// getOccurrencesReturn4_test.go

// getOccurrencesReturn4_test.go
static void TestGetOccurrencesReturn4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f(a: number) {
    if (a > 0) {
        return (function () {
            return/*1*/;
            return/*2*/;
            return/*3*/;

            if (false) {
                return/*4*/ true;
            }
        })() || true;
    }

    var unusued = [1, 2, 3, 4].map(x => { return/*5*/ 4 })

    return/*6*/;
    return/*7*/ true;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Markers())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesReturn4, TestGetOccurrencesReturn4);

// getOccurrencesReturnBroken_test.go

// getOccurrencesReturnBroken_test.go
static void TestGetOccurrencesReturnBroken(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(ret/*1*/urn;
retu/*2*/rn;
function f(a: number) {
    if (a > 0) {
        return (function () {
            () => [|return|];
            [|return|];
            [|return|];

            if (false) {
                [|return|] true;
            }
        })() || true;
    }

    var unusued = [1, 2, 3, 4].map(x => { return 4 })

    return;
    return true;
}

class A {
    ret/*3*/urn;
    r/*4*/eturn 8675309;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Markers())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesReturnBroken, TestGetOccurrencesReturnBroken);

// getOccurrencesReturn_test.go

// getOccurrencesReturn_test.go
static void TestGetOccurrencesReturn(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f(a: number) {
    if (a > 0) {
        [|ret/**/urn|] (function () {
            return;
            return;
            return;

            if (false) {
                return true;
            }
        })() || true;
    }

    var unusued = [1, 2, 3, 4].map(x => { return 4 })

    [|return|];
    [|return|] true;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesReturn, TestGetOccurrencesReturn);

// getOccurrencesSetAndGet2_test.go

// getOccurrencesSetAndGet2_test.go
static void TestGetOccurrencesSetAndGet2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
    set bar(b: any) {
    }

    public get bar(): any {
        return undefined;
    }

    public [|set|] set(s: any) {
    }

    public [|get|] set(): any {
        return undefined;
    }

    public set get(g: any) {
    }

    public get get(): any {
        return undefined;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesSetAndGet2, TestGetOccurrencesSetAndGet2);

// getOccurrencesSetAndGet3_test.go

// getOccurrencesSetAndGet3_test.go
static void TestGetOccurrencesSetAndGet3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
    set bar(b: any) {
    }

    public get bar(): any {
        return undefined;
    }

    public set set(s: any) {
    }

    public get set(): any {
        return undefined;
    }

    public [|set|] get(g: any) {
    }

    public [|get|] get(): any {
        return undefined;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesSetAndGet3, TestGetOccurrencesSetAndGet3);

// getOccurrencesSetAndGet_test.go

// getOccurrencesSetAndGet_test.go
static void TestGetOccurrencesSetAndGet(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class Foo {
    [|set|] bar(b: any) {
    }

    public [|get|] bar(): any {
        return undefined;
    }

    public set set(s: any) {
    }

    public get set(): any {
        return undefined;
    }

    public set get(g: any) {
    }

    public get get(): any {
        return undefined;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesSetAndGet, TestGetOccurrencesSetAndGet);

// getOccurrencesStatic1_test.go

// getOccurrencesStatic1_test.go
static void TestGetOccurrencesStatic1(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(namespace m {
    export class C1 {
        public pub1;
        public pub2;
        private priv1;
        private priv2;
        protected prot1;
        protected prot2;

        public public;
        private private;
        protected protected;

        public constructor(public a, private b, protected c, public d, private e, protected f) {
            this.public = 10;
            this.private = 10;
            this.protected = 10;
        }

        public get x() { return 10; }
        public set x(value) { }

        public [|static|] statPub;
        private [|static|] statPriv;
        protected [|static|] statProt;
    }

    export interface I1 {
    }

    export declare namespace ma.m1.m2.m3 {
        interface I2 {
        }
    }

    export namespace mb.m1.m2.m3 {
        declare var foo;

        export class C2 {
            public pub1;
            private priv1;
            protected prot1;

            protected constructor(public public, protected protected, private private) {
                public = private = protected;
            }
        }
    }

    declare var ambientThing: number;
    export var exportedThing = 10;
    declare function foo(): string;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesStatic1, TestGetOccurrencesStatic1);

// getOccurrencesStringLiteralTypes_test.go

// getOccurrencesStringLiteralTypes_test.go
static void TestGetOccurrencesStringLiteralTypes(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function foo(a: "[|option 1|]") { }
foo("[|option 1|]");)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesStringLiteralTypes, TestGetOccurrencesStringLiteralTypes);

// getOccurrencesStringLiterals_test.go

// getOccurrencesStringLiterals_test.go
static void TestGetOccurrencesStringLiterals(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(var x = "[|string|]";
function f(a = "[|initial value|]") { })TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesStringLiterals, TestGetOccurrencesStringLiterals);

// getOccurrencesSuper2_test.go

// getOccurrencesSuper2_test.go
static void TestGetOccurrencesSuper2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class SuperType {
    superMethod() {
    }

    static superStaticMethod() {
        return 10;
    }
}

class SubType extends SuperType {
    public  prop1 = super.superMethod;
    private prop2 = super.superMethod;

    constructor() {
        super();
    }

    public method1() {
        return super.superMethod();
    }

    private method2() {
        return super.superMethod();
    }

    public method3() {
        var x = () => super.superMethod();

        // Bad but still gets highlighted
        function f() {
            super.superMethod();
        }
    }

    // Bad but still gets highlighted.
    public static statProp1 = [|super|].superStaticMethod;

    public static staticMethod1() {
        return [|super|].superStaticMethod();
    }

    private static staticMethod2() {
        return [|supe/**/r|].superStaticMethod();
    }

    // Are not actually 'super' keywords.
    super = 10;
    static super = 20;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesSuper2, TestGetOccurrencesSuper2);

// getOccurrencesSuper3_test.go

// getOccurrencesSuper3_test.go
static void TestGetOccurrencesSuper3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(let x = {
    a() {
        return [|s/**/uper|].b();
    },
    b() {
        return [|super|].a();
    },
    c: function () {
        return [|super|].a();
    }
    d: () => [|super|].b();
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesSuper3, TestGetOccurrencesSuper3);

// getOccurrencesSuperNegatives_test.go

// getOccurrencesSuperNegatives_test.go
static void TestGetOccurrencesSuperNegatives(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f(x = [|super|]) {
    [|super|];
}

namespace M {
    [|super|];
    function f(x = [|super|]) {
    [|super|];
    }

    class A {
    }

    class B extends A {
        constructor() {
            super();
        }
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesSuperNegatives, TestGetOccurrencesSuperNegatives);

// getOccurrencesSuper_test.go

// getOccurrencesSuper_test.go
static void TestGetOccurrencesSuper(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(class SuperType {
    superMethod() {
    }

    static superStaticMethod() {
        return 10;
    }
}

class SubType extends SuperType {
    public  prop1 = [|s/**/uper|].superMethod;
    private prop2 = [|super|].superMethod;

    constructor() {
        [|super|]();
    }

    public method1() {
        return [|super|].superMethod();
    }

    private method2() {
        return [|super|].superMethod();
    }

    public method3() {
        var x = () => [|super|].superMethod();

        // Bad but still gets highlighted
        function f() {
            [|super|].superMethod();
        }
    }

    // Bad but still gets highlighted.
    public static statProp1 = super.superStaticMethod;

    public static staticMethod1() {
        return super.superStaticMethod();
    }

    private static staticMethod2() {
        return super.superStaticMethod();
    }

    // Are not actually 'super' keywords.
    super = 10;
    static super = 20;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesSuper, TestGetOccurrencesSuper);

// getOccurrencesSwitchCaseDefault2_test.go

// getOccurrencesSwitchCaseDefault2_test.go
static void TestGetOccurrencesSwitchCaseDefault2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(switch (10) {
    case 1:
    case 2:
    case 4:
    case 8:
        foo: [|switch|] (20) {
            [|case|] 1:
            [|case|] 2:
                [|break|];
            [|default|]:
                [|break|] foo;
        }
    case 0xBEEF:
    default:
        break;
    case 16:
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesSwitchCaseDefault2, TestGetOccurrencesSwitchCaseDefault2);

// getOccurrencesSwitchCaseDefault3_test.go

// getOccurrencesSwitchCaseDefault3_test.go
static void TestGetOccurrencesSwitchCaseDefault3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(foo: [|switch|] (1) {
    [|case|] 1:
    [|case|] 2:
        [|break|];
    [|case|] 3:
        switch (2) {
            case 1:
                [|break|] foo;
                continue; // invalid
            default:
                break;
        }
    [|default|]:
        [|break|];
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesSwitchCaseDefault3, TestGetOccurrencesSwitchCaseDefault3);

// getOccurrencesSwitchCaseDefault4_test.go

// getOccurrencesSwitchCaseDefault4_test.go
static void TestGetOccurrencesSwitchCaseDefault4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(foo: [|switch|] (10) {
    [|case|] 1:
    [|case|] 2:
    [|case|] 3:
        [|break|];
        [|break|] foo;
        co/*1*/ntinue;
        contin/*2*/ue foo;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Markers())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesSwitchCaseDefault4, TestGetOccurrencesSwitchCaseDefault4);

// getOccurrencesSwitchCaseDefault5_test.go

// getOccurrencesSwitchCaseDefault5_test.go
static void TestGetOccurrencesSwitchCaseDefault5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(switch/*1*/ (10) {
    case/*2*/ 1:
    case/*3*/ 2:
    case/*4*/ 4:
    case/*5*/ 8:
        foo: switch/*6*/ (20) {
            case/*7*/ 1:
            case/*8*/ 2:
                break/*9*/;
            default/*10*/:
                break foo;
        }
    case/*11*/ 0xBEEF:
    default/*12*/:
        break/*13*/;
    case 16/*14*/:
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Markers())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesSwitchCaseDefault5, TestGetOccurrencesSwitchCaseDefault5);

// getOccurrencesSwitchCaseDefaultBroken_test.go

// getOccurrencesSwitchCaseDefaultBroken_test.go
static void TestGetOccurrencesSwitchCaseDefaultBroken(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(swi/*1*/tch(10) {
    case 1:
    case 2:
    c/*2*/ase 4:
    case 8:
    case 0xBEEF:
    de/*4*/fult:
        break;
    /*5*/cas 16:
    c/*3*/ase 12:
        function f() {
            br/*11*/eak;
            /*12*/break;
        }
}

sw/*6*/itch (10) {
    de/*7*/fault
    case 1:
    case 2

    c/*8*/ose 4:
    case 8:
    case 0xBEEF:
        bre/*9*/ak;
    case 16:
        () => bre/*10*/ak;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Markers())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesSwitchCaseDefaultBroken, TestGetOccurrencesSwitchCaseDefaultBroken);

// getOccurrencesSwitchCaseDefault_test.go

// getOccurrencesSwitchCaseDefault_test.go
static void TestGetOccurrencesSwitchCaseDefault(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|switch|] (10) {
    [|case|] 1:
    [|case|] 2:
    [|case|] 4:
    [|case|] 8:
        foo: switch (20) {
            case 1:
            case 2:
                break;
            default:
                break foo;
        }
    [|case|] 0xBEEF:
    [|default|]:
        [|break|];
    [|case|] 16:
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesSwitchCaseDefault, TestGetOccurrencesSwitchCaseDefault);

// getOccurrencesThis2_test.go

// getOccurrencesThis2_test.go
static void TestGetOccurrencesThis2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(this;
this;

function f() {
    [|this|];
    [|this|];
    () => [|this|];
    () => {
        if ([|this|]) {
            [|this|];
        }
        else {
            [|t/**/his|].this;
        }
    }
    function inside() {
        this;
        (function (_) {
            this;
        })(this);
    }
}

namespace m {
    function f() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }
}

class A {
    public b = this.method1;

    public method1() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }

    private method2() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }

    public static staticB = this.staticMethod1;

    public static staticMethod1() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }

    private static staticMethod2() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }
}

var x = {
    f() {
        this;
    },
    g() {
        this;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesThis2, TestGetOccurrencesThis2);

// getOccurrencesThis3_test.go

// getOccurrencesThis3_test.go
static void TestGetOccurrencesThis3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(this;
this;

function f() {
    this;
    this;
    () => this;
    () => {
        if (this) {
            this;
        }
        else {
            this.this;
        }
    }
    function inside() {
        [|t/**/his|];
        (function (_) {
            this;
        })([|this|]);
    }
}

namespace m {
    function f() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }
}

class A {
    public b = this.method1;

    public method1() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }

    private method2() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }

    public static staticB = this.staticMethod1;

    public static staticMethod1() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }

    private static staticMethod2() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }
}

var x = {
    f() {
        this;
    },
    g() {
        this;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesThis3, TestGetOccurrencesThis3);

// getOccurrencesThis4_test.go

// getOccurrencesThis4_test.go
static void TestGetOccurrencesThis4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(this;
this;

function f() {
    this;
    this;
    () => this;
    () => {
        if (this) {
            this;
        }
        else {
            this.this;
        }
    }
    function inside() {
        this;
        (function (_) {
            this;
        })(this);
    }
}

namespace m {
    function f() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }
}

class A {
    public b = [|this|].method1;

    public method1() {
        [|this|];
        [|this|];
        () => [|this|];
        () => {
            if ([|this|]) {
                [|this|];
            }
            else {
                [|this|].this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }

    private method2() {
        [|this|];
        [|this|];
        () => [|t/**/his|];
        () => {
            if ([|this|]) {
                [|this|];
            }
            else {
                [|this|].this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }

    public static staticB = this.staticMethod1;

    public static staticMethod1() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }

    private static staticMethod2() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }
}

var x = {
    f() {
        this;
    },
    g() {
        this;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesThis4, TestGetOccurrencesThis4);

// getOccurrencesThis5_test.go

// getOccurrencesThis5_test.go
static void TestGetOccurrencesThis5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(this;
this;

function f() {
    this;
    this;
    () => this;
    () => {
        if (this) {
            this;
        }
        else {
            this.this;
        }
    }
    function inside() {
        this;
        (function (_) {
            this;
        })(this);
    }
}

namespace m {
    function f() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }
}

class A {
    public b = this.method1;

    public method1() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }

    private method2() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }

    public static staticB = [|this|].staticMethod1;

    public static staticMethod1() {
        [|this|];
        [|this|];
        () => [|this|];
        () => {
            if ([|this|]) {
                [|this|];
            }
            else {
                [|this|].this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }

    private static staticMethod2() {
        [|this|];
        [|this|];
        () => [|this|];
        () => {
            if ([|this|]) {
                [|this|];
            }
            else {
                [|t/**/his|].this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }
}

var x = {
    f() {
        this;
    },
    g() {
        this;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesThis5, TestGetOccurrencesThis5);

// getOccurrencesThis6_test.go

// getOccurrencesThis6_test.go
static void TestGetOccurrencesThis6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(this/*1*/;
this;

function f() {
    this/*2*/;
    this;
    () => this;
    () => {
        if (this) {
            this;
        }
        else {
            this.this;
        }
    }
    function inside() {
        this;
        (function (_) {
            this;
        })(this);
    }
}

namespace m {
    var x = th/*6*/is;
    function f() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }
}

class A {
    public b = this.method1;

    public method1() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this/*3*/;
            })(this);
        }
    }

    private method2() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }

    public static staticB = this.staticMethod1;

    public static staticMethod1() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }

    private static staticMethod2() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }
}

var x = {
    a: /*4*/this,

    f() {
        this/*5*/;
        function foo() {
            this;
        }
        const bar = () => {
            this;
        }
    },

    g() {
        this;
    },

    get h() {
        /*7*/this;
        function foo() {
            this;
        }
        const bar = () => {
            this;
        }
        return;
    },

    set h(foo: any) {
        this;
    },

    l: () => {
        /*8*/this;
        function foo() {
            this;
        }
        const bar = () => {
            this;
        }
    },
};
)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Markers())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesThis6, TestGetOccurrencesThis6);

// getOccurrencesThisNegatives2_test.go

// getOccurrencesThisNegatives2_test.go
static void TestGetOccurrencesThisNegatives2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(this;
this;

function f() {
    this;
    this;
    () => this;
    () => {
        if (this) {
            this;
        }
        else {
            this.t/*1*/his;
        }
    }
    function inside() {
        this;
        (function (_) {
            this;
        })(this);
    }
}

namespace m {
    function f() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this./*2*/this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }
}

class A {
    public b = this.method1;

    public method1() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.thi/*3*/s;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }

    private method2() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.t/*4*/his;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }

    public static staticB = this.staticMethod1;

    public static staticMethod1() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.th/*5*/is;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }

    private static staticMethod2() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.th/*6*/is;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }
}

var x = {
    f() {
        this;
    },
    g() {
        this;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Markers())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesThisNegatives2, TestGetOccurrencesThisNegatives2);

// getOccurrencesThis_test.go

// getOccurrencesThis_test.go
static void TestGetOccurrencesThis(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|this|];
[|th/**/is|];

function f() {
    this;
    this;
    () => this;
    () => {
        if (this) {
            this;
        }
        else {
            this.this;
        }
    }
    function inside() {
        this;
        (function (_) {
            this;
        })(this);
    }
}

namespace m {
    function f() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }
}

class A {
    public b = this.method1;

    public method1() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }

    private method2() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }

    public static staticB = this.staticMethod1;

    public static staticMethod1() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }

    private static staticMethod2() {
        this;
        this;
        () => this;
        () => {
            if (this) {
                this;
            }
            else {
                this.this;
            }
        }
        function inside() {
            this;
            (function (_) {
                this;
            })(this);
        }
    }
}

var x = {
    f() {
        this;
    },
    g() {
        this;
    }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesThis, TestGetOccurrencesThis);

// getOccurrencesThrow2_test.go

// getOccurrencesThrow2_test.go
static void TestGetOccurrencesThrow2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f(a: number) {
    try {
        throw "Hello";

        try {
            [|t/**/hrow|] 10;
        }
        catch (x) {
            return 100;
        }
        finally {
            throw 10;
        }
    }
    catch (x) {
        throw "Something";
    }
    finally {
        throw "Also something";
    }
    if (a > 0) {
        return (function () {
            return;
            return;
            return;

            if (false) {
                return true;
            }
            throw "Hello!";
        })() || true;
    }

    throw 10;

    var unusued = [1, 2, 3, 4].map(x => { throw 4 })

    return;
    return true;
    throw false;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesThrow2, TestGetOccurrencesThrow2);

// getOccurrencesThrow3_test.go

// getOccurrencesThrow3_test.go
static void TestGetOccurrencesThrow3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f(a: number) {
    try {
        [|throw|] "Hello";

        try {
            throw 10;
        }
        catch (x) {
            return 100;
        }
        finally {
            [|thr/**/ow|] 10;
        }
    }
    catch (x) {
        throw "Something";
    }
    finally {
        throw "Also something";
    }
    if (a > 0) {
        return (function () {
            return;
            return;
            return;

            if (false) {
                return true;
            }
            throw "Hello!";
        })() || true;
    }

    throw 10;

    var unusued = [1, 2, 3, 4].map(x => { throw 4 })

    return;
    return true;
    throw false;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesThrow3, TestGetOccurrencesThrow3);

// getOccurrencesThrow4_test.go

// getOccurrencesThrow4_test.go
static void TestGetOccurrencesThrow4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f(a: number) {
    try {
        throw "Hello";

        try {
            throw 10;
        }
        catch (x) {
            return 100;
        }
        finally {
            throw 10;
        }
    }
    catch (x) {
        throw "Something";
    }
    finally {
        throw "Also something";
    }
    if (a > 0) {
        return (function () {
            [|return|];
            [|return|];
            [|return|];

            if (false) {
                [|return|] true;
            }
            [|th/**/row|] "Hello!";
        })() || true;
    }

    throw 10;

    var unusued = [1, 2, 3, 4].map(x => { throw 4 })

    return;
    return true;
    throw false;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesThrow4, TestGetOccurrencesThrow4);

// getOccurrencesThrow5_test.go

// getOccurrencesThrow5_test.go
static void TestGetOccurrencesThrow5(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f(a: number) {
    try {
        throw "Hello";

        try {
            throw 10;
        }
        catch (x) {
            return 100;
        }
        finally {
            throw 10;
        }
    }
    catch (x) {
        throw "Something";
    }
    finally {
        throw "Also something";
    }
    if (a > 0) {
        return (function () {
            return;
            return;
            return;

            if (false) {
                return true;
            }
            throw "Hello!";
        })() || true;
    }

    throw 10;

    var unusued = [1, 2, 3, 4].map(x => { [|thr/**/ow|] 4 })

    return;
    return true;
    throw false;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesThrow5, TestGetOccurrencesThrow5);

// getOccurrencesThrow6_test.go

// getOccurrencesThrow6_test.go
static void TestGetOccurrencesThrow6(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS([|throw|] 100;

try {
    throw 0;
    var x = () => { throw 0; };
}
catch (y) {
    var x = () => { throw 0; };
    [|throw|] 200;
}
finally {
    [|throw|] 300;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesThrow6, TestGetOccurrencesThrow6);

// getOccurrencesThrow7_test.go

// getOccurrencesThrow7_test.go
static void TestGetOccurrencesThrow7(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(try {
    [|throw|] 10;

    try {
        throw 10;
    }
    catch (x) {
        [|throw|] 10;
    }
    finally {
        [|throw|] 10;
    }
}
finally {
    [|throw|] 10;
}

[|throw|] 10;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesThrow7, TestGetOccurrencesThrow7);

// getOccurrencesThrow8_test.go

// getOccurrencesThrow8_test.go
static void TestGetOccurrencesThrow8(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(try {
    throw 10;

    try {
        [|throw|] 10;
    }
    catch (x) {
        throw 10;
    }
    finally {
        throw 10;
    }
}
finally {
    throw 10;
}

throw 10;)TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesThrow8, TestGetOccurrencesThrow8);

// getOccurrencesThrow_test.go

// getOccurrencesThrow_test.go
static void TestGetOccurrencesThrow(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function f(a: number) {
    try {
        throw "Hello";

        try {
            throw 10;
        }
        catch (x) {
            [|return|] 100;
        }
        finally {
            throw 10;
        }
    }
    catch (x) {
        [|throw|] "Something";
    }
    finally {
        [|throw|] "Also something";
    }
    if (a > 0) {
        [|return|] (function () {
            return;
            return;
            return;

            if (false) {
                return true;
            }
            throw "Hello!";
        })() || true;
    }

    [|th/**/row|] 10;

    var unusued = [1, 2, 3, 4].map(x => { throw 4 })

    [|return|];
    [|return|] true;
    [|throw|] false;
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesThrow, TestGetOccurrencesThrow);

// getOccurrencesTryCatchFinally2_test.go

// getOccurrencesTryCatchFinally2_test.go
static void TestGetOccurrencesTryCatchFinally2(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(try {
    [|t/*1*/r/*2*/y|] {
    }
    [|c/*3*/atch|] (x) {
    }

    try {
    }
    finally {
    }
}
catch (e) {
}
finally {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Markers())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesTryCatchFinally2, TestGetOccurrencesTryCatchFinally2);

// getOccurrencesTryCatchFinally3_test.go

// getOccurrencesTryCatchFinally3_test.go
static void TestGetOccurrencesTryCatchFinally3(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(try {
    try {
    }
    catch (x) {
    }

    [|t/*1*/r/*2*/y|] {
    }
    [|finall/*3*/y|] {
    }
}
catch (e) {
}
finally {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Markers())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesTryCatchFinally3, TestGetOccurrencesTryCatchFinally3);

// getOccurrencesTryCatchFinally4_test.go

// getOccurrencesTryCatchFinally4_test.go
static void TestGetOccurrencesTryCatchFinally4(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(try/*1*/ {
    try/*2*/ {
    }
    catch/*3*/ (x) {
    }

    try/*4*/ {
    }
    finally/*5*/ {/*8*/
    }
}
catch/*6*/ (e) {
}
finally/*7*/ {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Markers())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesTryCatchFinally4, TestGetOccurrencesTryCatchFinally4);

// getOccurrencesTryCatchFinallyBroken_test.go

// getOccurrencesTryCatchFinallyBroken_test.go
static void TestGetOccurrencesTryCatchFinallyBroken(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(t /*1*/ry {
    t/*2*/ry {
    }
    ctch (x) {
    }

    tr {
    }
    fin/*3*/ally {
    }
}
c/*4*/atch (e) {
}
f/*5*/inally {
}

// Missing catch variable
t/*6*/ry {
}
catc/*7*/h {
}
/*8*/finally {
}

// Missing try entirely
cat/*9*/ch (x) {
}
final/*10*/ly {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Markers())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesTryCatchFinallyBroken, TestGetOccurrencesTryCatchFinallyBroken);

// getOccurrencesTryCatchFinally_test.go

// getOccurrencesTryCatchFinally_test.go
static void TestGetOccurrencesTryCatchFinally(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(/*1*/[|try|] {
    try {
    }
    catch (x) {
    }

    try {
    }
    finally {
    }
}
[|cat/*2*/ch|] (e) {
}
[|fina/*3*/lly|] {
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Markers())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesTryCatchFinally, TestGetOccurrencesTryCatchFinally);

// getOccurrencesYield_test.go

// getOccurrencesYield_test.go
static void TestGetOccurrencesYield(gostd::testing::T* t) {
	tsc::testutil::withRecoverAndFail(t, "Panic on fourslash test", [&] {
		t->Parallel();
		const std::string content = R"TS(function* f() {
 [|yield|] 100;
 [|y/**/ield|] [|yield|] 200;
  class Foo {
      *memberFunction() {
          return yield 1;
      }
  }
  return function* g() {
    yield 1;
  }
})TS";
		auto __fsp = fourslash::NewFourslash(t, nullptr, content); auto f = __fsp.first; auto done = __fsp.second; TSC_DEFER(done());
		f->VerifyBaselineDocumentHighlights(t, nullptr, asMonVec(tsu::ToAny(f->Ranges())));
	});
}
REGISTER_FOURSLASH_TEST(TestGetOccurrencesYield, TestGetOccurrencesYield);

} // namespace
